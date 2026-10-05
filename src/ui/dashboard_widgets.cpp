#include "ui/dashboard_widgets.hpp"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace hd::ui {
namespace {
const QColor background(21, 29, 41), border(45, 61, 78), foreground(232, 240, 248);
const QColor muted(145, 165, 186), accent(81, 211, 186), amber(247, 182, 74);
QColor readingColor(const Measurement& reading) {
    return reading.quality == Quality::Valid ? foreground :
        reading.quality == Quality::Stale ? amber : muted;
}
void card(QPainter& painter, const QRectF& rect) {
    painter.setPen(QPen(border, 1));
    painter.setBrush(background);
    painter.drawRoundedRect(rect.adjusted(1, 1, -1, -1), 12, 12);
}
QFont painterFont(double size, bool bold = false) {
    QFont result(QStringLiteral("Segoe UI"));
    result.setPointSizeF(size);
    result.setBold(bold);
    return result;
}
}
QString channelName(Channel channel) {
    switch (channel) {
    case Channel::Rpm: return QStringLiteral("ОБЕРТИ ДВИГУНА");
    case Channel::Speed: return QStringLiteral("ШВИДКІСТЬ");
    case Channel::Coolant: return QStringLiteral("ОХОЛОДЖУВАЛЬНА РІДИНА");
    case Channel::Intake: return QStringLiteral("ТЕМПЕРАТУРА ВПУСКУ");
    case Channel::Throttle: return QStringLiteral("ДРОСЕЛЬ");
    case Channel::Map: return QStringLiteral("ТИСК У ВПУСКУ");
    case Channel::Voltage: return QStringLiteral("НАПРУГА");
    }
    return {};
}
QString channelUnit(Channel channel) {
    switch (channel) {
    case Channel::Rpm: return QStringLiteral("об/хв");
    case Channel::Speed: return QStringLiteral("км/год");
    case Channel::Coolant: case Channel::Intake: return QStringLiteral("°C");
    case Channel::Throttle: return QStringLiteral("%");
    case Channel::Map: return QStringLiteral("кПа");
    case Channel::Voltage: return QStringLiteral("В");
    }
    return {};
}
QString qualityName(Quality quality) {
    switch (quality) {
    case Quality::NoData: return QStringLiteral("НЕМАЄ ДАНИХ");
    case Quality::Valid: return QStringLiteral("КОРЕКТНО");
    case Quality::Stale: return QStringLiteral("ЗАСТАРІЛІ ДАНІ");
    case Quality::Unsupported: return QStringLiteral("НЕ ПІДТРИМУЄТЬСЯ");
    case Quality::Invalid: return QStringLiteral("НЕКОРЕКТНІ ДАНІ");
    }
    return {};
}
QString readingText(const Measurement& reading, Channel channel) {
    if (!reading.value) return QStringLiteral("—");
    const int precision = channel == Channel::Voltage ? 2 :
        channel == Channel::Map || channel == Channel::Throttle || channel == Channel::Coolant || channel == Channel::Intake ? 1 : 0;
    return QString::number(*reading.value, 'f', precision);
}
ChannelCard::ChannelCard(Channel channel, QWidget* parent) : QWidget(parent), channel_(channel) {
    setMinimumSize(channel == Channel::Speed ? QSize(180, 90) : QSize(110, 64));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
void ChannelCard::setReading(const Measurement& reading) { reading_ = reading; update(); }
void ChannelCard::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing); card(p, rect());
    const bool speed = channel_ == Channel::Speed;
    p.setPen(muted); p.setFont(painterFont(speed ? 10 : 7.5, true));
    p.drawText(QRectF(13, 7, width() - 26, 17), Qt::AlignLeft | Qt::AlignVCenter, channelName(channel_));
    p.setPen(readingColor(reading_));
    p.setFont(painterFont(speed ? std::clamp(height() * .26, 27.0, 52.0) : std::clamp(height() * .21, 16.0, 29.0), true));
    p.drawText(QRectF(13, 23, width() - 26, height() - 42), Qt::AlignLeft | Qt::AlignVCenter, readingText(reading_, channel_));
    p.setFont(painterFont(8)); p.setPen(muted);
    p.drawText(QRectF(13, 23, width() - 26, height() - 42), Qt::AlignRight | Qt::AlignVCenter, channelUnit(channel_));
    p.setFont(painterFont(7)); p.setPen(reading_.quality == Quality::Stale ? amber : muted);
    p.drawText(QRectF(13, height() - 19, width() - 26, 15), Qt::AlignLeft | Qt::AlignVCenter, ui::qualityName(reading_.quality));
}
Tachometer::Tachometer(QWidget* parent) : QWidget(parent) {
    setMinimumSize(240, 245); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
void Tachometer::setReading(const Measurement& reading, double elapsedMs) {
    reading_ = reading;
    if (reading.quality == Quality::Valid && reading.value) {
        const double target = std::clamp(*reading.value, 0.0, channelInfo(Channel::Rpm).max);
        if (!initialized_) { needle_ = target; initialized_ = true; }
        else needle_ += (target - needle_) * (1.0 - std::exp(-std::min(elapsedMs, 100.0) / 95.0));
    }
    update();
}
void Tachometer::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing); card(p, rect());
    p.setPen(muted); p.setFont(painterFont(10, true));
    p.drawText(QRectF(10, 9, width() - 20, 22), Qt::AlignCenter, channelName(Channel::Rpm));
    const QPointF center(width() * .5, height() * .53);
    const double radius = std::min(width() * .40, height() * .38);
    const auto point = [&](double value, double rad) {
        const double angle = (135.0 + value / channelInfo(Channel::Rpm).max * 270.0) * std::numbers::pi / 180.0;
        return center + QPointF(std::cos(angle) * rad, std::sin(angle) * rad);
    };
    for (int i = 0; i <= 48; ++i) {
        const bool major = i % 4 == 0;
        const double value = channelInfo(Channel::Rpm).max * i / 48.0;
        p.setPen(QPen(border, major ? 3 : 1.3));
        p.drawLine(point(value, radius), point(value, radius - (major ? 13 : 7)));
        if (major) {
            p.setPen(muted); p.setFont(painterFont(8)); const auto pos = point(value, radius - 27);
            p.drawText(QRectF(pos.x() - 12, pos.y() - 9, 24, 18), Qt::AlignCenter, QString::number(value / 1000.0, 'f', 0));
        }
    }
    p.setFont(painterFont(8)); p.setPen(muted);
    p.drawText(QRectF(center.x() - 55, center.y() + radius * .41, 110, 18), Qt::AlignCenter, QStringLiteral("×1000 об/хв"));
    if (initialized_ && reading_.value) {
        p.setPen(QPen(reading_.quality == Quality::Valid ? accent : amber, 4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(center, point(needle_, radius - 32));
        p.setBrush(accent); p.setPen(Qt::NoPen); p.drawEllipse(center, 7, 7);
    }
    p.setPen(readingColor(reading_)); p.setFont(painterFont(std::clamp(height() * .09, 24.0, 37.0), true));
    p.drawText(QRectF(10, height() - 72, width() - 20, 43), Qt::AlignCenter, readingText(reading_, Channel::Rpm));
    p.setPen(reading_.quality == Quality::Stale ? amber : muted); p.setFont(painterFont(8));
    p.drawText(QRectF(10, height() - 28, width() - 20, 19), Qt::AlignCenter, ui::qualityName(reading_.quality));
}
HistoryChart::HistoryChart(QWidget* parent) : QWidget(parent) { setMinimumHeight(118); }
void HistoryChart::pushSample(const Sample& sample) {
    const std::size_t index = (start_ + count_) % Capacity;
    samples_[index] = sample;
    if (count_ < Capacity) ++count_; else start_ = (start_ + 1) % Capacity;
    update();
}
void HistoryChart::setNow(Time now) { now_ = now; update(); }
void HistoryChart::setChannel(Channel channel) { channel_ = channel; update(); }
void HistoryChart::clear() { start_ = count_ = 0; update(); }
void HistoryChart::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing); card(p, rect());
    const QRectF plot(49, 22, width() - 64, height() - 48);
    const std::size_t channel = static_cast<std::size_t>(channel_);
    const Time firstTime = now_ > 60000 ? now_ - 60000 : 0;
    const double right = static_cast<double>(now_), left = right - 60000.0;
    const auto& limits = channelInfo(channel_);
    const double low = limits.min, high = limits.max;
    for (int i = 0; i < 5; ++i) {
        const double y = plot.top() + plot.height() * i / 4;
        p.setPen(QPen(border, 1)); p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(muted); p.setFont(painterFont(7));
        p.drawText(QRectF(2, y - 8, 40, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(high - (high - low) * i / 4, 'f', 0));
    }
    p.setPen(muted); p.setFont(painterFont(7));
    p.drawText(QRectF(plot.left(), height() - 21, 120, 16), QStringLiteral("−60 с"));
    p.drawText(QRectF(plot.right() - 50, height() - 21, 50, 16), Qt::AlignRight, QStringLiteral("зараз"));
    p.drawText(QRectF(plot.left(), 4, plot.width(), 16), Qt::AlignCenter,
        channelUnit(channel_) + QStringLiteral(" · пропуски даних показано розривами"));
    QPainterPath path; bool connected = false; Time previous{};
    for (std::size_t i = 0; i < count_; ++i) {
        const auto& sample = samples_[(start_ + i) % Capacity];
        if (sample.time < firstTime || sample.time > now_) continue;
        if (sample.qualities[channel] != Quality::Valid || !sample.values[channel]) { connected = false; continue; }
        const QPointF position(plot.left() + (static_cast<double>(sample.time) - left) / 60000.0 * plot.width(),
            plot.bottom() - (std::clamp(*sample.values[channel], low, high) - low) / (high - low) * plot.height());
        if (!connected || sample.time - previous > 250) path.moveTo(position); else path.lineTo(position);
        previous = sample.time; connected = true;
    }
    p.save(); p.setClipRect(plot); p.setPen(QPen(accent, 2)); p.drawPath(path); p.restore();
    if (count_ == 0) {
        p.setPen(muted); p.setFont(painterFont(10)); p.drawText(plot, Qt::AlignCenter, QStringLiteral("Очікування вимірювань"));
    }
}
}
