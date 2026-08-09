#include "markdownhighlighter.h"
#include "markdownstyle.h"

#include <QFont>
#include <QHash>
#include <QRegularExpression>
#include <QTextDocument>
#include <QVector>

namespace {

enum EmphasisFlag : quint8 {
    NoEmphasis = 0,
    BoldEmphasis = 1,
    ItalicEmphasis = 2,
};

struct InlineSpan {
    int start = 0;
    int end = 0;
};

struct EmphasisSpan : InlineSpan {
    quint8 flag = NoEmphasis;
};

struct DelimiterRun {
    int start = 0;
    int length = 0;
    int remaining = 0;
    QChar marker;
    bool canOpen = false;
    bool canClose = false;
};

bool isEscaped(const QString &text, int position)
{
    int backslashes = 0;
    for (int index = position - 1;
         index >= 0 && text.at(index) == QLatin1Char('\\'); --index) {
        ++backslashes;
    }
    return (backslashes % 2) != 0;
}

QVector<InlineSpan> findCodeSpans(const QString &text)
{
    struct BacktickRun {
        int start = 0;
        int length = 0;
        int nextSameLength = -1;
    };

    QVector<BacktickRun> runs;
    for (int index = 0; index < text.size();) {
        if (text.at(index) != QLatin1Char('`') || isEscaped(text, index)) {
            ++index;
            continue;
        }
        const int start = index;
        while (index < text.size() && text.at(index) == QLatin1Char('`')) {
            ++index;
        }
        runs.append({start, index - start, -1});
    }

    QHash<int, int> nextRunByLength;
    for (int index = runs.size() - 1; index >= 0; --index) {
        BacktickRun &run = runs[index];
        run.nextSameLength = nextRunByLength.value(run.length, -1);
        nextRunByLength.insert(run.length, index);
    }

    QVector<InlineSpan> spans;
    for (int index = 0; index < runs.size();) {
        const BacktickRun &opening = runs.at(index);
        if (opening.nextSameLength < 0) {
            ++index;
            continue;
        }
        const int closingIndex = opening.nextSameLength;
        const BacktickRun &closing = runs.at(closingIndex);
        spans.append({opening.start, closing.start + closing.length});
        index = closingIndex + 1;
    }
    return spans;
}

char32_t codePointAt(const QString &text, int position)
{
    const QChar character = text.at(position);
    if (character.isLowSurrogate() && position > 0
        && text.at(position - 1).isHighSurrogate()) {
        return QChar::surrogateToUcs4(text.at(position - 1).unicode(),
                                      character.unicode());
    }
    if (character.isHighSurrogate() && position + 1 < text.size()
        && text.at(position + 1).isLowSurrogate()) {
        return QChar::surrogateToUcs4(character.unicode(),
                                      text.at(position + 1).unicode());
    }
    return character.unicode();
}

bool isWhitespaceAt(const QString &text, int position)
{
    return position < 0 || position >= text.size()
        || QChar::isSpace(codePointAt(text, position));
}

bool isPunctuationAt(const QString &text, int position)
{
    if (position < 0 || position >= text.size()) {
        return false;
    }
    const char32_t character = codePointAt(text, position);
    return QChar::isPunct(character) || QChar::isSymbol(character);
}

bool violatesRuleOfThree(const DelimiterRun &opening, const DelimiterRun &closing)
{
    if (!opening.canClose && !closing.canOpen) {
        return false;
    }
    return (opening.remaining + closing.remaining) % 3 == 0
        && (opening.remaining % 3 != 0 || closing.remaining % 3 != 0);
}

QVector<EmphasisSpan> findEmphasisSpans(const QString &text,
                                        const QVector<quint8> &opaque)
{
    QVector<DelimiterRun> delimiters;
    for (int index = 0; index < text.size();) {
        const QChar marker = text.at(index);
        if ((marker != QLatin1Char('*') && marker != QLatin1Char('_'))
            || opaque.at(index) || isEscaped(text, index)) {
            ++index;
            continue;
        }

        const int start = index;
        while (index < text.size() && text.at(index) == marker
               && !opaque.at(index)) {
            ++index;
        }
        const int length = index - start;
        const bool previousWhitespace = isWhitespaceAt(text, start - 1);
        const bool nextWhitespace = isWhitespaceAt(text, index);
        const bool previousPunctuation = isPunctuationAt(text, start - 1);
        const bool nextPunctuation = isPunctuationAt(text, index);
        const bool leftFlanking = !nextWhitespace
            && (!nextPunctuation || previousWhitespace || previousPunctuation);
        const bool rightFlanking = !previousWhitespace
            && (!previousPunctuation || nextWhitespace || nextPunctuation);

        bool canOpen = leftFlanking;
        bool canClose = rightFlanking;
        if (marker == QLatin1Char('_')) {
            canOpen = leftFlanking && (!rightFlanking || previousPunctuation);
            canClose = rightFlanking && (!leftFlanking || nextPunctuation);
        }
        delimiters.append({start, length, length, marker, canOpen, canClose});
    }

    QVector<EmphasisSpan> spans;
    QVector<int> activeOpeners;
    int openersBottom[2][2][3]{};
    for (auto &markerBottom : openersBottom) {
        for (auto &openBottom : markerBottom) {
            for (int &bottom : openBottom) {
                bottom = -1;
            }
        }
    }

    const auto markerIndex = [](QChar marker) {
        return marker == QLatin1Char('*') ? 0 : 1;
    };
    const auto resetBottoms = [&openersBottom](int marker) {
        for (auto &openBottom : openersBottom[marker]) {
            for (int &bottom : openBottom) {
                bottom = -1;
            }
        }
    };

    for (int delimiterIndex = 0; delimiterIndex < delimiters.size(); ++delimiterIndex) {
        DelimiterRun &closing = delimiters[delimiterIndex];
        while (closing.canClose && closing.remaining > 0) {
            while (!activeOpeners.isEmpty()
                   && delimiters.at(activeOpeners.constLast()).remaining == 0) {
                activeOpeners.removeLast();
            }

            const int marker = markerIndex(closing.marker);
            const int openClass = closing.canOpen ? 1 : 0;
            const int modulo = closing.remaining % 3;
            const int bottom = openersBottom[marker][openClass][modulo];
            int activePosition = -1;
            for (int position = activeOpeners.size() - 1; position >= 0; --position) {
                const int openingIndex = activeOpeners.at(position);
                if (openingIndex < bottom) {
                    break;
                }
                const DelimiterRun &opening = delimiters.at(openingIndex);
                if (opening.remaining > 0 && opening.marker == closing.marker
                    && !violatesRuleOfThree(opening, closing)) {
                    activePosition = position;
                    break;
                }
            }

            if (activePosition < 0) {
                openersBottom[marker][openClass][modulo] = delimiterIndex;
                break;
            }

            while (activeOpeners.size() - 1 > activePosition) {
                delimiters[activeOpeners.constLast()].remaining = 0;
                activeOpeners.removeLast();
            }

            DelimiterRun &opening = delimiters[activeOpeners.constLast()];
            const int useCount = opening.remaining >= 2 && closing.remaining >= 2 ? 2 : 1;
            const int openingStart = opening.start + opening.remaining - useCount;
            const int closingStart = closing.start + closing.length - closing.remaining;
            spans.append({openingStart, closingStart + useCount,
                          useCount == 2 ? BoldEmphasis : ItalicEmphasis});
            opening.remaining -= useCount;
            closing.remaining -= useCount;
            resetBottoms(marker);
            if (opening.remaining == 0) {
                activeOpeners.removeLast();
            }
        }

        if (closing.canOpen && closing.remaining > 0) {
            activeOpeners.append(delimiterIndex);
        }
    }
    return spans;
}

QVector<quint8> emphasisFlags(int textLength, const QVector<EmphasisSpan> &spans)
{
    QVector<int> boldDelta(textLength + 1);
    QVector<int> italicDelta(textLength + 1);
    for (const EmphasisSpan &span : spans) {
        QVector<int> &delta = span.flag == BoldEmphasis ? boldDelta : italicDelta;
        ++delta[span.start];
        --delta[span.end];
    }

    QVector<quint8> flags(textLength);
    int boldDepth = 0;
    int italicDepth = 0;
    for (int index = 0; index < textLength; ++index) {
        boldDepth += boldDelta.at(index);
        italicDepth += italicDelta.at(index);
        flags[index] = (boldDepth > 0 ? BoldEmphasis : NoEmphasis)
            | (italicDepth > 0 ? ItalicEmphasis : NoEmphasis);
    }
    return flags;
}

} // namespace

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
    static const QRegularExpression strikethrough(
        QStringLiteral(R"(~~(?=\S)(.+?\S)~~)"));
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

    const QVector<InlineSpan> codeSpans = hasBacktick ? findCodeSpans(text)
                                                       : QVector<InlineSpan>{};
    QVector<QRegularExpressionMatch> links;
    if (hasBracket) {
        QRegularExpressionMatchIterator matches = link.globalMatch(text);
        while (matches.hasNext()) {
            links.append(matches.next());
        }
    }

    QVector<quint8> opaque(text.size());
    const auto markOpaque = [&opaque](int start, int end) {
        for (int index = start; index < end; ++index) {
            opaque[index] = 1;
        }
    };
    for (const InlineSpan &span : codeSpans) {
        markOpaque(span.start, span.end);
    }
    for (const QRegularExpressionMatch &match : links) {
        markOpaque(match.capturedStart(0), match.capturedEnd(0));
    }

    const QVector<quint8> inlineFlags = (hasAsterisk || hasUnderscore)
        ? emphasisFlags(text.size(), findEmphasisSpans(text, opaque))
        : QVector<quint8>(text.size());
    for (int start = 0; start < inlineFlags.size();) {
        if (inlineFlags.at(start) == NoEmphasis) {
            ++start;
            continue;
        }
        int end = start + 1;
        while (end < inlineFlags.size() && inlineFlags.at(end) == inlineFlags.at(start)) {
            ++end;
        }
        const quint8 flags = inlineFlags.at(start);
        const QTextCharFormat &format = flags == (BoldEmphasis | ItalicEmphasis)
            ? m_boldItalicFormat
            : (flags == BoldEmphasis ? m_boldFormat : m_italicFormat);
        setFormat(start, end - start, format);
        start = end;
    }
    if (hasTilde) {
        applyMatches(strikethrough, m_strikethroughFormat);
    }
    const auto applyWithEmphasis = [this, &inlineFlags](int start, int length,
                                                        const QTextCharFormat &base) {
        const int rangeEnd = start + length;
        for (int segmentStart = start; segmentStart < rangeEnd;) {
            const quint8 flags = inlineFlags.at(segmentStart);
            int segmentEnd = segmentStart + 1;
            while (segmentEnd < rangeEnd && inlineFlags.at(segmentEnd) == flags) {
                ++segmentEnd;
            }
            QTextCharFormat format = base;
            if ((flags & BoldEmphasis) != 0) {
                format.setFontWeight(QFont::Bold);
            }
            if ((flags & ItalicEmphasis) != 0) {
                format.setFontItalic(true);
            }
            setFormat(segmentStart, segmentEnd - segmentStart, format);
            segmentStart = segmentEnd;
        }
    };
    for (const QRegularExpressionMatch &match : links) {
        applyWithEmphasis(match.capturedStart(2), match.capturedLength(2), m_linkFormat);
        applyWithEmphasis(match.capturedStart(5), match.capturedLength(5), m_linkFormat);
        for (const int capture : {1, 3, 4, 6}) {
            applyWithEmphasis(match.capturedStart(capture), match.capturedLength(capture),
                              m_linkBracketsFormat);
        }
    }
    for (const InlineSpan &span : codeSpans) {
        setFormat(span.start, span.end - span.start, m_inlineCodeFormat);
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
