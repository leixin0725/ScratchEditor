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
                 && initial.value(QStringLiteral("settingsSchemaVersion")).toInt() == 2
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
                 && initial.value(QStringLiteral("historyHoverCloseDelayMs")).toInt() == 250,
             initial);
    addCheck(checks, details, QStringLiteral("appearanceDefaults"),
             initial.value(QStringLiteral("theme")).toString() == QStringLiteral("dark")
                 && initial.value(QStringLiteral("editorFontFamily")).toString()
                    == QStringLiteral("Microsoft YaHei UI")
                 && initial.value(QStringLiteral("editorFontPointSize")).toInt() == 13
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

    const QJsonObject animOffApplied = request(
        QStringLiteral("testApplyAppearance"),
        {{QStringLiteral("theme"), QStringLiteral("light")},
         {QStringLiteral("fontFamily"), QStringLiteral("Microsoft YaHei UI")},
         {QStringLiteral("fontPointSize"), 15},
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
                 == QStringLiteral("共 8 字"),
             summaryPlain);
    addCheck(checks, details, QStringLiteral("statusPanelSummarySelected"),
             summarySelected.value(QStringLiteral("statusPanelSummary")).toString()
                 == QStringLiteral("2 / 8 字"),
             summarySelected);

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
                 && hasKey(keys, QStringLiteral("appearance/fontFamily"))
                 && hasKey(keys, QStringLiteral("appearance/fontPointSize"))
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

    // 动画开启、历史面板打开（推挤模式）时缩放：编辑区必须即时跟随窗口边缘，
    // 不允许 x/width 的 Behavior 逐帧重启动画造成滞后追赶（修复前 30ms 处差值约 20px）。
    request(QStringLiteral("testSetGeometry"),
            {{QStringLiteral("x"), 100}, {QStringLiteral("y"), 100},
             {QStringLiteral("width"), 960}, {QStringLiteral("height"), 660}});
    QThread::msleep(30);
    const QJsonObject resizedOpenHistory = request(QStringLiteral("status"));
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
    request(QStringLiteral("testSetGeometry"),
            {{QStringLiteral("x"), 100}, {QStringLiteral("y"), 100},
             {QStringLiteral("width"), 920}, {QStringLiteral("height"), 640}});
    QThread::msleep(180);

    execute(QStringLiteral("clipboardHistory"));
    QThread::msleep(150);
    const QJsonObject toggledClosedHistory = request(QStringLiteral("status"));
    addCheck(checks, details, QStringLiteral("historyCommandTogglesClosed"),
             !toggledClosedHistory.value(QStringLiteral("historyPanelOpen")).toBool(),
             toggledClosedHistory);
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

    const QJsonObject historyState = request(QStringLiteral("testClipboardHistoryState"));
    const QJsonArray historyItems = historyState.value(QStringLiteral("items")).toArray();
    const QString selectedHistoryId = historyItems.isEmpty()
        ? QString() : historyItems.first().toObject().value(QStringLiteral("id")).toString();
    const QJsonObject beforeSingleClickText = request(QStringLiteral("testText"));
    historyAction(QStringLiteral("historySelect"), selectedHistoryId);
    const QJsonObject afterSingleClickText = request(QStringLiteral("testText"));
    addCheck(checks, details, QStringLiteral("historySingleClickOnlySelects"),
             !selectedHistoryId.isEmpty()
                 && beforeSingleClickText.value(QStringLiteral("text"))
                    == afterSingleClickText.value(QStringLiteral("text")),
             afterSingleClickText);
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
