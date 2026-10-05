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
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QVariant>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace hd::ui {
namespace {
QString sessionName(SessionState state) {
    switch (state) {
    case SessionState::Stopped: return QStringLiteral("Зупинено");
    case SessionState::Handshaking: return QStringLiteral("Ініціалізація емулятора");
    case SessionState::Running: return QStringLiteral("Обмін працює");
    case SessionState::Faulted: return QStringLiteral("Немає відповіді на HELLO");
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
MainWindow::MainWindow(bool managedTime, QWidget* parent) : QMainWindow(parent), managedTime_(managedTime) {
    injectedQualities_.fill(Quality::Valid);
    setWindowTitle(QStringLiteral("HondaDash · synthetic-demo-v1"));
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
    auto* title = label(QStringLiteral("HONDA<span style='color:#51d3ba'>DASH</span>  <span style='font-size:10px;color:#91a5ba'>M0 · synthetic-demo-v1</span>"));
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
    auto* controlLayout = new QVBoxLayout(controls); controlLayout->setContentsMargins(3, 0, 3, 0); controlLayout->setSpacing(8);
    auto* controlTitle = label(QStringLiteral("КЕРУВАННЯ ЕМУЛЯТОРОМ")); controlTitle->setStyleSheet(QStringLiteral("font-weight:bold;color:#51d3ba;"));
    controlLayout->addWidget(controlTitle);
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
    auto* seedLayout = new QHBoxLayout; seedLayout->addWidget(label(QStringLiteral("Seed")));
    seed_ = new QSpinBox; seed_->setRange(0, 2147483647); seed_->setValue(42); seedLayout->addWidget(seed_); controlLayout->addLayout(seedLayout);
    session_.emulator().setSeed(42);
    session_.emulator().setScenario(Scenario::Demo, 0);
    connect(seed_, &QSpinBox::valueChanged, this, [this](int value) { session_.emulator().setSeed(static_cast<std::uint32_t>(value)); });
    auto* manualBox = new QGroupBox(QStringLiteral("Ручні значення · після відповіді"));
    auto* form = new QFormLayout(manualBox); form->setContentsMargins(9, 17, 9, 8); form->setSpacing(5);
    const std::array<double, ChannelCount> defaults{850, 0, 80, 23, 0, 30, 14.1};
    const std::array<QString, ChannelCount> shortNames{QStringLiteral("Оберти"),QStringLiteral("Швидкість"),QStringLiteral("Рідина"),QStringLiteral("Впуск"),QStringLiteral("Дросель"),QStringLiteral("Тиск"),QStringLiteral("Напруга")};
    for (std::size_t i = 0; i < ChannelCount; ++i) {
        const auto channel = static_cast<Channel>(i); const auto& info = channelInfo(channel);
        auto* spin = new QDoubleSpinBox; manual_[i] = spin;
        spin->setObjectName(QStringLiteral("manual_%1").arg(i)); spin->setRange(info.min, info.max); spin->setDecimals(info.decimals);
        spin->setSingleStep(i == 0 ? 100 : i == 6 ? .1 : 1); spin->setValue(defaults[i]); spin->setSuffix(QStringLiteral(" ") + channelUnit(channel));
        session_.emulator().setManual(channel, spin->value());
        form->addRow(shortNames[i], spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, channel](double value) { session_.emulator().setManual(channel, value); });
    }
    manualBox->setVisible(false); controlLayout->addWidget(manualBox);
    connect(scenario_, &QComboBox::currentIndexChanged, this, [this, manualBox](int index) {
        session_.emulator().setScenario(static_cast<Scenario>(index), now()); manualBox->setVisible(index == 3);
    });
    auto* faultBox = new QGroupBox(QStringLiteral("Несправності"));
    auto* faultLayout = new QVBoxLayout(faultBox); faultLayout->setContentsMargins(9, 17, 9, 8); faultLayout->setSpacing(6);
    silence_ = new QCheckBox(QStringLiteral("Не відповідати")); silence_->setObjectName(QStringLiteral("silence")); faultLayout->addWidget(silence_);
    connect(silence_, &QCheckBox::toggled, this, [this](bool enabled) { session_.emulator().faults().silent = enabled; });
    auto* delayRow = new QHBoxLayout; delayRow->addWidget(label(QStringLiteral("Затримка")));
    delay_ = new QSpinBox; delay_->setRange(0, 5000); delay_->setSuffix(QStringLiteral(" мс")); delayRow->addWidget(delay_); faultLayout->addLayout(delayRow);
    connect(delay_, &QSpinBox::valueChanged, this, [this](int value) { session_.emulator().faults().delayMs = static_cast<Time>(value); });
    auto* corrupt = new QPushButton(QStringLiteral("Пошкодити наступну CRC")); faultLayout->addWidget(corrupt);
    connect(corrupt, &QPushButton::clicked, this, [this] { session_.emulator().faults().corruptNext = true; });
    auto* truncate = new QPushButton(QStringLiteral("Обірвати наступний пакет")); faultLayout->addWidget(truncate);
    connect(truncate, &QPushButton::clicked, this, [this] { session_.emulator().faults().truncateNext = true; });
    auto* qualityChannel = new QComboBox; for (const auto& name : shortNames) qualityChannel->addItem(name); faultLayout->addWidget(qualityChannel);
    auto* quality = new QComboBox; quality->addItems({QStringLiteral("Коректний"), QStringLiteral("Не підтримується"), QStringLiteral("Некоректний")}); faultLayout->addWidget(quality);
    const auto applyQuality = [this, qualityChannel, quality] {
        const std::array<Quality, 3> types{Quality::Valid, Quality::Unsupported, Quality::Invalid};
        const auto channel = static_cast<Channel>(qualityChannel->currentIndex());
        const auto selectedQuality = types[static_cast<std::size_t>(quality->currentIndex())];
        injectedQualities_[channelIndex(channel)] = selectedQuality;
        session_.emulator().setChannelQuality(channel, selectedQuality);
    };
    connect(quality, &QComboBox::currentIndexChanged, this, [applyQuality](int) { applyQuality(); });
    connect(qualityChannel, &QComboBox::currentIndexChanged, this, [this, qualityChannel, quality] {
        const auto selected = injectedQualities_[static_cast<std::size_t>(qualityChannel->currentIndex())];
        quality->blockSignals(true);
        quality->setCurrentIndex(selected == Quality::Unsupported ? 1 : selected == Quality::Invalid ? 2 : 0);
        quality->blockSignals(false);
    });
    controlLayout->addWidget(faultBox);
    record_ = new QPushButton(QStringLiteral("Почати запис CSV + JSONL")); record_->setObjectName(QStringLiteral("record")); controlLayout->addWidget(record_);
    connect(record_, &QPushButton::clicked, this, [this] { toggleRecording(); });
    recordStatus_ = label(QStringLiteral("Журнал: зупинено")); recordStatus_->setObjectName(QStringLiteral("recordStatus"));
    recordStatus_->setTextInteractionFlags(Qt::TextSelectableByMouse); recordStatus_->setStyleSheet(QStringLiteral("color:#91a5ba;")); controlLayout->addWidget(recordStatus_);
    controlLayout->addStretch();
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setWidget(controls); scroll->setMinimumWidth(250); scroll->setMaximumWidth(290);
    body->addWidget(scroll); layout->addLayout(body, 1);
    auto* footer = new QHBoxLayout;
    sessionStatus_ = label({}); sessionStatus_->setMinimumWidth(130); footer->addWidget(sessionStatus_);
    statistics_ = label({}); statistics_->setAlignment(Qt::AlignRight | Qt::AlignVCenter); footer->addWidget(statistics_, 1);
    layout->addLayout(footer); setCentralWidget(root);
    session_.onSample = [this](const Sample& sample) { chart_->pushSample(sample); if (recorder_.active()) recorder_.enqueueSample(sample); };
    session_.onRaw = [this](const RawEvent& event) { if (recorder_.active()) recorder_.enqueueRaw(event); };
    clock_.start(); timer_ = new QTimer(this); timer_->setTimerType(Qt::PreciseTimer); timer_->setInterval(16);
    connect(timer_, &QTimer::timeout, this, [this] { session_.tick(now()); refreshDashboard(); });
    if (!managedTime_) timer_->start();
    refreshDashboard();
}
MainWindow::~MainWindow() { session_.stop(now()); recorder_.stop(); }
Time MainWindow::now() const { return managedTime_ ? managedNow_ : static_cast<Time>(std::max<qint64>(0, clock_.elapsed())); }
void MainWindow::startSession() { chart_->clear(); session_.start(now()); refreshDashboard(); }
void MainWindow::stopSession() { session_.stop(now()); refreshDashboard(); }
void MainWindow::refreshDashboard() {
    const auto currentTime = now(); const double elapsed = static_cast<double>(currentTime - lastPaint_); lastPaint_ = currentTime;
    const auto& measurements = session_.model().channels();
    for (std::size_t i = 0; i < ChannelCount; ++i) {
        auto reading = measurements[i]; reading.value = session_.model().current(static_cast<Channel>(i), currentTime);
        if (i == 0) tachometer_->setReading(reading, elapsed); else cards_[i]->setReading(reading);
    }
    chart_->setNow(currentTime); sessionStatus_->setText(QStringLiteral("Сесія: ") + sessionName(session_.state()));
    sessionStatus_->setStyleSheet(session_.state() == SessionState::Running ? QStringLiteral("color:#51d3ba;") : QStringLiteral("color:#ffca79;"));
    const auto& stats = session_.stats(); std::optional<Time> newest;
    for (const auto& reading : measurements) if (reading.lastValid && (!newest || *reading.lastValid > *newest)) newest = reading.lastValid;
    const auto age = newest ? QString::number(currentTime - *newest) + QStringLiteral(" мс") : QStringLiteral("—");
    const auto error = QString::fromUtf8(recorder_.error().c_str());
    const auto recordState = !error.isEmpty() ? QStringLiteral("ПОМИЛКА") : recorder_.active() ? QStringLiteral("ЗАПИС") : QStringLiteral("зупинено");
    statistics_->setText(QStringLiteral("%1 відп./с · Давність: %2 · Тайм-аути: %3 · Пошкоджені: %4 · Прийнято: %5 · Журнал: %6")
        .arg(stats.responseHz, 0, 'f', 1).arg(age).arg(stats.timeouts).arg(stats.corrupt).arg(stats.accepted).arg(recordState));
    start_->setEnabled(session_.state() == SessionState::Stopped || session_.state() == SessionState::Faulted);
    stop_->setEnabled(session_.state() != SessionState::Stopped);
    QString recordingText = QStringLiteral("Журнал: ") + QString::fromUtf8(recorder_.status().c_str());
    if (recorder_.active()) {
        const auto directory = recorder_.directory();
        if (!directory.empty()) recordingText += QStringLiteral("\n") + pathString(directory);
    }
    recordStatus_->setText(error.isEmpty() ? recordingText : QStringLiteral("ПОМИЛКА ЗАПИСУ: ") + error);
    recordStatus_->setStyleSheet(error.isEmpty() ? QStringLiteral("color:#91a5ba;") : QStringLiteral("color:#ff7e7e;"));
    record_->setText(recorder_.active() ? QStringLiteral("Зупинити запис") : QStringLiteral("Почати запис CSV + JSONL"));
}
void MainWindow::toggleRecording() {
    if (recorder_.active()) recorder_.stop();
    else {
        const auto directory = QFileDialog::getExistingDirectory(this, QStringLiteral("Папка для журналів HondaDash"), QDir::homePath());
        if (directory.isEmpty()) return;
        static constexpr std::array<const char*, 4> scenarios{"ignition", "idle", "demo", "manual"};
        recorder_.start(filePath(directory), {scenarios[static_cast<std::size_t>(scenario_->currentIndex())], static_cast<std::uint32_t>(seed_->value())});
    }
    refreshDashboard();
}
void MainWindow::advance(Time amount) {
    const Time end = managedNow_ + amount;
    while (managedNow_ < end) { managedNow_ = std::min(end, managedNow_ + 10); session_.tick(managedNow_); refreshDashboard(); }
}
void MainWindow::markOffscreenScreenshot() {
    warning_->setText(QStringLiteral("ЕМУЛЯЦІЯ — не підключено до автомобіля · знімок offscreen (без звичайного Windows GUI)"));
}
QJsonObject MainWindow::smokeTest(const QString& screenshotPath) {
    QJsonArray checks; bool passed = true;
    const auto check = [&](const QString& name, bool success) { checks.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("passed"), success}}); passed = passed && success; };
    check(QStringLiteral("managed_clock_enabled"), managedTime_);
    QFontMetrics metrics(QFont(QStringLiteral("Segoe UI"), 11));
    bool readableGlyphs = true;
    for (const QChar character : QStringLiteral("HondaDash0123456789ЕМУЛЯЦІЯЇЄҐобхв")) readableGlyphs = readableGlyphs && metrics.inFont(character);
    check(QStringLiteral("font_supports_dashboard_text"), readableGlyphs);
    scenario_->setCurrentIndex(3); start_->click(); advance(500);
    check(QStringLiteral("decoded_data_in_real_widgets"), session_.state() == SessionState::Running && tachometer_->reading().quality == Quality::Valid && tachometer_->reading().value.has_value() && chart_->sampleCount() > 0);
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
    check(QStringLiteral("repeated_start_stop_cancels_deliveries"), session_.state() == SessionState::Stopped && session_.pendingDeliveries() == 0);
    scenario_->setCurrentIndex(1); start_->click(); advance(1000);
    advance(70000);
    check(QStringLiteral("history_is_bounded_after_long_managed_run"), chart_->sampleCount() == HistoryChart::Capacity && session_.stats().accepted > HistoryChart::Capacity);
    QString screenshot1024;
    for (const QSize size : {QSize(1024, 600), QSize(1280, 720)}) {
        resize(size); centralWidget()->layout()->activate();
        check(QStringLiteral("layout_%1x%2").arg(size.width()).arg(size.height()), centralWidget()->width() <= size.width() && centralWidget()->height() <= size.height() && tachometer_->width() >= 240 && tachometer_->height() >= 245 && chart_->height() >= 118);
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
    const bool recordingStarted = recordingTemporaryDirectory.isValid() && recorder_.start(filePath(recordingParent), {"idle", static_cast<std::uint32_t>(seed_->value())});
    check(QStringLiteral("recording_started_with_unicode_space_path"), recordingStarted);
    advance(400);
    check(QStringLiteral("active_recording_visible_in_dashboard"), recorder_.active() && statistics_->text().contains(QStringLiteral("Журнал: ЗАПИС")));
    bool saved = false;
    if (!screenshotPath.isEmpty()) saved = grab().save(screenshotPath);
    if (!screenshotPath.isEmpty()) check(QStringLiteral("actual_widget_screenshot_saved"), saved);
    const auto accepted = session_.stats().accepted;
    close();
    check(QStringLiteral("close_stops_session_and_recording"), session_.state() == SessionState::Stopped && session_.pendingDeliveries() == 0 && !timer_->isActive() && !recorder_.active());
    QFile csv(pathString(recorder_.directory() / "measurements.csv"));
    QFile raw(pathString(recorder_.directory() / "raw.jsonl"));
    const bool csvOpened = csv.open(QIODevice::ReadOnly), rawOpened = raw.open(QIODevice::ReadOnly);
    const QByteArray csvBytes = csvOpened ? csv.readAll() : QByteArray{};
    const QByteArray rawBytes = rawOpened ? raw.readAll() : QByteArray{};
    csv.close(); raw.close();
    check(QStringLiteral("GUI_session_callbacks_record_decoded_csv_and_raw_bytes"), recorder_.error().empty() && csvBytes.contains(",Valid") && csvBytes.count('\n') >= 3 && rawBytes.contains("\"kind\":\"TX\"") && rawBytes.contains("\"kind\":\"RX\"") && rawBytes.contains("\"kind\":\"stop\""));
    return {{QStringLiteral("format_version"), 1}, {QStringLiteral("application"), QStringLiteral("HondaDash")},
        {QStringLiteral("profile"), QStringLiteral("synthetic-demo-v1")}, {QStringLiteral("passed"), passed},
        {QStringLiteral("result"), passed ? QStringLiteral("pass") : QStringLiteral("fail")},
        {QStringLiteral("clock"), QStringLiteral("managed monotonic milliseconds")}, {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("offscreen"), QGuiApplication::platformName() == QStringLiteral("offscreen")},
        {QStringLiteral("checks"), checks}, {QStringLiteral("accepted_samples_before_stop"), static_cast<qint64>(samplesBeforeStop)},
        {QStringLiteral("accepted_samples_final_session"), static_cast<qint64>(accepted)}, {QStringLiteral("screenshot"), screenshotPath},
        {QStringLiteral("screenshot_1024x600"), screenshot1024}, {QStringLiteral("offscreen_font_loaded"), qApp->property("offscreen_font_loaded").toBool()}};
}
void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F11) { isFullScreen() ? showNormal() : showFullScreen(); event->accept(); return; }
    if (event->key() == Qt::Key_Escape && isFullScreen()) { showNormal(); event->accept(); return; }
    QMainWindow::keyPressEvent(event);
}
void MainWindow::closeEvent(QCloseEvent* event) { timer_->stop(); session_.stop(now()); recorder_.stop(); event->accept(); }
}
