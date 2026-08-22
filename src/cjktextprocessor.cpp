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
        const QChar closingQuote = character == u'\u201C' ? u'\u201D'
            : character == u'\u2018' ? u'\u2019' : QChar();
        if (!closingQuote.isNull()) {
            const int close = line.indexOf(closingQuote, i + 1);
            if (close >= 0) {
                spans->append({base + i, base + close + 1,
                               base + i + 1, base + close,
                               ProtectedKind::InlineQuote});
                i = close + 1;
            } else {
                ++i;
            }
            continue;
        }
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

namespace {

enum class WordCharClass {
    Separator,
    LatinWord,
    CjkRun,
};

bool isSupplementaryCjkCodePoint(char32_t codePoint)
{
    // CJK 扩展 B–H 与兼容补充平面（代理对编码，不在 isCjk 的 BMP 范围内）。
    return codePoint >= 0x20000 && codePoint <= 0x323AF;
}

// 汉字表意字符（不含假名、谚文与标点）。
bool isHanCodePoint(char32_t codePoint)
{
    // CJK 扩展 A
    if (codePoint >= 0x3400 && codePoint <= 0x4DBF) return true;
    // CJK 统一表意文字
    if (codePoint >= 0x4E00 && codePoint <= 0x9FFF) return true;
    // CJK 兼容表意文字
    if (codePoint >= 0xF900 && codePoint <= 0xFAFF) return true;
    // CJK 扩展 B–H 与兼容补充平面（代理对编码）
    return isSupplementaryCjkCodePoint(codePoint);
}

// 返回 index 处字符占用的 UTF-16 code unit 数（配对的代理视为 2，其余为 1）。
int charUnitLengthAt(const QString &text, int index)
{
    const char16_t unit = text.at(index).unicode();
    if (QChar::isHighSurrogate(unit) && index + 1 < text.size()
        && QChar::isLowSurrogate(text.at(index + 1).unicode())) {
        return 2;
    }
    return 1;
}

// 返回以 endExclusive 为右边界的前一个字符占用的 UTF-16 code unit 数。
int charUnitLengthBefore(const QString &text, int endExclusive)
{
    if (endExclusive <= 0) {
        return 0;
    }
    const char16_t last = text.at(endExclusive - 1).unicode();
    if (QChar::isLowSurrogate(last) && endExclusive >= 2
        && QChar::isHighSurrogate(text.at(endExclusive - 2).unicode())) {
        return 2;
    }
    return 1;
}

// 返回 index 处字符（代理对视为一个原子单元）的词类别。
WordCharClass wordCharClassAt(const QString &text, int index)
{
    const char16_t unit = text.at(index).unicode();
    // 若 index 指向代理对的低位单元，按前一个高位单元归属，保持原子性。
    if (QChar::isLowSurrogate(unit) && index > 0
        && QChar::isHighSurrogate(text.at(index - 1).unicode())) {
        return wordCharClassAt(text, index - 1);
    }
    const int units = charUnitLengthAt(text, index);
    char32_t codePoint = unit;
    if (units == 2) {
        codePoint = QChar::surrogateToUcs4(unit, text.at(index + 1).unicode());
    }
    if (isSupplementaryCjkCodePoint(codePoint)) {
        return WordCharClass::CjkRun;
    }
    if (QChar::isHighSurrogate(unit) || QChar::isLowSurrogate(unit)) {
        // 未配对的代理（或非汉字补充平面字符）按分隔符处理。
        return WordCharClass::Separator;
    }
    const QChar ch(static_cast<char16_t>(codePoint));
    if (ch == u'_' || ch.isLetterOrNumber()) {
        return isCjk(ch) ? WordCharClass::CjkRun : WordCharClass::LatinWord;
    }
    return WordCharClass::Separator;
}

// 返回以 endExclusive 为右边界的前一个字符（代理对原子）的词类别。
WordCharClass wordCharClassBefore(const QString &text, int endExclusive)
{
    return wordCharClassAt(text,
                           endExclusive - charUnitLengthBefore(text, endExclusive));
}

// 从 start 向右跳过同一类连续字符，返回 run 的 exclusive 右边界。
int endOfWordRun(const QString &text, int start)
{
    const int size = text.size();
    const WordCharClass cls = wordCharClassAt(text, start);
    int i = start + charUnitLengthAt(text, start);
    while (i < size && wordCharClassAt(text, i) == cls) {
        i += charUnitLengthAt(text, i);
    }
    return i;
}

// 从 endExclusive 向左跳过同一类连续字符，返回 run 的 inclusive 左边界。
int startOfWordRun(const QString &text, int endExclusive)
{
    const WordCharClass cls = wordCharClassBefore(text, endExclusive);
    int i = endExclusive - charUnitLengthBefore(text, endExclusive);
    while (i > 0 && wordCharClassBefore(text, i) == cls) {
        i -= charUnitLengthBefore(text, i);
    }
    return i;
}

// 分隔符中只有空白（空格、换行、制表符等）不属于“可单独选中的标点”。
bool isSeparatorWhitespace(const QString &text, int index)
{
    const QChar ch = text.at(index);
    return !ch.isHighSurrogate() && !ch.isLowSurrogate() && ch.isSpace();
}

} // namespace

