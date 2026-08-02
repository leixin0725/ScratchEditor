#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QThread>

namespace {

QString serverName()
{
    const QByteArray overrideName = qgetenv("SCRATCHEDITOR_SERVER_NAME");
    return overrideName.isEmpty() ? QStringLiteral("ScratchEditor.Stage1.v1")
                                  : QString::fromUtf8(overrideName);
}

QJsonObject request(const QString &command, const QJsonObject &arguments = {}, int timeoutMs = 3000)
{
    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (!socket.waitForConnected(timeoutMs)) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), socket.errorString()}};
    }

    QJsonObject body = arguments;
    body.insert(QStringLiteral("command"), command);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact) + '\n';
    if (socket.write(payload) != payload.size()) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), socket.errorString()}};
    }
    socket.flush();

    QByteArray response;
    while (!response.contains('\n')) {
        if (!socket.waitForReadyRead(timeoutMs)) {
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), socket.errorString()}};
        }
        response += socket.readAll();
    }
    const QJsonDocument document = QJsonDocument::fromJson(response.left(response.indexOf('\n')));
    return document.isObject() ? document.object()
                               : QJsonObject{{QStringLiteral("ok"), false},
                                             {QStringLiteral("error"),
                                              QStringLiteral("invalid response")}};
}

QJsonObject execute(const QString &commandId)
{
    return request(QStringLiteral("testExecuteCommand"),
                   {{QStringLiteral("commandId"), commandId}});
}

void addCheck(QJsonObject &checks, QJsonObject &details, const QString &name, bool passed,
              const QJsonValue &detail = {})
{
    checks.insert(name, passed);
    if (!detail.isUndefined()) {
        details.insert(name, detail);
    }
}

