#include "markdownhighlighter.h"

#include <QRegularExpression>
#include <QTextDocument>

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    setDarkTheme(true);
}

void MarkdownHighlighter::setDarkTheme(bool darkTheme)
{
    if (m_themeConfigured && m_darkTheme == darkTheme) {
        return;
    }
    m_darkTheme = darkTheme;
    m_themeConfigured = true;

    m_headingFormat = QTextCharFormat{};
    m_quoteFormat = QTextCharFormat{};
    m_listFormat = QTextCharFormat{};
    m_taskFormat = QTextCharFormat{};
    m_boldFormat = QTextCharFormat{};
    m_italicFormat = QTextCharFormat{};
    m_codeFormat = QTextCharFormat{};
    m_linkFormat = QTextCharFormat{};

    m_headingFormat.setForeground(QColor(darkTheme ? QStringLiteral("#8ab4f8")
                                                   : QStringLiteral("#005cc5")));
    m_headingFormat.setFontWeight(QFont::DemiBold);

    m_quoteFormat.setForeground(QColor(darkTheme ? QStringLiteral("#8bc6a0")
                                                 : QStringLiteral("#237804")));
    m_quoteFormat.setFontItalic(true);

    m_listFormat.setForeground(QColor(darkTheme ? QStringLiteral("#f5c26b")
                                                : QStringLiteral("#9a6700")));
    m_listFormat.setFontWeight(QFont::DemiBold);

    m_taskFormat.setForeground(QColor(darkTheme ? QStringLiteral("#c49bea")
                                                : QStringLiteral("#8250df")));
    m_taskFormat.setFontWeight(QFont::DemiBold);

    m_boldFormat.setForeground(QColor(darkTheme ? QStringLiteral("#ffffff")
                                                : QStringLiteral("#1f2328")));
    m_boldFormat.setFontWeight(QFont::Bold);

    m_italicFormat.setForeground(QColor(darkTheme ? QStringLiteral("#e5c07b")
                                                  : QStringLiteral("#953800")));
    m_italicFormat.setFontItalic(true);

    m_codeFormat.setForeground(QColor(darkTheme ? QStringLiteral("#98c379")
                                                : QStringLiteral("#116329")));
    m_codeFormat.setBackground(QColor(darkTheme ? QStringLiteral("#303030")
                                                : QStringLiteral("#f2f4f6")));
    m_codeFormat.setFontFamilies({QStringLiteral("Cascadia Mono"),
                                  QStringLiteral("Microsoft YaHei UI")});

    m_linkFormat.setForeground(QColor(darkTheme ? QStringLiteral("#61afef")
                                                : QStringLiteral("#0969da")));
    m_linkFormat.setFontUnderline(true);

    m_fenceFormat = m_codeFormat;
    m_fenceFormat.setForeground(QColor(darkTheme ? QStringLiteral("#7dcfff")
                                                 : QStringLiteral("#0550ae")));
    rehighlight();
}

void MarkdownHighlighter::highlightBlock(const QString &text)
{
    static const QRegularExpression heading(QStringLiteral(R"(^\s{0,3}#{1,6}\s+.*$)"));
    static const QRegularExpression quote(QStringLiteral(R"(^\s{0,3}>\s?.*$)"));
    static const QRegularExpression list(QStringLiteral(R"(^\s*(?:[-+*]|\d+\.)\s+)"));
    static const QRegularExpression task(QStringLiteral(R"(^\s*[-+*]\s+\[[ xX]\]\s+)"));
    static const QRegularExpression bold(QStringLiteral(R"((\*\*|__)(?=\S)(.+?\S)\1)"));
    static const QRegularExpression italic(QStringLiteral(R"((?<!\*)\*(?!\*)(?=\S)(.+?\S)\*(?!\*))"));
    static const QRegularExpression inlineCode(QStringLiteral(R"(`[^`\n]+`)"));
    static const QRegularExpression link(QStringLiteral(R"(\[[^\]\n]+\]\([^\)\n]+\))"));

    const auto applyMatches = [this, &text](const QRegularExpression &expression,
                                            const QTextCharFormat &format) {
        QRegularExpressionMatchIterator matches = expression.globalMatch(text);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            setFormat(match.capturedStart(), match.capturedLength(), format);
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

    // Fenced blocks are stateful, so handle them before the ordinary-line fast path.
    // Checking the three marker characters directly avoids running a regular expression
    // for every plain block in large documents.
    const bool isFence = firstContent + 2 < view.size()
        && view.at(firstContent) == QLatin1Char('`')
        && view.at(firstContent + 1) == QLatin1Char('`')
        && view.at(firstContent + 2) == QLatin1Char('`');
    const bool insideFence = previousBlockState() == 1;
    if (insideFence || isFence) {
        setFormat(0, text.size(), isFence ? m_fenceFormat : m_codeFormat);
        setCurrentBlockState(isFence ? (insideFence ? 0 : 1) : 1);
        return;
    }
    setCurrentBlockState(0);

    if (firstContent >= view.size()) {
        return;
    }
    const QChar first = view.at(firstContent);
    const bool blockCandidate = first == QLatin1Char('#') || first == QLatin1Char('>')
        || first == QLatin1Char('-') || first == QLatin1Char('+')
        || first == QLatin1Char('*') || first.isDigit();
    bool hasAsterisk = false;
    bool hasUnderscore = false;
    bool hasBacktick = false;
    bool hasLink = false;
    for (qsizetype index = firstContent; index < view.size(); ++index) {
        const QChar character = view.at(index);
        hasAsterisk = hasAsterisk || character == QLatin1Char('*');
        hasUnderscore = hasUnderscore || character == QLatin1Char('_');
        hasBacktick = hasBacktick || character == QLatin1Char('`');
        hasLink = hasLink || character == QLatin1Char('[');
        if (hasAsterisk && hasUnderscore && hasBacktick && hasLink) {
            break;
        }
    }
    if (!blockCandidate && !hasAsterisk && !hasUnderscore && !hasBacktick && !hasLink) {
        return;
    }

    if (first == QLatin1Char('#') && heading.match(text).hasMatch()) {
        setFormat(0, text.size(), m_headingFormat);
    } else if (first == QLatin1Char('>') && quote.match(text).hasMatch()) {
        setFormat(0, text.size(), m_quoteFormat);
    }

    if (blockCandidate) {
        applyMatches(list, m_listFormat);
        if (hasLink) {
            applyMatches(task, m_taskFormat);
        }
    }
    if (hasAsterisk || hasUnderscore) {
        applyMatches(bold, m_boldFormat);
    }
    if (hasAsterisk) {
        applyMatches(italic, m_italicFormat);
    }
    if (hasBacktick) {
        applyMatches(inlineCode, m_codeFormat);
    }
    if (hasLink) {
        applyMatches(link, m_linkFormat);
    }
}