int countHanCharacters(const QString &text, int start, int end)
{
    const int size = text.size();
    start = qBound(0, start, size);
    end = end < 0 ? size : qMin(end, size);
    if (end <= start) {
        return 0;
    }
    int count = 0;
    int i = start;
    while (i < end) {
        const char16_t unit = text.at(i).unicode();
        char32_t codePoint = unit;
        int units = 1;
        if (QChar::isHighSurrogate(unit) && i + 1 < end
            && QChar::isLowSurrogate(text.at(i + 1).unicode())) {
            codePoint = QChar::surrogateToUcs4(unit, text.at(i + 1).unicode());
            units = 2;
        }
        if (isHanCodePoint(codePoint)) {
            ++count;
        }
        i += units;
    }
    return count;
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

int moveWordBoundary(const QString &text, int position, int direction)
{
    const int size = text.size();
    position = qBound(0, position, size);
    if (size == 0) {
        return 0;
    }

    if (direction > 0) {
        if (position >= size) {
            return size;
        }
        if (wordCharClassAt(text, position) == WordCharClass::Separator) {
            int i = position;
            while (i < size
                   && wordCharClassAt(text, i) == WordCharClass::Separator) {
                i += charUnitLengthAt(text, i);
            }
            return i;
        }
        return endOfWordRun(text, position);
    }

    if (position <= 0) {
        return 0;
    }
    if (wordCharClassBefore(text, position) == WordCharClass::Separator) {
        int i = position;
        while (i > 0
               && wordCharClassBefore(text, i) == WordCharClass::Separator) {
            i -= charUnitLengthBefore(text, i);
        }
        if (i <= 0) {
            return 0;
        }
        return startOfWordRun(text, i);
    }
    return startOfWordRun(text, position);
}

WordRange wordRangeAt(const QString &text, int position)
{
    const int size = text.size();
    position = qBound(0, position, size);
    if (size == 0) {
        return {0, 0};
    }

    const auto punctuationToken = [&](int index) -> WordRange {
        const char16_t unit = text.at(index).unicode();
        if (QChar::isLowSurrogate(unit) && index > 0
            && QChar::isHighSurrogate(text.at(index - 1).unicode())) {
            return {index - 1, index + 1};
        }
        if (isSeparatorWhitespace(text, index)) {
            return {index, index};
        }
        return {index, index + charUnitLengthAt(text, index)};
    };

    // 优先取 position 右侧字符所在的词；右侧是分隔符时再尝试左侧；
    // 两侧都是分隔符时，单个标点（含代理对）归入自身，空白不选中。
    if (position < size) {
        const WordCharClass cls = wordCharClassAt(text, position);
        if (cls != WordCharClass::Separator) {
            const int end = endOfWordRun(text, position);
            return {startOfWordRun(text, end), end};
        }
        if (!isSeparatorWhitespace(text, position)) {
            return punctuationToken(position);
        }
    }
    if (position > 0) {
        const int units = charUnitLengthBefore(text, position);
        const WordCharClass cls = wordCharClassBefore(text, position);
        if (cls != WordCharClass::Separator) {
            const int start = startOfWordRun(text, position);
            return {start, position};
        }
        if (!isSeparatorWhitespace(text, position - units)) {
            return {position - units, position};
        }
    }
    return {position, position};
}

WordRange wordRangeForCursor(const QString &text, int position)
{
    const int size = text.size();
    position = qBound(0, position, size);

    // 与原生“无选区按词包裹”的边界习惯一致：只有空白算“空侧”，
    // 标点不算；两侧都空时返回空范围（如 `a big ` 的结尾）。
    const bool emptyOnLeft = position == 0 || text.at(position - 1).isSpace();
    const bool emptyOnRight = position == size || text.at(position).isSpace();
    if (emptyOnLeft && emptyOnRight) {
        return {position, position};
    }

    const auto runRangeOf = [&](int charIndex) -> WordRange {
        const int end = endOfWordRun(text, charIndex);
        return {startOfWordRun(text, end), end};
    };
    const auto isWordRun = [&](const WordRange &range) {
        return range.start < range.end
            && wordCharClassAt(text, range.start) != WordCharClass::Separator;
    };

    const bool haveLeft = !emptyOnLeft;
    const bool haveRight = !emptyOnRight;
    const WordRange left = haveLeft ? runRangeOf(position - 1) : WordRange{};
    const WordRange right = haveRight ? runRangeOf(position) : WordRange{};

    if (haveLeft && haveRight) {
        if (left.start == right.start && left.end == right.end) {
            return left;
        }
        // 标点与词相邻时优先取词；两个词相邻（如 `abc今天`）时按
        // “光标位于左侧词尾”的惯例取左侧。
        const bool leftIsWord = isWordRun(left);
        const bool rightIsWord = isWordRun(right);
        if (leftIsWord && !rightIsWord) {
            return left;
        }
        if (rightIsWord && !leftIsWord) {
            return right;
        }
        return left;
    }
    if (haveLeft) {
        return left;
    }
    return right;
}

WordRange wordDeletionRange(const QString &text, int position, bool backwards)
{
    const int size = text.size();
    position = qBound(0, position, size);

    if (backwards) {
        if (position <= 0) {
            return {position, position};
        }
        if (wordCharClassBefore(text, position) != WordCharClass::Separator) {
            // 光标在词内/词尾：删除光标前的词内部分。
            return {startOfWordRun(text, position), position};
        }
        // 光标在分隔符后：分隔符段不含换行时连同前一个词一起删除
        // （与 Ctrl+Backspace 常见行为一致）；含换行时只删除分隔符段，
        // 避免把上一行的词一并删掉。
        int sepStart = position;
        while (sepStart > 0
               && wordCharClassBefore(text, sepStart) == WordCharClass::Separator) {
            sepStart -= charUnitLengthBefore(text, sepStart);
        }
        bool hasNewline = false;
        for (int i = sepStart; i < position; ++i) {
            if (text.at(i) == u'\n') {
                hasNewline = true;
                break;
            }
        }
        if (!hasNewline && sepStart > 0) {
            return {startOfWordRun(text, sepStart), position};
        }
        return {sepStart, position};
    }

    if (position >= size) {
        return {position, position};
    }
    if (wordCharClassAt(text, position) == WordCharClass::Separator) {
        // 光标在分隔符上：只删除分隔符段。
        int sepEnd = position;
        while (sepEnd < size
               && wordCharClassAt(text, sepEnd) == WordCharClass::Separator) {
            sepEnd += charUnitLengthAt(text, sepEnd);
        }
        return {position, sepEnd};
    }
    // 光标在词内/词首：删除词及其后的分隔符段（与 Ctrl+Delete 常见行为一致）。
    int end = endOfWordRun(text, position);
    while (end < size
           && wordCharClassAt(text, end) == WordCharClass::Separator) {
        end += charUnitLengthAt(text, end);
    }
    return {position, end};
}

bool spanContainsCjk(const QString &text, int start, int end)
{
    start = qMax(0, start);
    end = qMin(end, text.size());
    int i = start;
    while (i < end) {
        if (wordCharClassAt(text, i) == WordCharClass::CjkRun) {
            return true;
        }
        i += charUnitLengthAt(text, i);
    }
    return false;
}

} // namespace CjkText
