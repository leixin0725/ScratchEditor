#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStringList>
#include <QThread>

#include "cjktextprocessor.h"

#include <array>
#include <functional>
#include <utility>

namespace {

QString serverName()
{
    const QByteArray overrideName = qgetenv("SCRATCHEDITOR_SERVER_NAME");
    return overrideName.isEmpty() ? QStringLiteral("ScratchEditor.Editing.Validation")
                                  : QString::fromUtf8(overrideName);
}

QJsonObject request(const QString &command, QJsonObject arguments = {}, int timeoutMs = 5000)
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

bool setTextAndSelection(const QString &text, int start, int end, int cursor = -1)
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

QJsonObject tripleClick(int position)
{
    return request(QStringLiteral("testTripleClick"),
                   {{QStringLiteral("position"), position}});
}

QJsonObject keyPress(const QString &text = {}, const QString &key = {}, bool shift = false,
                     const QString &modifiers = {})
{
    return request(QStringLiteral("testKeyPress"),
                   {{QStringLiteral("text"), text},
                    {QStringLiteral("key"), key},
                    {QStringLiteral("shift"), shift},
                    {QStringLiteral("modifiers"), modifiers}});
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

QJsonObject formatAt(const QString &document, const QString &needle, int offset = 0)
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
              const QJsonValue &detail = {})
{
    checks.insert(name, passed);
    if (!detail.isUndefined()) {
        details.insert(name, detail);
    }
}

void addCjkCheck(QJsonObject &checks, QJsonObject &details, const QString &name,
                 bool passed, const QString &input, const QString &action,
                 const QString &expected, const QString &actual,
                 const QJsonObject &status = {})
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
               const QString &expectedText, int expectedCursor = -1,
               int expectedSelectionStart = -1, int expectedSelectionEnd = -1)
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

void formatExpect(QJsonObject &checks, QJsonObject &details, const QString &name,
                  const QString &input, int start, int end, int cursor,
                  const QString &expectedText, int expectedCursor = -1,
                  int expectedSelectionStart = -1, int expectedSelectionEnd = -1)
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

struct ExpectedSpan {
    int outerStart = 0;
    int outerEnd = 0;
    int contentStart = 0;
    int contentEnd = 0;
    CjkText::ProtectedKind kind = CjkText::ProtectedKind::FencedCode;
};

bool spansMatch(const QVector<CjkText::ProtectedSpan> &actual,
                const QVector<ExpectedSpan> &expected)
{
    if (actual.size() != expected.size()) {
        return false;
    }
    for (int i = 0; i < actual.size(); ++i) {
        const CjkText::ProtectedSpan &span = actual.at(i);
        const ExpectedSpan &wanted = expected.at(i);
        if (span.outerStart != wanted.outerStart
            || span.outerEnd != wanted.outerEnd
            || span.contentStart != wanted.contentStart
            || span.contentEnd != wanted.contentEnd
            || span.kind != wanted.kind) {
            return false;
        }
    }
    return true;
}

QString spanSummary(const QVector<CjkText::ProtectedSpan> &spans)
{
    QStringList parts;
    for (const CjkText::ProtectedSpan &span : spans) {
        parts << QStringLiteral("[%1,%2) content[%3,%4) kind=%5")
            .arg(span.outerStart).arg(span.outerEnd)
            .arg(span.contentStart).arg(span.contentEnd)
            .arg(static_cast<int>(span.kind));
    }
    return parts.join(QStringLiteral("; "));
}

QString expectedSpanSummary(const QVector<ExpectedSpan> &spans)
{
    QStringList parts;
    for (const ExpectedSpan &span : spans) {
        parts << QStringLiteral("[%1,%2) content[%3,%4) kind=%5")
            .arg(span.outerStart).arg(span.outerEnd)
            .arg(span.contentStart).arg(span.contentEnd)
            .arg(static_cast<int>(span.kind));
    }
    return parts.join(QStringLiteral("; "));
}

void parseExpect(QJsonObject &checks, QJsonObject &details, const QString &name,
                 const QString &text, const QVector<ExpectedSpan> &expectedBlockSpans,
                 const QVector<ExpectedSpan> &expectedInlineSpans,
                 const QVector<ExpectedSpan> &expectedUnclosedSpans = {})
{
    const CjkText::DocumentAnalysis analysis = CjkText::analyzeDocument(text);
    const bool passed = spansMatch(analysis.blockSpans, expectedBlockSpans)
        && spansMatch(analysis.inlineSpans, expectedInlineSpans)
        && spansMatch(analysis.unclosedInlineSpans, expectedUnclosedSpans);
    QJsonObject detail{
        {QStringLiteral("input"), text},
        {QStringLiteral("expectedBlockSpans"), expectedSpanSummary(expectedBlockSpans)},
        {QStringLiteral("expectedInlineSpans"), expectedSpanSummary(expectedInlineSpans)},
        {QStringLiteral("expectedUnclosedSpans"), expectedSpanSummary(expectedUnclosedSpans)},
        {QStringLiteral("actualBlockSpans"), spanSummary(analysis.blockSpans)},
        {QStringLiteral("actualInlineSpans"), spanSummary(analysis.inlineSpans)},
        {QStringLiteral("actualUnclosedSpans"), spanSummary(analysis.unclosedInlineSpans)},
    };
    addCheck(checks, details, name, passed, detail);
}

QString applyInsertions(const QString &text, const QVector<int> &insertions)
{
    QString result = text;
    for (auto it = insertions.crbegin(); it != insertions.crend(); ++it) {
        result.insert(*it, QLatin1Char(' '));
    }
    return result;
}

