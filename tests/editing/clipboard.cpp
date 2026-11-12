#include "suites.h"
#include "support.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QThread>

namespace EditingTests {

void checkClipboardEditing(QJsonObject &checks, QJsonObject &details)
{
    // --- 整行复制 / 剪切 / 智能粘贴 ---
    setClipboard(QString());
    setTextAndSelection(QStringLiteral("a\nb\nc"), 2, 2);
    const QJsonObject copiedMiddleLine = execute(QStringLiteral("copyLine"));
    const QJsonObject copyMiddleClipboard = clipboardText();
    addCheck(checks, details, QStringLiteral("copyLineMiddle"),
             copiedMiddleLine.value(QStringLiteral("text")).toString()
                    == QStringLiteral("a\nb\nc")
                 && copiedMiddleLine.value(QStringLiteral("cursorPosition")).toInt() == 2
                 && copyMiddleClipboard.value(QStringLiteral("text")).toString()
                    == QStringLiteral("b\n"),
             copyMiddleClipboard);

    setTextAndSelection(QStringLiteral("a\nb\nc"), 4, 4);
    execute(QStringLiteral("copyLine"));
    addCheck(checks, details, QStringLiteral("copyLineLastAddsNewline"),
             editorText() == QStringLiteral("a\nb\nc")
                 && clipboardText().value(QStringLiteral("text")).toString()
                    == QStringLiteral("c\n"),
             clipboardText());

    setTextAndSelection(QStringLiteral("a"), 0, 0);
    execute(QStringLiteral("copyLine"));
    addCheck(checks, details, QStringLiteral("copyLineSingleAddsNewline"),
             editorText() == QStringLiteral("a")
                 && clipboardText().value(QStringLiteral("text")).toString()
                    == QStringLiteral("a\n"),
             clipboardText());

    setTextAndSelection(QStringLiteral("a\n\nb"), 2, 2);
    execute(QStringLiteral("copyLine"));
    addCheck(checks, details, QStringLiteral("copyLineEmptyCopiesNewline"),
             editorText() == QStringLiteral("a\n\nb")
                 && clipboardText().value(QStringLiteral("text")).toString()
                    == QStringLiteral("\n"),
             clipboardText());

    setTextAndSelection(QStringLiteral("a\nb\nc"), 2, 2);
    const QJsonObject cutMiddleLine = execute(QStringLiteral("cutLine"));
    addCheck(checks, details, QStringLiteral("cutLineMiddle"),
             cutMiddleLine.value(QStringLiteral("text")).toString()
                    == QStringLiteral("a\nc")
                 && cutMiddleLine.value(QStringLiteral("cursorPosition")).toInt() == 2
                 && clipboardText().value(QStringLiteral("text")).toString()
                    == QStringLiteral("b\n"),
             cutMiddleLine);

    setTextAndSelection(QStringLiteral("a\nb\nc"), 0, 0);
    const QJsonObject cutFirstLine = execute(QStringLiteral("cutLine"));
    addCheck(checks, details, QStringLiteral("cutLineFirst"),
             cutFirstLine.value(QStringLiteral("text")).toString()
                    == QStringLiteral("b\nc")
                 && cutFirstLine.value(QStringLiteral("cursorPosition")).toInt() == 0
                 && clipboardText().value(QStringLiteral("text")).toString()
                    == QStringLiteral("a\n"),
             cutFirstLine);

    setTextAndSelection(QStringLiteral("a\nb"), 2, 2);
    const QJsonObject cutLastLine = execute(QStringLiteral("cutLine"));
    addCheck(checks, details, QStringLiteral("cutLineLast"),
             cutLastLine.value(QStringLiteral("text")).toString()
                    == QStringLiteral("a\n")
                 && cutLastLine.value(QStringLiteral("cursorPosition")).toInt() == 2
                 && clipboardText().value(QStringLiteral("text")).toString()
                    == QStringLiteral("b\n"),
             cutLastLine);

    setTextAndSelection(QStringLiteral("a"), 0, 0);
    const QJsonObject cutSingleLine = execute(QStringLiteral("cutLine"));
    addCheck(checks, details, QStringLiteral("cutLineSingle"),
             cutSingleLine.value(QStringLiteral("text")).toString().isEmpty()
                 && cutSingleLine.value(QStringLiteral("cursorPosition")).toInt() == 0
                 && clipboardText().value(QStringLiteral("text")).toString()
                    == QStringLiteral("a\n"),
             cutSingleLine);

    setTextAndSelection(QStringLiteral("a\n\nb"), 2, 2);
    const QJsonObject cutEmptyLine = execute(QStringLiteral("cutLine"));
    addCheck(checks, details, QStringLiteral("cutLineEmpty"),
             cutEmptyLine.value(QStringLiteral("text")).toString()
                    == QStringLiteral("a\nb")
                 && cutEmptyLine.value(QStringLiteral("cursorPosition")).toInt() == 2
                 && clipboardText().value(QStringLiteral("text")).toString()
                    == QStringLiteral("\n"),
             cutEmptyLine);

    setTextAndSelection(QStringLiteral("alpha\nbeta\ngamma"), 8, 8);
    const QJsonObject cutLineBeforeUndo = keyPress(
        {}, QStringLiteral("X"), false, QStringLiteral("ctrl"));
    const QJsonObject cutLineUndone = request(QStringLiteral("testUndo"));
    const QJsonObject cutLineRedone = request(QStringLiteral("testRedo"));
    setTextAndSelection(QStringLiteral("alpha\nbeta"), 8, 8);
    keyPress({}, QStringLiteral("X"), false, QStringLiteral("ctrl"));
    const QJsonObject cutLastLineUndone = request(QStringLiteral("testUndo"));
    setTextAndSelection(QStringLiteral("1. one\n2. two\n3. three"), 10, 10);
    const QJsonObject cutOrderedLineWithUndo = keyPress(
        {}, QStringLiteral("X"), false, QStringLiteral("ctrl"));
    const QJsonObject cutOrderedLineUndone = request(QStringLiteral("testUndo"));
    addCheck(checks, details, QStringLiteral("cutLineUndoRestoresOriginalCursor"),
             cutLineBeforeUndo.value(QStringLiteral("text")).toString()
                    == QStringLiteral("alpha\ngamma")
                 && cutLineUndone.value(QStringLiteral("text")).toString()
                    == QStringLiteral("alpha\nbeta\ngamma")
                 && cutLineUndone.value(QStringLiteral("cursorPosition")).toInt() == 8
                 && cutLineUndone.value(QStringLiteral("selectionStart")).toInt() == 8
                 && cutLineUndone.value(QStringLiteral("selectionEnd")).toInt() == 8
                 && cutLineRedone.value(QStringLiteral("text")).toString()
                    == QStringLiteral("alpha\ngamma")
                 && cutLineRedone.value(QStringLiteral("cursorPosition")).toInt() == 6
                 && cutLastLineUndone.value(QStringLiteral("text")).toString()
                    == QStringLiteral("alpha\nbeta")
                 && cutLastLineUndone.value(QStringLiteral("cursorPosition")).toInt() == 8
                 && cutOrderedLineWithUndo.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. one\n2. three")
                 && cutOrderedLineUndone.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. one\n2. two\n3. three")
                 && cutOrderedLineUndone.value(
                        QStringLiteral("cursorPosition")).toInt() == 10,
             QJsonObject{{QStringLiteral("cut"), cutLineBeforeUndo},
                         {QStringLiteral("undo"), cutLineUndone},
                         {QStringLiteral("redo"), cutLineRedone},
                         {QStringLiteral("lastLineUndo"), cutLastLineUndone},
                         {QStringLiteral("orderedCut"), cutOrderedLineWithUndo},
                         {QStringLiteral("orderedUndo"), cutOrderedLineUndone}});

    setClipboard(QString());
    setTextAndSelection(QStringLiteral("a\nb\nc"), 2, 2);
    keyPress({}, QStringLiteral("C"), false, QStringLiteral("ctrl"));
    const QJsonObject copyPasteOnce = keyPress({}, QStringLiteral("V"), false,
                                               QStringLiteral("ctrl"));
    addCheck(checks, details, QStringLiteral("ctrlCPasteDuplicatesBelow"),
             editorText() == QStringLiteral("a\nb\nb\nc")
                 && copyPasteOnce.value(QStringLiteral("cursorPosition")).toInt() == 5,
             copyPasteOnce);
    const QJsonObject copyPasteTwice = keyPress({}, QStringLiteral("V"), false,
                                                QStringLiteral("ctrl"));
    addCheck(checks, details, QStringLiteral("ctrlCPasteRepeatStacks"),
             editorText() == QStringLiteral("a\nb\nb\nb\nc")
                 && copyPasteTwice.value(QStringLiteral("cursorPosition")).toInt() == 7,
             copyPasteTwice);

    setTextAndSelection(QStringLiteral("a\nb\nc"), 2, 2);
    const QJsonObject cutWithCtrlX = keyPress({}, QStringLiteral("X"), false,
                                              QStringLiteral("ctrl"));
    addCheck(checks, details, QStringLiteral("ctrlXCutLine"),
             editorText() == QStringLiteral("a\nc")
                 && cutWithCtrlX.value(QStringLiteral("cursorPosition")).toInt() == 2
                 && clipboardText().value(QStringLiteral("text")).toString()
                    == QStringLiteral("b\n"),
             cutWithCtrlX);
    const QJsonObject pasteAfterCtrlX = keyPress({}, QStringLiteral("V"), false,
                                                 QStringLiteral("ctrl"));
    addCheck(checks, details, QStringLiteral("ctrlXPasteRestoresBelow"),
             editorText() == QStringLiteral("a\nc\nb\n")
                 && pasteAfterCtrlX.value(QStringLiteral("cursorPosition")).toInt() == 5,
             pasteAfterCtrlX);

    setClipboard(QStringLiteral("x\n"));
    setTextAndSelection(QStringLiteral("abc"), 1, 1);
    const QJsonObject pasteLineMidDocument = keyPress({}, QStringLiteral("V"), false,
                                                      QStringLiteral("ctrl"));
    addCheck(checks, details, QStringLiteral("pasteLineKeepsCurrentLine"),
             editorText() == QStringLiteral("abc\nx\n")
                 && pasteLineMidDocument.value(QStringLiteral("cursorPosition")).toInt() == 5,
             pasteLineMidDocument);

    setClipboard(QStringLiteral("xy"));
    setTextAndSelection(QStringLiteral("abc"), 1, 1);
    const QJsonObject pastePlainMidLine = keyPress({}, QStringLiteral("V"), false,
                                                   QStringLiteral("ctrl"));
    addCheck(checks, details, QStringLiteral("pastePlainInsertsAtCursor"),
             editorText() == QStringLiteral("axybc")
                 && pastePlainMidLine.value(QStringLiteral("cursorPosition")).toInt() == 3,
             pastePlainMidLine);

    // 粘贴由独立 QTextCursor 写入；撤销必须显式恢复粘贴前的光标或选区，
    // 不能采用该临时 cursor 创建时的文档开头位置。
    setClipboard(QStringLiteral("++"));
    setTextAndSelection(QStringLiteral("left-right"), 4, 4);
    const QJsonObject plainPasteBeforeUndo = editorStatus();
    const QJsonObject plainPasteApplied = keyPress({}, QStringLiteral("V"), false,
                                                   QStringLiteral("ctrl"));
    const QJsonObject plainPasteUndone = request(QStringLiteral("testUndo"));
    const QJsonObject plainPasteRedone = request(QStringLiteral("testRedo"));
    addCheck(checks, details, QStringLiteral("pasteUndoRestoresInsertionCursor"),
             plainPasteApplied.value(QStringLiteral("text")).toString()
                     == QStringLiteral("left++-right")
                 && plainPasteUndone.value(QStringLiteral("text")).toString()
                    == QStringLiteral("left-right")
                 && plainPasteUndone.value(QStringLiteral("cursorPosition")).toInt() == 4
                 && plainPasteUndone.value(QStringLiteral("selectionStart")).toInt() == 4
                 && plainPasteUndone.value(QStringLiteral("selectionEnd")).toInt() == 4
                 && plainPasteRedone.value(QStringLiteral("text")).toString()
                    == QStringLiteral("left++-right")
                 && plainPasteRedone.value(QStringLiteral("cursorPosition")).toInt() == 6,
             QJsonObject{{QStringLiteral("before"), plainPasteBeforeUndo},
                         {QStringLiteral("pasted"), plainPasteApplied},
                         {QStringLiteral("undone"), plainPasteUndone},
                         {QStringLiteral("redone"), plainPasteRedone}});

    setClipboard(QStringLiteral("line\n"));
    setTextAndSelection(QStringLiteral("alpha\nomega"), 2, 2);
    const QJsonObject smartPasteApplied = keyPress({}, QStringLiteral("V"), false,
                                                   QStringLiteral("ctrl"));
    const QJsonObject smartPasteUndone = request(QStringLiteral("testUndo"));
    addCheck(checks, details, QStringLiteral("smartLinePasteUndoRestoresOriginalCursor"),
             smartPasteApplied.value(QStringLiteral("text")).toString()
                     == QStringLiteral("alpha\nline\nomega")
                 && smartPasteUndone.value(QStringLiteral("text")).toString()
                    == QStringLiteral("alpha\nomega")
                 && smartPasteUndone.value(QStringLiteral("cursorPosition")).toInt() == 2
                 && smartPasteUndone.value(QStringLiteral("selectionStart")).toInt() == 2
                 && smartPasteUndone.value(QStringLiteral("selectionEnd")).toInt() == 2,
             QJsonObject{{QStringLiteral("pasted"), smartPasteApplied},
                         {QStringLiteral("undone"), smartPasteUndone}});

    const auto checkPasteSelectionUndo = [&](const QString &name, int activeEnd) {
        setClipboard(QStringLiteral("x"));
        setTextAndSelection(QStringLiteral("before TARGET after"), 7, 13, activeEnd);
        const QJsonObject before = editorStatus();
        const QJsonObject pasted = keyPress({}, QStringLiteral("V"), false,
                                            QStringLiteral("ctrl"));
        const QJsonObject undone = request(QStringLiteral("testUndo"));
        addCheck(checks, details, name,
                 pasted.value(QStringLiteral("text")).toString()
                         == QStringLiteral("before x after")
                     && undone.value(QStringLiteral("text")).toString()
                        == QStringLiteral("before TARGET after")
                     && undone.value(QStringLiteral("selectionStart")).toInt() == 7
                     && undone.value(QStringLiteral("selectionEnd")).toInt() == 13
                     && undone.value(QStringLiteral("cursorPosition")).toInt() == activeEnd,
                 QJsonObject{{QStringLiteral("before"), before},
                             {QStringLiteral("pasted"), pasted},
                             {QStringLiteral("undone"), undone}});
    };
    checkPasteSelectionUndo(QStringLiteral("pasteUndoRestoresForwardSelection"), 13);
    checkPasteSelectionUndo(QStringLiteral("pasteUndoRestoresReverseSelection"), 7);

    setClipboard(QStringLiteral("中"));
    setTextAndSelection(QStringLiteral("头😀尾"), 3, 3);
    const QJsonObject utf16PasteApplied = keyPress({}, QStringLiteral("V"), false,
                                                   QStringLiteral("ctrl"));
    const QJsonObject utf16PasteUndone = request(QStringLiteral("testUndo"));
    addCheck(checks, details, QStringLiteral("pasteUndoRestoresUtf16Cursor"),
             utf16PasteApplied.value(QStringLiteral("text")).toString()
                     == QStringLiteral("头😀中尾")
                 && utf16PasteUndone.value(QStringLiteral("text")).toString()
                    == QStringLiteral("头😀尾")
                 && utf16PasteUndone.value(QStringLiteral("cursorPosition")).toInt() == 3,
             QJsonObject{{QStringLiteral("pasted"), utf16PasteApplied},
                         {QStringLiteral("undone"), utf16PasteUndone}});

    setClipboard(QStringLiteral("2. second\n"));
    setTextAndSelection(QStringLiteral("1. first\n2. third"), 3, 3);
    const QJsonObject listPasteApplied = keyPress({}, QStringLiteral("V"), false,
                                                  QStringLiteral("ctrl"));
    const QJsonObject listPasteUndone = request(QStringLiteral("testUndo"));
    addCheck(checks, details, QStringLiteral("pasteUndoRestoresCursorAfterListRepair"),
             listPasteApplied.value(QStringLiteral("text")).toString()
                     == QStringLiteral("1. first\n2. second\n3. third")
                 && listPasteUndone.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. first\n2. third")
                 && listPasteUndone.value(QStringLiteral("cursorPosition")).toInt() == 3,
             QJsonObject{{QStringLiteral("pasted"), listPasteApplied},
                         {QStringLiteral("undone"), listPasteUndone}});

    // --- 智能粘贴边界：空文档 / 末尾空行不得产生前导空行 ---
    setClipboard(QStringLiteral("a\nb\n"));
    setTextAndSelection(QString(), 0, 0);
    const QJsonObject pasteIntoEmptyDoc = keyPress({}, QStringLiteral("V"), false,
                                                   QStringLiteral("ctrl"));
    addCheck(checks, details, QStringLiteral("pasteClipboardEmptyDoc"),
             editorText() == QStringLiteral("a\nb\n")
                 && pasteIntoEmptyDoc.value(QStringLiteral("cursorPosition")).toInt() == 3,
             pasteIntoEmptyDoc);

    setClipboard(QStringLiteral("a\nb\n"));
    setTextAndSelection(QStringLiteral("x\n"), 2, 2);
    const QJsonObject pasteAtTrailingEmptyLine = keyPress({}, QStringLiteral("V"), false,
                                                          QStringLiteral("ctrl"));
    addCheck(checks, details, QStringLiteral("pasteClipboardTrailingEmptyLine"),
             editorText() == QStringLiteral("x\na\nb\n")
                 && pasteAtTrailingEmptyLine.value(
                        QStringLiteral("cursorPosition")).toInt() == 5,
             pasteAtTrailingEmptyLine);

    setClipboard(QStringLiteral("\n"));
    setTextAndSelection(QString(), 0, 0);
    const QJsonObject pasteEmptyLineOnly = keyPress({}, QStringLiteral("V"), false,
                                                    QStringLiteral("ctrl"));
    addCheck(checks, details, QStringLiteral("pasteClipboardEmptyLineOnly"),
             editorText() == QStringLiteral("\n")
                 && pasteEmptyLineOnly.value(
                        QStringLiteral("cursorPosition")).toInt() == 1,
             pasteEmptyLineOnly);

    // 用户复现链：Ctrl+A 全选 → Ctrl+X 原生剪切（文档变空）→ Ctrl+V 智能粘贴，
    // 重复两轮，首行前不得累积空行。
    setClipboard(QString());
    setTextAndSelection(QStringLiteral("a\nb\n"), 0, 4);
    keyPress({}, QStringLiteral("A"), false, QStringLiteral("ctrl"));
    keyPress({}, QStringLiteral("X"), false, QStringLiteral("ctrl"));
    setClipboard(QStringLiteral("a\nb\n"));
    const QJsonObject loopPasteFirst = keyPress({}, QStringLiteral("V"), false,
                                                QStringLiteral("ctrl"));
    keyPress({}, QStringLiteral("A"), false, QStringLiteral("ctrl"));
    keyPress({}, QStringLiteral("X"), false, QStringLiteral("ctrl"));
    setClipboard(QStringLiteral("a\nb\n"));
    const QJsonObject loopPasteSecond = keyPress({}, QStringLiteral("V"), false,
                                                 QStringLiteral("ctrl"));
    addCheck(checks, details, QStringLiteral("ctrlAXVPasteLoopStable"),
             editorText() == QStringLiteral("a\nb\n")
                 && loopPasteFirst.value(QStringLiteral("cursorPosition")).toInt() == 3
                 && loopPasteSecond.value(QStringLiteral("cursorPosition")).toInt() == 3,
             loopPasteSecond);
}

void checkClipboardHistory(QJsonObject &checks, QJsonObject &details)
{
    // --- Clipboard history integration and command contract ---
    const QJsonObject historyShortcutDefault = request(
        QStringLiteral("testShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("clipboardHistory")}});
    const QJsonObject historyCommand = execute(QStringLiteral("clipboardHistory"));
    addCheck(checks, details, QStringLiteral("clipboardHistoryCommandRegistered"),
             historyCommand.value(QStringLiteral("executed")).toBool()
                 && historyShortcutDefault.value(QStringLiteral("shortcut")).toString().isEmpty(),
             historyCommand);
    const QJsonObject historyShortcutSet = request(
        QStringLiteral("testSetShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("clipboardHistory")},
         {QStringLiteral("sequence"), QStringLiteral("Ctrl+Alt+V")}});
    addCheck(checks, details, QStringLiteral("clipboardHistoryShortcutConfigurable"),
             historyShortcutSet.value(QStringLiteral("configured")).toBool()
                 && historyShortcutSet.value(QStringLiteral("shortcut")).toString()
                    == QStringLiteral("Ctrl+Alt+V"),
             historyShortcutSet);
    request(QStringLiteral("testSetShortcut"),
            {{QStringLiteral("commandId"), QStringLiteral("clipboardHistory")},
             {QStringLiteral("sequence"), QString()}});

    request(QStringLiteral("testResetClipboardHistory"));
    request(QStringLiteral("testSetClipboard"),
            {{QStringLiteral("text"), QStringLiteral("virtual-seed")}});
    request(QStringLiteral("show"));
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), QStringLiteral("committed-history")}});
    request(QStringLiteral("hide"));
    QThread::msleep(250);
    const QJsonObject committedClipboard = request(QStringLiteral("testClipboard"));
    const QJsonObject committedHistory = request(
        QStringLiteral("testClipboardHistoryState"));
    addCheck(checks, details, QStringLiteral("clipboardHistoryEscCommitUsesMemoryGateway"),
             committedClipboard.value(QStringLiteral("text")).toString()
                    == QStringLiteral("committed-history")
                 && committedHistory.value(QStringLiteral("items")).toArray().size() == 1
                 && committedHistory.value(QStringLiteral("items")).toArray().first()
                        .toObject().value(QStringLiteral("text")).toString()
                    == QStringLiteral("committed-history")
                 && committedHistory.value(QStringLiteral("clipboardBackend")).toString()
                    == QStringLiteral("memory")
                 && committedHistory.value(QStringLiteral("nativeClipboardAccessAttempts")).toInteger() == 0,
             committedHistory);

    request(QStringLiteral("testSetClipboard"),
            {{QStringLiteral("text"), QStringLiteral("discard-seed")}});
    request(QStringLiteral("show"));
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), QStringLiteral("must-be-discarded")}});
    request(QStringLiteral("testDiscardClose"));
    QThread::msleep(250);
    const QJsonObject discardedClipboard = request(QStringLiteral("testClipboard"));
    const QJsonObject discardedHistory = request(
        QStringLiteral("testClipboardHistoryState"));
    addCheck(checks, details, QStringLiteral("clipboardHistoryCtrlWDoesNotCapture"),
             discardedClipboard.value(QStringLiteral("text")).toString()
                    == QStringLiteral("discard-seed")
                 && discardedHistory.value(QStringLiteral("items")).toArray().size() == 1,
             discardedHistory);

    request(QStringLiteral("testEmitClipboardChange"),
            {{QStringLiteral("kind"), QStringLiteral("text")},
             {QStringLiteral("text"), QStringLiteral("immutable-original")},
             {QStringLiteral("sequenceNumber"), 901},
             {QStringLiteral("capturedAtMs"), 1786200000000.0}});
    request(QStringLiteral("testEmitClipboardChange"),
            {{QStringLiteral("kind"), QStringLiteral("text")},
             {QStringLiteral("text"), QStringLiteral("9. second\n")},
             {QStringLiteral("sequenceNumber"), 902},
             {QStringLiteral("capturedAtMs"), 1786200001000.0}});
    const QJsonObject beforeLoadHistory = request(
        QStringLiteral("testClipboardHistoryState"));
    const QJsonArray beforeLoadItems = beforeLoadHistory.value(
        QStringLiteral("items")).toArray();
    QString originalId;
    QString orderedListDragId;
    for (const QJsonValue &value : beforeLoadItems) {
        const QString itemText = value.toObject().value(QStringLiteral("text")).toString();
        if (itemText == QStringLiteral("immutable-original")) {
            originalId = value.toObject().value(QStringLiteral("id")).toString();
        } else if (itemText == QStringLiteral("9. second\n")) {
            orderedListDragId = value.toObject().value(QStringLiteral("id")).toString();
        }
    }
    request(QStringLiteral("show"));

    const QString historyDragTarget = QStringLiteral("头😀尾");
    setTextAndSelection(historyDragTarget, 0, 1);
    const QJsonObject historyDragBefore = editorStatus();
    const QJsonObject historyDragged = dragClipboardHistory(originalId, 3);
    const QJsonObject historyDragUndone = request(QStringLiteral("testUndo"));
    setTextAndSelection(historyDragTarget, 0, 1);
    const QJsonObject historyDragOutside = dragClipboardHistory(originalId, 3, true);
    addCheck(checks, details, QStringLiteral("clipboardHistoryDragInsertsAtUtf16DropAndUndoesOnce"),
             !originalId.isEmpty()
                 && historyDragged.value(QStringLiteral("dragActivated")).toBool()
                 && historyDragged.value(QStringLiteral("inserted")).toBool()
                 && historyDragged.value(QStringLiteral("text")).toString()
                    == QStringLiteral("头😀immutable-original尾")
                 && historyDragged.value(QStringLiteral("selectionStart")).toInt() == 3
                 && historyDragged.value(QStringLiteral("selectionEnd")).toInt() == 21
                 && historyDragUndone.value(QStringLiteral("text")).toString()
                    == historyDragTarget
                 && historyDragUndone.value(QStringLiteral("selectionStart"))
                    == historyDragBefore.value(QStringLiteral("selectionStart"))
                 && historyDragUndone.value(QStringLiteral("selectionEnd"))
                    == historyDragBefore.value(QStringLiteral("selectionEnd"))
                 && historyDragUndone.value(QStringLiteral("cursorPosition"))
                    == historyDragBefore.value(QStringLiteral("cursorPosition"))
                 && historyDragOutside.value(QStringLiteral("dragActivated")).toBool()
                 && !historyDragOutside.value(QStringLiteral("inserted")).toBool()
                 && historyDragOutside.value(QStringLiteral("text")).toString()
                    == historyDragTarget,
             QJsonObject{{QStringLiteral("before"), historyDragBefore},
                         {QStringLiteral("dropped"), historyDragged},
                         {QStringLiteral("undone"), historyDragUndone},
                         {QStringLiteral("outside"), historyDragOutside}});

    setTextAndSelection(historyDragTarget, 0, 1, 0);
    const QJsonObject historyReverseDragged = dragClipboardHistory(originalId, 3);
    const QJsonObject historyReverseUndone = request(QStringLiteral("testUndo"));
    addCheck(checks, details, QStringLiteral("clipboardHistoryDragUndoRestoresReverseSelection"),
             historyReverseDragged.value(QStringLiteral("inserted")).toBool()
                 && historyReverseUndone.value(QStringLiteral("text")).toString()
                    == historyDragTarget
                 && historyReverseUndone.value(QStringLiteral("selectionStart")).toInt() == 0
                 && historyReverseUndone.value(QStringLiteral("selectionEnd")).toInt() == 1
                 && historyReverseUndone.value(QStringLiteral("cursorPosition")).toInt() == 0,
             QJsonObject{{QStringLiteral("dropped"), historyReverseDragged},
                         {QStringLiteral("undone"), historyReverseUndone}});

    setTextAndSelection(historyDragTarget, 3, 3);
    const QJsonObject historyCursorDragged = dragClipboardHistory(originalId, 0);
    const QJsonObject historyCursorUndone = request(QStringLiteral("testUndo"));
    addCheck(checks, details, QStringLiteral("clipboardHistoryDragUndoRestoresUtf16Cursor"),
             historyCursorDragged.value(QStringLiteral("inserted")).toBool()
                 && historyCursorUndone.value(QStringLiteral("text")).toString()
                    == historyDragTarget
                 && historyCursorUndone.value(QStringLiteral("selectionStart")).toInt() == 3
                 && historyCursorUndone.value(QStringLiteral("selectionEnd")).toInt() == 3
                 && historyCursorUndone.value(QStringLiteral("cursorPosition")).toInt() == 3,
             QJsonObject{{QStringLiteral("dropped"), historyCursorDragged},
                         {QStringLiteral("undone"), historyCursorUndone}});

    const QString orderedListBefore = QStringLiteral("1. first\n2. third");
    setTextAndSelection(orderedListBefore, 0, 0);
    const QJsonObject orderedListDragged = dragClipboardHistory(orderedListDragId, 9);
    const QJsonObject orderedListDragUndone = request(QStringLiteral("testUndo"));
    addCheck(checks, details, QStringLiteral("clipboardHistoryDragRepairsOrderedListInUndoBlock"),
             !orderedListDragId.isEmpty()
                 && orderedListDragged.value(QStringLiteral("inserted")).toBool()
                 && orderedListDragged.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. first\n2. second\n3. third")
                 && orderedListDragUndone.value(QStringLiteral("text")).toString()
                    == orderedListBefore
                 && orderedListDragUndone.value(QStringLiteral("selectionStart")).toInt() == 0
                 && orderedListDragUndone.value(QStringLiteral("selectionEnd")).toInt() == 0
                 && orderedListDragUndone.value(QStringLiteral("cursorPosition")).toInt() == 0,
             QJsonObject{{QStringLiteral("dropped"), orderedListDragged},
                         {QStringLiteral("undone"), orderedListDragUndone}});

    request(QStringLiteral("testDiscardClose"));
    QThread::msleep(150);
    request(QStringLiteral("show"));

    request(QStringLiteral("testClipboardHistoryUiAction"),
            {{QStringLiteral("action"), QStringLiteral("historySelect")},
             {QStringLiteral("value"), originalId}});
    request(QStringLiteral("testClipboardHistoryUiAction"),
            {{QStringLiteral("action"), QStringLiteral("historyActivateSelected")}});
    QThread::msleep(150);
    const QJsonObject loadedText = request(QStringLiteral("testText"));
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), QStringLiteral("edited-from-history")}});
    request(QStringLiteral("hide"));
    QThread::msleep(250);
    const QJsonObject afterEditHistory = request(
        QStringLiteral("testClipboardHistoryState"));
    bool originalUnchanged = false;
    bool editedRecorded = false;
    for (const QJsonValue &value : afterEditHistory.value(QStringLiteral("items")).toArray()) {
        const QJsonObject item = value.toObject();
        originalUnchanged = originalUnchanged
            || (item.value(QStringLiteral("id")).toString() == originalId
                && item.value(QStringLiteral("text")).toString()
                   == QStringLiteral("immutable-original"));
        editedRecorded = editedRecorded
            || item.value(QStringLiteral("text")).toString()
               == QStringLiteral("edited-from-history");
    }
    addCheck(checks, details, QStringLiteral("clipboardHistoryLoadAndEditIsImmutable"),
             loadedText.value(QStringLiteral("text")).toString()
                    == QStringLiteral("immutable-original")
                 && originalUnchanged && editedRecorded,
             afterEditHistory);

    const QString crlfHistoryText = QStringLiteral("first\r\nsecond\r\nthird");
    request(QStringLiteral("testEmitClipboardChange"),
            {{QStringLiteral("kind"), QStringLiteral("text")},
             {QStringLiteral("text"), crlfHistoryText},
             {QStringLiteral("sequenceNumber"), 903},
             {QStringLiteral("capturedAtMs"), 1786200002000.0}});
    const QJsonArray crlfHistoryItems = request(
        QStringLiteral("testClipboardHistoryState"))
        .value(QStringLiteral("items")).toArray();
    QString crlfDragId;
    for (const QJsonValue &value : crlfHistoryItems) {
        if (value.toObject().value(QStringLiteral("text")).toString() == crlfHistoryText) {
            crlfDragId = value.toObject().value(QStringLiteral("id")).toString();
            break;
        }
    }
    request(QStringLiteral("show"));
    const QString crlfDragTarget = QStringLiteral("ORIGINAL");
    const QString normalizedCrlfText = QStringLiteral("first\nsecond\nthird");
    setTextAndSelection(crlfDragTarget, 2, 5);
    const QJsonObject crlfDragged = dragClipboardHistory(crlfDragId, 0);
    const QJsonObject crlfDragUndone = request(QStringLiteral("testUndo"));
    addCheck(checks, details,
             QStringLiteral("clipboardHistoryDragSelectsNormalizedCrlfTextOnly"),
             !crlfDragId.isEmpty()
                 && crlfDragged.value(QStringLiteral("inserted")).toBool()
                 && crlfDragged.value(QStringLiteral("text")).toString()
                    == normalizedCrlfText + crlfDragTarget
                 && crlfDragged.value(QStringLiteral("selectionStart")).toInt() == 0
                 && crlfDragged.value(QStringLiteral("selectionEnd")).toInt()
                    == normalizedCrlfText.size()
                 && crlfDragged.value(QStringLiteral("cursorPosition")).toInt()
                    == normalizedCrlfText.size()
                 && crlfDragUndone.value(QStringLiteral("text")).toString()
                    == crlfDragTarget
                 && crlfDragUndone.value(QStringLiteral("selectionStart")).toInt() == 2
                 && crlfDragUndone.value(QStringLiteral("selectionEnd")).toInt() == 5
                 && crlfDragUndone.value(QStringLiteral("cursorPosition")).toInt() == 5,
             QJsonObject{{QStringLiteral("expectedText"),
                          normalizedCrlfText + crlfDragTarget},
                         {QStringLiteral("expectedSelectionStart"), 0},
                         {QStringLiteral("expectedSelectionEnd"), normalizedCrlfText.size()},
                         {QStringLiteral("dropped"), crlfDragged},
                         {QStringLiteral("undone"), crlfDragUndone}});
    request(QStringLiteral("testDiscardClose"));
    QThread::msleep(150);
}

} // namespace EditingTests
