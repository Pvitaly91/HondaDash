#pragma once
#include "application/session.hpp"
#include "recording/recorder.hpp"
#include "transport/in_memory_transport.hpp"
#include "ui/dashboard_widgets.hpp"
#ifdef HONDADASH_WITH_SERIAL
#include "transport/serial_transport.hpp"
#endif
#include <QElapsedTimer>
#include <QJsonObject>
#include <QMainWindow>
#include <array>
#include <memory>

class QLabel;
class QPushButton;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;
class QTimer;
class QGroupBox;
class QScrollArea;

namespace hd::ui {
class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(bool managedTime = false, QWidget* parent = nullptr);
    ~MainWindow() override;
    QJsonObject smokeTest(const QString& screenshotPath);
    void markOffscreenScreenshot();
protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
private:
    Time now() const;
    void refreshDashboard();
    void advance(Time amount);
    void startSession();
    void stopSession();
    void toggleRecording();
    void refreshPorts();
    void changeSource();
    void updateCapabilities(const std::optional<DeviceInfo>& info);
    void applyManual();
    void settleControlLayout();
    RecordingMetadata recordingMetadata() const;
    bool serialSelected() const;
    InMemoryTransport memoryTransport_;
#ifdef HONDADASH_WITH_SERIAL
    std::unique_ptr<SerialTransport> serialTransport_;
#endif
    Session session_;
    Recorder recorder_;
    bool managedTime_{};
    Time managedNow_{}, lastPaint_{};
    QElapsedTimer clock_;
    QTimer* timer_{};
    QLabel *warning_{}, *sessionStatus_{}, *statistics_{}, *recordStatus_{}, *demoNotice_{}, *deviceInfo_{}, *capabilities_{}, *portDetails_{};
    QPushButton *start_{}, *stop_{}, *record_{}, *refreshPorts_{}, *corrupt_{}, *truncate_{};
    QComboBox *source_{}, *port_{}, *scenario_{}, *chartChannel_{}, *qualityChannel_{}, *quality_{};
    QGroupBox *manualBox_{};
    QWidget* controlPanel_{};
    QScrollArea* controlScroll_{};
    QCheckBox* silence_{};
    QSpinBox *seed_{}, *delay_{};
    std::array<QDoubleSpinBox*, ChannelCount> manual_{};
    std::array<Quality, ChannelCount> injectedQualities_{};
    DeviceFaults desiredFaults_{};
    bool offscreenScreenshot_{};
    Tachometer* tachometer_{};
    std::array<ChannelCard*, ChannelCount> cards_{};
    HistoryChart* chart_{};
};
}
