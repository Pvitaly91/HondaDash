#include "ui/main_window.hpp"
#include "honda_dlc/polling.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QVariant>
#ifdef HONDADASH_WITH_SERIAL
#include <QSerialPortInfo>
#endif
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>

namespace hd::ui {
namespace {
QString sessionName(SessionState state) {
    switch (state) {
    case SessionState::Stopped: return QStringLiteral("Зупинено");
    case SessionState::Opening: return QStringLiteral("Відкриття порту");
    case SessionState::BootWaiting: return QStringLiteral("Очікування запуску пристрою");
    case SessionState::Handshaking: return QStringLiteral("Перевірка HELLO + INFO");
    case SessionState::Running: return QStringLiteral("Обмін працює");
    case SessionState::Faulted: return QStringLiteral("Помилка з’єднання");
    }
    return {};
}
QString dlcSessionName(dlc::State state) {
    switch (state) {
    case dlc::State::Stopped: return QStringLiteral("DLC: зупинено");
    case dlc::State::Initializing: return QStringLiteral("DLC: ініціалізація");
    case dlc::State::Polling: return QStringLiteral("DLC: читання");
    case dlc::State::Faulted: return QStringLiteral("DLC: потрібен перезапуск");
    }
    return {};
}
QString bridgeSessionName(bridge::State state) {
    switch (state) {
    case bridge::State::Disconnected: return QStringLiteral("Міст: від’єднано");
    case bridge::State::Opening: return QStringLiteral("Міст: відкриття порту");
    case bridge::State::BootWaiting: return QStringLiteral("Міст: порт відкритий, запуск");
    case bridge::State::Handshaking: return QStringLiteral("Міст: перевірка identity");
    case bridge::State::Ready: return QStringLiteral("Міст розпізнано · новий експеримент");
    case bridge::State::Starting: return QStringLiteral("Міст: нова лабораторна межа");
    case bridge::State::Initializing: return QStringLiteral("Міст: DLC-ініціалізація");
    case bridge::State::Running: return QStringLiteral("Міст: ініціалізовано, читання");
    case bridge::State::Faulted: return QStringLiteral("Міст: обмін заблоковано");
    }
    return {};
}
std::filesystem::path filePath(const QString& path) {
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toUtf8().constData());
#endif
}
QString pathString(const std::filesystem::path& path) {
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromUtf8(path.string().c_str());
#endif
}
QLabel* label(const QString& text) { auto* result = new QLabel(text); result->setWordWrap(true); return result; }
}
MainWindow::MainWindow(bool managedTime, QWidget* parent) : QMainWindow(parent), session_(memoryTransport_), managedTime_(managedTime) {
#ifdef HONDADASH_WITH_SERIAL
    serialTransport_ = std::make_unique<SerialTransport>([this] { return now(); });
    responderTransport_ = std::make_unique<SerialTransport>([this] { return now(); });
#endif
    injectedQualities_.fill(Quality::Valid);
    setWindowTitle(QStringLiteral("HondaDash · M3a · two-Nano laboratory bench"));
    resize(1280, 720); setMinimumSize(980, 560);
    setStyleSheet(QStringLiteral(
        "QMainWindow,QWidget{background:#0c121d;color:#e8f0f8;font-family:'Segoe UI';font-size:11px;}"
        "QLabel{background:transparent;} QGroupBox{border:1px solid #2d3d4e;border-radius:7px;margin-top:10px;padding-top:9px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:10px;padding:0 4px;color:#91a5ba;}"
        "QPushButton{background:#1d2b3b;border:1px solid #364a60;border-radius:5px;padding:7px 9px;}"
        "QPushButton:hover{background:#293d50;} QPushButton:disabled{color:#546579;border-color:#233243;}"
        "QPushButton#startSession{background:#187b69;border-color:#249b85;font-weight:bold;}"
        "QComboBox,QSpinBox,QDoubleSpinBox{background:#151d29;border:1px solid #364a60;border-radius:4px;padding:4px;min-height:18px;}"
        "QComboBox QAbstractItemView{background:#151d29;color:#e8f0f8;selection-background-color:#187b69;}"
        "QScrollArea{border:none;} QCheckBox{spacing:7px;} QScrollBar:vertical{background:#0c121d;width:8px;}"
        "QScrollBar::handle:vertical{background:#364a60;min-height:25px;border-radius:4px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"));
    auto* root = new QWidget;
    auto* layout = new QVBoxLayout(root); layout->setContentsMargins(12, 10, 12, 10); layout->setSpacing(8);
    auto* top = new QHBoxLayout;
    auto* title = label(QStringLiteral("HONDA<span style='color:#51d3ba'>DASH</span>  <span style='font-size:10px;color:#91a5ba'>M3a · лабораторний стенд</span>"));
    title->setTextFormat(Qt::RichText); title->setStyleSheet(QStringLiteral("font-size:19px;font-weight:bold;"));
    top->addWidget(title, 1);
    auto* fullscreen = new QPushButton(QStringLiteral("На весь екран · F11"));
    connect(fullscreen, &QPushButton::clicked, this, [this] { isFullScreen() ? showNormal() : showFullScreen(); });
    top->addWidget(fullscreen); layout->addLayout(top);
    warning_ = label(QStringLiteral("ЕМУЛЯЦІЯ — не підключено до автомобіля"));
    warning_->setObjectName(QStringLiteral("emulationWarning"));
    warning_->setAlignment(Qt::AlignCenter);
    warning_->setStyleSheet(QStringLiteral("background:#382b1c;color:#ffca79;border:1px solid #705432;border-radius:5px;padding:5px;font-weight:bold;"));
    layout->addWidget(warning_);
    auto* body = new QHBoxLayout; body->setSpacing(10);
    auto* dashboard = new QVBoxLayout; dashboard->setSpacing(8);
    auto* gauges = new QHBoxLayout; gauges->setSpacing(8);
    tachometer_ = new Tachometer; tachometer_->setObjectName(QStringLiteral("tachometer")); gauges->addWidget(tachometer_, 11);
    auto* values = new QVBoxLayout; values->setSpacing(8);
    cards_[channelIndex(Channel::Speed)] = new ChannelCard(Channel::Speed);
    values->addWidget(cards_[channelIndex(Channel::Speed)], 4);
    auto* grid = new QGridLayout; grid->setSpacing(7);
    for (std::size_t i = 2; i < ChannelCount; ++i) {
        auto* card = new ChannelCard(static_cast<Channel>(i)); cards_[i] = card;
        if (i == 6) grid->addWidget(card, 2, 0, 1, 2);
        else grid->addWidget(card, static_cast<int>((i - 2) / 2), static_cast<int>((i - 2) % 2));
    }
    values->addLayout(grid, 8); gauges->addLayout(values, 12); dashboard->addLayout(gauges, 3);
    auto* chartHeader = new QHBoxLayout;
    chartHeader->addWidget(label(QStringLiteral("ОСТАННЯ ХВИЛИНА")), 1);
    chartChannel_ = new QComboBox;
    for (std::size_t i = 0; i < ChannelCount; ++i) chartChannel_->addItem(channelName(static_cast<Channel>(i)));
    chartHeader->addWidget(chartChannel_); dashboard->addLayout(chartHeader);
    chart_ = new HistoryChart; chart_->setObjectName(QStringLiteral("historyChart")); dashboard->addWidget(chart_, 1);
    connect(chartChannel_, &QComboBox::currentIndexChanged, this, [this](int index) { chart_->setChannel(static_cast<Channel>(index)); });
    body->addLayout(dashboard, 1);
    auto* controls = new QWidget; controls->setMinimumWidth(240); controls->setMaximumWidth(278);
    controlPanel_ = controls;
    auto* controlLayout = new QVBoxLayout(controls); controlLayout->setContentsMargins(3, 0, 3, 0); controlLayout->setSpacing(8);
    auto* controlTitle = label(QStringLiteral("КЕРУВАННЯ ЕМУЛЯТОРОМ")); controlTitle->setStyleSheet(QStringLiteral("font-weight:bold;color:#51d3ba;"));
    controlLayout->addWidget(controlTitle);
    source_ = new QComboBox; source_->setObjectName(QStringLiteral("source"));
    source_->addItem(QStringLiteral("Вбудований емулятор"), 0);
#ifdef HONDADASH_WITH_SERIAL
    source_->addItem(QStringLiteral("USB Serial · тестова Nano"), 1);
#endif
    source_->addItem(QStringLiteral("Honda DLC — лабораторна емуляція"), 2);
    source_->addItem(QStringLiteral("Honda DLC — тестовий міст"), 3);
    source_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    source_->setToolTip(QStringLiteral("Лабораторний DLC працює у пам’яті. Bridge-lab використовує virtual ECU. Окремий two-Nano bench потребує двох явно вибраних портів і низьковольтної схеми; автомобіль заборонений."));
    controlLayout->addWidget(source_);
    bridgeBackend_ = new QComboBox; bridgeBackend_->setObjectName(QStringLiteral("bridgeBackend"));
    bridgeBackend_->addItem(QStringLiteral("Міст на ПК"), 0);
#ifdef HONDADASH_WITH_SERIAL
    bridgeBackend_->addItem(QStringLiteral("Міст на Nano через USB"), 1);
    bridgeBackend_->addItem(QStringLiteral("Стенд · дві Nano"), 2);
#endif
    bridgeBackend_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    controlLayout->addWidget(bridgeBackend_);
    connect(bridgeBackend_, &QComboBox::currentIndexChanged, this, [this] { changeSource(); });
    auto* portRow = new QHBoxLayout;
    port_ = new QComboBox; port_->setObjectName(QStringLiteral("serialPort"));
    port_->setMinimumWidth(120); port_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    refreshPorts_ = new QPushButton(QStringLiteral("Оновити")); refreshPorts_->setObjectName(QStringLiteral("refreshPorts"));
    portRow->addWidget(port_, 1); portRow->addWidget(refreshPorts_); controlLayout->addLayout(portRow);
    portDetails_ = label({}); portDetails_->setStyleSheet(QStringLiteral("color:#91a5ba;font-size:10px;")); controlLayout->addWidget(portDetails_);
    benchPortsHint_ = label(QStringLiteral("Верхній порт: Nano №1 bridge-bench.\nНижній порт: Nano №2 responder-bench.\nПотрібні різні порти; з'єднання лише за схемою стенда."));
    controlLayout->addWidget(benchPortsHint_);
    responderPort_ = new QComboBox; responderPort_->setObjectName(QStringLiteral("responderPort"));
    responderPort_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    controlLayout->addWidget(responderPort_);
    benchPeerInfo_ = label({}); controlLayout->addWidget(benchPeerInfo_);
    benchPeerHex_ = label({}); benchPeerHex_->setStyleSheet(QStringLiteral("color:#91a5ba;font-size:9px;")); controlLayout->addWidget(benchPeerHex_);
    connect(responderPort_, &QComboBox::currentIndexChanged, this, [this] { refreshDashboard(); });
    deviceInfo_ = label({}); deviceInfo_->setObjectName(QStringLiteral("deviceInfo")); controlLayout->addWidget(deviceInfo_);
    capabilities_ = label({}); capabilities_->setObjectName(QStringLiteral("capabilities"));
    capabilities_->setStyleSheet(QStringLiteral("color:#91a5ba;font-size:10px;")); controlLayout->addWidget(capabilities_);
    connect(source_, &QComboBox::currentIndexChanged, this, [this] { changeSource(); });
    connect(refreshPorts_, &QPushButton::clicked, this, [this] { refreshPorts(); });
    connect(port_, &QComboBox::currentIndexChanged, this, [this] {
        portDetails_->setText(port_->currentData(Qt::ToolTipRole).toString()); refreshDashboard();
    });
    scenario_ = new QComboBox; scenario_->setObjectName(QStringLiteral("scenario"));
    scenario_->addItems({QStringLiteral("Запалювання · двигун зупинено"), QStringLiteral("Холостий хід"), QStringLiteral("Демонстраційний цикл"), QStringLiteral("Ручне керування")});
    scenario_->setCurrentIndex(2); controlLayout->addWidget(scenario_);
    demoNotice_ = label(QStringLiteral("Демонстраційний цикл: прискорений прогрів. Усі значення й діапазони синтетичні."));
    demoNotice_->setStyleSheet(QStringLiteral("color:#91a5ba;font-size:10px;")); controlLayout->addWidget(demoNotice_);
    auto* buttons = new QHBoxLayout;
    start_ = new QPushButton(QStringLiteral("Старт")); start_->setObjectName(QStringLiteral("startSession"));
    stop_ = new QPushButton(QStringLiteral("Стоп")); stop_->setObjectName(QStringLiteral("stopSession"));
    buttons->addWidget(start_); buttons->addWidget(stop_); controlLayout->addLayout(buttons);
    connect(start_, &QPushButton::clicked, this, [this] { startSession(); });
    connect(stop_, &QPushButton::clicked, this, [this] { stopSession(); });
    bridgeNewExperiment_ = new QPushButton(QStringLiteral("Новий лабораторний експеримент"));
    bridgeNewExperiment_->setObjectName(QStringLiteral("bridgeNewExperiment"));
    bridgeNewExperiment_->setToolTip(QStringLiteral("Явно створює новий віртуальний ECU, відкидає його старі події та запускає ініціалізацію. Це не recovery фізичного ECU."));
    controlLayout->addWidget(bridgeNewExperiment_);
    connect(bridgeNewExperiment_, &QPushButton::clicked, this, [this] { newBridgeExperiment(); });
    auto* seedLayout = new QHBoxLayout; seedLabel_ = label(QStringLiteral("Seed")); seedLayout->addWidget(seedLabel_);
    seed_ = new QSpinBox; seed_->setRange(0, 2147483647); seed_->setValue(42); seedLayout->addWidget(seed_); controlLayout->addLayout(seedLayout);
    session_.setScenario(2, 42);
    connect(seed_, &QSpinBox::valueChanged, this, [this](int value) { session_.setScenario(static_cast<std::uint8_t>(scenario_->currentIndex()), static_cast<std::uint32_t>(value)); });
    manualBox_ = new QGroupBox(QStringLiteral("Ручні значення · після відповіді"));
    auto* form = new QFormLayout(manualBox_); form->setContentsMargins(9, 17, 9, 8); form->setSpacing(5);
    const std::array<double, ChannelCount> defaults{850, 0, 80, 23, 0, 30, 14.1};
    const std::array<QString, ChannelCount> shortNames{QStringLiteral("Оберти"),QStringLiteral("Швидкість"),QStringLiteral("Рідина"),QStringLiteral("Впуск"),QStringLiteral("Дросель"),QStringLiteral("Тиск"),QStringLiteral("Напруга")};
    for (std::size_t i = 0; i < ChannelCount; ++i) {
        const auto channel = static_cast<Channel>(i); const auto& info = channelInfo(channel);
        auto* spin = new QDoubleSpinBox; manual_[i] = spin;
        spin->setObjectName(QStringLiteral("manual_%1").arg(i)); spin->setRange(info.min, info.max); spin->setDecimals(info.decimals);
        spin->setSingleStep(i == 0 ? 100 : i == 6 ? .1 : 1); spin->setValue(defaults[i]); spin->setSuffix(QStringLiteral(" ") + channelUnit(channel));
        form->addRow(shortNames[i], spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this] { applyManual(); });
    }
    applyManual();
    manualBox_->setVisible(false); controlLayout->addWidget(manualBox_);
    connect(scenario_, &QComboBox::currentIndexChanged, this, [this](int index) {
        session_.setScenario(static_cast<std::uint8_t>(index), static_cast<std::uint32_t>(seed_->value())); manualBox_->setVisible(index == 3);
    });
    auto* faultBox = new QGroupBox(QStringLiteral("Несправності"));
    faultBox_ = faultBox;
    auto* faultLayout = new QVBoxLayout(faultBox); faultLayout->setContentsMargins(9, 17, 9, 8); faultLayout->setSpacing(6);
    silence_ = new QCheckBox(QStringLiteral("Не відповідати")); silence_->setObjectName(QStringLiteral("silence")); faultLayout->addWidget(silence_);
    connect(silence_, &QCheckBox::toggled, this, [this](bool enabled) { desiredFaults_.silent = enabled; session_.setFaults(desiredFaults_); });
    auto* delayRow = new QHBoxLayout; delayRow->addWidget(label(QStringLiteral("Затримка")));
    delay_ = new QSpinBox; delay_->setRange(0, 5000); delay_->setSuffix(QStringLiteral(" мс")); delayRow->addWidget(delay_); faultLayout->addLayout(delayRow);
    connect(delay_, &QSpinBox::valueChanged, this, [this](int value) { desiredFaults_.delayMs = static_cast<std::uint16_t>(value); session_.setFaults(desiredFaults_); });
    corrupt_ = new QPushButton(QStringLiteral("Пошкодити наступну CRC")); faultLayout->addWidget(corrupt_);
    connect(corrupt_, &QPushButton::clicked, this, [this] { auto fault = desiredFaults_; fault.corruptNext = true; session_.setFaults(fault); });
    truncate_ = new QPushButton(QStringLiteral("Обірвати наступний пакет")); faultLayout->addWidget(truncate_);
    connect(truncate_, &QPushButton::clicked, this, [this] { auto fault = desiredFaults_; fault.truncateNext = true; session_.setFaults(fault); });
    qualityChannel_ = new QComboBox; for (const auto& name : shortNames) qualityChannel_->addItem(name); faultLayout->addWidget(qualityChannel_);
    quality_ = new QComboBox; quality_->addItems({QStringLiteral("Коректний"), QStringLiteral("Не підтримується"), QStringLiteral("Некоректний")}); faultLayout->addWidget(quality_);
    const auto applyQuality = [this] {
        const std::array<Quality, 3> types{Quality::Valid, Quality::Unsupported, Quality::Invalid};
        const auto channel = static_cast<Channel>(qualityChannel_->currentIndex());
        const auto selectedQuality = types[static_cast<std::size_t>(quality_->currentIndex())];
        injectedQualities_[channelIndex(channel)] = selectedQuality;
        session_.setQualities(injectedQualities_);
    };
    connect(quality_, &QComboBox::currentIndexChanged, this, [applyQuality](int) { applyQuality(); });
    connect(qualityChannel_, &QComboBox::currentIndexChanged, this, [this] {
        const auto selected = injectedQualities_[static_cast<std::size_t>(qualityChannel_->currentIndex())];
        quality_->blockSignals(true);
        quality_->setCurrentIndex(selected == Quality::Unsupported ? 1 : selected == Quality::Invalid ? 2 : 0);
        quality_->blockSignals(false);
    });
    controlLayout->addWidget(faultBox);
    dlcBox_ = new QGroupBox(QStringLiteral("Honda DLC · offline"));
    auto* dlcLayout = new QVBoxLayout(dlcBox_); dlcLayout->setContentsMargins(9, 17, 9, 8); dlcLayout->setSpacing(6);
    dlcProfile_ = new QComboBox; dlcProfile_->setObjectName(QStringLiteral("dlcProfile"));
    dlcProfile_->addItem(QStringLiteral("kerpz OBD1 · reference v1"), QStringLiteral("honda-dlc-kerpz-obd1-reference-v1"));
    dlcProfile_->setToolTip(QStringLiteral("honda-dlc-kerpz-obd1-reference-v1 · hardware_verified=false · live_enabled=false")); dlcLayout->addWidget(dlcProfile_);
    dlcScenario_ = new QComboBox; dlcScenario_->setObjectName(QStringLiteral("dlcScenario"));
    dlcScenario_->addItems({QStringLiteral("Набір A · базові raw-байти"), QStringLiteral("Набір B · вищі значення"), QStringLiteral("Зміна raw між читаннями"), QStringLiteral("Граничні raw / невизначене RPM")});
    dlcLayout->addWidget(dlcScenario_);
    connect(dlcProfile_, &QComboBox::currentIndexChanged, this, [this] { dlcSession_.setProfile(dlcProfile_->currentData().toString().toStdString(), now()); chart_->clear(); refreshDashboard(); });
    connect(dlcScenario_, &QComboBox::currentIndexChanged, this, [this](int index) {
        const auto scenario = static_cast<dlc::Scenario>(index); dlcSession_.setScenario(scenario);
        if (recorder_.active()) recorder_.enqueueRaw({now(), dlcSession_.id(), 0, "fixture_change", dlc::scenarioId(scenario), {}});
    });
    dlcSilence_ = new QCheckBox(QStringLiteral("Не відповідати")); dlcSilence_->setObjectName(QStringLiteral("dlcSilence")); dlcLayout->addWidget(dlcSilence_);
    auto* dlcDelayRow = new QHBoxLayout; dlcDelayRow->addWidget(label(QStringLiteral("Затримка")));
    dlcDelay_ = new QSpinBox; dlcDelay_->setRange(0, 5000); dlcDelay_->setSuffix(QStringLiteral(" мс")); dlcDelayRow->addWidget(dlcDelay_); dlcLayout->addLayout(dlcDelayRow);
    connect(dlcSilence_, &QCheckBox::toggled, this, [this](bool silent) { dlcFaults_.silent = silent; applyDlcFaults(dlcFaults_); });
    connect(dlcDelay_, &QSpinBox::valueChanged, this, [this](int delay) { dlcFaults_.delayMs = static_cast<Time>(delay); applyDlcFaults(dlcFaults_); });
    dlcCorrupt_ = new QPushButton(QStringLiteral("Пошкодити checksum")); dlcLayout->addWidget(dlcCorrupt_);
    dlcTruncate_ = new QPushButton(QStringLiteral("Обірвати відповідь")); dlcLayout->addWidget(dlcTruncate_);
    dlcLength_ = new QPushButton(QStringLiteral("Неправильна довжина")); dlcLayout->addWidget(dlcLength_);
    dlcNoise_ = new QPushButton(QStringLiteral("Шум перед відповіддю")); dlcLayout->addWidget(dlcNoise_);
    connect(dlcCorrupt_, &QPushButton::clicked, this, [this] { auto f = dlcFaults_; f.corruptNext = true; applyDlcFaults(f); });
    connect(dlcTruncate_, &QPushButton::clicked, this, [this] { auto f = dlcFaults_; f.truncateNext = true; applyDlcFaults(f); });
    connect(dlcLength_, &QPushButton::clicked, this, [this] { auto f = dlcFaults_; f.wrongLengthNext = true; applyDlcFaults(f); });
    connect(dlcNoise_, &QPushButton::clicked, this, [this] { auto f = dlcFaults_; f.noiseNext = true; applyDlcFaults(f); });
    dlcExchange_ = label({}); dlcExchange_->setObjectName(QStringLiteral("dlcExchange")); dlcExchange_->setTextFormat(Qt::PlainText);
    dlcExchange_->setTextInteractionFlags(Qt::TextSelectableByMouse); dlcExchange_->setStyleSheet(QStringLiteral("font-family:Consolas;font-size:10px;color:#b7cbdd;")); dlcLayout->addWidget(dlcExchange_);
    dlcLayout->insertWidget(2, dlcExchange_);
    controlLayout->addWidget(dlcBox_); dlcBox_->setVisible(false);
    bridgeBox_ = new QGroupBox(QStringLiteral("Віртуальний DLC backend"));
    auto* bridgeLayout = new QVBoxLayout(bridgeBox_); bridgeLayout->setContentsMargins(9, 17, 9, 8); bridgeLayout->setSpacing(6);
    auto* bridgeProfile = label(QStringLiteral("kerpz OBD1 · reference v1\nRPM / ECT / TPS · ECU не перевірено"));
    bridgeProfile->setToolTip(QStringLiteral("honda-dlc-kerpz-obd1-reference-v1 · hardware_verified=false · live_enabled=false")); bridgeLayout->addWidget(bridgeProfile);
    bridgePolicy_ = label({}); bridgePolicy_->setObjectName(QStringLiteral("bridgePolicy"));
    bridgePolicy_->setTextFormat(Qt::PlainText);
    bridgePolicy_->setStyleSheet(QStringLiteral("color:#91a5ba;font-size:10px;")); bridgeLayout->addWidget(bridgePolicy_);
    bridgeChannelDiagnostics_ = label({}); bridgeChannelDiagnostics_->setObjectName(QStringLiteral("bridgeChannelDiagnostics"));
    bridgeChannelDiagnostics_->setTextFormat(Qt::PlainText);
    bridgeChannelDiagnostics_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bridgeChannelDiagnostics_->setStyleSheet(QStringLiteral("color:#b7cbdd;font-size:10px;")); bridgeLayout->addWidget(bridgeChannelDiagnostics_);
    bridgeScenario_ = new QComboBox; bridgeScenario_->setObjectName(QStringLiteral("bridgeScenario"));
    bridgeScenario_->addItems({QStringLiteral("Набір A · 750 / 61 / 32"), QStringLiteral("Набір B · 1500 / 89 / 75")});
    bridgeLayout->addWidget(bridgeScenario_);
    connect(bridgeScenario_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (bridgeSession_) bridgeSession_->setScenario(static_cast<dlc::Scenario>(index));
    });
    bridgeSilence_ = new QCheckBox(QStringLiteral("DLC: не відповідати")); bridgeLayout->addWidget(bridgeSilence_);
    const auto bridgeTimeControl = [&](const QString& text, const QString& name, int maximum) {
        auto* row = new QHBoxLayout; row->addWidget(label(text)); auto* spin = new QSpinBox;
        spin->setObjectName(name); spin->setRange(0, maximum); spin->setSuffix(QStringLiteral(" мс")); row->addWidget(spin); bridgeLayout->addLayout(row); return spin;
    };
    bridgeDelay_ = bridgeTimeControl(QStringLiteral("DLC: затримка"), QStringLiteral("bridgeDelay"), 5000);
    bridgeGap_ = bridgeTimeControl(QStringLiteral("DLC: пауза байтів"), QStringLiteral("bridgeGap"), 1000);
    connect(bridgeSilence_, &QCheckBox::toggled, this, [this](bool value) { dlc::Faults f; f.silent = value; applyBridgeFaults(f); });
    connect(bridgeDelay_, &QSpinBox::valueChanged, this, [this](int value) { dlc::Faults f; f.delayMs = static_cast<Time>(value); applyBridgeFaults(f); });
    connect(bridgeGap_, &QSpinBox::valueChanged, this, [this](int value) { dlc::Faults f; f.gapMs = static_cast<Time>(value); applyBridgeFaults(f); });
    const auto bridgeButton = [&](const QString& text) { auto* button = new QPushButton(text); bridgeLayout->addWidget(button); return button; };
    bridgeCorrupt_ = bridgeButton(QStringLiteral("DLC: пошкодити checksum"));
    bridgeTruncate_ = bridgeButton(QStringLiteral("DLC: обірвати відповідь"));
    bridgeLength_ = bridgeButton(QStringLiteral("DLC: неправильна довжина"));
    bridgeHeader_ = bridgeButton(QStringLiteral("DLC: неправильний header"));
    bridgeTrailing_ = bridgeButton(QStringLiteral("DLC: зайві байти після відповіді"));
    connect(bridgeCorrupt_, &QPushButton::clicked, this, [this] { dlc::Faults f; f.corruptNext = true; applyBridgeFaults(f); });
    connect(bridgeTruncate_, &QPushButton::clicked, this, [this] { dlc::Faults f; f.truncateNext = true; applyBridgeFaults(f); });
    connect(bridgeLength_, &QPushButton::clicked, this, [this] { dlc::Faults f; f.wrongLengthNext = true; applyBridgeFaults(f); });
    connect(bridgeHeader_, &QPushButton::clicked, this, [this] { dlc::Faults f; f.headerNext = true; applyBridgeFaults(f); });
    connect(bridgeTrailing_, &QPushButton::clicked, this, [this] { dlc::Faults f; f.trailingNext = true; applyBridgeFaults(f); });
    const auto bridgeTraceLabel = [&](const QString& name) { auto* text = label({}); text->setObjectName(name); text->setTextFormat(Qt::PlainText);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse); text->setStyleSheet(QStringLiteral("font-family:Consolas;font-size:10px;color:#b7cbdd;")); bridgeLayout->addWidget(text); return text; };
    bridgeOuterHex_ = bridgeTraceLabel(QStringLiteral("bridgeOuterHex"));
    bridgeInnerHex_ = bridgeTraceLabel(QStringLiteral("bridgeInnerHex"));
    bridgeTiming_ = bridgeTraceLabel(QStringLiteral("bridgeTiming"));
    controlLayout->addWidget(bridgeBox_);
    record_ = new QPushButton(QStringLiteral("Почати запис CSV + JSONL")); record_->setObjectName(QStringLiteral("record")); controlLayout->addWidget(record_);
    connect(record_, &QPushButton::clicked, this, [this] { toggleRecording(); });
    recordStatus_ = label(QStringLiteral("Журнал: зупинено")); recordStatus_->setObjectName(QStringLiteral("recordStatus"));
    recordStatus_->setTextInteractionFlags(Qt::TextSelectableByMouse); recordStatus_->setStyleSheet(QStringLiteral("color:#91a5ba;")); controlLayout->addWidget(recordStatus_);
    controlLayout->addStretch();
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setWidget(controls);
    // Reserve the existing sidebar width, including its vertical scrollbar.
    // Shrinking the viewport to 242 px can violate the controls' minimum width.
    scroll->setFixedWidth(290);
    controlScroll_ = scroll;
    body->addWidget(scroll); layout->addLayout(body, 1);
    auto* footer = new QHBoxLayout;
    sessionStatus_ = label({}); sessionStatus_->setMinimumWidth(130); footer->addWidget(sessionStatus_);
    statistics_ = label({}); statistics_->setAlignment(Qt::AlignRight | Qt::AlignVCenter); footer->addWidget(statistics_, 1);
    layout->addLayout(footer); setCentralWidget(root);
    const auto sampleSink = [this](const Sample& sample) { chart_->pushSample(sample); if (recorder_.active()) recorder_.enqueueSample(sample); };
    session_.onSample = [this, sampleSink](const Sample& sample) { if (!dlcSelected() && !bridgeSelected()) sampleSink(sample); };
    session_.onRaw = [this](const RawEvent& event) { if (!dlcSelected() && !bridgeSelected() && recorder_.active()) recorder_.enqueueRaw(event); };
    dlcSession_.onSample = [this, sampleSink](const Sample& sample) { if (dlcSelected()) sampleSink(sample); };
    dlcSession_.onRaw = [this](const RawEvent& event) { if (dlcSelected() && recorder_.active()) recorder_.enqueueRaw(event); };
    clock_.start(); timer_ = new QTimer(this); timer_->setTimerType(Qt::PreciseTimer); timer_->setInterval(16);
    connect(timer_, &QTimer::timeout, this, [this] { tickSessions(); refreshDashboard(); });
    if (!managedTime_) timer_->start();
    rebuildBridge();
    refreshPorts();
    refreshDashboard();
}
MainWindow::~MainWindow() {
    stopSession();
    if (benchController_) {
        if (benchStopPending_) bridgeRaw({now(),0,0,"bench_stop_unconfirmed","Window destroyed before external QUIESCE acknowledgement; physical state unconfirmed",{}});
        benchController_->disconnect(now());
        benchController_->onRaw = {};
    }
    if (ownedBridgeClient_) ownedBridgeClient_->onRaw = {};
    bridgeSession_->onRaw = {};
    bridgeSession_.reset(); // Destroy link callbacks while Recorder and transports are still alive.
    benchController_.reset(); ownedBridgeClient_.reset(); bridgeClient_ = nullptr;
    recorder_.stop();
}
Time MainWindow::now() const { return managedTime_ ? managedNow_ : static_cast<Time>(std::max<qint64>(0, clock_.elapsed())); }
bool MainWindow::serialSelected() const { return source_->currentData().toInt() == 1; }
bool MainWindow::dlcSelected() const { return source_->currentData().toInt() == 2; }
bool MainWindow::bridgeSelected() const { return source_->currentData().toInt() == 3; }
bool MainWindow::bridgeSerialSelected() const { return bridgeSelected() && bridgeBackend_->currentData().toInt() == 1; }
bool MainWindow::benchSelected() const { return bridgeSelected() && bridgeBackend_->currentData().toInt() == 2; }
const Model& MainWindow::activeModel() const { return bridgeSelected() ? bridgeSession_->model() : dlcSelected() ? dlcSession_.model() : session_.model(); }
void MainWindow::tickSessions() {
    if (bridgeSelected()) bridgeSession_->tick(now()); else if (dlcSelected()) dlcSession_.tick(now()); else session_.tick(now());
    if (benchStopPending_ && (benchController_->quiescent() || now() >= benchStopDeadline_)) {
        if (!benchController_->quiescent()) {
            benchStopError_ = QStringLiteral("QUIESCE не підтверджено до deadline; обидва USB закриті. Стан лінії потребує перевірки.");
            bridgeRaw({now(),0,0,"bench_stop_unconfirmed",benchStopError_.toStdString(),{}});
        }
        benchController_->disconnect(now()); benchStopPending_ = false;
        if (closePending_) { closePending_ = false; close(); }
    }
}
void MainWindow::rebuildBridge() {
    if (bridgeSession_) bridgeSession_->stop(now());
    if (benchController_) benchController_->disconnect(now());
    else if (ownedBridgeClient_) ownedBridgeClient_->disconnect(now());
    bridgeSession_.reset(); benchController_.reset(); ownedBridgeClient_.reset(); bridgeClient_ = nullptr;
    benchStopPending_ = false; benchStopError_.clear();
    bridge::Settings settings;
#ifdef HONDADASH_WITH_SERIAL
    if (benchSelected()) {
        settings.bootMs = 1800;
        benchController_ = std::make_unique<bench::Controller>(*serialTransport_, *responderTransport_, settings);
        bridgeClient_ = &benchController_->bridgeClient();
    } else if (bridgeSerialSelected()) { settings.bootMs = 1500; ownedBridgeClient_ = std::make_unique<bridge::Client>(*serialTransport_, settings); }
    else
#endif
        ownedBridgeClient_ = std::make_unique<bridge::Client>(nativeBridge_, settings);
    if (!bridgeClient_) bridgeClient_ = ownedBridgeClient_.get();
    dlc::Link& link = benchController_ ? static_cast<dlc::Link&>(*benchController_) : static_cast<dlc::Link&>(*ownedBridgeClient_);
    bridgeSession_ = std::make_unique<dlc::Session>(link, dlc::bridgePollingSettings(), dlc::bridgeFreshness());
    std::array<std::optional<Time>, ChannelCount> plotGaps{};
    if (bridgeSelected()) for (const auto& field : dlc::fields())
        plotGaps[channelIndex(field.channel)] = dlc::bridgeMaximumRequestInterval(field.channel) + dlc::BridgeResultBudgetMs;
    chart_->setGapLimits(plotGaps);
    bridgeSession_->setScenario(static_cast<dlc::Scenario>(bridgeScenario_->currentIndex()));
    bridgeSession_->setFaults(bridgeFaults_);
    bridgeSession_->onSample = [this](const Sample& sample) {
        if (!bridgeSelected()) return;
        if (sample.updatedMask != AllChannelsMask && sample.request == bridgeDisplayedRequest_)
            bridgeInnerCheck_ = QStringLiteral("Міст і ПК: header / length / checksum OK; калібрування ECU не перевірено");
        auto plotSample = sample;
        for (std::size_t i = 0; i < ChannelCount; ++i)
            if ((sample.updatedMask & (1u << i)) != 0) {
                plotSample.qualities[i] = bridgeSession_->model().channels()[i].quality;
                if (sample.request) bridgeLastUpdatedChannel_ = static_cast<Channel>(i);
            }
        chart_->pushSample(plotSample); if (recorder_.active()) recorder_.enqueueSample(sample);
    };
    bridgeSession_->onRaw = [this](const RawEvent& event) { bridgeRaw(event); };
    if (benchController_) benchController_->onRaw = [this](const RawEvent& event) { bridgeRaw(event); };
    else ownedBridgeClient_->onRaw = [this](const RawEvent& event) { bridgeRaw(event); };
    bridgeOuterTx_.clear(); bridgeOuterRx_.clear(); bridgeInnerTx_.clear(); bridgeInnerRx_.clear();
    bridgeInnerRead_.clear(); bridgeInnerFormula_.clear(); bridgeInnerCheck_.clear(); bridgeDisplayedRequest_ = 0;
    bridgeLastUpdatedChannel_.reset();
    responderOuterTx_.clear(); responderOuterRx_.clear();
}
void MainWindow::bridgeRaw(const RawEvent& event) {
    if (!bridgeSelected()) return;
    const auto bytes = QString::fromStdString(dlc::hex(event.bytes));
    if (event.kind == "usb_tx") bridgeOuterTx_ = bytes;
    if (event.kind == "usb_rx") bridgeOuterRx_ = bytes;
    if (event.kind == "responder_usb_tx") responderOuterTx_ = bytes;
    if (event.kind == "responder_usb_rx") responderOuterRx_ = bytes;
    if (event.kind == "bridge_dlc_tx") {
        bridgeInnerTx_ = bytes; bridgeDisplayedRequest_ = event.request;
        bridgeInnerRead_ = QString::fromStdString(bridgeSession_->lastExchange().read);
        bridgeInnerFormula_ = QString::fromStdString(bridgeSession_->lastExchange().formulaSource);
    }
    if (event.kind == "bridge_dlc_rx") bridgeInnerRx_ = bytes;
    if (event.kind == "bridge_result" && event.bridge)
        bridgeInnerCheck_ = QStringLiteral("Статус мосту %1 · %2").arg(event.bridge->status).arg(event.bridge->status == 0 ? QStringLiteral("OK; очікування перевірки ПК") : QStringLiteral("помилка DLC"));
    if (recorder_.active()) recorder_.enqueueRaw(event);
}
void MainWindow::newBridgeExperiment() {
    if (benchStopPending_) return;
    if (!bridgeSelected() || !bridgeClient_->info()) return;
    if (benchController_ && !benchController_->responderInfo()) return;
    chart_->clear(); bridgeInnerTx_.clear(); bridgeInnerRx_.clear();
    bridgeInnerRead_.clear(); bridgeInnerFormula_.clear(); bridgeInnerCheck_.clear(); bridgeDisplayedRequest_ = 0;
    bridgeLastUpdatedChannel_.reset();
    // Client uses one raw sink. Once Session owns the experiment, route its
    // bridge events through Session metrics and then the same GUI/log sink.
    if (benchController_) benchController_->onRaw = {};
    else ownedBridgeClient_->onRaw = {};
    bridgeSession_->start(now()); refreshDashboard();
}
void MainWindow::applyBridgeFaults(dlc::Faults faults) {
    const QSignalBlocker silenceBlocker(bridgeSilence_), delayBlocker(bridgeDelay_), gapBlocker(bridgeGap_);
    bridgeSilence_->setChecked(faults.silent); bridgeDelay_->setValue(static_cast<int>(faults.delayMs)); bridgeGap_->setValue(static_cast<int>(faults.gapMs));
    bridgeFaults_ = faults;
    bridgeFaults_.corruptNext = bridgeFaults_.truncateNext = bridgeFaults_.wrongLengthNext = bridgeFaults_.noiseNext = bridgeFaults_.headerNext = bridgeFaults_.trailingNext = false;
    if (bridgeSession_) bridgeSession_->setFaults(faults);
}
void MainWindow::applyDlcFaults(dlc::Faults faults) {
    dlcSession_.setFaults(faults);
    if (recorder_.active()) {
        const auto detail = QStringLiteral("fixture_class=synthetic-fault; silent=%1; delay_ms=%2; checksum=%3; length=%4; truncate=%5; noise=%6")
            .arg(faults.silent).arg(faults.delayMs).arg(faults.corruptNext).arg(faults.wrongLengthNext).arg(faults.truncateNext).arg(faults.noiseNext);
        recorder_.enqueueRaw({now(), dlcSession_.id(), 0, "fault_configuration", detail.toStdString(), {}});
    }
}
void MainWindow::refreshPorts() {
    const auto selected = port_->currentData().toString();
    const auto selectedResponder = responderPort_->currentData().toString();
    const QSignalBlocker responderBlocker(responderPort_);
    responderPort_->clear(); responderPort_->addItem(QStringLiteral("Оберіть responder порт…"), QString{});
    port_->blockSignals(true); port_->clear();
    port_->addItem(QStringLiteral("Оберіть порт…"), QString{});
#ifdef HONDADASH_WITH_SERIAL
    for (const auto& info : QSerialPortInfo::availablePorts()) {
        port_->addItem(info.portName(), info.systemLocation());
        responderPort_->addItem(info.portName(), info.systemLocation());
        QString details = info.description().isEmpty() ? QStringLiteral("Без опису") : info.description();
        if (!info.manufacturer().isEmpty()) details += QStringLiteral(" · ") + info.manufacturer();
        details += QStringLiteral("\n") + info.systemLocation();
        if (info.hasVendorIdentifier()) details += QStringLiteral(" · VID %1").arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0'));
        if (info.hasProductIdentifier()) details += QStringLiteral(" · PID %1").arg(info.productIdentifier(), 4, 16, QLatin1Char('0'));
        if (!info.serialNumber().isEmpty()) details += QStringLiteral("\nS/N ") + info.serialNumber();
        details += QStringLiteral("\n115200 · 8N1 · без flow control");
        port_->setItemData(port_->count() - 1, details, Qt::ToolTipRole);
    }
#endif
    const int selectedIndex = selected.isEmpty() ? 0 : port_->findData(selected);
    port_->setCurrentIndex(std::max(0, selectedIndex)); port_->blockSignals(false);
    responderPort_->setCurrentIndex(std::max(0, responderPort_->findData(selectedResponder)));
    portDetails_->setText(port_->currentData(Qt::ToolTipRole).toString());
    refreshDashboard();
}
void MainWindow::changeSource() {
    recorder_.stop(); session_.stop(now()); dlcSession_.stop(now()); chart_->clear();
    rebuildBridge();
    SessionSettings settings;
#ifdef HONDADASH_WITH_SERIAL
    if (serialSelected()) { session_.setTransport(*serialTransport_); settings.bootDelayMs = 2000; }
    else
#endif
        session_.setTransport(memoryTransport_);
    session_.setSettings(settings);
    refreshDashboard();
}
void MainWindow::applyManual() {
    std::array<double, ChannelCount> values{};
    for (std::size_t i = 0; i < ChannelCount; ++i) values[i] = manual_[i]->value();
    session_.setManual(values);
}
void MainWindow::startSession() {
    if (bridgeSelected()) {
        if ((bridgeSerialSelected() || benchSelected()) && port_->currentData().toString().isEmpty()) return;
        if (benchSelected() && !bench::distinctPorts(responderPort_->currentData().toString().toStdString(), port_->currentData().toString().toStdString())) return;
        rebuildBridge();
#ifdef HONDADASH_WITH_SERIAL
        if (bridgeSerialSelected() || benchSelected()) serialTransport_->setPortName(port_->currentData().toString().toStdString());
        if (benchSelected()) responderTransport_->setPortName(responderPort_->currentData().toString().toStdString());
#endif
        chart_->clear();
        if (benchController_) benchController_->connect(now()); else ownedBridgeClient_->connect(now());
        refreshDashboard(); return;
    }
    if (dlcSelected()) { chart_->clear(); dlcSession_.start(now()); refreshDashboard(); return; }
    if (serialSelected() && port_->currentData().toString().isEmpty()) return;
    session_.stop(now());
#ifdef HONDADASH_WITH_SERIAL
    if (serialSelected()) serialTransport_->setPortName(port_->currentData().toString().toStdString());
#endif
    chart_->clear(); session_.start(now()); refreshDashboard();
}
void MainWindow::stopSession() {
    if (benchStopPending_) return;
    session_.stop(now()); dlcSession_.stop(now()); bridgeSession_->stop(now());
    if (benchController_ && benchController_->bridgeClient().info() && benchController_->responderInfo()) {
        benchStopPending_ = true; benchStopDeadline_ = now() + 2500;
    } else if (benchController_) benchController_->disconnect(now()); else ownedBridgeClient_->disconnect(now());
    refreshDashboard();
}
void MainWindow::updateCapabilities(const std::optional<DeviceInfo>& info) {
    const auto flags = info ? info->capabilities : serialSelected() ? 0 : CapabilityScenario | CapabilityManual | CapabilityFaults | CapabilityQuality;
    scenario_->setEnabled((flags & CapabilityScenario) != 0); seed_->setEnabled(scenario_->isEnabled());
    manualBox_->setEnabled((flags & CapabilityManual) != 0);
    for (QWidget* widget : std::array<QWidget*, 4>{silence_, delay_, corrupt_, truncate_}) widget->setEnabled((flags & CapabilityFaults) != 0);
    qualityChannel_->setEnabled((flags & CapabilityQuality) != 0); quality_->setEnabled(qualityChannel_->isEnabled());
    const auto explanation = QStringLiteral("Пристрій не оголосив підтримку цієї команди.");
    for (QWidget* widget : std::array<QWidget*, 9>{scenario_, seed_, manualBox_, silence_, delay_, corrupt_, truncate_, qualityChannel_, quality_})
        widget->setToolTip(widget->isEnabled() ? QString{} : explanation);
    if (!info && serialSelected()) capabilities_->setText(QStringLiteral("Порт ще не розпізнано. Команди доступні лише після HELLO + INFO."));
    else {
        QStringList unsupported;
        if ((flags & CapabilityScenario) == 0) unsupported.append(QStringLiteral("сценарії/seed"));
        if ((flags & CapabilityManual) == 0) unsupported.append(QStringLiteral("ручні значення"));
        if ((flags & CapabilityFaults) == 0) unsupported.append(QStringLiteral("несправності"));
        if ((flags & CapabilityQuality) == 0) unsupported.append(QStringLiteral("якість каналів"));
        capabilities_->setText(unsupported.isEmpty() ? QStringLiteral("Сценарії, ручні значення й несправності доступні.") : QStringLiteral("Не підтримується: ") + unsupported.join(QStringLiteral(", ")) + QStringLiteral("."));
    }
}
void MainWindow::refreshDashboard() {
    const auto currentTime = now(); const double elapsed = static_cast<double>(currentTime - lastPaint_); lastPaint_ = currentTime;
    const bool bridge = bridgeSelected();
    const bool honda = dlcSelected() || bridge;
    const auto& measurements = activeModel().channels();
    for (std::size_t i = 0; i < ChannelCount; ++i) {
        auto reading = measurements[i]; reading.value = activeModel().current(static_cast<Channel>(i), currentTime);
        if (i == 0) tachometer_->setReading(reading, elapsed); else cards_[i]->setReading(reading);
    }
    chart_->setNow(currentTime); sessionStatus_->setText(benchSelected() ? QStringLiteral("Стенд: ") + QString::fromUtf8(benchController_->stateName()) : bridge ? bridgeSessionName(bridgeClient_->state()) : honda ? dlcSessionName(dlcSession_.state()) : QStringLiteral("Сесія: ") + sessionName(session_.state()));
    sessionStatus_->setToolTip(QString::fromStdString(bridge ? bridgeClient_->error() + "; " + bridgeSession_->error() : honda ? dlcSession_.error() : session_.error()));
    const bool running = bridge ? bridgeSession_->state() == dlc::State::Polling && bridgeClient_->state() != bridge::State::Faulted : honda ? dlcSession_.state() == dlc::State::Polling : session_.state() == SessionState::Running;
    sessionStatus_->setStyleSheet(running ? QStringLiteral("color:#51d3ba;") : QStringLiteral("color:#ffca79;"));
    const auto& stats = session_.stats(); std::optional<Time> newest;
    for (const auto& reading : measurements) if (reading.lastValid && (!newest || *reading.lastValid > *newest)) newest = reading.lastValid;
    const auto age = newest ? QString::number(currentTime - *newest) + QStringLiteral(" мс") : QStringLiteral("—");
    const auto error = QString::fromUtf8(recorder_.error().c_str());
    const auto recordState = !error.isEmpty() ? QStringLiteral("ПОМИЛКА") : recorder_.active() ? QStringLiteral("ЗАПИС") : QStringLiteral("зупинено");
    statistics_->setText(QStringLiteral("%1 відп./с · Давність: %2 · Тайм-аути: %3 · Пошкоджені: %4 · Прийнято: %5 · Журнал: %6")
        .arg(stats.responseHz, 0, 'f', 1).arg(age).arg(stats.timeouts).arg(stats.corrupt).arg(stats.accepted).arg(recordState));
    statistics_->setToolTip({});
    const bool bridgeDisconnected = benchController_ ? benchController_->state() == bench::State::Disconnected : bridgeClient_->state() == bridge::State::Disconnected;
    const bool stopped = bridge ? bridgeDisconnected : honda ? dlcSession_.state() == dlc::State::Stopped || dlcSession_.state() == dlc::State::Faulted : session_.state() == SessionState::Stopped || session_.state() == SessionState::Faulted;
    const bool usb = serialSelected() || bridgeSerialSelected() || benchSelected();
    source_->setEnabled(stopped); port_->setEnabled(stopped); refreshPorts_->setEnabled(stopped);
    bridgeBackend_->setVisible(bridge); bridgeBackend_->setEnabled(stopped);
    port_->setVisible(usb); refreshPorts_->setVisible(usb); portDetails_->setVisible(usb);
    for (QWidget* widget : std::array<QWidget*, 4>{responderPort_, benchPortsHint_, benchPeerInfo_, benchPeerHex_}) widget->setVisible(benchSelected());
    responderPort_->setEnabled(stopped);
    start_->setText(bridge ? QStringLiteral("Старт / handshake") : usb ? QStringLiteral("Підключити") : QStringLiteral("Старт"));
    stop_->setText(usb ? QStringLiteral("Від’єднати") : QStringLiteral("Стоп"));
    const bool twoPorts = bench::distinctPorts(port_->currentData().toString().toStdString(), responderPort_->currentData().toString().toStdString());
    start_->setEnabled(stopped && (!usb || !port_->currentData().toString().isEmpty()) && (!benchSelected() || twoPorts));
    stop_->setEnabled(!benchStopPending_ && (bridge ? !bridgeDisconnected : honda ? dlcSession_.state() != dlc::State::Stopped : session_.state() != SessionState::Stopped));
    bridgeNewExperiment_->setVisible(bridge);
    const bool benchReady = benchController_ && benchController_->responderInfo() && (benchController_->state() == bench::State::Ready || benchController_->state() == bench::State::Faulted);
    bridgeNewExperiment_->setEnabled(!benchStopPending_ && bridge && bridgeClient_->info().has_value() && (benchSelected() ? benchReady : (bridgeClient_->state() == bridge::State::Ready || bridgeClient_->state() == bridge::State::Faulted)));
    scenario_->setVisible(!honda); demoNotice_->setVisible(!honda); seed_->setVisible(!honda); seedLabel_->setVisible(!honda);
    manualBox_->setVisible(!honda && scenario_->currentIndex() == 3); faultBox_->setVisible(!honda); dlcBox_->setVisible(dlcSelected()); bridgeBox_->setVisible(bridge);
    dlcProfile_->setEnabled(stopped);
    warning_->setText(benchSelected() ? QStringLiteral("СТЕНД: ДВІ NANO — ЕМУЛЯТОР ECU — НЕ ПІДКЛЮЧАТИ ДО АВТОМОБІЛЯ") : bridge ? QStringLiteral("ТЕСТОВИЙ МІСТ — ВІРТУАЛЬНИЙ ECU — ФІЗИЧНИЙ DLC ВИМКНЕНО") : honda ? QStringLiteral("ЛАБОРАТОРНА ЕМУЛЯЦІЯ HONDA DLC — ECU НЕ ПІДКЛЮЧЕНО") : usb ? QStringLiteral("ЕМУЛЯЦІЯ НА ПРИСТРОЇ — ECU НЕ ПІДКЛЮЧЕНО") : QStringLiteral("ЕМУЛЯЦІЯ — не підключено до автомобіля"));
    if (offscreenScreenshot_) warning_->setText(warning_->text() + QStringLiteral(" · знімок offscreen (без звичайного GUI)"));
    const auto& info = session_.deviceInfo();
    QString identity = info ? QStringLiteral("%1 · firmware %2").arg(QString::fromStdString(info->endpoint), QString::fromStdString(info->firmware)) : usb ? QStringLiteral("Тестовий пристрій не розпізнано") : QStringLiteral("Синтетичний профіль у пам’яті ПК");
    if (session_.state() == SessionState::Running) identity += QStringLiteral("\nПристрій розпізнано; свіжість показників — унизу.");
    if (!session_.error().empty()) identity += QStringLiteral("\nПОМИЛКА: ") + QString::fromStdString(session_.error());
    deviceInfo_->setText(identity);
    deviceInfo_->setStyleSheet(session_.error().empty() ? QStringLiteral("color:#91a5ba;") : QStringLiteral("color:#ff7e7e;"));
    if (bridge) refreshBridgeDetails(); else if (honda) refreshDlcDetails(); else updateCapabilities(info);
    QString recordingText = QStringLiteral("Журнал: ") + QString::fromUtf8(recorder_.status().c_str());
    if (recorder_.active()) {
        const auto directory = recorder_.directory();
        if (!directory.empty()) recordingText += QStringLiteral("\n") + pathString(directory);
    }
    recordStatus_->setText(error.isEmpty() ? recordingText : QStringLiteral("ПОМИЛКА ЗАПИСУ: ") + error);
    recordStatus_->setStyleSheet(error.isEmpty() ? QStringLiteral("color:#91a5ba;") : QStringLiteral("color:#ff7e7e;"));
    record_->setText(recorder_.active() ? QStringLiteral("Зупинити запис") : QStringLiteral("Почати запис CSV + JSONL"));
}
void MainWindow::refreshDlcDetails() {
    QString identity = QStringLiteral("Програмний відповідач · COM недоступний");
    if (dlcSession_.stats().accepted == 0) identity += QStringLiteral("\nІніціалізація не підтверджує відповідь ECU.");
    if (!dlcSession_.error().empty()) identity += QStringLiteral("\nПОМИЛКА: ") + QString::fromStdString(dlcSession_.error());
    deviceInfo_->setText(identity); deviceInfo_->setStyleSheet(dlcSession_.error().empty() ? QStringLiteral("color:#91a5ba;") : QStringLiteral("color:#ff7e7e;"));
    capabilities_->setText(QStringLiteral("Профіль за відкритим референсом; на обладнанні не перевірено. Valid означає коректність лабораторного читання."));
    const auto& exchange = dlcSession_.lastExchange();
    const auto validation = exchange.check.find("checksum OK") != std::string::npos ? QStringLiteral("Заголовок, довжина, checksum: OK") : QString::fromStdString(exchange.check);
    auto formula = QString::fromStdString(exchange.formulaSource);
    formula.replace(QStringLiteral(" | "), QStringLiteral("\n"));
    formula.replace(QStringLiteral("@"), QStringLiteral("\n@"));
    dlcExchange_->setText(QStringLiteral("TX: %1\nRX: %2\nЧитання: %3\nПеревірка: %4\nФормула: %5")
        .arg(QString::fromStdString(exchange.requestHex), QString::fromStdString(exchange.responseHex), QString::fromStdString(exchange.read), validation, formula));
    dlcExchange_->setToolTip(QString::fromStdString(exchange.formulaSource));
    dlcExchange_->setMinimumHeight(dlcExchange_->heightForWidth(240));
    const auto& stats = dlcSession_.stats();
    const auto& channels = dlcSession_.model().channels();
    const auto age = [&](Channel channel) { const auto& reading = channels[channelIndex(channel)]; return reading.lastValid ? QString::number(now() - *reading.lastValid) + QStringLiteral(" мс") : QStringLiteral("—"); };
    statistics_->setText(QStringLiteral("RPM %1 Гц / %2 · ECT %3 Гц / %4 · TPS %5 Гц / %6 · Тайм-аути %7 · Пошкоджені %8")
        .arg(stats.channelHz[0], 0, 'f', 1).arg(age(Channel::Rpm)).arg(stats.channelHz[2], 0, 'f', 1).arg(age(Channel::Coolant)).arg(stats.channelHz[4], 0, 'f', 1).arg(age(Channel::Throttle)).arg(stats.timeouts).arg(stats.corrupt));
}
void MainWindow::refreshBridgeDetails() {
    const auto& info = bridgeClient_->info();
    QString identity = info ? QString::fromStdString(info->identity) + (benchSelected() ? QStringLiteral("\nGPIO стенда · зовнішній responder") : QStringLiteral("\nbackend=virtual · фізичний DLC вимкнено")) : QStringLiteral("Bridge endpoint не розпізнано");
    if (info) identity += QStringLiteral("\nFirmware %1 · bridge v%2\nRead-policy v%3 · покоління %4").arg(QString::fromStdString(info->firmware)).arg(info->protocolVersion).arg(info->policyVersion).arg(info->generation);
    if (bridgeSession_->stats().accepted) identity += QStringLiteral("\nОтримано коректне DLC-читання.");
    else identity += QStringLiteral("\nІніціалізація не розпізнає ECU.");
    const auto error = benchController_ && !benchController_->error().empty() ? benchController_->error() : !bridgeClient_->error().empty() ? bridgeClient_->error() : bridgeSession_->error();
    if (!error.empty()) identity += QStringLiteral("\nОБМІН ЗАБЛОКОВАНО: ") + QString::fromStdString(error);
    if (benchSelected() && !benchStopError_.isEmpty()) identity += QStringLiteral("\n") + benchStopError_;
    deviceInfo_->setText(identity); deviceInfo_->setStyleSheet(error.empty() ? QStringLiteral("color:#91a5ba;") : QStringLiteral("color:#ff7e7e;"));
    capabilities_->setText(benchSelected() ? QStringLiteral("Дві firmware identity; дані simulation/reference-derived. Identity не є електричним доказом. Новий експеримент узгоджує обидві плати; фізичні рівні й обмін ще потребують перевірки.") : QStringLiteral("USB handshake перевіряє протокол мосту. Це не підтвердження Nano чи ECU. Початок читань — окремою кнопкою нового експерименту."));
    if (benchController_) {
        const auto& peer = benchController_->responderInfo();
        benchPeerInfo_->setText(peer ? QStringLiteral("Nano №2 · %1\nFirmware %2 · protocol/policy %3/%4\nGeneration %5").arg(QString::fromStdString(peer->identity), QString::fromStdString(peer->firmware)).arg(peer->protocolVersion).arg(peer->policyVersion).arg(peer->generation) : QStringLiteral("Nano №2 · responder не розпізнано"));
        benchPeerHex_->setText(QStringLiteral("USB responder · лише керування\nTX: %1\nRX: %2").arg(responderOuterTx_, responderOuterRx_));
    }
    bridgeScenario_->setEnabled(info.has_value());
    for (QWidget* widget : std::array<QWidget*, 8>{bridgeSilence_, bridgeDelay_, bridgeGap_, bridgeCorrupt_, bridgeTruncate_, bridgeLength_, bridgeHeader_, bridgeTrailing_}) widget->setEnabled(info.has_value());
    bridgeOuterHex_->setText(QStringLiteral("USB bridge frame · host\nTX: %1\nRX (останній фрагмент): %2").arg(bridgeOuterTx_, bridgeOuterRx_));
    bridgeInnerHex_->setText(QStringLiteral("DLC · повідомлено мостом\nОстанній завершений result\nTX: %1\nRX: %2\nЧитання: %3\nПеревірка: %4\nФормула: %5")
        .arg(bridgeInnerTx_, bridgeInnerRx_, bridgeInnerRead_, bridgeInnerCheck_, bridgeInnerFormula_));
    bridgeInnerHex_->setToolTip(bridgeInnerFormula_);
    const auto& stats = bridgeSession_->stats();
    const auto& metrics = bridgeSession_->metrics();
    const auto& channels = bridgeSession_->model().channels();
    const auto milliseconds = [](std::optional<Time> value) { return value ? QString::number(*value) + QStringLiteral(" мс") : QStringLiteral("—"); };
    const auto age = [&](Channel channel) { const auto& reading = channels[channelIndex(channel)]; return milliseconds(reading.lastValid ? std::optional<Time>(now() - *reading.lastValid) : std::nullopt); };
    const auto achieved = [&](Channel channel) { const auto index = channelIndex(channel); return metrics.channels[index].accepted >= 2 ? QString::number(stats.channelHz[index], 'f', 2) : QStringLiteral("—"); };
    const auto requested = dlc::bridgeRequestedIntervals();
    const auto distribution = [](const QString& name, const dlc::BoundedDistribution& values) {
        const auto summary = values.summary();
        const auto value = [](std::optional<Time> n) { return n ? QString::number(*n) : QStringLiteral("—"); };
        return QStringLiteral("%1 · n=%2 / усього %3\nmin / median / p95 / max:\n%4 / %5 / %6 / %7 мс")
            .arg(name).arg(summary.n).arg(summary.observations).arg(value(summary.min), value(summary.median), value(summary.p95), value(summary.max));
    };
    bridgePolicy_->setText(QStringLiteral("План: %1\nСвіжість: %2\nФакт: вікно до 10 с; «—» — мало даних.")
        .arg(QString::fromUtf8(dlc::BridgeSchedulerPolicy), QString::fromUtf8(dlc::BridgeFreshnessPolicy)));
    bridgePolicy_->setToolTip(QStringLiteral("Політика HondaDash для лабораторного мосту. Застосовується на межі нового експерименту. Давність — від початку host-запиту, не час вимірювання фізичного датчика. На рівності deadline значення ще видиме; Stale/приховування — після перевищення порога. Анімація приладу не є новими вимірюваннями."));
    QStringList diagnosticRows;
    QStringList channelDistributions;
    for (const auto channel : {Channel::Rpm, Channel::Coolant, Channel::Throttle}) {
        const auto index = channelIndex(channel);
        const auto threshold = bridgeSession_->model().freshness().effective(channel);
        const auto name = channel == Channel::Rpm ? QStringLiteral("RPM") : channel == Channel::Coolant ? QStringLiteral("ECT") : QStringLiteral("TPS");
        const auto& metric = metrics.channels[index];
        const auto state = QString::fromUtf8(hd::qualityName(channels[index].quality));
        diagnosticRows.append(QStringLiteral("%1 · %2 · факт %3 / план %4 Гц\nДавність %5 · Stale >%6 мс\nΔоновл. %7 · прихов. >%8 мс")
            .arg(name, state, achieved(channel), QString::number(1000.0 / static_cast<double>(requested[index]), 'f', 2), age(channel))
            .arg(threshold.staleMs).arg(milliseconds(metric.lastUpdateIntervalMs)).arg(threshold.hideMs));
        channelDistributions.append(distribution(name + QStringLiteral(" · інтервали оновлень"), metric.updateIntervals));
    }
    bridgeChannelDiagnostics_->setText(diagnosticRows.join(QStringLiteral("\n\n")));
    bridgeChannelDiagnostics_->setToolTip(channelDistributions.join(QStringLiteral("\n\n")) + QStringLiteral("\nОстанні ≤256 інтервалів; nearest-rank для median і p95. «—» означає n=0."));
    const auto& timing = bridgeClient_->diagnostics();
    QString acceptedTiming = QStringLiteral("Прийняте читання: ще немає даних.");
    if (bridgeLastUpdatedChannel_) {
        const auto& latest = metrics.channels[channelIndex(*bridgeLastUpdatedChannel_)];
        const auto name = *bridgeLastUpdatedChannel_ == Channel::Rpm ? QStringLiteral("RPM") : *bridgeLastUpdatedChannel_ == Channel::Coolant ? QStringLiteral("ECT") : QStringLiteral("TPS");
        acceptedTiming = QStringLiteral("Прийняте %1 · час ПК\nЗапит → result: %2\nПриймання → модель: %3\n(за годинником сесії, не CPU час)")
            .arg(name, milliseconds(latest.lastRequestToResultMs), milliseconds(latest.lastModelUpdateDelayMs));
    }
    const auto reportedTiming = timing.operation ? QStringLiteral("Firmware · останній result #%1\nОперація TX + завершення: %2 мс\nTX %3 мс · до рішення %4 мс\nНайбільша пауза RX %5 мс")
        .arg(timing.operation).arg(static_cast<unsigned>(timing.txElapsedMs) + timing.rxElapsedMs)
        .arg(timing.txElapsedMs).arg(timing.rxElapsedMs).arg(timing.maxGapMs) : QStringLiteral("Firmware: result ще не отримано.");
    bridgeTiming_->setText(acceptedTiming + QStringLiteral("\n\n") + reportedTiming +
        QStringLiteral("\nObservation window читання: 200 мс\nЧас до valid DLC payload: не вимірюється.\nЧас ПК і пристрою не синхронізовано.\n\n") +
        distribution(QStringLiteral("ПК · прийняті читання"), metrics.requestToResult) + QStringLiteral("\n\n") +
        distribution(QStringLiteral("Firmware · завершені читання"), metrics.firmwareOperation) +
        QStringLiteral("\nОстанні ≤256; nearest-rank.\n«—» — ще немає вибірок."));
    bridgeTiming_->setToolTip(QStringLiteral("Firmware передає відносний час до terminal decision, який містить observation window. Час першої повної коректної відповіді протокол не передає. 0 мс до моделі означає той самий tick керованого годинника, не виміряний CPU час декодування.") + QStringLiteral("\n") + QString::fromStdString(timing.summary));
    for (auto* trace : {bridgeOuterHex_, bridgeInnerHex_, bridgeTiming_, bridgePolicy_, bridgeChannelDiagnostics_}) trace->setMinimumHeight(trace->heightForWidth(240));
    statistics_->setText(QStringLiteral("Σ %1 транз./с · RPM %2/%3 · ECT %4/%5 · TPS %6/%7 · %8")
        .arg(stats.accepted >= 2 ? QString::number(stats.responseHz, 'f', 2) : QStringLiteral("—")).arg(achieved(Channel::Rpm) + QStringLiteral("Гц"), age(Channel::Rpm).remove(QLatin1Char(' ')), achieved(Channel::Coolant) + QStringLiteral("Гц"), age(Channel::Coolant).remove(QLatin1Char(' ')), achieved(Channel::Throttle) + QStringLiteral("Гц"), age(Channel::Throttle).remove(QLatin1Char(' ')), recorder_.active() ? QStringLiteral("ЗАПИС") : QStringLiteral("запис вимкн.")));
    statistics_->setToolTip(QStringLiteral("Сумарні завершені read-транзакції та частота окремих каналів; обмежене вікно до 10 с. Кожне читання оновлює тільки один канал. Дані simulation; для стенда байти надходять від зовнішнього responder через GPIO."));
}
RecordingMetadata MainWindow::recordingMetadata() const {
    if (bridgeSelected()) {
        RecordingMetadata metadata{dlc::scenarioId(static_cast<dlc::Scenario>(bridgeScenario_->currentIndex())), 0};
        metadata.formatVersion = 3; metadata.wireProtocol = "honda-dlc"; metadata.profile = "honda-dlc-kerpz-obd1-reference-v1";
        metadata.profileVersion = 1; metadata.evidenceStatus = "reference-derived; hardware-unverified"; metadata.fixtureClass = "reference-derived";
        metadata.fixtureId = metadata.scenario; metadata.transport = bridgeSerialSelected() ? "serial" : "in-memory-embedded-bridge";
        metadata.outerProtocol = "hondadash-dlc-bridge-lab-v1"; metadata.backend = "virtual"; metadata.physicalDlcEnabled = false;
        metadata.hardwareVerified = false; metadata.liveEnabled = false; metadata.firmware.clear();
        metadata.freshness = bridgeSession_->model().freshness();
        metadata.schedulerPolicy = dlc::BridgeSchedulerPolicy;
        metadata.schedulerPolicyVersion = dlc::BridgeSchedulerVersion;
        metadata.requestedIntervals = dlc::bridgeRequestedIntervals();
        metadata.freshnessPolicy = dlc::BridgeFreshnessPolicy;
        metadata.freshnessPolicyVersion = dlc::BridgeFreshnessVersion;
        metadata.timingEstimateSource = "host_request_start_lower_bound; firmware_relative_terminal_durations";
        metadata.measurementScope = bridgeSerialSelected() ? "serial_virtual_bridge; physical_device_unverified" : "native_virtual_bridge";
        metadata.endpoint = "unrecognized";
        if (bridgeClient_->info()) {
            const auto& info = *bridgeClient_->info();
            metadata.endpoint = info.identity; metadata.bridgeIdentity = info.identity;
            metadata.firmware = info.firmware;
            metadata.bridgeVersion = info.protocolVersion; metadata.readPolicyVersion = info.policyVersion;
        }
        if (bridgeSerialSelected() || benchSelected()) { metadata.port = port_->currentData().toString().toStdString(); metadata.baud = 115200; }
        if (benchSelected()) {
            metadata.outerProtocol = "hondadash-dlc-bridge-bench-v1";
            metadata.backend = "two-nano-bench"; metadata.transport = "serial-two-port";
            metadata.benchSchemaVersion = 1; metadata.benchIoEnabled = true;
            metadata.vehicleConnectionAllowed = false;
            metadata.firmwareTarget = "bridge-bench + responder-bench";
            metadata.responderPort = responderPort_->currentData().toString().toStdString();
            metadata.measurementScope = "two_nano_bench; MCU_reported_line_events; physical_electrical_verification_NOT_VERIFIED";
            metadata.responderIdentity = "unrecognized";
            if (benchController_->responderInfo()) {
                const auto& peer = *benchController_->responderInfo();
                metadata.responderIdentity = peer.identity; metadata.responderFirmware = peer.firmware;
                metadata.responderProtocolVersion = peer.protocolVersion;
                metadata.responderReadPolicyVersion = peer.policyVersion;
            }
        }
        return metadata;
    }
    if (dlcSelected()) {
        RecordingMetadata metadata{dlc::scenarioId(static_cast<dlc::Scenario>(dlcScenario_->currentIndex())), 0};
        metadata.formatVersion = 3; metadata.wireProtocol = "honda-dlc"; metadata.profile = "honda-dlc-kerpz-obd1-reference-v1";
        metadata.profileVersion = 1; metadata.evidenceStatus = "reference-derived; hardware-unverified"; metadata.fixtureClass = "reference-derived";
        metadata.fixtureId = metadata.scenario; metadata.transport = "offline-in-memory"; metadata.endpoint = "scripted-honda-ecu"; metadata.firmware.clear();
        metadata.hardwareVerified = false; metadata.liveEnabled = false;
        return metadata;
    }
    static constexpr std::array<const char*, 4> scenarios{"ignition", "idle", "demo", "manual"};
    RecordingMetadata metadata{scenarios[static_cast<std::size_t>(scenario_->currentIndex())], static_cast<std::uint32_t>(seed_->value())};
    metadata.transport = serialSelected() ? "serial" : "in-memory";
    if (session_.deviceInfo()) { metadata.endpoint = session_.deviceInfo()->endpoint; metadata.firmware = session_.deviceInfo()->firmware; }
    else if (serialSelected()) { metadata.endpoint = "unrecognized"; metadata.firmware.clear(); }
    if (serialSelected()) { metadata.port = port_->currentData().toString().toStdString(); metadata.baud = 115200; }
    return metadata;
}
void MainWindow::toggleRecording() {
    if (recorder_.active()) recorder_.stop();
    else {
        const auto directory = QFileDialog::getExistingDirectory(this, QStringLiteral("Папка для журналів HondaDash"), QDir::homePath());
        if (directory.isEmpty()) return;
        startRecording(filePath(directory));
    }
    refreshDashboard();
}
bool MainWindow::startRecording(const std::filesystem::path& directory) {
    if (!recorder_.start(directory, recordingMetadata())) return false;
    // A mid-session log declares profile availability without re-emitting old
    // values. lastValid begins only when a subsequent real partial read arrives.
    if (bridgeSelected()) return recorder_.enqueueSample(dlc::unavailable(now(), bridgeSession_->id()));
    if (dlcSelected()) return recorder_.enqueueSample(dlc::unavailable(now(), dlcSession_.id()));
    return true;
}
void MainWindow::advance(Time amount) {
    const Time end = managedNow_ + amount;
    // Preserve every 10 ms executor tick and refresh at every assertion boundary.
    // The long history check needs no more than 20 simulated paints per second;
    // normal interactive rendering still uses the 16 ms Qt timer.
    while (managedNow_ < end) {
        managedNow_ = std::min(end, managedNow_ + 10); tickSessions();
        if (managedNow_ == end || managedNow_ - lastPaint_ >= 50) refreshDashboard();
    }
}
void MainWindow::markOffscreenScreenshot() {
    offscreenScreenshot_ = true; refreshDashboard();
}
void MainWindow::settleControlLayout() {
    // Managed smoke advances no Qt event loop. Apply the real layouts directly
    // so wrapped text and a newly needed scrollbar are represented in its PNGs.
    layout()->activate();
    centralWidget()->layout()->activate();
    dlcBox_->layout()->invalidate();
    dlcBox_->layout()->activate();
    for (int pass = 0; pass < 2; ++pass) {
        const int width = std::min(controlPanel_->maximumWidth(), controlScroll_->viewport()->width());
        auto* layout = controlPanel_->layout(); layout->invalidate();
        controlPanel_->resize(width, layout->totalHeightForWidth(width));
        layout->activate();
        dlcBox_->layout()->activate();
    }
}
QJsonObject MainWindow::smokeTest(const QString& screenshotPath) {
    QJsonArray checks; bool passed = true;
    QJsonArray layoutGeometry;
    QString screenshotUsb;
    QJsonArray dlcScreenshots;
    const auto check = [&](const QString& name, bool success) { checks.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("passed"), success}}); passed = passed && success; };
    check(QStringLiteral("managed_clock_enabled"), managedTime_);
    QFontMetrics metrics(QFont(QStringLiteral("Segoe UI"), 11));
    bool readableGlyphs = true;
    for (const QChar character : QStringLiteral("HondaDash0123456789ЕМУЛЯЦІЯЇЄҐобхв")) readableGlyphs = readableGlyphs && metrics.inFont(character);
    check(QStringLiteral("font_supports_dashboard_text"), readableGlyphs);
    check(QStringLiteral("builtin_warning_always_visible"), warning_->text().contains(QStringLiteral("ЕМУЛЯЦІЯ — не підключено до автомобіля")));
    check(QStringLiteral("M0_M1_original_freshness_policy_preserved"), session_.model().freshness().effective(Channel::Rpm).staleMs == 1000 && session_.model().freshness().effective(Channel::Coolant).hideMs == 3000);
