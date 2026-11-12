#include "suites.h"
#include "support.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QThread>
#include <cmath>

namespace EditingTests {

void checkFindAndShortcuts(QJsonObject &checks, QJsonObject &details, const QJsonObject &initial,
                         const QString &headingScrollText)
{
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), QStringLiteral("Alpha beta alpha")}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}});
    const QJsonObject firstFind = request(QStringLiteral("testFindNext"),
                                          {{QStringLiteral("query"), QStringLiteral("alpha")},
                                           {QStringLiteral("caseSensitive"), false}});
    const QJsonObject secondFind = request(QStringLiteral("testFindNext"),
                                           {{QStringLiteral("query"), QStringLiteral("alpha")},
                                            {QStringLiteral("caseSensitive"), false}});
    const QJsonObject replaceCurrent = request(
        QStringLiteral("testReplaceCurrent"),
        {{QStringLiteral("query"), QStringLiteral("alpha")},
         {QStringLiteral("replacement"), QStringLiteral("X")},
         {QStringLiteral("caseSensitive"), false}});
    addCheck(checks, details, QStringLiteral("findAndReplaceCurrent"),
             firstFind.value(QStringLiteral("found")).toBool()
                 && firstFind.value(QStringLiteral("selectionStart")).toInt() == 0
                 && secondFind.value(QStringLiteral("selectionStart")).toInt() == 11
                 && replaceCurrent.value(QStringLiteral("replaced")).toBool()
                 && replaceCurrent.value(QStringLiteral("text")).toString()
                    == QStringLiteral("Alpha beta X"),
             replaceCurrent);

    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), QStringLiteral("cat Cat cat")}});
    const QJsonObject replaceAll = request(
        QStringLiteral("testReplaceAll"),
        {{QStringLiteral("query"), QStringLiteral("cat")},
         {QStringLiteral("replacement"), QStringLiteral("dog")},
         {QStringLiteral("caseSensitive"), false}});
    addCheck(checks, details, QStringLiteral("replaceAll"),
             replaceAll.value(QStringLiteral("replacementCount")).toInt() == 3
                 && replaceAll.value(QStringLiteral("text")).toString()
                    == QStringLiteral("dog dog dog"),
             replaceAll);

    const QJsonObject palette = execute(QStringLiteral("commandPalette"));
    const QJsonObject findPanel = execute(QStringLiteral("find"));
    addCheck(checks, details, QStringLiteral("lazyCommandPalette"),
             !initial.value(QStringLiteral("commandPaletteLoaded")).toBool()
                 && palette.value(QStringLiteral("commandPaletteLoaded")).toBool(),
             palette);
    addCheck(checks, details, QStringLiteral("findPanel"),
             findPanel.value(QStringLiteral("findPanelVisible")).toBool(), findPanel);
    const QJsonObject noPreview = execute(QStringLiteral("togglePreview"));
    addCheck(checks, details, QStringLiteral("previewExcluded"),
             !noPreview.value(QStringLiteral("executed")).toBool(), noPreview);
    request(QStringLiteral("testCloseOverlays"));

    // 同一命中在手动滚走后仍应显式定位，不能依赖光标变化通知。
    setTextAndSelection(headingScrollText, 0, 0);
    execute(QStringLiteral("find"));
    QThread::msleep(80);
    const auto findB = [] {
        return request(QStringLiteral("testFindNext"),
                       {{QStringLiteral("query"), QStringLiteral("# B")}});
    };
    findB();
    QThread::msleep(240);
    const QJsonObject searchLocated = editorStatus();
    addCheck(checks, details, QStringLiteral("findPanelReservesViewportSpace"),
             searchLocated.value(QStringLiteral("scrollViewportY")).toDouble()
                 > searchLocated.value(QStringLiteral("findPanelBottom")).toDouble(),
             searchLocated);
    setScrollY(0);
    findB();
    QThread::msleep(240);
    const QJsonObject searchRelocated = editorStatus();
    addCheck(checks, details, QStringLiteral("findSameMatchRestoresVisibility"),
             searchRelocated.value(QStringLiteral("scrollContentY")).toDouble() > 0
                 && std::abs(searchLocated.value(QStringLiteral("scrollContentY")).toDouble()
                             - searchRelocated.value(QStringLiteral("scrollContentY")).toDouble()) < 3,
             QJsonObject{{QStringLiteral("expected"), searchLocated},
                         {QStringLiteral("actual"), searchRelocated}});
    addCheck(checks, details, QStringLiteral("findPreservesPanelFocus"),
             !searchLocated.value(QStringLiteral("editorHasFocus")).toBool(true),
             searchLocated);
    findB();
    QThread::msleep(240);
    const QJsonObject searchVisible = editorStatus();
    addCheck(checks, details, QStringLiteral("findVisibleMatchKeepsViewport"),
             std::abs(searchVisible.value(QStringLiteral("scrollContentY")).toDouble()
                      - searchRelocated.value(QStringLiteral("scrollContentY")).toDouble()) < 1,
             QJsonObject{{QStringLiteral("expected"), searchRelocated},
                         {QStringLiteral("actual"), searchVisible}});
    request(QStringLiteral("testCloseOverlays"));

    setTextAndSelection(QStringLiteral("中文😀 match\n正文\nmatch"), 0, 0);
    execute(QStringLiteral("find"));
    QThread::msleep(80);
    const auto panelKey = [](const QString &text, const QString &key, bool shift = false) {
        return request(QStringLiteral("testKeyPress"),
                       {{QStringLiteral("text"), text}, {QStringLiteral("key"), key},
                        {QStringLiteral("shift"), shift}, {QStringLiteral("activeFocus"), true}});
    };
    panelKey(QStringLiteral("match"), {});
    panelKey({}, QStringLiteral("Enter"));
    const QJsonObject panelFirst = editorStatus();
    panelKey({}, QStringLiteral("Enter"));
    const QJsonObject panelSecond = editorStatus();
    panelKey({}, QStringLiteral("Enter"), true);
    const QJsonObject panelPrevious = editorStatus();
    addCheck(checks, details, QStringLiteral("findPanelEnterNavigatesWithoutEditing"),
             panelFirst.value(QStringLiteral("selectionStart")).toInt() == 5
                 && panelSecond.value(QStringLiteral("selectionStart")).toInt() == 14
                 && panelPrevious.value(QStringLiteral("selectionStart")).toInt() == 5
                 && !panelPrevious.value(QStringLiteral("editorHasFocus")).toBool()
                 && editorText() == QStringLiteral("中文😀 match\n正文\nmatch"),
             QJsonObject{{QStringLiteral("expectedStarts"), QJsonArray{5, 14, 5}},
                         {QStringLiteral("first"), panelFirst},
                         {QStringLiteral("second"), panelSecond},
                         {QStringLiteral("previous"), panelPrevious}});
    panelKey({}, QStringLiteral("Escape"));
    const QJsonObject panelClosed = editorStatus();
    addCheck(checks, details, QStringLiteral("findPanelEscapeRestoresEditorFocus"),
             !panelClosed.value(QStringLiteral("findPanelVisible")).toBool()
                 && panelClosed.value(QStringLiteral("editorHasFocus")).toBool()
                 && panelClosed.value(QStringLiteral("selectionStart")).toInt() == 5,
             panelClosed);

    const QString selectedQuery = QStringLiteral("单行😀 query");
    const QString selectionPrefillText = selectedQuery + QStringLiteral("\n第二行");
    setTextAndSelection(selectionPrefillText, 0, selectedQuery.size());
    const QJsonObject selectedFindPanel = execute(QStringLiteral("find"));
    addCheck(checks, details, QStringLiteral("findPanelPrefillsSingleLineSelection"),
             selectedFindPanel.value(QStringLiteral("findQueryText")).toString()
                 == selectedQuery,
             QJsonObject{{QStringLiteral("actual"),
                          selectedFindPanel.value(QStringLiteral("findQueryText"))},
                         {QStringLiteral("expected"), selectedQuery}});
    request(QStringLiteral("testCloseOverlays"));

    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 0},
             {QStringLiteral("end"), selectedQuery.size() + 1}});
    const QJsonObject multilineReplacePanel = execute(QStringLiteral("replace"));
    addCheck(checks, details, QStringLiteral("findPanelRejectsMultilineSelectionPrefill"),
             multilineReplacePanel.value(QStringLiteral("findQueryText")).toString()
                 == selectedQuery,
             QJsonObject{{QStringLiteral("actual"),
                          multilineReplacePanel.value(QStringLiteral("findQueryText"))},
                         {QStringLiteral("expected"), selectedQuery}});
    request(QStringLiteral("testCloseOverlays"));

    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 0},
             {QStringLiteral("end"), selectedQuery.size()},
             {QStringLiteral("cursor"), 0}});
    const QJsonObject reverseSelectionReplacePanel = execute(QStringLiteral("replace"));
    addCheck(checks, details, QStringLiteral("findPanelPrefillsReverseSelection"),
             reverseSelectionReplacePanel.value(QStringLiteral("findQueryText")).toString()
                 == selectedQuery,
             reverseSelectionReplacePanel);
    request(QStringLiteral("testFindPanelUiAction"),
            {{QStringLiteral("action"), QStringLiteral("focusEditor")}});
    const QJsonObject editorFocusedWithFind = editorStatus();
    panelKey({}, QStringLiteral("Escape"));
    const QJsonObject editorFocusEscape = editorStatus();
    addCheck(checks, details, QStringLiteral("findPanelEscapeWorksFromEditorFocus"),
             editorFocusedWithFind.value(QStringLiteral("findPanelVisible")).toBool()
                 && editorFocusedWithFind.value(QStringLiteral("editorHasFocus")).toBool()
                 && !editorFocusEscape.value(QStringLiteral("findPanelVisible")).toBool()
                 && editorFocusEscape.value(QStringLiteral("editorHasFocus")).toBool(),
             QJsonObject{{QStringLiteral("before"), editorFocusedWithFind},
                         {QStringLiteral("after"), editorFocusEscape}});

    const QJsonObject deleteLineShortcut = request(
        QStringLiteral("testShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("deleteLine")}});
    const QJsonObject checkboxShortcut = request(
        QStringLiteral("testShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("toggleCheckbox")}});
    const QJsonObject oldHeadingShortcut = request(
        QStringLiteral("testShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("cycleHeading")}});
    const QJsonObject headingOneShortcut = request(
        QStringLiteral("testShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("setHeading1")}});
    bool allHeadingShortcuts = true;
    for (int level = 1; level <= 6; ++level) {
        const QJsonObject shortcutResponse = request(
            QStringLiteral("testShortcut"),
            {{QStringLiteral("commandId"), QStringLiteral("setHeading%1").arg(level)}});
        allHeadingShortcuts = allHeadingShortcuts
            && shortcutResponse.value(QStringLiteral("shortcut")).toString()
                == QStringLiteral("Ctrl+Num+%1").arg(level);
    }
    const QJsonObject headingIncreaseShortcut = request(
        QStringLiteral("testShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("increaseHeadingLevel")}});
    const QJsonObject headingDecreaseShortcut = request(
        QStringLiteral("testSetShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("decreaseHeadingLevel")},
         {QStringLiteral("sequence"), QStringLiteral("Ctrl+Num++")}});
    addCheck(checks, details, QStringLiteral("newDefaultShortcuts"),
             deleteLineShortcut.value(QStringLiteral("shortcut")).toString()
                    == QStringLiteral("Ctrl+Shift+L")
                 && checkboxShortcut.value(QStringLiteral("shortcut")).toString()
                    == QStringLiteral("Ctrl+L")
                 && oldHeadingShortcut.value(QStringLiteral("shortcut")).toString().isEmpty()
                 && allHeadingShortcuts
                 && headingOneShortcut.value(QStringLiteral("shortcut")).toString()
                    == QStringLiteral("Ctrl+Num+1")
                 && headingIncreaseShortcut.value(QStringLiteral("shortcut")).toString()
                    == QStringLiteral("Ctrl+Num+-")
                 && headingDecreaseShortcut.value(QStringLiteral("configured")).toBool()
                 && headingDecreaseShortcut.value(QStringLiteral("shortcut")).toString()
                    == QStringLiteral("Ctrl+Num++"),
             headingDecreaseShortcut);

    const QJsonObject shortcut = request(
        QStringLiteral("testSetShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("toggleBold")},
         {QStringLiteral("sequence"), QStringLiteral("Ctrl+Alt+B")}});
    const QJsonObject conflict = request(
        QStringLiteral("testSetShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("toggleItalic")},
         {QStringLiteral("sequence"), QStringLiteral("Ctrl+Alt+B")}});
    addCheck(checks, details, QStringLiteral("configurableShortcuts"),
             shortcut.value(QStringLiteral("configured")).toBool()
                 && shortcut.value(QStringLiteral("shortcut")).toString()
                    == QStringLiteral("Ctrl+Alt+B")
                 && !conflict.value(QStringLiteral("configured")).toBool()
                 && conflict.value(QStringLiteral("shortcut")).toString()
                    == QStringLiteral("Ctrl+I"),
             shortcut);

    // --- 清空整个编辑区命令（默认 Alt+X） ---
    const QJsonObject clearShortcutDefault = request(
        QStringLiteral("testShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("clearDocument")}});
    setTextAndSelection(QStringLiteral("第一行\n第二行\n第三行"), 4, 4);
    const QJsonObject cleared = execute(QStringLiteral("clearDocument"));
    const QJsonObject clearUndo = request(QStringLiteral("testUndo"));
    const QJsonObject clearRedo = request(QStringLiteral("testRedo"));
    setTextAndSelection(QString(), 0, 0);
    const QJsonObject clearEmpty = execute(QStringLiteral("clearDocument"));
    addCheck(checks, details, QStringLiteral("clearDocumentCommand"),
             clearShortcutDefault.value(QStringLiteral("shortcut")).toString()
                    == QStringLiteral("Alt+X")
                 && cleared.value(QStringLiteral("executed")).toBool()
                 && cleared.value(QStringLiteral("text")).toString().isEmpty()
                 && cleared.value(QStringLiteral("cursorPosition")).toInt() == 0
                 && cleared.value(QStringLiteral("selectionStart")).toInt() == 0
                 && cleared.value(QStringLiteral("selectionEnd")).toInt() == 0
                 && clearUndo.value(QStringLiteral("text")).toString()
                    == QStringLiteral("第一行\n第二行\n第三行")
                 && clearRedo.value(QStringLiteral("text")).toString().isEmpty()
                 && clearEmpty.value(QStringLiteral("executed")).toBool()
                 && clearEmpty.value(QStringLiteral("text")).toString().isEmpty(),
             cleared);

    const QJsonObject clearConflict = request(
        QStringLiteral("testSetShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("clearDocument")},
         {QStringLiteral("sequence"), QStringLiteral("Ctrl+Alt+C")}});
    const QJsonObject clearShortcutSet = request(
        QStringLiteral("testSetShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("clearDocument")},
         {QStringLiteral("sequence"), QStringLiteral("Ctrl+Alt+Q")}});
    const QJsonObject clearShortcutRestored = request(
        QStringLiteral("testSetShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("clearDocument")},
         {QStringLiteral("sequence"), QStringLiteral("Alt+X")}});
    addCheck(checks, details, QStringLiteral("clearDocumentShortcutConfigurable"),
             !clearConflict.value(QStringLiteral("configured")).toBool()
                 && clearShortcutSet.value(QStringLiteral("configured")).toBool()
                 && clearShortcutSet.value(QStringLiteral("shortcut")).toString()
                    == QStringLiteral("Ctrl+Alt+Q")
                 && clearShortcutRestored.value(QStringLiteral("configured")).toBool()
                 && clearShortcutRestored.value(QStringLiteral("shortcut")).toString()
                    == QStringLiteral("Alt+X"),
             clearShortcutSet);
}

} // namespace EditingTests
