#include "suites.h"
#include "support.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextFormat>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextCharFormat>
#include <QTextLayout>
#include "markdownhighlighter.h"
#include "markdownstyle.h"
#include <array>

namespace EditingTests {

void checkStyleConfiguration(QJsonObject &checks, QJsonObject &details)
{
    QTemporaryDir markdownStyleDirectory(
        QDir::tempPath() + QStringLiteral("/ScratchEditor-MarkdownStyle-XXXXXX"));
    const QString markdownStylePath = markdownStyleDirectory.filePath(
        QStringLiteral("markdown-style.json"));
    QFile markdownStyleFile(markdownStylePath);
    const bool markdownStyleFixtureWritten = markdownStyleFile.open(QIODevice::WriteOnly)
        && markdownStyleFile.write(QJsonDocument(QJsonObject{
               {QStringLiteral("inlineCode"),
                QJsonObject{{QStringLiteral("fontStyle"), QStringLiteral("bold")}}},
               {QStringLiteral("quote"),
                QJsonObject{{QStringLiteral("fontStyle"), QStringLiteral("bold italic")},
                            {QStringLiteral("fontWeightDelta"), -200}}},
               {QStringLiteral("bold"),
                QJsonObject{{QStringLiteral("fontStyle"), QStringLiteral("normal")},
                            {QStringLiteral("fontWeightDelta"), 250}}},
               {QStringLiteral("strikethrough"),
                QJsonObject{{QStringLiteral("fontWeightDelta"), -900}}},
           }).toJson(QJsonDocument::Compact)) > 0;
    markdownStyleFile.close();
    const QByteArray previousMarkdownStyle = qgetenv("SCRATCHEDITOR_MARKDOWN_STYLE");
    qputenv("SCRATCHEDITOR_MARKDOWN_STYLE", QFile::encodeName(markdownStylePath));
    const MarkdownStyle parsedMarkdownStyle = MarkdownStyle::load(true);
    if (previousMarkdownStyle.isNull()) {
        qunsetenv("SCRATCHEDITOR_MARKDOWN_STYLE");
    } else {
        qputenv("SCRATCHEDITOR_MARKDOWN_STYLE", previousMarkdownStyle);
    }
    addCheck(checks, details, QStringLiteral("relativeMarkdownWeightConfiguration"),
             markdownStyleFixtureWritten && parsedMarkdownStyle.loadedFromFile()
                 && parsedMarkdownStyle.inlineCode.fontWeightDelta == 0
                 && !parsedMarkdownStyle.inlineCode.italic
                 && parsedMarkdownStyle.quote.fontWeightDelta == -200
                 && parsedMarkdownStyle.quote.italic
                 && parsedMarkdownStyle.bold.fontWeightDelta == 300
                 && parsedMarkdownStyle.strikethrough.fontWeightDelta == 0
                 && parsedMarkdownStyle.textFormat(parsedMarkdownStyle.quote, 100)
                        .fontWeight() == 100
                 && parsedMarkdownStyle.textFormat(parsedMarkdownStyle.bold, 800)
                        .fontWeight() == 900,
             QJsonObject{{QStringLiteral("fixtureWritten"), markdownStyleFixtureWritten},
                         {QStringLiteral("loaded"), parsedMarkdownStyle.loadedFromFile()},
                         {QStringLiteral("inlineCodeDelta"),
                          parsedMarkdownStyle.inlineCode.fontWeightDelta},
                         {QStringLiteral("quoteDelta"),
                          parsedMarkdownStyle.quote.fontWeightDelta},
                         {QStringLiteral("quoteItalic"), parsedMarkdownStyle.quote.italic},
                         {QStringLiteral("boldDelta"), parsedMarkdownStyle.bold.fontWeightDelta},
                         {QStringLiteral("strikethroughDelta"),
                          parsedMarkdownStyle.strikethrough.fontWeightDelta},
                         {QStringLiteral("minimumResolvedWeight"),
                          parsedMarkdownStyle.textFormat(parsedMarkdownStyle.quote, 100)
                              .fontWeight()},
                         {QStringLiteral("maximumResolvedWeight"),
                          parsedMarkdownStyle.textFormat(parsedMarkdownStyle.bold, 800)
                              .fontWeight()}});
}

QJsonObject checkInitializeEditor(QJsonObject &checks, QJsonObject &details)
{
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
    return initial;
}

void checkHighlighting(QJsonObject &checks, QJsonObject &details, const QJsonObject &initial)
{
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

    addCheck(checks, details, QStringLiteral("relativeMarkdownWeightDefaults"),
             inlineCodeStyle.value(QStringLiteral("fontWeight")).toInt() == 400
                 && quoteStyle.value(QStringLiteral("fontWeight")).toInt() == 400
                 && boldStyle.value(QStringLiteral("fontWeight")).toInt() == 700
                 && boldItalicStyle.value(QStringLiteral("fontWeight")).toInt() == 700,
             QJsonObject{{QStringLiteral("inlineCode"), inlineCodeStyle},
                         {QStringLiteral("quote"), quoteStyle},
                         {QStringLiteral("bold"), boldStyle},
                         {QStringLiteral("boldItalic"), boldItalicStyle}});

    const auto applyEditorWeight = [&initial](int fontWeight) {
        return request(
            QStringLiteral("testApplyAppearance"),
            {{QStringLiteral("theme"), initial.value(QStringLiteral("theme")).toString()},
             {QStringLiteral("fontFamily"),
              initial.value(QStringLiteral("editorFontFamily")).toString()},
             {QStringLiteral("fallbackFontFamily"),
              initial.value(QStringLiteral("editorFallbackFontFamily")).toString()},
             {QStringLiteral("fontPointSize"),
              initial.value(QStringLiteral("editorFontPointSize")).toInt()},
             {QStringLiteral("fontWeight"), fontWeight},
             {QStringLiteral("animationsEnabled"), false}});
    };
    const QJsonObject weight600Applied = applyEditorWeight(600);
    const QJsonObject codeAtWeight600 = formatAt(styledMarkdown, QStringLiteral("`code`"), 1);
    const QJsonObject boldAtWeight600 = formatAt(styledMarkdown, QStringLiteral("**bold**"), 2);
    addCheck(checks, details, QStringLiteral("relativeMarkdownWeightTracksAppearance"),
             weight600Applied.value(QStringLiteral("applied")).toBool()
                 && codeAtWeight600.value(QStringLiteral("fontWeight")).toInt() == 600
                 && boldAtWeight600.value(QStringLiteral("fontWeight")).toInt() == 900,
             QJsonObject{{QStringLiteral("applied"), weight600Applied},
                         {QStringLiteral("code"), codeAtWeight600},
                         {QStringLiteral("bold"), boldAtWeight600}});

    const QJsonObject weight800Applied = applyEditorWeight(800);
    const QJsonObject codeAtWeight800 = formatAt(styledMarkdown, QStringLiteral("`code`"), 1);
    const QJsonObject boldAtWeight800 = formatAt(styledMarkdown, QStringLiteral("**bold**"), 2);
    addCheck(checks, details, QStringLiteral("relativeMarkdownWeightClampsAtMaximum"),
             weight800Applied.value(QStringLiteral("applied")).toBool()
                 && codeAtWeight800.value(QStringLiteral("fontWeight")).toInt() == 800
                 && boldAtWeight800.value(QStringLiteral("fontWeight")).toInt() == 900,
             QJsonObject{{QStringLiteral("applied"), weight800Applied},
                         {QStringLiteral("code"), codeAtWeight800},
                         {QStringLiteral("bold"), boldAtWeight800}});
    applyEditorWeight(initial.value(QStringLiteral("editorFontWeight")).toInt());

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

    const QString externalLink = QStringLiteral("前缀 [标题](https://example.invalid/path) 后缀");
    const int externalLinkStart = externalLink.indexOf(QLatin1Char('['));
    const int hoverPosition = externalLinkStart + 2;
    QTextDocument linkDocument;
    linkDocument.setPlainText(externalLink);
    MarkdownHighlighter linkHighlighter(&linkDocument, MarkdownStyle::load(true), 400);
    const auto formatAtDocumentPosition = [&linkDocument](int position) {
        QTextCharFormat combinedFormat;
        const QTextBlock block = linkDocument.findBlock(position);
        if (!block.isValid() || !block.layout()) {
            return combinedFormat;
        }
        const int positionInBlock = position - block.position();
        for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
            if (positionInBlock >= range.start
                && positionInBlock < range.start + range.length) {
                combinedFormat.merge(range.format);
            }
        }
        return combinedFormat;
    };
    const bool externalLinkRecognized = !linkHighlighter.externalLinkAt(hoverPosition).isEmpty();
    linkHighlighter.setHoveredPosition(hoverPosition);
    const int externalLinkEnd = externalLink.indexOf(QStringLiteral(")")) + 1;
    const bool entireSyntaxHitTest = [&linkHighlighter, externalLinkStart,
                                      externalLinkEnd, &externalLink] {
        for (int position = externalLinkStart; position < externalLinkEnd; ++position) {
            if (linkHighlighter.externalLinkAt(position).isEmpty()) {
                return false;
            }
        }
        return linkHighlighter.setHoveredPosition(externalLinkEnd);
    }();
    bool completeSyntaxHighlighted = true;
    for (int position = externalLinkStart; position < externalLinkEnd; ++position) {
        const QColor background = formatAtDocumentPosition(position).background().color();
        completeSyntaxHighlighted = completeSyntaxHighlighted
            && formatAtDocumentPosition(position).hasProperty(QTextFormat::BackgroundBrush)
            && background.alpha() == 55;
    }
    const QTextCharFormat hoveredOpeningBracket = formatAtDocumentPosition(externalLinkStart);
    const QTextCharFormat hoveredLinkText = formatAtDocumentPosition(hoverPosition);
    const bool externalLinkHoverStyle = externalLinkRecognized && entireSyntaxHitTest
        && completeSyntaxHighlighted
        && hoveredOpeningBracket.foreground().color() == QColor(QStringLiteral("#999999"))
        && hoveredLinkText.foreground().color() == QColor(QStringLiteral("#85c7c0"));
    const bool invalidTargetRejected = linkHighlighter.externalLinkAt(
        externalLink.indexOf(QStringLiteral("前缀"))).isEmpty();
    const QString mailtoLink = QStringLiteral("[邮件](mailto:user@example.invalid)");
    linkDocument.setPlainText(mailtoLink);
    linkHighlighter.rehighlight();
    const bool nonWebSchemeRejected = linkHighlighter.externalLinkAt(
        mailtoLink.indexOf(QStringLiteral("mailto"))).isEmpty();
    const QString inlineCodeLink = QStringLiteral("`[代码](https://example.invalid)`");
    linkDocument.setPlainText(inlineCodeLink);
    linkHighlighter.rehighlight();
    const bool inlineCodeLinkRejected = linkHighlighter.externalLinkAt(
        inlineCodeLink.indexOf(QStringLiteral("https"))).isEmpty();
    const QString fencedCodeLink = QStringLiteral(
        "```md\n[代码](https://example.invalid)\n```");
    linkDocument.setPlainText(fencedCodeLink);
    linkHighlighter.rehighlight();
    const bool fencedCodeLinkRejected = linkHighlighter.externalLinkAt(
        fencedCodeLink.indexOf(QStringLiteral("https"))).isEmpty();
    const QString editableFence = QStringLiteral("~~~md\nbody\n~~~\n[link](https://example.invalid)");
    linkDocument.setPlainText(editableFence);
    linkHighlighter.rehighlight();
    const int linkOffset = editableFence.indexOf(QStringLiteral("https"));
    const bool initiallyLinked = !linkHighlighter.externalLinkAt(linkOffset).isEmpty();
    QTextCursor opener(&linkDocument);
    opener.insertText(QStringLiteral("~"));
    linkHighlighter.rehighlightBlock(linkDocument.firstBlock());
    const bool linkedInside = !linkHighlighter.externalLinkAt(linkOffset + 1).isEmpty();
    opener.deletePreviousChar();
    linkHighlighter.rehighlightBlock(linkDocument.firstBlock());
    const bool linkedAgain = !linkHighlighter.externalLinkAt(linkOffset).isEmpty();
    const QJsonArray actualLinkStates{initiallyLinked, linkedInside, linkedAgain};
    const QJsonArray expectedLinkStates{true, false, true};
    addCheck(checks, details, QStringLiteral("editedFenceLengthUpdatesLinkHitTesting"),
             actualLinkStates == expectedLinkStates,
             QJsonObject{{"actual", actualLinkStates}, {"expected", expectedLinkStates}});
    addCheck(checks, details, QStringLiteral("externalMarkdownLinkHoverStyle"),
             externalLinkHoverStyle && invalidTargetRejected && nonWebSchemeRejected
                 && inlineCodeLinkRejected && fencedCodeLinkRejected,
             QJsonObject{{QStringLiteral("externalLinkRecognized"), externalLinkRecognized},
                         {QStringLiteral("completeSyntaxHighlighted"),
                          completeSyntaxHighlighted},
                         {QStringLiteral("entireSyntaxHitTest"), entireSyntaxHitTest},
                         {QStringLiteral("openingBracketForeground"),
                          hoveredOpeningBracket.foreground().color().name()},
                         {QStringLiteral("linkTextForeground"),
                          hoveredLinkText.foreground().color().name()},
                         {QStringLiteral("invalidTargetRejected"), invalidTargetRejected},
                         {QStringLiteral("nonWebSchemeRejected"), nonWebSchemeRejected},
                         {QStringLiteral("inlineCodeLinkRejected"), inlineCodeLinkRejected},
                         {QStringLiteral("fencedCodeLinkRejected"), fencedCodeLinkRejected}});

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
}

} // namespace EditingTests
