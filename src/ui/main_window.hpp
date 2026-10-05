#pragma once
#include "application/session.hpp"
#include "recording/recorder.hpp"
#include "ui/dashboard_widgets.hpp"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QMainWindow>
#include <array>

class QLabel;
class QPushButton;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;
class QTimer;

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
    Session session_;
    Recorder recorder_;
    bool managedTime_{};
    Time managedNow_{}, lastPaint_{};
    QElapsedTimer clock_;
    QTimer* timer_{};
    QLabel *warning_{}, *sessionStatus_{}, *statistics_{}, *recordStatus_{}, *demoNotice_{};
    QPushButton *start_{}, *stop_{}, *record_{};
    QComboBox *scenario_{}, *chartChannel_{};
    QCheckBox* silence_{};
    QSpinBox *seed_{}, *delay_{};
    std::array<QDoubleSpinBox*, ChannelCount> manual_{};
    std::array<Quality, ChannelCount> injectedQualities_{};
    Tachometer* tachometer_{};
    std::array<ChannelCard*, ChannelCount> cards_{};
    HistoryChart* chart_{};
};
}
