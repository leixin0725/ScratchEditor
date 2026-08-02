#include "editorcontroller.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char *argv[])
{
    QElapsedTimer startupTimer;
    startupTimer.start();

    // The basic loop avoids the extra threaded hand-off measured on large
    // QTextDocument updates while retaining GPU-backed Qt Quick rendering.
    if (qEnvironmentVariableIsEmpty("QSG_RENDER_LOOP")) {
        qputenv("QSG_RENDER_LOOP", QByteArrayLiteral("basic"));
    }

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ScratchEditor"));
    app.setApplicationDisplayName(QStringLiteral("ScratchEditor"));
    app.setOrganizationName(QStringLiteral("ScratchEditor"));
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Lightweight Qt Quick scratch editor"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("background"), QStringLiteral("Start resident and hidden")});
    parser.addOption({QStringLiteral("toggle"), QStringLiteral("Toggle an existing instance")});
    parser.addOption({QStringLiteral("show"), QStringLiteral("Show an existing instance")});
    parser.addOption({QStringLiteral("hide"), QStringLiteral("Hide an existing instance")});
    parser.addOption({QStringLiteral("test-mode"), QStringLiteral("Enable isolated benchmark commands")});
    parser.process(app);

    QString forwardedCommand;
    if (parser.isSet(QStringLiteral("toggle"))) {
        forwardedCommand = QStringLiteral("toggle");
    } else if (parser.isSet(QStringLiteral("show"))) {
        forwardedCommand = QStringLiteral("show");
    } else if (parser.isSet(QStringLiteral("hide"))) {
        forwardedCommand = QStringLiteral("hide");
    } else if (!parser.isSet(QStringLiteral("background"))) {
        forwardedCommand = QStringLiteral("show");
    }

    if (EditorController::forwardToExistingInstance(forwardedCommand)) {
        return 0;
    }

    EditorController controller(parser.isSet(QStringLiteral("test-mode")), &startupTimer);
    if (!controller.startServer()) {
        return 2;
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    engine.loadFromModule(QStringLiteral("ScratchEditor"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 3;
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller,
                     &EditorController::shutdown);

    if (!parser.isSet(QStringLiteral("background"))) {
        QTimer::singleShot(0, &controller, &EditorController::showEditor);
    }

    return app.exec();
}