#ifdef HONDADASH_WITH_SERIAL
    source_->setCurrentIndex(1);
    check(QStringLiteral("usb_selection_requires_explicit_port_and_handshake"), session_.state() == SessionState::Stopped && !session_.deviceInfo() && !start_->isEnabled() && !scenario_->isEnabled() && deviceInfo_->text().contains(QStringLiteral("не розпізнано")));
    check(QStringLiteral("usb_emulation_warning_visible"), warning_->text().contains(QStringLiteral("ЕМУЛЯЦІЯ НА ПРИСТРОЇ — ECU НЕ ПІДКЛЮЧЕНО")));
    refreshPorts_->click();
    check(QStringLiteral("port_refresh_keeps_transport_closed_and_no_automatic_selection"), session_.state() == SessionState::Stopped && port_->currentData().toString().isEmpty() && !start_->isEnabled());
    port_->addItem(QStringLiteral("GUI smoke · відсутній порт"), QStringLiteral("HondaDash_SMOKE_NONEXISTENT_PORT_91C7EBD4"));
    port_->setCurrentIndex(port_->count() - 1); start_->click(); advance(100);
    check(QStringLiteral("missing_port_reports_error_without_recognizing_device"), session_.state() == SessionState::Faulted && !session_.error().empty() && !session_.deviceInfo() && !tachometer_->reading().value);
    if (!screenshotPath.isEmpty()) {
        const QFileInfo screenshotInfo(screenshotPath);
        screenshotUsb = screenshotInfo.dir().filePath(screenshotInfo.completeBaseName() + QStringLiteral("-usb-unconnected.png"));
        settleControlLayout();
        check(QStringLiteral("usb_unconnected_screenshot_saved"), grab().save(screenshotUsb));
    }
    stop_->click();
    check(QStringLiteral("disconnect_stops_usb_attempt"), session_.state() == SessionState::Stopped);
    source_->setCurrentIndex(0);
    check(QStringLiteral("source_switch_returns_to_builtin_controls"), !serialSelected() && scenario_->isEnabled() && start_->isEnabled());
