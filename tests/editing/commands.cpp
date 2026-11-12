#include "suites.h"
#include "support.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QStringList>
#include <QThread>

namespace EditingTests {

void checkOrderedLists(QJsonObject &checks, QJsonObject &details)
{
    // --- 把选区所在行的普通无序列表转换为有序列表 ---
    const QJsonObject orderedConversionShortcut = request(
        QStringLiteral("testShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("convertToOrderedList")}});
    const QString unorderedOnly = QStringLiteral("- alpha\n+ beta\n* gamma");
    setTextAndSelection(unorderedOnly, 0, unorderedOnly.size());
    const QJsonObject convertedUnorderedOnly = execute(
        QStringLiteral("convertToOrderedList"));
    const QString convertedUnorderedOnlyText =
        QStringLiteral("1. alpha\n2. beta\n3. gamma");
    setTextAndSelection(QStringLiteral("- one\n- two"), 7, 7);
    const QJsonObject convertedCurrentLine = execute(
        QStringLiteral("convertToOrderedList"));
    setTextAndSelection(QStringLiteral("- one\n- two"), 0, 6);
    const QJsonObject convertedExclusiveEnd = execute(
        QStringLiteral("convertToOrderedList"));
    addCheck(checks, details, QStringLiteral("convertUnorderedListBasics"),
             orderedConversionShortcut.value(QStringLiteral("shortcut")).toString().isEmpty()
                 && convertedUnorderedOnly.value(QStringLiteral("executed")).toBool()
                 && convertedUnorderedOnly.value(QStringLiteral("text")).toString()
                    == convertedUnorderedOnlyText
                 && convertedUnorderedOnly.value(QStringLiteral("selectionStart")).toInt() == 0
                 && convertedUnorderedOnly.value(QStringLiteral("selectionEnd")).toInt()
                    == convertedUnorderedOnlyText.size()
                 && convertedCurrentLine.value(QStringLiteral("text")).toString()
                    == QStringLiteral("- one\n1. two")
                 && convertedCurrentLine.value(QStringLiteral("selectionStart")).toInt() == 6
                 && convertedCurrentLine.value(QStringLiteral("selectionEnd")).toInt() == 12
                 && convertedExclusiveEnd.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. one\n- two"),
             QJsonObject{{QStringLiteral("all"), convertedUnorderedOnly},
                         {QStringLiteral("current"), convertedCurrentLine},
                         {QStringLiteral("exclusiveEnd"), convertedExclusiveEnd}});

    const QString mixedOrdered = QStringLiteral(
        "before\n4) one\n5) two\n- three\n8. four\n9. five\nafter");
    const int mixedSelectionStart = mixedOrdered.indexOf(QStringLiteral("5) two"));
    const int mixedSelectionEnd = mixedOrdered.indexOf(QStringLiteral("9. five"));
    setTextAndSelection(mixedOrdered, mixedSelectionStart, mixedSelectionEnd,
                        mixedSelectionStart);
    const QJsonObject convertedMixed = execute(QStringLiteral("convertToOrderedList"));
    const QString expectedMixed = QStringLiteral(
        "before\n4) one\n5) two\n6) three\n7) four\n8) five\nafter");
    const int mixedExpandedStart = expectedMixed.indexOf(QStringLiteral("4) one"));
    const int mixedExpandedEnd = expectedMixed.indexOf(QStringLiteral("\nafter"));
    const QJsonObject convertedMixedUndo = request(QStringLiteral("testUndo"));
    const QJsonObject convertedMixedRedo = request(QStringLiteral("testRedo"));
    addCheck(checks, details, QStringLiteral("convertMixedListExpandsAndUsesTopAnchor"),
             convertedMixed.value(QStringLiteral("text")).toString() == expectedMixed
                 && convertedMixed.value(QStringLiteral("selectionStart")).toInt()
                    == mixedExpandedStart
                 && convertedMixed.value(QStringLiteral("selectionEnd")).toInt()
                    == mixedExpandedEnd
                 && convertedMixedUndo.value(QStringLiteral("text")).toString() == mixedOrdered
                 && convertedMixedUndo.value(QStringLiteral("selectionStart")).toInt()
                    == mixedSelectionStart
                 && convertedMixedUndo.value(QStringLiteral("selectionEnd")).toInt()
                    == mixedSelectionEnd
                 && convertedMixedUndo.value(QStringLiteral("cursorPosition")).toInt()
                    == mixedSelectionStart
                 && convertedMixedRedo.value(QStringLiteral("text")).toString() == expectedMixed,
             QJsonObject{{QStringLiteral("converted"), convertedMixed},
                         {QStringLiteral("undo"), convertedMixedUndo},
                         {QStringLiteral("redo"), convertedMixedRedo}});

    const QString structuredLists = QStringLiteral(
        "1. parent\n    - child\n2. next\n- bridge\n4. tail\n"
        "- [ ] task\n```\n- code\n```\nplain\n> 3. quoted\n> - joined\n- solo");
    const int structuredSelectionEnd = structuredLists.size();
    setTextAndSelection(structuredLists, 0, structuredSelectionEnd);
    const QJsonObject convertedStructured = execute(
        QStringLiteral("convertToOrderedList"));
    const QString expectedStructured = QStringLiteral(
        "1. parent\n    1. child\n2. next\n3. bridge\n4. tail\n"
        "- [ ] task\n```\n- code\n```\nplain\n> 3. quoted\n> 4. joined\n1. solo");
    addCheck(checks, details, QStringLiteral("convertListRespectsStructureAndProtectedBoundaries"),
             convertedStructured.value(QStringLiteral("text")).toString()
                    == expectedStructured
                 && convertedStructured.value(QStringLiteral("selectionStart")).toInt() == 0
                 && convertedStructured.value(QStringLiteral("selectionEnd")).toInt()
                    == expectedStructured.size(),
             QJsonObject{{QStringLiteral("actual"), convertedStructured},
                         {QStringLiteral("expected"), expectedStructured}});

    const QString orderedOnly = QStringLiteral(
        "lead\n7. one\n8. two\n9. three\ntail");
    const int orderedOnlySelection = orderedOnly.indexOf(QStringLiteral("8. two"));
    setTextAndSelection(orderedOnly, orderedOnlySelection, orderedOnlySelection + 2);
    const QJsonObject expandedOrderedOnly = execute(
        QStringLiteral("convertToOrderedList"));
    const QJsonObject expandedOrderedOnlyUndo = request(QStringLiteral("testUndo"));
    addCheck(checks, details, QStringLiteral("convertOrderedOnlyExpandsWithoutUndoRecord"),
             expandedOrderedOnly.value(QStringLiteral("text")).toString() == orderedOnly
                 && expandedOrderedOnly.value(QStringLiteral("selectionStart")).toInt()
                    == orderedOnly.indexOf(QStringLiteral("7. one"))
                 && expandedOrderedOnly.value(QStringLiteral("selectionEnd")).toInt()
                    == orderedOnly.indexOf(QStringLiteral("\ntail"))
                 && expandedOrderedOnlyUndo.value(QStringLiteral("text")).toString()
                    == orderedOnly
                 && expandedOrderedOnlyUndo.value(QStringLiteral("selectionStart")).toInt()
                    == expandedOrderedOnly.value(QStringLiteral("selectionStart")).toInt()
                 && expandedOrderedOnlyUndo.value(QStringLiteral("selectionEnd")).toInt()
                    == expandedOrderedOnly.value(QStringLiteral("selectionEnd")).toInt(),
             QJsonObject{{QStringLiteral("expanded"), expandedOrderedOnly},
                         {QStringLiteral("undo"), expandedOrderedOnlyUndo}});

    const QString disorderedOnly = QStringLiteral("7. one\n20. two\n9. three");
    const int disorderedSelectionStart = disorderedOnly.indexOf(QStringLiteral("20. two"));
    setTextAndSelection(disorderedOnly, disorderedSelectionStart,
                        disorderedSelectionStart + 2);
    const QJsonObject repairedOrderedOnly = execute(
        QStringLiteral("convertToOrderedList"));
    const QJsonObject repairedOrderedOnlyUndo = request(QStringLiteral("testUndo"));
    addCheck(checks, details, QStringLiteral("convertOrderedOnlyRepairsInOneUndoBlock"),
             repairedOrderedOnly.value(QStringLiteral("text")).toString()
                    == QStringLiteral("7. one\n8. two\n9. three")
                 && repairedOrderedOnly.value(QStringLiteral("selectionStart")).toInt() == 0
                 && repairedOrderedOnly.value(QStringLiteral("selectionEnd")).toInt()
                    == QStringLiteral("7. one\n8. two\n9. three").size()
                 && repairedOrderedOnlyUndo.value(QStringLiteral("text")).toString()
                    == disorderedOnly
                 && repairedOrderedOnlyUndo.value(QStringLiteral("selectionStart")).toInt()
                    == disorderedSelectionStart
                 && repairedOrderedOnlyUndo.value(QStringLiteral("selectionEnd")).toInt()
                    == disorderedSelectionStart + 2,
             QJsonObject{{QStringLiteral("repaired"), repairedOrderedOnly},
                         {QStringLiteral("undo"), repairedOrderedOnlyUndo}});

    const QString unselectedBoundary = QStringLiteral(
        "5. before\n- keep\n7. selected\n- 加😀");
    const int boundarySelectionStart = unselectedBoundary.indexOf(
        QStringLiteral("7. selected"));
    setTextAndSelection(unselectedBoundary, boundarySelectionStart,
                        unselectedBoundary.size());
    const QJsonObject convertedAfterBoundary = execute(
        QStringLiteral("convertToOrderedList"));
    const QString expectedAfterBoundary = QStringLiteral(
        "5. before\n- keep\n7. selected\n8. 加😀");
    addCheck(checks, details, QStringLiteral("convertListStopsAtUnselectedBulletUtf16"),
             convertedAfterBoundary.value(QStringLiteral("text")).toString()
                    == expectedAfterBoundary
                 && convertedAfterBoundary.value(QStringLiteral("selectionStart")).toInt()
                    == expectedAfterBoundary.indexOf(QStringLiteral("7. selected"))
                 && convertedAfterBoundary.value(QStringLiteral("selectionEnd")).toInt()
                    == expectedAfterBoundary.size(),
             QJsonObject{{QStringLiteral("actual"), convertedAfterBoundary},
                         {QStringLiteral("expected"), expectedAfterBoundary}});

    QStringList largeConversionLines;
    largeConversionLines.reserve(1000);
    largeConversionLines.append(QStringLiteral("10. item 0"));
    for (int index = 1; index < 1000; ++index) {
        largeConversionLines.append(QStringLiteral("- item %1").arg(index));
    }
    const QString largeConversionSource = largeConversionLines.join(QLatin1Char('\n'));
    setTextAndSelection(largeConversionSource, 0, largeConversionSource.size());
    QElapsedTimer conversionTimer;
    conversionTimer.start();
    const QJsonObject convertedLargeList = execute(
        QStringLiteral("convertToOrderedList"));
    const qint64 conversionElapsedMs = conversionTimer.elapsed();
    const QString convertedLargeText = convertedLargeList.value(
        QStringLiteral("text")).toString();
    addCheck(checks, details, QStringLiteral("convertListScalesLinearly"),
             conversionElapsedMs < 2000
                 && convertedLargeText.startsWith(QStringLiteral("10. item 0\n11. item 1"))
                 && convertedLargeText.endsWith(QStringLiteral("1009. item 999")),
             QJsonObject{{QStringLiteral("elapsedMs"), conversionElapsedMs},
                         {QStringLiteral("length"), convertedLargeText.size()}});

    setTextAndSelection(QStringLiteral("one\ntwo"), 0, 7);
    execute(QStringLiteral("toggleTask"));
    const bool taskOn = editorText() == QStringLiteral("- [ ] one\n- [ ] two");
    const QJsonObject taskOff = execute(QStringLiteral("toggleTask"));
    addCheck(checks, details, QStringLiteral("toggleTask"),
             taskOn && editorText() == QStringLiteral("one\ntwo"), taskOff);

    setTextAndSelection(QStringLiteral("note"), 2, 2);
    const QJsonObject convertedCheckbox = execute(QStringLiteral("toggleCheckbox"));
    const QJsonObject checkedCheckbox = execute(QStringLiteral("toggleCheckbox"));
    const QJsonObject uncheckedCheckbox = execute(QStringLiteral("toggleCheckbox"));
    setTextAndSelection(QStringLiteral("> - [x] done"), 12, 12);
    const QJsonObject uncheckedQuotedCheckbox = execute(QStringLiteral("toggleCheckbox"));
    setTextAndSelection(QStringLiteral("  > > todo"), 10, 10);
    const QJsonObject convertedQuotedCheckbox = execute(QStringLiteral("toggleCheckbox"));
    setTextAndSelection(QStringLiteral("3. item"), 7, 7);
    const QJsonObject convertedOrderedCheckbox = execute(QStringLiteral("toggleCheckbox"));
    addCheck(checks, details, QStringLiteral("toggleCurrentLineCheckbox"),
             convertedCheckbox.value(QStringLiteral("text")).toString()
                    == QStringLiteral("- [ ] note")
                 && convertedCheckbox.value(QStringLiteral("cursorPosition")).toInt() == 8
                 && checkedCheckbox.value(QStringLiteral("text")).toString()
                    == QStringLiteral("- [x] note")
                 && uncheckedCheckbox.value(QStringLiteral("text")).toString()
                    == QStringLiteral("- [ ] note")
                 && uncheckedQuotedCheckbox.value(QStringLiteral("text")).toString()
                    == QStringLiteral("> - [ ] done")
                 && convertedQuotedCheckbox.value(QStringLiteral("text")).toString()
                    == QStringLiteral("  > > - [ ] todo")
                 && convertedOrderedCheckbox.value(QStringLiteral("text")).toString()
                    == QStringLiteral("3. [ ] item"),
             QJsonObject{{QStringLiteral("converted"), convertedCheckbox},
                         {QStringLiteral("checked"), checkedCheckbox},
                         {QStringLiteral("quoted"), uncheckedQuotedCheckbox},
                         {QStringLiteral("quotedPlain"), convertedQuotedCheckbox},
                         {QStringLiteral("ordered"), convertedOrderedCheckbox}});

    setTextAndSelection(QStringLiteral("one\ntwo"), 0, 7);
    execute(QStringLiteral("toggleQuote"));
    const bool quoteOn = editorText() == QStringLiteral("> one\n> two");
    setTextAndSelection(QStringLiteral("> one\n> two"), 0, 11);
    const QJsonObject quoteOff = execute(QStringLiteral("toggleQuote"));
    addCheck(checks, details, QStringLiteral("toggleQuote"),
             quoteOn && editorText() == QStringLiteral("one\ntwo"), quoteOff);

    setTextAndSelection(QString(), 0, 0);
    const QJsonObject emptyQuote = execute(QStringLiteral("toggleQuote"));
    addCheck(checks, details, QStringLiteral("emptyQuoteAndCollapsedSelection"),
             emptyQuote.value(QStringLiteral("text")).toString() == QStringLiteral("> ")
                 && emptyQuote.value(QStringLiteral("selectionStart")).toInt() == 2
                 && emptyQuote.value(QStringLiteral("selectionEnd")).toInt() == 2,
             emptyQuote);

    setTextAndSelection(QStringLiteral("one\ntwo"), 0, 7);
    const QJsonObject quotedSelection = execute(QStringLiteral("toggleQuote"));
    addCheck(checks, details, QStringLiteral("quoteDoesNotRemainSelected"),
             quotedSelection.value(QStringLiteral("text")).toString()
                    == QStringLiteral("> one\n> two")
                 && quotedSelection.value(QStringLiteral("selectionStart")).toInt()
                    == quotedSelection.value(QStringLiteral("selectionEnd")).toInt(),
             quotedSelection);

    setTextAndSelection(QStringLiteral("a\nb\nc"), 2, 2);
    const QJsonObject deletedMiddleLine = execute(QStringLiteral("deleteLine"));
    setTextAndSelection(QStringLiteral("a\nb\nc"), 4, 5);
    const QJsonObject deletedLastLine = execute(QStringLiteral("deleteLine"));
    addCheck(checks, details, QStringLiteral("deleteWholeLine"),
             deletedMiddleLine.value(QStringLiteral("text")).toString()
                    == QStringLiteral("a\nc")
                 && deletedMiddleLine.value(QStringLiteral("cursorPosition")).toInt() == 2
                 && deletedLastLine.value(QStringLiteral("text")).toString()
                    == QStringLiteral("a\nb"),
             deletedMiddleLine);

    setTextAndSelection(QStringLiteral("\nbody"), 0, 0);
    const QJsonObject deletedEmptyFirstLine = execute(QStringLiteral("deleteLine"));
    const QJsonObject deletedEmptyFirstLineUndo = request(QStringLiteral("testUndo"));
    addCheck(checks, details, QStringLiteral("deleteEmptyFirstLineKeepsFollowingText"),
             deletedEmptyFirstLine.value(QStringLiteral("text")).toString()
                     == QStringLiteral("body")
                 && deletedEmptyFirstLine.value(QStringLiteral("cursorPosition")).toInt() == 0
                 && deletedEmptyFirstLineUndo.value(QStringLiteral("text")).toString()
                     == QStringLiteral("\nbody"),
             QJsonObject{{QStringLiteral("deleted"), deletedEmptyFirstLine},
                         {QStringLiteral("undo"), deletedEmptyFirstLineUndo}});
}

void checkEditingCommands(QJsonObject &checks, QJsonObject &details)
{
    // --- 三击选中整行（Qt 原生：非末行含行尾换行符，末行不含） ---
    setTextAndSelection(QStringLiteral("alpha\nbeta\ngamma"), 0, 0);
    const QJsonObject tripleMiddle = tripleClick(8);
    addCheck(checks, details, QStringLiteral("tripleClickSelectsWholeLine"),
             tripleMiddle.value(QStringLiteral("selectionStart")).toInt() == 6
                 && tripleMiddle.value(QStringLiteral("selectionEnd")).toInt() == 11
                 && editorText() == QStringLiteral("alpha\nbeta\ngamma"),
             tripleMiddle);

    const QJsonObject tripleLast = tripleClick(13);
    addCheck(checks, details, QStringLiteral("tripleClickLastLineNoTrailingNewline"),
             tripleLast.value(QStringLiteral("selectionStart")).toInt() == 11
                 && tripleLast.value(QStringLiteral("selectionEnd")).toInt() == 16,
             tripleLast);

    setTextAndSelection(QStringLiteral("a\n\nb"), 2, 2);
    const QJsonObject tripleEmpty = tripleClick(2);
    addCheck(checks, details, QStringLiteral("tripleClickEmptyLine"),
             tripleEmpty.value(QStringLiteral("selectionStart")).toInt() == 2
                 && tripleEmpty.value(QStringLiteral("selectionEnd")).toInt() == 3,
             tripleEmpty);

    setTextAndSelection(QString(), 0, 0);
    const QJsonObject halfWidthPair = keyPress(QStringLiteral("("));
    keyPress({}, QStringLiteral("Tab"));
    const QJsonObject tabIndent = keyPress({}, QStringLiteral("Tab"));
    addCheck(checks, details, QStringLiteral("pairCompletionTabOutAndIndent"),
             halfWidthPair.value(QStringLiteral("text")).toString() == QStringLiteral("()")
                 && halfWidthPair.value(QStringLiteral("cursorPosition")).toInt() == 1
                 && tabIndent.value(QStringLiteral("text")).toString()
                    == QStringLiteral("    ()")
                 && tabIndent.value(QStringLiteral("cursorPosition")).toInt() == 6,
             tabIndent);

    setTextAndSelection(QString(), 0, 0);
    const QJsonObject fullWidthPair = inputMethodCommit(QStringLiteral("（"));
    setTextAndSelection(QString(), 0, 0);
    const QJsonObject fullWidthQuote = inputMethodCommit(QStringLiteral("“"));
    addCheck(checks, details, QStringLiteral("fullWidthPairAndQuoteCompletion"),
             fullWidthPair.value(QStringLiteral("text")).toString() == QStringLiteral("（）")
                 && fullWidthPair.value(QStringLiteral("cursorPosition")).toInt() == 1
                 && fullWidthQuote.value(QStringLiteral("text")).toString()
                    == QStringLiteral("“”")
                 && fullWidthQuote.value(QStringLiteral("cursorPosition")).toInt() == 1,
             fullWidthQuote);

    setTextAndSelection(QStringLiteral("正文"), 0, 0);
    const QJsonObject lineStartAsciiQuote = keyPress(QStringLiteral("\""));
    setTextAndSelection(QStringLiteral("正文"), 0, 0);
    const QJsonObject lineStartAsciiSingleQuote = keyPress(QStringLiteral("'"));
    setTextAndSelection(QStringLiteral("正文"), 0, 0);
    inputMethodCommit(QStringLiteral("“"));
    QThread::msleep(30);
    const QJsonObject lineStartImeQuote = editorStatus();
    const QString lineStartImeQuoteText = editorText();
    setTextAndSelection(QStringLiteral("正文"), 0, 0);
    inputMethodCommit(QStringLiteral("‘"));
    QThread::msleep(30);
    const QJsonObject lineStartImeSingleQuote = editorStatus();
    const QString lineStartImeSingleQuoteText = editorText();
    addCheck(checks, details, QStringLiteral("lineStartQuoteBeforeTextStaysOpen"),
             lineStartAsciiQuote.value(QStringLiteral("text")).toString()
                    == QStringLiteral("\"正文")
                 && lineStartAsciiQuote.value(QStringLiteral("cursorPosition")).toInt() == 1
                 && lineStartAsciiSingleQuote.value(QStringLiteral("text")).toString()
                    == QStringLiteral("'正文")
                 && lineStartAsciiSingleQuote.value(
                        QStringLiteral("cursorPosition")).toInt() == 1
                 && lineStartImeQuoteText == QStringLiteral("“正文")
                 && lineStartImeQuote.value(QStringLiteral("cursorPosition")).toInt() == 1
                 && lineStartImeSingleQuoteText == QStringLiteral("‘正文")
                 && lineStartImeSingleQuote.value(
                        QStringLiteral("cursorPosition")).toInt() == 1,
             QJsonObject{{QStringLiteral("key"), lineStartAsciiQuote},
                         {QStringLiteral("keySingle"), lineStartAsciiSingleQuote},
                         {QStringLiteral("ime"), lineStartImeQuote},
                         {QStringLiteral("imeText"), lineStartImeQuoteText},
                         {QStringLiteral("imeSingle"), lineStartImeSingleQuote},
                         {QStringLiteral("imeSingleText"), lineStartImeSingleQuoteText}});

    setTextAndSelection(QStringLiteral("正文"), 0, 0);
    keyPress(QStringLiteral("`"));
    keyPress(QStringLiteral("`"));
    const QJsonObject lineStartFenceBeforeText = keyPress(QStringLiteral("`"));
    addCheck(checks, details, QStringLiteral("lineStartFenceBeforeTextStillCompletes"),
             lineStartFenceBeforeText.value(QStringLiteral("text")).toString()
                    == QStringLiteral("```\n```\n正文")
                 && lineStartFenceBeforeText.value(QStringLiteral("cursorPosition")).toInt()
                    == 3,
             lineStartFenceBeforeText);

    setTextAndSelection(QString(), 0, 0);
    const QJsonObject inlineCode = keyPress(QStringLiteral("`"));
    keyPress(QStringLiteral("`"));
    const QJsonObject fencedCode = keyPress(QStringLiteral("`"));
    addCheck(checks, details, QStringLiteral("inlineAndFencedCodeCompletion"),
             inlineCode.value(QStringLiteral("text")).toString() == QStringLiteral("``")
                 && inlineCode.value(QStringLiteral("cursorPosition")).toInt() == 1
                 && fencedCode.value(QStringLiteral("text")).toString()
                    == QStringLiteral("```\n```")
                 && fencedCode.value(QStringLiteral("cursorPosition")).toInt() == 3,
             fencedCode);

    setTextAndSelection(QStringLiteral("**"), 1, 1);
    const QJsonObject deletedItalicPair = keyPress({}, QStringLiteral("Backspace"));
    setTextAndSelection(QStringLiteral("****"), 2, 2);
    const QJsonObject deletedBoldPair = keyPress({}, QStringLiteral("Backspace"));
    setTextAndSelection(QStringLiteral("``"), 1, 1);
    const QJsonObject deletedCodePair = keyPress({}, QStringLiteral("Backspace"));
    setTextAndSelection(QStringLiteral("```\n```"), 3, 3);
    const QJsonObject deletedFencePair = keyPress({}, QStringLiteral("Backspace"));
    setTextAndSelection(QStringLiteral("()"), 1, 1);
    const QJsonObject unchangedBracketBackspace = keyPress({}, QStringLiteral("Backspace"));
    addCheck(checks, details, QStringLiteral("backspaceDeletesEmptyMarkdownPairs"),
             deletedItalicPair.value(QStringLiteral("text")).toString().isEmpty()
                 && deletedBoldPair.value(QStringLiteral("text")).toString().isEmpty()
                 && deletedCodePair.value(QStringLiteral("text")).toString().isEmpty()
                 && deletedFencePair.value(QStringLiteral("text")).toString().isEmpty()
                 && unchangedBracketBackspace.value(QStringLiteral("text")).toString()
                    .isEmpty(),
             QJsonObject{{QStringLiteral("italic"), deletedItalicPair},
                         {QStringLiteral("bold"), deletedBoldPair},
                         {QStringLiteral("code"), deletedCodePair},
                         {QStringLiteral("fence"), deletedFencePair},
                         {QStringLiteral("bracket"), unchangedBracketBackspace}});

    bool allHeadingPrefixesDeleted = true;
    QJsonObject lastDeletedHeadingPrefix;
    for (int level = 1; level <= 6; ++level) {
        const QString heading = QString(level, QLatin1Char('#')) + QStringLiteral(" title");
        setTextAndSelection(heading, level + 1, level + 1);
        lastDeletedHeadingPrefix = keyPress({}, QStringLiteral("Backspace"));
        allHeadingPrefixesDeleted = allHeadingPrefixesDeleted
            && lastDeletedHeadingPrefix.value(QStringLiteral("text")).toString()
                == QStringLiteral("title")
            && lastDeletedHeadingPrefix.value(QStringLiteral("cursorPosition")).toInt() == 0;
    }
    setTextAndSelection(QStringLiteral("text ### title"), 9, 9);
    const QJsonObject inlineHashesBackspace = keyPress({}, QStringLiteral("Backspace"));
    setTextAndSelection(QStringLiteral("####### title"), 8, 8);
    const QJsonObject sevenHashesBackspace = keyPress({}, QStringLiteral("Backspace"));
    setTextAndSelection(QStringLiteral("> ### title"), 6, 6);
    const QJsonObject quotedHeadingBackspace = keyPress({}, QStringLiteral("Backspace"));
    setTextAndSelection(QStringLiteral("```\n### title\n```"), 8, 8);
    const QJsonObject fencedHeadingBackspace = keyPress({}, QStringLiteral("Backspace"));
    addCheck(checks, details, QStringLiteral("backspaceDeletesOnlyExactHeadingPrefixes"),
             allHeadingPrefixesDeleted
                 && inlineHashesBackspace.value(QStringLiteral("text")).toString()
                    == QStringLiteral("text ###title")
                 && sevenHashesBackspace.value(QStringLiteral("text")).toString()
                    == QStringLiteral("#######title")
                 && quotedHeadingBackspace.value(QStringLiteral("text")).toString()
                    == QStringLiteral("> ###title")
                 && fencedHeadingBackspace.value(QStringLiteral("text")).toString()
                    == QStringLiteral("```\n###title\n```"),
             QJsonObject{{QStringLiteral("heading"), lastDeletedHeadingPrefix},
                         {QStringLiteral("inline"), inlineHashesBackspace},
                         {QStringLiteral("sevenHashes"), sevenHashesBackspace},
                         {QStringLiteral("quoted"), quotedHeadingBackspace},
                         {QStringLiteral("fenced"), fencedHeadingBackspace}});

    setTextAndSelection(QStringLiteral("> "), 2, 2);
    const QJsonObject deletedEmptyQuotePrefix = keyPress({}, QStringLiteral("Backspace"));
    const QJsonObject deletedEmptyQuotePrefixUndo = request(QStringLiteral("testUndo"));
    setTextAndSelection(QStringLiteral("> text"), 2, 2);
    const QJsonObject deletedQuotePrefixBeforeContent =
        keyPress({}, QStringLiteral("Backspace"));
    setTextAndSelection(QStringLiteral("> > "), 4, 4);
    const QJsonObject deletedNestedQuotePrefix = keyPress({}, QStringLiteral("Backspace"));
    setTextAndSelection(QStringLiteral("> > text"), 4, 4);
    const QJsonObject deletedNestedQuotePrefixBeforeContent =
        keyPress({}, QStringLiteral("Backspace"));
    setTextAndSelection(QStringLiteral("```\n> \n```"), 6, 6);
    const QJsonObject fencedQuotePrefixBackspace = keyPress({}, QStringLiteral("Backspace"));
    addCheck(checks, details, QStringLiteral("backspaceDeletesQuotePrefixAsUnit"),
             deletedEmptyQuotePrefix.value(QStringLiteral("text")).toString().isEmpty()
                 && deletedEmptyQuotePrefix.value(QStringLiteral("cursorPosition")).toInt() == 0
                 && deletedEmptyQuotePrefixUndo.value(QStringLiteral("text")).toString()
                    == QStringLiteral("> ")
                 && deletedEmptyQuotePrefixUndo.value(QStringLiteral("cursorPosition")).toInt()
                    == 2
                 && deletedQuotePrefixBeforeContent.value(QStringLiteral("text")).toString()
                    == QStringLiteral("text")
                 && deletedQuotePrefixBeforeContent.value(
                        QStringLiteral("cursorPosition")).toInt() == 0
                 && deletedNestedQuotePrefix.value(QStringLiteral("text")).toString()
                    == QStringLiteral("> ")
                 && deletedNestedQuotePrefix.value(QStringLiteral("cursorPosition")).toInt() == 2
                 && deletedNestedQuotePrefixBeforeContent.value(
                        QStringLiteral("text")).toString() == QStringLiteral("> text")
                 && deletedNestedQuotePrefixBeforeContent.value(
                        QStringLiteral("cursorPosition")).toInt() == 2
                 && fencedQuotePrefixBackspace.value(QStringLiteral("text")).toString()
                    == QStringLiteral("```\n>\n```"),
             QJsonObject{{QStringLiteral("empty"), deletedEmptyQuotePrefix},
                         {QStringLiteral("emptyUndo"), deletedEmptyQuotePrefixUndo},
                         {QStringLiteral("content"), deletedQuotePrefixBeforeContent},
                         {QStringLiteral("nested"), deletedNestedQuotePrefix},
                         {QStringLiteral("nestedContent"),
                          deletedNestedQuotePrefixBeforeContent},
                         {QStringLiteral("fence"), fencedQuotePrefixBackspace}});

    setTextAndSelection(QString(), 0, 0);
    const QJsonObject autoListSpace = keyPress(QStringLiteral("-"));
    setTextAndSelection(QStringLiteral("……"), 2, 2);
    const QJsonObject deletedEllipsis = keyPress({}, QStringLiteral("Backspace"));
    setTextAndSelection(QStringLiteral("——"), 2, 2);
    const QJsonObject deletedDash = keyPress({}, QStringLiteral("Backspace"));
    addCheck(checks, details, QStringLiteral("lineDashAndWholePunctuationRules"),
             autoListSpace.value(QStringLiteral("text")).toString() == QStringLiteral("- ")
                 && autoListSpace.value(QStringLiteral("cursorPosition")).toInt() == 2
                 && deletedEllipsis.value(QStringLiteral("text")).toString().isEmpty()
                 && deletedDash.value(QStringLiteral("text")).toString().isEmpty(),
             QJsonObject{{QStringLiteral("list"), autoListSpace},
                         {QStringLiteral("ellipsis"), deletedEllipsis},
                         {QStringLiteral("dash"), deletedDash}});

    setTextAndSelection(QString(), 0, 0);
    const QJsonObject autoQuoteSpace = keyPress(QStringLiteral(">"));
    const QJsonObject autoQuoteSpaceUndo = request(QStringLiteral("testUndo"));
    setTextAndSelection(QStringLiteral("正文"), 0, 0);
    const QJsonObject fullwidthQuoteBeforeText = keyPress(QStringLiteral("》"));
    const QJsonObject fullwidthQuoteUndo = request(QStringLiteral("testUndo"));
    setTextAndSelection(QStringLiteral("内容"), 0, 0);
    inputMethodCommit(QStringLiteral("》"));
    QThread::msleep(30);
    const QJsonObject imeFullwidthQuote = editorStatus();
    const QString imeFullwidthQuoteText = editorText();
    const QJsonObject imeFullwidthQuoteUndo = request(QStringLiteral("testUndo"));
    addCheck(checks, details, QStringLiteral("lineStartQuoteAliasesAndUndo"),
             autoQuoteSpace.value(QStringLiteral("text")).toString() == QStringLiteral("> ")
                 && autoQuoteSpace.value(QStringLiteral("cursorPosition")).toInt() == 2
                 && autoQuoteSpaceUndo.value(QStringLiteral("text")).toString().isEmpty()
                 && autoQuoteSpaceUndo.value(QStringLiteral("cursorPosition")).toInt() == 0
                 && fullwidthQuoteBeforeText.value(QStringLiteral("text")).toString()
                    == QStringLiteral("> 正文")
                 && fullwidthQuoteBeforeText.value(QStringLiteral("cursorPosition")).toInt() == 2
                 && fullwidthQuoteUndo.value(QStringLiteral("text")).toString()
                    == QStringLiteral("正文")
                 && fullwidthQuoteUndo.value(QStringLiteral("cursorPosition")).toInt() == 0
                 && imeFullwidthQuoteText == QStringLiteral("> 内容")
                 && imeFullwidthQuote.value(QStringLiteral("cursorPosition")).toInt() == 2
                 && imeFullwidthQuoteUndo.value(QStringLiteral("text")).toString()
                    == QStringLiteral("内容")
                 && imeFullwidthQuoteUndo.value(QStringLiteral("cursorPosition")).toInt() == 0,
             QJsonObject{{QStringLiteral("ascii"), autoQuoteSpace},
                         {QStringLiteral("asciiUndo"), autoQuoteSpaceUndo},
                         {QStringLiteral("fullwidth"), fullwidthQuoteBeforeText},
                         {QStringLiteral("fullwidthUndo"), fullwidthQuoteUndo},
                         {QStringLiteral("ime"), imeFullwidthQuote},
                         {QStringLiteral("imeText"), imeFullwidthQuoteText},
                         {QStringLiteral("imeUndo"), imeFullwidthQuoteUndo}});

    setTextAndSelection(QStringLiteral("正文"), 1, 1);
    inputMethodCommit(QStringLiteral(">"));
    QThread::msleep(30);
    const QJsonObject quoteAliasMidline = editorStatus();
    const QString quoteAliasMidlineText = editorText();
    setTextAndSelection(QStringLiteral("  正文"), 2, 2);
    inputMethodCommit(QStringLiteral("》"));
    QThread::msleep(30);
    const QJsonObject quoteAliasAfterIndent = editorStatus();
    const QString quoteAliasAfterIndentText = editorText();
    setTextAndSelection(QStringLiteral("正文"), 0, 2);
    inputMethodCommit(QStringLiteral(">"));
    QThread::msleep(30);
    const QJsonObject quoteAliasSelection = editorStatus();
    const QString quoteAliasSelectionText = editorText();
    setTextAndSelection(QStringLiteral("```\n\n```"), 4, 4);
    inputMethodCommit(QStringLiteral(">"));
    QThread::msleep(30);
    const QJsonObject quoteAliasInFence = editorStatus();
    const QString quoteAliasInFenceText = editorText();
    addCheck(checks, details, QStringLiteral("lineStartQuoteAliasesRespectBoundaries"),
             quoteAliasMidlineText == QStringLiteral("正>文")
                 && quoteAliasAfterIndentText == QStringLiteral("  》正文")
                 && quoteAliasSelectionText == QStringLiteral(">")
                 && quoteAliasInFenceText == QStringLiteral("```\n>\n```"),
             QJsonObject{{QStringLiteral("midline"), quoteAliasMidline},
                         {QStringLiteral("midlineText"), quoteAliasMidlineText},
                         {QStringLiteral("indent"), quoteAliasAfterIndent},
                         {QStringLiteral("indentText"), quoteAliasAfterIndentText},
                         {QStringLiteral("selection"), quoteAliasSelection},
                         {QStringLiteral("selectionText"), quoteAliasSelectionText},
                         {QStringLiteral("fence"), quoteAliasInFence},
                         {QStringLiteral("fenceText"), quoteAliasInFenceText}});

    setTextAndSelection(QStringLiteral("- one"), 5, 5);
    const QJsonObject continuedBullet = keyPress({}, QStringLiteral("Enter"));
    const QJsonObject exitedBullet = keyPress({}, QStringLiteral("Enter"));
    setTextAndSelection(QStringLiteral("- [x] done"), 10, 10);
    const QJsonObject continuedTask = keyPress({}, QStringLiteral("Enter"));
    setTextAndSelection(QStringLiteral("3) item"), 7, 7);
    const QJsonObject continuedParenthesizedNumber = keyPress({}, QStringLiteral("Enter"));
    addCheck(checks, details, QStringLiteral("enterContinuesListsAndTasks"),
             continuedBullet.value(QStringLiteral("text")).toString()
                    == QStringLiteral("- one\n- ")
                 && continuedBullet.value(QStringLiteral("cursorPosition")).toInt() == 8
                 && exitedBullet.value(QStringLiteral("text")).toString()
                    == QStringLiteral("- one\n")
                 && exitedBullet.value(QStringLiteral("cursorPosition")).toInt() == 6
                 && continuedTask.value(QStringLiteral("text")).toString()
                    == QStringLiteral("- [x] done\n- [ ] ")
                 && continuedTask.value(QStringLiteral("cursorPosition")).toInt() == 17
                 && continuedParenthesizedNumber.value(QStringLiteral("text")).toString()
                    == QStringLiteral("3) item\n4) "),
             QJsonObject{{QStringLiteral("bullet"), continuedBullet},
                         {QStringLiteral("exit"), exitedBullet},
                         {QStringLiteral("task"), continuedTask},
                         {QStringLiteral("parenthesized"), continuedParenthesizedNumber}});

    setTextAndSelection(QStringLiteral("> 引用"), 4, 4);
    const QJsonObject continuedQuote = keyPress({}, QStringLiteral("Enter"));
    setTextAndSelection(QStringLiteral("> 前后"), 3, 3);
    const QJsonObject splitQuote = keyPress({}, QStringLiteral("Enter"));
    setTextAndSelection(QStringLiteral("> > 内容"), 6, 6);
    const QJsonObject continuedNestedQuote = keyPress({}, QStringLiteral("Enter"));
    setTextAndSelection(QStringLiteral("> "), 2, 2);
    const QJsonObject exitedQuote = keyPress({}, QStringLiteral("Enter"));
    setTextAndSelection(QStringLiteral("> > "), 4, 4);
    const QJsonObject exitedNestedQuoteOnce = keyPress({}, QStringLiteral("Enter"));
    const QJsonObject exitedNestedQuoteTwice = keyPress({}, QStringLiteral("Enter"));
    addCheck(checks, details, QStringLiteral("enterContinuesAndExitsQuotes"),
             continuedQuote.value(QStringLiteral("text")).toString()
                    == QStringLiteral("> 引用\n> ")
                 && continuedQuote.value(QStringLiteral("cursorPosition")).toInt() == 7
                 && splitQuote.value(QStringLiteral("text")).toString()
                    == QStringLiteral("> 前\n> 后")
                 && splitQuote.value(QStringLiteral("cursorPosition")).toInt() == 6
                 && continuedNestedQuote.value(QStringLiteral("text")).toString()
                    == QStringLiteral("> > 内容\n> > ")
                 && continuedNestedQuote.value(QStringLiteral("cursorPosition")).toInt() == 11
                 && exitedQuote.value(QStringLiteral("text")).toString().isEmpty()
                 && exitedQuote.value(QStringLiteral("cursorPosition")).toInt() == 0
                 && exitedNestedQuoteOnce.value(QStringLiteral("text")).toString()
                    == QStringLiteral("> ")
                 && exitedNestedQuoteOnce.value(QStringLiteral("cursorPosition")).toInt() == 2
                 && exitedNestedQuoteTwice.value(QStringLiteral("text")).toString().isEmpty()
                 && exitedNestedQuoteTwice.value(QStringLiteral("cursorPosition")).toInt() == 0,
             QJsonObject{{QStringLiteral("continued"), continuedQuote},
                         {QStringLiteral("split"), splitQuote},
                         {QStringLiteral("nested"), continuedNestedQuote},
                         {QStringLiteral("exit"), exitedQuote},
                         {QStringLiteral("nestedExitOnce"), exitedNestedQuoteOnce},
                         {QStringLiteral("nestedExitTwice"), exitedNestedQuoteTwice}});

    setTextAndSelection(QStringLiteral("> > "), 4, 4);
    const QJsonObject softEmptyQuote = keyPress({}, QStringLiteral("Enter"), true);
    setTextAndSelection(QStringLiteral("> 文"), 3, 3);
    const QJsonObject softContentQuote = keyPress({}, QStringLiteral("Enter"), true);
    setTextAndSelection(QStringLiteral("> - item"), 8, 8);
    const QJsonObject continuedQuotedList = keyPress({}, QStringLiteral("Enter"));
    setTextAndSelection(QStringLiteral("> - item"), 8, 8);
    const QJsonObject softQuotedList = keyPress({}, QStringLiteral("Enter"), true);
    setTextAndSelection(QStringLiteral("```\n> code\n```"), 10, 10);
    const QJsonObject softQuoteInFence = keyPress({}, QStringLiteral("Enter"), true);
    setTextAndSelection(QStringLiteral("> text"), 1, 1);
    const QJsonObject enterInsideQuotePrefix = keyPress({}, QStringLiteral("Enter"));
    setTextAndSelection(QStringLiteral("> 😀"), 4, 4);
    const QJsonObject continuedSupplementaryQuote = keyPress({}, QStringLiteral("Enter"));
    addCheck(checks, details, QStringLiteral("quoteContinuationShiftListAndBoundaries"),
             softEmptyQuote.value(QStringLiteral("text")).toString()
                    == QStringLiteral("> > \n> > ")
                 && softEmptyQuote.value(QStringLiteral("cursorPosition")).toInt() == 9
                 && softContentQuote.value(QStringLiteral("text")).toString()
                    == QStringLiteral("> 文\n> ")
                 && continuedQuotedList.value(QStringLiteral("text")).toString()
                    == QStringLiteral("> - item\n> - ")
                 && continuedQuotedList.value(QStringLiteral("cursorPosition")).toInt() == 13
                 && softQuotedList.value(QStringLiteral("text")).toString()
                    == QStringLiteral("> - item\n> ")
                 && softQuotedList.value(QStringLiteral("cursorPosition")).toInt() == 11
                 && softQuoteInFence.value(QStringLiteral("text")).toString()
                    == QStringLiteral("```\n> code\n\n```")
                 && enterInsideQuotePrefix.value(QStringLiteral("text")).toString()
                    == QStringLiteral(">\n text")
                 && continuedSupplementaryQuote.value(QStringLiteral("text")).toString()
                    == QStringLiteral("> 😀\n> ")
                 && continuedSupplementaryQuote.value(QStringLiteral("cursorPosition")).toInt()
                    == 7,
             QJsonObject{{QStringLiteral("softEmpty"), softEmptyQuote},
                         {QStringLiteral("softContent"), softContentQuote},
                         {QStringLiteral("list"), continuedQuotedList},
                         {QStringLiteral("softList"), softQuotedList},
                         {QStringLiteral("fence"), softQuoteInFence},
                         {QStringLiteral("insidePrefix"), enterInsideQuotePrefix},
                         {QStringLiteral("supplementary"), continuedSupplementaryQuote}});

    const QString listUndoSource = QStringLiteral("文档开头\n- 示例文本第一行");
    setTextAndSelection(listUndoSource, listUndoSource.size(), listUndoSource.size());
    const QJsonObject listContinuationBeforeUndo = keyPress({}, QStringLiteral("Enter"));
    const QJsonObject listContinuationUndo = request(QStringLiteral("testUndo"));
    const QJsonObject listContinuationRedo = request(QStringLiteral("testRedo"));
    addCheck(checks, details, QStringLiteral("listContinuationUndoRestoresCursor"),
             listContinuationBeforeUndo.value(QStringLiteral("text")).toString()
                    == listUndoSource + QStringLiteral("\n- ")
                 && listContinuationBeforeUndo.value(QStringLiteral("cursorPosition")).toInt()
                    == listUndoSource.size() + 3
                 && listContinuationUndo.value(QStringLiteral("text")).toString()
                    == listUndoSource
                 && listContinuationUndo.value(QStringLiteral("cursorPosition")).toInt()
                    == listUndoSource.size()
                 && listContinuationRedo.value(QStringLiteral("text")).toString()
                    == listUndoSource + QStringLiteral("\n- ")
                 && listContinuationRedo.value(QStringLiteral("cursorPosition")).toInt()
                    == listUndoSource.size() + 3,
             QJsonObject{{QStringLiteral("continued"), listContinuationBeforeUndo},
                         {QStringLiteral("undo"), listContinuationUndo},
                         {QStringLiteral("redo"), listContinuationRedo}});

    const QString orderedList = QStringLiteral("1. one\n2. two\n3. three");
    setTextAndSelection(orderedList, 6, 6);
    const QJsonObject insertedOrderedItem = keyPress({}, QStringLiteral("Enter"));
    const QString nestedOrderedList = QStringLiteral("1. one\n    1. child\n2. two");
    setTextAndSelection(nestedOrderedList, 6, 6);
    const QJsonObject insertedBeforeNestedItem = keyPress({}, QStringLiteral("Enter"));
    addCheck(checks, details, QStringLiteral("orderedListNumbersStaySequential"),
             insertedOrderedItem.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. one\n2. \n3. two\n4. three")
                 && insertedOrderedItem.value(QStringLiteral("cursorPosition")).toInt() == 10
                 && insertedBeforeNestedItem.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. one\n2. \n    1. child\n3. two")
                 && insertedBeforeNestedItem.value(QStringLiteral("cursorPosition")).toInt()
                    == 10,
             QJsonObject{{QStringLiteral("flat"), insertedOrderedItem},
                         {QStringLiteral("nested"), insertedBeforeNestedItem}});

    setTextAndSelection(QStringLiteral("1. one\n2. two\n3. three"), 7, 7);
    const QJsonObject deletedOrderedLine = execute(QStringLiteral("deleteLine"));
    const QJsonObject deletedOrderedLineUndo = request(QStringLiteral("testUndo"));
    const QJsonObject deletedOrderedLineRedo = request(QStringLiteral("testRedo"));
    setTextAndSelection(QStringLiteral("1. one\n2. two\n3. three"), 7, 7);
    const QJsonObject cutOrderedLine = keyPress({}, QStringLiteral("X"), false,
                                                QStringLiteral("ctrl"));
    addCheck(checks, details, QStringLiteral("orderedListLineRemovalRenumbers"),
             deletedOrderedLine.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. one\n2. three")
                 && deletedOrderedLineUndo.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. one\n2. two\n3. three")
                 && deletedOrderedLineRedo.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. one\n2. three")
                 && cutOrderedLine.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. one\n2. three"),
             QJsonObject{{QStringLiteral("delete"), deletedOrderedLine},
                         {QStringLiteral("undo"), deletedOrderedLineUndo},
                         {QStringLiteral("redo"), deletedOrderedLineRedo},
                         {QStringLiteral("cut"), cutOrderedLine}});

    setTextAndSelection(QStringLiteral("1. one\n2. two\n3. three"), 0, 7);
    keyPress({}, QStringLiteral("Delete"));
    QThread::msleep(30);
    const QJsonObject selectionDeletedOrderedItem = editorStatus();
    const QString selectionDeletedOrderedText = editorText();
    setTextAndSelection(QStringLiteral("1. one\n2. two\n3. three"), 7, 14);
    setClipboard(QStringLiteral("9. replacement\n"));
    keyPress({}, QStringLiteral("V"), false, QStringLiteral("ctrl"));
    QThread::msleep(30);
    const QJsonObject replacedOrderedItem = editorStatus();
    const QString replacedOrderedText = editorText();
    addCheck(checks, details, QStringLiteral("orderedListNativeStructuralEditsRenumber"),
             selectionDeletedOrderedText == QStringLiteral("1. two\n2. three")
                 && replacedOrderedText
                    == QStringLiteral("1. one\n2. replacement\n3. three"),
             QJsonObject{
                 {QStringLiteral("deleteSelection"), selectionDeletedOrderedItem},
                 {QStringLiteral("deleteSelectionText"), selectionDeletedOrderedText},
                 {QStringLiteral("replaceSelection"), replacedOrderedItem},
                 {QStringLiteral("replaceSelectionText"), replacedOrderedText}});

    setTextAndSelection(QStringLiteral("1. one\n2. two\n3. three"), 7, 8);
    keyPress(QStringLiteral("8"), QStringLiteral("8"));
    QThread::msleep(30);
    const QJsonObject normalizedMiddleNumber = editorStatus();
    const QString normalizedMiddleText = editorText();
    setTextAndSelection(QStringLiteral("1. one\n2. two\n3. three"), 8, 8);
    keyPress({}, QStringLiteral("Backspace"));
    keyPress(QStringLiteral("8"), QStringLiteral("8"));
    QThread::msleep(30);
    const QJsonObject normalizedRetypedMiddleNumber = editorStatus();
    const QString normalizedRetypedMiddleText = editorText();
    setTextAndSelection(QStringLiteral("1. one\n2. two\n3. three"), 0, 1);
    keyPress(QStringLiteral("4"), QStringLiteral("4"));
    QThread::msleep(30);
    const QJsonObject changedOrderedStart = editorStatus();
    const QString changedOrderedStartText = editorText();
    addCheck(checks, details, QStringLiteral("orderedListManualNumberPolicy"),
             normalizedMiddleText == QStringLiteral("1. one\n2. two\n3. three")
                 && normalizedRetypedMiddleText
                    == QStringLiteral("1. one\n2. two\n3. three")
                 && changedOrderedStartText
                    == QStringLiteral("4. one\n5. two\n6. three"),
             QJsonObject{
                 {QStringLiteral("middle"), normalizedMiddleNumber},
                 {QStringLiteral("middleText"), normalizedMiddleText},
                 {QStringLiteral("retypedMiddle"), normalizedRetypedMiddleNumber},
                 {QStringLiteral("retypedMiddleText"), normalizedRetypedMiddleText},
                 {QStringLiteral("start"), changedOrderedStart},
                 {QStringLiteral("startText"), changedOrderedStartText}});

    setTextAndSelection(QStringLiteral("1. \n2. two\n3. three"), 3, 3);
    const QJsonObject deletedFirstEmptyOrdered = keyPress({}, QStringLiteral("Backspace"));
    setTextAndSelection(QStringLiteral("1. one\n    1. child\n2. two"), 11, 11);
    const QJsonObject deletedNestedOrdered = execute(QStringLiteral("deleteLine"));
    setTextAndSelection(QStringLiteral("8. eight\n9. nine\n10. ten\n11. eleven"), 9, 9);
    const QJsonObject deletedAcrossDigitBoundary = execute(QStringLiteral("deleteLine"));
    setTextAndSelection(QStringLiteral("1. one\n2. two\n3. three\n\n7. seven\n8. eight"), 7, 7);
    const QJsonObject preservedSeparateOrderedBlock = execute(QStringLiteral("deleteLine"));
    addCheck(checks, details, QStringLiteral("orderedListDeletionBoundaries"),
             deletedFirstEmptyOrdered.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. two\n2. three")
                 && deletedNestedOrdered.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. one\n2. two")
                 && deletedAcrossDigitBoundary.value(QStringLiteral("text")).toString()
                    == QStringLiteral("8. eight\n9. ten\n10. eleven")
                 && preservedSeparateOrderedBlock.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. one\n2. three\n\n7. seven\n8. eight"),
             QJsonObject{{QStringLiteral("firstEmpty"), deletedFirstEmptyOrdered},
                         {QStringLiteral("nested"), deletedNestedOrdered},
                         {QStringLiteral("digits"), deletedAcrossDigitBoundary},
                         {QStringLiteral("separateBlock"), preservedSeparateOrderedBlock}});

    setTextAndSelection(QStringLiteral("1. one\n2. two\n3. three"), 7, 13);
    const QJsonObject indentedOrderedItem = keyPress({}, QStringLiteral("Tab"));
    const QJsonObject outdentedOrderedItem = keyPress({}, QStringLiteral("Tab"), true);
    setTextAndSelection(QStringLiteral("1. one\n2. two\n3. three\n4. four"), 7, 21);
    const QJsonObject indentedOrderedItems = keyPress({}, QStringLiteral("Tab"));
    setTextAndSelection(
        QStringLiteral("1. one\n    1. child\n2. two\n3. three"), 20, 26);
    const QJsonObject joinedExistingNestedList = keyPress({}, QStringLiteral("Tab"));
    setTextAndSelection(QStringLiteral("1. one\n2. two\n3. three"), 7, 14);
    const QJsonObject movedOrderedItem = dragSelection(7, 14, 0);
    addCheck(checks, details, QStringLiteral("orderedListMoveAndIndentRenumber"),
             indentedOrderedItem.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. one\n    1. two\n2. three")
                 && outdentedOrderedItem.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. one\n2. two\n3. three")
                 && indentedOrderedItems.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. one\n    1. two\n    2. three\n2. four")
                 && joinedExistingNestedList.value(QStringLiteral("text")).toString()
                    == QStringLiteral(
                        "1. one\n    1. child\n    2. two\n2. three")
                 && movedOrderedItem.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. two\n2. one\n3. three"),
             QJsonObject{{QStringLiteral("indent"), indentedOrderedItem},
                         {QStringLiteral("outdent"), outdentedOrderedItem},
                         {QStringLiteral("multiIndent"), indentedOrderedItems},
                         {QStringLiteral("joinNested"), joinedExistingNestedList},
                         {QStringLiteral("move"), movedOrderedItem}});

    QStringList largeOrderedLines;
    largeOrderedLines.reserve(500);
    for (int number = 1; number <= 500; ++number) {
        largeOrderedLines.append(QStringLiteral("%1. item %1").arg(number));
    }
    const QString largeOrderedList = largeOrderedLines.join(QLatin1Char('\n'));
    const int largeDeletePosition = largeOrderedList.indexOf(QStringLiteral("250. item"));
    setTextAndSelection(largeOrderedList, largeDeletePosition, largeDeletePosition);
    QElapsedTimer orderedRepairTimer;
    orderedRepairTimer.start();
    const QJsonObject repairedLargeOrderedList = execute(QStringLiteral("deleteLine"));
    const qint64 orderedRepairMs = orderedRepairTimer.elapsed();
    const QString repairedLargeText = repairedLargeOrderedList.value(
        QStringLiteral("text")).toString();
    addCheck(checks, details, QStringLiteral("orderedListRepairScalesLinearly"),
             orderedRepairMs < 2000
                 && repairedLargeText.contains(QStringLiteral("249. item 249\n250. item 251"))
                 && repairedLargeText.endsWith(QStringLiteral("499. item 500")),
             QJsonObject{{QStringLiteral("elapsedMs"), orderedRepairMs},
                         {QStringLiteral("length"), repairedLargeText.size()}});

    setTextAndSelection(QStringLiteral("- aaa\n- bbb\n- \n- ddd"), 14, 14);
    const QJsonObject backspaceEmptyBullet = keyPress({}, QStringLiteral("Backspace"));
    setTextAndSelection(QStringLiteral("- aaa\n- bbb\n- \n- ddd"), 14, 14);
    const QJsonObject enterEmptyBullet = keyPress({}, QStringLiteral("Enter"));
    setTextAndSelection(QStringLiteral("1. aaa\n2. bbb\n3. \n4. ddd"), 17, 17);
    const QJsonObject backspaceEmptyOrdered = keyPress({}, QStringLiteral("Backspace"));
    setTextAndSelection(QStringLiteral("- \n- ddd"), 2, 2);
    const QJsonObject backspaceFirstEmptyItem = keyPress({}, QStringLiteral("Backspace"));
    addCheck(checks, details, QStringLiteral("emptyListEnterAndBackspaceDiffer"),
             backspaceEmptyBullet.value(QStringLiteral("text")).toString()
                    == QStringLiteral("- aaa\n- bbb\n- ddd")
                 && backspaceEmptyBullet.value(QStringLiteral("cursorPosition")).toInt() == 11
                 && enterEmptyBullet.value(QStringLiteral("text")).toString()
                    == QStringLiteral("- aaa\n- bbb\n\n- ddd")
                 && enterEmptyBullet.value(QStringLiteral("cursorPosition")).toInt() == 12
                 && backspaceEmptyOrdered.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. aaa\n2. bbb\n3. ddd")
                 && backspaceEmptyOrdered.value(QStringLiteral("cursorPosition")).toInt() == 13
                 && backspaceFirstEmptyItem.value(QStringLiteral("text")).toString()
                    == QStringLiteral("- ddd")
                 && backspaceFirstEmptyItem.value(QStringLiteral("cursorPosition")).toInt() == 0,
             QJsonObject{{QStringLiteral("backspaceBullet"), backspaceEmptyBullet},
                         {QStringLiteral("enterBullet"), enterEmptyBullet},
                         {QStringLiteral("backspaceOrdered"), backspaceEmptyOrdered},
                         {QStringLiteral("backspaceFirst"), backspaceFirstEmptyItem}});

    const QString nestedEmptyBulletSource = QStringLiteral("- parent\n        - ");
    setTextAndSelection(nestedEmptyBulletSource, nestedEmptyBulletSource.size(),
                        nestedEmptyBulletSource.size());
    const QJsonObject nestedEmptyBulletFirstOutdent = keyPress({}, QStringLiteral("Enter"));
    const QJsonObject nestedEmptyBulletSecondOutdent = keyPress({}, QStringLiteral("Enter"));
    const QJsonObject nestedEmptyBulletExit = keyPress({}, QStringLiteral("Enter"));
    addCheck(checks, details, QStringLiteral("nestedEmptyListEnterOutdentsOneLevel"),
             nestedEmptyBulletFirstOutdent.value(QStringLiteral("text")).toString()
                    == QStringLiteral("- parent\n    - ")
                 && nestedEmptyBulletFirstOutdent.value(
                        QStringLiteral("cursorPosition")).toInt() == 15
                 && nestedEmptyBulletSecondOutdent.value(QStringLiteral("text")).toString()
                    == QStringLiteral("- parent\n- ")
                 && nestedEmptyBulletSecondOutdent.value(
                        QStringLiteral("cursorPosition")).toInt() == 11
                 && nestedEmptyBulletExit.value(QStringLiteral("text")).toString()
                    == QStringLiteral("- parent\n")
                 && nestedEmptyBulletExit.value(QStringLiteral("cursorPosition")).toInt() == 9,
             QJsonObject{{QStringLiteral("first"), nestedEmptyBulletFirstOutdent},
                         {QStringLiteral("second"), nestedEmptyBulletSecondOutdent},
                         {QStringLiteral("exit"), nestedEmptyBulletExit}});

    const QString orderedEmptyOutdentSource =
        QStringLiteral("3. parent\n    1) \n4. next");
    const int orderedEmptyOutdentCursor = orderedEmptyOutdentSource.indexOf(
        QStringLiteral("\n4. next"));
    setTextAndSelection(orderedEmptyOutdentSource, orderedEmptyOutdentCursor,
                        orderedEmptyOutdentCursor);
    const QJsonObject orderedEmptyOutdented = keyPress({}, QStringLiteral("Enter"));
    const QJsonObject orderedEmptyOutdentUndo = request(QStringLiteral("testUndo"));
    const QJsonObject orderedEmptyOutdentRedo = request(QStringLiteral("testRedo"));
    addCheck(checks, details, QStringLiteral("orderedEmptyListEnterJoinsParentSequence"),
             orderedEmptyOutdented.value(QStringLiteral("text")).toString()
                    == QStringLiteral("3. parent\n4. \n5. next")
                 && orderedEmptyOutdented.value(QStringLiteral("cursorPosition")).toInt() == 13
                 && orderedEmptyOutdentUndo.value(QStringLiteral("text")).toString()
                    == orderedEmptyOutdentSource
                 && orderedEmptyOutdentUndo.value(QStringLiteral("cursorPosition")).toInt()
                    == orderedEmptyOutdentCursor
                 && orderedEmptyOutdentRedo.value(QStringLiteral("text")).toString()
                    == QStringLiteral("3. parent\n4. \n5. next")
                 && orderedEmptyOutdentRedo.value(QStringLiteral("cursorPosition")).toInt()
                    == 13,
             QJsonObject{{QStringLiteral("outdent"), orderedEmptyOutdented},
                         {QStringLiteral("undo"), orderedEmptyOutdentUndo},
                         {QStringLiteral("redo"), orderedEmptyOutdentRedo}});

    const QString taskEmptyOutdentSource =
        QStringLiteral("3. parent\n    - [ ] \n4. next");
    const int taskEmptyOutdentCursor = taskEmptyOutdentSource.indexOf(
        QStringLiteral("\n4. next"));
    setTextAndSelection(taskEmptyOutdentSource, taskEmptyOutdentCursor,
                        taskEmptyOutdentCursor);
    const QJsonObject taskEmptyOutdented = keyPress({}, QStringLiteral("Enter"));
    addCheck(checks, details, QStringLiteral("nestedEmptyTaskFollowsParentListFormat"),
             taskEmptyOutdented.value(QStringLiteral("text")).toString()
                    == QStringLiteral("3. parent\n4. [ ] \n5. next")
                 && taskEmptyOutdented.value(QStringLiteral("cursorPosition")).toInt() == 17,
             taskEmptyOutdented);

    const QString orderedUnderBulletSource = QStringLiteral("* parent\n    7) ");
    setTextAndSelection(orderedUnderBulletSource, orderedUnderBulletSource.size(),
                        orderedUnderBulletSource.size());
    const QJsonObject orderedUnderBulletOutdented = keyPress({}, QStringLiteral("Enter"));
    addCheck(checks, details, QStringLiteral("nestedOrderedEmptyFollowsBulletParentFormat"),
             orderedUnderBulletOutdented.value(QStringLiteral("text")).toString()
                    == QStringLiteral("* parent\n* ")
                 && orderedUnderBulletOutdented.value(
                        QStringLiteral("cursorPosition")).toInt() == 11,
             orderedUnderBulletOutdented);

    const QString emptyWithDescendantSource =
        QStringLiteral("- root\n    - \n        1. grandchild");
    const int emptyWithDescendantCursor = emptyWithDescendantSource.indexOf(
        QStringLiteral("\n        1. grandchild"));
    setTextAndSelection(emptyWithDescendantSource, emptyWithDescendantCursor,
                        emptyWithDescendantCursor);
    const QJsonObject emptyWithDescendantOutdented = keyPress({}, QStringLiteral("Enter"));
    addCheck(checks, details, QStringLiteral("emptyListEnterLeavesDescendantsIndented"),
             emptyWithDescendantOutdented.value(QStringLiteral("text")).toString()
                    == QStringLiteral("- root\n- \n        1. grandchild")
                 && emptyWithDescendantOutdented.value(
                        QStringLiteral("cursorPosition")).toInt() == 9,
             emptyWithDescendantOutdented);

    const QString orphanEmptyListSource = QStringLiteral("\t7) ");
    setTextAndSelection(orphanEmptyListSource, orphanEmptyListSource.size(),
                        orphanEmptyListSource.size());
    const QJsonObject orphanEmptyListOutdented = keyPress({}, QStringLiteral("Enter"));
    addCheck(checks, details, QStringLiteral("orphanEmptyListStillOutdents"),
             orphanEmptyListOutdented.value(QStringLiteral("text")).toString()
                    == QStringLiteral("7) ")
                 && orphanEmptyListOutdented.value(QStringLiteral("cursorPosition")).toInt()
                    == 3,
             orphanEmptyListOutdented);

    setTextAndSelection(QStringLiteral("- item"), 6, 6);
    const QJsonObject softListBreak = keyPress({}, QStringLiteral("Enter"), true);
    setTextAndSelection(QStringLiteral("```\n- code\n```"), 10, 10);
    const QJsonObject fencedListBreak = keyPress({}, QStringLiteral("Enter"));
    addCheck(checks, details, QStringLiteral("listContinuationRespectsSoftBreakAndFences"),
             softListBreak.value(QStringLiteral("text")).toString()
                    == QStringLiteral("- item\n")
                 && fencedListBreak.value(QStringLiteral("text")).toString()
                    == QStringLiteral("```\n- code\n\n```"),
             QJsonObject{{QStringLiteral("softBreak"), softListBreak},
                         {QStringLiteral("fence"), fencedListBreak}});

    setTextAndSelection(QStringLiteral("***bold italic***"), 8, 8);
    const QJsonObject boldItalicTab = keyPress({}, QStringLiteral("Tab"));
    addCheck(checks, details, QStringLiteral("tabOutOfMarkdownEmphasis"),
             boldItalicTab.value(QStringLiteral("cursorPosition")).toInt() == 14,
             QJsonObject{{QStringLiteral("actual"), boldItalicTab},
                         {QStringLiteral("expectedCursor"), 14}});
    const QJsonObject boldItalicTabOut = keyPress({}, QStringLiteral("Tab"));
    addCheck(checks, details, QStringLiteral("tabOutOfMarkdownEmphasisSecondStage"),
             boldItalicTabOut.value(QStringLiteral("cursorPosition")).toInt() == 17,
             QJsonObject{{QStringLiteral("actual"), boldItalicTabOut},
                         {QStringLiteral("expectedCursor"), 17}});

    setTextAndSelection(QStringLiteral("a\nb"), 0, 3);
    const QJsonObject multiLineIndent = keyPress({}, QStringLiteral("Tab"));
    const QJsonObject multiLineOutdent = keyPress({}, QStringLiteral("Tab"), true);
    addCheck(checks, details, QStringLiteral("tabIndentAndShiftTabOutdent"),
             multiLineIndent.value(QStringLiteral("text")).toString()
                    == QStringLiteral("    a\n    b")
                 && multiLineOutdent.value(QStringLiteral("text")).toString()
                    == QStringLiteral("a\nb"),
             multiLineOutdent);

    setTextAndSelection(QStringLiteral("alpha"), 2, 2);
    const QJsonObject middleOfLineIndent = keyPress({}, QStringLiteral("Tab"));
    const QJsonObject middleOfLineOutdent = keyPress({}, QStringLiteral("Tab"), true);
    addCheck(checks, details, QStringLiteral("indentFromAnywhereInLine"),
             middleOfLineIndent.value(QStringLiteral("text")).toString()
                    == QStringLiteral("    alpha")
                 && middleOfLineIndent.value(QStringLiteral("cursorPosition")).toInt() == 6
                 && middleOfLineOutdent.value(QStringLiteral("text")).toString()
                    == QStringLiteral("alpha")
                 && middleOfLineOutdent.value(QStringLiteral("cursorPosition")).toInt() == 2,
             middleOfLineOutdent);

    const QString adjacentMarkdownLines = QStringLiteral(
        "* **bold** (Bold)\n* *italic* (Italic)");
    const int firstLineEnd = adjacentMarkdownLines.indexOf(QLatin1Char('\n'));
    setTextAndSelection(adjacentMarkdownLines, firstLineEnd, firstLineEnd);
    const QJsonObject tabAfterClosedBold = keyPress({}, QStringLiteral("Tab"));
    addCheck(checks, details, QStringLiteral("tabAfterClosedEmphasisIndentsLine"),
             tabAfterClosedBold.value(QStringLiteral("text")).toString()
                    == QStringLiteral("    * **bold** (Bold)\n* *italic* (Italic)")
                 && tabAfterClosedBold.value(QStringLiteral("cursorPosition")).toInt()
                    == firstLineEnd + 4,
             tabAfterClosedBold);

    const QString verticalBoundaryText = QStringLiteral("first line\nlast line");
    setTextAndSelection(verticalBoundaryText, 14, 14);
    const QJsonObject downAtLastLine = keyPress({}, QStringLiteral("Down"));
    setTextAndSelection(verticalBoundaryText, 3, 3);
    const QJsonObject upAtFirstLine = keyPress({}, QStringLiteral("Up"));
    setTextAndSelection(verticalBoundaryText, 3, 3);
    const QJsonObject downAcrossLines = keyPress({}, QStringLiteral("Down"));
    const QJsonObject upAcrossLines = keyPress({}, QStringLiteral("Up"));
    addCheck(checks, details, QStringLiteral("verticalArrowMovesAtDocumentBoundary"),
             downAtLastLine.value(QStringLiteral("cursorPosition")).toInt()
                    == verticalBoundaryText.size()
                 && upAtFirstLine.value(QStringLiteral("cursorPosition")).toInt() == 0
                 && downAcrossLines.value(QStringLiteral("cursorPosition")).toInt()
                    > verticalBoundaryText.indexOf(QLatin1Char('\n'))
                 && downAcrossLines.value(QStringLiteral("cursorPosition")).toInt()
                    < verticalBoundaryText.size()
                 && upAcrossLines.value(QStringLiteral("cursorPosition")).toInt() == 3,
             QJsonObject{{QStringLiteral("down"), downAtLastLine},
                         {QStringLiteral("up"), upAtFirstLine},
                         {QStringLiteral("downAcrossLines"), downAcrossLines},
                         {QStringLiteral("upAcrossLines"), upAcrossLines}});

    setTextAndSelection(QStringLiteral("code"), 0, 4);
    execute(QStringLiteral("wrapCode"));
    const bool inlineCodeOn = editorText() == QStringLiteral("`code`");
    execute(QStringLiteral("wrapCode"));
    const bool inlineCodeOff = editorText() == QStringLiteral("code");
    setTextAndSelection(QStringLiteral("line1\nline2"), 0, 11);
    execute(QStringLiteral("wrapCode"));
    const bool blockCodeOn = editorText() == QStringLiteral("```\nline1\nline2\n```");
    execute(QStringLiteral("wrapCode"));
    addCheck(checks, details, QStringLiteral("wrapCode"),
             inlineCodeOn && inlineCodeOff && blockCodeOn
                 && editorText() == QStringLiteral("line1\nline2"));
}

} // namespace EditingTests
