#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QThread>

#include <array>

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

QJsonObject keyPress(const QString &text = {}, const QString &key = {}, bool shift = false)
{
    return request(QStringLiteral("testKeyPress"),
                   {{QStringLiteral("text"), text},
                    {QStringLiteral("key"), key},
                    {QStringLiteral("shift"), shift}});
}

QJsonObject inputMethodCommit(const QString &text)
{
    return request(QStringLiteral("testInputMethodCommit"),
                   {{QStringLiteral("text"), text}});
}

QJsonObject formatAt(const QString &document, const QString &needle, int offset = 0)
{
    return request(QStringLiteral("testFormatAt"),
                   {{QStringLiteral("position"), document.indexOf(needle) + offset}});
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

    addCheck(checks, details, QStringLiteral("markdownStyleConfiguration"),
             initial.value(QStringLiteral("markdownStyleLoaded")).toBool()
                 && initial.value(QStringLiteral("markdownTextColor")).toString()
                    == QStringLiteral("#c2c0b6")
                 && initial.value(QStringLiteral("themeHeaderColor")).toString()
                    == QStringLiteral("#252525")
                 && initial.value(QStringLiteral("panelAccentColor")).toString()
                    == QStringLiteral("#85c7c0")
                 && initial.value(QStringLiteral("commandPaletteMaximumWidth")).toInt() == 620,
             initial);

    const QString markdown = QStringLiteral(
        "# 标题\n**粗体** 与 *斜体* 以及 `code`\n> 引用\n- 列表\n- [ ] 任务\n```cpp\nint x = 1;\n```\n[链接](https://example.invalid)");
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), markdown}});
    const QJsonObject highlight = request(QStringLiteral("testHighlightSummary"));
    addCheck(checks, details, QStringLiteral("markdownFormats"),
             highlight.value(QStringLiteral("formattedRanges")).toInt() >= 9
                 && highlight.value(QStringLiteral("fencedBlocks")).toInt() >= 1,
             highlight);

    const QString styledMarkdown = QStringLiteral(
        "# H1\n## H2\n### H3\n#### H4\n##### H5\n###### H6\n"
        "plain `code`\n```cpp\nfenced body\n```\n- item\n> quote\n"
        "**bold** *italic* ***both*** ~~gone~~\n"
        "[Title](URL)\n- [ ] todo\n- [x] done");
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), styledMarkdown}});

    const std::array<QString, 6> headingColors{
        QStringLiteral("#d04255"), QStringLiteral("#d5763f"),
        QStringLiteral("#e5b567"), QStringLiteral("#a8c373"),
        QStringLiteral("#6c99bb"), QStringLiteral("#9e86c8")};
    bool rainbowHeadings = true;
    for (int level = 1; level <= 6; ++level) {
        const QString headingText = QString(level, QLatin1Char('#'))
            + QStringLiteral(" H%1").arg(level);
        const QJsonObject format = formatAt(styledMarkdown, headingText, level + 1);
        rainbowHeadings = rainbowHeadings && hasColor(format, headingColors[level - 1])
            && format.value(QStringLiteral("bold")).toBool();
    }
    addCheck(checks, details, QStringLiteral("rainbowHeadingStyles"), rainbowHeadings);

    const QJsonObject inlineCodeStyle = formatAt(styledMarkdown, QStringLiteral("`code`"), 1);
    const QJsonObject fencedCodeStyle = formatAt(styledMarkdown, QStringLiteral("fenced body"));
    const QJsonObject fenceStyle = formatAt(styledMarkdown, QStringLiteral("```cpp"));
    const QJsonObject listStyle = formatAt(styledMarkdown, QStringLiteral("- item"));
    const QJsonObject quoteStyle = formatAt(styledMarkdown, QStringLiteral("> quote"), 2);
    addCheck(checks, details, QStringLiteral("structuralMarkdownStyles"),
             hasColor(inlineCodeStyle, QStringLiteral("#ffffff"))
                 && inlineCodeStyle.value(QStringLiteral("background")).toString()
                    == QStringLiteral("#303030")
                 && hasColor(fencedCodeStyle, QStringLiteral("#c2c0b6"))
                 && fencedCodeStyle.value(QStringLiteral("background")).toString()
                    == QStringLiteral("#303030")
                 && hasColor(fenceStyle, QStringLiteral("#c2c0b6"))
                 && hasColor(listStyle, QStringLiteral("#ffffff"))
                 && hasColor(quoteStyle, QStringLiteral("#999999"))
                 && quoteStyle.value(QStringLiteral("italic")).toBool(),
             inlineCodeStyle);

    const QJsonObject boldStyle = formatAt(styledMarkdown, QStringLiteral("**bold**"), 2);
    const QJsonObject italicStyle = formatAt(styledMarkdown, QStringLiteral("*italic*"), 1);
    const QJsonObject boldItalicStyle = formatAt(styledMarkdown, QStringLiteral("***both***"), 3);
    const QJsonObject strikeStyle = formatAt(styledMarkdown, QStringLiteral("~~gone~~"), 2);
    addCheck(checks, details, QStringLiteral("textDecorationStyles"),
             hasColor(boldStyle, QStringLiteral("#ffe6b7"))
                 && boldStyle.value(QStringLiteral("bold")).toBool()
                 && hasColor(italicStyle, QStringLiteral("#999999"))
                 && italicStyle.value(QStringLiteral("italic")).toBool()
                 && hasColor(boldItalicStyle, QStringLiteral("#ffe6b7"))
                 && boldItalicStyle.value(QStringLiteral("bold")).toBool()
                 && boldItalicStyle.value(QStringLiteral("italic")).toBool()
                 && hasColor(strikeStyle, QStringLiteral("#999999"))
                 && strikeStyle.value(QStringLiteral("strikeThrough")).toBool(),
             boldItalicStyle);

    const QJsonObject linkTextStyle = formatAt(styledMarkdown, QStringLiteral("Title"));
    const QJsonObject linkBracketStyle = formatAt(styledMarkdown, QStringLiteral("[Title"));
    const QJsonObject checkboxStyle = formatAt(styledMarkdown, QStringLiteral("[ ]"));
    const QJsonObject completedTaskStyle = formatAt(styledMarkdown, QStringLiteral("done"));
    addCheck(checks, details, QStringLiteral("linkAndTaskStyles"),
             hasColor(linkTextStyle, QStringLiteral("#85c7c0"))
                 && linkTextStyle.value(QStringLiteral("underline")).toBool()
                 && hasColor(linkBracketStyle, QStringLiteral("#999999"))
                 && hasColor(checkboxStyle, QStringLiteral("#4a4a4a"))
                 && hasColor(completedTaskStyle, QStringLiteral("#999999"))
                 && completedTaskStyle.value(QStringLiteral("strikeThrough")).toBool(),
             completedTaskStyle);

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

    bool allDirectHeadingLevels = true;
    for (int level = 1; level <= 6; ++level) {
        setTextAndSelection(QStringLiteral("one"), 0, 3);
        execute(QStringLiteral("setHeading%1").arg(level));
        allDirectHeadingLevels = allDirectHeadingLevels
            && editorText() == QString(level, QLatin1Char('#')) + QStringLiteral(" one");
    }

    setTextAndSelection(QStringLiteral("one\ntwo"), 0, 7);
    execute(QStringLiteral("setHeading3"));
    const bool headingThree = editorText() == QStringLiteral("### one\n### two");
    execute(QStringLiteral("increaseHeadingLevel"));
    const bool headingIncreased = editorText() == QStringLiteral("#### one\n#### two");
    execute(QStringLiteral("decreaseHeadingLevel"));
    addCheck(checks, details, QStringLiteral("directAndDirectionalHeadings"),
             allDirectHeadingLevels && headingThree && headingIncreased
                 && editorText() == QStringLiteral("### one\n### two"));

    setTextAndSelection(QStringLiteral("one\ntwo"), 0, 7);
    execute(QStringLiteral("toggleList"));
    const bool listOn = editorText() == QStringLiteral("- one\n- two");
    const QJsonObject listOff = execute(QStringLiteral("toggleList"));
    addCheck(checks, details, QStringLiteral("toggleList"),
             listOn && editorText() == QStringLiteral("one\ntwo"), listOff);

    setTextAndSelection(QStringLiteral("one\ntwo"), 0, 7);
    execute(QStringLiteral("toggleTask"));
    const bool taskOn = editorText() == QStringLiteral("- [ ] one\n- [ ] two");
    const QJsonObject taskOff = execute(QStringLiteral("toggleTask"));
    addCheck(checks, details, QStringLiteral("toggleTask"),
             taskOn && editorText() == QStringLiteral("one\ntwo"), taskOff);

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

    setTextAndSelection(QStringLiteral("***bold italic***"), 8, 8);
    const QJsonObject boldItalicTab = keyPress({}, QStringLiteral("Tab"));
    addCheck(checks, details, QStringLiteral("tabOutOfMarkdownEmphasis"),
             boldItalicTab.value(QStringLiteral("cursorPosition")).toInt() == 17,
             boldItalicTab);

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

    const QJsonObject deleteLineShortcut = request(
        QStringLiteral("testShortcut"),
        {{QStringLiteral("commandId"), QStringLiteral("deleteLine")}});
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