#endif
    scenario_->setCurrentIndex(3); start_->click(); advance(500);
    check(QStringLiteral("decoded_data_in_real_widgets"), session_.state() == SessionState::Running && tachometer_->reading().quality == Quality::Valid && tachometer_->reading().value.has_value() && chart_->sampleCount() > 0);
    check(QStringLiteral("firmware_identity_requires_info_and_source_is_locked_while_running"), session_.deviceInfo().has_value() && !deviceInfo_->text().contains(QStringLiteral("не розпізнано")) && !source_->isEnabled());
    stop_->click(); memoryTransport_.emulator().setCapabilities(CapabilityScenario); start_->click(); advance(500);
    check(QStringLiteral("unsupported_controls_disabled_with_explanation"), scenario_->isEnabled() && !manualBox_->isEnabled() && !silence_->isEnabled() && !quality_->isEnabled() && capabilities_->text().contains(QStringLiteral("Не підтримується")) && !manualBox_->toolTip().isEmpty());
    stop_->click(); memoryTransport_.emulator().setCapabilities(CapabilityScenario | CapabilityManual | CapabilityFaults | CapabilityQuality); start_->click(); advance(500);
    bool allChannelsPresented = true;
    for (std::size_t i = 0; i < ChannelCount; ++i) {
        const auto& displayed = i == 0 ? tachometer_->reading() : cards_[i]->reading();
        allChannelsPresented = allChannelsPresented && displayed.quality == Quality::Valid && displayed.value == session_.model().current(static_cast<Channel>(i), now());
    }
    check(QStringLiteral("all_seven_decoded_channels_presented"), allChannelsPresented);
    const auto before = tachometer_->reading().value;
    manual_[0]->setValue(2800);
    check(QStringLiteral("manual_change_does_not_bypass_byte_response"), tachometer_->reading().value == before && session_.model().current(Channel::Rpm, now()) == before);
    advance(300);
    check(QStringLiteral("manual_rpm_after_byte_response"), tachometer_->reading().value && std::abs(*tachometer_->reading().value - 2800) < .1);
    silence_->setChecked(true); advance(1400);
    check(QStringLiteral("silence_marks_stale_and_times_out"), tachometer_->reading().quality == Quality::Stale && session_.stats().timeouts > 0);
    advance(2300);
    check(QStringLiteral("old_value_hidden"), !tachometer_->reading().value && tachometer_->reading().quality == Quality::Stale);
    silence_->setChecked(false); advance(800);
    check(QStringLiteral("restored_byte_response_updates_widgets"), tachometer_->reading().quality == Quality::Valid && tachometer_->reading().value && *tachometer_->reading().value == 2800);
    const auto samplesBeforeStop = session_.stats().accepted;
    for (int i = 0; i < 5; ++i) { stop_->click(); start_->click(); advance(300); stop_->click(); }
    check(QStringLiteral("repeated_start_stop_cancels_deliveries"), session_.state() == SessionState::Stopped && memoryTransport_.pendingDeliveries() == 0);
    scenario_->setCurrentIndex(1); start_->click(); advance(1000);
    advance(70000);
    check(QStringLiteral("history_is_bounded_after_long_managed_run"), chart_->sampleCount() == HistoryChart::Capacity && session_.stats().accepted > HistoryChart::Capacity);
    QString screenshot1024;
    for (const QSize size : {QSize(1024, 600), QSize(1280, 720)}) {
        resize(size); settleControlLayout();
        layoutGeometry.append(QJsonObject{{QStringLiteral("requested_width"), size.width()}, {QStringLiteral("requested_height"), size.height()},
            {QStringLiteral("central_width"), centralWidget()->width()}, {QStringLiteral("central_height"), centralWidget()->height()},
            {QStringLiteral("tachometer_width"), tachometer_->width()}, {QStringLiteral("tachometer_height"), tachometer_->height()},
            {QStringLiteral("chart_height"), chart_->height()}, {QStringLiteral("controls_width"), controlPanel_->width()},
            {QStringLiteral("viewport_width"), controlScroll_->viewport()->width()}});
        check(QStringLiteral("layout_%1x%2").arg(size.width()).arg(size.height()), centralWidget()->width() <= size.width() && centralWidget()->height() <= size.height() && tachometer_->width() >= 240 && tachometer_->height() >= 245 && chart_->height() >= 118 && controlPanel_->width() <= controlScroll_->viewport()->width());
        if (size.width() == 1024 && !screenshotPath.isEmpty()) {
            const QFileInfo screenshotInfo(screenshotPath);
            screenshot1024 = screenshotInfo.dir().filePath(screenshotInfo.completeBaseName() + QStringLiteral("-1024x600.png"));
            check(QStringLiteral("actual_widget_screenshot_1024_saved"), grab().save(screenshot1024));
        }
    }
    QKeyEvent fullscreenEvent(QEvent::KeyPress, Qt::Key_F11, Qt::NoModifier);
    QApplication::sendEvent(this, &fullscreenEvent);
    check(QStringLiteral("F11_enters_fullscreen"), isFullScreen());
    QKeyEvent escapeEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(this, &escapeEvent);
    check(QStringLiteral("Escape_leaves_fullscreen"), !isFullScreen());
    QTemporaryDir recordingTemporaryDirectory;
    const auto recordingParent = recordingTemporaryDirectory.path() + QStringLiteral("/перевірка журналу з пробілами");
    const bool recordingStarted = recordingTemporaryDirectory.isValid() && recorder_.start(filePath(recordingParent), recordingMetadata());
    check(QStringLiteral("recording_started_with_unicode_space_path"), recordingStarted);
    advance(400);
    check(QStringLiteral("active_recording_visible_in_dashboard"), recorder_.active() && statistics_->text().contains(QStringLiteral("Журнал: ЗАПИС")));
    bool saved = false;
    if (!screenshotPath.isEmpty()) { settleControlLayout(); saved = grab().save(screenshotPath); }
    if (!screenshotPath.isEmpty()) check(QStringLiteral("actual_widget_screenshot_saved"), saved);
    const auto accepted = session_.stats().accepted;
    close();
    check(QStringLiteral("close_stops_session_and_recording"), session_.state() == SessionState::Stopped && memoryTransport_.pendingDeliveries() == 0 && !timer_->isActive() && !recorder_.active());
    QFile csv(pathString(recorder_.directory() / "measurements.csv"));
    QFile raw(pathString(recorder_.directory() / "raw.jsonl"));
    const bool csvOpened = csv.open(QIODevice::ReadOnly), rawOpened = raw.open(QIODevice::ReadOnly);
    const QByteArray csvBytes = csvOpened ? csv.readAll() : QByteArray{};
    const QByteArray rawBytes = rawOpened ? raw.readAll() : QByteArray{};
    csv.close(); raw.close();
    check(QStringLiteral("GUI_session_callbacks_record_decoded_csv_and_raw_bytes"), recorder_.error().empty() && csvBytes.contains(",Valid") && csvBytes.count('\n') >= 3 && rawBytes.contains("\"kind\":\"TX\"") && rawBytes.contains("\"kind\":\"RX\"") && rawBytes.contains("\"kind\":\"stop\""));
    check(QStringLiteral("recording_identifies_transport_endpoint_and_firmware"), rawBytes.contains("\"transport\":\"in-memory\"") && rawBytes.contains("\"endpoint\":") && rawBytes.contains("\"firmware\":"));
    show(); source_->setCurrentIndex(source_->findData(2));
    check(QStringLiteral("direct_offline_freshness_keeps_original_policy"), dlcSession_.model().freshness().effective(Channel::Coolant).staleMs == 1000 && dlcSession_.model().freshness().effective(Channel::Coolant).hideMs == 3000);
    check(QStringLiteral("dlc_source_is_offline_with_permanent_warning"), dlcSelected() && !serialSelected() && session_.state() == SessionState::Stopped && port_->isHidden() && warning_->text().contains(QStringLiteral("ЛАБОРАТОРНА ЕМУЛЯЦІЯ HONDA DLC — ECU НЕ ПІДКЛЮЧЕНО")));
    check(QStringLiteral("dlc_v3_recording_started"), startRecording(filePath(recordingParent + QStringLiteral("/Honda DLC"))));
    start_->click(); advance(100);
    check(QStringLiteral("dlc_initialization_does_not_invent_ack_or_measurement"), dlcSession_.state() == dlc::State::Initializing && dlcSession_.stats().accepted == 0 && !tachometer_->reading().value && dlcSession_.lastExchange().responseHex.empty());
    advance(1500);
    const auto valueIs = [&](Channel channel, double value) { const auto current = activeModel().current(channel, now()); return current && std::abs(*current - value) < .01; };
    check(QStringLiteral("dlc_golden_values_in_shared_widgets"), valueIs(Channel::Rpm, 750) && valueIs(Channel::Coolant, 61) && valueIs(Channel::Throttle, 32) && tachometer_->reading().value == activeModel().current(Channel::Rpm, now()));
    bool unknownChannels = true;
    for (const auto channel : {Channel::Speed, Channel::Intake, Channel::Map, Channel::Voltage}) {
        const auto& reading = cards_[channelIndex(channel)]->reading();
        unknownChannels = unknownChannels && !reading.value && reading.reason == "Не визначено для цього профілю";
    }
    check(QStringLiteral("dlc_unknown_channels_have_profile_reason_without_fake_numbers"), unknownChannels);
    const auto initialEct = activeModel().channels()[2].lastValid;
    for (int i = 0; i < 110 && activeModel().channels()[2].lastValid == initialEct; ++i) advance(10);
    const auto ectLast = activeModel().channels()[2].lastValid;
    const auto rpmLast = activeModel().channels()[0].lastValid;
    const auto ectPoints = chart_->sampleCount(Channel::Coolant);
    advance(150);
    check(QStringLiteral("dlc_rpm_does_not_refresh_ect_or_add_false_plot_samples"), activeModel().channels()[0].lastValid != rpmLast && activeModel().channels()[2].lastValid == ectLast && chart_->sampleCount(Channel::Coolant) == ectPoints && chart_->sampleCount(Channel::Rpm) > ectPoints);
    dlcScenario_->setCurrentIndex(1);
    check(QStringLiteral("dlc_raw_scenario_waits_for_byte_response"), valueIs(Channel::Rpm, 750));
    advance(1100);
    check(QStringLiteral("dlc_changed_raw_set_decodes_expected_values"), valueIs(Channel::Rpm, 1500) && valueIs(Channel::Coolant, 89) && valueIs(Channel::Throttle, 75));
    settleControlLayout();
    check(QStringLiteral("dlc_hex_profile_formula_and_rates_visible"), dlcExchange_->text().contains(QStringLiteral("TX: 20 05")) && dlcExchange_->text().contains(QStringLiteral("kerpz")) && dlcExchange_->height() >= dlcExchange_->heightForWidth(dlcExchange_->width()) && dlcSilence_->y() > dlcExchange_->geometry().bottom() && statistics_->text().contains(QStringLiteral("Гц")) && dlcProfile_->currentData().toString() == QStringLiteral("honda-dlc-kerpz-obd1-reference-v1"));
    dlcCorrupt_->click(); advance(400);
    check(QStringLiteral("dlc_corrupt_checksum_fault_is_visible"), dlcSession_.state() == dlc::State::Faulted && dlcSession_.stats().corrupt > 0 && !dlcSession_.error().empty());
    advance(1300);
    check(QStringLiteral("dlc_failed_exchange_becomes_stale"), tachometer_->reading().quality == Quality::Stale);
    advance(2200);
    check(QStringLiteral("dlc_old_value_hidden_after_failure"), !tachometer_->reading().value);
    start_->click(); advance(1500);
    check(QStringLiteral("dlc_explicit_restart_recovers_from_corruption"), dlcSession_.state() == dlc::State::Polling && valueIs(Channel::Rpm, 1500));
    dlcTruncate_->click(); advance(400);
    check(QStringLiteral("dlc_truncated_reply_times_out_and_stops_polling"), dlcSession_.state() == dlc::State::Faulted && dlcSession_.stats().timeouts > 0);
    start_->click(); advance(1500); dlcSilence_->setChecked(true); advance(400);
    check(QStringLiteral("dlc_silence_requires_explicit_restart"), dlcSession_.state() == dlc::State::Faulted && dlcSession_.stats().timeouts > 0);
    dlcSilence_->setChecked(false); start_->click(); advance(1500);
    check(QStringLiteral("dlc_recovery_after_silence_uses_bytes"), valueIs(Channel::Rpm, 1500));
    dlcScenario_->setCurrentIndex(3); advance(1100);
    check(QStringLiteral("dlc_boundary_preserves_invalid_rpm_valid_negative_ect_and_zero_tps"), tachometer_->reading().quality == Quality::Invalid && !tachometer_->reading().value && valueIs(Channel::Coolant, -43) && valueIs(Channel::Throttle, 0));
    dlcScenario_->setCurrentIndex(0); advance(1100);
    for (int i = 0; i < 40 && dlcSession_.lastExchange().check.find("checksum OK") == std::string::npos; ++i) advance(10);
    for (const QSize size : {QSize(1024, 600), QSize(1280, 720)}) {
        resize(size); settleControlLayout();
        check(QStringLiteral("dlc_layout_%1x%2").arg(size.width()).arg(size.height()), centralWidget()->width() <= size.width() && centralWidget()->height() <= size.height() && tachometer_->width() >= 240 && tachometer_->height() >= 245 && chart_->height() >= 118 && controlPanel_->width() <= controlScroll_->viewport()->width());
        if (!screenshotPath.isEmpty()) {
            const QFileInfo screenshotInfo(screenshotPath);
            const auto path = screenshotInfo.dir().filePath(screenshotInfo.completeBaseName() + QStringLiteral("-dlc-%1x%2.png").arg(size.width()).arg(size.height()));
            dlcScreenshots.append(path); check(QStringLiteral("dlc_screenshot_%1_saved").arg(size.width()), grab().save(path));
        }
    }
    close();
    check(QStringLiteral("dlc_close_cancels_session_and_recording"), dlcSession_.state() == dlc::State::Stopped && session_.state() == SessionState::Stopped && !timer_->isActive() && !recorder_.active());
    QFile dlcRaw(pathString(recorder_.directory() / "raw.jsonl"));
    QFile dlcCsv(pathString(recorder_.directory() / "measurements.csv"));
    const bool dlcRawOpened = dlcRaw.open(QIODevice::ReadOnly), dlcCsvOpened = dlcCsv.open(QIODevice::ReadOnly);
    const auto dlcRawBytes = dlcRawOpened ? dlcRaw.readAll() : QByteArray{};
    const auto dlcCsvBytes = dlcCsvOpened ? dlcCsv.readAll() : QByteArray{};
    check(QStringLiteral("dlc_journal_preserves_v3_partial_updates_raw_faults_and_evidence"), recorder_.error().empty() && dlcRawBytes.contains("\"format_version\":3") && dlcRawBytes.contains("\"wire_protocol\":\"honda-dlc\"") && dlcRawBytes.contains("\"updated_mask\":") && dlcRawBytes.contains("\"age_ms\":") && dlcRawBytes.contains("\"kind\":\"rx\"") && dlcRawBytes.contains("transaction_fault") && dlcRawBytes.contains("fixture_change") && dlcRawBytes.contains("\"hardware_verified\":false") && dlcCsvBytes.contains("host_transaction_id,updated_mask"));
    dlcRaw.close(); dlcCsv.close();
    show(); source_->setCurrentIndex(0); source_->setCurrentIndex(source_->findData(2));
    check(QStringLiteral("dlc_return_after_source_switch_has_no_old_readings"), !tachometer_->reading().value && dlcSession_.state() == dlc::State::Stopped && session_.state() == SessionState::Stopped);
    start_->click(); advance(1600);
    const auto previousRpmTime = activeModel().channels()[0].lastValid;
    const auto recordingStartTime = now();
    check(QStringLiteral("dlc_mid_session_recording_starts_with_availability_only"), startRecording(filePath(recordingParent + QStringLiteral("/Honda DLC mid-session"))));
    advance(200); close();
    QFile midRaw(pathString(recorder_.directory() / "raw.jsonl"));
    const bool midOpened = midRaw.open(QIODevice::ReadOnly);
    const auto midBytes = midOpened ? midRaw.readAll() : QByteArray{};
    const auto midLines = midBytes.split('\n');
    const auto bootstrap = midLines.size() > 1 ? QJsonDocument::fromJson(midLines[1]).object() : QJsonObject{};
    check(QStringLiteral("dlc_mid_session_journal_never_replays_old_valid_values"), recorder_.error().empty() && previousRpmTime && *previousRpmTime < recordingStartTime && bootstrap.value(QStringLiteral("updated_mask")).toInt() == 127 && !midLines.value(1).contains("\"quality\":\"Valid\"") && midLines.value(1).contains("\"quality\":\"Unsupported\"") && midLines.value(1).contains("honda-dlc-kerpz-obd1-reference-v1") && midBytes.contains("\"quality\":\"Valid\""));
    check(QStringLiteral("dlc_final_close_stops_active_session_and_recorder"), dlcSession_.state() == dlc::State::Stopped && !recorder_.active());
    QJsonArray bridgeScreenshots;
    show(); source_->setCurrentIndex(source_->findData(3));
    check(QStringLiteral("bridge_mode_is_separate_native_source_with_permanent_warning"), bridgeSelected() && !bridgeSerialSelected() && port_->isHidden() && !bridgeClient_->info() && session_.state() == SessionState::Stopped && dlcSession_.state() == dlc::State::Stopped && warning_->text().contains(QStringLiteral("ТЕСТОВИЙ МІСТ — ВІРТУАЛЬНИЙ ECU — ФІЗИЧНИЙ DLC ВИМКНЕНО")));
    check(QStringLiteral("bridge_requires_handshake_before_explicit_experiment"), !bridgeNewExperiment_->isEnabled() && !bridgeScenario_->isEnabled() && !tachometer_->reading().value);
    start_->click(); advance(300);
    check(QStringLiteral("bridge_handshake_alone_never_starts_dlc_or_invents_samples"), bridgeClient_->state() == bridge::State::Ready && bridgeClient_->info() && bridgeClient_->info()->backend == 1 && !bridgeClient_->info()->physicalDlcEnabled && bridgeSession_->stats().accepted == 0 && !tachometer_->reading().value && bridgeNewExperiment_->isEnabled() && bridgeInnerTx_.isEmpty());
    check(QStringLiteral("bridge_v3_recording_started"), startRecording(filePath(recordingParent + QStringLiteral("/Міст на ПК"))));
    bridgeNewExperiment_->click(); advance(100);
    check(QStringLiteral("bridge_init_wait_does_not_claim_ecu_recognition"), bridgeSession_->stats().accepted == 0 && !tachometer_->reading().value && deviceInfo_->text().contains(QStringLiteral("Ініціалізація не розпізнає ECU")));
    advance(2600);
    check(QStringLiteral("bridge_embedded_full_path_decodes_fixture_A"), bridgeSession_->state() == dlc::State::Polling && valueIs(Channel::Rpm, 750) && valueIs(Channel::Coolant, 61) && valueIs(Channel::Throttle, 32) && deviceInfo_->text().contains(QStringLiteral("Отримано коректне DLC-читання")));
    const auto bridgeMetadata = recordingMetadata();
    bool sameBridgePolicy = bridgeMetadata.schedulerPolicy == dlc::BridgeSchedulerPolicy && bridgeMetadata.freshnessPolicy == dlc::BridgeFreshnessPolicy && bridgeMetadata.requestedIntervals == dlc::bridgeRequestedIntervals();
    for (const auto channel : {Channel::Rpm, Channel::Coolant, Channel::Throttle}) {
        const auto displayed = bridgeSession_->model().freshness().effective(channel);
        const auto recorded = bridgeMetadata.freshness.effective(channel);
        sameBridgePolicy = sameBridgePolicy && displayed.staleMs == recorded.staleMs && displayed.hideMs == recorded.hideMs &&
            bridgeChannelDiagnostics_->text().contains(QStringLiteral("Stale >%1 мс").arg(displayed.staleMs)) && bridgeChannelDiagnostics_->text().contains(QStringLiteral("прихов. >%1 мс").arg(displayed.hideMs));
    }
    check(QStringLiteral("bridge_GUI_and_recording_share_explicit_channel_policy"), sameBridgePolicy && bridgePolicy_->text().contains(QString::fromUtf8(dlc::BridgeFreshnessPolicy)) && bridgePolicy_->text().contains(QString::fromUtf8(dlc::BridgeSchedulerPolicy)));
    advance(16000);
    bool steadyBridge = bridgeSession_->state() == dlc::State::Polling;
    for (const auto channel : {Channel::Rpm, Channel::Coolant, Channel::Throttle}) {
        const auto index = channelIndex(channel); const auto& metric = bridgeSession_->metrics().channels[index];
        const auto requestedHz = 1000.0 / static_cast<double>(bridgeMetadata.requestedIntervals[index]);
        steadyBridge = steadyBridge && metric.accepted > 5 && metric.staleEvents == 0 && metric.hiddenEvents == 0 &&
            activeModel().channels()[index].quality == Quality::Valid && activeModel().current(channel, now()).has_value() &&
            std::abs(bridgeSession_->stats().channelHz[index] - requestedHz) < .15;
    }
    check(QStringLiteral("bridge_normal_cadence_has_no_recurring_stale_or_hidden_values"), steadyBridge);
    check(QStringLiteral("bridge_diagnostics_distinguish_rates_ages_and_unmeasured_timing"), bridgeChannelDiagnostics_->text().contains(QStringLiteral("факт")) && bridgeChannelDiagnostics_->text().contains(QStringLiteral("план")) && bridgeChannelDiagnostics_->text().contains(QStringLiteral("Δоновл.")) && bridgePolicy_->text().contains(QStringLiteral("до 10 с")) && statistics_->text().contains(QStringLiteral("транз./с")) && bridgeTiming_->text().contains(QStringLiteral("Запит → result:")) && bridgeTiming_->text().contains(QStringLiteral("Приймання → модель:")) && bridgeTiming_->text().contains(QStringLiteral("Observation window читання: 200 мс")) && bridgeTiming_->text().contains(QStringLiteral("valid DLC payload: не вимірюється")));
    check(QStringLiteral("bridge_bounded_latency_distributions_have_real_samples_and_percentile_labels"), bridgeSession_->metrics().requestToResult.summary().n > 5 && bridgeSession_->metrics().firmwareOperation.summary().n > 5 && bridgeTiming_->text().contains(QStringLiteral("min / median / p95 / max")) && bridgeTiming_->text().contains(QStringLiteral("nearest-rank")) && bridgeChannelDiagnostics_->toolTip().contains(QStringLiteral("інтервали оновлень")));
    QApplication::sendEvent(this, &fullscreenEvent);
    const bool bridgeFullscreen = isFullScreen();
    QApplication::sendEvent(this, &escapeEvent); settleControlLayout();
    check(QStringLiteral("bridge_F11_Escape_preserve_policy_and_permanent_warning"), bridgeFullscreen && !isFullScreen() && bridgeSelected() && bridgePolicy_->text().contains(QString::fromUtf8(dlc::BridgeFreshnessPolicy)) && warning_->text().contains(QStringLiteral("ТЕСТОВИЙ МІСТ — ВІРТУАЛЬНИЙ ECU — ФІЗИЧНИЙ DLC ВИМКНЕНО")));
    bool bridgeUnknownChannels = true;
    for (const auto channel : {Channel::Speed, Channel::Intake, Channel::Map, Channel::Voltage}) {
        const auto& reading = cards_[channelIndex(channel)]->reading();
        bridgeUnknownChannels = bridgeUnknownChannels && !reading.value && reading.reason == "Не визначено для цього профілю";
    }
    check(QStringLiteral("bridge_does_not_fall_back_to_M1_values_for_undefined_channels"), bridgeUnknownChannels && session_.state() == SessionState::Stopped && dlcSession_.state() == dlc::State::Stopped);
    const auto bridgeRpmBefore = activeModel().channels()[0].lastValid;
    std::optional<Time> coolantBeforeRpm;
    std::size_t coolantPlotBefore{};
    for (int i = 0; i < 150 && activeModel().channels()[0].lastValid == bridgeRpmBefore; ++i) {
        coolantBeforeRpm = activeModel().channels()[2].lastValid; coolantPlotBefore = chart_->sampleCount(Channel::Coolant); advance(10);
    }
    check(QStringLiteral("bridge_partial_rpm_does_not_refresh_coolant_or_plot"), activeModel().channels()[0].lastValid != bridgeRpmBefore && activeModel().channels()[2].lastValid == coolantBeforeRpm && chart_->sampleCount(Channel::Coolant) == coolantPlotBefore);
    bridgeScenario_->setCurrentIndex(1);
    check(QStringLiteral("bridge_raw_set_change_waits_for_outer_and_inner_byte_exchange"), valueIs(Channel::Rpm, 750));
    advance(2400);
    check(QStringLiteral("bridge_embedded_full_path_decodes_fixture_B"), valueIs(Channel::Rpm, 1500) && valueIs(Channel::Coolant, 89) && valueIs(Channel::Throttle, 75));
    check(QStringLiteral("bridge_hex_separates_USB_envelope_from_reported_DLC_bytes"), bridgeOuterTx_.startsWith(QStringLiteral("A5 5A")) && !bridgeOuterRx_.isEmpty() && bridgeInnerTx_.startsWith(QStringLiteral("20 05")) && bridgeInnerRx_.startsWith(QStringLiteral("00")) && bridgeOuterHex_->text().contains(QStringLiteral("USB bridge frame")) && bridgeInnerHex_->text().contains(QStringLiteral("DLC · повідомлено мостом")) && bridgeTiming_->text().contains(QStringLiteral("не синхронізовано")));
    check(QStringLiteral("bridge_trace_context_matches_reported_TX_not_next_pending_read"),
        (bridgeInnerTx_.startsWith(QStringLiteral("20 05 00")) && bridgeInnerRead_.startsWith(QStringLiteral("RPM"))) ||
        (bridgeInnerTx_.startsWith(QStringLiteral("20 05 10")) && bridgeInnerRead_.startsWith(QStringLiteral("ECT"))) ||
        (bridgeInnerTx_.startsWith(QStringLiteral("20 05 14")) && bridgeInnerRead_.startsWith(QStringLiteral("TPS"))));
    bridgeCorrupt_->click(); advance(1000);
    check(QStringLiteral("bridge_DLC_checksum_failure_blocks_polling_with_explicit_restart"), bridgeSession_->state() == dlc::State::Faulted && bridgeClient_->state() == bridge::State::Faulted && !bridgeSession_->error().empty() && bridgeNewExperiment_->isEnabled() && !start_->isEnabled());
    const auto bridgeAcceptedAtFault = bridgeSession_->stats().accepted;
    advance(1500);
    check(QStringLiteral("bridge_fault_preserves_old_values_as_stale_without_new_samples"), tachometer_->reading().quality == Quality::Stale && bridgeSession_->stats().accepted == bridgeAcceptedAtFault);
    check(QStringLiteral("bridge_channel_diagnostics_show_textual_stale_and_growing_age"), bridgeChannelDiagnostics_->text().contains(QStringLiteral("RPM · Stale")) && bridgeChannelDiagnostics_->text().contains(QStringLiteral("Давність")) && bridgeTiming_->text().contains(QStringLiteral("не вимірюється")));
    advance(2200);
    check(QStringLiteral("bridge_fault_eventually_hides_old_value"), !tachometer_->reading().value);
    bridgeNewExperiment_->click(); advance(2600);
    check(QStringLiteral("bridge_explicit_new_experiment_recovers_via_embedded_byte_path"), bridgeSession_->state() == dlc::State::Polling && valueIs(Channel::Rpm, 1500) && valueIs(Channel::Coolant, 89) && valueIs(Channel::Throttle, 75));
    bridgeGap_->setValue(60); advance(1100);
    check(QStringLiteral("bridge_interbyte_fault_is_not_hidden_by_USB_result_delivery"), bridgeSession_->state() == dlc::State::Faulted && bridgeClient_->state() == bridge::State::Faulted);
    bridgeGap_->setValue(0); bridgeNewExperiment_->click(); advance(2600);
    bridgeTrailing_->click(); advance(1100);
    check(QStringLiteral("bridge_valid_reply_plus_trailing_bytes_blocks_exchange"), bridgeSession_->state() == dlc::State::Faulted && bridgeClient_->state() == bridge::State::Faulted);
    bridgeNewExperiment_->click(); advance(2600);
    bridgeScenario_->setCurrentIndex(0); advance(2400);
    for (const QSize size : {QSize(1024, 600), QSize(1280, 720)}) {
        resize(size); settleControlLayout();
        check(QStringLiteral("bridge_layout_%1x%2").arg(size.width()).arg(size.height()), centralWidget()->width() <= size.width() && centralWidget()->height() <= size.height() && tachometer_->width() >= 240 && tachometer_->height() >= 245 && chart_->height() >= 118 && controlPanel_->width() <= controlScroll_->viewport()->width() && QFontMetrics(statistics_->font()).horizontalAdvance(statistics_->text()) <= statistics_->contentsRect().width());
        if (!screenshotPath.isEmpty()) {
            const QFileInfo screenshotInfo(screenshotPath);
            const auto path = screenshotInfo.dir().filePath(screenshotInfo.completeBaseName() + QStringLiteral("-bridge-%1x%2.png").arg(size.width()).arg(size.height()));
            bridgeScreenshots.append(path); check(QStringLiteral("bridge_screenshot_%1_saved").arg(size.width()), grab().save(path));
            if (size.width() == 1280) {
                controlScroll_->verticalScrollBar()->setValue(bridgePolicy_->mapTo(controlPanel_, QPoint{}).y()); settleControlLayout();
                const auto diagnosticsPath = screenshotInfo.dir().filePath(screenshotInfo.completeBaseName() + QStringLiteral("-bridge-diagnostics.png"));
                bridgeScreenshots.append(diagnosticsPath);
                check(QStringLiteral("bridge_channel_diagnostics_fit_and_screenshot_saved"), bridgeChannelDiagnostics_->height() >= bridgeChannelDiagnostics_->heightForWidth(bridgeChannelDiagnostics_->width()) && grab().save(diagnosticsPath));
                controlScroll_->verticalScrollBar()->setValue(bridgeOuterHex_->mapTo(controlPanel_, QPoint{}).y()); settleControlLayout();
                const auto tracePath = screenshotInfo.dir().filePath(screenshotInfo.completeBaseName() + QStringLiteral("-bridge-trace.png"));
                bridgeScreenshots.append(tracePath);
                check(QStringLiteral("bridge_trace_labels_fit_their_contents"), bridgeOuterHex_->height() >= bridgeOuterHex_->heightForWidth(bridgeOuterHex_->width()) && bridgeInnerHex_->height() >= bridgeInnerHex_->heightForWidth(bridgeInnerHex_->width()) && bridgeTiming_->height() >= bridgeTiming_->heightForWidth(bridgeTiming_->width()));
                check(QStringLiteral("bridge_two_level_trace_screenshot_saved"), grab().save(tracePath));
                controlScroll_->verticalScrollBar()->setValue(bridgeTiming_->mapTo(controlPanel_, QPoint{}).y()); settleControlLayout();
                const auto timingPath = screenshotInfo.dir().filePath(screenshotInfo.completeBaseName() + QStringLiteral("-bridge-timing.png"));
                bridgeScreenshots.append(timingPath);
                check(QStringLiteral("bridge_latency_distribution_screenshot_saved"), grab().save(timingPath));
                controlScroll_->verticalScrollBar()->setValue(0);
            }
        }
    }
    close();
    check(QStringLiteral("bridge_close_stops_all_sessions_and_recording"), bridgeSession_->state() == dlc::State::Stopped && bridgeClient_->state() == bridge::State::Disconnected && !recorder_.active() && !timer_->isActive());
    QFile bridgeRawFile(pathString(recorder_.directory() / "raw.jsonl"));
    const auto bridgeOpened = bridgeRawFile.open(QIODevice::ReadOnly);
    const auto bridgeBytes = bridgeOpened ? bridgeRawFile.readAll() : QByteArray{};
    check(QStringLiteral("bridge_journal_preserves_outer_inner_faults_boundaries_and_partial_updates"), recorder_.error().empty() && bridgeBytes.contains("\"format_version\":3") && bridgeBytes.contains("\"source\":\"simulation\"") && bridgeBytes.contains("\"outer_protocol\":\"hondadash-dlc-bridge-lab-v1\"") && bridgeBytes.contains("\"backend\":\"virtual\"") && bridgeBytes.contains("\"physical_dlc_enabled\":false") && bridgeBytes.contains("\"kind\":\"usb_tx\"") && bridgeBytes.contains("\"kind\":\"usb_rx\"") && bridgeBytes.contains("\"kind\":\"bridge_dlc_rx\"") && bridgeBytes.contains("bridge_boundary") && bridgeBytes.contains("transaction_fault") && bridgeBytes.contains("\"updated_mask\":"));
    bridgeRawFile.close();
    const auto bridgeJournalMetadata = QJsonDocument::fromJson(bridgeBytes.split('\n').value(0)).object();
    const auto recordedThresholds = bridgeJournalMetadata.value(QStringLiteral("freshness_by_channel")).toObject();
    const auto recordedIntervals = bridgeJournalMetadata.value(QStringLiteral("requested_intervals_ms")).toObject();
    bool policyRoundtrip = bridgeJournalMetadata.value(QStringLiteral("scheduler_policy")).toString() == QString::fromUtf8(dlc::BridgeSchedulerPolicy) && bridgeJournalMetadata.value(QStringLiteral("freshness_policy")).toString() == QString::fromUtf8(dlc::BridgeFreshnessPolicy) && bridgeJournalMetadata.value(QStringLiteral("freshness_boundary")).toString() == QStringLiteral("age_gt_threshold");
    for (const auto& entry : {std::pair{Channel::Rpm, QStringLiteral("rpm")}, std::pair{Channel::Coolant, QStringLiteral("coolant")}, std::pair{Channel::Throttle, QStringLiteral("throttle")}}) {
        const auto expected = bridgeMetadata.freshness.effective(entry.first);
        const auto recorded = recordedThresholds.value(entry.second).toObject();
        policyRoundtrip = policyRoundtrip && recorded.value(QStringLiteral("stale_after_ms")).toInteger() == static_cast<qint64>(expected.staleMs) && recorded.value(QStringLiteral("hide_after_ms")).toInteger() == static_cast<qint64>(expected.hideMs) && recordedIntervals.value(entry.second).toInteger() == static_cast<qint64>(bridgeMetadata.requestedIntervals[channelIndex(entry.first)]);
    }
    check(QStringLiteral("bridge_saved_journal_identifies_displayed_freshness_and_scheduler"), policyRoundtrip);
    show(); start_->click(); advance(300);
    check(QStringLiteral("bridge_reconnect_requires_new_explicit_experiment_without_old_values"), bridgeClient_->state() == bridge::State::Ready && bridgeSession_->stats().accepted == 0 && !tachometer_->reading().value);
    bridgeNewExperiment_->click(); advance(2600);
    check(QStringLiteral("bridge_mid_session_recording_started"), startRecording(filePath(recordingParent + QStringLiteral("/Міст mid-session"))));
    advance(1600); close();
    QFile bridgeMidFile(pathString(recorder_.directory() / "raw.jsonl"));
    const bool bridgeMidOpened = bridgeMidFile.open(QIODevice::ReadOnly);
    const auto bridgeMidBytes = bridgeMidOpened ? bridgeMidFile.readAll() : QByteArray{};
    const auto bridgeMidLines = bridgeMidBytes.split('\n');
    check(QStringLiteral("bridge_mid_session_recording_does_not_replay_old_values_as_fresh"), recorder_.error().empty() && bridgeMidLines.value(1).contains("\"updated_mask\":127") && !bridgeMidLines.value(1).contains("\"quality\":\"Valid\"") && bridgeMidBytes.contains("\"quality\":\"Valid\""));
    bridgeMidFile.close();
    show(); source_->setCurrentIndex(0); source_->setCurrentIndex(source_->findData(3));
    check(QStringLiteral("bridge_source_switch_cancels_old_context_and_model"), bridgeClient_->state() == bridge::State::Disconnected && !bridgeClient_->info() && !tachometer_->reading().value && chart_->sampleCount() == 0);
