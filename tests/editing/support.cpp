#include "support.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStringList>
#include <QThread>
#include <functional>

namespace EditingTests {

static QString serverName()
{
    const QByteArray overrideName = qgetenv("SCRATCHEDITOR_SERVER_NAME");
    return overrideName.isEmpty() ? QStringLiteral("ScratchEditor.Editing.Validation")
                                  : QString::fromUtf8(overrideName);
}

QJsonObject request(const QString &command, QJsonObject arguments, int timeoutMs)
{
    arguments.insert(QStringLiteral("command"), command);
    QLocalSocket socket;
    socket.connectToServer(serverName(), QIODevice::ReadWrite);
    if (!socket.waitForConnected(timeoutMs)) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), socket.errorString()}};
    }
    socket.write(QJsonDocument(arguments).toJson(QJsonDocument::Compact) + '\n');
    if (!socket.waitForBytesWritten(timeoutMs)) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("write timeout")}};
    }
    QByteArray response;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 15000;
    while (!response.contains('\n')) {
        const qint64 remaining = deadline - QDateTime::currentMSecsSinceEpoch();
        if (remaining <= 0) {
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("read timeout")}};
        }
        socket.waitForReadyRead(qMin<qint64>(remaining, 250));
        response += socket.readAll();
    }
    const QJsonDocument document = QJsonDocument::fromJson(response.left(response.indexOf('\n')));
    return document.isObject() ? document.object()
                               : QJsonObject{{QStringLiteral("ok"), false},
                                             {QStringLiteral("error"),
                                              QStringLiteral("invalid response")}};
}

bool setTextAndSelection(const QString &text, int start, int end, int cursor)
{
    const QJsonObject set = request(QStringLiteral("testSetText"),
                                    {{QStringLiteral("text"), text}});
    QJsonObject selectArguments{{QStringLiteral("start"), start},
                                {QStringLiteral("end"), end}};
    if (cursor >= 0) {
        selectArguments.insert(QStringLiteral("cursor"), cursor);
    }
    const QJsonObject select = request(QStringLiteral("testSetSelection"), selectArguments);
    return set.value(QStringLiteral("ok")).toBool()
        && select.value(QStringLiteral("invoked")).toBool();
}

QJsonObject editorStatus()
{
    return request(QStringLiteral("status"));
}

QJsonObject headingFoldState()
{
    return request(QStringLiteral("testHeadingFoldState"));
}

QJsonObject setScrollY(double contentY)
{
    return request(QStringLiteral("testSetScrollY"),
                   {{QStringLiteral("contentY"), contentY}});
}

QJsonObject execute(const QString &commandId)
{
    return request(QStringLiteral("testExecuteCommand"),
                   {{QStringLiteral("commandId"), commandId}});
}

QJsonObject dragSelection(int start, int end, int dropPosition)
{
    return request(QStringLiteral("testDragSelection"),
                   {{QStringLiteral("start"), start},
                    {QStringLiteral("end"), end},
                    {QStringLiteral("dropPosition"), dropPosition}});
}

QJsonObject nativeSelectionDrag(const QString &phase, const QString &edge,
                                int position)
{
    return request(QStringLiteral("testNativeSelectionDrag"),
                   {{QStringLiteral("phase"), phase},
                    {QStringLiteral("edge"), edge},
                    {QStringLiteral("position"), position}});
}

QJsonObject dragClipboardHistory(const QString &id, int dropPosition,
                                 bool outsideEditor)
{
    return request(QStringLiteral("testDragClipboardHistory"),
                   {{QStringLiteral("id"), id},
                    {QStringLiteral("dropPosition"), dropPosition},
                    {QStringLiteral("outsideEditor"), outsideEditor}});
}

QJsonObject tripleClick(int position)
{
    return request(QStringLiteral("testTripleClick"),
                   {{QStringLiteral("position"), position}});
}

QJsonObject keyPress(const QString &text, const QString &key, bool shift,
                     const QString &modifiers)
{
    return request(QStringLiteral("testKeyPress"),
                   {{QStringLiteral("text"), text},
                    {QStringLiteral("key"), key},
                    {QStringLiteral("shift"), shift},
                    {QStringLiteral("modifiers"), modifiers}});
}

QJsonObject doubleClick(int position)
{
    return request(QStringLiteral("testDoubleClick"),
                   {{QStringLiteral("position"), position}});
}

QJsonObject clipboardText()
{
    return request(QStringLiteral("testClipboard"));
}

QJsonObject setClipboard(const QString &text)
{
    return request(QStringLiteral("testSetClipboard"),
                   {{QStringLiteral("text"), text}});
}

QJsonObject inputMethodCommit(const QString &text)
{
    return request(QStringLiteral("testInputMethodCommit"),
                   {{QStringLiteral("text"), text}});
}

QJsonObject dropUrls(const QStringList &urls)
{
    QJsonArray values;
    for (const QString &url : urls) {
        values.append(url);
    }
    return request(QStringLiteral("testInsertDroppedUrls"),
                   {{QStringLiteral("urls"), values}});
}

QString droppedPathText(const QString &path)
{
    const QString absolutePath = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    for (const QChar character : absolutePath) {
        if (character.isSpace()) {
            return QStringLiteral("\"") + absolutePath + QStringLiteral("\"");
        }
    }
    return absolutePath;
}

QJsonObject formatAt(const QString &document, const QString &needle, int offset)
{
    return request(QStringLiteral("testFormatAt"),
                   {{QStringLiteral("position"), document.indexOf(needle) + offset}});
}

