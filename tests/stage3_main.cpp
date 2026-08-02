#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QThread>

namespace {

QString serverName()
{
    const QByteArray overrideName = qgetenv("SCRATCHEDITOR_SERVER_NAME");
    return overrideName.isEmpty() ? QStringLiteral("ScratchEditor.Stage1.v1")
                                  : QString::fromUtf8(overrideName);
}

QJsonObject request(const QString &command, QJsonObject arguments = {}, int timeoutMs = 3000)
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
    while (!response.contains('\n')) {
        if (!socket.waitForReadyRead(timeoutMs)) {
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("read timeout")}};
        }
        response += socket.readAll();
    }
    const QJsonDocument document = QJsonDocument::fromJson(response.left(response.indexOf('\n')));
    return document.isObject() ? document.object()
                               : QJsonObject{{QStringLiteral("ok"), false},
                                             {QStringLiteral("error"),
                                              QStringLiteral("invalid response")}};
}

bool setTextAndSelection(const QString &text, int start, int end)
{
    const QJsonObject set = request(QStringLiteral("testSetText"),
                                    {{QStringLiteral("text"), text}});
    const QJsonObject select = request(QStringLiteral("testSetSelection"),
                                       {{QStringLiteral("start"), start},
                                        {QStringLiteral("end"), end}});
    return set.value(QStringLiteral("ok")).toBool()
        && select.value(QStringLiteral("invoked")).toBool();
}

QJsonObject execute(const QString &commandId)
{
    return request(QStringLiteral("testExecuteCommand"),
                   {{QStringLiteral("commandId"), commandId}});
}

QString editorText()
{
    return request(QStringLiteral("testText")).value(QStringLiteral("text")).toString();
}

void addCheck(QJsonObject &checks, QJsonObject &details, const QString &name, bool passed,
              const QJsonValue &detail = {})
{
    checks.insert(name, passed);
    if (!detail.isUndefined()) {
        details.insert(name, detail);
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QJsonObject checks;
    QJsonObject details;

    const QJsonObject initial = request(QStringLiteral("status"));
    addCheck(checks, details, QStringLiteral("registryAndHighlighter"),
             initial.value(QStringLiteral("ok")).toBool()
                 && initial.value(QStringLiteral("testMode")).toBool()
                 && initial.value(QStringLiteral("commandCount")).toInt() >= 10
                 && initial.value(QStringLiteral("markdownHighlighting")).toBool(),
             initial);

    const QString markdown = QStringLiteral(
        "# 标题\n**粗体** 与 *斜体* 以及 `code`\n> 引用\n- 列表\n- [ ] 任务\n```cpp\nint x = 1;\n```\n[链接](https://example.invalid)");
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), markdown}});
    const QJsonObject highlight = request(QStringLiteral("testHighlightSummary"));
    addCheck(checks, details, QStringLiteral("markdownFormats"),
             highlight.value(QStringLiteral("formattedRanges")).toInt() >= 9
                 && highlight.value(QStringLiteral("fencedBlocks")).toInt() >= 1,
             highlight);

    setTextAndSelection(QStringLiteral("alpha"), 0, 5);
    const QJsonObject boldOn = execute(QStringLiteral("toggleBold"));
    const bool boldOnPassed = boldOn.value(QStringLiteral("executed")).toBool()
        && editorText() == QStringLiteral("**alpha**")
        && boldOn.value(QStringLiteral("selectionStart")).toInt() == 2
        && boldOn.value(QStringLiteral("selectionEnd")).toInt() == 7;
    execute(QStringLiteral("toggleBold"));
    addCheck(checks, details, QStringLiteral("toggleBold"),
             boldOnPassed && editorText() == QStringLiteral("alpha"), boldOn);

    setTextAndSelection(QStringLiteral("alpha"), 0, 5);
    execute(QStringLiteral("toggleItalic"));
    const bool italicOn = editorText() == QStringLiteral("*alpha*");
    execute(QStringLiteral("toggleItalic"));
    addCheck(checks, details, QStringLiteral("toggleItalic"),
             italicOn && editorText() == QStringLiteral("alpha"));

    setTextAndSelection(QStringLiteral("one\ntwo"), 0, 7);
    execute(QStringLiteral("cycleHeading"));
    const bool headingOne = editorText() == QStringLiteral("# one\n# two");
    execute(QStringLiteral("cycleHeading"));
    addCheck(checks, details, QStringLiteral("cycleHeading"),
             headingOne && editorText() == QStringLiteral("## one\n## two"));

    setTextAndSelection(QStringLiteral("one\ntwo"), 0, 7);
    execute(QStringLiteral("toggleList"));
    const bool listOn = editorText() == QStringLiteral("- one\n- two");
    execute(QStringLiteral("toggleList"));
    addCheck(checks, details, QStringLiteral("toggleList"),
             listOn && editorText() == QStringLiteral("one\ntwo"));

    setTextAndSelection(QStringLiteral("one\ntwo"), 0, 7);
    execute(QStringLiteral("toggleTask"));
    const bool taskOn = editorText() == QStringLiteral("- [ ] one\n- [ ] two");
    execute(QStringLiteral("toggleTask"));
    addCheck(checks, details, QStringLiteral("toggleTask"),
             taskOn && editorText() == QStringLiteral("one\ntwo"));

    setTextAndSelection(QStringLiteral("one\ntwo"), 0, 7);
    execute(QStringLiteral("toggleQuote"));
    const bool quoteOn = editorText() == QStringLiteral("> one\n> two");
    execute(QStringLiteral("toggleQuote"));
    addCheck(checks, details, QStringLiteral("toggleQuote"),
             quoteOn && editorText() == QStringLiteral("one\ntwo"));

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
