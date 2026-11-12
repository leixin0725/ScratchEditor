#pragma once

#include <QJsonObject>
#include <QStringList>

#include <functional>

namespace EditingTests {

QJsonObject request(const QString &command, QJsonObject arguments = {}, int timeoutMs = 5000);
bool setTextAndSelection(const QString &text, int start, int end, int cursor = -1);
QJsonObject editorStatus();
QJsonObject headingFoldState();
QJsonObject setScrollY(double contentY);
QJsonObject execute(const QString &commandId);
QJsonObject dragSelection(int start, int end, int dropPosition);
QJsonObject nativeSelectionDrag(const QString &phase, const QString &edge,
                                int position = 0);
QJsonObject dragClipboardHistory(const QString &id, int dropPosition,
                                 bool outsideEditor = false);
QJsonObject tripleClick(int position);
QJsonObject keyPress(const QString &text = {}, const QString &key = {}, bool shift = false,
                     const QString &modifiers = {});
QJsonObject doubleClick(int position);
QJsonObject clipboardText();
QJsonObject setClipboard(const QString &text);
QJsonObject inputMethodCommit(const QString &text);
QJsonObject dropUrls(const QStringList &urls);
QString droppedPathText(const QString &path);
QJsonObject formatAt(const QString &document, const QString &needle, int offset = 0);
QJsonObject formatAtPosition(int position);
bool hasColor(const QJsonObject &format, const QString &color);
QString editorText();
void addCheck(QJsonObject &checks, QJsonObject &details, const QString &name, bool passed,
              const QJsonValue &detail = {});
void addCjkCheck(QJsonObject &checks, QJsonObject &details, const QString &name,
                 bool passed, const QString &input, const QString &action,
                 const QString &expected, const QString &actual,
                 const QJsonObject &status = {});
void cjkExpect(QJsonObject &checks, QJsonObject &details, const QString &name,
               const QString &input, int start, int end, const QString &action,
               const std::function<QJsonObject()> &perform,
               const QString &expectedText, int expectedCursor = -1,
               int expectedSelectionStart = -1, int expectedSelectionEnd = -1);
void undoCursorExpect(QJsonObject &checks, QJsonObject &details, const QString &name,
                      QString input, const std::function<void()> &perform,
                      const QString &expectedEditedText, int undoPosition = -1,
                      int selectionAnchor = -1);
void formatExpect(QJsonObject &checks, QJsonObject &details, const QString &name,
                  const QString &input, int start, int end, int cursor,
                  const QString &expectedText, int expectedCursor = -1,
                  int expectedSelectionStart = -1, int expectedSelectionEnd = -1);
std::function<QJsonObject()> keyAction(const QString &text);

} // namespace EditingTests
