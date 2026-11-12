#include "suites.h"
#include "support.h"

#include <QJsonObject>
#include <QThread>
#include <cmath>

namespace EditingTests {

void checkScrolling(QJsonObject &checks, QJsonObject &details)
{
    // --- 翻页浏览与输入自动滚动 ---
    QString scrollText;
    scrollText.reserve(4000);
    for (int i = 0; i < 80; ++i) {
        scrollText += QStringLiteral("line-%1 abcdefghij klmnopqrstuvwxyz\n")
                          .arg(i, 2, 10, QLatin1Char('0'));
    }
    const int scrollMidCursor = scrollText.indexOf(QStringLiteral("line-70"));
    const int scrollEndCursor = scrollText.size();

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 10}, {QStringLiteral("end"), 20}});
    setScrollY(0);
    QThread::msleep(50);
    const QJsonObject scrollTop = editorStatus();
    const double pageViewportHeight =
        scrollTop.value(QStringLiteral("scrollViewportHeight")).toDouble();
    const double pageMaxY =
        scrollTop.value(QStringLiteral("scrollContentHeight")).toDouble()
        - pageViewportHeight;
    keyPress({}, QStringLiteral("PageDown"));
    QThread::msleep(40);
    const QJsonObject pageDownStatus = editorStatus();
    keyPress({}, QStringLiteral("PageUp"));
    QThread::msleep(40);
    const QJsonObject pageUpStatus = editorStatus();
    addCheck(checks, details, QStringLiteral("pageScrollPageDownUpKeepsCursorSelection"),
             std::abs(pageDownStatus.value(QStringLiteral("scrollContentY")).toDouble()
                      - qMin(pageViewportHeight, pageMaxY)) < 1.5
                 && pageDownStatus.value(QStringLiteral("cursorPosition")).toInt() == 20
                 && pageDownStatus.value(QStringLiteral("selectionStart")).toInt() == 10
                 && pageDownStatus.value(QStringLiteral("selectionEnd")).toInt() == 20
                 && pageDownStatus.value(QStringLiteral("scrollContentY")).toDouble()
                    > scrollTop.value(QStringLiteral("scrollContentY")).toDouble()
                 && std::abs(pageUpStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - scrollTop.value(QStringLiteral("scrollContentY")).toDouble()) < 1.5,
             QJsonObject{{QStringLiteral("top"), scrollTop},
                         {QStringLiteral("pageDown"), pageDownStatus},
                         {QStringLiteral("pageUp"), pageUpStatus}});

    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}});
    setScrollY(pageMaxY);
    QThread::msleep(20);
    keyPress({}, QStringLiteral("PageDown"));
    QThread::msleep(40);
    const QJsonObject clampedBottom = editorStatus();
    setScrollY(0);
    QThread::msleep(20);
    keyPress({}, QStringLiteral("PageUp"));
    QThread::msleep(40);
    const QJsonObject clampedTop = editorStatus();
    addCheck(checks, details, QStringLiteral("pageScrollClampsAtEnds"),
             std::abs(clampedBottom.value(QStringLiteral("scrollContentY")).toDouble()
                      - pageMaxY) < 1.5
                 && std::abs(clampedTop.value(QStringLiteral("scrollContentY")).toDouble())
                        < 1.5,
             QJsonObject{{QStringLiteral("maxY"), pageMaxY},
                         {QStringLiteral("bottom"), clampedBottom},
                         {QStringLiteral("top"), clampedTop}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    QThread::msleep(50);
    const QJsonObject paddingStatus = editorStatus();
    const double paddingContentHeight =
        paddingStatus.value(QStringLiteral("scrollContentHeight")).toDouble();
    const double paddingTextHeight =
        paddingStatus.value(QStringLiteral("editorContentHeight")).toDouble();
    const double paddingViewportHeight =
        paddingStatus.value(QStringLiteral("scrollViewportHeight")).toDouble();
    addCheck(checks, details, QStringLiteral("scrollPastEndReservesTwoThirdsPage"),
             std::abs(paddingContentHeight
                      - (paddingTextHeight + paddingViewportHeight * 2.0 / 3.0)) < 1.5
                 && paddingContentHeight > paddingViewportHeight,
             paddingStatus);

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), QStringLiteral("short")}});
    // 滚动条可见性由 60ms 防抖定时器刷新，需等待其完成后读取。
    QThread::msleep(150);
    const QJsonObject shortDocStatus = editorStatus();
    keyPress({}, QStringLiteral("PageDown"));
    QThread::msleep(40);
    const QJsonObject shortDocPageDown = editorStatus();
    addCheck(checks, details, QStringLiteral("shortDocumentHasNoScrollRange"),
             std::abs(shortDocStatus.value(QStringLiteral("scrollContentHeight")).toDouble()
                      - shortDocStatus.value(QStringLiteral("scrollViewportHeight")).toDouble())
                     < 1.5
                 && !shortDocStatus.value(QStringLiteral("verticalScrollBarVisible")).toBool()
                 && std::abs(shortDocPageDown.value(QStringLiteral("scrollContentY")).toDouble())
                        < 1.5
                 && shortDocPageDown.value(QStringLiteral("cursorPosition")).toInt() == 5,
             QJsonObject{{QStringLiteral("before"), shortDocStatus},
                         {QStringLiteral("pageDown"), shortDocPageDown}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollEndCursor},
             {QStringLiteral("end"), scrollEndCursor}});
    setScrollY(0);
    QThread::msleep(50);
    const QJsonObject beforeEndInput = editorStatus();
    const double endMaxY =
        beforeEndInput.value(QStringLiteral("scrollContentHeight")).toDouble()
        - beforeEndInput.value(QStringLiteral("scrollViewportHeight")).toDouble();
    keyPress(QStringLiteral("x"));
    QThread::msleep(50);
    const QJsonObject afterEndInput = editorStatus();
    addCheck(checks, details, QStringLiteral("inputAtDocumentEndScrollsToBottom"),
             std::abs(afterEndInput.value(QStringLiteral("scrollContentY")).toDouble()
                      - endMaxY) < 1.5
                 && afterEndInput.value(QStringLiteral("cursorPosition")).toInt()
                    == scrollEndCursor + 1
                 && editorText() == scrollText + QStringLiteral("x"),
             QJsonObject{{QStringLiteral("maxY"), endMaxY},
                         {QStringLiteral("after"), afterEndInput}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollMidCursor},
             {QStringLiteral("end"), scrollMidCursor}});
    setScrollY(0);
    QThread::msleep(50);
    keyPress(QStringLiteral("x"));
    QThread::msleep(50);
    const QJsonObject midInputStatus = editorStatus();
    const double midExpectedY =
        midInputStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + midInputStatus.value(QStringLiteral("cursorRectY")).toDouble()
        - midInputStatus.value(QStringLiteral("scrollViewportHeight")).toDouble() / 3.0;
    const double midMaxY =
        midInputStatus.value(QStringLiteral("scrollContentHeight")).toDouble()
        - midInputStatus.value(QStringLiteral("scrollViewportHeight")).toDouble();
    addCheck(checks, details, QStringLiteral("inputMidDocumentAnchorsToUpperThird"),
             std::abs(midInputStatus.value(QStringLiteral("scrollContentY")).toDouble()
                      - midExpectedY) < 3.0
                 && midInputStatus.value(QStringLiteral("scrollContentY")).toDouble() > 0.0
                 && midInputStatus.value(QStringLiteral("scrollContentY")).toDouble() < midMaxY,
             QJsonObject{{QStringLiteral("expectedY"), midExpectedY},
                         {QStringLiteral("maxY"), midMaxY},
                         {QStringLiteral("status"), midInputStatus}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}});
    setScrollY(0);
    QThread::msleep(50);
    keyPress(QStringLiteral("x"));
    QThread::msleep(50);
    const QJsonObject topInputStatus = editorStatus();
    addCheck(checks, details, QStringLiteral("inputVisibleAboveBottomDoesNotScroll"),
             std::abs(topInputStatus.value(QStringLiteral("scrollContentY")).toDouble()) < 1.5
                 && editorText().startsWith(QStringLiteral("x")),
             topInputStatus);

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollMidCursor},
             {QStringLiteral("end"), scrollMidCursor}});
    setScrollY(0);
    QThread::msleep(50);
    keyPress({}, QStringLiteral("Down"));
    QThread::msleep(50);
    const QJsonObject arrowMoveStatus = editorStatus();
    const double arrowMoveY =
        arrowMoveStatus.value(QStringLiteral("scrollContentY")).toDouble();
    keyPress(QStringLiteral("x"));
    QThread::msleep(50);
    const QJsonObject afterArrowTypeStatus = editorStatus();
    const double arrowTypeY =
        afterArrowTypeStatus.value(QStringLiteral("scrollContentY")).toDouble();
    const double arrowTypeMaxY =
        afterArrowTypeStatus.value(QStringLiteral("scrollContentHeight")).toDouble()
        - afterArrowTypeStatus.value(QStringLiteral("scrollViewportHeight")).toDouble();
    addCheck(checks, details, QStringLiteral("arrowMoveUsesMinimalFollowNotAnchor"),
             arrowMoveY > 0.0 && arrowMoveY < arrowTypeY && arrowTypeY <= arrowTypeMaxY,
             QJsonObject{{QStringLiteral("arrowY"), arrowMoveY},
                         {QStringLiteral("typedY"), arrowTypeY},
                         {QStringLiteral("maxY"), arrowTypeMaxY},
                         {QStringLiteral("arrowStatus"), arrowMoveStatus}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollEndCursor},
             {QStringLiteral("end"), scrollEndCursor}});
    setScrollY(0);
    QThread::msleep(50);
    keyPress(QStringLiteral("x"));
    QThread::msleep(50);
    const QJsonObject beforeUndoInput = editorStatus();
    request(QStringLiteral("testUndo"));
    QThread::msleep(80);
    const QJsonObject afterUndoInput = editorStatus();
    const double undoInputMaxY =
        afterUndoInput.value(QStringLiteral("scrollContentHeight")).toDouble()
        - afterUndoInput.value(QStringLiteral("scrollViewportHeight")).toDouble();
    // 撤销不再回滚滚动位置：视图停在当前底部，只恢复文本。
    addCheck(checks, details, QStringLiteral("undoAfterEndInputKeepsViewAtBottom"),
             afterUndoInput.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor
                 && std::abs(afterUndoInput.value(QStringLiteral("scrollContentY")).toDouble()
                             - undoInputMaxY) < 1.5
                 && afterUndoInput.value(QStringLiteral("scrollContentY")).toDouble() > 0.0
                 && editorText() == scrollText,
             QJsonObject{{QStringLiteral("before"), beforeUndoInput},
                         {QStringLiteral("after"), afterUndoInput},
                         {QStringLiteral("maxY"), undoInputMaxY}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollEndCursor},
             {QStringLiteral("end"), scrollEndCursor}});
    setScrollY(0);
    QThread::msleep(50);
    keyPress(QStringLiteral("x"));
    QThread::msleep(50);
    const QJsonObject afterFirstEndInput = editorStatus();
    const double firstEndMaxY =
        afterFirstEndInput.value(QStringLiteral("scrollContentHeight")).toDouble()
        - afterFirstEndInput.value(QStringLiteral("scrollViewportHeight")).toDouble();
    // 段尾首次触底触发一次滚到底（max）；随后同一行继续输入不再触发。
    keyPress(QStringLiteral("y"));
    QThread::msleep(50);
    const QJsonObject afterSecondEndInput = editorStatus();
    // 回车新增一行后光标仍远离底边，同样不触发（间歇式，而非钉在 1/3）。
    keyPress({}, QStringLiteral("Enter"));
    QThread::msleep(50);
    const QJsonObject afterEndEnter = editorStatus();
    addCheck(checks, details, QStringLiteral("endInputScrollIsIntermittent"),
             std::abs(afterFirstEndInput.value(QStringLiteral("scrollContentY")).toDouble()
                      - firstEndMaxY) < 1.5
                 && std::abs(afterSecondEndInput.value(QStringLiteral("scrollContentY")).toDouble()
                             - firstEndMaxY) < 1.5
                 && std::abs(afterEndEnter.value(QStringLiteral("scrollContentY")).toDouble()
                             - firstEndMaxY) < 1.5
                 && afterSecondEndInput.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor + 2,
             QJsonObject{{QStringLiteral("maxY"), firstEndMaxY},
                         {QStringLiteral("afterFirst"), afterFirstEndInput},
                         {QStringLiteral("afterSecond"), afterSecondEndInput},
                         {QStringLiteral("afterEnter"), afterEndEnter}});

    // 继续连续回车，光标从视口上 1/3 自然下落，再次触底时才再次触发。
    int endEnterCount = 0;
    for (; endEnterCount < 39; ++endEnterCount) {
        keyPress({}, QStringLiteral("Enter"));
        // 与真实输入节奏一致（大于自动滚动检查的 40ms 延迟），
        // 保证每笔输入各有一条滚动记录，撤销链逐级对齐。
        QThread::msleep(50);
    }
    const QJsonObject afterManyEnters = editorStatus();
    const double chainY0 =
        afterManyEnters.value(QStringLiteral("scrollContentY")).toDouble();
    const double chainViewportHeight =
        afterManyEnters.value(QStringLiteral("scrollViewportHeight")).toDouble();
    const double chainBaseScreenY =
        afterManyEnters.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + afterManyEnters.value(QStringLiteral("cursorRectY")).toDouble()
        - chainY0;
    // 撤销链保持 + 顶镜像：连续撤销末尾删除时视图不被钳回新 max，
    // 光标逐行上移；越过顶边后由顶规则间歇触发（光标行锚定到视口距顶 2/3）。
    int chainUndoCount = 0;
    int chainTopTriggerCount = 0;
    bool chainHeldUntilTrigger = true;
    double chainFirstUndoY = -1.0;
    double chainFirstUndoScreenY = -1.0;
    QJsonObject chainTriggerSample;
    while (chainUndoCount < 50 && editorText() != scrollText) {
        request(QStringLiteral("testUndo"));
        // 每笔撤销后等待超过自动滚动检查的 40ms 延迟再采样，逐级观察保持与触发。
        QThread::msleep(55);
        const QJsonObject chainStep = editorStatus();
        const double stepScreenY =
            chainStep.value(QStringLiteral("editorContentOffsetY")).toDouble()
            + chainStep.value(QStringLiteral("cursorRectY")).toDouble()
            - chainStep.value(QStringLiteral("scrollContentY")).toDouble();
        const bool stepTriggered =
            std::abs(stepScreenY - chainViewportHeight * 2.0 / 3.0) < 3.0;
        if (chainUndoCount == 0) {
            chainFirstUndoY =
                chainStep.value(QStringLiteral("scrollContentY")).toDouble();
            chainFirstUndoScreenY = stepScreenY;
        }
        if (stepTriggered) {
            ++chainTopTriggerCount;
            chainTriggerSample = chainStep;
        } else if (chainTopTriggerCount == 0
                   && std::abs(chainStep.value(QStringLiteral("scrollContentY")).toDouble()
                               - chainY0) >= 1.5) {
            chainHeldUntilTrigger = false;
        }
        ++chainUndoCount;
    }
    QThread::msleep(60);
    const QJsonObject afterFullEndUndo = editorStatus();
    // 撤销链早期逐笔保持输入前视口位置（contentY ≈ Y0、光标屏上位置下降），
    // 越过顶边后顶触发发生；文本最终完整还原。
    addCheck(checks, details, QStringLiteral("endInputScrollReTriggersAndUndoChainKeepsView"),
             afterManyEnters.value(QStringLiteral("scrollContentY")).toDouble()
                    > firstEndMaxY
                 && chainUndoCount == 42
                 && std::abs(chainFirstUndoY - chainY0) < 1.5
                 && chainFirstUndoScreenY < chainBaseScreenY - 10.0
                 && chainHeldUntilTrigger
                 && chainTopTriggerCount >= 2
                 && afterFullEndUndo.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor
                 && afterFullEndUndo.value(QStringLiteral("scrollContentY")).toDouble() > 0.0
                 && editorText() == scrollText,
             QJsonObject{{QStringLiteral("maxY"), firstEndMaxY},
                         {QStringLiteral("afterEnters"), afterManyEnters},
                         {QStringLiteral("chainY0"), chainY0},
                         {QStringLiteral("baseScreenY"), chainBaseScreenY},
                         {QStringLiteral("firstUndoY"), chainFirstUndoY},
                         {QStringLiteral("firstUndoScreenY"), chainFirstUndoScreenY},
                         {QStringLiteral("topTriggerCount"), chainTopTriggerCount},
                         {QStringLiteral("triggerSample"), chainTriggerSample},
                         {QStringLiteral("afterFullUndo"), afterFullEndUndo}});

    // 两级撤销删除：第一笔撤销（回车新增行）收缩内容时视图保持输入前位置，
    // 光标随删除自然上移；第二笔撤销（删除字符、行高不变）视图继续稳定，
    // 不会被钳回新的底部。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollEndCursor},
             {QStringLiteral("end"), scrollEndCursor}});
    setScrollY(0);
    QThread::msleep(50);
    keyPress(QStringLiteral("x"));
    QThread::msleep(50);
    const QJsonObject firstScrolledInput = editorStatus();
    const double bottomEdgeY =
        firstScrolledInput.value(QStringLiteral("editorContentHeight")).toDouble()
        - firstScrolledInput.value(QStringLiteral("scrollViewportHeight")).toDouble();
    setScrollY(bottomEdgeY);
    QThread::msleep(20);
    keyPress({}, QStringLiteral("Enter"));
    QThread::msleep(50);
    const QJsonObject secondScrolledInput = editorStatus();
    const double secondScrolledScreenY =
        secondScrolledInput.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + secondScrolledInput.value(QStringLiteral("cursorRectY")).toDouble()
        - secondScrolledInput.value(QStringLiteral("scrollContentY")).toDouble();
    request(QStringLiteral("testUndo"));
    // 文本高度收缩后的首帧必须继续保持编辑前视口；旧实现会先被
    // Flickable 钳到新的自然 max，40ms 自动滚动检查后才恢复，形成抽动。
    QThread::msleep(20);
    const QJsonObject firstUndoTransient = editorStatus();
    QThread::msleep(60);
    const QJsonObject firstUndo = editorStatus();
    const double firstUndoScreenY =
        firstUndo.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + firstUndo.value(QStringLiteral("cursorRectY")).toDouble()
        - firstUndo.value(QStringLiteral("scrollContentY")).toDouble();
    request(QStringLiteral("testUndo"));
    QThread::msleep(80);
    const QJsonObject secondUndo = editorStatus();
    addCheck(checks, details, QStringLiteral("undoKeepsViewWhileTextRestores"),
             secondScrolledInput.value(QStringLiteral("scrollContentY")).toDouble()
                    > bottomEdgeY
                 && firstUndo.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor + 1
                 && std::abs(firstUndoTransient.value(
                                 QStringLiteral("scrollContentY")).toDouble()
                             - secondScrolledInput.value(
                                 QStringLiteral("scrollContentY")).toDouble()) < 1.5
                 && std::abs(firstUndo.value(QStringLiteral("scrollContentY")).toDouble()
                             - secondScrolledInput.value(
                                 QStringLiteral("scrollContentY")).toDouble()) < 1.5
                 && firstUndoScreenY < secondScrolledScreenY - 10.0
                 && secondUndo.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor
                 && std::abs(secondUndo.value(QStringLiteral("scrollContentY")).toDouble()
                             - secondScrolledInput.value(
                                 QStringLiteral("scrollContentY")).toDouble()) < 1.5
                 && secondUndo.value(QStringLiteral("scrollContentY")).toDouble()
                    > secondUndo.value(QStringLiteral("editorContentHeight")).toDouble()
                        - secondUndo.value(QStringLiteral("scrollViewportHeight")).toDouble()
                        + 1.5
                 && editorText() == scrollText,
             QJsonObject{{QStringLiteral("bottomEdgeY"), bottomEdgeY},
                         {QStringLiteral("first"), firstScrolledInput},
                         {QStringLiteral("second"), secondScrolledInput},
                         {QStringLiteral("secondScreenY"), secondScrolledScreenY},
                         {QStringLiteral("firstUndoTransient"), firstUndoTransient},
                         {QStringLiteral("firstUndo"), firstUndo},
                         {QStringLiteral("firstUndoScreenY"), firstUndoScreenY},
                         {QStringLiteral("secondUndo"), secondUndo},
                         {QStringLiteral("naturalMaxY"),
                          secondUndo.value(QStringLiteral("editorContentHeight")).toDouble()
                              - secondUndo.value(
                                  QStringLiteral("scrollViewportHeight")).toDouble()}});

    // 重做删除与撤销删除共用同一套保持：构造可重做的末尾删行
    // （回车新增空行 → 退格删行 → 撤销恢复 → 重做再删），
    // 重做删除同样保持视图、光标自然上移，而不是被钳回新的底部。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollEndCursor},
             {QStringLiteral("end"), scrollEndCursor}});
    setScrollY(0);
    QThread::msleep(50);
    keyPress(QStringLiteral("x"));
    QThread::msleep(50);
    const QJsonObject redoBaseScrolled = editorStatus();
    keyPress({}, QStringLiteral("Enter"));
    QThread::msleep(50);
    const QJsonObject redoEntered = editorStatus();
    // 把视口落到回车后带留白的新底边，让后续删行时输入前位置高于新 max，
    // 真正构造出“内容收缩 + 视图位于底部”的保持场景。
    setScrollY(redoEntered.value(QStringLiteral("scrollContentHeight")).toDouble()
               - redoEntered.value(QStringLiteral("scrollViewportHeight")).toDouble());
    QThread::msleep(30);
    const QJsonObject redoAtMax = editorStatus();
    const double redoEnteredScreenY =
        redoAtMax.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + redoAtMax.value(QStringLiteral("cursorRectY")).toDouble()
        - redoAtMax.value(QStringLiteral("scrollContentY")).toDouble();
    keyPress({}, QStringLiteral("Backspace"));
    QThread::msleep(20);
    const QJsonObject redoBackspaceTransient = editorStatus();
    QThread::msleep(60);
    const QJsonObject redoBackspace = editorStatus();
    const double redoBackspaceScreenY =
        redoBackspace.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + redoBackspace.value(QStringLiteral("cursorRectY")).toDouble()
        - redoBackspace.value(QStringLiteral("scrollContentY")).toDouble();
    request(QStringLiteral("testUndo"));
    QThread::msleep(80);
    const QJsonObject redoRestored = editorStatus();
    const double redoRestoredScreenY =
        redoRestored.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + redoRestored.value(QStringLiteral("cursorRectY")).toDouble()
        - redoRestored.value(QStringLiteral("scrollContentY")).toDouble();
    request(QStringLiteral("testRedo"));
    QThread::msleep(20);
    const QJsonObject redoDeletedTransient = editorStatus();
    QThread::msleep(60);
    const QJsonObject redoDeleted = editorStatus();
    const double redoDeletedScreenY =
        redoDeleted.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + redoDeleted.value(QStringLiteral("cursorRectY")).toDouble()
        - redoDeleted.value(QStringLiteral("scrollContentY")).toDouble();
    const double redoDeletedMaxY =
        redoDeleted.value(QStringLiteral("editorContentHeight")).toDouble()
        - redoDeleted.value(QStringLiteral("scrollViewportHeight")).toDouble();
    addCheck(checks, details, QStringLiteral("redoDeleteHoldsViewWhileCursorAscends"),
             std::abs(redoAtMax.value(QStringLiteral("scrollContentY")).toDouble()
                     - (redoEntered.value(QStringLiteral("scrollContentHeight")).toDouble()
                         - redoEntered.value(QStringLiteral("scrollViewportHeight")).toDouble()))
                    < 1.5
                 && std::abs(redoBackspaceTransient.value(
                                 QStringLiteral("scrollContentY")).toDouble()
                             - redoAtMax.value(
                                 QStringLiteral("scrollContentY")).toDouble()) < 1.5
                 && std::abs(redoBackspace.value(QStringLiteral("scrollContentY")).toDouble()
                             - redoAtMax.value(QStringLiteral("scrollContentY")).toDouble())
                        < 1.5
                 && redoBackspaceScreenY < redoEnteredScreenY - 10.0
                 && std::abs(redoDeleted.value(QStringLiteral("scrollContentY")).toDouble()
                             - redoRestored.value(QStringLiteral("scrollContentY")).toDouble())
                        < 1.5
                 && std::abs(redoDeletedTransient.value(
                                 QStringLiteral("scrollContentY")).toDouble()
                             - redoRestored.value(
                                 QStringLiteral("scrollContentY")).toDouble()) < 1.5
                 && redoDeletedScreenY < redoRestoredScreenY - 10.0
                 && redoDeleted.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor + 1
                 && redoDeleted.value(QStringLiteral("scrollContentY")).toDouble()
                    > redoDeletedMaxY + 1.5
                 && editorText() == scrollText + QStringLiteral("x"),
             QJsonObject{{QStringLiteral("base"), redoBaseScrolled},
                         {QStringLiteral("entered"), redoEntered},
                         {QStringLiteral("atMax"), redoAtMax},
                         {QStringLiteral("enteredScreenY"), redoEnteredScreenY},
                         {QStringLiteral("backspaceTransient"), redoBackspaceTransient},
                         {QStringLiteral("backspace"), redoBackspace},
                         {QStringLiteral("backspaceScreenY"), redoBackspaceScreenY},
                         {QStringLiteral("restored"), redoRestored},
                         {QStringLiteral("restoredScreenY"), redoRestoredScreenY},
                         {QStringLiteral("deletedTransient"), redoDeletedTransient},
                         {QStringLiteral("deleted"), redoDeleted},
                         {QStringLiteral("deletedScreenY"), redoDeletedScreenY},
                         {QStringLiteral("deletedMaxY"), redoDeletedMaxY}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}});
    setScrollY(0);
    QThread::msleep(50);
    keyPress(QStringLiteral("y"));
    QThread::msleep(50);
    const QJsonObject topTypedStatus = editorStatus();
    request(QStringLiteral("testUndo"));
    QThread::msleep(80);
    const QJsonObject topUndoStatus = editorStatus();
    // 光标可见时撤销不触发任何滚动（既无回滚也无顶边锚定）。
    addCheck(checks, details, QStringLiteral("undoVisibleCursorKeepsView"),
             std::abs(topTypedStatus.value(QStringLiteral("scrollContentY")).toDouble()) < 1.5
                 && std::abs(topUndoStatus.value(QStringLiteral("scrollContentY")).toDouble())
                        < 1.5
                 && editorText() == scrollText,
             QJsonObject{{QStringLiteral("typed"), topTypedStatus},
                         {QStringLiteral("undo"), topUndoStatus}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollEndCursor},
             {QStringLiteral("end"), scrollEndCursor}});
    setScrollY(0);
    QThread::msleep(50);
    inputMethodCommit(QStringLiteral("中文"));
    QThread::msleep(80);
    const QJsonObject imeScrollStatus = editorStatus();
    const QString imeCommittedText = editorText();
    const double imeMaxY =
        imeScrollStatus.value(QStringLiteral("scrollContentHeight")).toDouble()
        - imeScrollStatus.value(QStringLiteral("scrollViewportHeight")).toDouble();
    request(QStringLiteral("testUndo"));
    QThread::msleep(80);
    const QJsonObject imeUndoStatus = editorStatus();
    const double imeUndoMaxY =
        imeUndoStatus.value(QStringLiteral("scrollContentHeight")).toDouble()
        - imeUndoStatus.value(QStringLiteral("scrollViewportHeight")).toDouble();
    // IME 提交触底照常滚到底；撤销只恢复文本，视图停在底部不回跳。
    addCheck(checks, details, QStringLiteral("imeCommitAtEndScrollsAndUndoKeepsView"),
             std::abs(imeScrollStatus.value(QStringLiteral("scrollContentY")).toDouble()
                      - imeMaxY) < 1.5
                 && imeCommittedText == scrollText + QStringLiteral("中文")
                 && std::abs(imeUndoStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - imeUndoMaxY) < 1.5
                 && imeUndoStatus.value(QStringLiteral("scrollContentY")).toDouble() > 0.0
                 && editorText() == scrollText,
             QJsonObject{{QStringLiteral("maxY"), imeMaxY},
                         {QStringLiteral("committed"), imeScrollStatus},
                         {QStringLiteral("undo"), imeUndoStatus},
                         {QStringLiteral("undoMaxY"), imeUndoMaxY}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollEndCursor},
             {QStringLiteral("end"), scrollEndCursor}});
    setScrollY(endMaxY);
    QThread::msleep(50);
    const QJsonObject beforePagedInput = editorStatus();
    const double pagedMaxY =
        beforePagedInput.value(QStringLiteral("scrollContentHeight")).toDouble()
        - beforePagedInput.value(QStringLiteral("scrollViewportHeight")).toDouble();
    keyPress(QStringLiteral("x"));
    QThread::msleep(50);
    const QJsonObject pagedIntoBlankInput = editorStatus();
    addCheck(checks, details, QStringLiteral("inputAtEndAfterPagingIntoBlankStaysBottom"),
             std::abs(pagedIntoBlankInput.value(QStringLiteral("scrollContentY")).toDouble()
                      - pagedMaxY) < 1.5
                 && editorText() == scrollText + QStringLiteral("x"),
             QJsonObject{{QStringLiteral("maxY"), pagedMaxY},
                         {QStringLiteral("after"), pagedIntoBlankInput}});

    // --- 删除触顶自动滚动（严格镜像）与撤销/重做统一检查 ---
    const QString scrollLine =
        QStringLiteral("line-40 abcdefghij klmnopqrstuvwxyz\n");
    const int scrollNearTopCursor = scrollText.indexOf(QStringLiteral("line-24"));
    const int scrollTopCursor = scrollText.indexOf(QStringLiteral("line-40"));
    const int scrollSelectionStart = scrollText.indexOf(QStringLiteral("line-40"));
    const int scrollSelectionEnd = scrollText.indexOf(QStringLiteral("line-50"));

    // 下三分点锚定目标距第一行不足 1/3 屏时直接吸附到文档顶部，
    // 避免在接近开头的位置留下不足一屏的零碎回滚距离。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollNearTopCursor},
             {QStringLiteral("end"), scrollNearTopCursor}});
    setScrollY(pageMaxY);
    QThread::msleep(30);
    keyPress({}, QStringLiteral("Backspace"));
    QThread::msleep(80);
    const QJsonObject deleteNearTopStatus = editorStatus();
    const double deleteNearTopAnchorY =
        deleteNearTopStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + deleteNearTopStatus.value(QStringLiteral("cursorRectY")).toDouble()
        - deleteNearTopStatus.value(QStringLiteral("scrollViewportHeight")).toDouble()
            * 2.0 / 3.0;
    const double deleteNearTopThreshold =
        deleteNearTopStatus.value(QStringLiteral("scrollViewportHeight")).toDouble() / 3.0;
    addCheck(checks, details, QStringLiteral("deleteNearTopSnapsToDocumentTop"),
             deleteNearTopStatus.value(QStringLiteral("cursorPosition")).toInt()
                    == scrollNearTopCursor - 1
                 && deleteNearTopAnchorY > 1.5
                 && deleteNearTopAnchorY < deleteNearTopThreshold
                 && std::abs(deleteNearTopStatus.value(QStringLiteral("scrollContentY"))
                                 .toDouble()) < 1.5,
             QJsonObject{{QStringLiteral("anchorY"), deleteNearTopAnchorY},
                         {QStringLiteral("threshold"), deleteNearTopThreshold},
                         {QStringLiteral("status"), deleteNearTopStatus}});

    // 退格使光标位于视口顶边之上：先由 QML 最小跟随滚到顶边，
    // 再由统一检查把光标行锚定到视口距顶 2/3 处（下 1/3）。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollTopCursor},
             {QStringLiteral("end"), scrollTopCursor}});
    setScrollY(pageMaxY);
    QThread::msleep(30);
    keyPress({}, QStringLiteral("Backspace"));
    QThread::msleep(80);
    const QJsonObject deleteTopStatus = editorStatus();
    const double deleteTopExpectedY =
        deleteTopStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + deleteTopStatus.value(QStringLiteral("cursorRectY")).toDouble()
        - deleteTopStatus.value(QStringLiteral("scrollViewportHeight")).toDouble() * 2.0 / 3.0;
    const double deleteTopMaxY =
        deleteTopStatus.value(QStringLiteral("scrollContentHeight")).toDouble()
        - deleteTopStatus.value(QStringLiteral("scrollViewportHeight")).toDouble();
    addCheck(checks, details, QStringLiteral("deleteTouchTopAnchorsToLowerThird"),
             deleteTopStatus.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor - 1
                 && deleteTopStatus.value(QStringLiteral("cursorPosition")).toInt()
                    == scrollTopCursor - 1
                 && deleteTopExpectedY > 50.0
                 && std::abs(deleteTopStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - deleteTopExpectedY) < 3.0
                 && deleteTopStatus.value(QStringLiteral("scrollContentY")).toDouble()
                    < deleteTopMaxY,
             QJsonObject{{QStringLiteral("expectedY"), deleteTopExpectedY},
                         {QStringLiteral("maxY"), deleteTopMaxY},
                         {QStringLiteral("status"), deleteTopStatus}});

    // 间歇式：锚定后继续退格不重复触发；重新让光标越过顶边才再次触发。
    const double deleteAnchorY =
        deleteTopStatus.value(QStringLiteral("scrollContentY")).toDouble();
    keyPress({}, QStringLiteral("Backspace"));
    QThread::msleep(60);
    keyPress({}, QStringLiteral("Backspace"));
    QThread::msleep(60);
    const QJsonObject deleteNonTriggerStatus = editorStatus();
    setScrollY(pageMaxY);
    QThread::msleep(30);
    const QJsonObject deleteReCrossStatus = editorStatus();
    keyPress({}, QStringLiteral("Backspace"));
    QThread::msleep(80);
    const QJsonObject deleteReTriggerStatus = editorStatus();
    const double deleteReTriggerExpectedY =
        deleteReTriggerStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + deleteReTriggerStatus.value(QStringLiteral("cursorRectY")).toDouble()
        - deleteReTriggerStatus.value(QStringLiteral("scrollViewportHeight")).toDouble()
            * 2.0 / 3.0;
    addCheck(checks, details, QStringLiteral("deleteTopScrollIsIntermittent"),
             std::abs(deleteNonTriggerStatus.value(QStringLiteral("scrollContentY")).toDouble()
                      - deleteAnchorY) < 1.5
                 && deleteReCrossStatus.value(QStringLiteral("scrollContentY")).toDouble()
                    > deleteAnchorY + 100.0
                 && std::abs(deleteReTriggerStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - deleteReTriggerExpectedY) < 3.0,
             QJsonObject{{QStringLiteral("anchorY"), deleteAnchorY},
                         {QStringLiteral("nonTrigger"), deleteNonTriggerStatus},
                         {QStringLiteral("reCross"), deleteReCrossStatus},
                         {QStringLiteral("reTrigger"), deleteReTriggerStatus},
                         {QStringLiteral("expectedY"), deleteReTriggerExpectedY}});

    // 光标位于文档开头时退格不改变文本：统一检查 early return 不触发
    // 锚定，仅保留 QML 最小可见跟随（光标回到视口内）。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}});
    setScrollY(pageMaxY);
    QThread::msleep(30);
    const QJsonObject startBeforeDelete = editorStatus();
    keyPress({}, QStringLiteral("Backspace"));
    QThread::msleep(80);
    const QJsonObject startAfterDelete = editorStatus();
    addCheck(checks, details, QStringLiteral("deleteAtDocumentStartDoesNotScroll"),
             startAfterDelete.value(QStringLiteral("textLength")).toInt()
                    == startBeforeDelete.value(QStringLiteral("textLength")).toInt()
                 && std::abs(startAfterDelete.value(QStringLiteral("scrollContentY")).toDouble())
                        < 1.5,
             QJsonObject{{QStringLiteral("before"), startBeforeDelete},
                         {QStringLiteral("after"), startAfterDelete}});

    // 带选区删除：光标落在选区起点，越过视口顶边时触发同一锚定。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollSelectionStart},
             {QStringLiteral("end"), scrollSelectionEnd}});
    setScrollY(pageMaxY);
    QThread::msleep(30);
    keyPress({}, QStringLiteral("Delete"));
    QThread::msleep(80);
    const QJsonObject selectionDeleteStatus = editorStatus();
    const double selectionDeleteExpectedY =
        selectionDeleteStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + selectionDeleteStatus.value(QStringLiteral("cursorRectY")).toDouble()
        - selectionDeleteStatus.value(QStringLiteral("scrollViewportHeight")).toDouble()
            * 2.0 / 3.0;
    addCheck(checks, details, QStringLiteral("selectionDeleteAboveTopAnchors"),
             selectionDeleteStatus.value(QStringLiteral("cursorPosition")).toInt()
                    == scrollSelectionStart
                 && selectionDeleteStatus.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor - (scrollSelectionEnd - scrollSelectionStart)
                 && std::abs(selectionDeleteStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - selectionDeleteExpectedY) < 3.0,
             QJsonObject{{QStringLiteral("expectedY"), selectionDeleteExpectedY},
                         {QStringLiteral("status"), selectionDeleteStatus}});

    // 命令面板“删除整行”同样纳入触顶检查。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollTopCursor},
             {QStringLiteral("end"), scrollTopCursor}});
    setScrollY(pageMaxY);
    QThread::msleep(30);
    execute(QStringLiteral("deleteLine"));
    QThread::msleep(80);
    const QJsonObject deleteLineStatus = editorStatus();
    const double deleteLineExpectedY =
        deleteLineStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + deleteLineStatus.value(QStringLiteral("cursorRectY")).toDouble()
        - deleteLineStatus.value(QStringLiteral("scrollViewportHeight")).toDouble() * 2.0 / 3.0;
    addCheck(checks, details, QStringLiteral("deleteLineCommandScrolls"),
             deleteLineStatus.value(QStringLiteral("cursorPosition")).toInt()
                    == scrollTopCursor
                 && deleteLineStatus.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor - scrollLine.size()
                 && std::abs(deleteLineStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - deleteLineExpectedY) < 3.0,
             QJsonObject{{QStringLiteral("expectedY"), deleteLineExpectedY},
                         {QStringLiteral("status"), deleteLineStatus}});

    // 剪切（Ctrl+X 带选区）触发同一检查。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollSelectionStart},
             {QStringLiteral("end"), scrollSelectionEnd}});
    setClipboard(QString());
    setScrollY(pageMaxY);
    QThread::msleep(30);
    keyPress({}, QStringLiteral("X"), false, QStringLiteral("ctrl"));
    QThread::msleep(80);
    const QJsonObject cutStatus = editorStatus();
    const double cutExpectedY =
        cutStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + cutStatus.value(QStringLiteral("cursorRectY")).toDouble()
        - cutStatus.value(QStringLiteral("scrollViewportHeight")).toDouble() * 2.0 / 3.0;
    addCheck(checks, details, QStringLiteral("cutSelectionScrolls"),
             cutStatus.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor - (scrollSelectionEnd - scrollSelectionStart)
                 && std::abs(cutStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - cutExpectedY) < 3.0,
             QJsonObject{{QStringLiteral("expectedY"), cutExpectedY},
                         {QStringLiteral("status"), cutStatus}});

    // Ctrl+Backspace 词删除（TextEdit 原生处理）同样纳入检查。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollTopCursor + 20},
             {QStringLiteral("end"), scrollTopCursor + 20}});
    setScrollY(pageMaxY);
    QThread::msleep(30);
    const QJsonObject wordBeforeStatus = editorStatus();
    keyPress({}, QStringLiteral("Backspace"), false, QStringLiteral("ctrl"));
    QThread::msleep(80);
    const QJsonObject wordDeleteStatus = editorStatus();
    const double wordDeleteExpectedY =
        wordDeleteStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + wordDeleteStatus.value(QStringLiteral("cursorRectY")).toDouble()
        - wordDeleteStatus.value(QStringLiteral("scrollViewportHeight")).toDouble() * 2.0 / 3.0;
    addCheck(checks, details, QStringLiteral("wordDeleteScrolls"),
             wordDeleteStatus.value(QStringLiteral("textLength")).toInt()
                    < wordBeforeStatus.value(QStringLiteral("textLength")).toInt()
                 && std::abs(wordDeleteStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - wordDeleteExpectedY) < 3.0,
             QJsonObject{{QStringLiteral("before"), wordBeforeStatus},
                         {QStringLiteral("expectedY"), wordDeleteExpectedY},
                         {QStringLiteral("after"), wordDeleteStatus}});

    // 撤销/重做与删除/输入共用同一套检查：撤销大段粘贴后光标越过顶边
    // 按删除规则锚定；重做恢复粘贴后光标越过底边按输入规则锚定。
    QString scrollPasteBlock;
    scrollPasteBlock.reserve(1024);
    for (int i = 0; i < 20; ++i) {
        scrollPasteBlock += QStringLiteral("paste-line-%1 abcdefghij klmnopqrstuvwxyz\n")
                                .arg(i, 2, 10, QLatin1Char('0'));
    }
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollTopCursor},
             {QStringLiteral("end"), scrollTopCursor}});
    setClipboard(scrollPasteBlock);
    setScrollY(0);
    QThread::msleep(30);
    keyPress({}, QStringLiteral("V"), false, QStringLiteral("ctrl"));
    QThread::msleep(80);
    const QJsonObject pastedStatus = editorStatus();
    setScrollY(pageMaxY);
    QThread::msleep(30);
    request(QStringLiteral("testUndo"));
    QThread::msleep(80);
    const QJsonObject pasteUndoStatus = editorStatus();
    // 智能整行粘贴为一次撤销；撤销后必须回到粘贴前的 UTF-16 光标，
    // 再按该位置应用删除方向的顶边锚定规则。
    const double pasteUndoExpectedY = qMax(
        0.0, pasteUndoStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
                 + pasteUndoStatus.value(QStringLiteral("cursorRectY")).toDouble()
                 - pasteUndoStatus.value(QStringLiteral("scrollViewportHeight")).toDouble()
                     * 2.0 / 3.0);
    addCheck(checks, details, QStringLiteral("undoJumpsAboveTopAnchorsLikeDelete"),
             pasteUndoStatus.value(QStringLiteral("textLength")).toInt() == scrollEndCursor
                 && pasteUndoStatus.value(QStringLiteral("cursorPosition")).toInt()
                    == scrollTopCursor
                 && pasteUndoStatus.value(QStringLiteral("selectionStart")).toInt()
                    == scrollTopCursor
                 && pasteUndoStatus.value(QStringLiteral("selectionEnd")).toInt()
                    == scrollTopCursor
                 && std::abs(pasteUndoStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - pasteUndoExpectedY) < 3.0
                 && editorText() == scrollText,
             QJsonObject{{QStringLiteral("pasted"), pastedStatus},
                         {QStringLiteral("expectedY"), pasteUndoExpectedY},
                         {QStringLiteral("undo"), pasteUndoStatus}});

    request(QStringLiteral("testRedo"));
    QThread::msleep(80);
    const QJsonObject pasteRedoStatus = editorStatus();
    const double pasteRedoMaxY =
        pasteRedoStatus.value(QStringLiteral("scrollContentHeight")).toDouble()
        - pasteRedoStatus.value(QStringLiteral("scrollViewportHeight")).toDouble();
    // 重做恢复粘贴后光标越过底边，按输入规则锚定到视口上 1/3。
    const double pasteRedoExpectedY = qBound(
        0.0, pasteRedoStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
                 + pasteRedoStatus.value(QStringLiteral("cursorRectY")).toDouble()
                 - pasteRedoStatus.value(QStringLiteral("scrollViewportHeight")).toDouble()
                     / 3.0,
        pasteRedoMaxY);
    addCheck(checks, details, QStringLiteral("redoJumpsBelowBottomAnchorsLikeInput"),
             pasteRedoStatus.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor + scrollPasteBlock.size()
                 && std::abs(pasteRedoStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - pasteRedoExpectedY) < 3.0
                 && pasteRedoStatus.value(QStringLiteral("scrollContentY")).toDouble()
                    < pasteRedoMaxY,
             QJsonObject{{QStringLiteral("expectedY"), pasteRedoExpectedY},
                         {QStringLiteral("maxY"), pasteRedoMaxY},
                         {QStringLiteral("redo"), pasteRedoStatus}});

    // 末尾删除（视图位于 max、光标在上 1/3）：弹性底部缓冲保持视图不动，
    // 光标随删除自然上移，避免 Flickable 钳制导致“触顶失败”。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollEndCursor},
             {QStringLiteral("end"), scrollEndCursor}});
    setScrollY(0);
    QThread::msleep(50);
    keyPress(QStringLiteral("x"));
    QThread::msleep(80);
    const QJsonObject endDeleteBase = editorStatus();
    const double endDeleteBaseY =
        endDeleteBase.value(QStringLiteral("scrollContentY")).toDouble();
    const double endDeleteBaseScreenY =
        endDeleteBase.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + endDeleteBase.value(QStringLiteral("cursorRectY")).toDouble()
        - endDeleteBaseY;
    for (int i = 0; i < 40; ++i) {
        keyPress({}, QStringLiteral("Backspace"));
        QThread::msleep(16);
    }
    QThread::msleep(80);
    const QJsonObject endDeleteHeld = editorStatus();
    const double endDeleteHeldScreenY =
        endDeleteHeld.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + endDeleteHeld.value(QStringLiteral("cursorRectY")).toDouble()
        - endDeleteHeld.value(QStringLiteral("scrollContentY")).toDouble();
    addCheck(checks, details, QStringLiteral("deleteAtEndHoldsViewWhileCursorAscends"),
             std::abs(endDeleteHeld.value(QStringLiteral("scrollContentY")).toDouble()
                      - endDeleteBaseY) < 1.5
                 && endDeleteHeldScreenY < endDeleteBaseScreenY - 10.0
                 && endDeleteHeld.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor + 1 - 40
                 && endDeleteHeld.value(QStringLiteral("cursorPosition")).toInt()
                    == scrollEndCursor + 1 - 40,
             QJsonObject{{QStringLiteral("baseY"), endDeleteBaseY},
                         {QStringLiteral("baseScreenY"), endDeleteBaseScreenY},
                         {QStringLiteral("held"), endDeleteHeld},
                         {QStringLiteral("heldScreenY"), endDeleteHeldScreenY}});
}

} // namespace EditingTests
