#include "suites.h"
#include "support.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QThread>
#include <array>
#include <cmath>

namespace EditingTests {

QString checkHeadingNavigation(QJsonObject &checks, QJsonObject &details)
{
    // --- 标题折叠与标题导航 ---
    const QString headingTree = QStringLiteral(
        "前言\n# A\nA body\n## B\nB body\n### C\nC body\n# D\nD body");
    const int headingA = headingTree.indexOf(QStringLiteral("# A"));
    const int headingB = headingTree.indexOf(QStringLiteral("## B"));
    const int headingC = headingTree.indexOf(QStringLiteral("### C"));
    const int headingD = headingTree.indexOf(QStringLiteral("# D"));
    const int bodyA = headingTree.indexOf(QStringLiteral("A body"));
    const int headingAEnd = headingA + QStringLiteral("# A").size();

    setTextAndSelection(headingTree, bodyA + 2, bodyA + 2);
    const QJsonObject foldedCurrent = execute(QStringLiteral("foldCurrentHeading"));
    const QJsonObject foldedCurrentState = headingFoldState();
    const QJsonArray foldedCurrentVisible =
        foldedCurrentState.value(QStringLiteral("visibleBlockTexts")).toArray();
    addCheck(checks, details, QStringLiteral("foldCurrentHeadingFromBody"),
             foldedCurrent.value(QStringLiteral("executed")).toBool()
                 && foldedCurrent.value(QStringLiteral("text")).toString() == headingTree
                 && foldedCurrent.value(QStringLiteral("cursorPosition")).toInt() == headingAEnd
                 && foldedCurrentVisible
                    == QJsonArray{QStringLiteral("前言"), QStringLiteral("# A"),
                                  QStringLiteral("# D"), QStringLiteral("D body")},
             QJsonObject{{QStringLiteral("command"), foldedCurrent},
                         {QStringLiteral("state"), foldedCurrentState}});

    const QJsonObject unfoldedCurrent = execute(QStringLiteral("unfoldCurrentHeading"));
    const QJsonObject unfoldedCurrentState = headingFoldState();
    addCheck(checks, details, QStringLiteral("unfoldCurrentHeading"),
             unfoldedCurrent.value(QStringLiteral("executed")).toBool()
                 && unfoldedCurrent.value(QStringLiteral("cursorPosition")).toInt() == bodyA + 2
                 && unfoldedCurrentState.value(QStringLiteral("visibleBlockTexts")).toArray()
                    == QJsonArray{QStringLiteral("前言"), QStringLiteral("# A"),
                                  QStringLiteral("A body"), QStringLiteral("## B"),
                                  QStringLiteral("B body"), QStringLiteral("### C"),
                                  QStringLiteral("C body"), QStringLiteral("# D"),
                                  QStringLiteral("D body")},
             QJsonObject{{QStringLiteral("command"), unfoldedCurrent},
                         {QStringLiteral("state"), unfoldedCurrentState}});

    setTextAndSelection(headingTree, bodyA + 2, bodyA + 2);
    const QJsonObject foldedAll = execute(QStringLiteral("foldAllHeadings"));
    const QJsonObject foldedAllState = headingFoldState();
    const QJsonObject foldedAllUi = editorStatus();
    addCheck(checks, details, QStringLiteral("foldAllHeadingsKeepsRootOutline"),
             foldedAll.value(QStringLiteral("executed")).toBool()
                 && foldedAllState.value(QStringLiteral("visibleBlockTexts")).toArray()
                    == QJsonArray{QStringLiteral("前言"), QStringLiteral("# A"),
                                  QStringLiteral("# D")}
                 && foldedAllState.value(QStringLiteral("collapsedHeadingCount")).toInt() == 4
                 && foldedAll.value(QStringLiteral("cursorPosition")).toInt() == headingAEnd,
             foldedAllState);
    addCheck(checks, details, QStringLiteral("headingFoldGutterContract"),
             foldedAllUi.value(QStringLiteral("headingFoldGutterWidth")).toInt() == 16
                 && foldedAllUi.value(QStringLiteral("headingFoldMarkerCount")).toInt() == 2
                 && foldedAllUi.value(QStringLiteral("headingFoldIconSize")).toInt() == 16
                 && foldedAllUi.value(
                        QStringLiteral("headingFoldExpandedIconName")).toString()
                    == QStringLiteral("chevron-down")
                 && foldedAllUi.value(
                        QStringLiteral("headingFoldCollapsedIconName")).toString()
                    == QStringLiteral("chevron-right")
                 && foldedAllUi.value(QStringLiteral("headingFoldExpandedColor")).toString()
                    != foldedAllUi.value(QStringLiteral("themeAccentColor")).toString()
                 && foldedAllUi.value(QStringLiteral("headingFoldCollapsedColor")).toString()
                    == foldedAllUi.value(QStringLiteral("themeAccentColor")).toString(),
             foldedAllUi);

    const QJsonObject restoredAll = execute(QStringLiteral("unfoldAllHeadings"));
    addCheck(checks, details, QStringLiteral("unfoldAllRestoresBodyCursor"),
             restoredAll.value(QStringLiteral("cursorPosition")).toInt() == bodyA + 2,
             restoredAll);
    execute(QStringLiteral("foldAllHeadings"));

    const QJsonObject nextHiddenHeading = execute(QStringLiteral("nextHeading"));
    const QJsonObject afterHiddenNavigation = headingFoldState();
    const QJsonObject firstNavigationHighlight =
        nextHiddenHeading.value(QStringLiteral("headingNavigationHighlight")).toObject();
    addCheck(checks, details, QStringLiteral("headingNavigationRevealsAncestors"),
             nextHiddenHeading.value(QStringLiteral("executed")).toBool()
                 && nextHiddenHeading.value(QStringLiteral("cursorPosition")).toInt() == headingB
                 && firstNavigationHighlight.value(QStringLiteral("start")).toInt() == headingB
                 && firstNavigationHighlight.value(QStringLiteral("end")).toInt()
                    == headingB + QStringLiteral("## B").size()
                 && firstNavigationHighlight.value(QStringLiteral("revision")).toInt() > 0
                 && afterHiddenNavigation.value(QStringLiteral("visibleBlockTexts")).toArray()
                    == QJsonArray{QStringLiteral("前言"), QStringLiteral("# A"),
                                  QStringLiteral("A body"), QStringLiteral("## B"),
                                  QStringLiteral("# D")},
             QJsonObject{{QStringLiteral("command"), nextHiddenHeading},
                         {QStringLiteral("state"), afterHiddenNavigation}});

    const QJsonObject nextNestedHeading = execute(QStringLiteral("nextHeading"));
    const QJsonObject secondNavigationHighlight =
        nextNestedHeading.value(QStringLiteral("headingNavigationHighlight")).toObject();
    const QJsonObject previousNestedHeading = execute(QStringLiteral("previousHeading"));
    const QJsonObject unfoldedAll = execute(QStringLiteral("unfoldAllHeadings"));
    const QJsonObject unfoldedAllState = headingFoldState();
    addCheck(checks, details, QStringLiteral("headingNavigationAndUnfoldAll"),
             nextNestedHeading.value(QStringLiteral("cursorPosition")).toInt() == headingC
                 && secondNavigationHighlight.value(QStringLiteral("start")).toInt() == headingC
                 && secondNavigationHighlight.value(QStringLiteral("revision")).toInt()
                    > firstNavigationHighlight.value(QStringLiteral("revision")).toInt()
                 && previousNestedHeading.value(QStringLiteral("cursorPosition")).toInt()
                    == headingB
                 && unfoldedAll.value(QStringLiteral("executed")).toBool()
                 && unfoldedAllState.value(QStringLiteral("collapsedHeadingCount")).toInt() == 0
                 && unfoldedAllState.value(QStringLiteral("visibleBlockCount")).toInt() == 9,
             QJsonObject{{QStringLiteral("next"), nextNestedHeading},
                         {QStringLiteral("previous"), previousNestedHeading},
                         {QStringLiteral("unfoldAll"), unfoldedAllState}});

    const QString indentedHeadingTree = QStringLiteral(
        "  ## Child title   \nbody\n# Next\nnext");
    const int indentedHeadingStart = indentedHeadingTree.indexOf(QStringLiteral("##"));
    const int indentedHeadingEnd = indentedHeadingTree.indexOf(QStringLiteral("   \n"));
    const int indentedNext = indentedHeadingTree.indexOf(QStringLiteral("# Next"));
    setTextAndSelection(indentedHeadingTree, indentedNext, indentedNext);
    const QJsonObject previousIndented = execute(QStringLiteral("previousHeading"));
    const QJsonObject indentedHighlight =
        previousIndented.value(QStringLiteral("headingNavigationHighlight")).toObject();
    addCheck(checks, details, QStringLiteral("headingNavigationHighlightTrimsWhitespace"),
             indentedHighlight.value(QStringLiteral("start")).toInt() == indentedHeadingStart
                 && indentedHighlight.value(QStringLiteral("end")).toInt()
                    == indentedHeadingEnd,
             previousIndented);

    const QString utf16FoldText = QStringLiteral("# 标题\n甲😀乙\n# 尾");
    const int utf16Body = utf16FoldText.indexOf(QStringLiteral("甲"));
    const int utf16Cursor = utf16Body + QStringLiteral("甲😀").size();
    const int utf16HeadingEnd = QStringLiteral("# 标题").size();
    setTextAndSelection(utf16FoldText, utf16Cursor, utf16Cursor);
    const QJsonObject utf16Folded = execute(QStringLiteral("foldCurrentHeading"));
    const QJsonObject utf16Unfolded = execute(QStringLiteral("unfoldCurrentHeading"));
    addCheck(checks, details, QStringLiteral("headingFoldRestoresUtf16Cursor"),
             utf16Folded.value(QStringLiteral("cursorPosition")).toInt() == utf16HeadingEnd
                 && utf16Unfolded.value(QStringLiteral("cursorPosition")).toInt()
                    == utf16Cursor,
             QJsonObject{{QStringLiteral("folded"), utf16Folded},
                         {QStringLiteral("unfolded"), utf16Unfolded}});

    setTextAndSelection(headingTree, bodyA + 2, bodyA + 2);
    execute(QStringLiteral("foldAllHeadings"));
    const int headingDEnd = headingD + QStringLiteral("# D").size();
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), headingDEnd},
             {QStringLiteral("end"), headingDEnd}});
    const QJsonObject movedBeforeUnfold = execute(QStringLiteral("unfoldAllHeadings"));
    addCheck(checks, details, QStringLiteral("headingFoldDoesNotRestoreAfterCursorMove"),
             movedBeforeUnfold.value(QStringLiteral("cursorPosition")).toInt() == headingDEnd,
             movedBeforeUnfold);

    const QString fencedHeadings = QStringLiteral(
        "# Real\nbody\n```\n# fake\n```\n## Child\nchild body");
    const int realHeading = fencedHeadings.indexOf(QStringLiteral("# Real"));
    const int childHeading = fencedHeadings.indexOf(QStringLiteral("## Child"));
    setTextAndSelection(fencedHeadings, realHeading, realHeading);
    const QJsonObject fencedNext = execute(QStringLiteral("nextHeading"));
    const QJsonObject fencedState = headingFoldState();
    addCheck(checks, details, QStringLiteral("headingStructureIgnoresFencedCode"),
             fencedNext.value(QStringLiteral("cursorPosition")).toInt() == childHeading
                 && fencedState.value(QStringLiteral("headingCount")).toInt() == 2,
             QJsonObject{{QStringLiteral("command"), fencedNext},
                         {QStringLiteral("state"), fencedState}});

    setTextAndSelection(headingTree, bodyA + 1, bodyA + 1);
    execute(QStringLiteral("foldCurrentHeading"));
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), headingA + 3},
             {QStringLiteral("end"), headingA + 3}});
    keyPress(QStringLiteral("!"));
    const QJsonObject editedFoldState = headingFoldState();
    addCheck(checks, details, QStringLiteral("headingFoldSurvivesHeadingEdit"),
             editedFoldState.value(QStringLiteral("collapsedHeadingCount")).toInt() == 1
                 && editedFoldState.value(QStringLiteral("visibleBlockTexts")).toArray()
                    == QJsonArray{QStringLiteral("前言"), QStringLiteral("# A!"),
                                  QStringLiteral("# D"), QStringLiteral("D body")},
             editedFoldState);

    setTextAndSelection(headingTree, headingA, headingA);
    execute(QStringLiteral("foldAllHeadings"));
    const QJsonObject foundHiddenBody = request(
        QStringLiteral("testFindNext"),
        {{QStringLiteral("query"), QStringLiteral("B body")},
         {QStringLiteral("caseSensitive"), true},
         {QStringLiteral("backwards"), false}});
    const QJsonObject foundHiddenState = headingFoldState();
    addCheck(checks, details, QStringLiteral("findRevealsFoldedHeadingBody"),
             foundHiddenBody.value(QStringLiteral("found")).toBool()
                 && foundHiddenBody.value(QStringLiteral("selectionStart")).toInt()
                    == headingTree.indexOf(QStringLiteral("B body"))
                 && foundHiddenState.value(QStringLiteral("visibleBlockTexts")).toArray()
                    .contains(QStringLiteral("B body")),
             QJsonObject{{QStringLiteral("find"), foundHiddenBody},
                         {QStringLiteral("state"), foundHiddenState}});

    setTextAndSelection(headingTree, headingA, headingA);
    const QJsonObject previousBoundary = execute(QStringLiteral("previousHeading"));
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), headingD}, {QStringLiteral("end"), headingD}});
    const QJsonObject nextBoundary = execute(QStringLiteral("nextHeading"));
    const QJsonObject resetFoldState = headingFoldState();
    addCheck(checks, details, QStringLiteral("headingNavigationBoundariesAndSessionReset"),
             previousBoundary.value(QStringLiteral("cursorPosition")).toInt() == headingA
                 && nextBoundary.value(QStringLiteral("cursorPosition")).toInt() == headingD
                 && resetFoldState.value(QStringLiteral("collapsedHeadingCount")).toInt() == 0,
             QJsonObject{{QStringLiteral("previous"), previousBoundary},
                         {QStringLiteral("next"), nextBoundary},
                         {QStringLiteral("state"), resetFoldState}});

    const QString plainDocument = QStringLiteral("普通文本\n没有标题");
    setTextAndSelection(plainDocument, 3, 3);
    const QJsonObject plainFoldAll = execute(QStringLiteral("foldAllHeadings"));
    const QJsonObject plainNext = execute(QStringLiteral("nextHeading"));
    const QJsonObject plainHeadingState = headingFoldState();
    addCheck(checks, details, QStringLiteral("headingCommandsIgnorePlainDocument"),
             plainFoldAll.value(QStringLiteral("text")).toString() == plainDocument
                 && plainNext.value(QStringLiteral("cursorPosition")).toInt() == 3
                 && plainHeadingState.value(QStringLiteral("headingCount")).toInt() == 0
                 && plainHeadingState.value(QStringLiteral("visibleBlockCount")).toInt() == 2,
             QJsonObject{{QStringLiteral("foldAll"), plainFoldAll},
                         {QStringLiteral("next"), plainNext},
                         {QStringLiteral("state"), plainHeadingState}});

    const std::array<std::pair<QString, QString>, 6> headingFeatureShortcuts{{
        {QStringLiteral("foldAllHeadings"), QStringLiteral("Ctrl+M")},
        {QStringLiteral("unfoldAllHeadings"), QStringLiteral("Ctrl+Shift+M")},
        {QStringLiteral("foldCurrentHeading"), QStringLiteral("Ctrl+Shift+[")},
        {QStringLiteral("unfoldCurrentHeading"), QStringLiteral("Ctrl+Shift+]")},
        {QStringLiteral("previousHeading"), QStringLiteral("Ctrl+Up")},
        {QStringLiteral("nextHeading"), QStringLiteral("Ctrl+Down")},
    }};
    bool headingFeatureShortcutDefaults = true;
    QJsonArray headingFeatureShortcutDetails;
    for (const auto &[commandId, expectedShortcut] : headingFeatureShortcuts) {
        const QJsonObject shortcutResponse = request(
            QStringLiteral("testShortcut"), {{QStringLiteral("commandId"), commandId}});
        headingFeatureShortcutDefaults = headingFeatureShortcutDefaults
            && shortcutResponse.value(QStringLiteral("shortcut")).toString()
                == expectedShortcut;
        headingFeatureShortcutDetails.append(shortcutResponse);
    }
    addCheck(checks, details, QStringLiteral("headingFeatureDefaultShortcuts"),
             headingFeatureShortcutDefaults, headingFeatureShortcutDetails);

    // --- 标题跳转的视口 1/3 定位滚动（editing 套件动画已关闭，落位瞬时） ---
    // 滚动在 40ms 布局落定延迟后执行，采样前需等待其完成。
    QString headingScrollText;
    headingScrollText.reserve(8192);
    headingScrollText += QStringLiteral("# A\n");
    for (int i = 0; i < 60; ++i) {
        headingScrollText += QStringLiteral("A 正文 line-%1\n").arg(i);
    }
    headingScrollText += QStringLiteral("# B\n");
    for (int i = 0; i < 60; ++i) {
        headingScrollText += QStringLiteral("B 正文 line-%1\n").arg(i);
    }
    headingScrollText += QStringLiteral("# C\n");
    const int headingScrollB = headingScrollText.indexOf(QStringLiteral("# B"));
    const int headingScrollC = headingScrollText.indexOf(QStringLiteral("# C"));
    setTextAndSelection(headingScrollText, 0, 0);
    setScrollY(0);
    QThread::msleep(50);
    const QJsonObject headingJumped = execute(QStringLiteral("nextHeading"));
    QThread::msleep(120);
    const QJsonObject headingScrolled = editorStatus();
    const double headingAnchorY =
        headingScrolled.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + headingScrolled.value(QStringLiteral("cursorRectY")).toDouble()
        - headingScrolled.value(QStringLiteral("scrollViewportHeight")).toDouble() / 3.0;
    const double headingScrollMaxY =
        headingScrolled.value(QStringLiteral("scrollContentHeight")).toDouble()
        - headingScrolled.value(QStringLiteral("scrollViewportHeight")).toDouble();
    addCheck(checks, details, QStringLiteral("headingNavigationScrollAnchorsToUpperThird"),
             headingJumped.value(QStringLiteral("executed")).toBool()
                 && headingScrolled.value(QStringLiteral("cursorPosition")).toInt()
                    == headingScrollB
                 && std::abs(headingScrolled.value(QStringLiteral("scrollContentY")).toDouble()
                             - headingAnchorY) < 3.0
                 && headingScrolled.value(QStringLiteral("scrollContentY")).toDouble() > 0.0
                 && headingScrolled.value(QStringLiteral("scrollContentY")).toDouble()
                    < headingScrollMaxY,
             QJsonObject{{QStringLiteral("targetB"), headingScrollB},
                         {QStringLiteral("anchorY"), headingAnchorY},
                         {QStringLiteral("maxY"), headingScrollMaxY},
                         {QStringLiteral("command"), headingJumped},
                         {QStringLiteral("status"), headingScrolled}});

    // 回跳首个标题：目标在文档顶部，滚动钳到 0。
    execute(QStringLiteral("previousHeading"));
    QThread::msleep(120);
    const QJsonObject headingClampedTop = editorStatus();
    addCheck(checks, details, QStringLiteral("headingNavigationScrollClampsToTop"),
             headingClampedTop.value(QStringLiteral("cursorPosition")).toInt() == 0
                 && std::abs(headingClampedTop.value(QStringLiteral("scrollContentY")).toDouble())
                        < 1.5,
             headingClampedTop);

    // 折叠状态下跳转：目标从折叠祖先中展开后，滚动在布局落定后把标题锚到 1/3。
    QString nestedScrollText;
    nestedScrollText.reserve(8192);
    nestedScrollText += QStringLiteral("# A\n");
    for (int i = 0; i < 40; ++i) {
        nestedScrollText += QStringLiteral("A 正文 line-%1\n").arg(i);
    }
    nestedScrollText += QStringLiteral("## B\n");
    for (int i = 0; i < 40; ++i) {
        nestedScrollText += QStringLiteral("B 正文 line-%1\n").arg(i);
    }
    nestedScrollText += QStringLiteral("### C\n");
    const int nestedScrollB = nestedScrollText.indexOf(QStringLiteral("## B"));
    setTextAndSelection(nestedScrollText, 0, 0);
    execute(QStringLiteral("foldAllHeadings"));
    execute(QStringLiteral("nextHeading"));
    QThread::msleep(120);
    const QJsonObject headingRevealScrolled = editorStatus();
    const double revealAnchorY =
        headingRevealScrolled.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + headingRevealScrolled.value(QStringLiteral("cursorRectY")).toDouble()
        - headingRevealScrolled.value(QStringLiteral("scrollViewportHeight")).toDouble() / 3.0;
    addCheck(checks, details, QStringLiteral("headingNavigationScrollAfterReveal"),
             headingRevealScrolled.value(QStringLiteral("cursorPosition")).toInt()
                    == nestedScrollB
                 && std::abs(headingRevealScrolled.value(QStringLiteral("scrollContentY")).toDouble()
                             - revealAnchorY) < 3.0
                 && headingRevealScrolled.value(QStringLiteral("scrollContentY")).toDouble() > 0.0,
             QJsonObject{{QStringLiteral("targetB"), nestedScrollB},
                         {QStringLiteral("anchorY"), revealAnchorY},
                         {QStringLiteral("status"), headingRevealScrolled}});

    // 已处于边界无跳转时不滚动，视口保持原位置。
    setTextAndSelection(headingScrollText, headingScrollC, headingScrollC);
    setScrollY(0);
    QThread::msleep(50);
    execute(QStringLiteral("nextHeading"));
    QThread::msleep(120);
    const QJsonObject headingBoundary = editorStatus();
    addCheck(checks, details, QStringLiteral("headingNavigationNoJumpKeepsViewport"),
             headingBoundary.value(QStringLiteral("cursorPosition")).toInt() == headingScrollC
                 && std::abs(headingBoundary.value(QStringLiteral("scrollContentY")).toDouble())
                        < 1.5,
             headingBoundary);

    setTextAndSelection(QStringLiteral("one\ntwo"), 0, 7);
    execute(QStringLiteral("toggleList"));
    const bool listOn = editorText() == QStringLiteral("- one\n- two");
    const QJsonObject listOff = execute(QStringLiteral("toggleList"));
    addCheck(checks, details, QStringLiteral("toggleList"),
             listOn && editorText() == QStringLiteral("one\ntwo"), listOff);
    return headingScrollText;
}

} // namespace EditingTests
