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

#include "statuspanelhints.h"
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

QJsonObject keyPress(const QString &text = {}, const QString &key = {}, bool shift = false,
                     const QString &modifiers = {})
{
    return request(QStringLiteral("testKeyPress"),
                   {{QStringLiteral("text"), text},
                    {QStringLiteral("key"), key},
                    {QStringLiteral("shift"), shift},
                    {QStringLiteral("modifiers"), modifiers}});
}

QJsonObject historyAction(const QString &action, const QString &value = {})
{
    return request(QStringLiteral("testClipboardHistoryUiAction"),
                   {{QStringLiteral("action"), action},
                    {QStringLiteral("value"), value}}, 5000);
}

QJsonObject dragHistoryUi(const QString &id, int dropPosition,
                          bool activateDrag = true, bool outsideEditor = false,
                          bool finishDrag = true, const QString &previewText = {})
{
    return request(QStringLiteral("testClipboardHistoryDragUi"),
                   {{QStringLiteral("id"), id},
                    {QStringLiteral("dropPosition"), dropPosition},
                    {QStringLiteral("activateDrag"), activateDrag},
                    {QStringLiteral("outsideEditor"), outsideEditor},
                    {QStringLiteral("finishDrag"), finishDrag},
                    {QStringLiteral("previewText"), previewText}}, 5000);
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

bool hasHistoryPanelCornerShape(const QJsonObject &status)
{
    return qAbs(status.value(QStringLiteral("historyPanelTopLeftRadius")).toDouble() - 8.0)
               <= 0.001
        && qAbs(status.value(QStringLiteral("historyPanelBottomLeftRadius")).toDouble() - 8.0)
               <= 0.001
        && qAbs(status.value(QStringLiteral("historyPanelTopRightRadius")).toDouble()) <= 0.001
        && qAbs(status.value(QStringLiteral("historyPanelBottomRightRadius")).toDouble()) <= 0.001;
}

QJsonObject historyPanelCornerDetails(const QJsonObject &status)
{
    return {{QStringLiteral("actual"),
             QJsonObject{{QStringLiteral("topLeft"),
                          status.value(QStringLiteral("historyPanelTopLeftRadius"))},
                         {QStringLiteral("topRight"),
                          status.value(QStringLiteral("historyPanelTopRightRadius"))},
                         {QStringLiteral("bottomLeft"),
                          status.value(QStringLiteral("historyPanelBottomLeftRadius"))},
                         {QStringLiteral("bottomRight"),
                          status.value(QStringLiteral("historyPanelBottomRightRadius"))}}},
            {QStringLiteral("expected"),
             QJsonObject{{QStringLiteral("topLeft"), 8},
                         {QStringLiteral("topRight"), 0},
                         {QStringLiteral("bottomLeft"), 8},
                         {QStringLiteral("bottomRight"), 0}}}};
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
                 && initial.value(QStringLiteral("settingsSchemaVersion")).toInt() == 4
                 && configFile.endsWith(QStringLiteral(".ini"), Qt::CaseInsensitive)
                 && QFileInfo::exists(configFile),
             initial);
    addCheck(checks, details, QStringLiteral("uiConfigDefaults"),
             initial.value(QStringLiteral("uiConfigLoaded")).toBool()
                 && initial.value(QStringLiteral("uiConfigFile")).toString()
                        .endsWith(QStringLiteral("ui.json"))
                 && QFileInfo::exists(
                        initial.value(QStringLiteral("uiConfigFile")).toString())
                 && initial.value(QStringLiteral("commandPaletteMaximumWidth")).toInt() == 620
                 && initial.value(QStringLiteral("historyHoverOpenDelayMs")).toInt() == 100
                 && initial.value(QStringLiteral("historyHoverCloseDelayMs")).toInt() == 250
                 && qAbs(initial.value(
                        QStringLiteral("headingNavigationHighlightOpacity")).toDouble() - 0.2)
                    <= 0.001
                 && initial.value(
                        QStringLiteral("headingNavigationHighlightHoldDurationMs")).toInt()
                    == 400
                 && initial.value(
                        QStringLiteral("headingNavigationHighlightFadeDurationMs")).toInt()
                    == 300,
             initial);
    addCheck(checks, details, QStringLiteral("fileDropAreaCoversOnlyEditorViewport"),
             !initial.value(QStringLiteral("fileDropEnabled")).toBool()
                 && qAbs(initial.value(QStringLiteral("fileDropAreaX")).toDouble()
                         - initial.value(QStringLiteral("editorViewportX")).toDouble()) <= 0.5
                 && qAbs(initial.value(QStringLiteral("fileDropAreaY")).toDouble()
                         - initial.value(QStringLiteral("editorViewportY")).toDouble()) <= 0.5
                 && qAbs(initial.value(QStringLiteral("fileDropAreaWidth")).toDouble()
                         - initial.value(QStringLiteral("editorViewportWidth")).toDouble()) <= 0.5
                 && qAbs(initial.value(QStringLiteral("fileDropAreaHeight")).toDouble()
                         - initial.value(QStringLiteral("editorViewportHeight")).toDouble()) <= 0.5,
             initial);
    addCheck(checks, details, QStringLiteral("appearanceDefaults"),
             initial.value(QStringLiteral("theme")).toString() == QStringLiteral("dark")
                 && initial.value(QStringLiteral("editorFontFamily")).toString()
                    == QStringLiteral("Consolas")
                 && initial.value(QStringLiteral("editorFallbackFontFamily")).toString()
                    == QStringLiteral("NSimSun")
                 && initial.value(QStringLiteral("editorFontFamilies")).toArray()
                    == QJsonArray{QStringLiteral("Consolas"), QStringLiteral("NSimSun")}
                 && initial.value(QStringLiteral("editorFontPointSize")).toInt() == 13
                 && initial.value(QStringLiteral("editorFontWeight")).toInt() == 400
                 && initial.value(QStringLiteral("animationsEnabled")).toBool()
                 && initial.value(QStringLiteral("statusPanelFontSize")).toInt() == 10
                 && initial.value(QStringLiteral("statusPanelShowDelayMs")).toInt() == 300
                 && initial.value(QStringLiteral("statusPanelHideDelayMs")).toInt() == 250
                 && initial.value(QStringLiteral("statusPanelMaxWidth")).toInt() == 360
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

    const QString headingFoldUiText = QStringLiteral(
        "# Root\nbody\n## Child\nchild\n# Next\nnext");
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), headingFoldUiText}});
    const int headingFoldBodyCursor =
        headingFoldUiText.indexOf(QStringLiteral("body")) + 2;
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), headingFoldBodyCursor},
             {QStringLiteral("end"), headingFoldBodyCursor}});
    QThread::msleep(40);
    const QJsonObject headingFoldExpanded = request(QStringLiteral("status"));
    const QJsonObject headingFoldClicked = request(
        QStringLiteral("testClickHeadingFoldMarker"),
        {{QStringLiteral("position"), 0}});
    QThread::msleep(40);
    const QJsonObject headingFoldCollapsed = request(QStringLiteral("status"));
    const QJsonObject headingFoldCollapsedState = request(
        QStringLiteral("testHeadingFoldState"));
    const QJsonObject headingFoldExpandedAgain = request(
        QStringLiteral("testClickHeadingFoldMarker"),
        {{QStringLiteral("position"), 0}});
    QThread::msleep(40);
    const QJsonObject headingFoldExpandedState = request(
        QStringLiteral("testHeadingFoldState"));
    addCheck(checks, details, QStringLiteral("headingFoldGutterPersistentMarkers"),
             headingFoldExpanded.value(QStringLiteral("headingFoldGutterWidth")).toInt() == 16
                 && headingFoldExpanded.value(QStringLiteral("headingFoldMarkerCount")).toInt()
                    == 3
                 && headingFoldExpanded.value(
                        QStringLiteral("headingFoldIconSize")).toInt() == 16
                 && headingFoldExpanded.value(
                        QStringLiteral("headingFoldExpandedIconName")).toString()
                    == QStringLiteral("chevron-down")
                 && headingFoldExpanded.value(
                        QStringLiteral("headingFoldCollapsedIconName")).toString()
                    == QStringLiteral("chevron-right")
                 && headingFoldExpanded.value(
                        QStringLiteral("headingFoldExpandedColor")).toString()
                    != headingFoldExpanded.value(QStringLiteral("themeAccentColor")).toString()
                 && headingFoldExpanded.value(
                        QStringLiteral("headingFoldCollapsedColor")).toString()
                    == headingFoldExpanded.value(QStringLiteral("themeAccentColor")).toString(),
             headingFoldExpanded);
    addCheck(checks, details, QStringLiteral("headingFoldGutterClickTogglesSection"),
             headingFoldClicked.value(QStringLiteral("markerFound")).toBool()
                 && headingFoldClicked.value(QStringLiteral("markerIconName")).toString()
                    == QStringLiteral("chevron-right")
                 && headingFoldClicked.value(QStringLiteral("markerIconValid")).toBool()
                 && headingFoldClicked.value(QStringLiteral("markerIconSize")).toInt() == 16
                 && headingFoldCollapsedState.value(
                        QStringLiteral("collapsedHeadingCount")).toInt() == 1
                 && headingFoldClicked.value(QStringLiteral("cursorPosition")).toInt()
                    == QStringLiteral("# Root").size()
                 && headingFoldCollapsed.value(QStringLiteral("headingFoldMarkerCount")).toInt()
                    == 2
                 && headingFoldCollapsed.value(
                        QStringLiteral("editorVisibleContentHeight")).toDouble()
                    < headingFoldExpanded.value(
                        QStringLiteral("editorVisibleContentHeight")).toDouble()
                 && headingFoldExpandedAgain.value(QStringLiteral("markerFound")).toBool()
                 && headingFoldExpandedAgain.value(QStringLiteral("cursorPosition")).toInt()
                    == headingFoldBodyCursor
                 && headingFoldExpandedAgain.value(
                        QStringLiteral("markerIconName")).toString()
                    == QStringLiteral("chevron-down")
                 && headingFoldExpandedAgain.value(
                        QStringLiteral("markerIconValid")).toBool()
                 && headingFoldExpandedState.value(
                        QStringLiteral("collapsedHeadingCount")).toInt() == 0,
             QJsonObject{{QStringLiteral("expanded"), headingFoldExpanded},
                         {QStringLiteral("clicked"), headingFoldClicked},
                         {QStringLiteral("collapsed"), headingFoldCollapsed},
                         {QStringLiteral("collapsedState"), headingFoldCollapsedState},
                         {QStringLiteral("expandedAgain"), headingFoldExpandedAgain},
                         {QStringLiteral("expandedState"), headingFoldExpandedState}});

    const QString navigationHighlightText = QStringLiteral("前言\n  ## ")
        + QString(120, QLatin1Char('W')) + QStringLiteral("   \nbody");
    const int navigationHighlightStart =
        navigationHighlightText.indexOf(QStringLiteral("##"));
    const int navigationHighlightEnd =
        navigationHighlightText.indexOf(QStringLiteral("   \n"));
    request(QStringLiteral("show"));
    QThread::msleep(180);
    const QJsonObject visibleFileDrop = request(QStringLiteral("status"));
    addCheck(checks, details, QStringLiteral("fileDropEnabledForVisibleEditor"),
             visibleFileDrop.value(QStringLiteral("visible")).toBool()
                 && visibleFileDrop.value(QStringLiteral("fileDropEnabled")).toBool(),
             visibleFileDrop);
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), navigationHighlightText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}});
    const QJsonObject navigationHighlighted = execute(QStringLiteral("nextHeading"));
    QThread::msleep(450);
    const QJsonObject navigationHighlightFading = request(QStringLiteral("status"));
    QThread::msleep(350);
    const QJsonObject navigationHighlightFinished = request(QStringLiteral("status"));
    const QJsonObject navigationHighlight = navigationHighlighted.value(
        QStringLiteral("headingNavigationHighlight")).toObject();
    addCheck(checks, details, QStringLiteral("headingNavigationHighlightVisualFeedback"),
             navigationHighlight.value(QStringLiteral("start")).toInt()
                    == navigationHighlightStart
                 && navigationHighlight.value(QStringLiteral("end")).toInt()
                    == navigationHighlightEnd
                 && navigationHighlighted.value(
                        QStringLiteral("headingNavigationHighlightVisible")).toBool()
                 && navigationHighlighted.value(
                        QStringLiteral("headingNavigationHighlightRectCount")).toInt() >= 2
                 && navigationHighlighted.value(
                        QStringLiteral("headingNavigationHighlightDrawnBeforeText")).toBool()
                 && qAbs(navigationHighlighted.value(
                        QStringLiteral("headingNavigationHighlightEffectiveOpacity")).toDouble()
                         - 0.2) <= 0.02
                 && navigationHighlighted.value(
                        QStringLiteral("headingNavigationHighlightMaxWidth")).toDouble()
                    < navigationHighlighted.value(QStringLiteral("editorVisibleWidth")).toDouble()
                 && navigationHighlightFading.value(
                        QStringLiteral("headingNavigationHighlightEffectiveOpacity")).toDouble()
                    < 0.2
                 && navigationHighlightFading.value(
                        QStringLiteral("headingNavigationHighlightEffectiveOpacity")).toDouble()
                    > 0.0
                 && !navigationHighlightFinished.value(
                        QStringLiteral("headingNavigationHighlightVisible")).toBool(),
             QJsonObject{{QStringLiteral("started"), navigationHighlighted},
                         {QStringLiteral("fading"), navigationHighlightFading},
                         {QStringLiteral("finished"), navigationHighlightFinished}});
    request(QStringLiteral("hide"));
    QThread::msleep(180);
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), QString()}});
    const QJsonArray initialHints = initial.value(QStringLiteral("statusPanelHints")).toArray();
    const QStringList expectedHints = StatusPanelHints::forMode(false);
    bool hintsMatch = initialHints.size() == expectedHints.size();
    for (int index = 0; index < initialHints.size() && hintsMatch; ++index) {
        hintsMatch = initialHints.at(index).toString() == expectedHints.at(index);
    }
    addCheck(checks, details, QStringLiteral("statusPanelHintsDefault"), hintsMatch, initial);
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

    const QString headingFoldRenderText = QStringLiteral(
        "# Root\nMMMMMMMMMMMMMMMMMMMM");
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), headingFoldRenderText}});
    QThread::msleep(40);
    const QJsonObject headingFoldRenderedExpanded = request(
        QStringLiteral("testEditorRenderSample"),
        {{QStringLiteral("position"), 7}, {QStringLiteral("width"), 240}}, 5000);
    const QJsonObject headingFoldRenderClicked = request(
        QStringLiteral("testClickHeadingFoldMarker"),
        {{QStringLiteral("position"), 0}});
    const QJsonObject headingFoldRenderedCollapsed = request(
        QStringLiteral("testEditorRenderSample"),
        {{QStringLiteral("sampleRect"),
          headingFoldRenderedExpanded.value(QStringLiteral("sampleRect"))}}, 5000);
    const QJsonObject headingFoldRenderExpandedAgain = request(
        QStringLiteral("testClickHeadingFoldMarker"),
        {{QStringLiteral("position"), 0}});
    const QJsonObject headingFoldRenderedRestored = request(
        QStringLiteral("testEditorRenderSample"),
        {{QStringLiteral("sampleRect"),
          headingFoldRenderedExpanded.value(QStringLiteral("sampleRect"))}}, 5000);
    addCheck(checks, details, QStringLiteral("headingFoldRepaintsNextFrame"),
             headingFoldRenderClicked.value(QStringLiteral("markerFound")).toBool()
                 && headingFoldRenderExpandedAgain.value(QStringLiteral("markerFound")).toBool()
                 && headingFoldRenderedExpanded.value(
                        QStringLiteral("nonSurfacePixelCount")).toInt() > 30
                 && headingFoldRenderedCollapsed.value(
                        QStringLiteral("nonSurfacePixelCount")).toInt() <= 2
                 && headingFoldRenderedRestored.value(
                        QStringLiteral("nonSurfacePixelCount")).toInt() > 30
                 && !headingFoldRenderedExpanded.value(
                        QStringLiteral("historyPanelOpen")).toBool()
                 && !headingFoldRenderedCollapsed.value(
                        QStringLiteral("historyPanelOpen")).toBool()
                 && headingFoldRenderedExpanded.value(
                        QStringLiteral("editorVisibleWidth")).toDouble()
                    == headingFoldRenderedCollapsed.value(
                        QStringLiteral("editorVisibleWidth")).toDouble(),
             QJsonObject{{QStringLiteral("expanded"), headingFoldRenderedExpanded},
                         {QStringLiteral("collapsed"), headingFoldRenderedCollapsed},
                         {QStringLiteral("restored"), headingFoldRenderedRestored}});

    // 轻量关闭动画（形状收缩）期间，短文本无滚动范围时不得闪现右侧滚动条：
    // 可见性此前依赖 60ms 防抖快照，动画期间视口高度逐帧收缩而快照停滞，
    // 修复后可见性实时跟随 contentHeight。轮询采样，直到捕获一个几何已收缩
    // 的动画中段样本再断言，避免固定延时在慢速环境下错过 120ms 动画窗口。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), QString()}});
    QThread::msleep(150);
    const QJsonObject scrollbarBeforeClose = request(QStringLiteral("status"));
    request(QStringLiteral("hide"));
    QJsonObject scrollbarClosing;
    bool closingMidAnimationObserved = false;
    for (int attempt = 0; attempt < 12 && !closingMidAnimationObserved; ++attempt) {
        QThread::msleep(10);
        scrollbarClosing = request(QStringLiteral("status"));
        closingMidAnimationObserved =
            scrollbarClosing.value(QStringLiteral("windowTransitionActive")).toBool()
            && scrollbarClosing.value(QStringLiteral("width")).toInt()
                   < scrollbarBeforeClose.value(QStringLiteral("width")).toInt()
            && scrollbarClosing.value(QStringLiteral("height")).toInt()
                   < scrollbarBeforeClose.value(QStringLiteral("height")).toInt();
    }
    QThread::msleep(200);
    request(QStringLiteral("show"));
    QThread::msleep(animationSettleMs);
    addCheck(checks, details, QStringLiteral("closingAnimationScrollbarStaysHidden"),
             closingMidAnimationObserved
                 && !scrollbarBeforeClose.value(QStringLiteral("verticalScrollBarVisible")).toBool()
                 && !scrollbarClosing.value(QStringLiteral("verticalScrollBarVisible")).toBool(),
             QJsonObject{{QStringLiteral("before"), scrollbarBeforeClose},
                         {QStringLiteral("closing"), scrollbarClosing}});

    // 动画开启的闭合态窗口缩放回归：缩放动画期间历史面板右边缘不得探入编辑区交界。
    const QJsonObject geometryBeforeResize = request(QStringLiteral("status"));
    request(QStringLiteral("testSetGeometry"),
            {{QStringLiteral("x"), geometryBeforeResize.value(QStringLiteral("x")).toInt()},
             {QStringLiteral("y"), geometryBeforeResize.value(QStringLiteral("y")).toInt()},
             {QStringLiteral("width"),
              geometryBeforeResize.value(QStringLiteral("width")).toInt() + 40},
             {QStringLiteral("height"),
              geometryBeforeResize.value(QStringLiteral("height")).toInt() + 20}});
    QThread::msleep(30);
    const QJsonObject resizedClosedAnimated = request(QStringLiteral("status"));
    request(QStringLiteral("testSetGeometry"),
            {{QStringLiteral("x"), geometryBeforeResize.value(QStringLiteral("x")).toInt()},
             {QStringLiteral("y"), geometryBeforeResize.value(QStringLiteral("y")).toInt()},
             {QStringLiteral("width"),
              geometryBeforeResize.value(QStringLiteral("width")).toInt()},
             {QStringLiteral("height"),
              geometryBeforeResize.value(QStringLiteral("height")).toInt()}});
    QThread::msleep(180);
    addCheck(checks, details, QStringLiteral("historyClosedResizeKeepsEdgeClipped"),
             !resizedClosedAnimated.value(QStringLiteral("historyPanelOpen")).toBool()
                 && resizedClosedAnimated.value(
                        QStringLiteral("historyPanelEdgeIntrusion")).toDouble() <= 0.001,
             resizedClosedAnimated);

    // 窗口缩小（无真实滚动范围的短文本）时，右侧滚动条不得因 60ms 防抖快照滞后
    // 而闪现：缩小后 30ms（仍处防抖窗口内）采样，可见性必须保持隐藏。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), QStringLiteral("short")}});
    QThread::msleep(150);
    const QJsonObject scrollbarBeforeResize = request(QStringLiteral("status"));
    request(QStringLiteral("testSetGeometry"),
            {{QStringLiteral("x"), geometryBeforeResize.value(QStringLiteral("x")).toInt()},
             {QStringLiteral("y"), geometryBeforeResize.value(QStringLiteral("y")).toInt()},
             {QStringLiteral("width"),
              geometryBeforeResize.value(QStringLiteral("width")).toInt()},
             {QStringLiteral("height"),
              qMax(geometryBeforeResize.value(QStringLiteral("height")).toInt() - 100,
                   geometryBeforeResize.value(
                       QStringLiteral("minimumHeight")).toInt() + 10)}});
    QThread::msleep(30);
    const QJsonObject scrollbarResized = request(QStringLiteral("status"));
    request(QStringLiteral("testSetGeometry"),
            {{QStringLiteral("x"), geometryBeforeResize.value(QStringLiteral("x")).toInt()},
             {QStringLiteral("y"), geometryBeforeResize.value(QStringLiteral("y")).toInt()},
             {QStringLiteral("width"),
              geometryBeforeResize.value(QStringLiteral("width")).toInt()},
             {QStringLiteral("height"),
              geometryBeforeResize.value(QStringLiteral("height")).toInt()}});
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), QString()}});
    QThread::msleep(180);
    addCheck(checks, details, QStringLiteral("resizeScrollbarStaysHidden"),
             !scrollbarBeforeResize.value(QStringLiteral("verticalScrollBarVisible")).toBool()
                 && scrollbarResized.value(QStringLiteral("scrollViewportHeight")).toDouble()
                        < scrollbarBeforeResize.value(
                              QStringLiteral("scrollViewportHeight")).toDouble()
                 && !scrollbarResized.value(QStringLiteral("verticalScrollBarVisible")).toBool(),
             QJsonObject{{QStringLiteral("before"), scrollbarBeforeResize},
                         {QStringLiteral("resized"), scrollbarResized}});

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

    // 剪贴板模式：新内容默认光标落在文档末尾；内容未变化时保留原光标位置。
    // 编辑器文本为空时才 hide，避免 commitAndHide 向真实系统剪贴板写回测试内容。
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), QStringLiteral("alpha\nbeta")}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 2}, {QStringLiteral("end"), 2}});
    request(QStringLiteral("testSetClipboard"),
            {{QStringLiteral("text"), QStringLiteral("alpha\nbeta")}});
    const QJsonObject unchangedClipboard = request(QStringLiteral("show"));
    addCheck(checks, details, QStringLiteral("clipboardUnchangedKeepsCursor"),
             unchangedClipboard.value(QStringLiteral("cursorPosition")).toInt() == 2,
             unchangedClipboard);

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), QString()}});
    request(QStringLiteral("hide"));
    QThread::msleep(240);
    request(QStringLiteral("testSetClipboard"),
            {{QStringLiteral("text"), QStringLiteral("alpha\nbeta")}});
    const QJsonObject firstClipboard = request(QStringLiteral("show"));
    addCheck(checks, details, QStringLiteral("clipboardNewContentCursorAtEnd"),
             firstClipboard.value(QStringLiteral("cursorPosition")).toInt() == 10,
             firstClipboard);

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), QString()}});
    request(QStringLiteral("hide"));
    QThread::msleep(240);
    request(QStringLiteral("testSetClipboard"),
            {{QStringLiteral("text"), QStringLiteral("longer")}});
    const QJsonObject secondClipboard = request(QStringLiteral("show"));
    addCheck(checks, details, QStringLiteral("clipboardReplacedContentCursorAtEnd"),
             secondClipboard.value(QStringLiteral("cursorPosition")).toInt() == 6,
             secondClipboard);

    // --- 翻页与自动滚动轻量动画（窗口可见、动画默认开启时验证中间态与落定） ---
    QString scrollAnimText;
    scrollAnimText.reserve(4096);
    for (int i = 0; i < 80; ++i) {
        scrollAnimText += QStringLiteral("line-%1 abcdefghij klmnopqrstuvwxyz\n")
                              .arg(i, 2, 10, QLatin1Char('0'));
    }
    request(QStringLiteral("show"));
    QThread::msleep(120);
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollAnimText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}});
    request(QStringLiteral("testSetScrollY"), {{QStringLiteral("contentY"), 0}});
    QThread::msleep(60);
    const QJsonObject animPageBase = request(QStringLiteral("status"));
    const double animPageTarget = qMin(
        animPageBase.value(QStringLiteral("scrollViewportHeight")).toDouble(),
        animPageBase.value(QStringLiteral("scrollContentHeight")).toDouble()
            - animPageBase.value(QStringLiteral("scrollViewportHeight")).toDouble());
    keyPress({}, QStringLiteral("PageDown"));
    QThread::msleep(40);
    const QJsonObject animPageMid = request(QStringLiteral("status"));
    QThread::msleep(300);
    const QJsonObject animPageSettled = request(QStringLiteral("status"));
    addCheck(checks, details, QStringLiteral("pageDownScrollAnimatesAndSettles"),
             animPageMid.value(QStringLiteral("animationsEnabled")).toBool()
                 && animPageMid.value(QStringLiteral("scrollContentY")).toDouble() > 0.5
                 && animPageMid.value(QStringLiteral("scrollContentY")).toDouble()
                    < animPageTarget - 0.5
                 && qAbs(animPageSettled.value(QStringLiteral("scrollContentY")).toDouble()
                         - animPageTarget) < 1.5,
             QJsonObject{{QStringLiteral("targetY"), animPageTarget},
                         {QStringLiteral("mid"), animPageMid},
                         {QStringLiteral("settled"), animPageSettled}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollAnimText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollAnimText.size()},
             {QStringLiteral("end"), scrollAnimText.size()}});
    request(QStringLiteral("testSetScrollY"), {{QStringLiteral("contentY"), 0}});
    QThread::msleep(60);
    keyPress(QStringLiteral("x"));
    QThread::msleep(60);
    const QJsonObject animAutoMid = request(QStringLiteral("status"));
    const double animAutoMaxY =
        animAutoMid.value(QStringLiteral("scrollContentHeight")).toDouble()
        - animAutoMid.value(QStringLiteral("scrollViewportHeight")).toDouble();
    QThread::msleep(300);
    const QJsonObject animAutoSettled = request(QStringLiteral("status"));
    addCheck(checks, details, QStringLiteral("autoScrollAnimatesAndSettles"),
             animAutoMid.value(QStringLiteral("animationsEnabled")).toBool()
                 && animAutoMid.value(QStringLiteral("scrollContentY")).toDouble() > 0.5
                 && animAutoMid.value(QStringLiteral("scrollContentY")).toDouble()
                    < animAutoMaxY - 0.5
                 && qAbs(animAutoSettled.value(QStringLiteral("scrollContentY")).toDouble()
                         - animAutoMaxY) < 1.5,
             QJsonObject{{QStringLiteral("maxY"), animAutoMaxY},
                         {QStringLiteral("mid"), animAutoMid},
                         {QStringLiteral("settled"), animAutoSettled}});

    // 标题跳转滚动共用同一轻量动画入口：延迟 40ms 后启动约 160ms 平滑滚动，
    // 落定后标题首行锚定到视口上 1/3。
    QString headingAnimText;
    headingAnimText.reserve(8192);
    headingAnimText += QStringLiteral("# A\n");
    for (int i = 0; i < 60; ++i) {
        headingAnimText += QStringLiteral("line-%1 abcdefghij klmnopqrstuvwxyz\n")
                               .arg(i, 2, 10, QLatin1Char('0'));
    }
    headingAnimText += QStringLiteral("# B\n");
    for (int i = 0; i < 60; ++i) {
        headingAnimText += QStringLiteral("line-%1 abcdefghij klmnopqrstuvwxyz\n")
                               .arg(i, 2, 10, QLatin1Char('0'));
    }
    headingAnimText += QStringLiteral("# C\n");
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), headingAnimText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}});
    request(QStringLiteral("testSetScrollY"), {{QStringLiteral("contentY"), 0}});
    QThread::msleep(60);
    execute(QStringLiteral("nextHeading"));
    // 动画在 40ms 布局落定延迟后启动；留出一次繁忙渲染帧的余量后，
    // 采样仍应处于约 160ms 滚动动画的中间态（非瞬移）。
    QThread::msleep(100);
    const QJsonObject headingAnimMid = request(QStringLiteral("status"));
    const double headingAnimMaxY =
        headingAnimMid.value(QStringLiteral("scrollContentHeight")).toDouble()
        - headingAnimMid.value(QStringLiteral("scrollViewportHeight")).toDouble();
    QThread::msleep(300);
    const QJsonObject headingAnimSettled = request(QStringLiteral("status"));
    const double headingAnimAnchorY =
        headingAnimSettled.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + headingAnimSettled.value(QStringLiteral("cursorRectY")).toDouble()
        - headingAnimSettled.value(QStringLiteral("scrollViewportHeight")).toDouble() / 3.0;
    addCheck(checks, details, QStringLiteral("headingNavigationScrollAnimatesAndSettles"),
             headingAnimMid.value(QStringLiteral("animationsEnabled")).toBool()
                 && headingAnimMid.value(QStringLiteral("scrollContentY")).toDouble() > 0.5
                 && headingAnimMid.value(QStringLiteral("scrollContentY")).toDouble()
                    < headingAnimMaxY - 0.5
                 && headingAnimSettled.value(QStringLiteral("scrollContentY")).toDouble()
                    > headingAnimMid.value(QStringLiteral("scrollContentY")).toDouble() + 5.0
                 && std::abs(headingAnimSettled.value(QStringLiteral("scrollContentY")).toDouble()
                             - headingAnimAnchorY) < 2.0,
             QJsonObject{{QStringLiteral("maxY"), headingAnimMaxY},
                         {QStringLiteral("anchorY"), headingAnimAnchorY},
                         {QStringLiteral("mid"), headingAnimMid},
                         {QStringLiteral("settled"), headingAnimSettled}});

    const QJsonObject animOffApplied = request(
        QStringLiteral("testApplyAppearance"),
        {{QStringLiteral("theme"), QStringLiteral("light")},
         {QStringLiteral("fontFamily"), QStringLiteral("Microsoft YaHei UI")},
         {QStringLiteral("fallbackFontFamily"), QStringLiteral("NSimSun")},
         {QStringLiteral("fontPointSize"), 15},
         {QStringLiteral("fontWeight"), 400},
         {QStringLiteral("animationsEnabled"), false}});
    QThread::msleep(60);
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollAnimText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}});
    request(QStringLiteral("testSetScrollY"), {{QStringLiteral("contentY"), 0}});
    QThread::msleep(60);
    const QJsonObject animOffBase = request(QStringLiteral("status"));
    const double animOffTarget = qMin(
        animOffBase.value(QStringLiteral("scrollViewportHeight")).toDouble(),
        animOffBase.value(QStringLiteral("scrollContentHeight")).toDouble()
            - animOffBase.value(QStringLiteral("scrollViewportHeight")).toDouble());
    keyPress({}, QStringLiteral("PageDown"));
    QThread::msleep(20);
    const QJsonObject animOffStatus = request(QStringLiteral("status"));
    addCheck(checks, details, QStringLiteral("scrollAnimationDisabledIsInstant"),
             animOffApplied.value(QStringLiteral("applied")).toBool()
                 && !animOffStatus.value(QStringLiteral("animationsEnabled")).toBool()
                 && qAbs(animOffStatus.value(QStringLiteral("scrollContentY")).toDouble()
                         - animOffTarget) < 1.5,
             QJsonObject{{QStringLiteral("targetY"), animOffTarget},
                         {QStringLiteral("applied"), animOffApplied},
                         {QStringLiteral("status"), animOffStatus}});

    const QString animationOffHeadingText = QStringLiteral("plain\n# target\nbody");
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), animationOffHeadingText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}});
    const QJsonObject animationOffHighlightStarted = execute(QStringLiteral("nextHeading"));
    QThread::msleep(420);
    const QJsonObject animationOffHighlightFinished = request(QStringLiteral("status"));
    addCheck(checks, details, QStringLiteral("headingNavigationHighlightFadeDisabledIsImmediate"),
             animationOffHighlightStarted.value(
                    QStringLiteral("headingNavigationHighlightFadeDurationMs")).toInt() == 0
                 && animationOffHighlightStarted.value(
                        QStringLiteral("headingNavigationHighlightVisible")).toBool()
                 && !animationOffHighlightFinished.value(
                        QStringLiteral("headingNavigationHighlightVisible")).toBool(),
             QJsonObject{{QStringLiteral("started"), animationOffHighlightStarted},
                         {QStringLiteral("finished"), animationOffHighlightFinished}});

    const QJsonObject opened = execute(QStringLiteral("settings"));
    addCheck(checks, details, QStringLiteral("lazySettingsPage"),
             !initial.value(QStringLiteral("settingsPageLoaded")).toBool()
                 && opened.value(QStringLiteral("executed")).toBool()
                 && opened.value(QStringLiteral("settingsPageLoaded")).toBool()
                 && opened.value(QStringLiteral("settingsPageVisible")).toBool(),
             opened);
    addCheck(checks, details, QStringLiteral("fileDropDisabledBySettings"),
             !opened.value(QStringLiteral("fileDropEnabled")).toBool(), opened);
    const QJsonObject closed = request(QStringLiteral("testCloseOverlays"));
    addCheck(checks, details, QStringLiteral("settingsPageCloses"),
             closed.value(QStringLiteral("settingsClosed")).toBool()
                 && !closed.value(QStringLiteral("settingsPageLoaded")).toBool(),
             closed);
    const QJsonObject findOpened = execute(QStringLiteral("find"));
    addCheck(checks, details, QStringLiteral("fileDropDisabledByFindPanel"),
             findOpened.value(QStringLiteral("findPanelVisible")).toBool()
                 && !findOpened.value(QStringLiteral("fileDropEnabled")).toBool(),
             findOpened);
    request(QStringLiteral("testCloseOverlays"));
    const QJsonObject paletteOpened = execute(QStringLiteral("commandPalette"));
    addCheck(checks, details, QStringLiteral("fileDropDisabledByCommandPalette"),
             paletteOpened.value(QStringLiteral("commandPaletteLoaded")).toBool()
                 && !paletteOpened.value(QStringLiteral("fileDropEnabled")).toBool(),
             paletteOpened);
    request(QStringLiteral("testCloseOverlays"));

    const QJsonObject light = request(
        QStringLiteral("testApplyAppearance"),
        {{QStringLiteral("theme"), QStringLiteral("light")},
         {QStringLiteral("fontFamily"), QStringLiteral("Consolas")},
         {QStringLiteral("fallbackFontFamily"), QStringLiteral("NSimSun")},
         {QStringLiteral("fontPointSize"), 15},
         {QStringLiteral("fontWeight"), 600},
         {QStringLiteral("animationsEnabled"), false}});
    addCheck(checks, details, QStringLiteral("appearanceApplies"),
             light.value(QStringLiteral("applied")).toBool()
                 && light.value(QStringLiteral("theme")).toString() == QStringLiteral("light")
                 && light.value(QStringLiteral("editorFontFamily")).toString()
                    == QStringLiteral("Consolas")
                 && light.value(QStringLiteral("editorFallbackFontFamily")).toString()
                    == QStringLiteral("NSimSun")
                 && light.value(QStringLiteral("editorFontPointSize")).toInt() == 15
                 && light.value(QStringLiteral("editorFontWeight")).toInt() == 600
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
         {QStringLiteral("fallbackFontFamily"), QStringLiteral("NSimSun")},
         {QStringLiteral("fontPointSize"), 15},
         {QStringLiteral("fontWeight"), 400},
         {QStringLiteral("animationsEnabled"), true}});
    const QJsonObject invalidFont = request(
        QStringLiteral("testApplyAppearance"),
        {{QStringLiteral("theme"), QStringLiteral("dark")},
         {QStringLiteral("fontFamily"), QStringLiteral("ScratchEditor Missing Font")},
         {QStringLiteral("fallbackFontFamily"), QStringLiteral("NSimSun")},
         {QStringLiteral("fontPointSize"), 15},
         {QStringLiteral("fontWeight"), 400},
         {QStringLiteral("animationsEnabled"), true}});
    const QJsonObject invalidFallbackFont = request(
        QStringLiteral("testApplyAppearance"),
        {{QStringLiteral("theme"), QStringLiteral("dark")},
         {QStringLiteral("fontFamily"), QStringLiteral("Consolas")},
         {QStringLiteral("fallbackFontFamily"),
          QStringLiteral("ScratchEditor Missing Font")},
         {QStringLiteral("fontPointSize"), 15},
         {QStringLiteral("fontWeight"), 400},
         {QStringLiteral("animationsEnabled"), true}});
    const QJsonObject invalidSize = request(
        QStringLiteral("testApplyAppearance"),
        {{QStringLiteral("theme"), QStringLiteral("dark")},
         {QStringLiteral("fontFamily"), QStringLiteral("Microsoft YaHei UI")},
         {QStringLiteral("fallbackFontFamily"), QStringLiteral("NSimSun")},
         {QStringLiteral("fontPointSize"), 25},
         {QStringLiteral("fontWeight"), 400},
         {QStringLiteral("animationsEnabled"), true}});
    const QJsonObject invalidWeight = request(
        QStringLiteral("testApplyAppearance"),
        {{QStringLiteral("theme"), QStringLiteral("dark")},
         {QStringLiteral("fontFamily"), QStringLiteral("Consolas")},
         {QStringLiteral("fallbackFontFamily"), QStringLiteral("NSimSun")},
         {QStringLiteral("fontPointSize"), 15},
         {QStringLiteral("fontWeight"), 450},
         {QStringLiteral("animationsEnabled"), true}});
    addCheck(checks, details, QStringLiteral("appearanceValidation"),
             !invalidTheme.value(QStringLiteral("applied")).toBool()
                 && !invalidFont.value(QStringLiteral("applied")).toBool()
                 && !invalidFallbackFont.value(QStringLiteral("applied")).toBool()
                 && !invalidSize.value(QStringLiteral("applied")).toBool()
                 && !invalidWeight.value(QStringLiteral("applied")).toBool()
                 && !invalidWeight.value(QStringLiteral("settingsError")).toString().isEmpty()
                 && invalidWeight.value(QStringLiteral("editorFontWeight")).toInt() == 600
                 && invalidWeight.value(QStringLiteral("theme")).toString() == QStringLiteral("light"),
             invalidWeight);

    const QJsonObject panelApplied = request(
        QStringLiteral("testApplyStatusPanelSettings"),
        {{QStringLiteral("fontSize"), 12},
         {QStringLiteral("showDelayMs"), 500},
         {QStringLiteral("hideDelayMs"), 400},
         {QStringLiteral("maxWidth"), 420}});
    addCheck(checks, details, QStringLiteral("statusPanelApplies"),
             panelApplied.value(QStringLiteral("applied")).toBool()
                 && panelApplied.value(QStringLiteral("statusPanelFontSize")).toInt() == 12
                 && panelApplied.value(QStringLiteral("statusPanelShowDelayMs")).toInt() == 500
                 && panelApplied.value(QStringLiteral("statusPanelHideDelayMs")).toInt() == 400
                 && panelApplied.value(QStringLiteral("statusPanelMaxWidth")).toInt() == 420,
             panelApplied);

    const QJsonObject panelInvalid = request(
        QStringLiteral("testApplyStatusPanelSettings"),
        {{QStringLiteral("fontSize"), 8},
         {QStringLiteral("showDelayMs"), 2500},
         {QStringLiteral("hideDelayMs"), 100},
         {QStringLiteral("maxWidth"), 100}});
    addCheck(checks, details, QStringLiteral("statusPanelValidation"),
             !panelInvalid.value(QStringLiteral("applied")).toBool()
                 && !panelInvalid.value(QStringLiteral("settingsError")).toString().isEmpty()
                 && panelInvalid.value(QStringLiteral("statusPanelFontSize")).toInt() == 12
                 && panelInvalid.value(QStringLiteral("statusPanelShowDelayMs")).toInt() == 500
                 && panelInvalid.value(QStringLiteral("statusPanelHideDelayMs")).toInt() == 400
                 && panelInvalid.value(QStringLiteral("statusPanelMaxWidth")).toInt() == 420,
             panelInvalid);

    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), QStringLiteral("你好 World")}});
    const QJsonObject summaryPlain = request(QStringLiteral("status"));
    const QJsonObject summarySelected = request(
        QStringLiteral("testSetSelection"),
        {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 2}});
    addCheck(checks, details, QStringLiteral("statusPanelSummaryPlain"),
             summaryPlain.value(QStringLiteral("statusPanelSummary")).toString()
                 == QStringLiteral("共 8 字 · 2 汉字"),
             summaryPlain);
    addCheck(checks, details, QStringLiteral("statusPanelSummarySelected"),
             summarySelected.value(QStringLiteral("statusPanelSummary")).toString()
                 == QStringLiteral("2 / 8 字 · 2 / 2 汉字"),
             summarySelected);

    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}});
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), QStringLiteral("こんにちは、한글你好")}});
    const QJsonObject summaryKana = request(QStringLiteral("status"));
    addCheck(checks, details, QStringLiteral("statusPanelSummaryHanExcludesKanaHangul"),
             summaryKana.value(QStringLiteral("statusPanelSummary")).toString()
                 == QStringLiteral("共 10 字 · 2 汉字"),
             summaryKana);

    const QJsonObject shortcut = request(
        QStringLiteral("testSetShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("toggleBold")},
         {QStringLiteral("sequence"), QStringLiteral("Ctrl+Alt+B")}});
    const QJsonObject paletteShortcut = request(
        QStringLiteral("testSetShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("commandPalette")},
         {QStringLiteral("sequence"), QStringLiteral("Ctrl+Alt+P")}});
    const QJsonObject hintsAfterChange = request(QStringLiteral("status"));
    const QJsonArray changedHints =
        hintsAfterChange.value(QStringLiteral("statusPanelHints")).toArray();
    bool changedHintsMatch = changedHints.size() == expectedHints.size();
    int paletteHintCount = 0;
    for (int index = 0; index < changedHints.size() && changedHintsMatch; ++index) {
        const QString hint = changedHints.at(index).toString();
        changedHintsMatch = !hint.contains(QStringLiteral("Ctrl+Shift+P"));
        if (hint == QStringLiteral("Ctrl+Alt+P · 打开命令面板")) {
            ++paletteHintCount;
        }
    }
    changedHintsMatch = changedHintsMatch && paletteHintCount == 1;
    addCheck(checks, details, QStringLiteral("statusPanelHintsFollowShortcutChange"),
             paletteShortcut.value(QStringLiteral("configured")).toBool()
                 && changedHintsMatch,
             hintsAfterChange);
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
                 && hasKey(keys, QStringLiteral("appearance/fontFamily"))
                 && hasKey(keys, QStringLiteral("appearance/fallbackFontFamily"))
                 && hasKey(keys, QStringLiteral("appearance/fontPointSize"))
                 && hasKey(keys, QStringLiteral("appearance/fontWeight"))
                 && hasKey(keys, QStringLiteral("appearance/animationsEnabled"))
                 && hasKey(keys, QStringLiteral("statusPanel/fontSize"))
                 && hasKey(keys, QStringLiteral("statusPanel/showDelayMs"))
                 && hasKey(keys, QStringLiteral("statusPanel/hideDelayMs"))
                 && hasKey(keys, QStringLiteral("statusPanel/maxWidth"))
                 && hasKey(keys, QStringLiteral("shortcuts/toggleBold"))
                 && fileText.contains(QStringLiteral("[appearance]"))
                 && !fileText.contains(QStringLiteral("[editor]"))
                 && !fileText.contains(QStringLiteral("[ui]"))
                 && fileText.contains(QStringLiteral("[statusPanel]"))
                 && fileText.contains(QStringLiteral("[shortcuts]")),
             config);

    const QJsonObject panelReset = request(QStringLiteral("testResetStatusPanelSettings"));
    addCheck(checks, details, QStringLiteral("statusPanelReset"),
             panelReset.value(QStringLiteral("statusPanelFontSize")).toInt() == 10
                 && panelReset.value(QStringLiteral("statusPanelShowDelayMs")).toInt() == 300
                 && panelReset.value(QStringLiteral("statusPanelHideDelayMs")).toInt() == 250
                 && panelReset.value(QStringLiteral("statusPanelMaxWidth")).toInt() == 360,
             panelReset);

    const QJsonObject beforeHistoryLayout = request(QStringLiteral("status"));
    request(QStringLiteral("testDiscardClose"));
    QThread::msleep(180);
    request(QStringLiteral("testResetClipboardHistory"));
    request(QStringLiteral("testSetGeometry"),
            {{QStringLiteral("x"), 100}, {QStringLiteral("y"), 100},
             {QStringLiteral("width"), 920}, {QStringLiteral("height"), 640}});
    request(QStringLiteral("show"));
    QThread::msleep(180);
    const QString historyHeadingText = QStringLiteral("# ")
        + QString(200, QLatin1Char('W'))
        + QStringLiteral("\nroot body\n## Child\nchild body");
    const int historyChildHeadingPosition = historyHeadingText.indexOf(QStringLiteral("## Child"));
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), historyHeadingText}});
    QThread::msleep(40);
    const QJsonObject historyHeadingClosedMarker = request(
        QStringLiteral("testHeadingFoldMarkerState"),
        {{QStringLiteral("position"), historyChildHeadingPosition}});
    const QJsonObject historyInitial = request(QStringLiteral("status"));
    addCheck(checks, details, QStringLiteral("historyPanelDefaults"),
             historyInitial.value(QStringLiteral("historyAvailable")).toBool()
                 && historyInitial.value(QStringLiteral("historyPanelLoaded")).toBool()
                 && !historyInitial.value(QStringLiteral("historyPanelOpen")).toBool()
                 && historyInitial.value(QStringLiteral("historyTriggerWidth")).toInt() == 12
                 && historyInitial.value(QStringLiteral("historyRevealZoneX")).toInt() == 0
                 && historyInitial.value(QStringLiteral("historyRevealZoneWidth")).toInt() == 30
                 && historyInitial.value(QStringLiteral("historyCardHeight")).toInt() == 58
                 && historyInitial.value(QStringLiteral("historyPanelClipped")).toBool()
                 && !historyInitial.value(QStringLiteral("historyRevealBlocksPointer")).toBool()
                 && historyInitial.value(QStringLiteral("historyHoverOpenDelayMs")).toInt() == 100
                 && historyInitial.value(QStringLiteral("historyHoverCloseDelayMs")).toInt() == 250,
             historyInitial);
    addCheck(checks, details, QStringLiteral("historyPanelCornerShapeClosed"),
             hasHistoryPanelCornerShape(historyInitial),
             historyPanelCornerDetails(historyInitial));
    request(QStringLiteral("testEmitClipboardChange"),
            {{QStringLiteral("kind"), QStringLiteral("text")},
             {QStringLiteral("text"), QStringLiteral("alpha\nsecond line")},
             {QStringLiteral("capturedAtMs"), 1786200000000.0}});
    request(QStringLiteral("testEmitClipboardChange"),
            {{QStringLiteral("kind"), QStringLiteral("text")},
             {QStringLiteral("text"), QStringLiteral("Beta 😀")},
             {QStringLiteral("capturedAtMs"), 1786200001000.0}});
    const QJsonObject historyCommand = execute(QStringLiteral("clipboardHistory"));
    QThread::msleep(150);
    const QJsonObject wideHistory = request(QStringLiteral("status"));
    const QJsonObject historyHeadingOpenMarker = request(
        QStringLiteral("testHeadingFoldMarkerState"),
        {{QStringLiteral("position"), historyChildHeadingPosition}});
    addCheck(checks, details, QStringLiteral("historyCommandOpensFocusedPushPanel"),
             historyCommand.value(QStringLiteral("executed")).toBool()
                 && wideHistory.value(QStringLiteral("historyPanelOpen")).toBool()
                 && wideHistory.value(QStringLiteral("historyQueryFocused")).toBool()
                 && !wideHistory.value(QStringLiteral("historyPanelOverlay")).toBool()
                 && wideHistory.value(QStringLiteral("width")).toInt() == 920
                 && qAbs(wideHistory.value(QStringLiteral("historyPanelWidth")).toDouble()
                         - 920.0 / 3.0) < 1.0
                 && wideHistory.value(QStringLiteral("editorVisibleWidth")).toDouble() >= 320.0,
             wideHistory);
    addCheck(checks, details, QStringLiteral("historyPanelCornerShapeOpen"),
             hasHistoryPanelCornerShape(wideHistory),
             historyPanelCornerDetails(wideHistory));
    const QJsonObject historyHeadingClosedGeometry = historyHeadingClosedMarker.value(
        QStringLiteral("marker")).toObject();
    const QJsonObject historyHeadingOpenGeometry = historyHeadingOpenMarker.value(
        QStringLiteral("marker")).toObject();
    addCheck(checks, details, QStringLiteral("headingFoldMarkersFollowHistoryPanelOpen"),
             historyHeadingClosedMarker.value(QStringLiteral("markerFound")).toBool()
                 && historyHeadingOpenMarker.value(QStringLiteral("markerFound")).toBool()
                 && qAbs(historyHeadingClosedGeometry.value(
                            QStringLiteral("y")).toDouble()
                         - historyHeadingClosedGeometry.value(
                            QStringLiteral("expectedY")).toDouble()) <= 1.0
                 && qAbs(historyHeadingOpenGeometry.value(
                            QStringLiteral("y")).toDouble()
                         - historyHeadingOpenGeometry.value(
                            QStringLiteral("expectedY")).toDouble()) <= 1.0
                 && historyHeadingOpenGeometry.value(QStringLiteral("y")).toDouble()
                     > historyHeadingClosedGeometry.value(QStringLiteral("y")).toDouble()
                 && historyHeadingOpenGeometry.value(QStringLiteral("x")).toDouble()
                     > historyHeadingClosedGeometry.value(QStringLiteral("x")).toDouble(),
             QJsonObject{{QStringLiteral("closed"), historyHeadingClosedMarker},
                         {QStringLiteral("open"), historyHeadingOpenMarker}});
    const double titleXDiff = qAbs(
        wideHistory.value(QStringLiteral("historyTitleX")).toDouble()
        - wideHistory.value(QStringLiteral("headerTitleX")).toDouble());
    const double titleYDiff = qAbs(
        wideHistory.value(QStringLiteral("historyTitleY")).toDouble()
        - wideHistory.value(QStringLiteral("headerTitleY")).toDouble());
    addCheck(checks, details, QStringLiteral("historyTitleMatchesEditorTitle"),
             titleXDiff <= 0.5 && titleYDiff <= 0.5,
             QJsonObject{{QStringLiteral("actual"),
                          QJsonObject{{QStringLiteral("x"),
                                       wideHistory.value(QStringLiteral("historyTitleX"))},
                                      {QStringLiteral("y"),
                                       wideHistory.value(QStringLiteral("historyTitleY"))}}},
                         {QStringLiteral("expected"),
                          QJsonObject{{QStringLiteral("x"),
                                       wideHistory.value(QStringLiteral("headerTitleX"))},
                                      {QStringLiteral("y"),
                                       wideHistory.value(QStringLiteral("headerTitleY"))}}},
                         {QStringLiteral("diff"),
                          QJsonObject{{QStringLiteral("x"), titleXDiff},
                                      {QStringLiteral("y"), titleYDiff}}}});
    const double searchTopDiff = qAbs(
        wideHistory.value(QStringLiteral("historySearchFrameY")).toDouble()
        - wideHistory.value(QStringLiteral("editorViewportY")).toDouble());
    addCheck(checks, details, QStringLiteral("historySearchMatchesEditorTop"),
             searchTopDiff <= 0.5,
             QJsonObject{{QStringLiteral("actual"),
                          wideHistory.value(QStringLiteral("historySearchFrameY"))},
                         {QStringLiteral("expected"),
                          wideHistory.value(QStringLiteral("editorViewportY"))},
                         {QStringLiteral("diff"), searchTopDiff}});
    const double listGap =
        wideHistory.value(QStringLiteral("historyListY")).toDouble()
        - wideHistory.value(QStringLiteral("historySearchFrameY")).toDouble()
        - wideHistory.value(QStringLiteral("historySearchFrameHeight")).toDouble();
    addCheck(checks, details, QStringLiteral("historyListKeepsSearchGap"),
             qAbs(listGap - 8.0) <= 0.5,
             QJsonObject{{QStringLiteral("actual"), listGap},
                         {QStringLiteral("expected"), 8}});
    const double footerGap =
        wideHistory.value(QStringLiteral("historyDeleteButtonY")).toDouble()
        - wideHistory.value(QStringLiteral("historyListY")).toDouble()
        - wideHistory.value(QStringLiteral("historyListHeight")).toDouble();
    addCheck(checks, details, QStringLiteral("historyListKeepsFooterGap"),
             qAbs(footerGap - 10.0) <= 0.5,
             QJsonObject{{QStringLiteral("actual"), footerGap},
                         {QStringLiteral("expected"), 10}});

    // 动画开启、历史面板打开（推挤模式）时缩放：编辑区必须即时跟随窗口边缘，
    // 不允许 x/width 的 Behavior 逐帧重启动画造成滞后追赶（修复前 30ms 处差值约 20px）。
    request(QStringLiteral("testSetGeometry"),
            {{QStringLiteral("x"), 100}, {QStringLiteral("y"), 100},
             {QStringLiteral("width"), 960}, {QStringLiteral("height"), 660}});
    QThread::msleep(30);
    const QJsonObject resizedOpenHistory = request(QStringLiteral("status"));
    const QJsonObject historyHeadingResizedMarker = request(
        QStringLiteral("testHeadingFoldMarkerState"),
        {{QStringLiteral("position"), historyChildHeadingPosition}});
    const double openWidthDiff = qAbs(resizedOpenHistory.value(
        QStringLiteral("editorViewportWidth")).toDouble()
        - resizedOpenHistory.value(QStringLiteral("editorVisibleWidth")).toDouble());
    addCheck(checks, details, QStringLiteral("editorResizeFollowsInstantlyOpen"),
             resizedOpenHistory.value(QStringLiteral("historyPanelOpen")).toBool()
                 && !resizedOpenHistory.value(QStringLiteral("historyPanelOverlay")).toBool()
                 && openWidthDiff <= 1.0,
             QJsonObject{{QStringLiteral("viewportWidth"),
                          resizedOpenHistory.value(
                              QStringLiteral("editorViewportWidth")).toDouble()},
                         {QStringLiteral("editorVisibleWidth"),
                          resizedOpenHistory.value(
                              QStringLiteral("editorVisibleWidth")).toDouble()},
                         {QStringLiteral("diff"), openWidthDiff}});
    addCheck(checks, details, QStringLiteral("historyPanelCornerShapeAfterResize"),
             hasHistoryPanelCornerShape(resizedOpenHistory),
             historyPanelCornerDetails(resizedOpenHistory));
    const QJsonObject historyHeadingResizedGeometry = historyHeadingResizedMarker.value(
        QStringLiteral("marker")).toObject();
    addCheck(checks, details, QStringLiteral("headingFoldMarkersFollowResizeWithHistoryOpen"),
             historyHeadingResizedMarker.value(QStringLiteral("markerFound")).toBool()
                 && qAbs(historyHeadingResizedGeometry.value(
                            QStringLiteral("x")).toDouble()
                         - historyHeadingResizedGeometry.value(
                            QStringLiteral("expectedX")).toDouble()) <= 1.0
                 && qAbs(historyHeadingResizedGeometry.value(
                            QStringLiteral("y")).toDouble()
                         - historyHeadingResizedGeometry.value(
                            QStringLiteral("expectedY")).toDouble()) <= 1.0,
             historyHeadingResizedMarker);
    request(QStringLiteral("testSetGeometry"),
            {{QStringLiteral("x"), 100}, {QStringLiteral("y"), 100},
             {QStringLiteral("width"), 920}, {QStringLiteral("height"), 640}});
    QThread::msleep(180);

    execute(QStringLiteral("clipboardHistory"));
    QThread::msleep(150);
    const QJsonObject toggledClosedHistory = request(QStringLiteral("status"));
    const QJsonObject historyHeadingReclosedMarker = request(
        QStringLiteral("testHeadingFoldMarkerState"),
        {{QStringLiteral("position"), historyChildHeadingPosition}});
    addCheck(checks, details, QStringLiteral("historyCommandTogglesClosed"),
             !toggledClosedHistory.value(QStringLiteral("historyPanelOpen")).toBool(),
             toggledClosedHistory);
    const QJsonObject historyHeadingReclosedGeometry = historyHeadingReclosedMarker.value(
        QStringLiteral("marker")).toObject();
    addCheck(checks, details, QStringLiteral("headingFoldMarkersFollowHistoryPanelClose"),
             historyHeadingReclosedMarker.value(QStringLiteral("markerFound")).toBool()
                 && qAbs(historyHeadingReclosedGeometry.value(
                            QStringLiteral("x")).toDouble()
                         - historyHeadingReclosedGeometry.value(
                            QStringLiteral("expectedX")).toDouble()) <= 1.0
                 && qAbs(historyHeadingReclosedGeometry.value(
                            QStringLiteral("y")).toDouble()
                         - historyHeadingReclosedGeometry.value(
                            QStringLiteral("expectedY")).toDouble()) <= 1.0,
             historyHeadingReclosedMarker);
    // 动画开启、历史面板闭合时缩放：宽度同样必须即时跟随（修复前 30ms 处差值约 30px）。
    request(QStringLiteral("testSetGeometry"),
            {{QStringLiteral("x"), 100}, {QStringLiteral("y"), 100},
             {QStringLiteral("width"), 960}, {QStringLiteral("height"), 660}});
    QThread::msleep(30);
    const QJsonObject resizedClosedHistory = request(QStringLiteral("status"));
    const double closedWidthDiff = qAbs(resizedClosedHistory.value(
        QStringLiteral("editorViewportWidth")).toDouble()
        - resizedClosedHistory.value(QStringLiteral("editorVisibleWidth")).toDouble());
    addCheck(checks, details, QStringLiteral("editorResizeFollowsInstantlyClosed"),
             !resizedClosedHistory.value(QStringLiteral("historyPanelOpen")).toBool()
                 && closedWidthDiff <= 1.0,
             QJsonObject{{QStringLiteral("viewportWidth"),
                          resizedClosedHistory.value(
                              QStringLiteral("editorViewportWidth")).toDouble()},
                         {QStringLiteral("editorVisibleWidth"),
                          resizedClosedHistory.value(
                              QStringLiteral("editorVisibleWidth")).toDouble()},
                         {QStringLiteral("diff"), closedWidthDiff}});
    request(QStringLiteral("testSetGeometry"),
            {{QStringLiteral("x"), 100}, {QStringLiteral("y"), 100},
             {QStringLiteral("width"), 920}, {QStringLiteral("height"), 640}});
    QThread::msleep(180);

    execute(QStringLiteral("clipboardHistory"));
    QThread::msleep(150);
    const QJsonObject toggledReopenedHistory = request(QStringLiteral("status"));
    addCheck(checks, details, QStringLiteral("historyCommandTogglesReopened"),
             toggledReopenedHistory.value(QStringLiteral("historyPanelOpen")).toBool()
                 && toggledReopenedHistory.value(QStringLiteral("historyQueryFocused")).toBool(),
             toggledReopenedHistory);
    addCheck(checks, details, QStringLiteral("fileDropDisabledByHistoryPanel"),
             toggledReopenedHistory.value(QStringLiteral("historyPanelOpen")).toBool()
                 && !toggledReopenedHistory.value(
                        QStringLiteral("fileDropEnabled")).toBool(),
             toggledReopenedHistory);

    const QJsonObject historyState = request(QStringLiteral("testClipboardHistoryState"));
    const QJsonArray historyItems = historyState.value(QStringLiteral("items")).toArray();
    const QString selectedHistoryId = historyItems.isEmpty()
        ? QString() : historyItems.first().toObject().value(QStringLiteral("id")).toString();
    const QString selectedHistoryText = historyItems.isEmpty()
        ? QString() : historyItems.first().toObject().value(QStringLiteral("text")).toString();
    QString selectedHistoryPreview = selectedHistoryText.left(240);
    selectedHistoryPreview.remove(QLatin1Char('\r'));
    const QString hoveredHistoryId = historyItems.size() > 1
        ? historyItems.at(1).toObject().value(QStringLiteral("id")).toString()
        : QString();
    const QString historyTextBeforePointerClick = request(QStringLiteral("testText"))
                                                        .value(QStringLiteral("text")).toString();
    const QJsonObject historyPointerClick = dragHistoryUi(
        hoveredHistoryId, 0, false);
    addCheck(checks, details, QStringLiteral("historyDragBelowThresholdRemainsClick"),
             !hoveredHistoryId.isEmpty()
                 && historyPointerClick.value(QStringLiteral("eventsAccepted")).toBool()
                 && historyPointerClick.value(QStringLiteral("text")).toString()
                    == historyTextBeforePointerClick
                 && historyPointerClick.value(QStringLiteral("historySelectedId")).toString()
                    == hoveredHistoryId
                 && historyPointerClick.value(QStringLiteral("historyPanelOpen")).toBool()
                 && !historyPointerClick.value(
                        QStringLiteral("historyCardDragActive")).toBool()
                 && !historyPointerClick.value(
                        QStringLiteral("historyDragPreviewVisible")).toBool()
                 && !historyPointerClick.value(
                        QStringLiteral("historyDragCursorOverridden")).toBool(),
             historyPointerClick);

    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), QStringLiteral("A😀B")}});
    const QJsonObject historyDragHeld = dragHistoryUi(
        selectedHistoryId, 3, true, false, false, selectedHistoryPreview);
    const QJsonObject historyDragCanceled = historyAction(
        QStringLiteral("historyEscape"));
    addCheck(checks, details, QStringLiteral("historyDragKeepsPanelStableUntilFinish"),
             historyDragHeld.value(QStringLiteral("eventsAccepted")).toBool()
                 && historyDragHeld.value(QStringLiteral("historyCardDragActive")).toBool()
                 && historyDragHeld.value(QStringLiteral("historyCardDropAllowed")).toBool()
                 && historyDragHeld.value(
                        QStringLiteral("historyDragPreviewVisible")).toBool()
                 && historyDragHeld.value(QStringLiteral("historyDragPreviewText")).toString()
                    == selectedHistoryPreview
                 && historyDragHeld.value(
                        QStringLiteral("historyDragCursorOverridden")).toBool()
                 && historyDragHeld.value(QStringLiteral("historyDragCursorShape")).toInt()
                    == static_cast<int>(Qt::DragCopyCursor)
                 && historyDragHeld.value(QStringLiteral("historyDragPreviewX")).toDouble() >= 0
                 && historyDragHeld.value(QStringLiteral("historyDragPreviewY")).toDouble() >= 0
                 && historyDragHeld.value(QStringLiteral("historyDragPreviewX")).toDouble()
                        + historyDragHeld.value(
                            QStringLiteral("historyDragPreviewWidth")).toDouble()
                    <= historyDragHeld.value(QStringLiteral("width")).toDouble()
                 && historyDragHeld.value(QStringLiteral("historyDragPreviewY")).toDouble()
                        + historyDragHeld.value(
                            QStringLiteral("historyDragPreviewHeight")).toDouble()
                    <= historyDragHeld.value(QStringLiteral("height")).toDouble()
                 && historyDragHeld.value(
                        QStringLiteral("historyDragPreviewWidth")).toDouble() > 0
                 && historyDragHeld.value(
                        QStringLiteral("historyDragPreviewHeight")).toDouble() > 0
                 && qAbs(historyDragHeld.value(
                             QStringLiteral("historyDragPreviewOpacity")).toDouble()
                         - 0.72) <= 0.001
                 && historyDragHeld.value(QStringLiteral("historyPanelOpen")).toBool()
                 && historyDragHeld.value(QStringLiteral("editorVisibleWidth"))
                    == toggledReopenedHistory.value(QStringLiteral("editorVisibleWidth"))
                 && historyDragCanceled.value(QStringLiteral("invoked")).toBool()
                 && !historyDragCanceled.value(
                        QStringLiteral("historyCardDragActive")).toBool()
                 && !historyDragCanceled.value(
                        QStringLiteral("historyDragPreviewVisible")).toBool()
                 && !historyDragCanceled.value(
                        QStringLiteral("historyDragCursorOverridden")).toBool()
                 && historyDragCanceled.value(QStringLiteral("historyPanelOpen")).toBool(),
             QJsonObject{{QStringLiteral("held"), historyDragHeld},
                         {QStringLiteral("canceled"), historyDragCanceled}});

    const QJsonObject historyPointerDrop = dragHistoryUi(
        selectedHistoryId, 3, true, false, true, selectedHistoryPreview);
    const QJsonObject historyPointerUndo = request(QStringLiteral("testUndo"));
    addCheck(checks, details, QStringLiteral("historyDragClosesPanelAndFocusesEditor"),
             !selectedHistoryId.isEmpty()
                 && historyPointerDrop.value(QStringLiteral("eventsAccepted")).toBool()
                 && historyPointerDrop.value(QStringLiteral("text")).toString()
                    == QStringLiteral("A😀") + selectedHistoryText + QStringLiteral("B")
                 && historyPointerDrop.value(QStringLiteral("selectionStart")).toInt() == 3
                 && historyPointerDrop.value(QStringLiteral("selectionEnd")).toInt()
                    == 3 + selectedHistoryText.size()
                 && !historyPointerDrop.value(QStringLiteral("historyPanelOpen")).toBool()
                 && historyPointerDrop.value(QStringLiteral("editorHasFocus")).toBool()
                 && !historyPointerDrop.value(
                        QStringLiteral("historyCardDragActive")).toBool()
                 && !historyPointerDrop.value(
                        QStringLiteral("historyCardDropAllowed")).toBool()
                 && !historyPointerDrop.value(
                        QStringLiteral("historyDragPreviewVisible")).toBool()
                 && !historyPointerDrop.value(
                        QStringLiteral("historyDragCursorOverridden")).toBool()
                 && historyPointerUndo.value(QStringLiteral("text")).toString()
                    == QStringLiteral("A😀B"),
             QJsonObject{{QStringLiteral("dropped"), historyPointerDrop},
                         {QStringLiteral("undone"), historyPointerUndo}});

    request(QStringLiteral("testDiscardClose"));
    QThread::msleep(180);
    request(QStringLiteral("show"));
    QThread::msleep(180);
    execute(QStringLiteral("clipboardHistory"));
    QThread::msleep(150);
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), QStringLiteral("outside-drop-target")}});
    const QJsonObject historyOutsideHeld = dragHistoryUi(
        selectedHistoryId, 0, true, true, false, selectedHistoryPreview);
    const QJsonObject historyOutsideCanceled = historyAction(
        QStringLiteral("historyEscape"));
    addCheck(checks, details, QStringLiteral("historyDragOutsideShowsForbiddenFeedback"),
             historyOutsideHeld.value(QStringLiteral("eventsAccepted")).toBool()
                 && historyOutsideHeld.value(
                        QStringLiteral("historyCardDragActive")).toBool()
                 && !historyOutsideHeld.value(
                        QStringLiteral("historyCardDropAllowed")).toBool()
                 && historyOutsideHeld.value(
                        QStringLiteral("historyDragPreviewVisible")).toBool()
                 && historyOutsideHeld.value(
                        QStringLiteral("historyDragCursorOverridden")).toBool()
                 && historyOutsideHeld.value(QStringLiteral("historyDragCursorShape")).toInt()
                    == static_cast<int>(Qt::ForbiddenCursor)
                 && historyOutsideCanceled.value(QStringLiteral("invoked")).toBool()
                 && !historyOutsideCanceled.value(
                        QStringLiteral("historyDragPreviewVisible")).toBool()
                 && !historyOutsideCanceled.value(
                        QStringLiteral("historyDragCursorOverridden")).toBool(),
             QJsonObject{{QStringLiteral("held"), historyOutsideHeld},
                         {QStringLiteral("canceled"), historyOutsideCanceled}});
    const QJsonObject historyPointerOutside = dragHistoryUi(
        selectedHistoryId, 0, true, true, true, selectedHistoryPreview);
    addCheck(checks, details, QStringLiteral("historyDragDropOutsideCancels"),
             historyPointerOutside.value(QStringLiteral("eventsAccepted")).toBool()
                 && historyPointerOutside.value(QStringLiteral("text")).toString()
                    == QStringLiteral("outside-drop-target")
                 && historyPointerOutside.value(QStringLiteral("historyPanelOpen")).toBool()
                 && !historyPointerOutside.value(
                        QStringLiteral("historyCardDragActive")).toBool()
                 && !historyPointerOutside.value(
                        QStringLiteral("historyCardDropAllowed")).toBool(),
             historyPointerOutside);
    const QJsonObject beforeSingleClickText = request(QStringLiteral("testText"));
    historyAction(QStringLiteral("historySelect"), selectedHistoryId);
    const QJsonObject afterSingleClickText = request(QStringLiteral("testText"));
    addCheck(checks, details, QStringLiteral("historySingleClickOnlySelects"),
             !selectedHistoryId.isEmpty()
                 && beforeSingleClickText.value(QStringLiteral("text"))
                    == afterSingleClickText.value(QStringLiteral("text")),
             afterSingleClickText);
    const QJsonObject hoveredHistory = historyAction(
        QStringLiteral("historyItemHoverEnter"), hoveredHistoryId);
    const QJsonObject unhoveredHistory = historyAction(
        QStringLiteral("historyItemHoverLeave"), hoveredHistoryId);
    addCheck(checks, details, QStringLiteral("historyItemHoverOnlyHighlights"),
             !hoveredHistoryId.isEmpty()
                 && hoveredHistory.value(QStringLiteral("invoked")).toBool()
                 && hoveredHistory.value(QStringLiteral("historyHoveredId")).toString()
                    == hoveredHistoryId
                 && hoveredHistory.value(QStringLiteral("historySelectedId")).toString()
                    == selectedHistoryId
                 && unhoveredHistory.value(QStringLiteral("invoked")).toBool()
                 && unhoveredHistory.value(QStringLiteral("historyHoveredId")).toString().isEmpty()
                 && unhoveredHistory.value(QStringLiteral("historySelectedId")).toString()
                    == selectedHistoryId,
             QJsonObject{{QStringLiteral("actualHover"),
                          hoveredHistory.value(QStringLiteral("historyHoveredId"))},
                         {QStringLiteral("actualSelection"),
                          hoveredHistory.value(QStringLiteral("historySelectedId"))},
                         {QStringLiteral("afterLeaveHover"),
                          unhoveredHistory.value(QStringLiteral("historyHoveredId"))},
                         {QStringLiteral("expectedHover"), hoveredHistoryId},
                         {QStringLiteral("expectedSelection"), selectedHistoryId}});
    historyAction(QStringLiteral("historySetQuery"), QStringLiteral("ALPHA"));
    const QJsonObject filteredHistory = request(QStringLiteral("testClipboardHistoryState"));
    addCheck(checks, details, QStringLiteral("historySearchIsCaseInsensitiveFullText"),
             filteredHistory.value(QStringLiteral("visibleIds")).toArray().size() == 1,
             filteredHistory);
    const QString filteredId = filteredHistory.value(QStringLiteral("visibleIds"))
                                   .toArray().first().toString();
    historyAction(QStringLiteral("historySelect"), filteredId);
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), QStringLiteral("dirty buffer")}});
    const QJsonObject dirtyBefore = request(QStringLiteral("status"));
    const QString dirtyTextBefore = request(QStringLiteral("testText"))
                                        .value(QStringLiteral("text")).toString();
    historyAction(QStringLiteral("historyActivateSelected"));
    const QJsonObject dirtyConfirmation = request(QStringLiteral("status"));
    addCheck(checks, details, QStringLiteral("fileDropDisabledByLoadConfirmation"),
             dirtyConfirmation.value(
                QStringLiteral("historyLoadConfirmationVisible")).toBool()
                 && !dirtyConfirmation.value(QStringLiteral("fileDropEnabled")).toBool(),
             dirtyConfirmation);
    historyAction(QStringLiteral("historyCancelLoad"));
    const QJsonObject dirtyCancelled = request(QStringLiteral("status"));
    const QString dirtyTextAfterCancel = request(QStringLiteral("testText"))
                                             .value(QStringLiteral("text")).toString();
    addCheck(checks, details, QStringLiteral("historyDirtyLoadCancellationPreservesState"),
             dirtyConfirmation.value(QStringLiteral("historyLoadConfirmationVisible")).toBool()
                 && !dirtyCancelled.value(QStringLiteral("historyLoadConfirmationVisible")).toBool()
                 && dirtyTextAfterCancel == dirtyTextBefore
                 && dirtyCancelled.value(QStringLiteral("historyPanelOpen")).toBool()
                 && dirtyCancelled.value(QStringLiteral("historySelectedId")).toString()
                    == filteredId
                 && dirtyCancelled.value(QStringLiteral("cursorPosition")).toInt()
                    == dirtyBefore.value(QStringLiteral("cursorPosition")).toInt(),
             dirtyCancelled);
    historyAction(QStringLiteral("historyActivateSelected"));
    historyAction(QStringLiteral("historyConfirmLoad"));
    QThread::msleep(100);
    const QJsonObject loadedHistoryText = request(QStringLiteral("testText"));
    const QJsonObject loadedHistoryStatus = request(QStringLiteral("status"));
    addCheck(checks, details, QStringLiteral("historyConfirmedLoadResetsEditorState"),
             loadedHistoryText.value(QStringLiteral("text")).toString()
                    == QStringLiteral("alpha\nsecond line")
                 && loadedHistoryStatus.value(QStringLiteral("cursorPosition")).toInt() == 17
                 && !loadedHistoryStatus.value(QStringLiteral("historyPanelOpen")).toBool()
                 && loadedHistoryStatus.value(QStringLiteral("editorHasFocus")).toBool(),
             loadedHistoryStatus);

    historyAction(QStringLiteral("historyClose"));
    request(QStringLiteral("testSetGeometry"),
            {{QStringLiteral("x"), 100}, {QStringLiteral("y"), 100},
             {QStringLiteral("width"), 500}, {QStringLiteral("height"), 640}});
    QThread::msleep(180);
    execute(QStringLiteral("clipboardHistory"));
    QThread::msleep(150);
    const QJsonObject narrowHistory = request(QStringLiteral("status"));
    addCheck(checks, details, QStringLiteral("historyNarrowWindowUsesOverlay"),
             narrowHistory.value(QStringLiteral("historyPanelOverlay")).toBool()
                 && qAbs(narrowHistory.value(QStringLiteral("historyPanelWidth")).toDouble()
                         - 200.0) < 1.0
                 && narrowHistory.value(QStringLiteral("editorVisibleWidth")).toDouble() >= 320.0,
             narrowHistory);
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), QStringLiteral("overlay-drop-target")}});
    const QJsonObject historyOverlayCoveredDrop = dragHistoryUi(selectedHistoryId, 0);
    addCheck(checks, details, QStringLiteral("historyOverlayCoveredEditorRejectsDrop"),
             historyOverlayCoveredDrop.value(QStringLiteral("eventsAccepted")).toBool()
                 && historyOverlayCoveredDrop.value(QStringLiteral("text")).toString()
                    == QStringLiteral("overlay-drop-target")
                 && historyOverlayCoveredDrop.value(
                        QStringLiteral("historyPanelOpen")).toBool(),
             historyOverlayCoveredDrop);
    const double overlayTitleXDiff = qAbs(
        narrowHistory.value(QStringLiteral("historyTitleX")).toDouble()
        - narrowHistory.value(QStringLiteral("headerTitleX")).toDouble());
    const double overlayTitleYDiff = qAbs(
        narrowHistory.value(QStringLiteral("historyTitleY")).toDouble()
        - narrowHistory.value(QStringLiteral("headerTitleY")).toDouble());
    addCheck(checks, details, QStringLiteral("historyTitleAlignmentPersistsInOverlay"),
             overlayTitleXDiff <= 0.5 && overlayTitleYDiff <= 0.5,
             QJsonObject{{QStringLiteral("actual"),
                          QJsonObject{{QStringLiteral("x"),
                                       narrowHistory.value(QStringLiteral("historyTitleX"))},
                                      {QStringLiteral("y"),
                                       narrowHistory.value(QStringLiteral("historyTitleY"))}}},
                         {QStringLiteral("expected"),
                          QJsonObject{{QStringLiteral("x"),
                                       narrowHistory.value(QStringLiteral("headerTitleX"))},
                                      {QStringLiteral("y"),
                                       narrowHistory.value(QStringLiteral("headerTitleY"))}}}});
    const double overlaySearchTopDiff = qAbs(
        narrowHistory.value(QStringLiteral("historySearchFrameY")).toDouble()
        - narrowHistory.value(QStringLiteral("editorViewportY")).toDouble());
    addCheck(checks, details, QStringLiteral("historySearchAlignmentPersistsInOverlay"),
             overlaySearchTopDiff <= 0.5,
             QJsonObject{{QStringLiteral("actual"),
                          narrowHistory.value(QStringLiteral("historySearchFrameY"))},
                         {QStringLiteral("expected"),
                          narrowHistory.value(QStringLiteral("editorViewportY"))},
                         {QStringLiteral("diff"), overlaySearchTopDiff}});
    historyAction(QStringLiteral("historyClose"));
    historyAction(QStringLiteral("historyHoverTriggerEnter"));
    QThread::msleep(60);
    const bool hoverBeforeThreshold = request(QStringLiteral("status"))
                                          .value(QStringLiteral("historyPanelOpen")).toBool();
    QThread::msleep(70);
    const bool hoverAfterThreshold = request(QStringLiteral("status"))
                                         .value(QStringLiteral("historyPanelOpen")).toBool();
    historyAction(QStringLiteral("historyHoverTriggerLeave"));
    QThread::msleep(150);
    const bool closeBeforeThreshold = request(QStringLiteral("status"))
                                          .value(QStringLiteral("historyPanelOpen")).toBool();
    QThread::msleep(140);
    const bool closeAfterThreshold = request(QStringLiteral("status"))
                                         .value(QStringLiteral("historyPanelOpen")).toBool();
    addCheck(checks, details, QStringLiteral("historyHoverUsesNominalDelays"),
             !hoverBeforeThreshold && hoverAfterThreshold
                 && closeBeforeThreshold && !closeAfterThreshold);

    historyAction(QStringLiteral("historyHoverTriggerEnter"));
    QThread::msleep(130);
    historyAction(QStringLiteral("historyHoverTriggerLeave"));
    historyAction(QStringLiteral("historyPanelEnter"));
    QThread::msleep(290);
    const bool panelTransferStaysOpen = request(QStringLiteral("status"))
                                            .value(QStringLiteral("historyPanelOpen")).toBool();
    historyAction(QStringLiteral("historyPanelLeave"));
    QThread::msleep(280);
    const bool panelTransferEventuallyCloses = !request(QStringLiteral("status"))
                                                   .value(QStringLiteral("historyPanelOpen")).toBool();
    addCheck(checks, details, QStringLiteral("historyHoverTransferKeepsPanelOpen"),
             panelTransferStaysOpen && panelTransferEventuallyCloses);

    const QJsonObject leftExit = request(
        QStringLiteral("testClipboardHistoryWindowLeave"),
        {{QStringLiteral("x"), -4}, {QStringLiteral("y"), 320},
         {QStringLiteral("buttonDown"), false}});
    addCheck(checks, details, QStringLiteral("historyFastLeftExitOpensImmediately"),
             leftExit.value(QStringLiteral("edgeExitEmitted")).toBool()
                 && leftExit.value(QStringLiteral("historyPanelOpen")).toBool(),
             leftExit);
    historyAction(QStringLiteral("historyClose"));
    const QJsonObject rightExit = request(
        QStringLiteral("testClipboardHistoryWindowLeave"),
        {{QStringLiteral("x"), 924}, {QStringLiteral("y"), 320},
         {QStringLiteral("buttonDown"), false}});
    const QJsonObject topLeftExit = request(
        QStringLiteral("testClipboardHistoryWindowLeave"),
        {{QStringLiteral("x"), -4}, {QStringLiteral("y"), 4},
         {QStringLiteral("buttonDown"), false}});
    const QJsonObject bottomLeftExit = request(
        QStringLiteral("testClipboardHistoryWindowLeave"),
        {{QStringLiteral("x"), -4}, {QStringLiteral("y"), 636},
         {QStringLiteral("buttonDown"), false}});
    const QJsonObject draggingLeftExit = request(
        QStringLiteral("testClipboardHistoryWindowLeave"),
        {{QStringLiteral("x"), -4}, {QStringLiteral("y"), 320},
         {QStringLiteral("buttonDown"), true}});
    addCheck(checks, details, QStringLiteral("historyNonIntentionalWindowLeaveIgnored"),
             !rightExit.value(QStringLiteral("edgeExitEmitted")).toBool()
                 && !topLeftExit.value(QStringLiteral("edgeExitEmitted")).toBool()
                 && !bottomLeftExit.value(QStringLiteral("edgeExitEmitted")).toBool()
                 && !draggingLeftExit.value(QStringLiteral("edgeExitEmitted")).toBool()
                 && !draggingLeftExit.value(QStringLiteral("historyPanelOpen")).toBool(),
             QJsonObject{{QStringLiteral("right"), rightExit},
                         {QStringLiteral("topLeft"), topLeftExit},
                         {QStringLiteral("bottomLeft"), bottomLeftExit},
                         {QStringLiteral("dragging"), draggingLeftExit}});

    request(QStringLiteral("testEmitClipboardChange"),
            {{QStringLiteral("kind"), QStringLiteral("text")},
             {QStringLiteral("text"), QStringLiteral("delete-me")}});
    request(QStringLiteral("testSetClipboard"),
            {{QStringLiteral("text"), QStringLiteral("management-clipboard")}});
    const QString managementEditorBefore = request(QStringLiteral("testText"))
                                               .value(QStringLiteral("text")).toString();
    execute(QStringLiteral("clipboardHistory"));
    QThread::msleep(100);
    const QJsonObject beforeDelete = request(QStringLiteral("testClipboardHistoryState"));
    const QJsonObject managementDeliveryBefore = request(QStringLiteral("testDeliveredText"));
    const QJsonObject managementKeysBefore = request(QStringLiteral("testConfigKeys"));
    const QJsonObject managementStatusBefore = request(QStringLiteral("status"));
    const QString deleteId = beforeDelete.value(QStringLiteral("items")).toArray()
                                 .first().toObject().value(QStringLiteral("id")).toString();
    historyAction(QStringLiteral("historySelect"), deleteId);
    historyAction(QStringLiteral("historyDeleteSelected"));
    const QJsonObject afterDelete = request(QStringLiteral("testClipboardHistoryState"));
    historyAction(QStringLiteral("historyRequestClear"));
    const QJsonObject clearRequested = request(QStringLiteral("status"));
    addCheck(checks, details, QStringLiteral("fileDropDisabledByClearConfirmation"),
             clearRequested.value(
                QStringLiteral("historyClearConfirmationVisible")).toBool()
                 && !clearRequested.value(QStringLiteral("fileDropEnabled")).toBool(),
             clearRequested);
    historyAction(QStringLiteral("historyEscape"));
    const QJsonObject afterClearEscape = request(QStringLiteral("testClipboardHistoryState"));
    historyAction(QStringLiteral("historyRequestClear"));
    historyAction(QStringLiteral("historyCancelClear"));
    const QJsonObject afterClearCancel = request(QStringLiteral("testClipboardHistoryState"));
    historyAction(QStringLiteral("historyRequestClear"));
    historyAction(QStringLiteral("historyConfirmClear"));
    const QJsonObject afterClearConfirm = request(QStringLiteral("testClipboardHistoryState"));
    const QJsonObject managementClipboardAfter = request(QStringLiteral("testClipboard"));
    const QJsonObject managementDeliveryAfter = request(QStringLiteral("testDeliveredText"));
    const QJsonObject managementKeysAfter = request(QStringLiteral("testConfigKeys"));
    const QJsonObject managementStatusAfter = request(QStringLiteral("status"));
    const QString managementEditorAfter = request(QStringLiteral("testText"))
                                              .value(QStringLiteral("text")).toString();
    addCheck(checks, details, QStringLiteral("historyDeleteAndClearAreInternalOnly"),
             afterDelete.value(QStringLiteral("items")).toArray().size()
                    == beforeDelete.value(QStringLiteral("items")).toArray().size() - 1
                 && clearRequested.value(QStringLiteral("historyClearConfirmationVisible")).toBool()
                 && !afterClearEscape.value(QStringLiteral("historyClearConfirmationVisible")).toBool()
                 && afterClearEscape.value(QStringLiteral("items")).toArray().size()
                    == afterDelete.value(QStringLiteral("items")).toArray().size()
                 && afterClearCancel.value(QStringLiteral("items")).toArray().size()
                    == afterDelete.value(QStringLiteral("items")).toArray().size()
                 && afterClearConfirm.value(QStringLiteral("items")).toArray().isEmpty()
                 && managementClipboardAfter.value(QStringLiteral("text")).toString()
                    == QStringLiteral("management-clipboard")
                 && managementEditorAfter == managementEditorBefore
                 && managementDeliveryAfter.value(QStringLiteral("text"))
                    == managementDeliveryBefore.value(QStringLiteral("text"))
                 && managementKeysAfter.value(QStringLiteral("keys"))
                    == managementKeysBefore.value(QStringLiteral("keys"))
                 && managementStatusBefore.value(QStringLiteral("selectionStart"))
                    == managementStatusAfter.value(QStringLiteral("selectionStart"))
                 && managementStatusBefore.value(QStringLiteral("selectionEnd"))
                    == managementStatusAfter.value(QStringLiteral("selectionEnd")),
             afterClearConfirm);
    const QJsonObject animationsOff = request(QStringLiteral("status"));
    addCheck(checks, details, QStringLiteral("historyAnimationDisabledIsImmediate"),
             animationsOff.value(QStringLiteral("transitionDuration")).toInt() == 0,
             animationsOff);
    historyAction(QStringLiteral("historyClose"));
    request(QStringLiteral("testDiscardClose"));
    QThread::msleep(180);
    request(QStringLiteral("testSetGeometry"),
            {{QStringLiteral("x"), beforeHistoryLayout.value(QStringLiteral("x")).toInt()},
             {QStringLiteral("y"), beforeHistoryLayout.value(QStringLiteral("y")).toInt()},
             {QStringLiteral("width"), beforeHistoryLayout.value(QStringLiteral("width")).toInt()},
             {QStringLiteral("height"), beforeHistoryLayout.value(QStringLiteral("height")).toInt()}});
    QThread::msleep(180);

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
