#include "ui/main_window.hpp"
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
#endif
    injectedQualities_.fill(Quality::Valid);
    setWindowTitle(QStringLiteral("HondaDash · M2a · offline laboratory"));
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
    auto* title = label(QStringLiteral("HONDA<span style='color:#51d3ba'>DASH</span>  <span style='font-size:10px;color:#91a5ba'>M2a · лабораторія протоколів</span>"));
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
    source_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    source_->setToolTip(QStringLiteral("Honda DLC — лабораторна емуляція працює лише з програмним відповідачем. USB доступний тільки синтетичному M1."));
    controlLayout->addWidget(source_);
    auto* portRow = new QHBoxLayout;
    port_ = new QComboBox; port_->setObjectName(QStringLiteral("serialPort"));
    port_->setMinimumWidth(120); port_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    refreshPorts_ = new QPushButton(QStringLiteral("Оновити")); refreshPorts_->setObjectName(QStringLiteral("refreshPorts"));
    portRow->addWidget(port_, 1); portRow->addWidget(refreshPorts_); controlLayout->addLayout(portRow);
    portDetails_ = label({}); portDetails_->setStyleSheet(QStringLiteral("color:#91a5ba;font-size:10px;")); controlLayout->addWidget(portDetails_);
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
    session_.onSample = [this, sampleSink](const Sample& sample) { if (!dlcSelected()) sampleSink(sample); };
    session_.onRaw = [this](const RawEvent& event) { if (!dlcSelected() && recorder_.active()) recorder_.enqueueRaw(event); };
    dlcSession_.onSample = [this, sampleSink](const Sample& sample) { if (dlcSelected()) sampleSink(sample); };
    dlcSession_.onRaw = [this](const RawEvent& event) { if (dlcSelected() && recorder_.active()) recorder_.enqueueRaw(event); };
    clock_.start(); timer_ = new QTimer(this); timer_->setTimerType(Qt::PreciseTimer); timer_->setInterval(16);
    connect(timer_, &QTimer::timeout, this, [this] { tickSessions(); refreshDashboard(); });
    if (!managedTime_) timer_->start();
    refreshPorts();
    refreshDashboard();
}
MainWindow::~MainWindow() { session_.stop(now()); dlcSession_.stop(now()); recorder_.stop(); }
Time MainWindow::now() const { return managedTime_ ? managedNow_ : static_cast<Time>(std::max<qint64>(0, clock_.elapsed())); }
bool MainWindow::serialSelected() const { return source_->currentData().toInt() == 1; }
bool MainWindow::dlcSelected() const { return source_->currentData().toInt() == 2; }
const Model& MainWindow::activeModel() const { return dlcSelected() ? dlcSession_.model() : session_.model(); }
void MainWindow::tickSessions() { if (dlcSelected()) dlcSession_.tick(now()); else session_.tick(now()); }
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
    port_->blockSignals(true); port_->clear();
    port_->addItem(QStringLiteral("Оберіть порт…"), QString{});