QJsonObject formatAtPosition(int position)
{
    return request(QStringLiteral("testFormatAt"),
                   {{QStringLiteral("position"), position}});
}

bool hasColor(const QJsonObject &format, const QString &color)
{
    return format.value(QStringLiteral("formatted")).toBool()
        && format.value(QStringLiteral("foreground")).toString()
            == color.toLower();
}

QString editorText()
{
    return request(QStringLiteral("testText")).value(QStringLiteral("text")).toString();
}

void addCheck(QJsonObject &checks, QJsonObject &details, const QString &name, bool passed,
              const QJsonValue &detail)
{
    checks.insert(name, passed);
    if (!detail.isUndefined()) {
        details.insert(name, detail);
    }
}

void addCjkCheck(QJsonObject &checks, QJsonObject &details, const QString &name,
                 bool passed, const QString &input, const QString &action,
                 const QString &expected, const QString &actual,
                 const QJsonObject &status)
{
    QJsonObject detail{
        {QStringLiteral("input"), input},
        {QStringLiteral("action"), action},
        {QStringLiteral("expected"), expected},
        {QStringLiteral("actual"), actual},
    };
    if (!status.isEmpty()) {
        detail.insert(QStringLiteral("status"), status);
    }
    addCheck(checks, details, name, passed, detail);
}

void cjkExpect(QJsonObject &checks, QJsonObject &details, const QString &name,
               const QString &input, int start, int end, const QString &action,
               const std::function<QJsonObject()> &perform,
               const QString &expectedText, int expectedCursor,
               int expectedSelectionStart, int expectedSelectionEnd)
{
    setTextAndSelection(input, start, end);
    perform();
    QThread::msleep(30);
    const QString actualText = editorText();
    const QJsonObject status = editorStatus();
    const bool textOk = actualText == expectedText;
    const bool cursorOk = expectedCursor < 0
        || status.value(QStringLiteral("cursorPosition")).toInt() == expectedCursor;
    const bool selectionOk = (expectedSelectionStart < 0
                              || status.value(QStringLiteral("selectionStart")).toInt()
                                  == expectedSelectionStart)
        && (expectedSelectionEnd < 0
            || status.value(QStringLiteral("selectionEnd")).toInt()
                == expectedSelectionEnd);
    addCjkCheck(checks, details, name, textOk && cursorOk && selectionOk,
                input, action, expectedText, actualText, status);
}

// 同时检查第一次及重做后的撤销，避免单次选区快照掩盖错误的事务光标。
void undoCursorExpect(QJsonObject &checks, QJsonObject &details, const QString &name,
                      QString input, const std::function<void()> &perform,
                      const QString &expectedEditedText, int undoPosition,
                      int selectionAnchor)
{
    const int position = input.indexOf(QLatin1Char('|'));
    input.remove(position, 1);
    if (undoPosition < 0) {
        undoPosition = position;
    }
    if (selectionAnchor >= 0) {
        setTextAndSelection(input, qMin(position, selectionAnchor),
                            qMax(position, selectionAnchor), position);
    } else {
        setTextAndSelection(input, position, position);
    }
    perform();
    QThread::msleep(30);
    const QString edited = editorText();
    addCheck(checks, details, name + QStringLiteral("Edit"), edited == expectedEditedText,
             QJsonObject{{QStringLiteral("actualText"), edited},
                         {QStringLiteral("expectedText"), expectedEditedText}});
    for (int cycle = 0; cycle < 2; ++cycle) {
        const QJsonObject undone = request(QStringLiteral("testUndo"));
        const QString actualText = editorText();
        addCheck(checks, details, name + QStringLiteral("Undo%1").arg(cycle + 1),
                 actualText == input
                     && undone.value(QStringLiteral("cursorPosition")).toInt() == undoPosition,
                 QJsonObject{{QStringLiteral("actualText"), actualText},
                             {QStringLiteral("expectedText"), input},
                             {QStringLiteral("actualCursor"), undone.value(QStringLiteral("cursorPosition"))},
                             {QStringLiteral("expectedCursor"), undoPosition}});
        if (cycle == 0) {
            request(QStringLiteral("testRedo"));
            const QString redone = editorText();
            addCheck(checks, details, name + QStringLiteral("Redo"), redone == expectedEditedText,
                     QJsonObject{{QStringLiteral("actualText"), redone},
                                 {QStringLiteral("expectedText"), expectedEditedText}});
        }
    }
}

void formatExpect(QJsonObject &checks, QJsonObject &details, const QString &name,
                  const QString &input, int start, int end, int cursor,
                  const QString &expectedText, int expectedCursor,
                  int expectedSelectionStart, int expectedSelectionEnd)
{
    setTextAndSelection(input, start, end, cursor);
    execute(QStringLiteral("formatSpacing"));
    const QString actualText = editorText();
    const QJsonObject status = editorStatus();
    const bool textOk = actualText == expectedText;
    const bool cursorOk = expectedCursor < 0
        || status.value(QStringLiteral("cursorPosition")).toInt() == expectedCursor;
    const bool selectionOk = (expectedSelectionStart < 0
                              || status.value(QStringLiteral("selectionStart")).toInt()
                                  == expectedSelectionStart)
        && (expectedSelectionEnd < 0
            || status.value(QStringLiteral("selectionEnd")).toInt()
                == expectedSelectionEnd);
    addCjkCheck(checks, details, name, textOk && cursorOk && selectionOk,
                input, QStringLiteral("Alt+F / formatSpacing"), expectedText,
                actualText, status);
}

std::function<QJsonObject()> keyAction(const QString &text)
{
    return [text] { return keyPress(text, text); };
}

} // namespace EditingTests
