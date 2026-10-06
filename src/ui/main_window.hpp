#pragma once
#include "application/session.hpp"
#include "bridge/client.hpp"
#include "bridge/native_transport.hpp"
#include "bench/controller.hpp"
#include "honda_dlc/session.hpp"
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
    bool startRecording(const std::filesystem::path& directory);
    void refreshPorts();
    void changeSource();
    void updateCapabilities(const std::optional<DeviceInfo>& info);
    void applyManual();
    void settleControlLayout();
    void tickSessions();
    void refreshDlcDetails();
    void refreshBridgeDetails();
    void rebuildBridge();
    void newBridgeExperiment();
    void applyBridgeFaults(dlc::Faults faults);
    void bridgeRaw(const RawEvent& event);
    const Model& activeModel() const;
    void applyDlcFaults(dlc::Faults faults);
    RecordingMetadata recordingMetadata() const;
    bool serialSelected() const;
    bool dlcSelected() const;
    bool bridgeSelected() const;
    bool bridgeSerialSelected() const;
    bool benchSelected() const;
    InMemoryTransport memoryTransport_;
#ifdef HONDADASH_WITH_SERIAL
    std::unique_ptr<SerialTransport> serialTransport_;
    std::unique_ptr<SerialTransport> responderTransport_;
#endif
    Session session_;
    dlc::Session dlcSession_;
    bridge::NativeTransport nativeBridge_;
    std::unique_ptr<bridge::Client> ownedBridgeClient_;
    std::unique_ptr<bench::Controller> benchController_;
    const bridge::Client* bridgeClient_{};
    bool benchStopPending_{}, closePending_{};
    Time benchStopDeadline_{};
    QString benchStopError_;
    std::unique_ptr<dlc::Session> bridgeSession_;
    Recorder recorder_;
    bool managedTime_{};
    Time managedNow_{}, lastPaint_{};
    QElapsedTimer clock_;
    QTimer* timer_{};
    QLabel *warning_{}, *sessionStatus_{}, *statistics_{}, *recordStatus_{}, *demoNotice_{}, *deviceInfo_{}, *capabilities_{}, *portDetails_{};
    QPushButton *start_{}, *stop_{}, *record_{}, *refreshPorts_{}, *corrupt_{}, *truncate_{};
    QComboBox *source_{}, *port_{}, *scenario_{}, *chartChannel_{}, *qualityChannel_{}, *quality_{};
    QGroupBox *manualBox_{};
    QGroupBox *faultBox_{}, *dlcBox_{};
    QComboBox *dlcProfile_{}, *dlcScenario_{};
    QLabel *dlcExchange_{}, *seedLabel_{};
    QCheckBox* dlcSilence_{};
    QSpinBox* dlcDelay_{};
    QPushButton *dlcCorrupt_{}, *dlcTruncate_{}, *dlcLength_{}, *dlcNoise_{};
    dlc::Faults dlcFaults_{};
    QComboBox *bridgeBackend_{}, *bridgeScenario_{};
    QComboBox* responderPort_{};
    QLabel *benchPortsHint_{}, *benchPeerInfo_{}, *benchPeerHex_{};
    QGroupBox* bridgeBox_{};
    QPushButton *bridgeNewExperiment_{}, *bridgeCorrupt_{}, *bridgeTruncate_{}, *bridgeLength_{}, *bridgeHeader_{}, *bridgeTrailing_{};
    QCheckBox* bridgeSilence_{};
    QSpinBox *bridgeDelay_{}, *bridgeGap_{};
    QLabel *bridgeOuterHex_{}, *bridgeInnerHex_{}, *bridgeTiming_{};
    QLabel *bridgePolicy_{}, *bridgeChannelDiagnostics_{};
    dlc::Faults bridgeFaults_{};
    QString bridgeOuterTx_, bridgeOuterRx_, bridgeInnerTx_, bridgeInnerRx_;
    QString responderOuterTx_, responderOuterRx_;
    QString bridgeInnerRead_, bridgeInnerFormula_, bridgeInnerCheck_;
    std::optional<Channel> bridgeLastUpdatedChannel_;
    std::uint32_t bridgeDisplayedRequest_{};
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