#ifdef HONDADASH_WITH_SERIAL
    for (const auto& info : QSerialPortInfo::availablePorts()) {
        port_->addItem(info.portName(), info.systemLocation());
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
    portDetails_->setText(port_->currentData(Qt::ToolTipRole).toString());
    refreshDashboard();
}
void MainWindow::changeSource() {
    recorder_.stop(); session_.stop(now()); dlcSession_.stop(now()); chart_->clear();
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
    if (dlcSelected()) { chart_->clear(); dlcSession_.start(now()); refreshDashboard(); return; }
    if (serialSelected() && port_->currentData().toString().isEmpty()) return;
    session_.stop(now());
#ifdef HONDADASH_WITH_SERIAL
    if (serialSelected()) serialTransport_->setPortName(port_->currentData().toString().toStdString());
#endif
    chart_->clear(); session_.start(now()); refreshDashboard();
}
void MainWindow::stopSession() { session_.stop(now()); dlcSession_.stop(now()); refreshDashboard(); }
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
    const bool honda = dlcSelected();
    const auto& measurements = activeModel().channels();
    for (std::size_t i = 0; i < ChannelCount; ++i) {
        auto reading = measurements[i]; reading.value = activeModel().current(static_cast<Channel>(i), currentTime);
        if (i == 0) tachometer_->setReading(reading, elapsed); else cards_[i]->setReading(reading);
    }
    chart_->setNow(currentTime); sessionStatus_->setText(honda ? dlcSessionName(dlcSession_.state()) : QStringLiteral("Сесія: ") + sessionName(session_.state()));
    sessionStatus_->setToolTip(QString::fromStdString(honda ? dlcSession_.error() : session_.error()));
    const bool running = honda ? dlcSession_.state() == dlc::State::Polling : session_.state() == SessionState::Running;
    sessionStatus_->setStyleSheet(running ? QStringLiteral("color:#51d3ba;") : QStringLiteral("color:#ffca79;"));
    const auto& stats = session_.stats(); std::optional<Time> newest;
    for (const auto& reading : measurements) if (reading.lastValid && (!newest || *reading.lastValid > *newest)) newest = reading.lastValid;
    const auto age = newest ? QString::number(currentTime - *newest) + QStringLiteral(" мс") : QStringLiteral("—");
    const auto error = QString::fromUtf8(recorder_.error().c_str());
    const auto recordState = !error.isEmpty() ? QStringLiteral("ПОМИЛКА") : recorder_.active() ? QStringLiteral("ЗАПИС") : QStringLiteral("зупинено");
    statistics_->setText(QStringLiteral("%1 відп./с · Давність: %2 · Тайм-аути: %3 · Пошкоджені: %4 · Прийнято: %5 · Журнал: %6")
        .arg(stats.responseHz, 0, 'f', 1).arg(age).arg(stats.timeouts).arg(stats.corrupt).arg(stats.accepted).arg(recordState));
    const bool stopped = honda ? dlcSession_.state() == dlc::State::Stopped || dlcSession_.state() == dlc::State::Faulted : session_.state() == SessionState::Stopped || session_.state() == SessionState::Faulted;
    const bool usb = serialSelected();
    source_->setEnabled(stopped); port_->setEnabled(stopped); refreshPorts_->setEnabled(stopped);
    port_->setVisible(usb); refreshPorts_->setVisible(usb); portDetails_->setVisible(usb);
    start_->setText(usb ? QStringLiteral("Підключити") : QStringLiteral("Старт"));
    stop_->setText(usb ? QStringLiteral("Від’єднати") : QStringLiteral("Стоп"));
    start_->setEnabled(stopped && (!usb || !port_->currentData().toString().isEmpty()));
    stop_->setEnabled(honda ? dlcSession_.state() != dlc::State::Stopped : session_.state() != SessionState::Stopped);
    scenario_->setVisible(!honda); demoNotice_->setVisible(!honda); seed_->setVisible(!honda); seedLabel_->setVisible(!honda);
    manualBox_->setVisible(!honda && scenario_->currentIndex() == 3); faultBox_->setVisible(!honda); dlcBox_->setVisible(honda);
    dlcProfile_->setEnabled(stopped);
    warning_->setText(honda ? QStringLiteral("ЛАБОРАТОРНА ЕМУЛЯЦІЯ HONDA DLC — ECU НЕ ПІДКЛЮЧЕНО") : usb ? QStringLiteral("ЕМУЛЯЦІЯ НА ПРИСТРОЇ — ECU НЕ ПІДКЛЮЧЕНО") : QStringLiteral("ЕМУЛЯЦІЯ — не підключено до автомобіля"));
    if (offscreenScreenshot_) warning_->setText(warning_->text() + QStringLiteral(" · знімок offscreen (без звичайного GUI)"));
    const auto& info = session_.deviceInfo();
    QString identity = info ? QStringLiteral("%1 · firmware %2").arg(QString::fromStdString(info->endpoint), QString::fromStdString(info->firmware)) : usb ? QStringLiteral("Тестовий пристрій не розпізнано") : QStringLiteral("Синтетичний профіль у пам’яті ПК");
    if (session_.state() == SessionState::Running) identity += QStringLiteral("\nПристрій розпізнано; свіжість показників — унизу.");
    if (!session_.error().empty()) identity += QStringLiteral("\nПОМИЛКА: ") + QString::fromStdString(session_.error());
    deviceInfo_->setText(identity);
    deviceInfo_->setStyleSheet(session_.error().empty() ? QStringLiteral("color:#91a5ba;") : QStringLiteral("color:#ff7e7e;"));
    if (honda) refreshDlcDetails(); else updateCapabilities(info);
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
RecordingMetadata MainWindow::recordingMetadata() const {
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
    if (dlcSelected()) return recorder_.enqueueSample(dlc::unavailable(now(), dlcSession_.id()));
    return true;
}
void MainWindow::advance(Time amount) {
    const Time end = managedNow_ + amount;
    while (managedNow_ < end) { managedNow_ = std::min(end, managedNow_ + 10); tickSessions(); refreshDashboard(); }
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
    return {{QStringLiteral("format_version"), 1}, {QStringLiteral("application"), QStringLiteral("HondaDash")},
        {QStringLiteral("profile"), QStringLiteral("synthetic-demo-v1")}, {QStringLiteral("passed"), passed},
        {QStringLiteral("result"), passed ? QStringLiteral("pass") : QStringLiteral("fail")},
        {QStringLiteral("clock"), QStringLiteral("managed monotonic milliseconds")}, {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("offscreen"), QGuiApplication::platformName() == QStringLiteral("offscreen")},
        {QStringLiteral("physical_usb_verified"), false}, {QStringLiteral("checks"), checks}, {QStringLiteral("accepted_samples_before_stop"), static_cast<qint64>(samplesBeforeStop)},
        {QStringLiteral("layout_geometry"), layoutGeometry},
        {QStringLiteral("accepted_samples_final_session"), static_cast<qint64>(accepted)}, {QStringLiteral("screenshot"), screenshotPath},
        {QStringLiteral("screenshot_1024x600"), screenshot1024}, {QStringLiteral("screenshot_usb_unconnected"), screenshotUsb},
        {QStringLiteral("dlc_screenshots"), dlcScreenshots}, {QStringLiteral("real_ecu_verified"), false},
        {QStringLiteral("offscreen_font_loaded"), qApp->property("offscreen_font_loaded").toBool()}};
}
void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F11) { isFullScreen() ? showNormal() : showFullScreen(); event->accept(); return; }
    if (event->key() == Qt::Key_Escape && isFullScreen()) { showNormal(); event->accept(); return; }
    QMainWindow::keyPressEvent(event);
}
void MainWindow::closeEvent(QCloseEvent* event) { timer_->stop(); session_.stop(now()); dlcSession_.stop(now()); recorder_.stop(); event->accept(); }
}
