#include "editorcontroller.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
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

    // Keep scene graph rendering off the GUI thread. The basic loop blocks the
    // native Windows sizing loop behind every TextEdit relayout and makes
    // interactive edge/corner resizing visibly stall, especially for wrapped
    // Markdown documents.
    if (qEnvironmentVariableIsEmpty("QSG_RENDER_LOOP")) {
        qputenv("QSG_RENDER_LOOP", QByteArrayLiteral("threaded"));
    }
#ifdef Q_OS_WIN
    // During native live resizing, the Windows client area can grow before a
    // vsync-blocked Qt Quick render thread has resized and presented its
    // swapchain. Let Qt's threaded loop use timer-driven presentation so the
    // new client pixels receive a frame without waiting for the next vsync.
    if (qEnvironmentVariableIsEmpty("QSG_NO_VSYNC")) {
        qputenv("QSG_NO_VSYNC", QByteArrayLiteral("1"));
    }
    // Keep render-thread and GUI-thread animations tied to elapsed time when
    // presentation is not used as the animation clock.
    if (qEnvironmentVariableIsEmpty("QSG_USE_SIMPLE_ANIMATION_DRIVER")) {
        qputenv("QSG_USE_SIMPLE_ANIMATION_DRIVER", QByteArrayLiteral("1"));
    }
#endif

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
    parser.addOption({QStringLiteral("wait"),
                      QStringLiteral("Wait until editing the provided file is complete")});
    parser.addOption({QStringLiteral("test-mode"), QStringLiteral("Enable isolated benchmark commands")});
    parser.addPositionalArgument(QStringLiteral("file"),
                                 QStringLiteral("UTF-8 file to edit for an external CLI"),
                                 QStringLiteral("[file]"));
    parser.process(app);

    const QStringList positionalArguments = parser.positionalArguments();
    if (positionalArguments.size() > 1) {
        qCritical().noquote() << QStringLiteral("ScratchEditor accepts at most one file path");
        return 4;
    }
    const QString externalFilePath = positionalArguments.value(0);
    const bool externalFileMode = !externalFilePath.isEmpty();
    const bool residentOptionSet = parser.isSet(QStringLiteral("background"))
        || parser.isSet(QStringLiteral("toggle"))
        || parser.isSet(QStringLiteral("show"))
        || parser.isSet(QStringLiteral("hide"));
    if (externalFileMode && residentOptionSet) {
        qCritical().noquote()
            << QStringLiteral("File editing cannot be combined with resident window options");
        return 4;
    }
    if (parser.isSet(QStringLiteral("wait")) && !externalFileMode) {
        qCritical().noquote() << QStringLiteral("--wait requires a file path");
        return 4;
    }

    QString forwardedCommand;
    if (externalFileMode) {
        forwardedCommand.clear();
    } else if (parser.isSet(QStringLiteral("toggle"))) {
        forwardedCommand = QStringLiteral("toggle");
    } else if (parser.isSet(QStringLiteral("show"))) {
        forwardedCommand = QStringLiteral("show");
    } else if (parser.isSet(QStringLiteral("hide"))) {
        forwardedCommand = QStringLiteral("hide");
    } else if (!parser.isSet(QStringLiteral("background"))) {
        forwardedCommand = QStringLiteral("show");
    }

    if (!externalFileMode && EditorController::forwardToExistingInstance(forwardedCommand)) {
        return 0;
    }

    EditorController controller(parser.isSet(QStringLiteral("test-mode")), &startupTimer,
                                externalFilePath);
    if (externalFileMode && !controller.externalFileReady()) {
        qCritical().noquote() << controller.externalFileError();
        return 4;
    }
    if (!externalFileMode && !controller.startServer()) {
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

    if (externalFileMode || !parser.isSet(QStringLiteral("background"))) {
        QTimer::singleShot(0, &controller, &EditorController::showEditor);
    }
    if (externalFileMode && controller.testMode()
        && qEnvironmentVariableIsSet("SCRATCHEDITOR_EXTERNAL_TEST_TEXT")) {
        const QString replacementText = qEnvironmentVariable(
            "SCRATCHEDITOR_EXTERNAL_TEST_TEXT");
        QTimer::singleShot(500, &controller, [&controller, replacementText] {
            controller.completeExternalFileTest(replacementText);
        });
    }

    return app.exec();
}
