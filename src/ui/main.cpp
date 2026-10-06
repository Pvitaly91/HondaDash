#include "ui/main_window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QDir>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QTextStream>
#include <QTimer>
#include <QVariant>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("HondaDash"));
    QApplication::setApplicationVersion(QStringLiteral("0.5.0"));
#ifdef _WIN32
    // Windows' offscreen platform may have no system font discovery. Load the
    // user's already installed font for test rendering; no font is redistributed.
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        const QDir systemDirectory(qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows")));
        const int regular = QFontDatabase::addApplicationFont(systemDirectory.filePath(QStringLiteral("Fonts/segoeui.ttf")));
        QFontDatabase::addApplicationFont(systemDirectory.filePath(QStringLiteral("Fonts/segoeuib.ttf")));
        application.setProperty("offscreen_font_loaded", regular >= 0);
        if (regular >= 0) QApplication::setFont(QFont(QStringLiteral("Segoe UI"), 11));
    }
#endif
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("HondaDash M2c · synthetic M0/M1, Honda DLC offline та віртуальний міст"));
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({QStringLiteral("smoke-test"), QStringLiteral("Перевірити справжні віджети з керованим часом та завершитися.")});
    parser.addOption({QStringLiteral("report"), QStringLiteral("Записати JSON-звіт smoke test."), QStringLiteral("path")});
    parser.addOption({QStringLiteral("screenshot"), QStringLiteral("Зберегти PNG фактичного вікна під час smoke test."), QStringLiteral("path")});
    parser.process(application);
    const bool smoke = parser.isSet(QStringLiteral("smoke-test"));
    if (!smoke && (parser.isSet(QStringLiteral("report")) || parser.isSet(QStringLiteral("screenshot")))) {
        QTextStream(stderr) << "--report and --screenshot require --smoke-test\n"; return 2;
    }
    hd::ui::MainWindow window(smoke);
    if (smoke && QGuiApplication::platformName() == QStringLiteral("offscreen") && parser.isSet(QStringLiteral("screenshot"))) window.markOffscreenScreenshot();
    window.show();
    if (smoke) {
        application.setQuitOnLastWindowClosed(false);
        QTimer::singleShot(0, &window, [&] {
            auto report = window.smokeTest(parser.value(QStringLiteral("screenshot")));
            auto json = QJsonDocument(report).toJson(QJsonDocument::Indented);
            int result = report.value(QStringLiteral("passed")).toBool() ? 0 : 1;
            if (parser.isSet(QStringLiteral("report"))) {
                QFile output(parser.value(QStringLiteral("report")));
                if (!output.open(QIODevice::WriteOnly) || output.write(json) != json.size()) { QTextStream(stderr) << "Cannot write smoke report\n"; result = 2; }
            }
            QTextStream(stdout) << json;
            application.exit(result);
        });
    }
    return application.exec();
}