bool hasKey(const QJsonArray &keys, const QString &key)
{
    for (const QJsonValue &value : keys) {
        if (value.toString() == key) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QJsonObject checks;
    QJsonObject details;

    const QJsonObject initial = request(QStringLiteral("status"));
    const QString configFile = initial.value(QStringLiteral("settingsFile")).toString();
    addCheck(checks, details, QStringLiteral("centralSettingsReady"),
             initial.value(QStringLiteral("ok")).toBool()
                 && initial.value(QStringLiteral("testMode")).toBool()
                 && initial.value(QStringLiteral("settingsStatus")).toInt() == 0
                 && initial.value(QStringLiteral("settingsSchemaVersion")).toInt() == 1
                 && configFile.endsWith(QStringLiteral(".ini"), Qt::CaseInsensitive)
                 && QFileInfo::exists(configFile),
             initial);
    addCheck(checks, details, QStringLiteral("appearanceDefaults"),
             initial.value(QStringLiteral("theme")).toString() == QStringLiteral("dark")
                 && initial.value(QStringLiteral("editorFontFamily")).toString()
                    == QStringLiteral("Microsoft YaHei UI")
                 && initial.value(QStringLiteral("editorFontPointSize")).toInt() == 13
                 && initial.value(QStringLiteral("animationsEnabled")).toBool()
                 && initial.value(QStringLiteral("transitionDuration")).toInt() == 120
                 && initial.value(QStringLiteral("themeBackgroundColor")).toString()
                    == QStringLiteral("#252525"),
             initial);
    addCheck(checks, details, QStringLiteral("windowInteractionLayout"),
             initial.value(QStringLiteral("cornerResizeEnabled")).toBool()
                 && initial.value(QStringLiteral("edgeDragEnabled")).toBool()
                 && initial.value(QStringLiteral("windowShapeAnimationEnabled")).toBool()
                 && initial.value(QStringLiteral("resizeMargin")).toInt() >= 8
                 && initial.value(QStringLiteral("edgeDragWidth")).toInt() > 0
                 && initial.value(QStringLiteral("themeEditorSurfaceColor")).toString()
                    != initial.value(QStringLiteral("themeBackgroundColor")).toString(),
             initial);

    constexpr int animationStressRounds = 20;
    bool closingShapeStable = true;
    QJsonObject hiddenWindow;
    QJsonObject parkedWindow;
    QJsonObject reopenedWindow;
    for (int round = 0; round < animationStressRounds; ++round) {
        const QJsonObject opening = request(QStringLiteral("show"));
        QThread::msleep(180);
        const QJsonObject openedWindow = request(QStringLiteral("status"));
        const QJsonObject closing = request(QStringLiteral("hide"));
        QThread::msleep(180);
        hiddenWindow = request(QStringLiteral("status"));
        QThread::msleep(100);
        parkedWindow = request(QStringLiteral("status"));
        request(QStringLiteral("show"));
        QThread::msleep(180);
        reopenedWindow = request(QStringLiteral("status"));

        closingShapeStable = closingShapeStable
            && opening.value(QStringLiteral("visible")).toBool()
            && openedWindow.value(QStringLiteral("visible")).toBool()
            && !closing.value(QStringLiteral("visible")).toBool()
            && !hiddenWindow.value(QStringLiteral("visible")).toBool()
            && !hiddenWindow.value(QStringLiteral("windowTransitionActive")).toBool()
            && hiddenWindow.value(QStringLiteral("windowOpacity")).toDouble() <= 0.001
            && hiddenWindow.value(QStringLiteral("width")).toInt()
                < hiddenWindow.value(QStringLiteral("windowRestingWidth")).toInt()
            && hiddenWindow.value(QStringLiteral("height")).toInt()
                < hiddenWindow.value(QStringLiteral("windowRestingHeight")).toInt()
            && parkedWindow.value(QStringLiteral("x")).toInt()
                == hiddenWindow.value(QStringLiteral("x")).toInt()
            && parkedWindow.value(QStringLiteral("y")).toInt()
                == hiddenWindow.value(QStringLiteral("y")).toInt()
            && parkedWindow.value(QStringLiteral("width")).toInt()
                == hiddenWindow.value(QStringLiteral("width")).toInt()
            && parkedWindow.value(QStringLiteral("height")).toInt()
                == hiddenWindow.value(QStringLiteral("height")).toInt()
            && reopenedWindow.value(QStringLiteral("visible")).toBool()
            && reopenedWindow.value(QStringLiteral("width")).toInt()
                == reopenedWindow.value(QStringLiteral("windowRestingWidth")).toInt()
            && reopenedWindow.value(QStringLiteral("height")).toInt()
                == reopenedWindow.value(QStringLiteral("windowRestingHeight")).toInt()
            && reopenedWindow.value(QStringLiteral("windowOpacity")).toDouble() >= 0.999
            && reopenedWindow.value(
                   QStringLiteral("windowTransitionPreparationStable")).toBool();
    }
    addCheck(checks, details, QStringLiteral("closingShapeDoesNotRebound"), closingShapeStable,
             QJsonObject{{QStringLiteral("rounds"), animationStressRounds},
                         {QStringLiteral("hidden"), hiddenWindow},
                         {QStringLiteral("parked"), parkedWindow},
                         {QStringLiteral("reopened"), reopenedWindow}});
    request(QStringLiteral("hide"));
    QThread::msleep(180);

    const QJsonObject opened = execute(QStringLiteral("settings"));
    addCheck(checks, details, QStringLiteral("lazySettingsPage"),
             !initial.value(QStringLiteral("settingsPageLoaded")).toBool()
                 && opened.value(QStringLiteral("executed")).toBool()
                 && opened.value(QStringLiteral("settingsPageLoaded")).toBool()
                 && opened.value(QStringLiteral("settingsPageVisible")).toBool(),
             opened);
    const QJsonObject closed = request(QStringLiteral("testCloseOverlays"));
    addCheck(checks, details, QStringLiteral("settingsPageCloses"),
             closed.value(QStringLiteral("settingsClosed")).toBool()
                 && !closed.value(QStringLiteral("settingsPageLoaded")).toBool(),
             closed);

    const QJsonObject light = request(
        QStringLiteral("testApplyAppearance"),
        {{QStringLiteral("theme"), QStringLiteral("light")},
         {QStringLiteral("fontFamily"), QStringLiteral("Microsoft YaHei UI")},
         {QStringLiteral("fontPointSize"), 15},
         {QStringLiteral("animationsEnabled"), false}});
    addCheck(checks, details, QStringLiteral("appearanceApplies"),
             light.value(QStringLiteral("applied")).toBool()
                 && light.value(QStringLiteral("theme")).toString() == QStringLiteral("light")
                 && light.value(QStringLiteral("editorFontPointSize")).toInt() == 15
                 && !light.value(QStringLiteral("animationsEnabled")).toBool()
                 && light.value(QStringLiteral("transitionDuration")).toInt() == 0
                 && light.value(QStringLiteral("themeBackgroundColor")).toString()
                    == QStringLiteral("#f7f8fa")
                 && light.value(QStringLiteral("markdownHighlighting")).toBool(),
             light);

    const QJsonObject invalidTheme = request(
        QStringLiteral("testApplyAppearance"),
        {{QStringLiteral("theme"), QStringLiteral("system")},
         {QStringLiteral("fontFamily"), QStringLiteral("Microsoft YaHei UI")},
         {QStringLiteral("fontPointSize"), 15},
         {QStringLiteral("animationsEnabled"), true}});
    const QJsonObject invalidFont = request(
        QStringLiteral("testApplyAppearance"),
        {{QStringLiteral("theme"), QStringLiteral("dark")},
         {QStringLiteral("fontFamily"), QStringLiteral("ScratchEditor Missing Font")},
         {QStringLiteral("fontPointSize"), 15},
         {QStringLiteral("animationsEnabled"), true}});
    const QJsonObject invalidSize = request(
        QStringLiteral("testApplyAppearance"),
        {{QStringLiteral("theme"), QStringLiteral("dark")},
         {QStringLiteral("fontFamily"), QStringLiteral("Microsoft YaHei UI")},
         {QStringLiteral("fontPointSize"), 25},
         {QStringLiteral("animationsEnabled"), true}});
    addCheck(checks, details, QStringLiteral("appearanceValidation"),
             !invalidTheme.value(QStringLiteral("applied")).toBool()
                 && !invalidFont.value(QStringLiteral("applied")).toBool()
                 && !invalidSize.value(QStringLiteral("applied")).toBool()
                 && !invalidSize.value(QStringLiteral("settingsError")).toString().isEmpty()
                 && invalidSize.value(QStringLiteral("theme")).toString() == QStringLiteral("light"),
             invalidSize);

    const QJsonObject shortcut = request(
        QStringLiteral("testSetShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("toggleBold")},
         {QStringLiteral("sequence"), QStringLiteral("Ctrl+Alt+B")}});
    const QJsonObject config = request(QStringLiteral("testConfigKeys"));
    const QJsonArray keys = config.value(QStringLiteral("keys")).toArray();
    QFile settingsFile(configFile);
    const bool fileOpened = settingsFile.open(QIODevice::ReadOnly | QIODevice::Text);
    const QString fileText = fileOpened ? QString::fromUtf8(settingsFile.readAll()) : QString();
    addCheck(checks, details, QStringLiteral("configurationIsCentralized"),
             shortcut.value(QStringLiteral("configured")).toBool()
                 && config.value(QStringLiteral("settingsFile")).toString() == configFile
                 && hasKey(keys, QStringLiteral("meta/schemaVersion"))
                 && hasKey(keys, QStringLiteral("appearance/theme"))
                 && hasKey(keys, QStringLiteral("editor/fontFamily"))
                 && hasKey(keys, QStringLiteral("editor/fontPointSize"))
                 && hasKey(keys, QStringLiteral("ui/animationsEnabled"))
                 && hasKey(keys, QStringLiteral("shortcuts/toggleBold"))
                 && fileText.contains(QStringLiteral("[appearance]"))
                 && fileText.contains(QStringLiteral("[editor]"))
                 && fileText.contains(QStringLiteral("[ui]"))
                 && fileText.contains(QStringLiteral("[shortcuts]")),
             config);

    const QStringList excludedCommands{
        QStringLiteral("togglePreview"), QStringLiteral("historyDrafts"),
        QStringLiteral("tabs"), QStringLiteral("pinnedDraft"),
        QStringLiteral("multiCursor"), QStringLiteral("plugins"), QStringLiteral("lsp"),
    };
    bool excluded = true;
    for (const QString &commandId : excludedCommands) {
        excluded = excluded && !execute(commandId).value(QStringLiteral("executed")).toBool();
    }
    addCheck(checks, details, QStringLiteral("deferredFeaturesExcluded"), excluded);

    bool allPassed = true;
    for (auto it = checks.constBegin(); it != checks.constEnd(); ++it) {
        allPassed = allPassed && it.value().toBool();
    }
    const QJsonObject result{
        {QStringLiteral("allPassed"), allPassed},
        {QStringLiteral("checks"), checks},
        {QStringLiteral("details"), details},
    };
    const QByteArray output = QJsonDocument(result).toJson(QJsonDocument::Indented);
    fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout);
    return allPassed ? 0 : 1;
}
