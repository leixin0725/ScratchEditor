#include "markdownhighlighter.h"
#include "markdownstyle.h"

#include <QRegularExpression>
#include <QTextDocument>

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document, const MarkdownStyle &style)
    : QSyntaxHighlighter(document)
{
    setStyle(style);
}

void MarkdownHighlighter::setStyle(const MarkdownStyle &style)
{
    for (size_t index = 0; index < m_headingFormats.size(); ++index) {
        m_headingFormats[index] = style.textFormat(style.headings[index]);
    }
    m_quoteFormat = style.textFormat(style.quote);
    m_listMarkerFormat = style.textFormat(style.listMarker);
    m_boldFormat = style.textFormat(style.bold);
    m_italicFormat = style.textFormat(style.italic);
    m_boldItalicFormat = style.textFormat(style.boldItalic);
    m_strikethroughFormat = style.textFormat(style.strikethrough);
    m_inlineCodeFormat = style.textFormat(style.inlineCode);
    m_codeBlockFormat = style.textFormat(style.codeBlock);
    m_codeFenceFormat = style.textFormat(style.codeFence);
    m_linkFormat = style.textFormat(style.link);
    m_linkBracketsFormat = style.textFormat(style.linkBrackets);
    m_completedTaskFormat = style.textFormat(style.completedTask);
    m_checkboxBracketsFormat = style.textFormat(style.checkboxBrackets);
    rehighlight();
}

void MarkdownHighlighter::highlightBlock(const QString &text)
{
    static const QRegularExpression heading(QStringLiteral(R"(^\s{0,3}(#{1,6})\s+.*$)"));
    static const QRegularExpression quote(QStringLiteral(R"(^\s{0,3}>\s?.*$)"));
    static const QRegularExpression listMarker(
        QStringLiteral(R"(^(\s*)([-+*]|\d+\.)(\s+))"));
    static const QRegularExpression checkbox(
        QStringLiteral(R"(^\s*[-+*]\s+(\[[ xX]\]))"));
    static const QRegularExpression completedTask(
        QStringLiteral(R"(^\s*[-+*]\s+\[[xX]\]\s+.*$)"));
    static const QRegularExpression bold(QStringLiteral(R"((\*\*|__)(?=\S)(.+?\S)\1)"));
    static const QRegularExpression italic(
        QStringLiteral(R"((?<![*_])([*_])(?![*_])(?=\S)(.+?\S)\1(?![*_]))"));
    static const QRegularExpression boldItalic(
        QStringLiteral(R"((\*\*\*|___)(?=\S)(.+?\S)\1)"));
    static const QRegularExpression strikethrough(
        QStringLiteral(R"(~~(?=\S)(.+?\S)~~)"));
    static const QRegularExpression inlineCode(QStringLiteral(R"(`[^`\n]+`)"));
    static const QRegularExpression link(
        QStringLiteral(R"((\[)([^\]\n]+)(\])(\()([^\)\n]+)(\)))"));

    const auto applyMatches = [this, &text](const QRegularExpression &expression,
                                            const QTextCharFormat &format,
                                            int capture = 0) {
        QRegularExpressionMatchIterator matches = expression.globalMatch(text);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            setFormat(match.capturedStart(capture), match.capturedLength(capture), format);
        }
    };

    const QStringView view(text);
    const qsizetype firstContent = [&view] {
        for (qsizetype index = 0; index < view.size(); ++index) {
            if (!view.at(index).isSpace()) {
                return index;
            }
        }
        return view.size();
    }();

    const bool isFence = firstContent + 2 < view.size()
        && view.at(firstContent) == QLatin1Char('`')
        && view.at(firstContent + 1) == QLatin1Char('`')
        && view.at(firstContent + 2) == QLatin1Char('`');
    const bool insideFence = previousBlockState() == 1;
    if (insideFence || isFence) {
        setFormat(0, text.size(), isFence ? m_codeFenceFormat : m_codeBlockFormat);
        setCurrentBlockState(isFence ? (insideFence ? 0 : 1) : 1);
        return;
    }
    setCurrentBlockState(0);

    if (firstContent >= view.size()) {
        return;
    }

    const QRegularExpressionMatch headingMatch = heading.match(text);
    if (headingMatch.hasMatch()) {
        const int level = headingMatch.capturedLength(1);
        setFormat(0, text.size(), m_headingFormats.at(static_cast<size_t>(level - 1)));
        return;
    }
    if (quote.match(text).hasMatch()) {
        setFormat(0, text.size(), m_quoteFormat);
        return;
    }

    const bool hasAsterisk = text.contains(QLatin1Char('*'));
    const bool hasUnderscore = text.contains(QLatin1Char('_'));
    const bool hasBacktick = text.contains(QLatin1Char('`'));
    const bool hasTilde = text.contains(QLatin1Char('~'));
    const bool hasBracket = text.contains(QLatin1Char('['));
    const QChar first = view.at(firstContent);
    const bool listCandidate = first == QLatin1Char('-') || first == QLatin1Char('+')
        || first == QLatin1Char('*') || first.isDigit();
    if (!listCandidate && !hasAsterisk && !hasUnderscore && !hasBacktick
        && !hasTilde && !hasBracket) {
        return;
    }

    if (hasAsterisk || hasUnderscore) {
        applyMatches(bold, m_boldFormat);
        applyMatches(italic, m_italicFormat);
        applyMatches(boldItalic, m_boldItalicFormat);
    }
    if (hasTilde) {
        applyMatches(strikethrough, m_strikethroughFormat);
    }
    if (hasBracket) {
        QRegularExpressionMatchIterator links = link.globalMatch(text);
        while (links.hasNext()) {
            const QRegularExpressionMatch match = links.next();
            setFormat(match.capturedStart(2), match.capturedLength(2), m_linkFormat);
            setFormat(match.capturedStart(5), match.capturedLength(5), m_linkFormat);
            for (const int capture : {1, 3, 4, 6}) {
                setFormat(match.capturedStart(capture), match.capturedLength(capture),
                          m_linkBracketsFormat);
            }
        }
    }
    if (hasBacktick) {
        applyMatches(inlineCode, m_inlineCodeFormat);
    }

    if (listCandidate && completedTask.match(text).hasMatch()) {
        setFormat(0, text.size(), m_completedTaskFormat);
    }
    if (listCandidate) {
        applyMatches(listMarker, m_listMarkerFormat, 2);
        if (hasBracket) {
            applyMatches(checkbox, m_checkboxBracketsFormat, 1);
        }
    }
}
