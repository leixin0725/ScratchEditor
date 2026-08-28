#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStringList>
#include <QThread>

#include "cjktextprocessor.h"

#include <array>
#include <cmath>
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
    // 滚动位置断言按瞬时语义编写，这里先关闭动画保证确定性；
    // 动画本身的中间态与落定行为由 window-ui 套件在窗口可见时验证。
    const QJsonObject scrollAnimationsOff = request(
        QStringLiteral("testApplyAppearance"),
        {{QStringLiteral("theme"), initial.value(QStringLiteral("theme")).toString()},
         {QStringLiteral("fontFamily"),
          initial.value(QStringLiteral("editorFontFamily")).toString()},
         {QStringLiteral("fallbackFontFamily"),
          initial.value(QStringLiteral("editorFallbackFontFamily")).toString()},
         {QStringLiteral("fontPointSize"),
          initial.value(QStringLiteral("editorFontPointSize")).toInt()},
         {QStringLiteral("fontWeight"),
          initial.value(QStringLiteral("editorFontWeight")).toInt()},
         {QStringLiteral("animationsEnabled"), false}});
    addCheck(checks, details, QStringLiteral("editingScrollAnimationsDisabled"),
             scrollAnimationsOff.value(QStringLiteral("applied")).toBool()
                 && !scrollAnimationsOff.value(QStringLiteral("animationsEnabled")).toBool()
                 && scrollAnimationsOff.value(QStringLiteral("transitionDuration")).toInt() == 0,
             scrollAnimationsOff);

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

    const QString emptyFirstLineText = QStringLiteral("\nbody");
    setTextAndSelection(emptyFirstLineText, 0, 0);
    const QJsonObject emptyFirstLineDirectHeading = execute(QStringLiteral("setHeading3"));
    const QJsonObject emptyFirstLineDirectHeadingUndo = request(QStringLiteral("testUndo"));
    setTextAndSelection(emptyFirstLineText, 0, 0);
    const QJsonObject emptyFirstLineCycledHeading = execute(QStringLiteral("cycleHeading"));
    addCheck(checks, details, QStringLiteral("headingCommandsKeepFollowingLineAfterEmptyFirstLine"),
             emptyFirstLineDirectHeading.value(QStringLiteral("text")).toString()
                     == QStringLiteral("### \nbody")
                 && emptyFirstLineDirectHeading.value(
                        QStringLiteral("cursorPosition")).toInt() == 4
                 && emptyFirstLineDirectHeadingUndo.value(QStringLiteral("text")).toString()
                     == emptyFirstLineText
                 && emptyFirstLineDirectHeadingUndo.value(
                        QStringLiteral("cursorPosition")).toInt() == 0
                 && emptyFirstLineCycledHeading.value(QStringLiteral("text")).toString()
                     == QStringLiteral("# \nbody")
                 && emptyFirstLineCycledHeading.value(
                        QStringLiteral("cursorPosition")).toInt() == 2,
             QJsonObject{{QStringLiteral("direct"), emptyFirstLineDirectHeading},
                         {QStringLiteral("undo"), emptyFirstLineDirectHeadingUndo},
                         {QStringLiteral("cycle"), emptyFirstLineCycledHeading}});

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

    // --- Tab 跳出配对的 CJK 全角转换（TAB-CJK-001..008） ---
    const auto tabAction = [] { return keyPress({}, QStringLiteral("Tab")); };
    cjkExpect(checks, details, QStringLiteral("tabCjkParenPair"),
              QStringLiteral("x(中文)"), 4, 4, QStringLiteral("Tab"),
              tabAction, QStringLiteral("x（中文）"), 5);
    cjkExpect(checks, details, QStringLiteral("tabCjkBracketPair"),
              QStringLiteral("x[中文]"), 4, 4, QStringLiteral("Tab"),
              tabAction, QStringLiteral("x【中文】"), 5);
    cjkExpect(checks, details, QStringLiteral("tabCjkDoubleQuotePair"),
              QStringLiteral("x\"中文\""), 4, 4, QStringLiteral("Tab"),
              tabAction, QStringLiteral("x“中文”"), 5);
    cjkExpect(checks, details, QStringLiteral("tabCjkSingleQuotePair"),
              QStringLiteral("x'中文'"), 4, 4, QStringLiteral("Tab"),
              tabAction, QStringLiteral("x‘中文’"), 5);
    cjkExpect(checks, details, QStringLiteral("tabCjkNestedOuterPair"),
              QStringLiteral("x([中文])"), 6, 6, QStringLiteral("Tab"),
              tabAction, QStringLiteral("x（[中文]）"), 7);
    cjkExpect(checks, details, QStringLiteral("tabCjkAsciiContentNegative"),
              QStringLiteral("x(abc)"), 5, 5, QStringLiteral("Tab"),
              tabAction, QStringLiteral("x(abc)"), 6);
    cjkExpect(checks, details, QStringLiteral("tabCjkFullwidthPairUnchanged"),
              QStringLiteral("x（中文）"), 4, 4, QStringLiteral("Tab"),
              tabAction, QStringLiteral("x（中文）"), 5);
    cjkExpect(checks, details, QStringLiteral("tabCjkProtectedInlineCode"),
              QStringLiteral("`x(中文)`"), 5, 5, QStringLiteral("Tab"),
              tabAction, QStringLiteral("`x(中文)`"), 6);
    {
        // 转换与跳转合并为一次撤销（UNDO-GROUP-…）。
        setTextAndSelection(QStringLiteral("x(中文)"), 4, 4);
        keyPress({}, QStringLiteral("Tab"));
        QThread::msleep(30);
        const QString converted = editorText();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString undone = editorText();
        addCheck(checks, details, QStringLiteral("tabCjkConversionSingleUndo"),
                 converted == QStringLiteral("x（中文）")
                     && undone == QStringLiteral("x(中文)"),
                 QJsonObject{{QStringLiteral("converted"), converted},
                             {QStringLiteral("undone"), undone}});
    }

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
    cjkExpect(checks, details, QStringLiteral("lineEndAsciiQuoteClosesUnclosed"),
              QStringLiteral("a\"bc"), 4, 4,
              QStringLiteral("key \" closes unclosed quote at line end"),
              keyAction(QStringLiteral("\"")), QStringLiteral("a\"bc\""), 5);
    cjkExpect(checks, details, QStringLiteral("lineEndCjkQuoteClosesBeforePairing"),
              QStringLiteral("中文“内容"), 5, 5,
              QStringLiteral("key \" closes CJK quote at line end"),
              keyAction(QStringLiteral("\"")), QStringLiteral("中文 “内容”"), 7);
    cjkExpect(checks, details, QStringLiteral("lineEndCurlyOpeningKeyClosesUnclosed"),
              QStringLiteral("a“bc"), 4, 4,
              QStringLiteral("key “ closes unclosed curly quote at line end"),
              keyAction(QStringLiteral("“")), QStringLiteral("a “bc”"), 6);
    cjkExpect(checks, details, QStringLiteral("lineEndFullwidthQuoteClosesUnclosed"),
              QStringLiteral("a＂bc"), 4, 4,
              QStringLiteral("key ＂ closes unclosed fullwidth quote at line end"),
              keyAction(QStringLiteral("＂")), QStringLiteral("a＂bc＂"), 5);
    cjkExpect(checks, details, QStringLiteral("lineEndBacktickClosesUnclosed"),
              QStringLiteral("a`code"), 6, 6,
              QStringLiteral("key ` closes unclosed code span at line end"),
              keyAction(QStringLiteral("`")), QStringLiteral("a `code`"), 7);
    cjkExpect(checks, details, QStringLiteral("lineEndImeQuoteClosesUnclosed"),
              QStringLiteral("a\"中文"), 4, 4,
              QStringLiteral("IME commit \" closes quote at line end"),
              [] { return inputMethodCommit(QStringLiteral("\"")); },
              QStringLiteral("a “中文”"), 6);
    cjkExpect(checks, details, QStringLiteral("lineEndEscapedQuoteDoesNotClose"),
              QStringLiteral("abc\\\"def"), 8, 8,
              QStringLiteral("escaped quote is not an opener"),
              keyAction(QStringLiteral("\"")), QStringLiteral("abc\\\"def\"\""), 9);
    cjkExpect(checks, details, QStringLiteral("lineEndQuoteDoesNotCrossLine"),
              QStringLiteral("\"first\nsecond"), 13, 13,
              QStringLiteral("quote opener on previous line is ignored"),
              keyAction(QStringLiteral("\"")), QStringLiteral("\"first\nsecond\"\""), 14);
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
              QStringLiteral("a “中文” x"), 6);
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
    const QJsonObject beforeLoadHistory = request(
        QStringLiteral("testClipboardHistoryState"));
    const QJsonArray beforeLoadItems = beforeLoadHistory.value(
        QStringLiteral("items")).toArray();
    QString originalId;
    for (const QJsonValue &value : beforeLoadItems) {
        if (value.toObject().value(QStringLiteral("text")).toString()
            == QStringLiteral("immutable-original")) {
            originalId = value.toObject().value(QStringLiteral("id")).toString();
            break;
        }
    }
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

    // --- 翻页浏览与输入自动滚动 ---
    QString scrollText;
    scrollText.reserve(4000);
    for (int i = 0; i < 80; ++i) {
        scrollText += QStringLiteral("line-%1 abcdefghij klmnopqrstuvwxyz\n")
                          .arg(i, 2, 10, QLatin1Char('0'));
    }
    const int scrollMidCursor = scrollText.indexOf(QStringLiteral("line-70"));
    const int scrollEndCursor = scrollText.size();

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 10}, {QStringLiteral("end"), 20}});
    setScrollY(0);
    QThread::msleep(50);
    const QJsonObject scrollTop = editorStatus();
    const double pageViewportHeight =
        scrollTop.value(QStringLiteral("scrollViewportHeight")).toDouble();
    const double pageMaxY =
        scrollTop.value(QStringLiteral("scrollContentHeight")).toDouble()
        - pageViewportHeight;
    keyPress({}, QStringLiteral("PageDown"));
    QThread::msleep(40);
    const QJsonObject pageDownStatus = editorStatus();
    keyPress({}, QStringLiteral("PageUp"));
    QThread::msleep(40);
    const QJsonObject pageUpStatus = editorStatus();
    addCheck(checks, details, QStringLiteral("pageScrollPageDownUpKeepsCursorSelection"),
             std::abs(pageDownStatus.value(QStringLiteral("scrollContentY")).toDouble()
                      - qMin(pageViewportHeight, pageMaxY)) < 1.5
                 && pageDownStatus.value(QStringLiteral("cursorPosition")).toInt() == 20
                 && pageDownStatus.value(QStringLiteral("selectionStart")).toInt() == 10
                 && pageDownStatus.value(QStringLiteral("selectionEnd")).toInt() == 20
                 && pageDownStatus.value(QStringLiteral("scrollContentY")).toDouble()
                    > scrollTop.value(QStringLiteral("scrollContentY")).toDouble()
                 && std::abs(pageUpStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - scrollTop.value(QStringLiteral("scrollContentY")).toDouble()) < 1.5,
             QJsonObject{{QStringLiteral("top"), scrollTop},
                         {QStringLiteral("pageDown"), pageDownStatus},
                         {QStringLiteral("pageUp"), pageUpStatus}});

    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}});
    setScrollY(pageMaxY);
    QThread::msleep(20);
    keyPress({}, QStringLiteral("PageDown"));
    QThread::msleep(40);
    const QJsonObject clampedBottom = editorStatus();
    setScrollY(0);
    QThread::msleep(20);
    keyPress({}, QStringLiteral("PageUp"));
    QThread::msleep(40);
    const QJsonObject clampedTop = editorStatus();
    addCheck(checks, details, QStringLiteral("pageScrollClampsAtEnds"),
             std::abs(clampedBottom.value(QStringLiteral("scrollContentY")).toDouble()
                      - pageMaxY) < 1.5
                 && std::abs(clampedTop.value(QStringLiteral("scrollContentY")).toDouble())
                        < 1.5,
             QJsonObject{{QStringLiteral("maxY"), pageMaxY},
                         {QStringLiteral("bottom"), clampedBottom},
                         {QStringLiteral("top"), clampedTop}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    QThread::msleep(50);
    const QJsonObject paddingStatus = editorStatus();
    const double paddingContentHeight =
        paddingStatus.value(QStringLiteral("scrollContentHeight")).toDouble();
    const double paddingTextHeight =
        paddingStatus.value(QStringLiteral("editorContentHeight")).toDouble();
    const double paddingViewportHeight =
        paddingStatus.value(QStringLiteral("scrollViewportHeight")).toDouble();
    addCheck(checks, details, QStringLiteral("scrollPastEndReservesTwoThirdsPage"),
             std::abs(paddingContentHeight
                      - (paddingTextHeight + paddingViewportHeight * 2.0 / 3.0)) < 1.5
                 && paddingContentHeight > paddingViewportHeight,
             paddingStatus);

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), QStringLiteral("short")}});
    // 滚动条可见性由 60ms 防抖定时器刷新，需等待其完成后读取。
    QThread::msleep(150);
    const QJsonObject shortDocStatus = editorStatus();
    keyPress({}, QStringLiteral("PageDown"));
    QThread::msleep(40);
    const QJsonObject shortDocPageDown = editorStatus();
    addCheck(checks, details, QStringLiteral("shortDocumentHasNoScrollRange"),
             std::abs(shortDocStatus.value(QStringLiteral("scrollContentHeight")).toDouble()
                      - shortDocStatus.value(QStringLiteral("scrollViewportHeight")).toDouble())
                     < 1.5
                 && !shortDocStatus.value(QStringLiteral("verticalScrollBarVisible")).toBool()
                 && std::abs(shortDocPageDown.value(QStringLiteral("scrollContentY")).toDouble())
                        < 1.5
                 && shortDocPageDown.value(QStringLiteral("cursorPosition")).toInt() == 5,
             QJsonObject{{QStringLiteral("before"), shortDocStatus},
                         {QStringLiteral("pageDown"), shortDocPageDown}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollEndCursor},
             {QStringLiteral("end"), scrollEndCursor}});
    setScrollY(0);
    QThread::msleep(50);
    const QJsonObject beforeEndInput = editorStatus();
    const double endMaxY =
        beforeEndInput.value(QStringLiteral("scrollContentHeight")).toDouble()
        - beforeEndInput.value(QStringLiteral("scrollViewportHeight")).toDouble();
    keyPress(QStringLiteral("x"));
    QThread::msleep(50);
    const QJsonObject afterEndInput = editorStatus();
    addCheck(checks, details, QStringLiteral("inputAtDocumentEndScrollsToBottom"),
             std::abs(afterEndInput.value(QStringLiteral("scrollContentY")).toDouble()
                      - endMaxY) < 1.5
                 && afterEndInput.value(QStringLiteral("cursorPosition")).toInt()
                    == scrollEndCursor + 1
                 && editorText() == scrollText + QStringLiteral("x"),
             QJsonObject{{QStringLiteral("maxY"), endMaxY},
                         {QStringLiteral("after"), afterEndInput}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollMidCursor},
             {QStringLiteral("end"), scrollMidCursor}});
    setScrollY(0);
    QThread::msleep(50);
    keyPress(QStringLiteral("x"));
    QThread::msleep(50);
    const QJsonObject midInputStatus = editorStatus();
    const double midExpectedY =
        midInputStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + midInputStatus.value(QStringLiteral("cursorRectY")).toDouble()
        - midInputStatus.value(QStringLiteral("scrollViewportHeight")).toDouble() / 3.0;
    const double midMaxY =
        midInputStatus.value(QStringLiteral("scrollContentHeight")).toDouble()
        - midInputStatus.value(QStringLiteral("scrollViewportHeight")).toDouble();
    addCheck(checks, details, QStringLiteral("inputMidDocumentAnchorsToUpperThird"),
             std::abs(midInputStatus.value(QStringLiteral("scrollContentY")).toDouble()
                      - midExpectedY) < 3.0
                 && midInputStatus.value(QStringLiteral("scrollContentY")).toDouble() > 0.0
                 && midInputStatus.value(QStringLiteral("scrollContentY")).toDouble() < midMaxY,
             QJsonObject{{QStringLiteral("expectedY"), midExpectedY},
                         {QStringLiteral("maxY"), midMaxY},
                         {QStringLiteral("status"), midInputStatus}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}});
    setScrollY(0);
    QThread::msleep(50);
    keyPress(QStringLiteral("x"));
    QThread::msleep(50);
    const QJsonObject topInputStatus = editorStatus();
    addCheck(checks, details, QStringLiteral("inputVisibleAboveBottomDoesNotScroll"),
             std::abs(topInputStatus.value(QStringLiteral("scrollContentY")).toDouble()) < 1.5
                 && editorText().startsWith(QStringLiteral("x")),
             topInputStatus);

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollMidCursor},
             {QStringLiteral("end"), scrollMidCursor}});
    setScrollY(0);
    QThread::msleep(50);
    keyPress({}, QStringLiteral("Down"));
    QThread::msleep(50);
    const QJsonObject arrowMoveStatus = editorStatus();
    const double arrowMoveY =
        arrowMoveStatus.value(QStringLiteral("scrollContentY")).toDouble();
    keyPress(QStringLiteral("x"));
    QThread::msleep(50);
    const QJsonObject afterArrowTypeStatus = editorStatus();
    const double arrowTypeY =
        afterArrowTypeStatus.value(QStringLiteral("scrollContentY")).toDouble();
    const double arrowTypeMaxY =
        afterArrowTypeStatus.value(QStringLiteral("scrollContentHeight")).toDouble()
        - afterArrowTypeStatus.value(QStringLiteral("scrollViewportHeight")).toDouble();
    addCheck(checks, details, QStringLiteral("arrowMoveUsesMinimalFollowNotAnchor"),
             arrowMoveY > 0.0 && arrowMoveY < arrowTypeY && arrowTypeY <= arrowTypeMaxY,
             QJsonObject{{QStringLiteral("arrowY"), arrowMoveY},
                         {QStringLiteral("typedY"), arrowTypeY},
                         {QStringLiteral("maxY"), arrowTypeMaxY},
                         {QStringLiteral("arrowStatus"), arrowMoveStatus}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollEndCursor},
             {QStringLiteral("end"), scrollEndCursor}});
    setScrollY(0);
    QThread::msleep(50);
    keyPress(QStringLiteral("x"));
    QThread::msleep(50);
    const QJsonObject beforeUndoInput = editorStatus();
    request(QStringLiteral("testUndo"));
    QThread::msleep(80);
    const QJsonObject afterUndoInput = editorStatus();
    const double undoInputMaxY =
        afterUndoInput.value(QStringLiteral("scrollContentHeight")).toDouble()
        - afterUndoInput.value(QStringLiteral("scrollViewportHeight")).toDouble();
    // 撤销不再回滚滚动位置：视图停在当前底部，只恢复文本。
    addCheck(checks, details, QStringLiteral("undoAfterEndInputKeepsViewAtBottom"),
             afterUndoInput.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor
                 && std::abs(afterUndoInput.value(QStringLiteral("scrollContentY")).toDouble()
                             - undoInputMaxY) < 1.5
                 && afterUndoInput.value(QStringLiteral("scrollContentY")).toDouble() > 0.0
                 && editorText() == scrollText,
             QJsonObject{{QStringLiteral("before"), beforeUndoInput},
                         {QStringLiteral("after"), afterUndoInput},
                         {QStringLiteral("maxY"), undoInputMaxY}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollEndCursor},
             {QStringLiteral("end"), scrollEndCursor}});
    setScrollY(0);
    QThread::msleep(50);
    keyPress(QStringLiteral("x"));
    QThread::msleep(50);
    const QJsonObject afterFirstEndInput = editorStatus();
    const double firstEndMaxY =
        afterFirstEndInput.value(QStringLiteral("scrollContentHeight")).toDouble()
        - afterFirstEndInput.value(QStringLiteral("scrollViewportHeight")).toDouble();
    // 段尾首次触底触发一次滚到底（max）；随后同一行继续输入不再触发。
    keyPress(QStringLiteral("y"));
    QThread::msleep(50);
    const QJsonObject afterSecondEndInput = editorStatus();
    // 回车新增一行后光标仍远离底边，同样不触发（间歇式，而非钉在 1/3）。
    keyPress({}, QStringLiteral("Enter"));
    QThread::msleep(50);
    const QJsonObject afterEndEnter = editorStatus();
    addCheck(checks, details, QStringLiteral("endInputScrollIsIntermittent"),
             std::abs(afterFirstEndInput.value(QStringLiteral("scrollContentY")).toDouble()
                      - firstEndMaxY) < 1.5
                 && std::abs(afterSecondEndInput.value(QStringLiteral("scrollContentY")).toDouble()
                             - firstEndMaxY) < 1.5
                 && std::abs(afterEndEnter.value(QStringLiteral("scrollContentY")).toDouble()
                             - firstEndMaxY) < 1.5
                 && afterSecondEndInput.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor + 2,
             QJsonObject{{QStringLiteral("maxY"), firstEndMaxY},
                         {QStringLiteral("afterFirst"), afterFirstEndInput},
                         {QStringLiteral("afterSecond"), afterSecondEndInput},
                         {QStringLiteral("afterEnter"), afterEndEnter}});

    // 继续连续回车，光标从视口上 1/3 自然下落，再次触底时才再次触发。
    int endEnterCount = 0;
    for (; endEnterCount < 39; ++endEnterCount) {
        keyPress({}, QStringLiteral("Enter"));
        // 与真实输入节奏一致（大于自动滚动检查的 40ms 延迟），
        // 保证每笔输入各有一条滚动记录，撤销链逐级对齐。
        QThread::msleep(50);
    }
    const QJsonObject afterManyEnters = editorStatus();
    const double chainY0 =
        afterManyEnters.value(QStringLiteral("scrollContentY")).toDouble();
    const double chainViewportHeight =
        afterManyEnters.value(QStringLiteral("scrollViewportHeight")).toDouble();
    const double chainBaseScreenY =
        afterManyEnters.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + afterManyEnters.value(QStringLiteral("cursorRectY")).toDouble()
        - chainY0;
    // 撤销链保持 + 顶镜像：连续撤销末尾删除时视图不被钳回新 max，
    // 光标逐行上移；越过顶边后由顶规则间歇触发（光标行锚定到视口距顶 2/3）。
    int chainUndoCount = 0;
    int chainTopTriggerCount = 0;
    bool chainHeldUntilTrigger = true;
    double chainFirstUndoY = -1.0;
    double chainFirstUndoScreenY = -1.0;
    QJsonObject chainTriggerSample;
    while (chainUndoCount < 50 && editorText() != scrollText) {
        request(QStringLiteral("testUndo"));
        // 每笔撤销后等待超过自动滚动检查的 40ms 延迟再采样，逐级观察保持与触发。
        QThread::msleep(55);
        const QJsonObject chainStep = editorStatus();
        const double stepScreenY =
            chainStep.value(QStringLiteral("editorContentOffsetY")).toDouble()
            + chainStep.value(QStringLiteral("cursorRectY")).toDouble()
            - chainStep.value(QStringLiteral("scrollContentY")).toDouble();
        const bool stepTriggered =
            std::abs(stepScreenY - chainViewportHeight * 2.0 / 3.0) < 3.0;
        if (chainUndoCount == 0) {
            chainFirstUndoY =
                chainStep.value(QStringLiteral("scrollContentY")).toDouble();
            chainFirstUndoScreenY = stepScreenY;
        }
        if (stepTriggered) {
            ++chainTopTriggerCount;
            chainTriggerSample = chainStep;
        } else if (chainTopTriggerCount == 0
                   && std::abs(chainStep.value(QStringLiteral("scrollContentY")).toDouble()
                               - chainY0) >= 1.5) {
            chainHeldUntilTrigger = false;
        }
        ++chainUndoCount;
    }
    QThread::msleep(60);
    const QJsonObject afterFullEndUndo = editorStatus();
    // 撤销链早期逐笔保持输入前视口位置（contentY ≈ Y0、光标屏上位置下降），
    // 越过顶边后顶触发发生；文本最终完整还原。
    addCheck(checks, details, QStringLiteral("endInputScrollReTriggersAndUndoChainKeepsView"),
             afterManyEnters.value(QStringLiteral("scrollContentY")).toDouble()
                    > firstEndMaxY
                 && chainUndoCount == 42
                 && std::abs(chainFirstUndoY - chainY0) < 1.5
                 && chainFirstUndoScreenY < chainBaseScreenY - 10.0
                 && chainHeldUntilTrigger
                 && chainTopTriggerCount >= 2
                 && afterFullEndUndo.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor
                 && afterFullEndUndo.value(QStringLiteral("scrollContentY")).toDouble() > 0.0
                 && editorText() == scrollText,
             QJsonObject{{QStringLiteral("maxY"), firstEndMaxY},
                         {QStringLiteral("afterEnters"), afterManyEnters},
                         {QStringLiteral("chainY0"), chainY0},
                         {QStringLiteral("baseScreenY"), chainBaseScreenY},
                         {QStringLiteral("firstUndoY"), chainFirstUndoY},
                         {QStringLiteral("firstUndoScreenY"), chainFirstUndoScreenY},
                         {QStringLiteral("topTriggerCount"), chainTopTriggerCount},
                         {QStringLiteral("triggerSample"), chainTriggerSample},
                         {QStringLiteral("afterFullUndo"), afterFullEndUndo}});

    // 两级撤销删除：第一笔撤销（回车新增行）收缩内容时视图保持输入前位置，
    // 光标随删除自然上移；第二笔撤销（删除字符、行高不变）视图继续稳定，
    // 不会被钳回新的底部。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollEndCursor},
             {QStringLiteral("end"), scrollEndCursor}});
    setScrollY(0);
    QThread::msleep(50);
    keyPress(QStringLiteral("x"));
    QThread::msleep(50);
    const QJsonObject firstScrolledInput = editorStatus();
    const double bottomEdgeY =
        firstScrolledInput.value(QStringLiteral("editorContentHeight")).toDouble()
        - firstScrolledInput.value(QStringLiteral("scrollViewportHeight")).toDouble();
    setScrollY(bottomEdgeY);
    QThread::msleep(20);
    keyPress({}, QStringLiteral("Enter"));
    QThread::msleep(50);
    const QJsonObject secondScrolledInput = editorStatus();
    const double secondScrolledScreenY =
        secondScrolledInput.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + secondScrolledInput.value(QStringLiteral("cursorRectY")).toDouble()
        - secondScrolledInput.value(QStringLiteral("scrollContentY")).toDouble();
    request(QStringLiteral("testUndo"));
    QThread::msleep(80);
    const QJsonObject firstUndo = editorStatus();
    const double firstUndoScreenY =
        firstUndo.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + firstUndo.value(QStringLiteral("cursorRectY")).toDouble()
        - firstUndo.value(QStringLiteral("scrollContentY")).toDouble();
    request(QStringLiteral("testUndo"));
    QThread::msleep(80);
    const QJsonObject secondUndo = editorStatus();
    addCheck(checks, details, QStringLiteral("undoKeepsViewWhileTextRestores"),
             secondScrolledInput.value(QStringLiteral("scrollContentY")).toDouble()
                    > bottomEdgeY
                 && firstUndo.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor + 1
                 && std::abs(firstUndo.value(QStringLiteral("scrollContentY")).toDouble()
                             - secondScrolledInput.value(
                                 QStringLiteral("scrollContentY")).toDouble()) < 1.5
                 && firstUndoScreenY < secondScrolledScreenY - 10.0
                 && secondUndo.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor
                 && std::abs(secondUndo.value(QStringLiteral("scrollContentY")).toDouble()
                             - secondScrolledInput.value(
                                 QStringLiteral("scrollContentY")).toDouble()) < 1.5
                 && secondUndo.value(QStringLiteral("scrollContentY")).toDouble()
                    > secondUndo.value(QStringLiteral("editorContentHeight")).toDouble()
                        - secondUndo.value(QStringLiteral("scrollViewportHeight")).toDouble()
                        + 1.5
                 && editorText() == scrollText,
             QJsonObject{{QStringLiteral("bottomEdgeY"), bottomEdgeY},
                         {QStringLiteral("first"), firstScrolledInput},
                         {QStringLiteral("second"), secondScrolledInput},
                         {QStringLiteral("secondScreenY"), secondScrolledScreenY},
                         {QStringLiteral("firstUndo"), firstUndo},
                         {QStringLiteral("firstUndoScreenY"), firstUndoScreenY},
                         {QStringLiteral("secondUndo"), secondUndo},
                         {QStringLiteral("naturalMaxY"),
                          secondUndo.value(QStringLiteral("editorContentHeight")).toDouble()
                              - secondUndo.value(
                                  QStringLiteral("scrollViewportHeight")).toDouble()}});

    // 重做删除与撤销删除共用同一套保持：构造可重做的末尾删行
    // （回车新增空行 → 退格删行 → 撤销恢复 → 重做再删），
    // 重做删除同样保持视图、光标自然上移，而不是被钳回新的底部。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollEndCursor},
             {QStringLiteral("end"), scrollEndCursor}});
    setScrollY(0);
    QThread::msleep(50);
    keyPress(QStringLiteral("x"));
    QThread::msleep(50);
    const QJsonObject redoBaseScrolled = editorStatus();
    keyPress({}, QStringLiteral("Enter"));
    QThread::msleep(50);
    const QJsonObject redoEntered = editorStatus();
    // 把视口落到回车后带留白的新底边，让后续删行时输入前位置高于新 max，
    // 真正构造出“内容收缩 + 视图位于底部”的保持场景。
    setScrollY(redoEntered.value(QStringLiteral("scrollContentHeight")).toDouble()
               - redoEntered.value(QStringLiteral("scrollViewportHeight")).toDouble());
    QThread::msleep(30);
    const QJsonObject redoAtMax = editorStatus();
    const double redoEnteredScreenY =
        redoAtMax.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + redoAtMax.value(QStringLiteral("cursorRectY")).toDouble()
        - redoAtMax.value(QStringLiteral("scrollContentY")).toDouble();
    keyPress({}, QStringLiteral("Backspace"));
    QThread::msleep(80);
    const QJsonObject redoBackspace = editorStatus();
    const double redoBackspaceScreenY =
        redoBackspace.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + redoBackspace.value(QStringLiteral("cursorRectY")).toDouble()
        - redoBackspace.value(QStringLiteral("scrollContentY")).toDouble();
    request(QStringLiteral("testUndo"));
    QThread::msleep(80);
    const QJsonObject redoRestored = editorStatus();
    const double redoRestoredScreenY =
        redoRestored.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + redoRestored.value(QStringLiteral("cursorRectY")).toDouble()
        - redoRestored.value(QStringLiteral("scrollContentY")).toDouble();
    request(QStringLiteral("testRedo"));
    QThread::msleep(80);
    const QJsonObject redoDeleted = editorStatus();
    const double redoDeletedScreenY =
        redoDeleted.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + redoDeleted.value(QStringLiteral("cursorRectY")).toDouble()
        - redoDeleted.value(QStringLiteral("scrollContentY")).toDouble();
    const double redoDeletedMaxY =
        redoDeleted.value(QStringLiteral("editorContentHeight")).toDouble()
        - redoDeleted.value(QStringLiteral("scrollViewportHeight")).toDouble();
    addCheck(checks, details, QStringLiteral("redoDeleteHoldsViewWhileCursorAscends"),
             std::abs(redoAtMax.value(QStringLiteral("scrollContentY")).toDouble()
                     - (redoEntered.value(QStringLiteral("scrollContentHeight")).toDouble()
                         - redoEntered.value(QStringLiteral("scrollViewportHeight")).toDouble()))
                    < 1.5
                 && std::abs(redoBackspace.value(QStringLiteral("scrollContentY")).toDouble()
                             - redoAtMax.value(QStringLiteral("scrollContentY")).toDouble())
                        < 1.5
                 && redoBackspaceScreenY < redoEnteredScreenY - 10.0
                 && std::abs(redoDeleted.value(QStringLiteral("scrollContentY")).toDouble()
                             - redoRestored.value(QStringLiteral("scrollContentY")).toDouble())
                        < 1.5
                 && redoDeletedScreenY < redoRestoredScreenY - 10.0
                 && redoDeleted.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor + 1
                 && redoDeleted.value(QStringLiteral("scrollContentY")).toDouble()
                    > redoDeletedMaxY + 1.5
                 && editorText() == scrollText + QStringLiteral("x"),
             QJsonObject{{QStringLiteral("base"), redoBaseScrolled},
                         {QStringLiteral("entered"), redoEntered},
                         {QStringLiteral("atMax"), redoAtMax},
                         {QStringLiteral("enteredScreenY"), redoEnteredScreenY},
                         {QStringLiteral("backspace"), redoBackspace},
                         {QStringLiteral("backspaceScreenY"), redoBackspaceScreenY},
                         {QStringLiteral("restored"), redoRestored},
                         {QStringLiteral("restoredScreenY"), redoRestoredScreenY},
                         {QStringLiteral("deleted"), redoDeleted},
                         {QStringLiteral("deletedScreenY"), redoDeletedScreenY},
                         {QStringLiteral("deletedMaxY"), redoDeletedMaxY}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}});
    setScrollY(0);
    QThread::msleep(50);
    keyPress(QStringLiteral("y"));
    QThread::msleep(50);
    const QJsonObject topTypedStatus = editorStatus();
    request(QStringLiteral("testUndo"));
    QThread::msleep(80);
    const QJsonObject topUndoStatus = editorStatus();
    // 光标可见时撤销不触发任何滚动（既无回滚也无顶边锚定）。
    addCheck(checks, details, QStringLiteral("undoVisibleCursorKeepsView"),
             std::abs(topTypedStatus.value(QStringLiteral("scrollContentY")).toDouble()) < 1.5
                 && std::abs(topUndoStatus.value(QStringLiteral("scrollContentY")).toDouble())
                        < 1.5
                 && editorText() == scrollText,
             QJsonObject{{QStringLiteral("typed"), topTypedStatus},
                         {QStringLiteral("undo"), topUndoStatus}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollEndCursor},
             {QStringLiteral("end"), scrollEndCursor}});
    setScrollY(0);
    QThread::msleep(50);
    inputMethodCommit(QStringLiteral("中文"));
    QThread::msleep(80);
    const QJsonObject imeScrollStatus = editorStatus();
    const QString imeCommittedText = editorText();
    const double imeMaxY =
        imeScrollStatus.value(QStringLiteral("scrollContentHeight")).toDouble()
        - imeScrollStatus.value(QStringLiteral("scrollViewportHeight")).toDouble();
    request(QStringLiteral("testUndo"));
    QThread::msleep(80);
    const QJsonObject imeUndoStatus = editorStatus();
    const double imeUndoMaxY =
        imeUndoStatus.value(QStringLiteral("scrollContentHeight")).toDouble()
        - imeUndoStatus.value(QStringLiteral("scrollViewportHeight")).toDouble();
    // IME 提交触底照常滚到底；撤销只恢复文本，视图停在底部不回跳。
    addCheck(checks, details, QStringLiteral("imeCommitAtEndScrollsAndUndoKeepsView"),
             std::abs(imeScrollStatus.value(QStringLiteral("scrollContentY")).toDouble()
                      - imeMaxY) < 1.5
                 && imeCommittedText == scrollText + QStringLiteral("中文")
                 && std::abs(imeUndoStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - imeUndoMaxY) < 1.5
                 && imeUndoStatus.value(QStringLiteral("scrollContentY")).toDouble() > 0.0
                 && editorText() == scrollText,
             QJsonObject{{QStringLiteral("maxY"), imeMaxY},
                         {QStringLiteral("committed"), imeScrollStatus},
                         {QStringLiteral("undo"), imeUndoStatus},
                         {QStringLiteral("undoMaxY"), imeUndoMaxY}});

    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollEndCursor},
             {QStringLiteral("end"), scrollEndCursor}});
    setScrollY(endMaxY);
    QThread::msleep(50);
    const QJsonObject beforePagedInput = editorStatus();
    const double pagedMaxY =
        beforePagedInput.value(QStringLiteral("scrollContentHeight")).toDouble()
        - beforePagedInput.value(QStringLiteral("scrollViewportHeight")).toDouble();
    keyPress(QStringLiteral("x"));
    QThread::msleep(50);
    const QJsonObject pagedIntoBlankInput = editorStatus();
    addCheck(checks, details, QStringLiteral("inputAtEndAfterPagingIntoBlankStaysBottom"),
             std::abs(pagedIntoBlankInput.value(QStringLiteral("scrollContentY")).toDouble()
                      - pagedMaxY) < 1.5
                 && editorText() == scrollText + QStringLiteral("x"),
             QJsonObject{{QStringLiteral("maxY"), pagedMaxY},
                         {QStringLiteral("after"), pagedIntoBlankInput}});

    // --- 删除触顶自动滚动（严格镜像）与撤销/重做统一检查 ---
    const QString scrollLine =
        QStringLiteral("line-40 abcdefghij klmnopqrstuvwxyz\n");
    const int scrollTopCursor = scrollText.indexOf(QStringLiteral("line-40"));
    const int scrollSelectionStart = scrollText.indexOf(QStringLiteral("line-40"));
    const int scrollSelectionEnd = scrollText.indexOf(QStringLiteral("line-50"));

    // 退格使光标位于视口顶边之上：先由 QML 最小跟随滚到顶边，
    // 再由统一检查把光标行锚定到视口距顶 2/3 处（下 1/3）。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollTopCursor},
             {QStringLiteral("end"), scrollTopCursor}});
    setScrollY(pageMaxY);
    QThread::msleep(30);
    keyPress({}, QStringLiteral("Backspace"));
    QThread::msleep(80);
    const QJsonObject deleteTopStatus = editorStatus();
    const double deleteTopExpectedY =
        deleteTopStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + deleteTopStatus.value(QStringLiteral("cursorRectY")).toDouble()
        - deleteTopStatus.value(QStringLiteral("scrollViewportHeight")).toDouble() * 2.0 / 3.0;
    const double deleteTopMaxY =
        deleteTopStatus.value(QStringLiteral("scrollContentHeight")).toDouble()
        - deleteTopStatus.value(QStringLiteral("scrollViewportHeight")).toDouble();
    addCheck(checks, details, QStringLiteral("deleteTouchTopAnchorsToLowerThird"),
             deleteTopStatus.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor - 1
                 && deleteTopStatus.value(QStringLiteral("cursorPosition")).toInt()
                    == scrollTopCursor - 1
                 && deleteTopExpectedY > 50.0
                 && std::abs(deleteTopStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - deleteTopExpectedY) < 3.0
                 && deleteTopStatus.value(QStringLiteral("scrollContentY")).toDouble()
                    < deleteTopMaxY,
             QJsonObject{{QStringLiteral("expectedY"), deleteTopExpectedY},
                         {QStringLiteral("maxY"), deleteTopMaxY},
                         {QStringLiteral("status"), deleteTopStatus}});

    // 间歇式：锚定后继续退格不重复触发；重新让光标越过顶边才再次触发。
    const double deleteAnchorY =
        deleteTopStatus.value(QStringLiteral("scrollContentY")).toDouble();
    keyPress({}, QStringLiteral("Backspace"));
    QThread::msleep(60);
    keyPress({}, QStringLiteral("Backspace"));
    QThread::msleep(60);
    const QJsonObject deleteNonTriggerStatus = editorStatus();
    setScrollY(pageMaxY);
    QThread::msleep(30);
    const QJsonObject deleteReCrossStatus = editorStatus();
    keyPress({}, QStringLiteral("Backspace"));
    QThread::msleep(80);
    const QJsonObject deleteReTriggerStatus = editorStatus();
    const double deleteReTriggerExpectedY =
        deleteReTriggerStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + deleteReTriggerStatus.value(QStringLiteral("cursorRectY")).toDouble()
        - deleteReTriggerStatus.value(QStringLiteral("scrollViewportHeight")).toDouble()
            * 2.0 / 3.0;
    addCheck(checks, details, QStringLiteral("deleteTopScrollIsIntermittent"),
             std::abs(deleteNonTriggerStatus.value(QStringLiteral("scrollContentY")).toDouble()
                      - deleteAnchorY) < 1.5
                 && deleteReCrossStatus.value(QStringLiteral("scrollContentY")).toDouble()
                    > deleteAnchorY + 100.0
                 && std::abs(deleteReTriggerStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - deleteReTriggerExpectedY) < 3.0,
             QJsonObject{{QStringLiteral("anchorY"), deleteAnchorY},
                         {QStringLiteral("nonTrigger"), deleteNonTriggerStatus},
                         {QStringLiteral("reCross"), deleteReCrossStatus},
                         {QStringLiteral("reTrigger"), deleteReTriggerStatus},
                         {QStringLiteral("expectedY"), deleteReTriggerExpectedY}});

    // 光标位于文档开头时退格不改变文本：统一检查 early return 不触发
    // 锚定，仅保留 QML 最小可见跟随（光标回到视口内）。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}});
    setScrollY(pageMaxY);
    QThread::msleep(30);
    const QJsonObject startBeforeDelete = editorStatus();
    keyPress({}, QStringLiteral("Backspace"));
    QThread::msleep(80);
    const QJsonObject startAfterDelete = editorStatus();
    addCheck(checks, details, QStringLiteral("deleteAtDocumentStartDoesNotScroll"),
             startAfterDelete.value(QStringLiteral("textLength")).toInt()
                    == startBeforeDelete.value(QStringLiteral("textLength")).toInt()
                 && std::abs(startAfterDelete.value(QStringLiteral("scrollContentY")).toDouble())
                        < 1.5,
             QJsonObject{{QStringLiteral("before"), startBeforeDelete},
                         {QStringLiteral("after"), startAfterDelete}});

    // 带选区删除：光标落在选区起点，越过视口顶边时触发同一锚定。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollSelectionStart},
             {QStringLiteral("end"), scrollSelectionEnd}});
    setScrollY(pageMaxY);
    QThread::msleep(30);
    keyPress({}, QStringLiteral("Delete"));
    QThread::msleep(80);
    const QJsonObject selectionDeleteStatus = editorStatus();
    const double selectionDeleteExpectedY =
        selectionDeleteStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + selectionDeleteStatus.value(QStringLiteral("cursorRectY")).toDouble()
        - selectionDeleteStatus.value(QStringLiteral("scrollViewportHeight")).toDouble()
            * 2.0 / 3.0;
    addCheck(checks, details, QStringLiteral("selectionDeleteAboveTopAnchors"),
             selectionDeleteStatus.value(QStringLiteral("cursorPosition")).toInt()
                    == scrollSelectionStart
                 && selectionDeleteStatus.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor - (scrollSelectionEnd - scrollSelectionStart)
                 && std::abs(selectionDeleteStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - selectionDeleteExpectedY) < 3.0,
             QJsonObject{{QStringLiteral("expectedY"), selectionDeleteExpectedY},
                         {QStringLiteral("status"), selectionDeleteStatus}});

    // 命令面板“删除整行”同样纳入触顶检查。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollTopCursor},
             {QStringLiteral("end"), scrollTopCursor}});
    setScrollY(pageMaxY);
    QThread::msleep(30);
    execute(QStringLiteral("deleteLine"));
    QThread::msleep(80);
    const QJsonObject deleteLineStatus = editorStatus();
    const double deleteLineExpectedY =
        deleteLineStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + deleteLineStatus.value(QStringLiteral("cursorRectY")).toDouble()
        - deleteLineStatus.value(QStringLiteral("scrollViewportHeight")).toDouble() * 2.0 / 3.0;
    addCheck(checks, details, QStringLiteral("deleteLineCommandScrolls"),
             deleteLineStatus.value(QStringLiteral("cursorPosition")).toInt()
                    == scrollTopCursor
                 && deleteLineStatus.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor - scrollLine.size()
                 && std::abs(deleteLineStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - deleteLineExpectedY) < 3.0,
             QJsonObject{{QStringLiteral("expectedY"), deleteLineExpectedY},
                         {QStringLiteral("status"), deleteLineStatus}});

    // 剪切（Ctrl+X 带选区）触发同一检查。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollSelectionStart},
             {QStringLiteral("end"), scrollSelectionEnd}});
    setClipboard(QString());
    setScrollY(pageMaxY);
    QThread::msleep(30);
    keyPress({}, QStringLiteral("X"), false, QStringLiteral("ctrl"));
    QThread::msleep(80);
    const QJsonObject cutStatus = editorStatus();
    const double cutExpectedY =
        cutStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + cutStatus.value(QStringLiteral("cursorRectY")).toDouble()
        - cutStatus.value(QStringLiteral("scrollViewportHeight")).toDouble() * 2.0 / 3.0;
    addCheck(checks, details, QStringLiteral("cutSelectionScrolls"),
             cutStatus.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor - (scrollSelectionEnd - scrollSelectionStart)
                 && std::abs(cutStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - cutExpectedY) < 3.0,
             QJsonObject{{QStringLiteral("expectedY"), cutExpectedY},
                         {QStringLiteral("status"), cutStatus}});

    // Ctrl+Backspace 词删除（TextEdit 原生处理）同样纳入检查。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollTopCursor + 20},
             {QStringLiteral("end"), scrollTopCursor + 20}});
    setScrollY(pageMaxY);
    QThread::msleep(30);
    const QJsonObject wordBeforeStatus = editorStatus();
    keyPress({}, QStringLiteral("Backspace"), false, QStringLiteral("ctrl"));
    QThread::msleep(80);
    const QJsonObject wordDeleteStatus = editorStatus();
    const double wordDeleteExpectedY =
        wordDeleteStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + wordDeleteStatus.value(QStringLiteral("cursorRectY")).toDouble()
        - wordDeleteStatus.value(QStringLiteral("scrollViewportHeight")).toDouble() * 2.0 / 3.0;
    addCheck(checks, details, QStringLiteral("wordDeleteScrolls"),
             wordDeleteStatus.value(QStringLiteral("textLength")).toInt()
                    < wordBeforeStatus.value(QStringLiteral("textLength")).toInt()
                 && std::abs(wordDeleteStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - wordDeleteExpectedY) < 3.0,
             QJsonObject{{QStringLiteral("before"), wordBeforeStatus},
                         {QStringLiteral("expectedY"), wordDeleteExpectedY},
                         {QStringLiteral("after"), wordDeleteStatus}});

    // 撤销/重做与删除/输入共用同一套检查：撤销大段粘贴后光标越过顶边
    // 按删除规则锚定；重做恢复粘贴后光标越过底边按输入规则锚定。
    QString scrollPasteBlock;
    scrollPasteBlock.reserve(1024);
    for (int i = 0; i < 20; ++i) {
        scrollPasteBlock += QStringLiteral("paste-line-%1 abcdefghij klmnopqrstuvwxyz\n")
                                .arg(i, 2, 10, QLatin1Char('0'));
    }
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollTopCursor},
             {QStringLiteral("end"), scrollTopCursor}});
    setClipboard(scrollPasteBlock);
    setScrollY(0);
    QThread::msleep(30);
    keyPress({}, QStringLiteral("V"), false, QStringLiteral("ctrl"));
    QThread::msleep(80);
    const QJsonObject pastedStatus = editorStatus();
    setScrollY(pageMaxY);
    QThread::msleep(30);
    request(QStringLiteral("testUndo"));
    QThread::msleep(80);
    const QJsonObject pasteUndoStatus = editorStatus();
    // 智能整行粘贴为一次撤销；Qt 撤销后光标落在何处（插入点或文档开头）
    // 不预设，按实际光标位置计算顶边锚定期望（文档开头时自然夹取到 0）。
    const double pasteUndoExpectedY = qMax(
        0.0, pasteUndoStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
                 + pasteUndoStatus.value(QStringLiteral("cursorRectY")).toDouble()
                 - pasteUndoStatus.value(QStringLiteral("scrollViewportHeight")).toDouble()
                     * 2.0 / 3.0);
    addCheck(checks, details, QStringLiteral("undoJumpsAboveTopAnchorsLikeDelete"),
             pasteUndoStatus.value(QStringLiteral("textLength")).toInt() == scrollEndCursor
                 && std::abs(pasteUndoStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - pasteUndoExpectedY) < 3.0
                 && editorText() == scrollText,
             QJsonObject{{QStringLiteral("pasted"), pastedStatus},
                         {QStringLiteral("expectedY"), pasteUndoExpectedY},
                         {QStringLiteral("undo"), pasteUndoStatus}});

    request(QStringLiteral("testRedo"));
    QThread::msleep(80);
    const QJsonObject pasteRedoStatus = editorStatus();
    const double pasteRedoMaxY =
        pasteRedoStatus.value(QStringLiteral("scrollContentHeight")).toDouble()
        - pasteRedoStatus.value(QStringLiteral("scrollViewportHeight")).toDouble();
    // 重做恢复粘贴后光标越过底边，按输入规则锚定到视口上 1/3。
    const double pasteRedoExpectedY = qBound(
        0.0, pasteRedoStatus.value(QStringLiteral("editorContentOffsetY")).toDouble()
                 + pasteRedoStatus.value(QStringLiteral("cursorRectY")).toDouble()
                 - pasteRedoStatus.value(QStringLiteral("scrollViewportHeight")).toDouble()
                     / 3.0,
        pasteRedoMaxY);
    addCheck(checks, details, QStringLiteral("redoJumpsBelowBottomAnchorsLikeInput"),
             pasteRedoStatus.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor + scrollPasteBlock.size()
                 && std::abs(pasteRedoStatus.value(QStringLiteral("scrollContentY")).toDouble()
                             - pasteRedoExpectedY) < 3.0
                 && pasteRedoStatus.value(QStringLiteral("scrollContentY")).toDouble()
                    < pasteRedoMaxY,
             QJsonObject{{QStringLiteral("expectedY"), pasteRedoExpectedY},
                         {QStringLiteral("maxY"), pasteRedoMaxY},
                         {QStringLiteral("redo"), pasteRedoStatus}});

    // 末尾删除（视图位于 max、光标在上 1/3）：弹性底部缓冲保持视图不动，
    // 光标随删除自然上移，避免 Flickable 钳制导致“触顶失败”。
    request(QStringLiteral("testSetText"), {{QStringLiteral("text"), scrollText}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), scrollEndCursor},
             {QStringLiteral("end"), scrollEndCursor}});
    setScrollY(0);
    QThread::msleep(50);
    keyPress(QStringLiteral("x"));
    QThread::msleep(80);
    const QJsonObject endDeleteBase = editorStatus();
    const double endDeleteBaseY =
        endDeleteBase.value(QStringLiteral("scrollContentY")).toDouble();
    const double endDeleteBaseScreenY =
        endDeleteBase.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + endDeleteBase.value(QStringLiteral("cursorRectY")).toDouble()
        - endDeleteBaseY;
    for (int i = 0; i < 40; ++i) {
        keyPress({}, QStringLiteral("Backspace"));
        QThread::msleep(16);
    }
    QThread::msleep(80);
    const QJsonObject endDeleteHeld = editorStatus();
    const double endDeleteHeldScreenY =
        endDeleteHeld.value(QStringLiteral("editorContentOffsetY")).toDouble()
        + endDeleteHeld.value(QStringLiteral("cursorRectY")).toDouble()
        - endDeleteHeld.value(QStringLiteral("scrollContentY")).toDouble();
    addCheck(checks, details, QStringLiteral("deleteAtEndHoldsViewWhileCursorAscends"),
             std::abs(endDeleteHeld.value(QStringLiteral("scrollContentY")).toDouble()
                      - endDeleteBaseY) < 1.5
                 && endDeleteHeldScreenY < endDeleteBaseScreenY - 10.0
                 && endDeleteHeld.value(QStringLiteral("textLength")).toInt()
                    == scrollEndCursor + 1 - 40
                 && endDeleteHeld.value(QStringLiteral("cursorPosition")).toInt()
                    == scrollEndCursor + 1 - 40,
             QJsonObject{{QStringLiteral("baseY"), endDeleteBaseY},
                         {QStringLiteral("baseScreenY"), endDeleteBaseScreenY},
                         {QStringLiteral("held"), endDeleteHeld},
                         {QStringLiteral("heldScreenY"), endDeleteHeldScreenY}});
    // --- 中英文词边界：纯函数测试（SEG-001..） ---
    const auto wordBoundaryExpect = [&](const QString &name, const QString &text,
                                        int position, int direction, int expected) {
        const int actual = CjkText::moveWordBoundary(text, position, direction);
        addCheck(checks, details, name, actual == expected,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("position"), position},
                             {QStringLiteral("direction"), direction},
                             {QStringLiteral("expected"), expected},
                             {QStringLiteral("actual"), actual}});
    };
    const auto wordRangeExpect = [&](const QString &name, const QString &text,
                                     int position, int expectedStart,
                                     int expectedEnd) {
        const CjkText::WordRange actual = CjkText::wordRangeAt(text, position);
        const QString expected =
            QStringLiteral("[%1,%2)").arg(expectedStart).arg(expectedEnd);
        const QString actualRange =
            QStringLiteral("[%1,%2)").arg(actual.start).arg(actual.end);
        addCheck(checks, details, name,
                 actual.start == expectedStart && actual.end == expectedEnd,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("position"), position},
                             {QStringLiteral("expected"), expected},
                             {QStringLiteral("actual"), actualRange}});
    };
    const auto spanCjkExpect = [&](const QString &name, const QString &text,
                                   int start, int end, bool expected) {
        const bool actual = CjkText::spanContainsCjk(text, start, end);
        addCheck(checks, details, name, actual == expected,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("start"), start},
                             {QStringLiteral("end"), end},
                             {QStringLiteral("expected"), expected},
                             {QStringLiteral("actual"), actual}});
    };
    const auto hanCountExpect = [&](const QString &name, const QString &text,
                                    int expected) {
        const int actual = CjkText::countHanCharacters(text);
        addCheck(checks, details, name, actual == expected,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("expected"), expected},
                             {QStringLiteral("actual"), actual}});
    };
    const auto ucs4 = [](char32_t codePoint) {
        return QString::fromUcs4(&codePoint, 1);
    };
    const auto wordRangeCursorExpect = [&](const QString &name, const QString &text,
                                           int position, int expectedStart,
                                           int expectedEnd) {
        const CjkText::WordRange actual =
            CjkText::wordRangeForCursor(text, position);
        const QString expected =
            QStringLiteral("[%1,%2)").arg(expectedStart).arg(expectedEnd);
        const QString actualRange =
            QStringLiteral("[%1,%2)").arg(actual.start).arg(actual.end);
        addCheck(checks, details, name,
                 actual.start == expectedStart && actual.end == expectedEnd,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("position"), position},
                             {QStringLiteral("expected"), expected},
                             {QStringLiteral("actual"), actualRange}});
    };

    const QString cjkRunText = QStringLiteral("今天天气真好");
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkRunRightFromStart"),
                       cjkRunText, 0, 1, 6);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkRunRightInside"),
                       cjkRunText, 3, 1, 6);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkRunRightAtEnd"),
                       cjkRunText, 6, 1, 6);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkRunLeftFromEnd"),
                       cjkRunText, 6, -1, 0);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkRunLeftInside"),
                       cjkRunText, 2, -1, 0);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkRunLeftAtStart"),
                       cjkRunText, 0, -1, 0);
    wordRangeExpect(QStringLiteral("wordRangeCjkRunMiddle"), cjkRunText, 3, 0, 6);
    wordRangeExpect(QStringLiteral("wordRangeCjkRunEnd"), cjkRunText, 6, 0, 6);
    spanCjkExpect(QStringLiteral("spanCjkRunAll"), cjkRunText, 0, 6, true);

    // --- 汉字字符数统计（HAN-001..）：只计汉字表意字符，按码点计数 ---
    hanCountExpect(QStringLiteral("hanCountPlainCjk"), QStringLiteral("你好世界"), 4);
    hanCountExpect(QStringLiteral("hanCountMixedLatin"), QStringLiteral("你好 World 123"), 2);
    hanCountExpect(QStringLiteral("hanCountExtensionA"), ucs4(0x3400), 1);
    hanCountExpect(QStringLiteral("hanCountCompatibilityIdeograph"), ucs4(0xFA0E), 1);
    hanCountExpect(QStringLiteral("hanCountSupplementaryPlane"), ucs4(0x20000), 1);
    hanCountExpect(QStringLiteral("hanCountExcludesKana"), QStringLiteral("こんにちは"), 0);
    hanCountExpect(QStringLiteral("hanCountExcludesHangul"), QStringLiteral("한글"), 0);
    hanCountExpect(QStringLiteral("hanCountExcludesCjkPunctuation"),
                   QStringLiteral("，。！？、"), 0);
    hanCountExpect(QStringLiteral("hanCountExcludesAscii"), QStringLiteral("abc 123 !"), 0);
    hanCountExpect(QStringLiteral("hanCountEmpty"), QString(), 0);

    const QString hanRangeText = QStringLiteral("你好abc好");
    addCheck(checks, details, QStringLiteral("hanCountRangeSubset"),
             CjkText::countHanCharacters(hanRangeText, 0, 2) == 2
                 && CjkText::countHanCharacters(hanRangeText, 2, 5) == 0
                 && CjkText::countHanCharacters(hanRangeText, 5, 6) == 1
                 && CjkText::countHanCharacters(hanRangeText, 1, 6) == 2
                 && CjkText::countHanCharacters(hanRangeText, 3, 3) == 0
                 && CjkText::countHanCharacters(hanRangeText, -3, 99) == 3
                 && CjkText::countHanCharacters(hanRangeText, 4, 2) == 0
                 && CjkText::countHanCharacters(hanRangeText, 0, -1) == 3,
             QJsonObject{{QStringLiteral("input"), hanRangeText}});

    const char32_t supplementaryCodes[] = {0x20000, 0x4E00};
    const QString hanPairText = QString::fromUcs4(supplementaryCodes, 2);
    addCheck(checks, details, QStringLiteral("hanCountRangeSurrogateBoundary"),
             CjkText::countHanCharacters(hanPairText) == 2
                 && CjkText::countHanCharacters(hanPairText, 0, 2) == 1
                 && CjkText::countHanCharacters(hanPairText, 0, 1) == 0
                 && CjkText::countHanCharacters(hanPairText, 1, 3) == 1,
             QJsonObject{{QStringLiteral("input"), hanPairText}});

    const QString cjkPunctText = QStringLiteral("今天，天气真好！我们走吧");
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctRightRun"),
                       cjkPunctText, 0, 1, 2);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctRightSkipComma"),
                       cjkPunctText, 2, 1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctRightRun2"),
                       cjkPunctText, 3, 1, 7);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctRightSkipBang"),
                       cjkPunctText, 7, 1, 8);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctRightLastRun"),
                       cjkPunctText, 8, 1, 12);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctLeftLastRun"),
                       cjkPunctText, 12, -1, 8);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctLeftSkipBang"),
                       cjkPunctText, 8, -1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctLeftSkipComma"),
                       cjkPunctText, 3, -1, 0);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctLeftInsideFirst"),
                       cjkPunctText, 2, -1, 0);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctLeftInsideSecond"),
                       cjkPunctText, 7, -1, 3);
    wordRangeExpect(QStringLiteral("wordRangeCjkPunctRunFirst"),
                    cjkPunctText, 1, 0, 2);
    wordRangeExpect(QStringLiteral("wordRangeCjkPunctComma"),
                    cjkPunctText, 2, 2, 3);
    wordRangeExpect(QStringLiteral("wordRangeCjkPunctRunSecond"),
                    cjkPunctText, 4, 3, 7);
    wordRangeExpect(QStringLiteral("wordRangeCjkPunctBang"),
                    cjkPunctText, 7, 7, 8);
    wordRangeExpect(QStringLiteral("wordRangeCjkPunctRunLast"),
                    cjkPunctText, 9, 8, 12);

    const QString mixedText = QStringLiteral("abc今天天气 good 123");
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedRightLatin"),
                       mixedText, 0, 1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedRightLatinInside"),
                       mixedText, 1, 1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedRightCjk"),
                       mixedText, 3, 1, 7);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedRightSpace"),
                       mixedText, 7, 1, 8);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedRightWord"),
                       mixedText, 8, 1, 12);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedRightSpaceNum"),
                       mixedText, 12, 1, 13);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedRightNumber"),
                       mixedText, 13, 1, 16);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedLeftNumber"),
                       mixedText, 16, -1, 13);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedLeftWord"),
                       mixedText, 13, -1, 8);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedLeftSpaceCjk"),
                       mixedText, 8, -1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedLeftLatin"),
                       mixedText, 3, -1, 0);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedLeftCjkInside"),
                       mixedText, 5, -1, 3);
    wordRangeExpect(QStringLiteral("wordRangeMixedLatin"),
                    mixedText, 1, 0, 3);
    wordRangeExpect(QStringLiteral("wordRangeMixedCjk"),
                    mixedText, 4, 3, 7);
    wordRangeExpect(QStringLiteral("wordRangeMixedWord"),
                    mixedText, 8, 8, 12);
    wordRangeExpect(QStringLiteral("wordRangeMixedNumber"),
                    mixedText, 14, 13, 16);
    spanCjkExpect(QStringLiteral("spanCjkMixedLatinOnly"), mixedText, 0, 3, false);
    spanCjkExpect(QStringLiteral("spanCjkMixedLatinAndCjk"), mixedText, 0, 4, true);

    const QString cjkAsciiText = QStringLiteral("中文，ABC");
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkAsciiRightRun"),
                       cjkAsciiText, 0, 1, 2);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkAsciiRightSkipComma"),
                       cjkAsciiText, 2, 1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkAsciiRightWord"),
                       cjkAsciiText, 3, 1, 6);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkAsciiLeftWord"),
                       cjkAsciiText, 6, -1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkAsciiLeftSkipComma"),
                       cjkAsciiText, 3, -1, 0);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkAsciiLeftRun"),
                       cjkAsciiText, 2, -1, 0);

    QString emojiText = QStringLiteral("中");
    emojiText += QChar(0xD83D);
    emojiText += QChar(0xDE00);
    emojiText += QStringLiteral("文");
    wordBoundaryExpect(QStringLiteral("wordBoundaryEmojiRightCjk"),
                       emojiText, 0, 1, 1);
    wordBoundaryExpect(QStringLiteral("wordBoundaryEmojiRightSkipPair"),
                       emojiText, 1, 1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryEmojiRightTail"),
                       emojiText, 3, 1, 4);
    wordBoundaryExpect(QStringLiteral("wordBoundaryEmojiLeftTail"),
                       emojiText, 4, -1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryEmojiLeftSkipPair"),
                       emojiText, 3, -1, 0);
    wordBoundaryExpect(QStringLiteral("wordBoundaryEmojiLeftInsidePair"),
                       emojiText, 2, -1, 0);
    wordRangeExpect(QStringLiteral("wordRangeEmojiHead"), emojiText, 0, 0, 1);
    wordRangeExpect(QStringLiteral("wordRangeEmojiPair"), emojiText, 1, 1, 3);
    wordRangeExpect(QStringLiteral("wordRangeEmojiTail"), emojiText, 3, 3, 4);
    wordRangeExpect(QStringLiteral("wordRangeEmojiEnd"), emojiText, 4, 3, 4);
    spanCjkExpect(QStringLiteral("spanCjkEmojiOnly"), emojiText, 1, 3, false);
    spanCjkExpect(QStringLiteral("spanCjkEmojiAndCjk"), emojiText, 0, 4, true);

    QString extBText;
    extBText += QChar(0xD840);
    extBText += QChar(0xDC00);
    extBText += QStringLiteral("中");
    wordBoundaryExpect(QStringLiteral("wordBoundaryExtBRight"),
                       extBText, 0, 1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryExtBLeft"),
                       extBText, 3, -1, 0);
    wordRangeExpect(QStringLiteral("wordRangeExtB"), extBText, 0, 0, 3);
    spanCjkExpect(QStringLiteral("spanCjkExtB"), extBText, 0, 2, true);

    const QString newlineText = QStringLiteral("中文\ndef");
    wordBoundaryExpect(QStringLiteral("wordBoundaryNewlineRightRun"),
                       newlineText, 0, 1, 2);
    wordBoundaryExpect(QStringLiteral("wordBoundaryNewlineRightSkip"),
                       newlineText, 2, 1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryNewlineRightWord"),
                       newlineText, 3, 1, 6);
    wordBoundaryExpect(QStringLiteral("wordBoundaryNewlineLeftWord"),
                       newlineText, 6, -1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryNewlineLeftSkip"),
                       newlineText, 3, -1, 0);

    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkRunInside"),
                          cjkRunText, 3, 0, 6);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkRunStart"),
                          cjkRunText, 0, 0, 6);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkRunEnd"),
                          cjkRunText, 6, 0, 6);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkPunctRunFirst"),
                          cjkPunctText, 1, 0, 2);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkPunctBeforeComma"),
                          cjkPunctText, 2, 0, 2);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkPunctAfterComma"),
                          cjkPunctText, 3, 3, 7);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkPunctRunSecond"),
                          cjkPunctText, 4, 3, 7);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkPunctBeforeBang"),
                          cjkPunctText, 7, 3, 7);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkPunctAfterBang"),
                          cjkPunctText, 8, 8, 12);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkPunctRunLast"),
                          cjkPunctText, 9, 8, 12);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorMixedLatinInside"),
                          mixedText, 1, 0, 3);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorMixedCjkLatinBoundary"),
                          mixedText, 3, 0, 3);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorMixedCjkInside"),
                          mixedText, 4, 3, 7);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorMixedWordStart"),
                          mixedText, 8, 8, 12);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorMixedWordEnd"),
                          mixedText, 12, 8, 12);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkAsciiBeforeComma"),
                          cjkAsciiText, 2, 0, 2);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkAsciiAfterComma"),
                          cjkAsciiText, 3, 3, 6);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorLatinEndOfWord"),
                          QStringLiteral("a big"), 5, 2, 5);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorLatinStartOfWord"),
                          QStringLiteral("a big rat"), 6, 6, 9);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorLatinEmptyBoundary"),
                          QStringLiteral("a big "), 6, 6, 6);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorEmojiBetween"),
                          emojiText, 1, 0, 1);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorEmojiPair"),
                          emojiText, 2, 1, 3);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorEmojiTail"),
                          emojiText, 3, 3, 4);

    const auto wordDeletionExpect = [&](const QString &name, const QString &text,
                                        int position, bool backwards,
                                        int expectedStart, int expectedEnd) {
        const CjkText::WordRange actual =
            CjkText::wordDeletionRange(text, position, backwards);
        const QString expected =
            QStringLiteral("[%1,%2)").arg(expectedStart).arg(expectedEnd);
        const QString actualRange =
            QStringLiteral("[%1,%2)").arg(actual.start).arg(actual.end);
        addCheck(checks, details, name,
                 actual.start == expectedStart && actual.end == expectedEnd,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("position"), position},
                             {QStringLiteral("backwards"), backwards},
                             {QStringLiteral("expected"), expected},
                             {QStringLiteral("actual"), actualRange}});
    };

    wordDeletionExpect(QStringLiteral("wordDeletionCjkRunBackFromEnd"),
                       cjkRunText, 6, true, 0, 6);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkRunBackInside"),
                       cjkRunText, 3, true, 0, 3);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkRunBackAtStart"),
                       cjkRunText, 0, true, 0, 0);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkRunFwdFromStart"),
                       cjkRunText, 0, false, 0, 6);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkRunFwdInside"),
                       cjkRunText, 3, false, 3, 6);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkRunFwdAtEnd"),
                       cjkRunText, 6, false, 6, 6);
    wordDeletionExpect(QStringLiteral("wordDeletionMixedBackCjkFromEnd"),
                       mixedText, 8, true, 3, 8);
    wordDeletionExpect(QStringLiteral("wordDeletionMixedBackInsideCjk"),
                       mixedText, 4, true, 3, 4);
    wordDeletionExpect(QStringLiteral("wordDeletionMixedFwdLatin"),
                       mixedText, 0, false, 0, 3);
    wordDeletionExpect(QStringLiteral("wordDeletionMixedFwdCjk"),
                       mixedText, 3, false, 3, 8);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkPunctFwdRunAndComma"),
                       cjkPunctText, 0, false, 0, 3);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkPunctFwdCommaOnly"),
                       cjkPunctText, 2, false, 2, 3);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkPunctFwdSecondRun"),
                       cjkPunctText, 3, false, 3, 8);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkPunctBackWordAndComma"),
                       cjkPunctText, 3, true, 0, 3);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkPunctBackSecondRun"),
                       cjkPunctText, 7, true, 3, 7);
    wordDeletionExpect(QStringLiteral("wordDeletionLatinBackWordAndSpace"),
                       QStringLiteral("hello world"), 6, true, 0, 6);
    wordDeletionExpect(QStringLiteral("wordDeletionLatinBackInsideWord"),
                       QStringLiteral("hello world"), 11, true, 6, 11);
    wordDeletionExpect(QStringLiteral("wordDeletionLatinFwdWordAndSpace"),
                       QStringLiteral("hello world"), 0, false, 0, 6);
    wordDeletionExpect(QStringLiteral("wordDeletionLatinFwdSpaceOnly"),
                       QStringLiteral("hello world"), 5, false, 5, 6);
    wordDeletionExpect(QStringLiteral("wordDeletionLatinBackAtStart"),
                       QStringLiteral("hello world"), 0, true, 0, 0);
    wordDeletionExpect(QStringLiteral("wordDeletionLatinFwdAtEnd"),
                       QStringLiteral("hello world"), 11, false, 11, 11);
    wordDeletionExpect(QStringLiteral("wordDeletionNewlineBackOnlyNewline"),
                       newlineText, 3, true, 2, 3);
    wordDeletionExpect(QStringLiteral("wordDeletionNewlineFwdOnlyNewline"),
                       newlineText, 2, false, 2, 3);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkSpaceBackWordAndSpace"),
                       QStringLiteral("中文 test"), 3, true, 0, 3);

    // --- 中英文词边界：端到端（Ctrl+方向键 / 双击选词） ---
    const auto wordNavExpect = [&](const QString &name, const QString &text,
                                   int start, const QString &key, bool shift,
                                   const QString &modifiers, int expectedCursor,
                                   int expectedSelStart, int expectedSelEnd) {
        setTextAndSelection(text, start, start);
        const QJsonObject result = keyPress({}, key, shift, modifiers);
        const int cursor = result.value(QStringLiteral("cursorPosition")).toInt();
        const int selStart = result.value(QStringLiteral("selectionStart")).toInt();
        const int selEnd = result.value(QStringLiteral("selectionEnd")).toInt();
        addCheck(checks, details, name,
                 cursor == expectedCursor && selStart == expectedSelStart
                     && selEnd == expectedSelEnd,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("start"), start},
                             {QStringLiteral("key"), key},
                             {QStringLiteral("shift"), shift},
                             {QStringLiteral("modifiers"), modifiers},
                             {QStringLiteral("expectedCursor"), expectedCursor},
                             {QStringLiteral("actualCursor"), cursor},
                             {QStringLiteral("expectedSelection"),
                              QStringLiteral("[%1,%2)").arg(expectedSelStart)
                                  .arg(expectedSelEnd)},
                             {QStringLiteral("actualSelection"),
                              QStringLiteral("[%1,%2)").arg(selStart).arg(selEnd)}});
    };

    wordNavExpect(QStringLiteral("e2eCtrlRightCjkRun"),
                  cjkRunText, 0, QStringLiteral("Right"), false,
                  QStringLiteral("ctrl"), 6, 6, 6);
    wordNavExpect(QStringLiteral("e2eCtrlLeftCjkRun"),
                  cjkRunText, 6, QStringLiteral("Left"), false,
                  QStringLiteral("ctrl"), 0, 0, 0);
    wordNavExpect(QStringLiteral("e2eCtrlRightCjkPunctFirstRun"),
                  cjkPunctText, 0, QStringLiteral("Right"), false,
                  QStringLiteral("ctrl"), 2, 2, 2);
    wordNavExpect(QStringLiteral("e2eCtrlRightCjkPunctSkipComma"),
                  cjkPunctText, 2, QStringLiteral("Right"), false,
                  QStringLiteral("ctrl"), 3, 3, 3);
    wordNavExpect(QStringLiteral("e2eCtrlLeftCjkPunctSkipBang"),
                  cjkPunctText, 8, QStringLiteral("Left"), false,
                  QStringLiteral("ctrl"), 3, 3, 3);
    wordNavExpect(QStringLiteral("e2eCtrlLeftCjkPunctSkipComma"),
                  cjkPunctText, 3, QStringLiteral("Left"), false,
                  QStringLiteral("ctrl"), 0, 0, 0);
    wordNavExpect(QStringLiteral("e2eCtrlRightMixedCjkBoundary"),
                  mixedText, 3, QStringLiteral("Right"), false,
                  QStringLiteral("ctrl"), 7, 7, 7);
    wordNavExpect(QStringLiteral("e2eCtrlRightMixedSpace"),
                  mixedText, 7, QStringLiteral("Right"), false,
                  QStringLiteral("ctrl"), 8, 8, 8);
    wordNavExpect(QStringLiteral("e2eCtrlLeftMixedCjkStart"),
                  mixedText, 8, QStringLiteral("Left"), false,
                  QStringLiteral("ctrl"), 3, 3, 3);
    wordNavExpect(QStringLiteral("e2eCtrlLeftMixedLatinStart"),
                  mixedText, 3, QStringLiteral("Left"), false,
                  QStringLiteral("ctrl"), 0, 0, 0);

    // Ctrl+Shift 扩选：锚点固定，active end 逐词移动；活动端追平锚点按
    // Qt 原生语义收起选区。
    setTextAndSelection(cjkPunctText, 12, 12);
    const QJsonObject shiftLeft1 = keyPress({}, QStringLiteral("Left"), true,
                                            QStringLiteral("ctrl"));
    const QJsonObject shiftLeft2 = keyPress({}, QStringLiteral("Left"), true,
                                            QStringLiteral("ctrl"));
    const QJsonObject shiftRight1 = keyPress({}, QStringLiteral("Right"), true,
                                             QStringLiteral("ctrl"));
    addCheck(checks, details, QStringLiteral("e2eCtrlShiftSelection"),
             shiftLeft1.value(QStringLiteral("cursorPosition")).toInt() == 8
                 && shiftLeft1.value(QStringLiteral("selectionStart")).toInt() == 8
                 && shiftLeft1.value(QStringLiteral("selectionEnd")).toInt() == 12
                 && shiftLeft2.value(QStringLiteral("cursorPosition")).toInt() == 3
                 && shiftLeft2.value(QStringLiteral("selectionStart")).toInt() == 3
                 && shiftLeft2.value(QStringLiteral("selectionEnd")).toInt() == 12
                 && shiftRight1.value(QStringLiteral("cursorPosition")).toInt() == 7
                 && shiftRight1.value(QStringLiteral("selectionStart")).toInt() == 7
                 && shiftRight1.value(QStringLiteral("selectionEnd")).toInt() == 12,
             QJsonObject{{QStringLiteral("shiftLeft1"), shiftLeft1},
                         {QStringLiteral("shiftLeft2"), shiftLeft2},
                         {QStringLiteral("shiftRight1"), shiftRight1}});

    // 纯 ASCII 文本：Ctrl+方向键落点必须与 Qt 原生逐位置一致（不回归）。
    const QString latinText = QStringLiteral("Hello, world! This is a test.");
    setTextAndSelection(latinText, 0, 0);
    const QVector<int> expectedLatinRight{5, 7, 12, 14, 19, 22, 24, 28, 29};
    bool latinRightOk = true;
    QVector<int> latinRightActual;
    for (const int expected : expectedLatinRight) {
        const QJsonObject result = keyPress({}, QStringLiteral("Right"), false,
                                            QStringLiteral("ctrl"));
        const int cursor = result.value(QStringLiteral("cursorPosition")).toInt();
        latinRightActual.append(cursor);
        latinRightOk = latinRightOk && cursor == expected;
    }
    addCheck(checks, details, QStringLiteral("e2eCtrlRightLatinParity"),
             latinRightOk,
             QJsonObject{{QStringLiteral("expected"),
                          [&expectedLatinRight] {
                              QStringList values;
                              for (int value : expectedLatinRight) {
                                  values << QString::number(value);
                              }
                              return values.join(QLatin1Char(','));
                          }()},
                         {QStringLiteral("actual"),
                          [&latinRightActual] {
                              QStringList values;
                              for (int value : latinRightActual) {
                                  values << QString::number(value);
                              }
                              return values.join(QLatin1Char(','));
                          }()}});
    setTextAndSelection(latinText, latinText.size(), latinText.size());
    const QVector<int> expectedLatinLeft{28, 24, 22, 19, 14, 12, 7, 5, 0};
    bool latinLeftOk = true;
    QVector<int> latinLeftActual;
    for (const int expected : expectedLatinLeft) {
        const QJsonObject result = keyPress({}, QStringLiteral("Left"), false,
                                            QStringLiteral("ctrl"));
        const int cursor = result.value(QStringLiteral("cursorPosition")).toInt();
        latinLeftActual.append(cursor);
        latinLeftOk = latinLeftOk && cursor == expected;
    }
    addCheck(checks, details, QStringLiteral("e2eCtrlLeftLatinParity"),
             latinLeftOk,
             QJsonObject{{QStringLiteral("expected"),
                          [&expectedLatinLeft] {
                              QStringList values;
                              for (int value : expectedLatinLeft) {
                                  values << QString::number(value);
                              }
                              return values.join(QLatin1Char(','));
                          }()},
                         {QStringLiteral("actual"),
                          [&latinLeftActual] {
                              QStringList values;
                              for (int value : latinLeftActual) {
                                  values << QString::number(value);
                              }
                              return values.join(QLatin1Char(','));
                          }()}});

    const auto dblClickExpect = [&](const QString &name, const QString &text,
                                    int position, int expectedStart,
                                    int expectedEnd) {
        setTextAndSelection(text, 0, 0);
        const QJsonObject result = doubleClick(position);
        const int selStart = result.value(QStringLiteral("selectionStart")).toInt();
        const int selEnd = result.value(QStringLiteral("selectionEnd")).toInt();
        addCheck(checks, details, name,
                 result.value(QStringLiteral("eventsAccepted")).toBool()
                     && selStart == expectedStart && selEnd == expectedEnd,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("position"), position},
                             {QStringLiteral("expected"),
                              QStringLiteral("[%1,%2)").arg(expectedStart)
                                  .arg(expectedEnd)},
                             {QStringLiteral("actual"),
                              QStringLiteral("[%1,%2)").arg(selStart).arg(selEnd)},
                             {QStringLiteral("response"), result}});
    };

    dblClickExpect(QStringLiteral("e2eDoubleClickCjkRunMiddle"),
                   cjkPunctText, 4, 3, 7);
    dblClickExpect(QStringLiteral("e2eDoubleClickCjkRunWhole"),
                   cjkRunText, 2, 0, 6);
    dblClickExpect(QStringLiteral("e2eDoubleClickMixedLatin"),
                   mixedText, 1, 0, 3);
    dblClickExpect(QStringLiteral("e2eDoubleClickMixedCjk"),
                   mixedText, 4, 3, 7);
    dblClickExpect(QStringLiteral("e2eDoubleClickLatinWord"),
                   latinText, 8, 7, 12);
    dblClickExpect(QStringLiteral("e2eDoubleClickPunctWithSpaces"),
                   QStringLiteral("今天 ， 天气"), 3, 3, 4);

    // Markdown 快捷键（Ctrl+B / Ctrl+I / Ctrl+Alt+C）无选区时同样使用
    // 中英文自适应词边界，不再把“英文+中文”混合串整体包裹。
    const auto markdownWordExpect = [&](const QString &name, const QString &text,
                                        int position, const QString &commandId,
                                        const QString &expectedText,
                                        int expectedCursor) {
        setTextAndSelection(text, position, position);
        const QJsonObject result = execute(commandId);
        const int cursor = result.value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, name,
                 result.value(QStringLiteral("text")).toString() == expectedText
                     && cursor == expectedCursor,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("position"), position},
                             {QStringLiteral("commandId"), commandId},
                             {QStringLiteral("expectedText"), expectedText},
                             {QStringLiteral("actualText"),
                              result.value(QStringLiteral("text")).toString()},
                             {QStringLiteral("expectedCursor"), expectedCursor},
                             {QStringLiteral("actualCursor"), cursor}});
    };

    markdownWordExpect(QStringLiteral("e2eBoldCjkRunWhole"),
                       cjkRunText, 3, QStringLiteral("toggleBold"),
                       QStringLiteral("**今天天气真好**"), 5);
    markdownWordExpect(QStringLiteral("e2eBoldMixedLatinSide"),
                       mixedText, 1, QStringLiteral("toggleBold"),
                       QStringLiteral("**abc**今天天气 good 123"), 3);
    markdownWordExpect(QStringLiteral("e2eBoldMixedCjkSide"),
                       mixedText, 4, QStringLiteral("toggleBold"),
                       QStringLiteral("abc**今天天气** good 123"), 6);
    markdownWordExpect(QStringLiteral("e2eBoldMixedBoundaryTakesLeft"),
                       mixedText, 3, QStringLiteral("toggleBold"),
                       QStringLiteral("**abc**今天天气 good 123"), 5);
    markdownWordExpect(QStringLiteral("e2eBoldCjkPunctAfterComma"),
                       cjkPunctText, 3, QStringLiteral("toggleBold"),
                       QStringLiteral("今天，**天气真好**！我们走吧"), 5);
    markdownWordExpect(QStringLiteral("e2eItalicCjkRun"),
                       cjkRunText, 2, QStringLiteral("toggleItalic"),
                       QStringLiteral("*今天天气真好*"), 3);
    markdownWordExpect(QStringLiteral("e2eCodeCjkRun"),
                       cjkRunText, 2, QStringLiteral("wrapCode"),
                       QStringLiteral("`今天天气真好`"), 3);

    // Ctrl+Backspace / Ctrl+Delete 按词删除：纯 ASCII 跨度保持 Qt 原生行为，
    // 跨中文时使用同一套中英文分词边界。
    const auto wordDeleteExpect = [&](const QString &name, const QString &text,
                                      int position, const QString &key,
                                      const QString &expectedText,
                                      int expectedCursor) {
        setTextAndSelection(text, position, position);
        const QJsonObject result = keyPress({}, key, false,
                                            QStringLiteral("ctrl"));
        const int cursor = result.value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, name,
                 result.value(QStringLiteral("text")).toString() == expectedText
                     && cursor == expectedCursor,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("position"), position},
                             {QStringLiteral("key"), key},
                             {QStringLiteral("expectedText"), expectedText},
                             {QStringLiteral("actualText"),
                              result.value(QStringLiteral("text")).toString()},
                             {QStringLiteral("expectedCursor"), expectedCursor},
                             {QStringLiteral("actualCursor"), cursor}});
    };

    wordDeleteExpect(QStringLiteral("e2eCtrlBackspaceCjkRun"),
                     cjkRunText, 6, QStringLiteral("Backspace"),
                     QString(), 0);
    wordDeleteExpect(QStringLiteral("e2eCtrlBackspaceMixedCjk"),
                     mixedText, 7, QStringLiteral("Backspace"),
                     QStringLiteral("abc good 123"), 3);
    wordDeleteExpect(QStringLiteral("e2eCtrlBackspaceInsideCjk"),
                     mixedText, 4, QStringLiteral("Backspace"),
                     QStringLiteral("abc天天气 good 123"), 3);
    wordDeleteExpect(QStringLiteral("e2eCtrlDeleteMixedLatin"),
                     mixedText, 0, QStringLiteral("Delete"),
                     QStringLiteral("今天天气 good 123"), 0);
    wordDeleteExpect(QStringLiteral("e2eCtrlDeleteCjkPunct"),
                     cjkPunctText, 0, QStringLiteral("Delete"),
                     QStringLiteral("天气真好！我们走吧"), 0);
    wordDeleteExpect(QStringLiteral("e2eCtrlBackspaceLatinParity"),
                     QStringLiteral("hello world"), 6, QStringLiteral("Backspace"),
                     QStringLiteral("world"), 0);
    wordDeleteExpect(QStringLiteral("e2eCtrlDeleteLatinParity"),
                     QStringLiteral("hello world"), 0, QStringLiteral("Delete"),
                     QStringLiteral("world"), 0);
    wordDeleteExpect(QStringLiteral("e2eCtrlBackspaceLineJoin"),
                     QStringLiteral("hello\nworld"), 6, QStringLiteral("Backspace"),
                     QStringLiteral("helloworld"), 5);
    wordDeleteExpect(QStringLiteral("e2eCtrlBackspaceNoOpAtStart"),
                     QStringLiteral("hello"), 0, QStringLiteral("Backspace"),
                     QStringLiteral("hello"), 0);
    wordDeleteExpect(QStringLiteral("e2eCtrlDeleteNoOpAtEnd"),
                     QStringLiteral("hello"), 5, QStringLiteral("Delete"),
                     QStringLiteral("hello"), 5);

    // 连续 Ctrl+Backspace 沿标点逐段删除中文词。
    setTextAndSelection(cjkPunctText, 12, 12);
    const QJsonObject deleteStep1 = keyPress({}, QStringLiteral("Backspace"),
                                             false, QStringLiteral("ctrl"));
    const QJsonObject deleteStep2 = keyPress({}, QStringLiteral("Backspace"),
                                             false, QStringLiteral("ctrl"));
    const QJsonObject deleteStep3 = keyPress({}, QStringLiteral("Backspace"),
                                             false, QStringLiteral("ctrl"));
    addCheck(checks, details, QStringLiteral("e2eCtrlBackspaceCjkPunctChain"),
             deleteStep1.value(QStringLiteral("text")).toString()
                    == QStringLiteral("今天，天气真好！")
                 && deleteStep1.value(QStringLiteral("cursorPosition")).toInt() == 8
                 && deleteStep2.value(QStringLiteral("text")).toString()
                    == QStringLiteral("今天，")
                 && deleteStep2.value(QStringLiteral("cursorPosition")).toInt() == 3
                 && deleteStep3.value(QStringLiteral("text")).toString().isEmpty()
                 && deleteStep3.value(QStringLiteral("cursorPosition")).toInt() == 0,
             QJsonObject{{QStringLiteral("step1"), deleteStep1},
                         {QStringLiteral("step2"), deleteStep2},
                         {QStringLiteral("step3"), deleteStep3}});

    // 有选区时 Ctrl+Backspace 与原生一致：删除选区。
    setTextAndSelection(mixedText, 3, 7);
    const QJsonObject deleteSelection = keyPress({}, QStringLiteral("Backspace"),
                                                 false, QStringLiteral("ctrl"));
    addCheck(checks, details, QStringLiteral("e2eCtrlWordDeleteWithSelection"),
             deleteSelection.value(QStringLiteral("text")).toString()
                    == QStringLiteral("abc good 123")
                 && deleteSelection.value(QStringLiteral("cursorPosition")).toInt() == 3,
             deleteSelection);

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
