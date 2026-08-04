#include "cjktextprocessor.h"

#include <algorithm>
#include <QSet>

namespace CjkText {
namespace {

bool isBlankOrDollarLine(const QString &line)
{
    int i = 0;
    while (i < line.size() && (line.at(i) == u' ' || line.at(i) == u'\t')) {
        ++i;
    }
    if (i + 1 >= line.size() || line.at(i) != u'$' || line.at(i + 1) != u'$') {
        return false;
    }
    i += 2;
    while (i < line.size() && (line.at(i) == u' ' || line.at(i) == u'\t')) {
        ++i;
    }
    return i == line.size();
}

bool startsWithFenceOpener(const QString &line, QChar *activeChar, int *runLength)
{
    int i = 0;
    while (i < line.size() && (line.at(i) == u' ' || line.at(i) == u'\t')) {
        ++i;
    }
    if (i >= line.size()) {
        return false;
    }
    const QChar character = line.at(i);
    if (character != u'`' && character != u'~') {
        return false;
    }
    int run = 0;
    while (i + run < line.size() && line.at(i + run) == character) {
        ++run;
    }
    if (run < 3) {
        return false;
    }
    *activeChar = character;
    *runLength = run;
    return true;
}

bool isFenceCloser(const QString &line, QChar activeChar, int minimumRunLength)
{
    int i = 0;
    while (i < line.size() && (line.at(i) == u' ' || line.at(i) == u'\t')) {
        ++i;
    }
    if (i >= line.size() || line.at(i) != activeChar) {
        return false;
    }
    int run = 0;
    while (i + run < line.size() && line.at(i + run) == activeChar) {
        ++run;
    }
    if (run < minimumRunLength) {
        return false;
    }
    i += run;
    while (i < line.size() && (line.at(i) == u' ' || line.at(i) == u'\t')) {
        ++i;
    }
    return i == line.size();
}

bool isSingleLineBlockFormula(const QString &line, int *contentStart, int *contentEnd)
{
    int i = 0;
    while (i < line.size() && (line.at(i) == u' ' || line.at(i) == u'\t')) {
        ++i;
    }
    if (i + 1 >= line.size() || line.at(i) != u'$' || line.at(i + 1) != u'$') {
        return false;
    }
    const int firstDollar = i;
    const int secondDollar = line.indexOf(QStringLiteral("$$"), i + 2);
    if (secondDollar < 0) {
        return false;
    }
    int k = secondDollar + 2;
    while (k < line.size() && (line.at(k) == u' ' || line.at(k) == u'\t')) {
        ++k;
    }
    if (k != line.size()) {
        return false;
    }
    *contentStart = firstDollar + 2;
    *contentEnd = secondDollar;
    return true;
}

bool isEscapedDollar(const QString &text, int position)
{
    int backslashes = 0;
    for (int i = position - 1; i >= 0 && text.at(i) == u'\\'; --i) {
        ++backslashes;
    }
    return (backslashes % 2) == 1;
}

int findExactBacktickRun(const QString &line, int from, int runLength)
{
    int i = from;
    const int length = line.size();
    while (i < length) {
        if (line.at(i) != u'`') {
            ++i;
            continue;
        }
        const int runStart = i;
        while (i < length && line.at(i) == u'`') {
            ++i;
        }
        if (i - runStart == runLength) {
            return runStart;
        }
    }
    return -1;
}

void parseInlineSpans(const QString &line, int base, QVector<ProtectedSpan> *spans)
{
    int i = 0;
    const int length = line.size();
    while (i < length) {
        const QChar character = line.at(i);
        if (character == u'`') {
            const int runStart = i;
            while (i < length && line.at(i) == u'`') {
                ++i;
            }
            const int runLength = i - runStart;
            const int close = findExactBacktickRun(line, i, runLength);
            if (close >= 0) {
                spans->append({base + runStart, base + close + runLength,
                               base + runStart + runLength, base + close,
                               ProtectedKind::InlineCode});
                i = close + runLength;
            }
            continue;
        }
        if (character == u'$') {
            if (i + 1 < length && line.at(i + 1) == u'$') {
                i += 2;
                continue;
            }
            if (isEscapedDollar(line, i)) {
                ++i;
                continue;
            }
            const int openPosition = i;
            int closePosition = -1;
            int j = i + 1;
            while (j < length) {
                if (line.at(j) == u'$') {
                    if (j + 1 < length && line.at(j + 1) == u'$') {
                        j += 2;
                        continue;
                    }
                    if (isEscapedDollar(line, j)) {
                        ++j;
                        continue;
                    }
                    closePosition = j;
                    break;
                }
                ++j;
            }
            if (closePosition >= 0) {
                spans->append({base + openPosition, base + closePosition + 1,
                               base + openPosition + 1, base + closePosition,
                               ProtectedKind::InlineFormula});
                i = closePosition + 1;
            } else {
                ++i;
            }
            continue;
        }
        ++i;
    }
}

std::optional<ProtectedSpan> findUnclosedInlineSpan(const QString &line, int base)
{
    int i = 0;
    const int length = line.size();
    while (i < length) {
        const QChar character = line.at(i);
        if (character == u'`') {
            const int runStart = i;
            while (i < length && line.at(i) == u'`') {
                ++i;
            }
            const int runLength = i - runStart;
            const int close = findExactBacktickRun(line, i, runLength);
            if (close >= 0) {
                i = close + runLength;
                continue;
            }
            return ProtectedSpan{base + runStart, base + length,
                                 base + runStart + runLength, base + length,
                                 ProtectedKind::InlineCode};
        }
        if (character == u'$') {
            if (i + 1 < length && line.at(i + 1) == u'$') {
                i += 2;
                continue;
            }
            if (isEscapedDollar(line, i)) {
                ++i;
                continue;
            }
            const int openPosition = i;
            int closePosition = -1;
            int j = i + 1;
            while (j < length) {
                if (line.at(j) == u'$') {
                    if (j + 1 < length && line.at(j + 1) == u'$') {
                        j += 2;
                        continue;
                    }
                    if (isEscapedDollar(line, j)) {
                        ++j;
                        continue;
                    }
                    closePosition = j;
                    break;
                }
                ++j;
            }
            if (closePosition >= 0) {
                i = closePosition + 1;
                continue;
            }
            return ProtectedSpan{base + openPosition, base + length,
                                 base + openPosition + 1, base + length,
                                 ProtectedKind::InlineFormula};
        }
        ++i;
    }
    return std::nullopt;
}

bool canInsertLeftOfSpan(const QString &text, int position)
{
    if (position <= 0 || position > text.size()) {
        return false;
    }
    const QChar left = text.at(position - 1);
    return left != u' ' && left != u'\n'
        && !isSoftSeparator(left)
        && (isCjk(left) || isAsciiAlnum(left));
}

bool canInsertRightOfSpan(const QString &text, int position)
{
    if (position < 0 || position >= text.size()) {
        return false;
    }
    const QChar right = text.at(position);
    return right != u' ' && right != u'\n'
        && !isSoftSeparator(right)
        && (isCjk(right) || isAsciiAlnum(right));
}

} // namespace

bool isCjk(QChar ch)
{
    const char16_t u = ch.unicode();
    // CJK Unified Ideographs
    if (u >= 0x4E00 && u <= 0x9FFF) return true;
    // CJK Extension A
    if (u >= 0x3400 && u <= 0x4DBF) return true;
    // Hiragana
    if (u >= 0x3040 && u <= 0x309F) return true;
    // Katakana + Katakana Phonetic Extensions
    if (u >= 0x30A0 && u <= 0x30FF) return true;
    if (u >= 0x31F0 && u <= 0x31FF) return true;
    // Hangul Jamo
    if (u >= 0x1100 && u <= 0x11FF) return true;
    // Hangul Compatibility Jamo
    if (u >= 0x3130 && u <= 0x318F) return true;
    // Hangul Syllables
    if (u >= 0xAC00 && u <= 0xD7AF) return true;
    return false;
}

bool isAsciiAlnum(QChar ch)
{
    return (ch >= u'A' && ch <= u'Z')
        || (ch >= u'a' && ch <= u'z')
        || (ch >= u'0' && ch <= u'9');
}

bool isSoftSeparator(QChar ch)
{
    static const QSet<char16_t> fullwidthSeps = {
        0xFF0C, 0x3002, 0x3001, 0xFF1F, 0xFF01, 0xFF1A, 0xFF1B,  // ，。、？！：；
        0x201C, 0x201D, 0x2018, 0x2019,                            // “”‘’
        0xFF08, 0xFF09, 0x3010, 0x3011, 0x300A, 0x300B,            // （）【】《》
        0x300C, 0x300D, 0x300E, 0x300F,                            // 「」『』
    };
    if (fullwidthSeps.contains(ch.unicode())) return true;

    static const QSet<char16_t> halfwidthSeps = {
        u',', u'.', u'?', u'!', u':', u';', u'"', u'\'',
        u'-', u'(', u')', u'[', u']', u'{', u'}',
    };
    return halfwidthSeps.contains(ch.unicode());
}

DocumentAnalysis analyzeDocument(const QString &text)
{
    DocumentAnalysis analysis;
    enum class State {
        Normal,
        FencedCode,
        BlockFormula,
    };

    State state = State::Normal;
    QChar activeFenceChar;
    int activeFenceLength = 0;
    int blockStart = 0;
    int blockContentStart = 0;

    int lineStart = 0;
    const int documentSize = text.size();
    while (lineStart <= documentSize) {
        int lineEnd = text.indexOf(QLatin1Char('\n'), lineStart);
        if (lineEnd < 0) {
            lineEnd = documentSize;
        }
        const QString line = text.mid(lineStart, lineEnd - lineStart);

        if (state == State::FencedCode) {
            if (isFenceCloser(line, activeFenceChar, activeFenceLength)) {
                analysis.blockSpans.append({blockStart, lineEnd, blockContentStart,
                                            lineStart, ProtectedKind::FencedCode});
                state = State::Normal;
            }
        } else if (state == State::BlockFormula) {
            if (isBlankOrDollarLine(line)) {
                analysis.blockSpans.append({blockStart, lineEnd, blockContentStart,
                                            lineStart, ProtectedKind::BlockFormula});
                state = State::Normal;
            }
        } else {
            QChar fenceChar;
            int fenceLength = 0;
            if (startsWithFenceOpener(line, &fenceChar, &fenceLength)) {
                state = State::FencedCode;
                activeFenceChar = fenceChar;
                activeFenceLength = fenceLength;
                blockStart = lineStart;
                blockContentStart = lineEnd + 1;
            } else {
                int contentStart = 0;
                int contentEnd = 0;
                if (isSingleLineBlockFormula(line, &contentStart, &contentEnd)) {
                    analysis.blockSpans.append({lineStart, lineEnd,
                                                lineStart + contentStart,
                                                lineStart + contentEnd,
                                                ProtectedKind::BlockFormula});
                } else if (isBlankOrDollarLine(line)) {
                    state = State::BlockFormula;
                    blockStart = lineStart;
                    blockContentStart = lineEnd + 1;
                } else {
                    parseInlineSpans(line, lineStart, &analysis.inlineSpans);
                    if (const auto unclosed = findUnclosedInlineSpan(line, lineStart)) {
                        analysis.unclosedInlineSpans.append(*unclosed);
                    }
                }
            }
        }

        if (lineEnd == documentSize) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    if (state == State::FencedCode) {
        analysis.blockSpans.append({blockStart, documentSize,
                                    blockContentStart, documentSize,
                                    ProtectedKind::FencedCode});
    } else if (state == State::BlockFormula) {
        analysis.blockSpans.append({blockStart, documentSize,
                                    blockContentStart, documentSize,
                                    ProtectedKind::BlockFormula});
    }
    return analysis;
}

QVector<int> collectSpacingInsertions(
    const QString &text,
    BoundaryRange allowedBoundaries,
    const DocumentAnalysis &analysis,
    bool allowPendingDelimiterSpacing)
{
    QVector<int> insertions;
    if (allowedBoundaries.first > allowedBoundaries.last) {
        return insertions;
    }
    const int first = qMax(1, allowedBoundaries.first);
    const int last = qMin(text.size() - 1, allowedBoundaries.last);
    if (first > last) {
        return insertions;
    }

    const auto &blockSpans = analysis.blockSpans;
    const auto &inlineSpans = analysis.inlineSpans;
    const auto &unclosedSpans = analysis.unclosedInlineSpans;
    int blockIndex = 0;
    int inlineIndex = 0;
    int unclosedIndex = 0;

    for (int position = first; position <= last; ++position) {
        while (blockIndex < blockSpans.size()
               && blockSpans.at(blockIndex).outerEnd < position) {
            ++blockIndex;
        }
        while (inlineIndex < inlineSpans.size()
               && inlineSpans.at(inlineIndex).outerEnd < position) {
            ++inlineIndex;
        }
        while (unclosedIndex < unclosedSpans.size()
               && unclosedSpans.at(unclosedIndex).outerEnd < position) {
            ++unclosedIndex;
        }

        bool blocked = false;
        if (blockIndex < blockSpans.size()) {
            const ProtectedSpan &span = blockSpans.at(blockIndex);
            if (span.outerStart < position && position < span.outerEnd) {
                blocked = true;
            }
        }
        if (!blocked && inlineIndex < inlineSpans.size()) {
            const ProtectedSpan &span = inlineSpans.at(inlineIndex);
            if (span.outerStart < position && position < span.outerEnd) {
                blocked = true;
            } else if (position == span.outerStart) {
                if (canInsertLeftOfSpan(text, position)) {
                    insertions.append(position);
                }
                continue;
            } else if (position == span.outerEnd) {
                if (canInsertRightOfSpan(text, position)) {
                    insertions.append(position);
                }
                continue;
            }
        }
        if (!blocked && unclosedIndex < unclosedSpans.size()) {
            const ProtectedSpan &span = unclosedSpans.at(unclosedIndex);
            if (span.outerStart < position && position < span.outerEnd) {
                blocked = true;
            } else if (position == span.outerStart) {
                // 实时输入允许在未闭合分隔符起点补空格；Alt+F 对未闭合段整体不操作。
                if (allowPendingDelimiterSpacing
                    && canInsertLeftOfSpan(text, position)) {
                    insertions.append(position);
                }
                continue;
            }
        }
        if (blocked) {
            continue;
        }

        const QChar left = text.at(position - 1);
        const QChar right = text.at(position);
        if (left == u' ' || right == u' ' || left == u'\n' || right == u'\n') {
            continue;
        }
        if (isSoftSeparator(left) || isSoftSeparator(right)) {
            continue;
        }
        if (allowPendingDelimiterSpacing) {
            // 未闭合的行内分隔符起点：反引号 run 或单个未转义 `$` 紧跟 CJK/ASCII 字母时，
            // 立即补左外侧空格，避免“中文`”或“中文$”在输入过程中缺失边界空格。
            if (right == u'`' && text.at(position - 1) != u'`') {
                if (canInsertLeftOfSpan(text, position)) {
                    insertions.append(position);
                }
                continue;
            }
            if (right == u'$'
                && (position + 1 >= text.size() || text.at(position + 1) != u'$')
                && !isEscapedDollar(text, position)) {
                if (canInsertLeftOfSpan(text, position)) {
                    insertions.append(position);
                }
                continue;
            }
        }
        if ((isCjk(left) && isAsciiAlnum(right))
            || (isAsciiAlnum(left) && isCjk(right))) {
            insertions.append(position);
        }
    }
    return insertions;
}

bool isPositionProtected(const DocumentAnalysis &analysis, int position)
{
    const auto contains = [position](const QVector<ProtectedSpan> &spans) {
        const auto it = std::lower_bound(
            spans.cbegin(), spans.cend(), position,
            [](const ProtectedSpan &span, int value) {
                return span.outerEnd <= value;
            });
        return it != spans.cend() && it->outerStart < position
            && position < it->outerEnd;
    };
    return contains(analysis.blockSpans) || contains(analysis.inlineSpans);
}

std::pair<QString, QString> resolveSelectionPair(
    const QString &opening, const QString &closing, const QString &selection)
{
    bool hasCjk = false;
    for (const QChar character : selection) {
        if (isCjk(character)) {
            hasCjk = true;
            break;
        }
    }
    if (!hasCjk) {
        return {opening, closing};
    }
    if (opening == QStringLiteral("(")) {
        return {QStringLiteral("（"), QStringLiteral("）")};
    }
    if (opening == QStringLiteral("[")) {
        return {QStringLiteral("【"), QStringLiteral("】")};
    }
    if (opening == QStringLiteral("\"")) {
        return {QStringLiteral("“"), QStringLiteral("”")};
    }
    if (opening == QStringLiteral("'")) {
        return {QStringLiteral("‘"), QStringLiteral("’")};
    }
    return {opening, closing};
}

int positionAfterInsertions(int originalPosition,
                            const QVector<int> &insertions,
                            bool includeInsertionAtPosition)
{
    int shifted = originalPosition;
    for (const int insertion : insertions) {
        if (insertion < originalPosition
            || (insertion == originalPosition && includeInsertionAtPosition)) {
            ++shifted;
        }
    }
    return shifted;
}

} // namespace CjkText
