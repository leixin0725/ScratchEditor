#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QRect>
#include <QSize>
#include <QThread>
#include <QVector>

#include "windowplacement.h"

namespace {

QString serverName()
{
    const QByteArray overrideName = qgetenv("SCRATCHEDITOR_SERVER_NAME");
    return overrideName.isEmpty() ? QStringLiteral("ScratchEditor.WindowUi.Validation")
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

QJsonObject rectToJson(const QRect &rect)
{
    return {{QStringLiteral("x"), rect.x()},
            {QStringLiteral("y"), rect.y()},
            {QStringLiteral("width"), rect.width()},
            {QStringLiteral("height"), rect.height()}};
}

void addPlacementUnitChecks(QJsonObject &checks, QJsonObject &details)
{
    using WindowPlacement::fitRestoredGeometry;
    using WindowPlacement::nativeToLogicalRect;
    using WindowPlacement::placeNearWindow;
    const QSize minimum(500, 320);
    const QSize defaultSize(920, 640);

    const QVector<QRect> twoScreens{
        QRect(0, 0, 1920, 1080), QRect(1920, 0, 1920, 1080)};
    const QVector<QRect> singleScreen{QRect(0, 0, 1920, 1080)};
    const QVector<QRect> smallScreen{QRect(0, 0, 400, 300)};
    const QVector<QRect> smallReferenceScreen{
        QRect(0, 0, 1000, 700), QRect(1920, 0, 1920, 1080)};

    const QRect mixedDpiMapped = nativeToLogicalRect(
        QRect(2304, 180, 1024, 720), QRect(2048, 0, 2560, 1440),
        QRect(2048, 0, 2048, 1152));
    addCheck(checks, details, QStringLiteral("placementMapsMixedDpiSecondaryOrigin"),
             mixedDpiMapped == QRect(2253, 144, 819, 576),
             rectToJson(mixedDpiMapped));

    const QRect leftScreenMapped = nativeToLogicalRect(
        QRect(-2304, 160, 1024, 640), QRect(-2560, 0, 2560, 1600),
        QRect(-2560, 0, 2048, 1280));
    addCheck(checks, details, QStringLiteral("placementMapsNegativeSecondaryOrigin"),
             leftScreenMapped == QRect(-2355, 128, 819, 512),
             rectToJson(leftScreenMapped));

    const auto inside = fitRestoredGeometry(QRect(100, 100, 800, 600), minimum, twoScreens);
    addCheck(checks, details, QStringLiteral("placementFitRestoredInside"),
             inside && *inside == QRect(100, 100, 800, 600),
             inside ? QJsonValue(rectToJson(*inside)) : QJsonValue(false));

    const auto straddle = fitRestoredGeometry(QRect(1900, 100, 100, 600), minimum, twoScreens);
    addCheck(checks, details, QStringLiteral("placementFitRestoredStraddle"),
             straddle && *straddle == QRect(1920, 100, 500, 600),
             straddle ? QJsonValue(rectToJson(*straddle)) : QJsonValue(false));

    const auto offScreens = fitRestoredGeometry(QRect(5000, 5000, 800, 600), minimum, twoScreens);
    addCheck(checks, details, QStringLiteral("placementFitRestoredOffScreensInvalid"),
             !offScreens.has_value(), offScreens ? QJsonValue(rectToJson(*offScreens))
                                                 : QJsonValue(QStringLiteral("invalid")));

    const auto oversize = fitRestoredGeometry(QRect(0, 0, 3000, 1500), minimum, singleScreen);
    addCheck(checks, details, QStringLiteral("placementFitRestoredShrinksOversize"),
             oversize && *oversize == QRect(0, 0, 1920, 1080),
             oversize ? QJsonValue(rectToJson(*oversize)) : QJsonValue(false));

    const auto undersize = fitRestoredGeometry(QRect(0, 0, 300, 200), minimum, singleScreen);
    addCheck(checks, details, QStringLiteral("placementFitRestoredEnforcesMinimum"),
             undersize && *undersize == QRect(0, 0, 500, 320),
             undersize ? QJsonValue(rectToJson(*undersize)) : QJsonValue(false));

    const auto minOverflow =
        fitRestoredGeometry(QRect(0, 0, 1000, 800), minimum, smallScreen);
    addCheck(checks, details, QStringLiteral("placementFitRestoredMinimumLegal"),
             minOverflow && *minOverflow == QRect(0, 0, 500, 320),
             minOverflow ? QJsonValue(rectToJson(*minOverflow)) : QJsonValue(false));

    const QRect reference(100, 100, 800, 60);
    const auto rememberedAnchor = placeNearWindow(
        QSize(920, 640), defaultSize, minimum, singleScreen, reference, QRect());
    addCheck(checks, details, QStringLiteral("placementNearRememberedRightAnchor"),
             rememberedAnchor == QRect(916, 100, 920, 640),
             rectToJson(rememberedAnchor));

    const auto defaultFallback = placeNearWindow(
        QSize(3000, 2000), defaultSize, minimum, singleScreen, reference, QRect());
    addCheck(checks, details, QStringLiteral("placementNearDefaultSizeFallback"),
             defaultFallback == QRect(916, 100, 920, 640),
             rectToJson(defaultFallback));

    const auto shrunk = placeNearWindow(
        QSize(2000, 2000), QSize(2000, 2000), minimum, singleScreen, reference, QRect());
    addCheck(checks, details, QStringLiteral("placementNearShrinksToFit"),
             shrunk == QRect(0, 0, 1920, 1080), rectToJson(shrunk));

    const auto minLegal = placeNearWindow(
        QSize(), defaultSize, minimum, smallScreen, QRect(0, 0, 200, 100), QRect());
    addCheck(checks, details, QStringLiteral("placementNearMinimumLegal"),
             minLegal == QRect(0, 0, 500, 320), rectToJson(minLegal));

    const auto anchorOrder = placeNearWindow(
        QSize(920, 640), defaultSize, minimum, singleScreen, reference, QRect(0, 0, 50, 50));
    addCheck(checks, details, QStringLiteral("placementNearAnchorOrderRightFirst"),
             anchorOrder == QRect(916, 100, 920, 640), rectToJson(anchorOrder));

    const auto avoidOverlap = placeNearWindow(
        QSize(500, 320), QSize(500, 320), minimum, singleScreen, QRect(0, 0, 800, 60),
        QRect(816, 0, 300, 300));
    addCheck(checks, details, QStringLiteral("placementNearAvoidsObstacleOverlap"),
             avoidOverlap == QRect(0, 76, 500, 320), rectToJson(avoidOverlap));

    const auto referenceScreen = placeNearWindow(
        QSize(1500, 900), defaultSize, minimum, twoScreens, QRect(2000, 100, 800, 60),
        QRect());
    addCheck(checks, details, QStringLiteral("placementNearPrefersReferenceScreen"),
             referenceScreen == QRect(2000, 176, 1500, 900), rectToJson(referenceScreen));

    const auto degradedOnReferenceScreen = placeNearWindow(
        QSize(1500, 900), defaultSize, minimum, smallReferenceScreen,
        QRect(100, 100, 800, 60), QRect());
    addCheck(checks, details, QStringLiteral("placementNearDegradesSizeOnReferenceScreen"),
             degradedOnReferenceScreen == QRect(40, 30, 920, 640),
             rectToJson(degradedOnReferenceScreen));

    const auto noReference = placeNearWindow(
        QSize(), defaultSize, minimum, singleScreen, std::nullopt, QRect());
    addCheck(checks, details, QStringLiteral("placementNearCentersWithoutReference"),
             noReference == QRect(500, 220, 920, 640), rectToJson(noReference));
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QJsonObject checks;
    QJsonObject details;

    addPlacementUnitChecks(checks, details);

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
                     == QStringLiteral("#252525")
                  && initial.value(QStringLiteral("themeAccentColor")).toString()
                     == QStringLiteral("#85c7c0")
                  && initial.value(QStringLiteral("themeSelectionColor")).toString()
                     == QStringLiteral("#85c7c0")
                  && initial.value(QStringLiteral("selectionDragColor")).toString()
                     == QStringLiteral("#85c7c0"),
             initial);
    addCheck(checks, details, QStringLiteral("windowInteractionLayout"),
             initial.value(QStringLiteral("cornerResizeEnabled")).toBool()
                 && initial.value(QStringLiteral("edgeDragEnabled")).toBool()
                 && initial.value(QStringLiteral("windowShapeAnimationEnabled")).toBool()
                 && initial.value(QStringLiteral("renderLoop")).toString()
                    == QStringLiteral("threaded")
                 && initial.value(
                        QStringLiteral("resizePresentationUnthrottled")).toBool()
                 && initial.value(QStringLiteral("simpleAnimationDriver")).toBool()
                 && initial.value(QStringLiteral("resizeMargin")).toInt() >= 8
                 && initial.value(QStringLiteral("edgeDragWidth")).toInt() > 0
                 && initial.value(QStringLiteral("themeEditorSurfaceColor")).toString()
                    != initial.value(QStringLiteral("themeBackgroundColor")).toString(),
             initial);

    constexpr int animationStressRounds = 20;
    constexpr int animationSettleMs = 240;
    bool closingShapeStable = true;
    QJsonObject hiddenWindow;
    QJsonObject parkedWindow;
    QJsonObject reopenedWindow;
    for (int round = 0; round < animationStressRounds; ++round) {
        const QJsonObject opening = request(QStringLiteral("show"));
        QThread::msleep(animationSettleMs);
        const QJsonObject openedWindow = request(QStringLiteral("status"));
        const QJsonObject closing = request(QStringLiteral("hide"));
        QThread::msleep(animationSettleMs);
        hiddenWindow = request(QStringLiteral("status"));
        QThread::msleep(100);
        parkedWindow = request(QStringLiteral("status"));
        request(QStringLiteral("show"));
        QThread::msleep(animationSettleMs);
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

    const QJsonObject residentGeometry = request(QStringLiteral("getWindowGeometry"));
    const QJsonObject residentStatus = request(QStringLiteral("status"));
    addCheck(checks, details, QStringLiteral("residentGeometryQuery"),
             residentGeometry.value(QStringLiteral("valid")).toBool()
                 && residentGeometry.value(QStringLiteral("width")).toInt() > 0
                 && residentGeometry.value(QStringLiteral("height")).toInt() > 0
                 && residentGeometry.value(QStringLiteral("width")).toInt()
                     == residentStatus.value(QStringLiteral("windowRestingWidth")).toInt()
                 && residentGeometry.value(QStringLiteral("height")).toInt()
                     == residentStatus.value(QStringLiteral("windowRestingHeight")).toInt(),
             residentGeometry);

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
                  && light.value(QStringLiteral("themeAccentColor")).toString()
                     == QStringLiteral("#85c7c0")
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
