#include "transport/serial_transport.hpp"
#include <QSerialPort>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace hd {

SerialTransport::SerialTransport(Clock clock, QObject* parent)
    : SerialTransport(std::move(clock), DeviceFactory{}, parent) {}

SerialTransport::SerialTransport(Clock clock, DeviceFactory testDevice, QObject* parent)
    : QObject(parent), clock_(std::move(clock)), testDevice_(std::move(testDevice)) {}

SerialTransport::~SerialTransport() { close(); }

void SerialTransport::assertThread() const {
    Q_ASSERT_X(QThread::currentThread() == thread(), "SerialTransport", "Use the QObject owning thread");
}

Time SerialTransport::now() const { return clock_ ? clock_() : lastTime_; }

void SerialTransport::setPortName(std::string name) {
    assertThread();
    // Selecting a different device never silently reopens an active connection.
    if (isOpen()) {
        fail("Cannot change the serial port while connected", now());
        return;
    }
    portName_ = std::move(name);
}

void SerialTransport::start(TransportCallbacks callbacks, Time time) {
    assertThread();
    close();
    callbacks_ = std::move(callbacks);
    lastTime_ = time;
    controlLineNotice_.clear();
    const auto generation = generation_;
    if (!testDevice_ && portName_.empty()) {
        fail("Select a serial port before connecting", time);
        return;
    }
    device_ = testDevice_ ? testDevice_() : std::make_unique<QSerialPort>();
    if (!device_) {
        fail("Serial device creation failed", time);
        return;
    }
    device_->setParent(this);
    auto* serial = qobject_cast<QSerialPort*>(device_.get());
    if (serial) {
        serial->setPortName(QString::fromUtf8(portName_.data(), static_cast<qsizetype>(portName_.size())));
        serial->setReadBufferSize(RxCapacity);
    }
    if (!device_->open(QIODevice::ReadWrite)) {
        fail("Cannot open serial port: " + device_->errorString().toStdString(), time);
        return;
    }
    if (serial) {
        // Check every setter after open, when the OS must apply it.
        if (!serial->setBaudRate(115200) || !serial->setDataBits(QSerialPort::Data8)
            || !serial->setParity(QSerialPort::NoParity) || !serial->setStopBits(QSerialPort::OneStop)
            || !serial->setFlowControl(QSerialPort::NoFlowControl)) {
            fail("Cannot configure serial port (115200 8N1): " + serial->errorString().toStdString(), time);
            return;
        }
        // Exactly one DTR assertion and RTS deassertion per open, never per HELLO.
        // PTYs have no modem lines; their unsupported ioctl is not a baud error.
        const auto setLine = [this, serial, time](bool dtr) {
            const bool ok = dtr ? serial->setDataTerminalReady(true) : serial->setRequestToSend(false);
            if (ok) return true;
            const auto error = serial->error();
            if (error == QSerialPort::UnsupportedOperationError || error == QSerialPort::UnknownError) {
                if (!controlLineNotice_.empty()) controlLineNotice_ += "; ";
                controlLineNotice_ += dtr ? "DTR unsupported by this endpoint" : "RTS unsupported by this endpoint";
                serial->clearError();
                return true;
            }
            fail("Cannot set serial control line: " + serial->errorString().toStdString(), time);
            return false;
        };
        if (!setLine(true) || !setLine(false)) return;
        connect(serial, &QSerialPort::errorOccurred, this, [this, generation](QSerialPort::SerialPortError error) {
            if (generation != generation_ || error == QSerialPort::NoError) return;
            fail("Serial I/O error: " + device_->errorString().toStdString(), now());
        });
    }
    connect(device_.get(), &QIODevice::readyRead, this, [this, generation] {
        if (generation == generation_) readReady();
    });
    connect(device_.get(), &QIODevice::bytesWritten, this, [this, generation](qint64) {
        if (generation == generation_) schedulePump(0);
    });
    connect(device_.get(), &QIODevice::aboutToClose, this, [this, generation] {
        if (generation == generation_) fail("Serial device disconnected", now());
    });
    if (!controlLineNotice_.empty()) {
        const auto raw = callbacks_.raw;
        if (raw) raw("control_line_notice:" + controlLineNotice_, {}, time);
        if (generation != generation_) return;
    }
    auto opened = callbacks_.opened;
    if (opened) opened(time);
    if (generation == generation_ && isOpen() && device_->bytesAvailable() > 0) scheduleRead();
}

void SerialTransport::close() {
    assertThread();
    ++generation_;
    callbacks_ = {};
    activeTx_ = pumpScheduled_ = readScheduled_ = false;
    txSize_ = txOffset_ = 0;
    if (device_) {
        disconnect(device_.get(), nullptr, this, nullptr);
        device_->close();
        // close() can run from errorOccurred/readyRead: never delete the sender
        // on its signal stack. QObject ownership also covers application shutdown.
        device_.release()->deleteLater();
    }
}

bool SerialTransport::isOpen() const {
    assertThread();
    return device_ && device_->isOpen();
}

std::size_t SerialTransport::pendingBytes() const {
    assertThread();
    const auto queued = device_ ? std::max<qint64>(0, device_->bytesToWrite()) : 0;
    return txSize_ - txOffset_ + static_cast<std::size_t>(queued);
}