#ifdef HONDADASH_WITH_SERIAL
    bridgeBackend_->setCurrentIndex(bridgeBackend_->findData(1)); port_->setCurrentIndex(0);
    check(QStringLiteral("bridge_USB_requires_explicit_port_without_auto_open_or_fallback"), bridgeSerialSelected() && !start_->isEnabled() && bridgeClient_->state() == bridge::State::Disconnected && !bridgeClient_->info() && !tachometer_->reading().value);
    port_->addItem(QStringLiteral("GUI smoke · bridge відсутній порт"), QStringLiteral("HondaDash_BRIDGE_SMOKE_NONEXISTENT_PORT_6491")); port_->setCurrentIndex(port_->count() - 1);
    start_->click(); advance(200);
    check(QStringLiteral("bridge_USB_missing_port_is_visible_and_never_becomes_native_bridge"), bridgeClient_->state() == bridge::State::Faulted && !bridgeClient_->info() && !bridgeClient_->error().empty() && !tachometer_->reading().value && !bridgeNewExperiment_->isEnabled());
    stop_->click(); bridgeBackend_->setCurrentIndex(2); port_->setCurrentIndex(0); responderPort_->setCurrentIndex(0);
    check(QStringLiteral("bench_two_explicit_ports_no_fallback_or_values"), benchSelected() && !start_->isEnabled() && !responderPort_->isHidden() && benchController_->state() == bench::State::Disconnected && !tachometer_->reading().value && bridgeInnerTx_.isEmpty());
    check(QStringLiteral("bench_permanent_warning_and_separate_metadata"), warning_->text().contains(QStringLiteral("СТЕНД: ДВІ NANO — ЕМУЛЯТОР ECU — НЕ ПІДКЛЮЧАТИ ДО АВТОМОБІЛЯ")) && !warning_->text().contains(QStringLiteral("ФІЗИЧНИЙ DLC ВИМКНЕНО")) && recordingMetadata().benchIoEnabled && recordingMetadata().benchSchemaVersion == 1 && !recordingMetadata().vehicleConnectionAllowed);
    port_->addItem(QStringLiteral("same test port"), QStringLiteral("COM991")); port_->setCurrentIndex(port_->count()-1);
    responderPort_->addItem(QStringLiteral("same test port"), QStringLiteral("COM991")); responderPort_->setCurrentIndex(responderPort_->count()-1);
    check(QStringLiteral("bench_rejects_same_port_alias_before_open"), !start_->isEnabled() && benchController_->state() == bench::State::Disconnected);
    port_->setCurrentIndex(0); responderPort_->setCurrentIndex(0);
    for (const auto size : {QSize(1024,600), QSize(1280,720)}) {
        resize(size); settleControlLayout();
        check(QStringLiteral("bench_layout_%1x%2").arg(size.width()).arg(size.height()), centralWidget()->width() <= size.width() && centralWidget()->height() <= size.height() && controlPanel_->width() <= controlScroll_->viewport()->width() && warning_->isVisible() && !tachometer_->reading().value);
        if (!screenshotPath.isEmpty()) {
            const QFileInfo info(screenshotPath);
            const auto benchShot = info.dir().filePath(info.completeBaseName() + QStringLiteral("-bench-unconnected-%1.png").arg(size.width()));
            bridgeScreenshots.append(benchShot);
            check(QStringLiteral("bench_unconnected_screenshot_%1").arg(size.width()), grab().save(benchShot));
        }
    }
    bridgeBackend_->setCurrentIndex(0);
