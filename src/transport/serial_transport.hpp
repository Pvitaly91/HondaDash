#pragma once

#include "application/transport.hpp"
#include <QIODevice>
#include <QObject>
#include <array>
#include <functional>
#include <memory>

namespace hd {

// All methods and the supplied clock run on this QObject's owning thread.
// Qt 6.8 has no bounded write-buffer API: pump() explicitly limits its queue.
class SerialTransport final : public QObject, public Transport {
public:
    using Clock = std::function<Time()>;
    // Test seam for a sequential device. Production uses a fresh QSerialPort.
    using DeviceFactory = std::function<std::unique_ptr<QIODevice>()>;
    static constexpr std::size_t TxCapacity = 256;
    static constexpr std::size_t QtTxCapacity = 128;
    static constexpr std::size_t RxCapacity = 4096;
    static constexpr std::size_t RxCallbackBudget = 1024;
    static constexpr Time TxTimeoutMs = 500;

    explicit SerialTransport(Clock clock, QObject* parent = nullptr);
    SerialTransport(Clock clock, DeviceFactory testDevice, QObject* parent = nullptr);
    ~SerialTransport() override;
    void setPortName(std::string name);
    const std::string& portName() const { return portName_; }
    const std::string& controlLineNotice() const { return controlLineNotice_; }
    void start(TransportCallbacks callbacks, Time now) override;
    void close() override;
    bool send(std::span<const std::uint8_t> bytes, Time now) override;
    void tick(Time now) override;
    bool isOpen() const override;
    std::size_t pendingBytes() const override;
    std::string name() const override { return "serial"; }

private:
    void assertThread() const;
    Time now() const;
    void fail(std::string message, Time time);
    void schedulePump(int delayMs);
    void pump(Time time);
    void scheduleRead(int delayMs = 0);
    void readReady();

    Clock clock_;
    DeviceFactory testDevice_;
    std::unique_ptr<QIODevice> device_;
    TransportCallbacks callbacks_;
    std::string portName_, controlLineNotice_;
    std::array<std::uint8_t, TxCapacity> tx_{};
    std::size_t txSize_{}, txOffset_{};
    Time txStarted_{}, lastTime_{};
    std::uint64_t generation_{};
    bool activeTx_{}, pumpScheduled_{}, readScheduled_{};
};

} // namespace hd
