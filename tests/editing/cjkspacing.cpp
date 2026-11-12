#include "suites.h"
#include "support.h"

#include <QJsonObject>
#include <QThread>

namespace EditingTests {

void checkSpacingAndIme(QJsonObject &checks, QJsonObject &details)
{
    // --- CJK Fix: Real-time Spacing (LIVE-001..009) ---
    cjkExpect(checks, details, QStringLiteral("liveCjkThenAscii"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key A"),
              keyAction(QStringLiteral("A")), QStringLiteral("中文 A"), 4);
    cjkExpect(checks, details, QStringLiteral("liveAsciiThenCjk"),
              QStringLiteral("ABC"), 3, 3, QStringLiteral("key 中"),
              keyAction(QStringLiteral("中")), QStringLiteral("ABC 中"), 5);
    cjkExpect(checks, details, QStringLiteral("liveSingleLineStartNumberThenCjk"),
              QStringLiteral("1"), 1, 1, QStringLiteral("key 中"),
              keyAction(QStringLiteral("中")), QStringLiteral("1 中"), 3);
    cjkExpect(checks, details, QStringLiteral("liveLineStartNumberThenCjk"),
              QStringLiteral("123"), 3, 3, QStringLiteral("key 中"),
              keyAction(QStringLiteral("中")), QStringLiteral("123 中"), 5);
    const QJsonObject lineStartNumberUndo = request(QStringLiteral("testUndo"));
    addCheck(checks, details, QStringLiteral("liveLineStartNumberThenCjkUndo"),
             lineStartNumberUndo.value(QStringLiteral("text")).toString()
                    == QStringLiteral("123")
                 && lineStartNumberUndo.value(QStringLiteral("cursorPosition")).toInt() == 3,
             lineStartNumberUndo);
    cjkExpect(checks, details, QStringLiteral("liveLaterLineStartNumberThenCjkIme"),
              QStringLiteral("上一行\n123"), 7, 7, QStringLiteral("IME commit 中文"),
              [] { return inputMethodCommit(QStringLiteral("中文")); },
              QStringLiteral("上一行\n123 中文"), 10);
    cjkExpect(checks, details, QStringLiteral("liveOrderedListDotPrefixPreserved"),
              QStringLiteral("1"), 1, 1, QStringLiteral("key . + space + 中"),
              [] {
                  keyPress(QStringLiteral("."), QStringLiteral("."));
                  keyPress(QStringLiteral(" "), QStringLiteral(" "));
                  return keyPress(QStringLiteral("中"), QStringLiteral("中"));
              },
              QStringLiteral("1. 中"), 4);
    cjkExpect(checks, details, QStringLiteral("liveOrderedListParenPrefixPreserved"),
              QStringLiteral("1"), 1, 1, QStringLiteral("key ) + space + 中"),
              [] {
                  keyPress(QStringLiteral(")"), QStringLiteral(")"));
                  keyPress(QStringLiteral(" "), QStringLiteral(" "));
                  return keyPress(QStringLiteral("中"), QStringLiteral("中"));
              },
              QStringLiteral("1) 中"), 4);
    cjkExpect(checks, details, QStringLiteral("liveMiddleInsert"),
              QStringLiteral("中文"), 1, 1, QStringLiteral("key A"),
              keyAction(QStringLiteral("A")), QStringLiteral("中 A 文"), 3);
    cjkExpect(checks, details, QStringLiteral("liveInlineCodeMiddle"),
              QStringLiteral("`中文`"), 2, 2, QStringLiteral("key A inside inline code"),
              keyAction(QStringLiteral("A")), QStringLiteral("`中A文`"), 3);
    cjkExpect(checks, details, QStringLiteral("liveBacktickOpeningSpacing"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key ` after CJK"),
              keyAction(QStringLiteral("`")), QStringLiteral("中文 ``"), 4);
    cjkExpect(checks, details, QStringLiteral("liveDollarOpeningSpacing"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key $ after CJK"),
              keyAction(QStringLiteral("$")), QStringLiteral("中文 $"), 4);

    setTextAndSelection(QStringLiteral("中文"), 0, 0);
    keyPress(QStringLiteral("A"), QStringLiteral("A"));
    keyPress(QStringLiteral("B"), QStringLiteral("B"));
    keyPress(QStringLiteral("C"), QStringLiteral("C"));
    QThread::msleep(30);
    const QString liveAsciiRunAfterCjkText = editorText();
    const QJsonObject liveAsciiRunAfterCjkStatus = editorStatus();
    addCjkCheck(checks, details, QStringLiteral("liveAsciiRunAfterCjk"),
                liveAsciiRunAfterCjkText == QStringLiteral("ABC 中文")
                    && liveAsciiRunAfterCjkStatus.value(
                           QStringLiteral("cursorPosition")).toInt() == 3,
                QStringLiteral("中文@0 + A B C"),
                QStringLiteral("typing ABC before 中文"),
                QStringLiteral("ABC 中文"), liveAsciiRunAfterCjkText,
                liveAsciiRunAfterCjkStatus);

    setTextAndSelection(QStringLiteral("中文"), 1, 1);
    keyPress(QStringLiteral("A"), QStringLiteral("A"));
    keyPress(QStringLiteral("B"), QStringLiteral("B"));
    QThread::msleep(30);
    const QString liveAsciiRunMiddleCjkText = editorText();
    const QJsonObject liveAsciiRunMiddleCjkStatus = editorStatus();
    addCjkCheck(checks, details, QStringLiteral("liveAsciiRunMiddleCjk"),
                liveAsciiRunMiddleCjkText == QStringLiteral("中 AB 文")
                    && liveAsciiRunMiddleCjkStatus.value(
                           QStringLiteral("cursorPosition")).toInt() == 4,
                QStringLiteral("中|文 + A B"),
                QStringLiteral("typing AB between 中文"),
                QStringLiteral("中 AB 文"), liveAsciiRunMiddleCjkText,
                liveAsciiRunMiddleCjkStatus);
    cjkExpect(checks, details, QStringLiteral("liveInlineFormulaMiddle"),
              QStringLiteral("$中文$"), 2, 2, QStringLiteral("key A inside inline formula"),
              keyAction(QStringLiteral("A")), QStringLiteral("$中A文$"), 3);

    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    keyPress(QStringLiteral("`"), QStringLiteral("`"));
    keyPress(QStringLiteral("A"), QStringLiteral("A"));
    keyPress(QStringLiteral("`"), QStringLiteral("`"));
    keyPress(QStringLiteral("文"), QStringLiteral("文"));
    QThread::msleep(30);
    const QString liveInlineCodeSpacingText = editorText();
    const QJsonObject liveInlineCodeSpacingStatus = editorStatus();
    addCjkCheck(checks, details, QStringLiteral("liveInlineCodeSpacing"),
                liveInlineCodeSpacingText == QStringLiteral("中文 `A` 文"),
                QStringLiteral("中文 + ` + A + ` + 文"),
                QStringLiteral("typing 中文`A`文"),
                QStringLiteral("中文 `A` 文"), liveInlineCodeSpacingText,
                liveInlineCodeSpacingStatus);

    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    keyPress(QStringLiteral("$"), QStringLiteral("$"));
    keyPress(QStringLiteral("x"), QStringLiteral("x"));
    keyPress(QStringLiteral("$"), QStringLiteral("$"));
    keyPress(QStringLiteral("文"), QStringLiteral("文"));
    QThread::msleep(30);
    const QString liveInlineFormulaSpacingText = editorText();
    const QJsonObject liveInlineFormulaSpacingStatus = editorStatus();
    addCjkCheck(checks, details, QStringLiteral("liveInlineFormulaSpacing"),
                liveInlineFormulaSpacingText == QStringLiteral("中文 $x$ 文"),
                QStringLiteral("中文 + $ + x + $ + 文"),
                QStringLiteral("typing 中文$x$文"),
                QStringLiteral("中文 $x$ 文"), liveInlineFormulaSpacingText,
                liveInlineFormulaSpacingStatus);

    setTextAndSelection(QStringLiteral("```\n$$\n```\n"), 11, 11);
    keyPress(QStringLiteral("中"), QStringLiteral("中"));
    keyPress(QStringLiteral("文"), QStringLiteral("文"));
    keyPress(QStringLiteral("A"), QStringLiteral("A"));
    QThread::msleep(30);
    const QString liveFenceWithFormulaText = editorText();
    const QJsonObject liveFenceWithFormulaStatus = editorStatus();
    addCjkCheck(checks, details, QStringLiteral("liveFenceWithFormulaMarker"),
                liveFenceWithFormulaText == QStringLiteral("```\n$$\n```\n中文 A"),
                QStringLiteral("```\\n$$\\n```\\n + 中文A"),
                QStringLiteral("typing 中文A after fence containing $$"),
                QStringLiteral("```\n$$\n```\n中文 A"), liveFenceWithFormulaText,
                liveFenceWithFormulaStatus);

    setTextAndSelection(QStringLiteral("$$\n```\n$$\n"), 11, 11);
    keyPress(QStringLiteral("中"), QStringLiteral("中"));
    keyPress(QStringLiteral("文"), QStringLiteral("文"));
    keyPress(QStringLiteral("A"), QStringLiteral("A"));
    QThread::msleep(30);
    const QString liveFormulaWithFenceText = editorText();
    const QJsonObject liveFormulaWithFenceStatus = editorStatus();
    addCjkCheck(checks, details, QStringLiteral("liveFormulaWithFenceMarker"),
                liveFormulaWithFenceText == QStringLiteral("$$\n```\n$$\n中文 A"),
                QStringLiteral("$$\\n```\\n$$\\n + 中文A"),
                QStringLiteral("typing 中文A after formula containing ```"),
                QStringLiteral("$$\n```\n$$\n中文 A"), liveFormulaWithFenceText,
                liveFormulaWithFenceStatus);

    // --- 悬空右边界空格回收：ASCII 自动空格后输入 CJK ---
    {
        setTextAndSelection(QStringLiteral("中文中文"), 2, 2);
        keyPress(QStringLiteral("a"), QStringLiteral("a"));
        keyPress(QStringLiteral("b"), QStringLiteral("b"));
        keyPress(QStringLiteral("c"), QStringLiteral("c"));
        inputMethodCommit(QStringLiteral("新的中文"));
        QThread::msleep(30);
        const QString trailingCjkText = editorText();
        const QJsonObject trailingCjkStatus = editorStatus();
        addCjkCheck(checks, details, QStringLiteral("liveAsciiTrailingCjkCommit"),
                    trailingCjkText == QStringLiteral("中文 abc 新的中文中文")
                        && trailingCjkStatus.value(QStringLiteral("cursorPosition")).toInt() == 11,
                    QStringLiteral("中文中文@2 + abc + IME 新的中文"),
                    QStringLiteral("IME commit CJK after ASCII run"),
                    QStringLiteral("中文 abc 新的中文中文"), trailingCjkText,
                    trailingCjkStatus);

        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString trailingCjkUndone = editorText();
        addCheck(checks, details, QStringLiteral("liveAsciiTrailingCjkUndo"),
                 trailingCjkUndone == QStringLiteral("中文 abc 中文"),
                 QJsonObject{{QStringLiteral("undone"), trailingCjkUndone}});
    }

    setTextAndSelection(QStringLiteral("中文 abc 中文"), 6, 6);
    inputMethodCommit(QStringLiteral("新"));
    QThread::msleep(30);
    const QString singleCjkText = editorText();
    const QJsonObject singleCjkStatus = editorStatus();
    addCjkCheck(checks, details, QStringLiteral("liveTrailingSpaceSingleCjkCommit"),
                singleCjkText == QStringLiteral("中文 abc 新中文")
                    && singleCjkStatus.value(QStringLiteral("cursorPosition")).toInt() == 8,
                QStringLiteral("中文 abc 中文@6 + IME 新"),
                QStringLiteral("IME commit single CJK before trailing space"),
                QStringLiteral("中文 abc 新中文"), singleCjkText, singleCjkStatus);

    setTextAndSelection(QStringLiteral("中文 abc 中文"), 6, 6);
    inputMethodCommit(QStringLiteral("def"));
    QThread::msleep(30);
    const QString asciiCommitText = editorText();
    const QJsonObject asciiCommitStatus = editorStatus();
    addCjkCheck(checks, details, QStringLiteral("liveTrailingSpaceAsciiKeeps"),
                asciiCommitText == QStringLiteral("中文 abcdef 中文")
                    && asciiCommitStatus.value(QStringLiteral("cursorPosition")).toInt() == 9,
                QStringLiteral("中文 abc 中文@6 + IME def"),
                QStringLiteral("IME commit ASCII before trailing space"),
                QStringLiteral("中文 abcdef 中文"), asciiCommitText, asciiCommitStatus);

    setTextAndSelection(QStringLiteral("中文中文"), 2, 2);
    keyPress(QStringLiteral("a"), QStringLiteral("a"));
    keyPress(QStringLiteral("b"), QStringLiteral("b"));
    keyPress(QStringLiteral("c"), QStringLiteral("c"));
    inputMethodCommit(QStringLiteral("新的中文"));
    QThread::msleep(30);
    keyPress(QStringLiteral("d"), QStringLiteral("d"));
    QThread::msleep(30);
    const QString nextAsciiText = editorText();
    const QJsonObject nextAsciiStatus = editorStatus();
    addCjkCheck(checks, details, QStringLiteral("liveTrailingSpaceNextAscii"),
                nextAsciiText == QStringLiteral("中文 abc 新的中文 d 中文")
                    && nextAsciiStatus.value(QStringLiteral("cursorPosition")).toInt() == 13,
                QStringLiteral("中文中文@2 + abc + IME 新的中文 + d"),
                QStringLiteral("ASCII after CJK-then-CJK merge"),
                QStringLiteral("中文 abc 新的中文 d 中文"), nextAsciiText, nextAsciiStatus);

    setTextAndSelection(QStringLiteral("中文 abc 123"), 6, 6);
    inputMethodCommit(QStringLiteral("新"));
    QThread::msleep(30);
    const QString beforeAsciiText = editorText();
    const QJsonObject beforeAsciiStatus = editorStatus();
    addCjkCheck(checks, details, QStringLiteral("liveTrailingSpaceBeforeAsciiNoRemove"),
                beforeAsciiText == QStringLiteral("中文 abc 新 123")
                    && beforeAsciiStatus.value(QStringLiteral("cursorPosition")).toInt() == 8,
                QStringLiteral("中文 abc 123@6 + IME 新"),
                QStringLiteral("IME commit CJK before ASCII content"),
                QStringLiteral("中文 abc 新 123"), beforeAsciiText, beforeAsciiStatus);

    // --- CJK Fix: Selection Wrapping via KeyPress (WRAP-001..007) ---
    cjkExpect(checks, details, QStringLiteral("wrapKeyParen"),
              QStringLiteral("中文"), 0, 2, QStringLiteral("key ( on CJK selection"),
              keyAction(QStringLiteral("(")), QStringLiteral("（中文）"), 3, 1, 3);
    cjkExpect(checks, details, QStringLiteral("wrapKeyBracket"),
              QStringLiteral("中文"), 0, 2, QStringLiteral("key [ on CJK selection"),
              keyAction(QStringLiteral("[")), QStringLiteral("【中文】"), 3, 1, 3);
    cjkExpect(checks, details, QStringLiteral("wrapKeyDoubleQuote"),
              QStringLiteral("中文"), 0, 2, QStringLiteral("key \" on CJK selection"),
              keyAction(QStringLiteral("\"")), QStringLiteral("“中文”"), 3, 1, 3);
    cjkExpect(checks, details, QStringLiteral("wrapKeySingleQuote"),
              QStringLiteral("中文"), 0, 2, QStringLiteral("key ' on CJK selection"),
              keyAction(QStringLiteral("'")), QStringLiteral("‘中文’"), 3, 1, 3);
    cjkExpect(checks, details, QStringLiteral("wrapKeyLessThan"),
              QStringLiteral("中文"), 0, 2, QStringLiteral("key < on CJK selection"),
              keyAction(QStringLiteral("<")), QStringLiteral("<中文>"), 3, 1, 3);
    cjkExpect(checks, details, QStringLiteral("wrapKeyAsciiParen"),
              QStringLiteral("ABC"), 0, 3, QStringLiteral("key ( on ASCII selection"),
              keyAction(QStringLiteral("(")), QStringLiteral("(ABC)"), 4, 1, 4);
    cjkExpect(checks, details, QStringLiteral("wrapKeyMixedBracket"),
              QStringLiteral("A中文B"), 0, 4, QStringLiteral("key [ on mixed selection"),
              keyAction(QStringLiteral("[")), QStringLiteral("【A中文B】"), 5, 1, 5);

    // --- 保护区内选区包裹必须退回 ASCII（最终清单 §4/§D） ---
    cjkExpect(checks, details, QStringLiteral("wrapProtectedInlineCodeKeyParen"),
              QStringLiteral("`中文`"), 1, 3, QStringLiteral("key ( inside inline code"),
              keyAction(QStringLiteral("(")), QStringLiteral("`(中文)`"), 4, 2, 4);
    cjkExpect(checks, details, QStringLiteral("wrapProtectedInlineFormulaKeyParen"),
              QStringLiteral("$中文$"), 1, 3, QStringLiteral("key ( inside inline formula"),
              keyAction(QStringLiteral("(")), QStringLiteral("$(中文)$"), 4, 2, 4);
    cjkExpect(checks, details, QStringLiteral("wrapProtectedInlineCodeImeParen"),
              QStringLiteral("`中文`"), 1, 3, QStringLiteral("IME ( inside inline code"),
              [] { return inputMethodCommit(QStringLiteral("(")); },
              QStringLiteral("`(中文)`"), 4, 2, 4);
    cjkExpect(checks, details, QStringLiteral("wrapProtectedInlineFormulaImeParen"),
              QStringLiteral("$中文$"), 1, 3, QStringLiteral("IME ( inside inline formula"),
              [] { return inputMethodCommit(QStringLiteral("(")); },
              QStringLiteral("$(中文)$"), 4, 2, 4);

    // --- CJK Fix: Alt+F (FORMAT-001..015) ---
    formatExpect(checks, details, QStringLiteral("formatCurrentLineOnly"),
                 QStringLiteral("第一行ABC\n第二行"), 0, 0, 0,
                 QStringLiteral("第一行 ABC\n第二行"), 0);
    formatExpect(checks, details, QStringLiteral("formatSelectionInsideLine"),
                 QStringLiteral("AA中文B\nCC"), 2, 5, 5,
                 QStringLiteral("AA中文 B\nCC"), 6, 2, 6);
    formatExpect(checks, details, QStringLiteral("formatInlineContentSelection"),
                 QStringLiteral("`中文ABC`"), 1, 7, 7,
                 QStringLiteral("`中文ABC`"), 7, 1, 7);
    formatExpect(checks, details, QStringLiteral("formatCrossLeftDelimiter"),
                 QStringLiteral("中文`code`文"), 1, 6, 6,
                 QStringLiteral("中文 `code`文"), 7, 1, 7);
    formatExpect(checks, details, QStringLiteral("formatCrossRightDelimiter"),
                 QStringLiteral("中文`code`文"), 4, 9, 9,
                 QStringLiteral("中文`code` 文"), 10, 4, 10);
    formatExpect(checks, details, QStringLiteral("formatFullInlineSpan"),
                 QStringLiteral("中文`code`文"), 1, 9, 9,
                 QStringLiteral("中文 `code` 文"), 11, 1, 11);
    formatExpect(checks, details, QStringLiteral("formatFenceSelection"),
                 QStringLiteral("```\n中文ABC\n```"), 4, 10, 10,
                 QStringLiteral("```\n中文ABC\n```"), 10, 4, 10);
    formatExpect(checks, details, QStringLiteral("formatBlockFormulaSelection"),
                 QStringLiteral("$$\n中文ABC\n$$"), 3, 9, 9,
                 QStringLiteral("$$\n中文ABC\n$$"), 9, 3, 9);
    formatExpect(checks, details, QStringLiteral("formatSingleLineFormulaNextLine"),
                 QStringLiteral("$$x+1$$\n中文ABC"), 0, 13, 13,
                 QStringLiteral("$$x+1$$\n中文 ABC"), 14, 0, 14);
    formatExpect(checks, details, QStringLiteral("formatInvalidFenceTrailing"),
                 QStringLiteral("```js\n中文ABC\n```oops\n后续ABC\n```"), 0, 0, 0,
                 QStringLiteral("```js\n中文ABC\n```oops\n后续ABC\n```"), 0);
    formatExpect(checks, details, QStringLiteral("formatForwardSelection"),
                 QStringLiteral("前中文ABC后"), 1, 6, 6,
                 QStringLiteral("前中文 ABC后"), 7, 1, 7);
    formatExpect(checks, details, QStringLiteral("formatReverseSelection"),
                 QStringLiteral("前中文ABC后"), 1, 6, 1,
                 QStringLiteral("前中文 ABC后"), 1, 1, 7);
    formatExpect(checks, details, QStringLiteral("formatCollapsedCursor"),
                 QStringLiteral("中文ABC"), 3, 3, 3,
                 QStringLiteral("中文 ABC"), 4);

    setTextAndSelection(QStringLiteral("中文 ABC"), 3, 3, 3);
    execute(QStringLiteral("formatSpacing"));
    const QString formatNoChangeText = editorText();
    const QJsonObject formatNoChangeStatus = editorStatus();
    request(QStringLiteral("testUndo"));
    const QString formatNoChangeAfterUndo = editorText();
    addCjkCheck(checks, details, QStringLiteral("formatNoChange"),
                formatNoChangeText == QStringLiteral("中文 ABC")
                    && formatNoChangeAfterUndo == QStringLiteral("中文 ABC")
                    && formatNoChangeStatus.value(QStringLiteral("cursorPosition")).toInt() == 3,
                QStringLiteral("中文 ABC"), QStringLiteral("Alt+F then Undo"),
                QStringLiteral("中文 ABC / 中文 ABC"), formatNoChangeText,
                formatNoChangeStatus);

    setTextAndSelection(QStringLiteral("中文ABC中文123"), 0, 0, 0);
    execute(QStringLiteral("formatSpacing"));
    const QString formatMultiText = editorText();
    const QJsonObject formatMultiStatus = editorStatus();
    const QJsonObject formatUndo = request(QStringLiteral("testUndo"));
    const QString formatMultiAfterUndo = formatUndo.value(QStringLiteral("text")).toString();
    const QJsonObject formatRedo = request(QStringLiteral("testRedo"));
    const QString formatMultiAfterRedo = formatRedo.value(QStringLiteral("text")).toString();
    addCjkCheck(checks, details, QStringLiteral("formatMultiInsertionsUndoRedo"),
                formatMultiText == QStringLiteral("中文 ABC 中文 123")
                    && formatMultiAfterUndo == QStringLiteral("中文ABC中文123")
                    && formatMultiAfterRedo == QStringLiteral("中文 ABC 中文 123"),
                QStringLiteral("中文ABC中文123"), QStringLiteral("Alt+F / Undo / Redo"),
                QStringLiteral("中文 ABC 中文 123 → 中文ABC中文123 → 中文 ABC 中文 123"),
                formatMultiText, formatMultiStatus);

    // --- 未闭合行内分隔符：Alt+F 整体不操作（最终清单 §5/用例二） ---
    formatExpect(checks, details, QStringLiteral("formatUnclosedFormulaNoOp"),
                 QStringLiteral("末$中文ABC"), 0, 0, 0,
                 QStringLiteral("末$中文ABC"), 0);
    formatExpect(checks, details, QStringLiteral("formatUnclosedBacktickNoOp"),
                 QStringLiteral("末`中文ABC"), 0, 0, 0,
                 QStringLiteral("末`中文ABC"), 0);

    // --- 验收用例一：反向、多行、跨保护区的选区格式化 ---
    {
        const QString caseOneInput = QStringLiteral(
            "前A中文ABC\n"
            "前`中A文`后\n"
            "前$中A文$后\n"
            "```js\n"
            "$$\n"
            "中文ABC\n"
            "```oops\n"
            "ABC中文\n"
            "```\n"
            "$$\n"
            "```\n"
            "中文ABC\n"
            "$$\n"
            "$$x中文A$$\n"
            "普通한글ABC123结束\n"
            "尾ABC中文Z外");
        const QString caseOneExpected = QStringLiteral(
            "前A中文 ABC\n"
            "前 `中A文` 后\n"
            "前 $中A文$ 后\n"
            "```js\n"
            "$$\n"
            "中文ABC\n"
            "```oops\n"
            "ABC中文\n"
            "```\n"
            "$$\n"
            "```\n"
            "中文ABC\n"
            "$$\n"
            "$$x中文A$$\n"
            "普通한글 ABC123 结束\n"
            "尾 ABC中文Z外");
        const int selectionStart = caseOneInput.indexOf(QStringLiteral("前A")) + 2;
        const int selectionEnd = caseOneInput.indexOf(QStringLiteral("尾ABC")) + 4;
        setTextAndSelection(caseOneInput, selectionStart, selectionEnd, selectionStart);
        execute(QStringLiteral("formatSpacing"));
        const QString caseOneActual = editorText();
        const QJsonObject caseOneStatus = editorStatus();
        const int expectedEnd = selectionEnd
            + (caseOneExpected.size() - caseOneInput.size());
        const bool caseOneTextOk = caseOneActual == caseOneExpected;
        const bool caseOneSelectionOk =
            caseOneStatus.value(QStringLiteral("selectionStart")).toInt() == selectionStart
            && caseOneStatus.value(QStringLiteral("selectionEnd")).toInt() == expectedEnd
            && caseOneStatus.value(QStringLiteral("cursorPosition")).toInt() == selectionStart;
        keyPress({}, QStringLiteral("Z"), false, QStringLiteral("ctrl"));
        const QString caseOneUndoText = editorText();
        const QJsonObject caseOneUndoStatus = editorStatus();
        const bool caseOneUndoOk = caseOneUndoText == caseOneInput
            && caseOneUndoStatus.value(QStringLiteral("selectionStart")).toInt()
                == selectionStart
            && caseOneUndoStatus.value(QStringLiteral("selectionEnd")).toInt()
                == selectionEnd
            && caseOneUndoStatus.value(QStringLiteral("cursorPosition")).toInt()
                == selectionStart;
        const QJsonObject caseOneRedo = request(QStringLiteral("testRedo"));
        const bool caseOneRedoOk =
            caseOneRedo.value(QStringLiteral("text")).toString() == caseOneExpected;
        addCjkCheck(checks, details, QStringLiteral("acceptanceCaseOne"),
                    caseOneTextOk && caseOneSelectionOk && caseOneUndoOk && caseOneRedoOk,
                    QStringLiteral("反向多行跨保护区选区"), QStringLiteral("Alt+F / Undo / Redo"),
                    caseOneExpected, caseOneActual,
                    QJsonObject{
                        {QStringLiteral("status"), caseOneStatus},
                        {QStringLiteral("undoStatus"), caseOneUndoStatus},
                        {QStringLiteral("undoOk"), caseOneUndoOk},
                        {QStringLiteral("redoOk"), caseOneRedoOk},
                    });
    }

    // --- 验收用例二：整行格式化（未闭合公式段不操作） ---
    {
        const QString caseTwoInput = QStringLiteral(
            "上一行ABC中文\n"
            "$$x+1$$\n"
            "甲A1乙，B2丙-日C3前`中A文`后$x中A$末$中文ABC\n"
            "下一行中文ABC");
        const QString caseTwoExpected = QStringLiteral(
            "上一行ABC中文\n"
            "$$x+1$$\n"
            "甲 A1 乙，B2 丙-日 C3 前 `中A文` 后 $x中A$ 末$中文ABC\n"
            "下一行中文ABC");
        const int lineThreeStart = caseTwoInput.indexOf(QStringLiteral("甲"));
        setTextAndSelection(caseTwoInput, lineThreeStart, lineThreeStart, lineThreeStart);
        execute(QStringLiteral("formatSpacing"));
        const QString caseTwoActual = editorText();
        const QJsonObject caseTwoStatus = editorStatus();
        const bool caseTwoTextOk = caseTwoActual == caseTwoExpected;
        const bool caseTwoCursorOk =
            caseTwoStatus.value(QStringLiteral("cursorPosition")).toInt() == lineThreeStart;
        const QJsonObject caseTwoUndo = request(QStringLiteral("testUndo"));
        const bool caseTwoUndoOk =
            caseTwoUndo.value(QStringLiteral("text")).toString() == caseTwoInput;
        addCjkCheck(checks, details, QStringLiteral("acceptanceCaseTwo"),
                    caseTwoTextOk && caseTwoCursorOk && caseTwoUndoOk,
                    QStringLiteral("整行格式化含未闭合公式"), QStringLiteral("Alt+F / Undo"),
                    caseTwoExpected, caseTwoActual,
                    QJsonObject{
                        {QStringLiteral("status"), caseTwoStatus},
                        {QStringLiteral("undoOk"), caseTwoUndoOk},
                    });
    }

    // --- CJK Fix: IME (IME-001..008) ---
    const auto imeAction = [](const QString &text) {
        return [text] { return inputMethodCommit(text); };
    };
    cjkExpect(checks, details, QStringLiteral("imeAfterCjk"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("IME commit ABC"),
              imeAction(QStringLiteral("ABC")), QStringLiteral("中文 ABC"), 6);
    cjkExpect(checks, details, QStringLiteral("imeMiddle"),
              QStringLiteral("中文"), 1, 1, QStringLiteral("IME commit ABC"),
              imeAction(QStringLiteral("ABC")), QStringLiteral("中 ABC 文"), 5);
    cjkExpect(checks, details, QStringLiteral("imeMultiCharInternalBoundaries"),
              QStringLiteral("首尾"), 1, 1, QStringLiteral("IME commit A中文B"),
              imeAction(QStringLiteral("A中文B")), QStringLiteral("首 A 中文 B 尾"), 8);
    cjkExpect(checks, details, QStringLiteral("imeInlineCode"),
              QStringLiteral("`中文`"), 1, 1, QStringLiteral("IME commit ABC in inline code"),
              imeAction(QStringLiteral("ABC")), QStringLiteral("`ABC中文`"), 4);
    cjkExpect(checks, details, QStringLiteral("imeInlineFormula"),
              QStringLiteral("$中文$"), 1, 1, QStringLiteral("IME commit ABC in inline formula"),
              imeAction(QStringLiteral("ABC")), QStringLiteral("$ABC中文$"), 4);
    cjkExpect(checks, details, QStringLiteral("imeFence"),
              QStringLiteral("```\n中文\n```"), 6, 6,
              QStringLiteral("IME commit ABC in fenced code"),
              imeAction(QStringLiteral("ABC")), QStringLiteral("```\n中文ABC\n```"), 9);
    cjkExpect(checks, details, QStringLiteral("imeBlockFormula"),
              QStringLiteral("$$\n中文\n$$"), 5, 5,
              QStringLiteral("IME commit ABC in block formula"),
              imeAction(QStringLiteral("ABC")), QStringLiteral("$$\n中文ABC\n$$"), 8);
    cjkExpect(checks, details, QStringLiteral("imeWrapParen"),
              QStringLiteral("中文"), 0, 2, QStringLiteral("IME commit ( on CJK selection"),
              imeAction(QStringLiteral("(")), QStringLiteral("（中文）"), 3, 1, 3);
    cjkExpect(checks, details, QStringLiteral("imeWrapBracket"),
              QStringLiteral("中文"), 0, 2, QStringLiteral("IME commit [ on CJK selection"),
              imeAction(QStringLiteral("[")), QStringLiteral("【中文】"), 3, 1, 3);
    cjkExpect(checks, details, QStringLiteral("imeWrapDoubleQuote"),
              QStringLiteral("中文"), 0, 2, QStringLiteral("IME commit \" on CJK selection"),
              imeAction(QStringLiteral("\"")), QStringLiteral("“中文”"), 3, 1, 3);
    cjkExpect(checks, details, QStringLiteral("imeWrapSingleQuote"),
              QStringLiteral("中文"), 0, 2, QStringLiteral("IME commit ' on CJK selection"),
              imeAction(QStringLiteral("'")), QStringLiteral("‘中文’"), 3, 1, 3);
    cjkExpect(checks, details, QStringLiteral("imeWrapLessThan"),
              QStringLiteral("中文"), 0, 2, QStringLiteral("IME commit < on CJK selection"),
              imeAction(QStringLiteral("<")), QStringLiteral("<中文>"), 3, 1, 3);
    cjkExpect(checks, details, QStringLiteral("imeWrapAsciiParen"),
              QStringLiteral("ABC"), 0, 3, QStringLiteral("IME commit ( on ASCII selection"),
              imeAction(QStringLiteral("(")), QStringLiteral("(ABC)"), 4, 1, 4);
    cjkExpect(checks, details, QStringLiteral("imePunctuationNoConvert"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("IME commit ,"),
              imeAction(QStringLiteral(",")), QStringLiteral("中文,"), 3);
    cjkExpect(checks, details, QStringLiteral("imeChainNotConverted"),
              QStringLiteral("中文："), 3, 3, QStringLiteral("IME commit : after ："),
              imeAction(QStringLiteral(":")), QStringLiteral("中文：:"), 4);
}

} // namespace EditingTests