#else
    check(QStringLiteral("bridge_simulation_only_has_native_backend_without_SerialPort"), bridgeBackend_->count() == 1 && bridgeBackend_->currentData().toInt() == 0);
#endif
    for (int i = 0; i < 5; ++i) { start_->click(); advance(300); bridgeNewExperiment_->click(); advance(800); stop_->click(); }
    check(QStringLiteral("bridge_repeated_start_stop_cancels_context_and_clears_readings"), bridgeClient_->state() == bridge::State::Disconnected && bridgeSession_->state() == dlc::State::Stopped && !tachometer_->reading().value);
    close();
    return {{QStringLiteral("format_version"), 1}, {QStringLiteral("application"), QStringLiteral("HondaDash")},
        {QStringLiteral("profile"), QStringLiteral("synthetic-demo-v1")}, {QStringLiteral("passed"), passed},
        {QStringLiteral("result"), passed ? QStringLiteral("pass") : QStringLiteral("fail")},
        {QStringLiteral("clock"), QStringLiteral("managed monotonic milliseconds")}, {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("offscreen"), QGuiApplication::platformName() == QStringLiteral("offscreen")},
        {QStringLiteral("physical_usb_verified"), false}, {QStringLiteral("checks"), checks}, {QStringLiteral("accepted_samples_before_stop"), static_cast<qint64>(samplesBeforeStop)},
        {QStringLiteral("layout_geometry"), layoutGeometry},
        {QStringLiteral("accepted_samples_final_session"), static_cast<qint64>(accepted)}, {QStringLiteral("screenshot"), screenshotPath},
        {QStringLiteral("screenshot_1024x600"), screenshot1024}, {QStringLiteral("screenshot_usb_unconnected"), screenshotUsb},
        {QStringLiteral("dlc_screenshots"), dlcScreenshots}, {QStringLiteral("bridge_screenshots"), bridgeScreenshots}, {QStringLiteral("real_ecu_verified"), false},
        {QStringLiteral("offscreen_font_loaded"), qApp->property("offscreen_font_loaded").toBool()}};
}
void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F11) { isFullScreen() ? showNormal() : showFullScreen(); event->accept(); return; }
    if (event->key() == Qt::Key_Escape && isFullScreen()) { showNormal(); event->accept(); return; }
    QMainWindow::keyPressEvent(event);
}
void MainWindow::closeEvent(QCloseEvent* event) {
    if (benchController_ && benchController_->state() != bench::State::Disconnected) {
        if (!benchStopPending_) stopSession();
        if (benchStopPending_) { closePending_ = true; event->ignore(); return; }
    }
    timer_->stop(); session_.stop(now()); dlcSession_.stop(now()); bridgeSession_->stop(now());
    if (benchController_) benchController_->disconnect(now()); else ownedBridgeClient_->disconnect(now());
    recorder_.stop(); refreshDashboard(); event->accept();
}
}
