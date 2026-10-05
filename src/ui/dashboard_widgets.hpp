#pragma once
#include "core/model.hpp"
#include <QWidget>
#include <array>
#include <optional>

namespace hd::ui {
QString channelName(Channel channel);
QString channelUnit(Channel channel);
QString qualityName(Quality quality);
QString readingText(const Measurement& reading, Channel channel);

class ChannelCard final : public QWidget {
public:
    ChannelCard(Channel channel, QWidget* parent = nullptr);
    void setReading(const Measurement& reading);
    const Measurement& reading() const { return reading_; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    Channel channel_;
    Measurement reading_{};
};

class Tachometer final : public QWidget {
public:
    explicit Tachometer(QWidget* parent = nullptr);
    void setReading(const Measurement& reading, double elapsedMs);
    const Measurement& reading() const { return reading_; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    Measurement reading_{};
    double needle_{};
    bool initialized_{};
};

class HistoryChart final : public QWidget {
public:
    static constexpr std::size_t Capacity = 640;
    explicit HistoryChart(QWidget* parent = nullptr);
    void pushSample(const Sample& sample);
    void setNow(Time now);
    void setChannel(Channel channel);
    void clear();
    std::size_t sampleCount() const { return count_; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    std::array<Sample, Capacity> samples_{};
    std::size_t start_{}, count_{};
    Channel channel_{Channel::Rpm};
    Time now_{};
};
}