QString buildPerfDocument(int minimumLength)
{
    QString document;
    document.reserve(minimumLength + 256);
    const QString normalLine = QStringLiteral("中文ABC123`code`中文$x+1$结束\n");
    const QString mixedBlock = QStringLiteral(
        "```\n$$\n中文ABC\n```\n"
        "$$\n```\n中文ABC\n$$\n");
    int blockIndex = 0;
    while (document.size() < minimumLength) {
        document += normalLine;
        if (++blockIndex % 5 == 0) {
            document += mixedBlock;
        }
    }
    return document;
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
                  && initial.value(QStringLiteral("themeAccentColor")).toString()
                     == QStringLiteral("#85c7c0")
                  && initial.value(QStringLiteral("themeAccentTextColor")).toString()
                     == QStringLiteral("#183331")
                  && initial.value(QStringLiteral("themeSelectionColor")).toString()
                     == QStringLiteral("#85c7c0")
                  && initial.value(QStringLiteral("selectionDragColor")).toString()
                     == QStringLiteral("#85c7c0")
                  && initial.value(QStringLiteral("commandPaletteMaximumWidth")).toInt() == 620,
             initial);

    const QString dragText = QStringLiteral("AA<move>BB");
    setTextAndSelection(dragText, 2, 8);
    const QJsonObject movedForward = dragSelection(2, 8, dragText.size());
    const QJsonObject undoneMove = request(QStringLiteral("testUndo"));
    setTextAndSelection(dragText, 2, 8);
    const QJsonObject movedBackward = dragSelection(2, 8, 0);
    setTextAndSelection(dragText, 2, 8);
    const QJsonObject droppedInside = dragSelection(2, 8, 5);
    const QString multilineDragText = QStringLiteral("top\nmiddle\nbottom");
    setTextAndSelection(multilineDragText, 4, 11);
    const QJsonObject movedMultiline = dragSelection(4, 11, 0);
    addCheck(checks, details, QStringLiteral("selectionDragMove"),
             movedForward.value(QStringLiteral("eventsAccepted")).toBool()
                 && movedForward.value(QStringLiteral("moved")).toBool()
                 && movedForward.value(QStringLiteral("text")).toString()
                    == QStringLiteral("AABB<move>")
                 && movedForward.value(QStringLiteral("selectionStart")).toInt() == 4
                 && movedForward.value(QStringLiteral("selectionEnd")).toInt() == 10
                 && undoneMove.value(QStringLiteral("text")).toString() == dragText
                 && movedBackward.value(QStringLiteral("eventsAccepted")).toBool()
                 && movedBackward.value(QStringLiteral("moved")).toBool()
                 && movedBackward.value(QStringLiteral("text")).toString()
                    == QStringLiteral("<move>AABB")
                 && movedBackward.value(QStringLiteral("selectionStart")).toInt() == 0
                 && movedBackward.value(QStringLiteral("selectionEnd")).toInt() == 6
                 && droppedInside.value(QStringLiteral("eventsAccepted")).toBool()
                 && !droppedInside.value(QStringLiteral("moved")).toBool()
                 && droppedInside.value(QStringLiteral("text")).toString() == dragText
                 && movedMultiline.value(QStringLiteral("eventsAccepted")).toBool()
                 && movedMultiline.value(QStringLiteral("moved")).toBool()
                 && movedMultiline.value(QStringLiteral("text")).toString()
                    == QStringLiteral("middle\ntop\nbottom"),
             QJsonObject{{QStringLiteral("forward"), movedForward},
                         {QStringLiteral("undo"), undoneMove},
                         {QStringLiteral("backward"), movedBackward},
                         {QStringLiteral("inside"), droppedInside},
                         {QStringLiteral("multiline"), movedMultiline}});

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

    const QString pathMarkdown = QStringLiteral(
        "左侧文字 `D:\\_Dev\\ScratchEditor` 示例文字 `D:\\_Dev\\ScratchEditor` 右侧文字");
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), pathMarkdown}});
    const QJsonObject pathCodeStyle = formatAtPosition(pathMarkdown.indexOf(QStringLiteral("_Dev")));
    const QJsonObject pathMiddleStyle = formatAtPosition(
        pathMarkdown.indexOf(QStringLiteral("示例文字")));
    addCheck(checks, details, QStringLiteral("underscorePathsDoNotCrossCodeSpans"),
             pathCodeStyle.value(QStringLiteral("background")).toString()
                    == QStringLiteral("#303030")
                 && !pathCodeStyle.value(QStringLiteral("italic")).toBool()
                 && !pathMiddleStyle.value(QStringLiteral("italic")).toBool(),
             QJsonObject{{QStringLiteral("code"), pathCodeStyle},
                         {QStringLiteral("middle"), pathMiddleStyle}});

    const QString barePathMarkdown = QStringLiteral(
        "左侧文字 D:\\_Dev\\ScratchEditor 示例文字 D:\\_Dev\\ScratchEditor 右侧文字");
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), barePathMarkdown}});
    const QJsonObject barePathMiddleStyle = formatAtPosition(
        barePathMarkdown.indexOf(QStringLiteral("示例文字")));
    addCheck(checks, details, QStringLiteral("bareUnderscorePathsStayLiteral"),
             !barePathMiddleStyle.value(QStringLiteral("italic")).toBool(),
             barePathMiddleStyle);

    const QString emphasisBoundaries = QStringLiteral(
        "_italic_ __bold__ ___both___ foo_bar_baz foo*star*baz \\_literal\\_ "
        "**outer *inner* outer**");
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), emphasisBoundaries}});
    const QJsonObject underscoreItalic = formatAt(emphasisBoundaries,
                                                   QStringLiteral("italic"));
    const QJsonObject underscoreBold = formatAt(emphasisBoundaries,
                                                 QStringLiteral("bold"));
    const QJsonObject underscoreBoth = formatAt(emphasisBoundaries,
                                                 QStringLiteral("both"));
    const QJsonObject intrawordUnderscore = formatAt(emphasisBoundaries,
                                                     QStringLiteral("bar"));
    const QJsonObject intrawordAsterisk = formatAt(emphasisBoundaries,
                                                   QStringLiteral("star"));
    const QJsonObject escapedUnderscore = formatAt(emphasisBoundaries,
                                                   QStringLiteral("literal"));
    const QJsonObject nestedOuter = formatAt(emphasisBoundaries,
                                              QStringLiteral("outer"));
    const QJsonObject nestedInner = formatAt(emphasisBoundaries,
                                              QStringLiteral("inner"));
    addCheck(checks, details, QStringLiteral("commonMarkEmphasisBoundaries"),
             underscoreItalic.value(QStringLiteral("italic")).toBool()
                 && underscoreBold.value(QStringLiteral("bold")).toBool()
                 && !underscoreBold.value(QStringLiteral("italic")).toBool()
                 && underscoreBoth.value(QStringLiteral("bold")).toBool()
                 && underscoreBoth.value(QStringLiteral("italic")).toBool()
                 && !intrawordUnderscore.value(QStringLiteral("italic")).toBool()
                 && intrawordAsterisk.value(QStringLiteral("italic")).toBool()
                 && !escapedUnderscore.value(QStringLiteral("italic")).toBool()
                 && nestedOuter.value(QStringLiteral("bold")).toBool()
                 && !nestedOuter.value(QStringLiteral("italic")).toBool()
                 && nestedInner.value(QStringLiteral("bold")).toBool()
                 && nestedInner.value(QStringLiteral("italic")).toBool(),
             QJsonObject{{QStringLiteral("underscoreItalic"), underscoreItalic},
                         {QStringLiteral("underscoreBold"), underscoreBold},
                         {QStringLiteral("underscoreBoth"), underscoreBoth},
                         {QStringLiteral("intrawordUnderscore"), intrawordUnderscore},
                         {QStringLiteral("intrawordAsterisk"), intrawordAsterisk},
                         {QStringLiteral("escaped"), escapedUnderscore},
                         {QStringLiteral("nestedOuter"), nestedOuter},
                         {QStringLiteral("nestedInner"), nestedInner}});

    const QString emphasisEdgeCases = QStringLiteral(
        "_中文_ 中_文_中 €_symbol_ 😀_emoji_\n_ leading_\n_trailing _\n\\\\*even*\n"
        "*foo _bar* baz_\n*a `*`*");
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), emphasisEdgeCases}});
    const QJsonObject unicodeEmphasis = formatAt(emphasisEdgeCases,
                                                  QStringLiteral("中文"));
    const QJsonObject unicodeIntraword = formatAtPosition(
        emphasisEdgeCases.indexOf(QStringLiteral("中_文")) + 2);
    const QJsonObject unicodeSymbolBoundary = formatAt(emphasisEdgeCases,
                                                        QStringLiteral("symbol"));
    const QJsonObject surrogateSymbolBoundary = formatAt(emphasisEdgeCases,
                                                          QStringLiteral("emoji"));
    const QJsonObject leadingWhitespace = formatAt(emphasisEdgeCases,
                                                    QStringLiteral("leading"));
    const QJsonObject trailingWhitespace = formatAt(emphasisEdgeCases,
                                                     QStringLiteral("trailing"));
    const QJsonObject evenlyEscaped = formatAt(emphasisEdgeCases,
                                                QStringLiteral("even"));
    const int overlapStart = emphasisEdgeCases.indexOf(QStringLiteral("*foo"));
    const QJsonObject overlapFirst = formatAtPosition(overlapStart + 1);
    const QJsonObject overlapInner = formatAtPosition(
        emphasisEdgeCases.indexOf(QStringLiteral("_bar")) + 1);
    const QJsonObject overlapTail = formatAt(emphasisEdgeCases,
                                              QStringLiteral("baz"));
    const int codePrecedenceStart = emphasisEdgeCases.indexOf(QStringLiteral("*a `"));
    const QJsonObject emphasisAroundCode = formatAtPosition(codePrecedenceStart + 1);
    const QJsonObject codeInsideEmphasis = formatAtPosition(codePrecedenceStart + 4);
    addCheck(checks, details, QStringLiteral("emphasisUnicodeAndPrecedenceEdges"),
             unicodeEmphasis.value(QStringLiteral("italic")).toBool()
                 && !unicodeIntraword.value(QStringLiteral("italic")).toBool()
                 && unicodeSymbolBoundary.value(QStringLiteral("italic")).toBool()
                 && surrogateSymbolBoundary.value(QStringLiteral("italic")).toBool()
                 && !leadingWhitespace.value(QStringLiteral("italic")).toBool()
                 && !trailingWhitespace.value(QStringLiteral("italic")).toBool()
                 && evenlyEscaped.value(QStringLiteral("italic")).toBool()
                 && overlapFirst.value(QStringLiteral("italic")).toBool()
                 && overlapInner.value(QStringLiteral("italic")).toBool()
                 && !overlapTail.value(QStringLiteral("italic")).toBool()
                 && emphasisAroundCode.value(QStringLiteral("italic")).toBool()
                 && codeInsideEmphasis.value(QStringLiteral("background")).toString()
                    == QStringLiteral("#303030")
                 && !codeInsideEmphasis.value(QStringLiteral("italic")).toBool(),
             QJsonObject{{QStringLiteral("unicode"), unicodeEmphasis},
                         {QStringLiteral("unicodeIntraword"), unicodeIntraword},
                         {QStringLiteral("unicodeSymbol"), unicodeSymbolBoundary},
                         {QStringLiteral("surrogateSymbol"), surrogateSymbolBoundary},
                         {QStringLiteral("leadingWhitespace"), leadingWhitespace},
                         {QStringLiteral("trailingWhitespace"), trailingWhitespace},
                         {QStringLiteral("evenEscape"), evenlyEscaped},
                         {QStringLiteral("overlapFirst"), overlapFirst},
                         {QStringLiteral("overlapInner"), overlapInner},
                         {QStringLiteral("overlapTail"), overlapTail},
                         {QStringLiteral("aroundCode"), emphasisAroundCode},
                         {QStringLiteral("insideCode"), codeInsideEmphasis}});

    const QString emphasisResolution = QStringLiteral(
        "*foo **bar***\n**foo *bar* baz**\n**first **short close**\n"
        "foo***triple***baz");
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), emphasisResolution}});
    const QJsonObject italicOuter = formatAt(emphasisResolution, QStringLiteral("foo"));
    const QJsonObject strongInsideItalic = formatAt(emphasisResolution,
                                                     QStringLiteral("bar"));
    const int secondLine = emphasisResolution.indexOf(QStringLiteral("**foo *bar"));
    const QJsonObject strongOuter = formatAtPosition(secondLine + 2);
    const QJsonObject italicInsideStrong = formatAtPosition(
        emphasisResolution.indexOf(QStringLiteral("*bar*"), secondLine) + 1);
    const QJsonObject earlierSameOpener = formatAt(emphasisResolution,
                                                   QStringLiteral("first"));
    const QJsonObject shorterSameCloser = formatAt(emphasisResolution,
                                                    QStringLiteral("short"));
    const QJsonObject tripleIntraword = formatAt(emphasisResolution,
                                                 QStringLiteral("triple"));
    addCheck(checks, details, QStringLiteral("commonMarkNestingResolution"),
             italicOuter.value(QStringLiteral("italic")).toBool()
                 && !italicOuter.value(QStringLiteral("bold")).toBool()
                 && strongInsideItalic.value(QStringLiteral("italic")).toBool()
                 && strongInsideItalic.value(QStringLiteral("bold")).toBool()
                 && strongOuter.value(QStringLiteral("bold")).toBool()
                 && !strongOuter.value(QStringLiteral("italic")).toBool()
                 && italicInsideStrong.value(QStringLiteral("bold")).toBool()
                 && italicInsideStrong.value(QStringLiteral("italic")).toBool()
                 && !earlierSameOpener.value(QStringLiteral("bold")).toBool()
                 && shorterSameCloser.value(QStringLiteral("bold")).toBool()
                 && tripleIntraword.value(QStringLiteral("bold")).toBool()
                 && tripleIntraword.value(QStringLiteral("italic")).toBool(),
             QJsonObject{{QStringLiteral("italicOuter"), italicOuter},
                         {QStringLiteral("strongInsideItalic"), strongInsideItalic},
                         {QStringLiteral("strongOuter"), strongOuter},
                         {QStringLiteral("italicInsideStrong"), italicInsideStrong},
                         {QStringLiteral("earlierSameOpener"), earlierSameOpener},
                         {QStringLiteral("shorterSameCloser"), shorterSameCloser},
                         {QStringLiteral("tripleIntraword"), tripleIntraword}});

    const QString codeAndLinkMarkdown = QStringLiteral(
        "`_single_` ``a`_multi_`` _outside_ *[Title](URL_with_under)* "
        "`unclosed ``_later_``");
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), codeAndLinkMarkdown}});
    const QJsonObject singleCodeEmphasis = formatAt(codeAndLinkMarkdown,
                                                     QStringLiteral("single"));
    const QJsonObject multiCodeEmphasis = formatAt(codeAndLinkMarkdown,
                                                    QStringLiteral("multi"));
    const QJsonObject outsideEmphasis = formatAt(codeAndLinkMarkdown,
                                                  QStringLiteral("outside"));
    const QJsonObject emphasizedLink = formatAt(codeAndLinkMarkdown,
                                                 QStringLiteral("Title"));
    const QJsonObject emphasizedLinkTarget = formatAt(codeAndLinkMarkdown,
                                                       QStringLiteral("URL"));
    const QJsonObject codeAfterUnmatchedRun = formatAt(codeAndLinkMarkdown,
                                                        QStringLiteral("later"));
    addCheck(checks, details, QStringLiteral("codeAndLinkPrecedeEmphasis"),
             singleCodeEmphasis.value(QStringLiteral("background")).toString()
                    == QStringLiteral("#303030")
                 && !singleCodeEmphasis.value(QStringLiteral("italic")).toBool()
                 && multiCodeEmphasis.value(QStringLiteral("background")).toString()
                    == QStringLiteral("#303030")
                 && !multiCodeEmphasis.value(QStringLiteral("italic")).toBool()
                 && outsideEmphasis.value(QStringLiteral("italic")).toBool()
                 && emphasizedLink.value(QStringLiteral("underline")).toBool()
                 && emphasizedLink.value(QStringLiteral("italic")).toBool()
                 && emphasizedLinkTarget.value(QStringLiteral("underline")).toBool()
                 && emphasizedLinkTarget.value(QStringLiteral("italic")).toBool()
                 && codeAfterUnmatchedRun.value(QStringLiteral("background")).toString()
                    == QStringLiteral("#303030")
                 && !codeAfterUnmatchedRun.value(QStringLiteral("italic")).toBool(),
             QJsonObject{{QStringLiteral("singleCode"), singleCodeEmphasis},
                         {QStringLiteral("multiCode"), multiCodeEmphasis},
                         {QStringLiteral("outside"), outsideEmphasis},
                         {QStringLiteral("link"), emphasizedLink},
                         {QStringLiteral("linkTarget"), emphasizedLinkTarget},
                         {QStringLiteral("afterUnmatched"), codeAfterUnmatchedRun}});

    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), styledMarkdown}});
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

    setTextAndSelection(QStringLiteral("a big rat"), 6, 6);
    const QJsonObject boldNextWord = execute(QStringLiteral("toggleBold"));
    setTextAndSelection(QStringLiteral("a big"), 5, 5);
    const QJsonObject boldPreviousWord = execute(QStringLiteral("toggleBold"));
    setTextAndSelection(QStringLiteral("a big "), 6, 6);
    const QJsonObject boldAtEmptyBoundary = execute(QStringLiteral("toggleBold"));
    addCheck(checks, details, QStringLiteral("collapsedFormattingUsesWordBoundaries"),
             boldNextWord.value(QStringLiteral("text")).toString()
                    == QStringLiteral("a big **rat**")
                 && boldNextWord.value(QStringLiteral("cursorPosition")).toInt() == 8
                 && boldPreviousWord.value(QStringLiteral("text")).toString()
                    == QStringLiteral("a **big**")
                 && boldPreviousWord.value(QStringLiteral("cursorPosition")).toInt() == 7
                 && boldAtEmptyBoundary.value(QStringLiteral("text")).toString()
                    == QStringLiteral("a big ****")
                 && boldAtEmptyBoundary.value(QStringLiteral("cursorPosition")).toInt() == 8,
             QJsonObject{{QStringLiteral("nextWord"), boldNextWord},
                         {QStringLiteral("previousWord"), boldPreviousWord},
                         {QStringLiteral("emptyBoundary"), boldAtEmptyBoundary}});

    setTextAndSelection(QStringLiteral("**big rat**"), 4, 4);
    const QJsonObject boldOffAtCursor = execute(QStringLiteral("toggleBold"));
    setTextAndSelection(QStringLiteral("**big rat**"), 2, 5);
    const QJsonObject boldOffAroundSelection = execute(QStringLiteral("toggleBold"));
    setTextAndSelection(QStringLiteral("`code`"), 3, 3);
    const QJsonObject codeOffAtCursor = execute(QStringLiteral("wrapCode"));
    addCheck(checks, details, QStringLiteral("formattingTogglesContainingBlock"),
             boldOffAtCursor.value(QStringLiteral("text")).toString()
                    == QStringLiteral("big rat")
                 && boldOffAtCursor.value(QStringLiteral("cursorPosition")).toInt() == 2
                 && boldOffAroundSelection.value(QStringLiteral("text")).toString()
                    == QStringLiteral("big rat")
                 && boldOffAroundSelection.value(QStringLiteral("selectionStart")).toInt() == 0
                 && boldOffAroundSelection.value(QStringLiteral("selectionEnd")).toInt() == 3
                 && codeOffAtCursor.value(QStringLiteral("text")).toString()
                    == QStringLiteral("code")
                 && codeOffAtCursor.value(QStringLiteral("cursorPosition")).toInt() == 2,
             QJsonObject{{QStringLiteral("cursor"), boldOffAtCursor},
                         {QStringLiteral("selection"), boldOffAroundSelection},
                         {QStringLiteral("code"), codeOffAtCursor}});

    setTextAndSelection(QStringLiteral("***both***"), 5, 5);
    const QJsonObject boldLayerOff = execute(QStringLiteral("toggleBold"));
    setTextAndSelection(QStringLiteral("***both***"), 5, 5);
    const QJsonObject italicLayerOff = execute(QStringLiteral("toggleItalic"));
    addCheck(checks, details, QStringLiteral("boldItalicLayersToggleIndependently"),
             boldLayerOff.value(QStringLiteral("text")).toString() == QStringLiteral("*both*")
                 && italicLayerOff.value(QStringLiteral("text")).toString()
                    == QStringLiteral("**both**"),
             QJsonObject{{QStringLiteral("boldOff"), boldLayerOff},
                         {QStringLiteral("italicOff"), italicLayerOff}});

    const QString crossingBoldText = QStringLiteral("pre **bold** tail");
    setTextAndSelection(crossingBoldText, 0, 12);
    const QJsonObject crossingSameFormat = execute(QStringLiteral("toggleBold"));
    setTextAndSelection(crossingBoldText, 0, 12);
    const QJsonObject crossingDifferentFormat = execute(QStringLiteral("toggleItalic"));
    setTextAndSelection(QStringLiteral("**bold**"), 2, 6);
    const QJsonObject nestedDifferentFormat = execute(QStringLiteral("toggleItalic"));
    addCheck(checks, details, QStringLiteral("crossingSelectionRespectsMarkerType"),
             crossingSameFormat.value(QStringLiteral("text")).toString()
                    == QStringLiteral("**pre bold** tail")
                 && crossingDifferentFormat.value(QStringLiteral("text")).toString()
                    == QStringLiteral("*pre **bold*** tail")
                 && nestedDifferentFormat.value(QStringLiteral("text")).toString()
                    == QStringLiteral("***bold***"),
             QJsonObject{{QStringLiteral("same"), crossingSameFormat},
                         {QStringLiteral("different"), crossingDifferentFormat},
                         {QStringLiteral("nested"), nestedDifferentFormat}});

    const QString crossingCodeText = QStringLiteral("pre `code` tail");
    setTextAndSelection(crossingCodeText, 0, 10);
    const QJsonObject crossingSameCode = execute(QStringLiteral("wrapCode"));
    setTextAndSelection(crossingCodeText, 0, 10);
    const QJsonObject crossingCodeWithBold = execute(QStringLiteral("toggleBold"));
    setTextAndSelection(crossingBoldText, 0, 12);
    const QJsonObject crossingBoldWithCode = execute(QStringLiteral("wrapCode"));
    setTextAndSelection(QStringLiteral("`code`"), 1, 5);
    const QJsonObject nestedBoldInCode = execute(QStringLiteral("toggleBold"));
    addCheck(checks, details, QStringLiteral("inlineCodeComposesWithOtherMarkers"),
             crossingSameCode.value(QStringLiteral("text")).toString()
                    == QStringLiteral("`pre code` tail")
                 && crossingCodeWithBold.value(QStringLiteral("text")).toString()
                    == QStringLiteral("**pre `code`** tail")
                 && crossingBoldWithCode.value(QStringLiteral("text")).toString()
                    == QStringLiteral("`pre **bold**` tail")
                 && nestedBoldInCode.value(QStringLiteral("text")).toString()
                    == QStringLiteral("`**code**`"),
             QJsonObject{{QStringLiteral("sameCode"), crossingSameCode},
                         {QStringLiteral("codeWithBold"), crossingCodeWithBold},
                         {QStringLiteral("boldWithCode"), crossingBoldWithCode},
                         {QStringLiteral("nestedBold"), nestedBoldInCode}});

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

    setTextAndSelection(QStringLiteral("plain"), 2, 2);
    const QJsonObject plainHeadingIncrease = execute(QStringLiteral("increaseHeadingLevel"));
    setTextAndSelection(QStringLiteral("plain"), 2, 2);
    const QJsonObject plainHeadingDecrease = execute(QStringLiteral("decreaseHeadingLevel"));
    setTextAndSelection(QString(), 0, 0);
    const QJsonObject emptyHeadingIncrease = execute(QStringLiteral("increaseHeadingLevel"));
    setTextAndSelection(QStringLiteral("### title\nplain"), 0, 14);
    const QJsonObject mixedHeadingIncrease = execute(QStringLiteral("increaseHeadingLevel"));
    setTextAndSelection(QStringLiteral("# only"), 2, 2);
    const QJsonObject levelOneDecrease = execute(QStringLiteral("decreaseHeadingLevel"));
    addCheck(checks, details, QStringLiteral("directionalHeadingsOnlyExistingHeadingLines"),
             plainHeadingIncrease.value(QStringLiteral("text")).toString()
                     == QStringLiteral("plain")
                 && plainHeadingIncrease.value(QStringLiteral("cursorPosition")).toInt() == 2
                 && plainHeadingDecrease.value(QStringLiteral("text")).toString()
                     == QStringLiteral("plain")
                 && emptyHeadingIncrease.value(QStringLiteral("text")).toString().isEmpty()
                 && emptyHeadingIncrease.value(QStringLiteral("cursorPosition")).toInt() == 0
                 && mixedHeadingIncrease.value(QStringLiteral("text")).toString()
                     == QStringLiteral("#### title\nplain")
                 && levelOneDecrease.value(QStringLiteral("text")).toString()
                     == QStringLiteral("# only"),
             QJsonObject{{QStringLiteral("increasePlain"), plainHeadingIncrease},
                         {QStringLiteral("decreasePlain"), plainHeadingDecrease},
                         {QStringLiteral("increaseEmpty"), emptyHeadingIncrease},
                         {QStringLiteral("increaseMixed"), mixedHeadingIncrease},
                         {QStringLiteral("decreaseLevelOne"), levelOneDecrease}});

    setTextAndSelection(QStringLiteral("plain"), 2, 2);
    const QJsonObject collapsedDirectHeading = execute(QStringLiteral("setHeading2"));
    setTextAndSelection(QStringLiteral("# plain"), 5, 5);
    const QJsonObject collapsedCycledHeading = execute(QStringLiteral("cycleHeading"));
    addCheck(checks, details, QStringLiteral("headingCommandsDoNotSelectCurrentLine"),
             collapsedDirectHeading.value(QStringLiteral("text")).toString()
                    == QStringLiteral("## plain")
                 && collapsedDirectHeading.value(QStringLiteral("selectionStart")).toInt() == 5
                 && collapsedDirectHeading.value(QStringLiteral("selectionEnd")).toInt() == 5
                 && collapsedCycledHeading.value(QStringLiteral("text")).toString()
                    == QStringLiteral("## plain")
                 && collapsedCycledHeading.value(QStringLiteral("selectionStart")).toInt() == 6
                 && collapsedCycledHeading.value(QStringLiteral("selectionEnd")).toInt() == 6,
             QJsonObject{{QStringLiteral("direct"), collapsedDirectHeading},
                         {QStringLiteral("cycle"), collapsedCycledHeading}});

    bool allEmptyHeadingLevels = true;
    QJsonObject lastEmptyHeadingToggle;
    for (int level = 1; level <= 6; ++level) {
        setTextAndSelection(QString(), 0, 0);
        const QJsonObject createdHeading = execute(QStringLiteral("setHeading%1").arg(level));
        const QString expected = QString(level, QLatin1Char('#')) + QLatin1Char(' ');
        lastEmptyHeadingToggle = execute(QStringLiteral("setHeading%1").arg(level));
        allEmptyHeadingLevels = allEmptyHeadingLevels
            && createdHeading.value(QStringLiteral("text")).toString() == expected
            && createdHeading.value(QStringLiteral("cursorPosition")).toInt()
                == expected.size()
            && createdHeading.value(QStringLiteral("selectionStart")).toInt()
                == createdHeading.value(QStringLiteral("selectionEnd")).toInt()
            && lastEmptyHeadingToggle.value(QStringLiteral("text")).toString().isEmpty()
            && lastEmptyHeadingToggle.value(QStringLiteral("cursorPosition")).toInt() == 0;
    }
    setTextAndSelection(QString(), 0, 0);
    execute(QStringLiteral("setHeading3"));
    const QJsonObject changedEmptyHeadingLevel = execute(QStringLiteral("setHeading4"));
    setTextAndSelection(QStringLiteral("### title"), 6, 6);
    const QJsonObject removedNonEmptyHeading = execute(QStringLiteral("setHeading3"));
    setTextAndSelection(QStringLiteral("### title"), 6, 6);
    const QJsonObject changedNonEmptyHeadingLevel = execute(QStringLiteral("setHeading4"));
    setTextAndSelection(QStringLiteral("## one\n## two"), 0, 13);
    const QJsonObject removedSelectedHeadings = execute(QStringLiteral("setHeading2"));
    addCheck(checks, details, QStringLiteral("headingShortcutsToggleMatchingLevel"),
             allEmptyHeadingLevels
                 && changedEmptyHeadingLevel.value(QStringLiteral("text")).toString()
                    == QStringLiteral("#### ")
                 && removedNonEmptyHeading.value(QStringLiteral("text")).toString()
                    == QStringLiteral("title")
                 && removedNonEmptyHeading.value(QStringLiteral("cursorPosition")).toInt() == 2
                 && changedNonEmptyHeadingLevel.value(QStringLiteral("text")).toString()
                    == QStringLiteral("#### title")
                 && removedSelectedHeadings.value(QStringLiteral("text")).toString()
                    == QStringLiteral("one\ntwo"),
             QJsonObject{{QStringLiteral("lastToggle"), lastEmptyHeadingToggle},
                         {QStringLiteral("changedLevel"), changedEmptyHeadingLevel},
                         {QStringLiteral("removedContent"), removedNonEmptyHeading},
                         {QStringLiteral("changedContent"), changedNonEmptyHeadingLevel},
                         {QStringLiteral("selection"), removedSelectedHeadings}});

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
    setTextAndSelection(QStringLiteral("1. one\n2. two\n3. three"), 7, 14);
    const QJsonObject movedOrderedItem = dragSelection(7, 14, 0);
    addCheck(checks, details, QStringLiteral("orderedListMoveAndIndentRenumber"),
             indentedOrderedItem.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. one\n    2. two\n2. three")
                 && movedOrderedItem.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. two\n2. one\n3. three"),
             QJsonObject{{QStringLiteral("indent"), indentedOrderedItem},
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

    // --- CJK Punctuation Auto Conversion ---
    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    keyPress(QStringLiteral(","), QStringLiteral(","));
    const bool commaConverted = editorText() == QStringLiteral("中文，");

    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    keyPress(QStringLiteral("."), QStringLiteral("."));
    const bool periodConverted = editorText() == QStringLiteral("中文。");

    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    keyPress(QStringLiteral(":"), QStringLiteral(":"));
    const bool colonConverted = editorText() == QStringLiteral("中文：");

    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    keyPress(QStringLiteral("?"), QStringLiteral("?"));
    const bool questionConverted = editorText() == QStringLiteral("中文？");

    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    keyPress(QStringLiteral("!"), QStringLiteral("!"));
    const bool exclaimConverted = editorText() == QStringLiteral("中文！");

    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    keyPress(QStringLiteral(";"), QStringLiteral(";"));
    const bool semiConverted = editorText() == QStringLiteral("中文；");

    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    keyPress(QStringLiteral("("), QStringLiteral("("));
    const bool parenPairConverted = editorText() == QStringLiteral("中文（）");

    setTextAndSelection(QStringLiteral("中文.."), 4, 4);
    keyPress(QStringLiteral("."), QStringLiteral("."));
    const bool ellipsisConverted = editorText() == QStringLiteral("中文……");

    setTextAndSelection(QStringLiteral("中文-"), 3, 3);
    keyPress(QStringLiteral("-"), QStringLiteral("-"));
    const bool emdashConverted = editorText() == QStringLiteral("中文——");

    setTextAndSelection(QStringLiteral("中文，"), 3, 3);
    keyPress(QStringLiteral(","), QStringLiteral(","));
    const bool chainedCommaConverted = editorText() == QStringLiteral("中文，，");

    addCheck(checks, details, QStringLiteral("cjkPunctuationAutoConversion"),
             commaConverted && periodConverted && colonConverted && questionConverted
                 && exclaimConverted && semiConverted && parenPairConverted
                 && ellipsisConverted && emdashConverted && chainedCommaConverted,
             {});

    // --- CJK Selection Wrapping ---
    setTextAndSelection(QStringLiteral("中文"), 0, 2);
    keyPress(QStringLiteral("("), QStringLiteral("("));
    const bool cjkParenWrap = editorText() == QStringLiteral("（中文）");

    setTextAndSelection(QStringLiteral("中文"), 0, 2);
    keyPress(QStringLiteral("["), QStringLiteral("["));
    const bool cjkBracketWrap = editorText() == QStringLiteral("【中文】");

    setTextAndSelection(QStringLiteral("ABC"), 0, 3);
    keyPress(QStringLiteral("("), QStringLiteral("("));
    const bool asciiParenWrap = editorText() == QStringLiteral("(ABC)");

    addCheck(checks, details, QStringLiteral("cjkSelectionWrapping"),
             cjkParenWrap && cjkBracketWrap && asciiParenWrap, {});

    // --- CJK Pair Backspace ---
    setTextAndSelection(QStringLiteral("（）"), 1, 1);
    keyPress({}, QStringLiteral("Backspace"));
    const bool fullParenDeleted = editorText().isEmpty();

    setTextAndSelection(QStringLiteral("……"), 2, 2);
    keyPress({}, QStringLiteral("Backspace"));
    const bool ellipsisDeleted = editorText().isEmpty();
    setTextAndSelection(QStringLiteral("——"), 2, 2);
    keyPress({}, QStringLiteral("Backspace"));
    const bool emdashDeleted = editorText().isEmpty();

    addCheck(checks, details, QStringLiteral("cjkPairBackspace"),
             fullParenDeleted && ellipsisDeleted && emdashDeleted, {});

    // --- 全配对退格参数化（验收用例三 F） ---
    {
        const QVector<std::pair<QString, QString>> allPairs{
            {QStringLiteral("("), QStringLiteral(")")},
            {QStringLiteral("["), QStringLiteral("]")},
            {QStringLiteral("{"), QStringLiteral("}")},
            {QStringLiteral("<"), QStringLiteral(">")},
            {QStringLiteral("（"), QStringLiteral("）")},
            {QStringLiteral("［"), QStringLiteral("］")},
            {QStringLiteral("｛"), QStringLiteral("｝")},
            {QStringLiteral("＜"), QStringLiteral("＞")},
            {QStringLiteral("【"), QStringLiteral("】")},
            {QStringLiteral("〔"), QStringLiteral("〕")},
            {QStringLiteral("〖"), QStringLiteral("〗")},
            {QStringLiteral("〘"), QStringLiteral("〙")},
            {QStringLiteral("〚"), QStringLiteral("〛")},
            {QStringLiteral("《"), QStringLiteral("》")},
            {QStringLiteral("〈"), QStringLiteral("〉")},
            {QStringLiteral("「"), QStringLiteral("」")},
            {QStringLiteral("『"), QStringLiteral("』")},
            {QStringLiteral("“"), QStringLiteral("”")},
            {QStringLiteral("‘"), QStringLiteral("’")},
            {QStringLiteral("`"), QStringLiteral("`")},
            {QStringLiteral("\""), QStringLiteral("\"")},
            {QStringLiteral("'"), QStringLiteral("'")},
            {QStringLiteral("＂"), QStringLiteral("＂")},
            {QStringLiteral("＇"), QStringLiteral("＇")},
        };
        QStringList failures;
        for (const auto &pair : allPairs) {
            const QString pairText = pair.first + pair.second;
            setTextAndSelection(pairText, pair.first.size(), pair.first.size());
            keyPress({}, QStringLiteral("Backspace"));
            const QString afterBackspace = editorText();
            request(QStringLiteral("testUndo"));
            const QString afterUndo = editorText();
            if (!afterBackspace.isEmpty() || afterUndo != pairText) {
                failures << QStringLiteral("%1(backspace=%2,undo=%3)")
                    .arg(pairText, afterBackspace, afterUndo);
            }
        }
        addCheck(checks, details, QStringLiteral("backspaceAllDelimiterPairs"),
                 failures.isEmpty(),
                 QJsonObject{{QStringLiteral("failures"), failures.join(QLatin1Char(';'))},
                             {QStringLiteral("pairCount"), allPairs.size()}});
    }

    // --- CJK Auto Spacing & Cursor Following ---
    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    keyPress(QStringLiteral("A"), QStringLiteral("A"));
    // Allow small event loop delay for async auto-space timer
    QThread::msleep(50);
    QCoreApplication::processEvents();
    const bool cjkAsciiSpaced = editorText() == QStringLiteral("中文 A");

    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    inputMethodCommit(QStringLiteral("测试"));
    QThread::msleep(50);
    const QJsonObject imeStatus = request(QStringLiteral("status"));
    const bool cjkImeCursorPassed = (editorText() == QStringLiteral("中文测试"))
                                 && (imeStatus.value(QStringLiteral("cursorPosition")).toInt() == 4);

    setTextAndSelection(QStringLiteral("第一行"), 3, 3);
    inputMethodCommit(QStringLiteral("ABC"));
    QThread::msleep(50);
    QCoreApplication::processEvents();
    const bool cjkImeMultiCharSpaced = (editorText() == QStringLiteral("第一行 ABC"));

    addCheck(checks, details, QStringLiteral("cjkAutoSpacing"),
             cjkAsciiSpaced && cjkImeCursorPassed && cjkImeMultiCharSpaced,
             QJsonObject{{QStringLiteral("cjkAsciiSpaced"), cjkAsciiSpaced},
                         {QStringLiteral("cjkImeCursorPassed"), cjkImeCursorPassed},
                         {QStringLiteral("cjkImeMultiCharSpaced"), cjkImeMultiCharSpaced},
                         {QStringLiteral("text"), editorText()},
                         {QStringLiteral("imeStatus"), imeStatus}});

    // --- Format Spacing Command (Alt+F) ---
    setTextAndSelection(QStringLiteral("中文ABC"), 0, 0);
    execute(QStringLiteral("formatSpacing"));
    const bool fmtCjkAscii = editorText() == QStringLiteral("中文 ABC");

    setTextAndSelection(QStringLiteral("ABC中文"), 0, 0);
    execute(QStringLiteral("formatSpacing"));
    const bool fmtAsciiCjk = editorText() == QStringLiteral("ABC 中文");

    setTextAndSelection(QStringLiteral("中文123"), 0, 0);
    execute(QStringLiteral("formatSpacing"));
    const bool fmtCjkNum = editorText() == QStringLiteral("中文 123");

    setTextAndSelection(QStringLiteral("Python3"), 0, 0);
    execute(QStringLiteral("formatSpacing"));
    const bool fmtAlnumNoSpace = editorText() == QStringLiteral("Python3");

    setTextAndSelection(QStringLiteral("中文，ABC"), 0, 0);
    execute(QStringLiteral("formatSpacing"));
    const bool fmtSoftSepNoSpace = editorText() == QStringLiteral("中文，ABC");

    setTextAndSelection(QStringLiteral("中文`code`中文"), 0, 0);
    execute(QStringLiteral("formatSpacing"));
    const bool fmtInlineCodeBoundaries = editorText() == QStringLiteral("中文 `code` 中文");

    setTextAndSelection(QStringLiteral("```\n中文ABC\n```"), 0, 0);
    execute(QStringLiteral("formatSpacing"));
    const bool fmtFencedCodeIgnored = editorText() == QStringLiteral("```\n中文ABC\n```");

    setTextAndSelection(QStringLiteral("中文ABC\nDEF"), 0, 5);
    execute(QStringLiteral("formatSpacing"));
    const bool fmtSelectionOnly = editorText() == QStringLiteral("中文 ABC\nDEF");

    setTextAndSelection(QStringLiteral("第一行ABC中文"), 3, 8);
    execute(QStringLiteral("formatSpacing"));
    const bool fmtPartialLineSelection = editorText() == QStringLiteral("第一行ABC 中文");

    // Full User Benchmark Document Test
    const QString userDocInput = QStringLiteral(
"# 基础边界\n\n"
"中文ABC\n"
"ABC中文\n"
"中文123\n"
"123中文\n"
"使用Python3编程\n"
"Python3中文\n"
"版本GPT4已经发布\n"
"学习HTML5和CSS3\n"
"中文 ABC\n"
"ABC 中文\n\n"
"# 标点与符号\n\n"
"中文，ABC\n"
"中文。ABC\n"
"中文-ABC\n"
"ABC/中文\n"
"中文（ABC）\n"
"中文[ABC]\n"
"ABC：中文\n\n"
"# 行内代码\n\n"
"中文`code`文本\n"
"使用`printf`函数\n"
"中文，`code`\n"
"`code`，中文\n"
"中文(`code`)文本\n"
"测试`中文ABC123`结束\n"
"已有 `code` 空格\n\n"
"# 行内公式\n\n"
"公式$x+1$成立\n"
"中文，$x+1$\n"
"$x+1$，中文\n"
"中文($x+1$)文本\n"
"测试$中文ABC123$结束\n"
"已有 $x+1$ 空格\n\n"
"# 围栏代码块：内部必须保持原样\n\n"
"```javascript\n"
"const title=\"中文ABC123\";\n"
"const value=\"ABC中文\";\n"
"console.log(\"测试code123\");\n"
"```\n\n"
"~~~text\n"
"中文ABC\n"
"ABC中文\n"
"中文123\n"
"~~~\n\n"
"# 块级公式：内部必须保持原样\n\n"
"$$\n"
"中文ABC+x1\n"
"ABC中文+123中文\n"
"$$\n\n"
"# 代码块之后继续整理\n\n"
"代码块结束后继续使用Python3编程\n"
"最后测试ABC中文123结束"
    );

    const QString userDocExpected = QStringLiteral(
"# 基础边界\n\n"
"中文 ABC\n"
"ABC 中文\n"
"中文 123\n"
"123 中文\n"
"使用 Python3 编程\n"
"Python3 中文\n"
"版本 GPT4 已经发布\n"
"学习 HTML5 和 CSS3\n"
"中文 ABC\n"
"ABC 中文\n\n"
"# 标点与符号\n\n"
"中文，ABC\n"
"中文。ABC\n"
"中文-ABC\n"
"ABC/中文\n"
"中文（ABC）\n"
"中文[ABC]\n"
"ABC：中文\n\n"
"# 行内代码\n\n"
"中文 `code` 文本\n"
"使用 `printf` 函数\n"
"中文，`code`\n"
"`code`，中文\n"
"中文(`code`)文本\n"
"测试 `中文ABC123` 结束\n"
"已有 `code` 空格\n\n"
"# 行内公式\n\n"
"公式 $x+1$ 成立\n"
"中文，$x+1$\n"
"$x+1$，中文\n"
"中文($x+1$)文本\n"
"测试 $中文ABC123$ 结束\n"
"已有 $x+1$ 空格\n\n"
"# 围栏代码块：内部必须保持原样\n\n"
"```javascript\n"
"const title=\"中文ABC123\";\n"
"const value=\"ABC中文\";\n"
"console.log(\"测试code123\");\n"
"```\n\n"
"~~~text\n"
"中文ABC\n"
"ABC中文\n"
"中文123\n"
"~~~\n\n"
"# 块级公式：内部必须保持原样\n\n"
"$$\n"
"中文ABC+x1\n"
"ABC中文+123中文\n"
"$$\n\n"
"# 代码块之后继续整理\n\n"
"代码块结束后继续使用 Python3 编程\n"
"最后测试 ABC 中文 123 结束"
    );

    setTextAndSelection(userDocInput, 0, userDocInput.size());
    execute(QStringLiteral("formatSpacing"));
    const bool fmtUserBenchmarkPassed = (editorText() == userDocExpected);

    addCheck(checks, details, QStringLiteral("formatSpacingCommand"),
             fmtCjkAscii && fmtAsciiCjk && fmtCjkNum && fmtAlnumNoSpace
                 && fmtSoftSepNoSpace && fmtInlineCodeBoundaries
                 && fmtFencedCodeIgnored && fmtSelectionOnly
                 && fmtUserBenchmarkPassed,
             QJsonObject{{QStringLiteral("benchmarkPassed"), fmtUserBenchmarkPassed},
                         {QStringLiteral("actualOutput"), editorText()}});

    // --- CJK Fix: Individual KeyPress Punctuation Checks (KEY-001..013) ---
    const auto keyAction = [](const QString &text) {
        return [text] { return keyPress(text, text); };
    };
    cjkExpect(checks, details, QStringLiteral("cjkKeyComma"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key ,"),
              keyAction(QStringLiteral(",")), QStringLiteral("中文，"), 3);
    cjkExpect(checks, details, QStringLiteral("cjkKeyPeriod"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key ."),
              keyAction(QStringLiteral(".")), QStringLiteral("中文。"), 3);
    cjkExpect(checks, details, QStringLiteral("cjkKeyColon"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key :"),
              keyAction(QStringLiteral(":")), QStringLiteral("中文："), 3);
    cjkExpect(checks, details, QStringLiteral("cjkKeyQuestion"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key ?"),
              keyAction(QStringLiteral("?")), QStringLiteral("中文？"), 3);
    cjkExpect(checks, details, QStringLiteral("cjkKeyExclaim"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key !"),
              keyAction(QStringLiteral("!")), QStringLiteral("中文！"), 3);
    cjkExpect(checks, details, QStringLiteral("cjkKeySemi"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key ;"),
              keyAction(QStringLiteral(";")), QStringLiteral("中文；"), 3);
    cjkExpect(checks, details, QStringLiteral("cjkKeyParenPair"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key ("),
              keyAction(QStringLiteral("(")), QStringLiteral("中文（）"), 3);
    cjkExpect(checks, details, QStringLiteral("cjkKeyBracketPair"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key [ after CJK"),
              keyAction(QStringLiteral("[")), QStringLiteral("中文【】"), 3);
    cjkExpect(checks, details, QStringLiteral("cjkKeyDoubleQuotePair"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key \" after CJK"),
              keyAction(QStringLiteral("\"")), QStringLiteral("中文 “”"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeySingleQuotePair"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key ' after CJK"),
              keyAction(QStringLiteral("'")), QStringLiteral("中文 ‘’"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyChainComma"),
              QStringLiteral("中文，"), 3, 3, QStringLiteral("key ,"),
              keyAction(QStringLiteral(",")), QStringLiteral("中文，，"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyChainPeriod"),
              QStringLiteral("中文。"), 3, 3, QStringLiteral("key ."),
              keyAction(QStringLiteral(".")), QStringLiteral("中文。。"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyChainColon"),
              QStringLiteral("中文："), 3, 3, QStringLiteral("key :"),
              keyAction(QStringLiteral(":")), QStringLiteral("中文：："), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyChainSemicolon"),
              QStringLiteral("中文；"), 3, 3, QStringLiteral("key ;"),
              keyAction(QStringLiteral(";")), QStringLiteral("中文；；"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyChainQuestion"),
              QStringLiteral("中文？"), 3, 3, QStringLiteral("key ?"),
              keyAction(QStringLiteral("?")), QStringLiteral("中文？？"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyChainExclaim"),
              QStringLiteral("中文！"), 3, 3, QStringLiteral("key !"),
              keyAction(QStringLiteral("!")), QStringLiteral("中文！！"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyChainAsciiPrevNegative"),
              QStringLiteral("a:"), 2, 2, QStringLiteral("key : after ASCII"),
              keyAction(QStringLiteral(":")), QStringLiteral("a::"), 3);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossColonAfterQuestion"),
              QStringLiteral("中文？"), 3, 3, QStringLiteral("key : after ？"),
              keyAction(QStringLiteral(":")), QStringLiteral("中文？："), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossCommaAfterColon"),
              QStringLiteral("中文："), 3, 3, QStringLiteral("key , after ："),
              keyAction(QStringLiteral(",")), QStringLiteral("中文：，"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossQuestionAfterPeriod"),
              QStringLiteral("中文。"), 3, 3, QStringLiteral("key ? after 。"),
              keyAction(QStringLiteral("?")), QStringLiteral("中文。？"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossExclaimAfterSemi"),
              QStringLiteral("中文；"), 3, 3, QStringLiteral("key ! after ；"),
              keyAction(QStringLiteral("!")), QStringLiteral("中文；！"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossPeriodAfterExclaim"),
              QStringLiteral("中文！"), 3, 3, QStringLiteral("key . after ！"),
              keyAction(QStringLiteral(".")), QStringLiteral("中文！。"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossCloseParen"),
              QStringLiteral("中文？"), 3, 3, QStringLiteral("key ) after ？"),
              keyAction(QStringLiteral(")")), QStringLiteral("中文？）"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossCloseParenSkip"),
              QStringLiteral("中文？）"), 3, 3, QStringLiteral("key ) skips existing ）"),
              keyAction(QStringLiteral(")")), QStringLiteral("中文？）"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossOpenParenPair"),
              QStringLiteral("中文？"), 3, 3, QStringLiteral("key ( after ？"),
              keyAction(QStringLiteral("(")), QStringLiteral("中文？（）"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossOpenBracketPair"),
              QStringLiteral("中文？"), 3, 3, QStringLiteral("key [ after ？"),
              keyAction(QStringLiteral("[")), QStringLiteral("中文？【】"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossColonAfterCloseParen"),
              QStringLiteral("中文）"), 3, 3, QStringLiteral("key : after ）"),
              keyAction(QStringLiteral(":")), QStringLiteral("中文）："), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossCommaAfterCloseParen"),
              QStringLiteral("中文）"), 3, 3, QStringLiteral("key , after ）"),
              keyAction(QStringLiteral(",")), QStringLiteral("中文），"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyChainCloseParen"),
              QStringLiteral("中文）"), 3, 3, QStringLiteral("key ) after ）"),
              keyAction(QStringLiteral(")")), QStringLiteral("中文））"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCloseParenSkipAfterCloseParen"),
              QStringLiteral("中文））"), 3, 3, QStringLiteral("key ) skips existing ）"),
              keyAction(QStringLiteral(")")), QStringLiteral("中文））"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyTwoSpacesComma"),
              QStringLiteral("abc,"), 4, 4, QStringLiteral("key space twice after ,"),
              [] {
                  keyPress(QStringLiteral(" "), QStringLiteral(" "));
                  return keyPress(QStringLiteral(" "), QStringLiteral(" "));
              },
              QStringLiteral("abc，"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyTwoSpacesColon"),
              QStringLiteral("abc:"), 4, 4, QStringLiteral("key space twice after :"),
              [] {
                  keyPress(QStringLiteral(" "), QStringLiteral(" "));
                  return keyPress(QStringLiteral(" "), QStringLiteral(" "));
              },
              QStringLiteral("abc："), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyTwoSpacesSemicolon"),
              QStringLiteral("abc;"), 4, 4, QStringLiteral("key space twice after ;"),
              [] {
                  keyPress(QStringLiteral(" "), QStringLiteral(" "));
                  return keyPress(QStringLiteral(" "), QStringLiteral(" "));
              },
              QStringLiteral("abc；"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyTwoSpacesQuestion"),
              QStringLiteral("abc?"), 4, 4, QStringLiteral("key space twice after ?"),
              [] {
                  keyPress(QStringLiteral(" "), QStringLiteral(" "));
                  return keyPress(QStringLiteral(" "), QStringLiteral(" "));
              },
              QStringLiteral("abc？"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyTwoSpacesExclaim"),
              QStringLiteral("abc!"), 4, 4, QStringLiteral("key space twice after !"),
              [] {
                  keyPress(QStringLiteral(" "), QStringLiteral(" "));
                  return keyPress(QStringLiteral(" "), QStringLiteral(" "));
              },
              QStringLiteral("abc！"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyTwoSpacesPeriod"),
              QStringLiteral("abc."), 4, 4, QStringLiteral("key space twice after ."),
              [] {
                  keyPress(QStringLiteral(" "), QStringLiteral(" "));
                  return keyPress(QStringLiteral(" "), QStringLiteral(" "));
              },
              QStringLiteral("abc。"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyTwoSpacesDotSequence"),
              QStringLiteral("abc.."), 5, 5, QStringLiteral("key space twice after .."),
              [] {
                  keyPress(QStringLiteral(" "), QStringLiteral(" "));
                  return keyPress(QStringLiteral(" "), QStringLiteral(" "));
              },
              QStringLiteral("abc.。"), 5);
    cjkExpect(checks, details, QStringLiteral("cjkKeyTwoSpacesSingleNegative"),
              QStringLiteral("abc,"), 4, 4, QStringLiteral("key space once after ,"),
              keyAction(QStringLiteral(" ")), QStringLiteral("abc, "), 5);
    cjkExpect(checks, details, QStringLiteral("protectKeyTwoSpacesComma"),
              QStringLiteral("`abc,`"), 5, 5, QStringLiteral("key space twice inside inline code"),
              [] {
                  keyPress(QStringLiteral(" "), QStringLiteral(" "));
                  return keyPress(QStringLiteral(" "), QStringLiteral(" "));
              },
              QStringLiteral("`abc,  `"), 7);
    cjkExpect(checks, details, QStringLiteral("cjkKeyEllipsisAscii"),
              QStringLiteral("中文.."), 4, 4, QStringLiteral("key ."),
              keyAction(QStringLiteral(".")), QStringLiteral("中文……"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyEllipsisFull"),
              QStringLiteral("中文。。"), 4, 4, QStringLiteral("key ."),
              keyAction(QStringLiteral(".")), QStringLiteral("中文……"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyEllipsisFullFull"),
              QStringLiteral("中文。。"), 4, 4, QStringLiteral("key 。"),
              keyAction(QStringLiteral("。")), QStringLiteral("中文……"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyEllipsisMixedDotFullNegative"),
              QStringLiteral("中文.。"), 4, 4, QStringLiteral("key ."),
              keyAction(QStringLiteral(".")), QStringLiteral("中文.。."), 5);
    cjkExpect(checks, details, QStringLiteral("cjkKeyEllipsisMixedFullDotNegative"),
              QStringLiteral("中文.."), 4, 4, QStringLiteral("key 。"),
              keyAction(QStringLiteral("。")), QStringLiteral("中文..。"), 5);
    cjkExpect(checks, details, QStringLiteral("cjkKeyEllipsisFullDotNegative"),
              QStringLiteral("中文。."), 4, 4, QStringLiteral("key 。"),
              keyAction(QStringLiteral("。")), QStringLiteral("中文。.。"), 5);
    cjkExpect(checks, details, QStringLiteral("cjkKeyEllipsisHalfDotTailNegative"),
              QStringLiteral("中文。."), 4, 4, QStringLiteral("key ."),
              keyAction(QStringLiteral(".")), QStringLiteral("中文。.."), 5);
    cjkExpect(checks, details, QStringLiteral("cjkKeyEmdash"),
              QStringLiteral("中文-"), 3, 3, QStringLiteral("key -"),
              keyAction(QStringLiteral("-")), QStringLiteral("中文——"), 4);

    // --- CJK Fix: Protected Region KeyPress (PROTECT-KEY-001..005) ---
    cjkExpect(checks, details, QStringLiteral("protectKeyInlineCode"),
              QStringLiteral("`中文`"), 2, 2, QStringLiteral("key , inside inline code"),
              keyAction(QStringLiteral(",")), QStringLiteral("`中,文`"), 3);
    cjkExpect(checks, details, QStringLiteral("protectKeyInlineQuote"),
              QStringLiteral("`中文`"), 3, 3, QStringLiteral("key \" inside inline code"),
              keyAction(QStringLiteral("\"")), QStringLiteral("`中文\"`"), 4);
    cjkExpect(checks, details, QStringLiteral("protectKeyChainColon"),
              QStringLiteral("`中文：`"), 4, 4, QStringLiteral("key : inside inline code"),
              keyAction(QStringLiteral(":")), QStringLiteral("`中文：:`"), 5);

    // --- 行中引号：单开符 + 闭合收尾（MID-QUOTE-001..011） ---
    cjkExpect(checks, details, QStringLiteral("midQuoteAsciiSingleOpen"),
              QStringLiteral("abc"), 1, 1, QStringLiteral("key \" at midline"),
              keyAction(QStringLiteral("\"")), QStringLiteral("a\"bc"), 2);
    cjkExpect(checks, details, QStringLiteral("midQuoteSingleQuoteSingleOpen"),
              QStringLiteral("abc"), 1, 1, QStringLiteral("key ' at midline"),
              keyAction(QStringLiteral("'")), QStringLiteral("a'bc"), 2);
    cjkExpect(checks, details, QStringLiteral("midQuoteBacktickSingleOpen"),
              QStringLiteral("abc"), 1, 1, QStringLiteral("key ` at midline"),
              keyAction(QStringLiteral("`")), QStringLiteral("a`bc"), 2);
    cjkExpect(checks, details, QStringLiteral("midQuoteCurlySingleOpen"),
              QStringLiteral("abc"), 1, 1, QStringLiteral("key “ at midline"),
              keyAction(QStringLiteral("“")), QStringLiteral("a“bc"), 2);
    cjkExpect(checks, details, QStringLiteral("midQuoteAsciiCloseNoCjk"),
              QStringLiteral("a\"bc"), 3, 3, QStringLiteral("key \" to close wrap"),
              keyAction(QStringLiteral("\"")), QStringLiteral("a\"b\"c"), 4);
    cjkExpect(checks, details, QStringLiteral("midQuoteAsciiCloseCjkConverts"),
              QStringLiteral("a\"中文x"), 4, 4, QStringLiteral("key \" closes CJK wrap"),
              keyAction(QStringLiteral("\"")), QStringLiteral("a “中文” x"), 6);
    cjkExpect(checks, details, QStringLiteral("midQuoteCloseSkipsExisting"),
              QStringLiteral("a\"b\"c"), 3, 3, QStringLiteral("key \" skips existing closer"),
              keyAction(QStringLiteral("\"")), QStringLiteral("a\"b\"c"), 4);
    cjkExpect(checks, details, QStringLiteral("midQuoteCloseTriggersAutoSpacing"),
              QStringLiteral("中文“Ax"), 4, 4, QStringLiteral("key ” closes wrap"),
              keyAction(QStringLiteral("”")), QStringLiteral("中文 “A” x"), 6);
    cjkExpect(checks, details, QStringLiteral("midQuoteLineEndKeepsAutoPair"),
              QStringLiteral("abc"), 3, 3, QStringLiteral("key \" at line end"),
              keyAction(QStringLiteral("\"")), QStringLiteral("abc\"\""), 4);
    {
        // 输入与自动空格必须合并为一次撤销（UNDO-GROUP-001）。
        setTextAndSelection(QStringLiteral("中文"), 2, 2);
        keyPress(QStringLiteral("\""), QStringLiteral("\""));
        QThread::msleep(30);
        const QString spaced = editorText();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString undone = editorText();
        addCheck(checks, details, QStringLiteral("undoGroupsQuoteAndSpacing"),
                 spaced == QStringLiteral("中文 “”") && undone == QStringLiteral("中文"),
                 QJsonObject{{QStringLiteral("spaced"), spaced},
                             {QStringLiteral("undone"), undone}});
    }
    cjkExpect(checks, details, QStringLiteral("midQuoteImeCurlySingleOpen"),
              QStringLiteral("abc"), 1, 1, QStringLiteral("IME commit “ at midline"),
              [] { return inputMethodCommit(QStringLiteral("“")); },
              QStringLiteral("a“bc"), 2);
    cjkExpect(checks, details, QStringLiteral("midQuoteImeCurlyCloseCjk"),
              QStringLiteral("a“中文x"), 4, 4, QStringLiteral("IME commit ” closes wrap"),
              [] { return inputMethodCommit(QStringLiteral("”")); },
              QStringLiteral("a“中文”x"), 5);
    cjkExpect(checks, details, QStringLiteral("midQuoteImeAsciiCloseCjkConverts"),
              QStringLiteral("a\"中文x"), 4, 4, QStringLiteral("IME commit \" closes CJK wrap"),
              [] { return inputMethodCommit(QStringLiteral("\"")); },
              QStringLiteral("a “中文” x"), 6);
    cjkExpect(checks, details, QStringLiteral("midQuoteImeBacktickSingleOpen"),
              QStringLiteral("abc"), 1, 1, QStringLiteral("IME commit ` at midline"),
              [] { return inputMethodCommit(QStringLiteral("`")); },
              QStringLiteral("a`bc"), 2);
    cjkExpect(checks, details, QStringLiteral("midQuoteKeyBacktickCloseCjkSpacing"),
              QStringLiteral("a`中文x"), 4, 4,
              QStringLiteral("key ` closes wrap with boundary spacing"),
              keyAction(QStringLiteral("`")), QStringLiteral("a `中文` x"), 5);
    cjkExpect(checks, details, QStringLiteral("midQuoteImeBacktickCloseCjkSpacing"),
              QStringLiteral("a`中文x"), 4, 4,
              QStringLiteral("IME commit ` closes wrap with boundary spacing"),
              [] { return inputMethodCommit(QStringLiteral("`")); },
              QStringLiteral("a `中文` x"), 5);

    // --- `·`（U+00B7）反引号别名（DOT-ALIAS-001..012） ---
    // 紧贴字符输入单个 `·` 保持字面；连续两个 `·` 生成反引号对（光标居中）。
    cjkExpect(checks, details, QStringLiteral("dotAliasLiteralAfterChar"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key · directly after CJK"),
              keyAction(QStringLiteral("·")), QStringLiteral("中文·"), 3);
    cjkExpect(checks, details, QStringLiteral("dotAliasImeLiteral"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("IME commit · directly after CJK"),
              [] { return inputMethodCommit(QStringLiteral("·")); },
              QStringLiteral("中文·"), 3);
    cjkExpect(checks, details, QStringLiteral("dotAliasDoubleLineEndPair"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key · twice directly after CJK"),
              [] {
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  return keyPress(QStringLiteral("·"), QStringLiteral("·"));
              },
              QStringLiteral("中文 ``"), 4);
    cjkExpect(checks, details, QStringLiteral("dotAliasImeDoubleLineEndPair"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("IME commit · twice directly after CJK"),
              [] {
                  inputMethodCommit(QStringLiteral("·"));
                  return inputMethodCommit(QStringLiteral("·"));
              },
              QStringLiteral("中文 ``"), 4);
    cjkExpect(checks, details, QStringLiteral("dotAliasDoubleMidlinePair"),
              QStringLiteral("中文"), 1, 1, QStringLiteral("key · twice midline"),
              [] {
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  return keyPress(QStringLiteral("·"), QStringLiteral("·"));
              },
              QStringLiteral("中 `` 文"), 3);
    cjkExpect(checks, details, QStringLiteral("dotAliasImeDoubleMidlinePair"),
              QStringLiteral("中文"), 1, 1, QStringLiteral("IME commit · twice midline"),
              [] {
                  inputMethodCommit(QStringLiteral("·"));
                  return inputMethodCommit(QStringLiteral("·"));
              },
              QStringLiteral("中 `` 文"), 3);
    // 仅在空格后单点号时触发反引号转换。
    cjkExpect(checks, details, QStringLiteral("dotAliasAfterSpaceBecomesBacktick"),
              QStringLiteral("中文 "), 3, 3, QStringLiteral("key · after space"),
              keyAction(QStringLiteral("·")), QStringLiteral("中文 ``"), 4);
    cjkExpect(checks, details, QStringLiteral("dotAliasAsciiAfterSpace"),
              QStringLiteral("abc "), 4, 4, QStringLiteral("key · after space (ASCII)"),
              keyAction(QStringLiteral("·")), QStringLiteral("abc ``"), 5);
    cjkExpect(checks, details, QStringLiteral("dotAliasImeAfterSpace"),
              QStringLiteral("中文 "), 3, 3, QStringLiteral("IME commit · after space"),
              [] { return inputMethodCommit(QStringLiteral("·")); },
              QStringLiteral("中文 ``"), 4);
    cjkExpect(checks, details, QStringLiteral("dotAliasSpaceAfterDotKeepsLiteral"),
              QStringLiteral("中文·"), 3, 3, QStringLiteral("key space after ·"),
              keyAction(QStringLiteral(" ")), QStringLiteral("中文· "), 4);
    // 行首与空行的连续双 `·` 同样生成反引号对（DOT2-001..004）。
    cjkExpect(checks, details, QStringLiteral("dotAliasDoubleLineStartPair"),
              QStringLiteral("中文"), 0, 0, QStringLiteral("key · twice at line start"),
              [] {
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  return keyPress(QStringLiteral("·"), QStringLiteral("·"));
              },
              QStringLiteral("`` 中文"), 1);
    cjkExpect(checks, details, QStringLiteral("dotAliasImeDoubleLineStartPair"),
              QStringLiteral("中文"), 0, 0, QStringLiteral("IME commit · twice at line start"),
              [] {
                  inputMethodCommit(QStringLiteral("·"));
                  return inputMethodCommit(QStringLiteral("·"));
              },
              QStringLiteral("`` 中文"), 1);
    cjkExpect(checks, details, QStringLiteral("dotAliasDoubleEmptyLinePair"),
              QString(), 0, 0, QStringLiteral("key · twice on empty line"),
              [] {
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  return keyPress(QStringLiteral("·"), QStringLiteral("·"));
              },
              QStringLiteral("``"), 1);
    cjkExpect(checks, details, QStringLiteral("dotAliasImeDoubleEmptyLinePair"),
              QString(), 0, 0, QStringLiteral("IME commit · twice on empty line"),
              [] {
                  inputMethodCommit(QStringLiteral("·"));
                  return inputMethodCommit(QStringLiteral("·"));
              },
              QStringLiteral("``"), 1);
    // 完全空行上连按三个 `·`：`` 对升级为大代码块围栏（DOT2-FENCE-001..002）。
    cjkExpect(checks, details, QStringLiteral("dotAliasTripleEmptyLineFence"),
              QString(), 0, 0, QStringLiteral("key · three times on empty line"),
              [] {
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  return keyPress(QStringLiteral("·"), QStringLiteral("·"));
              },
              QStringLiteral("```\n```"), 3);
    cjkExpect(checks, details, QStringLiteral("dotAliasImeTripleEmptyLineFence"),
              QString(), 0, 0, QStringLiteral("IME commit · three times on empty line"),
              [] {
                  inputMethodCommit(QStringLiteral("·"));
                  inputMethodCommit(QStringLiteral("·"));
                  return inputMethodCommit(QStringLiteral("·"));
              },
              QStringLiteral("```\n```"), 3);
    // 行尾 ASCII 双 `·` 同样成对（DOT2-005）。
    cjkExpect(checks, details, QStringLiteral("dotAliasDoubleLineEndAsciiPair"),
              QStringLiteral("abc"), 3, 3, QStringLiteral("key · twice after ASCII"),
              [] {
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  return keyPress(QStringLiteral("·"), QStringLiteral("·"));
              },
              QStringLiteral("abc ``"), 5);
    // 围栏代码块内：双 `·` 与三连点均保持字面（DOT2-FENCE-003..005）。
    cjkExpect(checks, details, QStringLiteral("dotAliasDoubleInsideFenceLiteral"),
              QStringLiteral("```\n\n```"), 4, 4,
              QStringLiteral("key · twice inside fenced code"),
              [] {
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  return keyPress(QStringLiteral("·"), QStringLiteral("·"));
              },
              QStringLiteral("```\n··\n```"), 6);
    cjkExpect(checks, details, QStringLiteral("dotAliasImeDoubleInsideFenceLiteral"),
              QStringLiteral("```\n\n```"), 4, 4,
              QStringLiteral("IME commit · twice inside fenced code"),
              [] {
                  inputMethodCommit(QStringLiteral("·"));
                  return inputMethodCommit(QStringLiteral("·"));
              },
              QStringLiteral("```\n··\n```"), 6);
    cjkExpect(checks, details, QStringLiteral("dotAliasTripleInsideFenceLiteral"),
              QStringLiteral("```\n\n```"), 4, 4,
              QStringLiteral("key · three times inside fenced code"),
              [] {
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  return keyPress(QStringLiteral("·"), QStringLiteral("·"));
              },
              QStringLiteral("```\n···\n```"), 7);
    // 有选区时输入单个 `·` 等价于 `` ` ``：包裹选区并触发自动空格（DOT2-WRAP-001）。
    cjkExpect(checks, details, QStringLiteral("dotAliasSelectionWrapsBackticks"),
              QStringLiteral("中文"), 1, 2, QStringLiteral("key · with selection"),
              keyAction(QStringLiteral("·")),
              QStringLiteral("中 `文`"), -1, 3, 4);
    {
        // 撤销：光标回到输入 `·` 之前的位置（DOT-UNDO-CURSOR-001..003）。
        setTextAndSelection(QStringLiteral("中文 "), 3, 3);
        keyPress(QStringLiteral("·"), QStringLiteral("·"));
        QThread::msleep(30);
        const QString spaced = editorText();
        const int spacedCursor = editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString undone = editorText();
        const int undoneCursor = editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("dotAliasUndoRestoresPosition"),
                 spaced == QStringLiteral("中文 ``") && spacedCursor == 4
                     && undone == QStringLiteral("中文 ") && undoneCursor == 3,
                 QJsonObject{{QStringLiteral("spaced"), spaced},
                             {QStringLiteral("spacedCursor"), spacedCursor},
                             {QStringLiteral("undone"), undone},
                             {QStringLiteral("undoneCursor"), undoneCursor}});

        // IME 提交的插入与转换/自动空格必须合并为一次撤销（DOT-UNDO-IME-001..002）。
        setTextAndSelection(QStringLiteral("中文 "), 3, 3);
        inputMethodCommit(QStringLiteral("·"));
        QThread::msleep(30);
        const QString imeSpaced = editorText();
        const int imeSpacedCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString imeUndone = editorText();
        const int imeUndoneCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("dotAliasImeUndoRestoresPosition"),
                 imeSpaced == QStringLiteral("中文 ``") && imeSpacedCursor == 4
                     && imeUndone == QStringLiteral("中文 ") && imeUndoneCursor == 3,
                 QJsonObject{{QStringLiteral("imeSpaced"), imeSpaced},
                             {QStringLiteral("imeSpacedCursor"), imeSpacedCursor},
                             {QStringLiteral("imeUndone"), imeUndone},
                             {QStringLiteral("imeUndoneCursor"), imeUndoneCursor}});

        setTextAndSelection(QStringLiteral("中文"), 2, 2);
        inputMethodCommit(QStringLiteral("A"));
        QThread::msleep(30);
        const QString imeAutoSpaced = editorText();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString imeAutoUndone = editorText();
        addCheck(checks, details, QStringLiteral("imeCommitAutoSpacingUndoRestores"),
                 imeAutoSpaced == QStringLiteral("中文 A")
                     && imeAutoUndone == QStringLiteral("中文"),
                 QJsonObject{{QStringLiteral("imeAutoSpaced"), imeAutoSpaced},
                             {QStringLiteral("imeAutoUndone"), imeAutoUndone}});
    }
    {
        // 双 `·` 成对、空行围栏升级与选区包裹均合并为一次撤销（DOT2-UNDO-001..005）。
        setTextAndSelection(QStringLiteral("中文"), 1, 1);
        keyPress(QStringLiteral("·"), QStringLiteral("·"));
        keyPress(QStringLiteral("·"), QStringLiteral("·"));
        QThread::msleep(30);
        const QString paired = editorText();
        const int pairedCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString pairedUndone = editorText();
        const int pairedUndoneCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("dotAliasDoubleUndoRestoresPosition"),
                 paired == QStringLiteral("中 `` 文") && pairedCursor == 3
                     && pairedUndone == QStringLiteral("中文") && pairedUndoneCursor == 1,
                 QJsonObject{{QStringLiteral("paired"), paired},
                             {QStringLiteral("pairedCursor"), pairedCursor},
                             {QStringLiteral("undone"), pairedUndone},
                             {QStringLiteral("undoneCursor"), pairedUndoneCursor}});

        // IME 双 `·` 同样合并为一次撤销。
        setTextAndSelection(QStringLiteral("中文"), 2, 2);
        inputMethodCommit(QStringLiteral("·"));
        inputMethodCommit(QStringLiteral("·"));
        QThread::msleep(30);
        const QString imePaired = editorText();
        const int imePairedCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString imePairedUndone = editorText();
        const int imePairedUndoneCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("dotAliasImeDoubleUndoRestoresAll"),
                 imePaired == QStringLiteral("中文 ``") && imePairedCursor == 4
                     && imePairedUndone == QStringLiteral("中文")
                     && imePairedUndoneCursor == 2,
                 QJsonObject{{QStringLiteral("paired"), imePaired},
                             {QStringLiteral("pairedCursor"), imePairedCursor},
                             {QStringLiteral("undone"), imePairedUndone},
                             {QStringLiteral("undoneCursor"), imePairedUndoneCursor}});

        setTextAndSelection(QString(), 0, 0);
        keyPress(QStringLiteral("·"), QStringLiteral("·"));
        keyPress(QStringLiteral("·"), QStringLiteral("·"));
        keyPress(QStringLiteral("·"), QStringLiteral("·"));
        QThread::msleep(30);
        const QString fenced = editorText();
        const int fencedCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString fencedUndone = editorText();
        const int fencedUndoneCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("dotAliasTripleFenceUndoRestoresAll"),
                 fenced == QStringLiteral("```\n```") && fencedCursor == 3
                     && fencedUndone == QString() && fencedUndoneCursor == 0,
                 QJsonObject{{QStringLiteral("fenced"), fenced},
                             {QStringLiteral("fencedCursor"), fencedCursor},
                             {QStringLiteral("undone"), fencedUndone},
                             {QStringLiteral("undoneCursor"), fencedUndoneCursor}});

        // IME 空行三连点围栏同样合并为一次撤销。
        setTextAndSelection(QString(), 0, 0);
        inputMethodCommit(QStringLiteral("·"));
        inputMethodCommit(QStringLiteral("·"));
        inputMethodCommit(QStringLiteral("·"));
        QThread::msleep(30);
        const QString imeFenced = editorText();
        const int imeFencedCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString imeFencedUndone = editorText();
        const int imeFencedUndoneCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("dotAliasImeTripleFenceUndoRestoresAll"),
                 imeFenced == QStringLiteral("```\n```") && imeFencedCursor == 3
                     && imeFencedUndone == QString() && imeFencedUndoneCursor == 0,
                 QJsonObject{{QStringLiteral("fenced"), imeFenced},
                             {QStringLiteral("fencedCursor"), imeFencedCursor},
                             {QStringLiteral("undone"), imeFencedUndone},
                             {QStringLiteral("undoneCursor"), imeFencedUndoneCursor}});

        setTextAndSelection(QStringLiteral("中文"), 1, 2);
        keyPress(QStringLiteral("·"), QStringLiteral("·"));
        QThread::msleep(30);
        const QString wrapped = editorText();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString wrappedUndone = editorText();
        const int wrappedUndoneCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("dotAliasSelectionWrapUndoRestores"),
                 wrapped == QStringLiteral("中 `文`") && wrappedUndone == QStringLiteral("中文")
                     && wrappedUndoneCursor == 2,
                 QJsonObject{{QStringLiteral("wrapped"), wrapped},
                             {QStringLiteral("undone"), wrappedUndone},
                             {QStringLiteral("undoneCursor"), wrappedUndoneCursor}});
    }
    {
        // 行中双反引号：光标位于两个反引号中间，两侧自动空格（BT-PAIR-001..002）。
        setTextAndSelection(QStringLiteral("中文xyz"), 4, 4);
        keyPress(QStringLiteral("`"), QStringLiteral("`"));
        QThread::msleep(30);
        keyPress(QStringLiteral("`"), QStringLiteral("`"));
        QThread::msleep(30);
        const QString pairText = editorText();
        const int pairCursor = editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("backtickPairMidlineSpacing"),
                 pairText == QStringLiteral("中文xy `` z") && pairCursor == 6,
                 QJsonObject{{QStringLiteral("text"), pairText},
                             {QStringLiteral("cursor"), pairCursor}});

        setTextAndSelection(QString(), 0, 0);
        keyPress(QStringLiteral("`"), QStringLiteral("`"));
        QThread::msleep(30);
        keyPress(QStringLiteral("`"), QStringLiteral("`"));
        QThread::msleep(30);
        keyPress(QStringLiteral("`"), QStringLiteral("`"));
        QThread::msleep(30);
        const QString fenceText = editorText();
        addCheck(checks, details, QStringLiteral("backtickFenceUpgradeLineStart"),
                 fenceText == QStringLiteral("```\n```"),
                 QJsonObject{{QStringLiteral("text"), fenceText}});
    }
    {
        // 行首围栏自动补全：光标后本行有文字时，闭合围栏必须单独成行，
        // 后续文字移到闭合围栏下一行，避免闭合边界后紧跟文字
        // （FENCE-SUFFIX-001..003：键盘三次 / IME / 单次整串）。
        cjkExpect(checks, details, QStringLiteral("fenceSuffixOwnLineKeyboard"),
                  QStringLiteral("测试文字"), 0, 0,
                  QStringLiteral("key ` three times at line start before text"),
                  [] {
                      keyPress(QStringLiteral("`"), QStringLiteral("`"));
                      keyPress(QStringLiteral("`"), QStringLiteral("`"));
                      return keyPress(QStringLiteral("`"), QStringLiteral("`"));
                  },
                  QStringLiteral("```\n```\n测试文字"), 3);
        cjkExpect(checks, details, QStringLiteral("fenceSuffixOwnLineIme"),
                  QStringLiteral("测试文字"), 0, 0,
                  QStringLiteral("IME commit ``` at line start before text"),
                  [] { return inputMethodCommit(QStringLiteral("```")); },
                  QStringLiteral("```\n```\n测试文字"), 3);
        cjkExpect(checks, details, QStringLiteral("fenceSuffixOwnLineSingleEvent"),
                  QStringLiteral("测试文字"), 0, 0,
                  QStringLiteral("single key ``` at line start before text"),
                  [] { return keyPress(QStringLiteral("```"), QStringLiteral("```")); },
                  QStringLiteral("```\n```\n测试文字"), 3);
        // 行首围栏自动补全：光标后无文字时不补多余换行（FENCE-EMPTY-001..002）。
        cjkExpect(checks, details, QStringLiteral("fenceEmptyLineNoExtraNewlineIme"),
                  QString(), 0, 0,
                  QStringLiteral("IME commit ``` on empty line"),
                  [] { return inputMethodCommit(QStringLiteral("```")); },
                  QStringLiteral("```\n```"), 3);
        cjkExpect(checks, details, QStringLiteral("fenceNextLineTextStays"),
                  QStringLiteral("\n测试文字"), 0, 0,
                  QStringLiteral("key ` three times on empty line above text"),
                  [] {
                      keyPress(QStringLiteral("`"), QStringLiteral("`"));
                      keyPress(QStringLiteral("`"), QStringLiteral("`"));
                      return keyPress(QStringLiteral("`"), QStringLiteral("`"));
                  },
                  QStringLiteral("```\n```\n测试文字"), 3);
        cjkExpect(checks, details, QStringLiteral("fenceTrailingEmptyLineStays"),
                  QStringLiteral("测试文字\n"), 5, 5,
                  QStringLiteral("key ` three times on trailing empty line"),
                  [] {
                      keyPress(QStringLiteral("`"), QStringLiteral("`"));
                      keyPress(QStringLiteral("`"), QStringLiteral("`"));
                      return keyPress(QStringLiteral("`"), QStringLiteral("`"));
                  },
                  QStringLiteral("测试文字\n```\n```"), 8);
        // 光标后只有空白：空白不算文字，不加换行（FENCE-WHITESPACE-001）。
        cjkExpect(checks, details, QStringLiteral("fenceWhitespaceSuffixNoNewline"),
                  QStringLiteral("  "), 0, 0,
                  QStringLiteral("key ` three times before spaces only"),
                  [] {
                      keyPress(QStringLiteral("`"), QStringLiteral("`"));
                      keyPress(QStringLiteral("`"), QStringLiteral("`"));
                      return keyPress(QStringLiteral("`"), QStringLiteral("`"));
                  },
                  QStringLiteral("```\n```  "), 3);
        // 仅列 0 行首触发：行中、行末、带缩进行首均不自动补全围栏，
        // IME 提交保持字面（FENCE-SCOPE-001..003）。
        cjkExpect(checks, details, QStringLiteral("fenceMidLineNoCompletionIme"),
                  QStringLiteral("中文字"), 1, 1,
                  QStringLiteral("IME commit ``` in line middle"),
                  [] { return inputMethodCommit(QStringLiteral("```")); },
                  QStringLiteral("中```文字"), 4);
        cjkExpect(checks, details, QStringLiteral("fenceLineEndNoCompletionIme"),
                  QStringLiteral("测试文字"), 4, 4,
                  QStringLiteral("IME commit ``` at line end"),
                  [] { return inputMethodCommit(QStringLiteral("```")); },
                  QStringLiteral("测试文字```"), 7);
        cjkExpect(checks, details, QStringLiteral("fenceIndentedLineStartNoCompletionIme"),
                  QStringLiteral("  测试文字"), 2, 2,
                  QStringLiteral("IME commit ``` on indented line start"),
                  [] { return inputMethodCommit(QStringLiteral("```")); },
                  QStringLiteral("  ```测试文字"), 5);
        // 键盘三次单独输入在行中、行末、带缩进行首同样不产生围栏（回归保护）。
        const auto keyTriple = [] {
            keyPress(QStringLiteral("`"), QStringLiteral("`"));
            keyPress(QStringLiteral("`"), QStringLiteral("`"));
            return keyPress(QStringLiteral("`"), QStringLiteral("`"));
        };
        setTextAndSelection(QStringLiteral("中文字"), 1, 1);
        keyTriple();
        QThread::msleep(30);
        const QString midLineText = editorText();
        setTextAndSelection(QStringLiteral("测试文字"), 4, 4);
        keyTriple();
        QThread::msleep(30);
        const QString lineEndText = editorText();
        setTextAndSelection(QStringLiteral("  测试文字"), 2, 2);
        keyTriple();
        QThread::msleep(30);
        const QString indentedText = editorText();
        addCheck(checks, details,
                 QStringLiteral("fenceNoCompletionOutsideLineStartKeyboard"),
                 !midLineText.contains(QStringLiteral("\n```"))
                     && !lineEndText.contains(QStringLiteral("\n```"))
                     && !indentedText.contains(QStringLiteral("\n```")),
                 QJsonObject{{QStringLiteral("midLine"), midLineText},
                             {QStringLiteral("lineEnd"), lineEndText},
                             {QStringLiteral("indented"), indentedText}});
        // 行首带字围栏补全后一次撤销恢复原文（FENCE-UNDO-001）。
        setTextAndSelection(QStringLiteral("测试文字"), 0, 0);
        inputMethodCommit(QStringLiteral("```"));
        QThread::msleep(30);
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString undoneText = editorText();
        const int undoneCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("fenceSuffixOwnLineImeUndo"),
                 undoneText == QStringLiteral("测试文字") && undoneCursor == 0,
                 QJsonObject{{QStringLiteral("undoneText"), undoneText},
                             {QStringLiteral("undoneCursor"), undoneCursor}});
    }
    {
        // 先输后引号、再输前引号也必须与先前后后一样触发两侧自动空格
        // （BT-REVERSE-001..004：键盘/IME × CJK/ASCII，引号全角转换）。
        const auto moveCursor = [](int position) {
            return request(QStringLiteral("testSetSelection"),
                           {{QStringLiteral("start"), position},
                            {QStringLiteral("end"), position}});
        };

        setTextAndSelection(QStringLiteral("前中文后"), 3, 3);
        keyPress(QStringLiteral("`"), QStringLiteral("`"));
        QThread::msleep(30);
        moveCursor(1);
        keyPress(QStringLiteral("`"), QStringLiteral("`"));
        QThread::msleep(30);
        const QString reverseKeyboardText = editorText();
        const int reverseKeyboardCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("backtickReverseOrderKeyboardSpacing"),
                 reverseKeyboardText == QStringLiteral("前 `中文` 后")
                     && reverseKeyboardCursor == 3,
                 QJsonObject{{QStringLiteral("text"), reverseKeyboardText},
                             {QStringLiteral("cursor"), reverseKeyboardCursor}});

        setTextAndSelection(QStringLiteral("前中文后"), 3, 3);
        inputMethodCommit(QStringLiteral("`"));
        QThread::msleep(30);
        moveCursor(1);
        inputMethodCommit(QStringLiteral("`"));
        QThread::msleep(30);
        const QString reverseImeText = editorText();
        const int reverseImeCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("backtickReverseOrderImeSpacing"),
                 reverseImeText == QStringLiteral("前 `中文` 后")
                     && reverseImeCursor == 3,
                 QJsonObject{{QStringLiteral("text"), reverseImeText},
                             {QStringLiteral("cursor"), reverseImeCursor}});

        setTextAndSelection(QStringLiteral("abcd"), 3, 3);
        keyPress(QStringLiteral("`"), QStringLiteral("`"));
        QThread::msleep(30);
        moveCursor(1);
        keyPress(QStringLiteral("`"), QStringLiteral("`"));
        QThread::msleep(30);
        const QString reverseAsciiText = editorText();
        addCheck(checks, details, QStringLiteral("backtickReverseOrderAsciiSpacing"),
                 reverseAsciiText == QStringLiteral("a `bc` d"),
                 QJsonObject{{QStringLiteral("text"), reverseAsciiText}});

        setTextAndSelection(QStringLiteral("前中文后"), 3, 3);
        keyPress(QStringLiteral("\""), QStringLiteral("\""));
        QThread::msleep(30);
        moveCursor(1);
        keyPress(QStringLiteral("\""), QStringLiteral("\""));
        QThread::msleep(30);
        const QString reverseQuoteText = editorText();
        addCheck(checks, details, QStringLiteral("quoteReverseOrderFullwidthSpacing"),
                 reverseQuoteText == QStringLiteral("前 “中文” 后"),
                 QJsonObject{{QStringLiteral("text"), reverseQuoteText}});
    }
    cjkExpect(checks, details, QStringLiteral("protectKeyInlineFormula"),
              QStringLiteral("$中文..$"), 5, 5, QStringLiteral("key . inside inline formula"),
              keyAction(QStringLiteral(".")), QStringLiteral("$中文...$"), 6);
    cjkExpect(checks, details, QStringLiteral("protectKeyFence"),
              QStringLiteral("```\n中文-\n```"), 7, 7,
              QStringLiteral("key - inside fenced code"),
              keyAction(QStringLiteral("-")), QStringLiteral("```\n中文--\n```"), 8);
    cjkExpect(checks, details, QStringLiteral("protectKeyBlockFormula"),
              QStringLiteral("$$\n中文\n$$"), 5, 5,
              QStringLiteral("key , inside block formula"),
              keyAction(QStringLiteral(",")), QStringLiteral("$$\n中文,\n$$"), 6);
    setTextAndSelection(QStringLiteral("$$中文$$"), 4, 4);
    keyPress(QStringLiteral(","), QStringLiteral(","));
    QThread::msleep(30);
    const QString singleLineFormulaCommaText = editorText();
    const bool singleLineFormulaCommaKept =
        singleLineFormulaCommaText == QStringLiteral("$$中文,$$");
    setTextAndSelection(QStringLiteral("$$中文$$"), 3, 3);
    keyPress(QStringLiteral("A"), QStringLiteral("A"));
    QThread::msleep(30);
    const QString singleLineFormulaSpacingText = editorText();
    const bool singleLineFormulaSpacingKept =
        singleLineFormulaSpacingText == QStringLiteral("$$中A文$$");
    const QJsonObject singleLineFormulaStatus = editorStatus();
    addCjkCheck(checks, details, QStringLiteral("protectKeySingleLineFormula"),
                singleLineFormulaCommaKept && singleLineFormulaSpacingKept,
                QStringLiteral("$$中文$$"), QStringLiteral("key , / key A inside $$...$$"),
                QStringLiteral("$$中文,$$ 与 $$中A文$$"),
                singleLineFormulaCommaText + QStringLiteral(" 与 ")
                    + singleLineFormulaSpacingText,
                singleLineFormulaStatus);

    // --- CJK Fix: Real-time Spacing (LIVE-001..009) ---
    cjkExpect(checks, details, QStringLiteral("liveCjkThenAscii"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key A"),
              keyAction(QStringLiteral("A")), QStringLiteral("中文 A"), 4);
    cjkExpect(checks, details, QStringLiteral("liveAsciiThenCjk"),
              QStringLiteral("ABC"), 3, 3, QStringLiteral("key 中"),
              keyAction(QStringLiteral("中")), QStringLiteral("ABC 中"), 5);
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

    // --- CJK Fix: Pure Parser Tests (PARSE-001..019) ---
    using CjkText::ProtectedKind;
    parseExpect(checks, details, QStringLiteral("parseFencedBlock"),
                QStringLiteral("```js\n中文ABC\n```"),
                {{0, 15, 6, 12, ProtectedKind::FencedCode}}, {});
    parseExpect(checks, details, QStringLiteral("parseInvalidFenceTrailing"),
                QStringLiteral("```js\n中文ABC\n```oops\n后续ABC\n```"),
                {{0, 29, 6, 26, ProtectedKind::FencedCode}}, {});
    parseExpect(checks, details, QStringLiteral("parseLongerOpeningRun"),
                QStringLiteral("````\nabc\n```\nxyz\n````"),
                {{0, 21, 5, 17, ProtectedKind::FencedCode}}, {});
    parseExpect(checks, details, QStringLiteral("parseLongerClosingRun"),
                QStringLiteral("```\nabc\n````"),
                {{0, 12, 4, 8, ProtectedKind::FencedCode}}, {});
    parseExpect(checks, details, QStringLiteral("parseTildeDoesNotClose"),
                QStringLiteral("```\n~~~\n```"),
                {{0, 11, 4, 8, ProtectedKind::FencedCode}}, {});
    parseExpect(checks, details, QStringLiteral("parseFormulaMarkerInsideFence"),
                QStringLiteral("```\n$$\n```\n中文A"),
                {{0, 10, 4, 7, ProtectedKind::FencedCode}}, {});
    parseExpect(checks, details, QStringLiteral("parseFenceMarkerInsideFormula"),
                QStringLiteral("$$\n```\n$$\n中文A"),
                {{0, 9, 3, 7, ProtectedKind::BlockFormula}}, {});
    parseExpect(checks, details, QStringLiteral("parseSingleLineFormula"),
                QStringLiteral("$$x+1$$\n中文A"),
                {{0, 7, 2, 5, ProtectedKind::BlockFormula}}, {});
    parseExpect(checks, details, QStringLiteral("parseSingleLineFormulaSpaces"),
                QStringLiteral("$$ x $$"),
                {{0, 7, 2, 5, ProtectedKind::BlockFormula}}, {});
    parseExpect(checks, details, QStringLiteral("parseMultiLineFormula"),
                QStringLiteral("$$\n中文\n$$"),
                {{0, 8, 3, 6, ProtectedKind::BlockFormula}}, {});
    parseExpect(checks, details, QStringLiteral("parseUnclosedFormula"),
                QStringLiteral("$$\n中文"),
                {{0, 5, 3, 5, ProtectedKind::BlockFormula}}, {});
    parseExpect(checks, details, QStringLiteral("parseUnclosedFormulaLikeLine"),
                QStringLiteral("$$x+1"), {}, {});
    parseExpect(checks, details, QStringLiteral("parseInlineCode"),
                QStringLiteral("`code`"),
                {}, {{0, 6, 1, 5, ProtectedKind::InlineCode}});
    parseExpect(checks, details, QStringLiteral("parseInlineCodeDoubleRun"),
                QStringLiteral("``code``"),
                {}, {{0, 8, 2, 6, ProtectedKind::InlineCode}});
    parseExpect(checks, details, QStringLiteral("parseInlineCodeLongerRunRejected"),
                QStringLiteral("`a``"), {}, {},
                {{0, 4, 1, 4, ProtectedKind::InlineCode}});
    parseExpect(checks, details, QStringLiteral("parseInlineCodeUnclosed"),
                QStringLiteral("`unclosed"), {}, {},
                {{0, 9, 1, 9, ProtectedKind::InlineCode}});
    parseExpect(checks, details, QStringLiteral("parseInlineFormula"),
                QStringLiteral("$x+1$"),
                {}, {{0, 5, 1, 4, ProtectedKind::InlineFormula}});
    parseExpect(checks, details, QStringLiteral("parseEscapedFormulaOpening"),
                QStringLiteral("\\$x$"), {}, {},
                {{3, 4, 4, 4, ProtectedKind::InlineFormula}});
    parseExpect(checks, details, QStringLiteral("parseInlineInsideBlockIgnored"),
                QStringLiteral("```\n`code` $x$\n```"),
                {{0, 18, 4, 15, ProtectedKind::FencedCode}}, {});
    parseExpect(checks, details, QStringLiteral("parseUnclosedFormulaTail"),
                QStringLiteral("末$中文ABC"), {}, {},
                {{1, 7, 2, 7, ProtectedKind::InlineFormula}});
    parseExpect(checks, details, QStringLiteral("parseUnclosedBacktickTail"),
                QStringLiteral("末`中文ABC"), {}, {},
                {{1, 7, 2, 7, ProtectedKind::InlineCode}});
    parseExpect(checks, details, QStringLiteral("parseClosedFormulaNoUnclosed"),
                QStringLiteral("$x$"), {}, {{0, 3, 1, 2, ProtectedKind::InlineFormula}}, {});
    parseExpect(checks, details, QStringLiteral("parseUnclosedAfterClosed"),
                QStringLiteral("$x$末$中文ABC"),
                {}, {{0, 3, 1, 2, ProtectedKind::InlineFormula}},
                {{4, 10, 5, 10, ProtectedKind::InlineFormula}});

    // --- CJK Fix: Pure Spacing Planner Tests (SPACE-001..018) ---
    const auto spaceExpect = [&](const QString &name, const QString &text,
                                 CjkText::BoundaryRange range,
                                 const QVector<int> &expected,
                                 const QString &expectedText,
                                 bool allowPendingDelimiterSpacing = true) {
        const CjkText::DocumentAnalysis analysis = CjkText::analyzeDocument(text);
        const QVector<int> actual = CjkText::collectSpacingInsertions(
            text, range, analysis, allowPendingDelimiterSpacing);
        const QString actualText = applyInsertions(text, actual);
        const auto joinInts = [](const QVector<int> &values) {
            QStringList parts;
            for (int value : values) {
                parts << QString::number(value);
            }
            return parts.join(QLatin1Char(','));
        };
        QJsonObject detail{
            {QStringLiteral("input"), text},
            {QStringLiteral("range"), QStringLiteral("[%1,%2]").arg(range.first).arg(range.last)},
            {QStringLiteral("expectedInsertions"), joinInts(expected)},
            {QStringLiteral("actualInsertions"), joinInts(actual)},
            {QStringLiteral("expectedText"), expectedText},
            {QStringLiteral("actualText"), actualText},
        };
        addCheck(checks, details, name,
                 actual == expected && actualText == expectedText, detail);
    };

    spaceExpect(QStringLiteral("spaceCjkAscii"), QStringLiteral("中文ABC"),
                {1, 4}, {2}, QStringLiteral("中文 ABC"));
    spaceExpect(QStringLiteral("spaceAsciiCjk"), QStringLiteral("ABC中文"),
                {1, 4}, {3}, QStringLiteral("ABC 中文"));
    spaceExpect(QStringLiteral("spaceCjkNumber"), QStringLiteral("中文123"),
                {1, 4}, {2}, QStringLiteral("中文 123"));
    spaceExpect(QStringLiteral("spaceAlnumOnly"), QStringLiteral("Python3"),
                {1, 6}, {}, QStringLiteral("Python3"));
    spaceExpect(QStringLiteral("spaceExistingSpace"), QStringLiteral("中文 ABC"),
                {1, 5}, {}, QStringLiteral("中文 ABC"));
    spaceExpect(QStringLiteral("spaceSoftSeparator"), QStringLiteral("中文，ABC"),
                {1, 5}, {}, QStringLiteral("中文，ABC"));
    spaceExpect(QStringLiteral("spaceHyphen"), QStringLiteral("中文-ABC"),
                {1, 5}, {}, QStringLiteral("中文-ABC"));
    spaceExpect(QStringLiteral("spaceSlash"), QStringLiteral("ABC/中文"),
                {1, 5}, {}, QStringLiteral("ABC/中文"));
    spaceExpect(QStringLiteral("spaceInlineCodeBoundaries"),
                QStringLiteral("中文`code`中文"), {1, 8}, {2, 8},
                QStringLiteral("中文 `code` 中文"));
    spaceExpect(QStringLiteral("spaceInlineFormulaBoundaries"),
                QStringLiteral("公式$x+1$成立"), {1, 7}, {2, 7},
                QStringLiteral("公式 $x+1$ 成立"));
    spaceExpect(QStringLiteral("spaceInlineCodeContent"),
                QStringLiteral("`中文ABC`"), {1, 7}, {},
                QStringLiteral("`中文ABC`"));
    spaceExpect(QStringLiteral("spaceInlineFormulaContent"),
                QStringLiteral("$中文ABC$"), {1, 6}, {},
                QStringLiteral("$中文ABC$"));
    spaceExpect(QStringLiteral("spaceInlineContentOnly"),
                QStringLiteral("`中文ABC`"), {2, 6}, {},
                QStringLiteral("`中文ABC`"));
    spaceExpect(QStringLiteral("spaceTruncatedDelimiter"),
                QStringLiteral("中文`code`文"), {1, 3}, {2},
                QStringLiteral("中文 `code`文"));
    spaceExpect(QStringLiteral("spaceInsideFence"),
                QStringLiteral("```\n中文ABC\n```"), {1, 13}, {},
                QStringLiteral("```\n中文ABC\n```"));
    spaceExpect(QStringLiteral("spaceAfterFence"),
                QStringLiteral("```\n```\n中文ABC"), {1, 13}, {10},
                QStringLiteral("```\n```\n中文 ABC"));
    spaceExpect(QStringLiteral("spaceAfterSingleLineFormula"),
                QStringLiteral("$$x$$\n中文ABC"), {1, 10}, {8},
                QStringLiteral("$$x$$\n中文 ABC"));
    spaceExpect(QStringLiteral("spaceEmptyAndSingleCharRange"),
                QStringLiteral("中文ABC"), {2, 1}, {},
                QStringLiteral("中文ABC"));
    spaceExpect(QStringLiteral("spacePendingBacktickStart"),
                QStringLiteral("中文``"), {1, 3}, {2},
                QStringLiteral("中文 ``"));
    spaceExpect(QStringLiteral("spacePendingDollarStart"),
                QStringLiteral("中文$"), {1, 2}, {2},
                QStringLiteral("中文 $"));
    spaceExpect(QStringLiteral("spaceEscapedDollarStart"),
                QStringLiteral("中文\\$"), {1, 3}, {},
                QStringLiteral("中文\\$"));
    spaceExpect(QStringLiteral("spaceUnclosedFormulaAltF"),
                QStringLiteral("末$中文ABC"), {1, 6}, {},
                QStringLiteral("末$中文ABC"), /*allowPendingDelimiterSpacing=*/false);
    {
        const QString unclosedText = QStringLiteral("末$中文ABC");
        const CjkText::DocumentAnalysis unclosedAnalysis =
            CjkText::analyzeDocument(unclosedText);
        const QVector<int> realtimeInsertions = CjkText::collectSpacingInsertions(
            unclosedText, {1, 6}, unclosedAnalysis, /*allowPendingDelimiterSpacing=*/true);
        addCheck(checks, details, QStringLiteral("spaceUnclosedFormulaRealtime"),
                 realtimeInsertions == QVector<int>{1},
                 QJsonObject{{QStringLiteral("insertions"),
                              [&realtimeInsertions] {
                                  QStringList values;
                                  for (int value : realtimeInsertions) {
                                      values << QString::number(value);
                                  }
                                  return values.join(QLatin1Char(','));
                              }()}});
    }

    // --- CJK Fix: Performance Scaling Record ---
    const QString perfSmall = buildPerfDocument(20000);
    const QString perfLarge = buildPerfDocument(100000);
    QElapsedTimer perfTimer;
    perfTimer.start();
    const CjkText::DocumentAnalysis smallAnalysis = CjkText::analyzeDocument(perfSmall);
    const qint64 smallAnalyzeNs = perfTimer.nsecsElapsed();
    perfTimer.restart();
    const QVector<int> smallInsertions = CjkText::collectSpacingInsertions(
        perfSmall, {1, static_cast<int>(perfSmall.size() - 1)}, smallAnalysis);
    const qint64 smallCollectNs = perfTimer.nsecsElapsed();
    perfTimer.restart();
    const CjkText::DocumentAnalysis largeAnalysis = CjkText::analyzeDocument(perfLarge);
    const qint64 largeAnalyzeNs = perfTimer.nsecsElapsed();
    perfTimer.restart();
    const QVector<int> largeInsertions = CjkText::collectSpacingInsertions(
        perfLarge, {1, static_cast<int>(perfLarge.size() - 1)}, largeAnalysis);
    const qint64 largeCollectNs = perfTimer.nsecsElapsed();
    perfTimer.restart();
    const CjkText::DocumentAnalysis imeAnalysis = CjkText::analyzeDocument(perfLarge);
    const qint64 imeAnalyzeNs = perfTimer.nsecsElapsed();
    perfTimer.restart();
    const int footprintStart = perfLarge.size() / 2;
    const QVector<int> imeInsertions = CjkText::collectSpacingInsertions(
        perfLarge, {footprintStart, footprintStart + 100}, imeAnalysis);
    const qint64 imeCollectNs = perfTimer.nsecsElapsed();
    const bool perfScalingOk = largeAnalyzeNs < 25 * qMax<qint64>(1, smallAnalyzeNs)
        && largeCollectNs < 25 * qMax<qint64>(1, smallCollectNs);
    const bool perfStructureOk = largeAnalysis.blockSpans.size() >= 3
        && largeAnalysis.inlineSpans.size() >= 100;
    addCheck(checks, details, QStringLiteral("cjkPerfScaling"),
             perfScalingOk && perfStructureOk,
             QJsonObject{
                 {QStringLiteral("smallLength"), perfSmall.size()},
                 {QStringLiteral("largeLength"), perfLarge.size()},
                 {QStringLiteral("smallAnalyzeMs"),
                  QString::number(smallAnalyzeNs / 1000000.0, 'f', 3)},
                 {QStringLiteral("smallCollectMs"),
                  QString::number(smallCollectNs / 1000000.0, 'f', 3)},
                 {QStringLiteral("largeAnalyzeMs"),
                  QString::number(largeAnalyzeNs / 1000000.0, 'f', 3)},
                 {QStringLiteral("largeCollectMs"),
                  QString::number(largeCollectNs / 1000000.0, 'f', 3)},
                 {QStringLiteral("imeAnalyzeMs"),
                  QString::number(imeAnalyzeNs / 1000000.0, 'f', 3)},
                 {QStringLiteral("imeCollectMs"),
                  QString::number(imeCollectNs / 1000000.0, 'f', 3)},
                 {QStringLiteral("blockSpans"), largeAnalysis.blockSpans.size()},
                 {QStringLiteral("inlineSpans"), largeAnalysis.inlineSpans.size()},
                 {QStringLiteral("smallInsertions"), smallInsertions.size()},
                 {QStringLiteral("largeInsertions"), largeInsertions.size()},
                 {QStringLiteral("imeInsertions"), imeInsertions.size()},
             });

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