void SerialTransport::fail(std::string message, Time time) {
    auto error = callbacks_.error;
    close();
    if (error) error(message, time);
}

bool SerialTransport::send(std::span<const std::uint8_t> bytes, Time time) {
    assertThread();
    if (!isOpen()) {
        fail("Serial port is not open", time);
        return false;
    }
    if (bytes.empty() || bytes.size() > TxCapacity || activeTx_ || device_->bytesToWrite() != 0) {
        fail("Serial TX queue overflow or overlapping request", time);
        return false;
    }
    std::copy(bytes.begin(), bytes.end(), tx_.begin());
    txSize_ = bytes.size();
    txOffset_ = 0;
    txStarted_ = time;
    lastTime_ = time;
    activeTx_ = true;
    // A scheduled pump gives the caller time to install its pending request.
    schedulePump(0);
    return true;
}

void SerialTransport::tick(Time time) {
    assertThread();
    lastTime_ = time;
    pump(time);
}

void SerialTransport::schedulePump(int delayMs) {
    if (!activeTx_ || pumpScheduled_ || !isOpen()) return;
    pumpScheduled_ = true;
    const auto generation = generation_;
    QTimer::singleShot(delayMs, this, [this, generation] {
        if (generation != generation_) return;
        pumpScheduled_ = false;
        pump(now());
    });
}

void SerialTransport::pump(Time time) {
    if (!activeTx_ || !isOpen()) return;
    const auto generation = generation_;
    if (time >= txStarted_ && time - txStarted_ >= TxTimeoutMs) {
        fail("Serial TX deadline exceeded", time);
        return;
    }
    const qint64 queued = device_->bytesToWrite();
    if (queued < 0 || queued > static_cast<qint64>(QtTxCapacity)) {
        fail("Serial Qt TX buffer exceeded its bound", time);
        return;
    }
    if (txOffset_ < txSize_ && queued < static_cast<qint64>(QtTxCapacity)) {
        const auto count = std::min(txSize_ - txOffset_, QtTxCapacity - static_cast<std::size_t>(queued));
        const auto start = txOffset_;
        const auto accepted = device_->write(reinterpret_cast<const char*>(tx_.data() + start), static_cast<qint64>(count));
        if (generation != generation_) return; // write can synchronously signal a serial error
        if (accepted < 0 || accepted > static_cast<qint64>(count)) {
            fail("Serial write failed: " + device_->errorString().toStdString(), time);
            return;
        }
        txOffset_ += static_cast<std::size_t>(accepted);
        if (accepted > 0) {
            const auto raw = callbacks_.raw;
            if (raw) raw("TX", std::span<const std::uint8_t>(tx_.data() + start, static_cast<std::size_t>(accepted)), time);
            if (generation != generation_) return;
        }
    }
    if (device_->bytesToWrite() > static_cast<qint64>(QtTxCapacity)) {
        fail("Serial Qt TX buffer exceeded its bound", time);
        return;
    }
    if (txOffset_ == txSize_ && device_->bytesToWrite() == 0) {
        activeTx_ = false;
        txOffset_ = txSize_ = 0;
        const auto sent = callbacks_.sent;
        if (sent) sent(time);
        return;
    }
    // Includes zero writes and stalled OS buffers; no spinning, bounded work.
    schedulePump(5);
}

void SerialTransport::scheduleRead(int delayMs) {
    if (readScheduled_ || !isOpen()) return;
    readScheduled_ = true;
    const auto generation = generation_;
    QTimer::singleShot(delayMs, this, [this, generation] {
        if (generation != generation_) return;
        readScheduled_ = false;
        readReady();
    });
}

void SerialTransport::readReady() {
    if (!isOpen()) return;
    const auto generation = generation_;
    // A fast endpoint can become readable before the bytesWritten continuation.
    // Observe drained queues now so its response uses the response deadline,
    // never the original TX deadline, before delivering even the first byte.
    if (activeTx_ && txOffset_ == txSize_ && device_->bytesToWrite() == 0) pump(now());
    if (generation != generation_) return;
    std::array<std::uint8_t, 256> bytes{};
    std::size_t budget = RxCallbackBudget;
    int continuationDelay = 0;
    while (budget > 0 && device_->bytesAvailable() > 0) {
        const auto size = device_->read(reinterpret_cast<char*>(bytes.data()), static_cast<qint64>(std::min(bytes.size(), budget)));
        if (generation != generation_) return;
        if (size < 0) {
            fail("Serial read failed: " + device_->errorString().toStdString(), now());
            return;
        }
        if (size == 0) { continuationDelay = 5; break; }
        const auto time = now();
        const auto fragment = std::span<const std::uint8_t>(bytes.data(), static_cast<std::size_t>(size));
        const auto raw = callbacks_.raw;
        if (raw) raw("RX", fragment, time);
        if (generation != generation_) return;
        const auto received = callbacks_.received;
        if (received) received(fragment, time);
        if (generation != generation_) return;
        budget -= static_cast<std::size_t>(size);
    }
    if (device_->bytesAvailable() > 0) scheduleRead(continuationDelay);
}

} // namespace hd
