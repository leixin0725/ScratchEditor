#include "editorcommandregistry.h"
#include "appsettings.h"
#include "cjktextprocessor.h"
#include "headingfoldmanager.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeySequence>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QRegularExpression>
#include <QSet>
#include <QStyleHints>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace {
// 文档/布局瞬态落定等待：真实输入与标题折叠展开都会产生延迟到下一帧 polish
// 才落定的几何，立即读取会得到过期值；输入自动滚动与标题跳转滚动共用此约定。
constexpr int layoutSettleDelayMs = 40;

QString normalizeSelectedText(QString text)
{
    return text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
}

QTextDocument::FindFlags findFlags(bool caseSensitive, bool backwards = false)
{
    QTextDocument::FindFlags flags;
    if (caseSensitive) {
        flags |= QTextDocument::FindCaseSensitively;
    }
    if (backwards) {
        flags |= QTextDocument::FindBackward;
    }
    return flags;
}

struct LineRange {
    int start = 0;
    int end = 0;
};

LineRange lineRangeAt(const QString &text, int position)
{
    const int boundedPosition = qBound(0, position, text.size());
    // QString::lastIndexOf(..., -1) 会从文档末尾反向搜索。文档以空行开头时，
    // 位置 0 必须显式归属于首个空块，不能把首个换行误当成前置分隔符。
    const int start = boundedPosition == 0
        ? 0 : text.lastIndexOf(QLatin1Char('\n'), boundedPosition - 1) + 1;
    int end = text.indexOf(QLatin1Char('\n'), boundedPosition);
    if (end < 0) {
        end = text.size();
    }
    return {start, end};
}

struct DelimiterPair {
    QString opening;
    QString closing;
};

const QVector<DelimiterPair> &delimiterPairs()
{
    static const QVector<DelimiterPair> pairs{
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
    return pairs;
}

const DelimiterPair *pairForOpening(const QString &text)
{
    const auto &pairs = delimiterPairs();
    const auto found = std::find_if(pairs.cbegin(), pairs.cend(), [&text](const auto &pair) {
        return pair.opening == text;
    });
    return found == pairs.cend() ? nullptr : &*found;
}

bool isClosingDelimiter(const QString &text)
{
    const auto &pairs = delimiterPairs();
    return std::any_of(pairs.cbegin(), pairs.cend(), [&text](const auto &pair) {
        return pair.closing == text;
    });
}

struct QuotePair {
    QChar opening;
    QChar closing;
};

const QuotePair *quotePairFor(const QChar &character)
{
    static const QHash<char16_t, QuotePair> quotePairs{
        {u'`', {u'`', u'`'}},
        {u'"', {u'"', u'"'}},
        {u'\'', {u'\'', u'\''}},
        {u'\u201C', {u'\u201C', u'\u201D'}},  // “”
        {u'\u2018', {u'\u2018', u'\u2019'}},  // ‘’
        {u'\uFF02', {u'\uFF02', u'\uFF02'}},  // ＂
        {u'\uFF07', {u'\uFF07', u'\uFF07'}},  // ＇
    };
    const auto it = quotePairs.constFind(character.unicode());
    return it == quotePairs.cend() ? nullptr : &it.value();
}

const QuotePair *quotePairForClosing(const QChar &character)
{
    static const QHash<char16_t, QuotePair> quotePairs{
        {u'\u201D', {u'\u201C', u'\u201D'}},  // “”
        {u'\u2019', {u'\u2018', u'\u2019'}},  // ‘’
    };
    const auto it = quotePairs.constFind(character.unicode());
    return it == quotePairs.cend() ? nullptr : &it.value();
}

bool isCjkOrAsciiAlnumOrUnderscore(const QChar &character)
{
    return character == u'_' || CjkText::isCjk(character)
        || CjkText::isAsciiAlnum(character);
}

bool hasNonBlankCharacterAfter(const QString &text, int position)
{
    for (int i = position; i < text.size(); ++i) {
        const QChar character = text.at(i);
        if (character == QLatin1Char('\n')) {
            break;
        }
        if (character != QLatin1Char(' ') && character != QLatin1Char('\t')) {
            return true;
        }
    }
    return false;
}

int nextSameQuoteOnLine(const QString &text, int from, QChar quote)
{
    if (from < 0 || from >= text.size()) {
        return -1;
    }
    const int lineEnd = lineRangeAt(text, from).end;
    for (int i = from; i < lineEnd; ++i) {
        if (text.at(i) == quote) {
            return i;
        }
    }
    return -1;
}

bool isEscapedAt(const QString &text, int position, int lineStart)
{
    int backslashes = 0;
    for (int i = position - 1; i >= lineStart && text.at(i) == QLatin1Char('\\'); --i) {
        ++backslashes;
    }
    return (backslashes % 2) == 1;
}

struct MidlineQuotePlan {
    QChar opening;
    QChar closing;
    bool symmetric = false;
    bool closer = false;
    int openerPosition = -1;
};

std::optional<MidlineQuotePlan> buildMidlineQuotePlan(const QString &text,
                                                      int position,
                                                      const QChar &typed)
{
    const int lineStart = lineRangeAt(text, position).start;
    if (position <= lineStart) {
        return std::nullopt;
    }
    const QuotePair *pair = quotePairFor(typed);
    const QuotePair *closingPair = quotePairForClosing(typed);
    const bool typedExplicitCloser = !pair && closingPair;
    if (!pair) {
        pair = closingPair;
    }
    if (!pair) {
        return std::nullopt;
    }

    MidlineQuotePlan plan;
    plan.opening = pair->opening;
    plan.closing = pair->closing;
    plan.symmetric = pair->opening == pair->closing;
    if (plan.symmetric) {
        int count = 0;
        int lastPosition = -1;
        QVector<QChar> fullwidthOpeners;
        QVector<int> fullwidthOpenerPositions;
        const QChar fullwidthCloser = typed == u'"' ? u'\u201D'
            : typed == u'\'' ? u'\u2019' : QChar();
        for (int i = lineStart; i < position; ++i) {
            const QChar character = text.at(i);
            if (isEscapedAt(text, i, lineStart)) {
                continue;
            }
            if (character == typed) {
                ++count;
                lastPosition = i;
            }
            if (!fullwidthCloser.isNull()
                && (character == u'\u201C' || character == u'\u2018')) {
                fullwidthOpeners.append(character);
                fullwidthOpenerPositions.append(i);
            } else if (!fullwidthCloser.isNull()
                       && (character == u'\u201D' || character == u'\u2019')) {
                if (!fullwidthOpeners.isEmpty()
                    && ((character == u'\u201D' && fullwidthOpeners.back() == u'\u201C')
                        || (character == u'\u2019' && fullwidthOpeners.back() == u'\u2018'))) {
                    fullwidthOpeners.pop_back();
                    fullwidthOpenerPositions.pop_back();
                }
            }
        }
        const bool closesFullwidthPair = !fullwidthOpeners.isEmpty()
            && ((typed == u'"' && fullwidthOpeners.back() == u'\u201C')
                || (typed == u'\'' && fullwidthOpeners.back() == u'\u2018'));
        plan.closer = closesFullwidthPair || ((count % 2) == 1);
        plan.opening = closesFullwidthPair
            ? (typed == u'"' ? u'\u201C' : u'\u2018')
            : pair->opening;
        plan.closing = closesFullwidthPair
            ? (typed == u'"' ? u'\u201D' : u'\u2019')
            : pair->closing;
        plan.openerPosition = plan.closer
            ? (closesFullwidthPair ? fullwidthOpenerPositions.back() : lastPosition)
            : -1;
    } else {
        QVector<QuotePair> openStack;
        QVector<int> openerPositions;
        for (int i = lineStart; i < position; ++i) {
            const QChar character = text.at(i);
            if (isEscapedAt(text, i, lineStart)) {
                continue;
            }
            if (!openStack.isEmpty() && character == openStack.back().closing) {
                openStack.pop_back();
                openerPositions.pop_back();
                continue;
            }
            const QuotePair *currentPair = quotePairFor(character);
            if (!currentPair || currentPair->opening == currentPair->closing) {
                continue;
            }
            openStack.append(*currentPair);
            openerPositions.append(i);
        }
        plan.closer = !openStack.isEmpty()
            && pair->opening == openStack.back().opening;
        if (plan.closer) {
            plan.opening = openStack.back().opening;
            plan.closing = openStack.back().closing;
            plan.openerPosition = openerPositions.back();
        }
    }

    // 行尾没有后续内容时，仅允许收尾已有的未闭合引号；否则继续交给普通
    // 配对补全生成一整对。行中仍保持“只插入单个开符号”的既有语义。
    if (!plan.closer) {
        if (typedExplicitCloser) {
            return std::nullopt;
        }
        if (!hasNonBlankCharacterAfter(text, position)) {
            return std::nullopt;
        }
        const bool leftTouches = isCjkOrAsciiAlnumOrUnderscore(text.at(position - 1));
        const bool rightTouches = position < text.size()
            && text.at(position) != QLatin1Char('\n')
            && isCjkOrAsciiAlnumOrUnderscore(text.at(position));
        if (!leftTouches && !rightTouches) {
            return std::nullopt;
        }
    }
    return plan;
}

bool isCjkOrFullwidthPunctuationTrigger(const QChar &ch)
{
    // 全角 `，。：；？！）` 在标点转换规则中视同 CJK 字符。
    static const QSet<char16_t> fullwidthTriggers = {
        u'\uFF0C', // ，
        u'\u3002', // 。
        u'\uFF1A', // ：
        u'\uFF1B', // ；
        u'\uFF1F', // ？
        u'\uFF01', // ！
        u'\uFF09', // ）
    };
    return CjkText::isCjk(ch) || fullwidthTriggers.contains(ch.unicode());
}

std::optional<QChar> fullwidthPunctuationFor(const QChar &ch)
{
    // 半角 `, . : ; ? !` 对应的全角标点；供“符号加两空格转全角”使用。
    static const QHash<char16_t, QChar> halfwidthPunctuationToFull = {
        {u',', u'\uFF0C'},
        {u'.', u'\u3002'},
        {u':', u'\uFF1A'},
        {u';', u'\uFF1B'},
        {u'?', u'\uFF1F'},
        {u'!', u'\uFF01'},
    };
    const auto it = halfwidthPunctuationToFull.find(ch.unicode());
    if (it == halfwidthPunctuationToFull.end()) {
        return std::nullopt;
    }
    return it.value();
}

std::optional<std::pair<QChar, QChar>> fullwidthQuotesFor(const QChar &opening,
                                                          const QChar &closing)
{
    if (opening == u'"' && closing == u'"') {
        return std::pair<QChar, QChar>{u'\u201C', u'\u201D'};
    }
    if (opening == u'\'' && closing == u'\'') {
        return std::pair<QChar, QChar>{u'\u2018', u'\u2019'};
    }
    return std::nullopt;
}

std::optional<std::pair<QString, QString>> fullwidthPairFor(const QChar &opening,
                                                            const QChar &closing)
{
    // Tab 跳出配对时的整对全角映射；与输入时 `( [ " '` 生成全角配对的映射一致。
    if (opening == u'(' && closing == u')') {
        return std::pair<QString, QString>{QStringLiteral("（"), QStringLiteral("）")};
    }
    if (opening == u'[' && closing == u']') {
        return std::pair<QString, QString>{QStringLiteral("【"), QStringLiteral("】")};
    }
    if (opening == u'"' && closing == u'"') {
        return std::pair<QString, QString>{QStringLiteral("“"), QStringLiteral("”")};
    }
    if (opening == u'\'' && closing == u'\'') {
        return std::pair<QString, QString>{QStringLiteral("‘"), QStringLiteral("’")};
    }
    return std::nullopt;
}

bool wrapContentContainsCjk(const QString &text, int openerPosition, int closerPosition)
{
    if (openerPosition < 0 || closerPosition <= openerPosition) {
        return false;
    }
    for (int i = openerPosition + 1; i < closerPosition; ++i) {
        if (CjkText::isCjk(text.at(i))) {
            return true;
        }
    }
    return false;
}

bool convertAsciiQuoteWrap(QTextDocument *document, int openerPosition,
                           int closerPosition)
{
    if (openerPosition < 0 || closerPosition <= openerPosition) {
        return false;
    }
    const QString text = document->toPlainText();
    const QChar opening = text.at(openerPosition);
    const QChar closing = text.at(closerPosition);
    const auto fullwidth = fullwidthQuotesFor(opening, closing);
    if (!fullwidth || !wrapContentContainsCjk(text, openerPosition, closerPosition)) {
        return false;
    }
    QTextCursor cursor(document);
    cursor.setPosition(closerPosition);
    cursor.setPosition(closerPosition + 1, QTextCursor::KeepAnchor);
    cursor.insertText(QString(fullwidth->second));
    cursor.setPosition(openerPosition);
    cursor.setPosition(openerPosition + 1, QTextCursor::KeepAnchor);
    cursor.insertText(QString(fullwidth->first));
    return true;
}

bool canSpaceBacktickBoundary(const QChar &character)
{
    return character != QLatin1Char(' ') && character != QLatin1Char('\n')
        && !CjkText::isSoftSeparator(character)
        && (CjkText::isCjk(character) || CjkText::isAsciiAlnum(character));
}

int spaceBacktickPairBoundaries(QTextDocument *document, int openPosition,
                                int closePosition)
{
    if (!document || openPosition < 0 || closePosition <= openPosition) {
        return 0;
    }
    const QString text = document->toPlainText();
    if (text.at(openPosition) != QLatin1Char('`')
        || text.at(closePosition) != QLatin1Char('`')) {
        return 0;
    }
    QVector<int> insertions;
    int leftInsertions = 0;
    if (openPosition > 0 && canSpaceBacktickBoundary(text.at(openPosition - 1))) {
        insertions.append(openPosition);
        leftInsertions = 1;
    }
    const int rightPosition = closePosition + 1;
    if (rightPosition < text.size()
        && canSpaceBacktickBoundary(text.at(rightPosition))) {
        insertions.append(rightPosition);
    }
    if (insertions.isEmpty()) {
        return 0;
    }
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    for (auto it = insertions.crbegin(); it != insertions.crend(); ++it) {
        QTextCursor insertionCursor(document);
        insertionCursor.setPosition(*it);
        insertionCursor.insertText(QStringLiteral(" "));
    }
    cursor.endEditBlock();
    return leftInsertions;
}

bool isExactMarkerRun(const QString &text, int position, const QString &marker)
{
    if (position < 0 || position + marker.size() > text.size()
        || text.mid(position, marker.size()) != marker) {
        return false;
    }
    const QChar markerCharacter = marker.front();
    return (position == 0 || text.at(position - 1) != markerCharacter)
        && (position + marker.size() == text.size()
            || text.at(position + marker.size()) != markerCharacter);
}

int nextExactMarkerRun(const QString &text, const QString &marker, int from)
{
    int position = text.indexOf(marker, from);
    while (position >= 0 && !isExactMarkerRun(text, position, marker)) {
        position = text.indexOf(marker, position + 1);
    }
    return position;
}

enum class MarkupKind {
    Bold,
    Italic,
    InlineCode,
    Generic,
};

struct MarkerRun {
    int start = 0;
    int length = 0;
};

struct MarkupMarker {
    int runStart = 0;
    int runLength = 0;
    int removeStart = 0;
    int removeLength = 0;
};

struct MarkupSpan {
    MarkupMarker opening;
    MarkupMarker closing;

    int contentStart() const { return opening.runStart + opening.runLength; }
    int contentEnd() const { return closing.runStart; }
    int outerStart() const { return opening.runStart; }
    int outerEnd() const { return closing.runStart + closing.runLength; }
};

struct TextRange {
    int start = 0;
    int end = 0;

    bool isValid() const { return start < end; }
};

struct MarkdownListItem {
    bool valid = false;
    bool ordered = false;
    bool task = false;
    QString structuralPrefix;
    QString marker;
    QString numberText;
    qlonglong number = 0;
    QChar delimiter;
    QString content;
    int markerStart = -1;
    int numberStart = -1;
    int contentStart = -1;
    int indentColumns = 0;
    int quoteDepth = 0;

    bool isEmpty() const { return content.trimmed().isEmpty(); }

    QString continuationPrefix() const
    {
        const QString nextMarker = ordered
            ? QString::number(number + 1) + delimiter
            : marker;
        return structuralPrefix + nextMarker
            + (task ? QStringLiteral(" [ ] ") : QStringLiteral(" "));
    }
};

struct MarkdownQuoteLine {
    bool valid = false;
    QString prefix;
    QString content;
    int contentStart = -1;
    int deepestPrefixStart = -1;

    bool isEmpty() const { return content.trimmed().isEmpty(); }
};

MarkdownQuoteLine parseMarkdownQuoteLine(const QString &line)
{
    static const QRegularExpression quotePattern(
        QStringLiteral(R"(^([\t ]*(?:>[\t ]*)+)(.*)$)"));
    const QRegularExpressionMatch match = quotePattern.match(line);
    if (!match.hasMatch()) {
        return {};
    }

    MarkdownQuoteLine quote;
    quote.valid = true;
    quote.prefix = match.captured(1);
    quote.content = match.captured(2);
    quote.contentStart = match.capturedStart(2);
    quote.deepestPrefixStart = quote.prefix.lastIndexOf(QLatin1Char('>'));
    return quote;
}

int indentationColumns(const QString &prefix)
{
    int columns = 0;
    for (const QChar character : prefix) {
        if (character == QLatin1Char('>')) {
            break;
        }
        if (character == QLatin1Char('\t')) {
            columns += 4 - (columns % 4);
        } else if (character == QLatin1Char(' ')) {
            ++columns;
        }
    }
    return columns;
}

MarkdownListItem parseMarkdownListItem(const QString &line)
{
    static const QRegularExpression listPattern(QStringLiteral(
        R"(^([\t ]*(?:>[\t ]*)*)([-+*]|(\d+)([.)]))[\t ]+(?:\[([ xX])\][\t ]+)?(.*)$)"));
    const QRegularExpressionMatch match = listPattern.match(line);
    if (!match.hasMatch()) {
        return {};
    }

    MarkdownListItem item;
    item.valid = true;
    item.structuralPrefix = match.captured(1);
    item.marker = match.captured(2);
    item.ordered = match.capturedStart(3) >= 0;
    item.task = match.capturedStart(5) >= 0;
    item.content = match.captured(6);
    item.markerStart = match.capturedStart(2);
    item.numberStart = match.capturedStart(3);
    item.contentStart = match.capturedStart(6);
    item.indentColumns = indentationColumns(item.structuralPrefix);
    item.quoteDepth = item.structuralPrefix.count(QLatin1Char('>'));
    if (item.ordered) {
        bool numberOk = false;
        item.numberText = match.captured(3);
        item.number = item.numberText.toLongLong(&numberOk);
        if (!numberOk || item.number == std::numeric_limits<qlonglong>::max()) {
            return {};
        }
        item.delimiter = match.captured(4).front();
    }
    return item;
}

struct TextReplacement {
    int start = 0;
    int length = 0;
    QString replacement;
};

struct ParsedListLine {
    int start = 0;
    int end = 0;
    MarkdownListItem item;
};

struct OrderedListSequence {
    QString signature;
    qlonglong startNumber = 0;
    QVector<int> lineIndexes;
};

QString orderedListSignature(const MarkdownListItem &item)
{
    return QStringLiteral("%1:%2:%3")
        .arg(item.indentColumns)
        .arg(item.quoteDepth)
        .arg(static_cast<int>(item.delimiter.unicode()));
}

struct AffectedListArea {
    QVector<ParsedListLine> lines;
    QVector<std::pair<int, int>> regions;
};

AffectedListArea affectedListArea(const QString &text, int changeStart, int changeEnd)
{
    AffectedListArea area;
    if (text.isEmpty()) {
        return area;
    }

    const int boundedStart = qBound(0, qMin(changeStart, changeEnd), text.size());
    const int boundedEnd = qBound(boundedStart, qMax(changeStart, changeEnd), text.size());
    const LineRange firstRange = lineRangeAt(text, boundedStart);
    const LineRange lastRange = lineRangeAt(text, boundedEnd);
    QVector<int> seeds;
    if (firstRange.start > 0) {
        seeds.append(lineRangeAt(text, firstRange.start - 1).start);
    }
    int seedStart = firstRange.start;
    while (seedStart <= lastRange.start) {
        seeds.append(seedStart);
        const LineRange seedRange = lineRangeAt(text, seedStart);
        if (seedRange.end >= text.size()) {
            break;
        }
        seedStart = seedRange.end + 1;
    }
    if (lastRange.end < text.size()) {
        seeds.append(lastRange.end + 1);
    }

    QSet<int> collectedStarts;
    const auto collectDirection = [&](int initialStart, bool backwards) {
        int lineStart = initialStart;
        while (lineStart >= 0 && lineStart <= text.size()) {
            const LineRange range = lineRangeAt(text, lineStart);
            const MarkdownListItem item = parseMarkdownListItem(
                text.mid(range.start, range.end - range.start));
            if (!item.valid) {
                break;
            }
            if (!collectedStarts.contains(range.start)) {
                collectedStarts.insert(range.start);
                area.lines.append({range.start, range.end, item});
            }
            if (backwards) {
                if (range.start == 0) {
                    break;
                }
                lineStart = lineRangeAt(text, range.start - 1).start;
            } else {
                if (range.end >= text.size()) {
                    break;
                }
                lineStart = range.end + 1;
            }
        }
    };

    for (const int seed : seeds) {
        const LineRange range = lineRangeAt(text, seed);
        if (collectedStarts.contains(range.start)) {
            continue;
        }
        const MarkdownListItem item = parseMarkdownListItem(
            text.mid(range.start, range.end - range.start));
        if (!item.valid) {
            continue;
        }
        collectDirection(range.start, true);
        collectDirection(range.start, false);
    }

    std::sort(area.lines.begin(), area.lines.end(),
              [](const ParsedListLine &left, const ParsedListLine &right) {
                  return left.start < right.start;
              });
    for (int index = 0; index < area.lines.size(); ++index) {
        const bool startsRegion = index == 0
            || area.lines.at(index).start != area.lines.at(index - 1).end + 1;
        if (startsRegion) {
            area.regions.append({index, index});
        } else {
            area.regions.last().second = index;
        }
    }
    return area;
}

QVector<OrderedListSequence> orderedSequences(
    const QVector<ParsedListLine> &lines,
    const QVector<std::pair<int, int>> &regions)
{
    QVector<OrderedListSequence> sequences;
    for (const auto &[regionStart, regionEnd] : regions) {
        for (int index = regionStart; index <= regionEnd; ++index) {
            const MarkdownListItem &root = lines.at(index).item;
            if (!root.ordered) {
                continue;
            }

            bool hasPreviousSibling = false;
            for (int previous = index - 1; previous >= regionStart; --previous) {
                const MarkdownListItem &candidate = lines.at(previous).item;
                const bool deeperLevel = candidate.indentColumns > root.indentColumns
                    && candidate.quoteDepth == root.quoteDepth;
                if (deeperLevel) {
                    continue;
                }
                hasPreviousSibling = candidate.indentColumns == root.indentColumns
                    && candidate.quoteDepth == root.quoteDepth && candidate.ordered
                    && candidate.delimiter == root.delimiter;
                break;
            }
            if (hasPreviousSibling) {
                continue;
            }

            OrderedListSequence sequence;
            sequence.signature = orderedListSignature(root);
            sequence.startNumber = root.number;
            sequence.lineIndexes.append(index);
            for (int following = index + 1; following <= regionEnd; ++following) {
                const MarkdownListItem &candidate = lines.at(following).item;
                const bool deeperLevel = candidate.indentColumns > root.indentColumns
                    && candidate.quoteDepth == root.quoteDepth;
                if (deeperLevel) {
                    continue;
                }
                const bool sameSequence = candidate.indentColumns == root.indentColumns
                    && candidate.quoteDepth == root.quoteDepth && candidate.ordered
                    && candidate.delimiter == root.delimiter;
                if (!sameSequence) {
                    break;
                }
                sequence.lineIndexes.append(following);
            }
            sequences.append(std::move(sequence));
        }
    }
    return sequences;
}

QString repairedAffectedOrderedLists(QTextDocument *document, const QString &beforeText,
                                     const QString &afterText, bool preservePreviousStart)
{
    if (!document) {
        return afterText;
    }
    if (beforeText == afterText) {
        return afterText;
    }

    int commonPrefix = 0;
    const int sharedLength = qMin(beforeText.size(), afterText.size());
    while (commonPrefix < sharedLength
           && beforeText.at(commonPrefix) == afterText.at(commonPrefix)) {
        ++commonPrefix;
    }
    int commonSuffix = 0;
    while (commonSuffix < sharedLength - commonPrefix
           && beforeText.at(beforeText.size() - 1 - commonSuffix)
               == afterText.at(afterText.size() - 1 - commonSuffix)) {
        ++commonSuffix;
    }

    const int beforeChangeEnd = beforeText.size() - commonSuffix;
    const int afterChangeEnd = afterText.size() - commonSuffix;
    const AffectedListArea beforeArea = affectedListArea(
        beforeText, commonPrefix, beforeChangeEnd);
    const AffectedListArea afterArea = affectedListArea(
        afterText, commonPrefix, afterChangeEnd);
    const QVector<OrderedListSequence> beforeSequences = orderedSequences(
        beforeArea.lines, beforeArea.regions);
    const QVector<OrderedListSequence> afterSequences = orderedSequences(
        afterArea.lines, afterArea.regions);

    QHash<QString, QVector<qlonglong>> previousStarts;
    if (preservePreviousStart) {
        for (const OrderedListSequence &sequence : beforeSequences) {
            previousStarts[sequence.signature].append(sequence.startNumber);
        }
    }
    QHash<QString, int> usedStarts;
    QVector<TextReplacement> replacements;
    for (const OrderedListSequence &sequence : afterSequences) {
        qlonglong expectedNumber = sequence.startNumber;
        const QVector<qlonglong> candidates = previousStarts.value(sequence.signature);
        const int candidateIndex = usedStarts.value(sequence.signature);
        if (candidateIndex < candidates.size()) {
            expectedNumber = candidates.at(candidateIndex);
            usedStarts[sequence.signature] = candidateIndex + 1;
        }
        for (const int lineIndex : sequence.lineIndexes) {
            const ParsedListLine &line = afterArea.lines.at(lineIndex);
            const QString expectedText = QString::number(expectedNumber);
            if (line.item.numberText != expectedText) {
                replacements.append({line.start + line.item.numberStart,
                                     static_cast<int>(line.item.numberText.size()),
                                     expectedText});
            }
            if (expectedNumber == std::numeric_limits<qlonglong>::max()) {
                break;
            }
            ++expectedNumber;
        }
    }

    QString repairedText = afterText;
    for (auto replacement = replacements.crbegin(); replacement != replacements.crend();
         ++replacement) {
        QTextCursor cursor(document);
        cursor.setPosition(replacement->start);
        cursor.setPosition(replacement->start + replacement->length,
                           QTextCursor::KeepAnchor);
        cursor.insertText(replacement->replacement);
        repairedText.replace(replacement->start, replacement->length,
                             replacement->replacement);
    }
    return repairedText;
}

struct OrderedListConversionLine {
    int start = 0;
    int end = 0;
    int previousSameLevel = -1;
    int nextSameLevel = -1;
    MarkdownListItem item;
    bool selected = false;
    bool fenced = false;
};

struct OrderedListConversionPlan {
    QVector<TextReplacement> replacements;
    int selectionStart = -1;
    int selectionEnd = -1;
    bool handled = false;
};

OrderedListConversionPlan orderedListConversionPlan(const QString &text,
                                                    int selectionStart,
                                                    int selectionEnd)
{
    OrderedListConversionPlan plan;
    if (selectionStart < 0 || selectionEnd < selectionStart
        || selectionEnd > text.size()) {
        return plan;
    }

    const bool hasSelection = selectionStart != selectionEnd;
    const LineRange firstSelectedLine = lineRangeAt(text, selectionStart);
    const LineRange lastSelectedLine = lineRangeAt(
        text, hasSelection ? selectionEnd - 1 : selectionEnd);
    const CjkText::DocumentAnalysis analysis = CjkText::analyzeDocument(text);

    QVector<OrderedListConversionLine> lines;
    int blockIndex = 0;
    int lineStart = 0;
    while (lineStart <= text.size()) {
        const LineRange range = lineRangeAt(text, lineStart);
        while (blockIndex < analysis.blockSpans.size()
               && analysis.blockSpans.at(blockIndex).outerEnd < range.start) {
            ++blockIndex;
        }
        bool fenced = false;
        if (blockIndex < analysis.blockSpans.size()) {
            const CjkText::ProtectedSpan &span = analysis.blockSpans.at(blockIndex);
            fenced = span.kind == CjkText::ProtectedKind::FencedCode
                && span.outerStart <= range.end && range.start <= span.outerEnd;
        }
        const bool selected = range.start >= firstSelectedLine.start
            && range.start <= lastSelectedLine.start;
        lines.append({range.start, range.end, -1, -1,
                      parseMarkdownListItem(
                          text.mid(range.start, range.end - range.start)),
                      selected, fenced});
        if (range.end >= text.size()) {
            break;
        }
        lineStart = range.end + 1;
    }

    // 每个活动层级只保留最近的同层列表项。进入更深层时父级保持活动，
    // 回到浅层时弹出子级，因此每行只会入栈、出栈各一次。
    QVector<std::pair<int, int>> activeLevels;
    int activeQuoteDepth = -1;
    for (int index = 0; index < lines.size(); ++index) {
        OrderedListConversionLine &line = lines[index];
        if (line.fenced || !line.item.valid || line.item.task) {
            activeLevels.clear();
            activeQuoteDepth = -1;
            continue;
        }
        if (activeQuoteDepth >= 0 && activeQuoteDepth != line.item.quoteDepth) {
            activeLevels.clear();
        }
        activeQuoteDepth = line.item.quoteDepth;
        while (!activeLevels.isEmpty()
               && activeLevels.back().first > line.item.indentColumns) {
            activeLevels.pop_back();
        }
        if (!activeLevels.isEmpty()
            && activeLevels.back().first == line.item.indentColumns) {
            line.previousSameLevel = activeLevels.back().second;
            lines[line.previousSameLevel].nextSameLevel = index;
            activeLevels.back().second = index;
        } else {
            activeLevels.append({line.item.indentColumns, index});
        }
    }

    const auto includeGroup = [&plan, &lines](const QVector<int> &chain,
                                              int first, int last,
                                              qlonglong startNumber,
                                              QChar delimiter) {
        for (int position = first; position <= last; ++position) {
            const OrderedListConversionLine &line = lines.at(chain.at(position));
            plan.selectionStart = plan.selectionStart < 0
                ? line.start : qMin(plan.selectionStart, line.start);
            plan.selectionEnd = qMax(plan.selectionEnd, line.end);
        }

        qlonglong number = startNumber;
        for (int position = first; position <= last; ++position) {
            const OrderedListConversionLine &line = lines.at(chain.at(position));
            const QString marker = QString::number(number) + delimiter;
            if (line.item.marker != marker) {
                plan.replacements.append({line.start + line.item.markerStart,
                                          static_cast<int>(line.item.marker.size()),
                                          marker});
            }
            if (number == std::numeric_limits<qlonglong>::max()) {
                break;
            }
            ++number;
        }
    };

    for (int head = 0; head < lines.size(); ++head) {
        const OrderedListConversionLine &headLine = lines.at(head);
        if (headLine.fenced || !headLine.item.valid || headLine.item.task
            || headLine.previousSameLevel >= 0) {
            continue;
        }

        QVector<int> chain;
        for (int index = head; index >= 0; index = lines.at(index).nextSameLevel) {
            chain.append(index);
        }
        int segmentStart = 0;
        while (segmentStart < chain.size()) {
            const OrderedListConversionLine &firstLine = lines.at(chain.at(segmentStart));
            if (!firstLine.item.ordered && !firstLine.selected) {
                ++segmentStart;
                continue;
            }
            int segmentEnd = segmentStart;
            while (segmentEnd + 1 < chain.size()) {
                const OrderedListConversionLine &next = lines.at(chain.at(segmentEnd + 1));
                if (!next.item.ordered && !next.selected) {
                    break;
                }
                ++segmentEnd;
            }

            bool selectedOrdered = false;
            bool selectedUnordered = false;
            for (int position = segmentStart; position <= segmentEnd; ++position) {
                const OrderedListConversionLine &line = lines.at(chain.at(position));
                selectedOrdered = selectedOrdered
                    || (line.selected && line.item.ordered);
                selectedUnordered = selectedUnordered
                    || (line.selected && !line.item.ordered);
            }
            plan.handled = plan.handled || selectedOrdered || selectedUnordered;

            if (selectedOrdered && selectedUnordered) {
                int anchor = segmentStart;
                while (anchor <= segmentEnd
                       && !lines.at(chain.at(anchor)).item.ordered) {
                    ++anchor;
                }
                const MarkdownListItem &anchorItem = lines.at(chain.at(anchor)).item;
                includeGroup(chain, segmentStart, segmentEnd,
                             anchorItem.number, anchorItem.delimiter);
            } else if (selectedUnordered) {
                int position = segmentStart;
                while (position <= segmentEnd) {
                    if (lines.at(chain.at(position)).item.ordered) {
                        ++position;
                        continue;
                    }
                    const int first = position;
                    while (position + 1 <= segmentEnd
                           && !lines.at(chain.at(position + 1)).item.ordered) {
                        ++position;
                    }
                    includeGroup(chain, first, position, 1, QLatin1Char('.'));
                    ++position;
                }
            } else if (selectedOrdered) {
                int first = segmentStart;
                while (first <= segmentEnd) {
                    const QChar delimiter = lines.at(chain.at(first)).item.delimiter;
                    int last = first;
                    bool groupSelected = lines.at(chain.at(first)).selected;
                    while (last + 1 <= segmentEnd
                           && lines.at(chain.at(last + 1)).item.delimiter == delimiter) {
                        ++last;
                        groupSelected = groupSelected || lines.at(chain.at(last)).selected;
                    }
                    if (groupSelected) {
                        includeGroup(chain, first, last,
                                     lines.at(chain.at(first)).item.number, delimiter);
                    }
                    first = last + 1;
                }
            }
            segmentStart = segmentEnd + 1;
        }
    }

    return plan;
}

bool orderedListStructureAffected(const QString &text, int start, int end)
{
    const int rangeStart = qBound(0, qMin(start, end), text.size());
    const int rangeEnd = qBound(rangeStart, qMax(start, end), text.size());
    int inspectStart = lineRangeAt(text, rangeStart).start;
    if (inspectStart > 0) {
        inspectStart = lineRangeAt(text, inspectStart - 1).start;
    }
    int inspectEnd = lineRangeAt(text, rangeEnd).end;
    if (inspectEnd < text.size()) {
        inspectEnd = lineRangeAt(text, inspectEnd + 1).end;
    }
    bool orderedListNearby = false;
    int lineStart = inspectStart;
    while (lineStart <= inspectEnd) {
        const LineRange range = lineRangeAt(text, lineStart);
        const ParsedListLine line{
            range.start, range.end,
            parseMarkdownListItem(text.mid(range.start, range.end - range.start))};
        orderedListNearby = orderedListNearby || line.item.ordered;
        if (line.item.ordered) {
            const int prefixEnd = line.start + line.item.contentStart;
            if (rangeStart == rangeEnd) {
                if (rangeStart >= line.start && rangeStart < prefixEnd) {
                    return true;
                }
            } else if (rangeStart < prefixEnd && rangeEnd > line.start) {
                return true;
            }
        } else {
            // 删除编号中的单个字符后，该行会暂时不再满足完整列表正则；仍需识别
            // `. item`、`2 item`、`2.item` 这类中间状态，保证逐字修改编号可收尾。
            static const QRegularExpression partialOrderedPrefix(QStringLiteral(
                R"(^([	 ]*(?:>[	 ]*)*)(?:\d+[.)]?|[.)])[	 ]*)"));
            const QString lineText = text.mid(line.start, line.end - line.start);
            const QRegularExpressionMatch partialMatch = partialOrderedPrefix.match(lineText);
            if (partialMatch.hasMatch()) {
                const int prefixEnd = line.start + partialMatch.capturedLength();
                if ((rangeStart == rangeEnd
                     && rangeStart >= line.start && rangeStart <= prefixEnd)
                    || (rangeStart < prefixEnd && rangeEnd > line.start)) {
                    return true;
                }
            }
        }
        if (range.end >= text.size() || range.end >= inspectEnd) {
            break;
        }
        lineStart = range.end + 1;
    }
    return rangeEnd > rangeStart
        && text.mid(rangeStart, rangeEnd - rangeStart).contains(QLatin1Char('\n'))
        && orderedListNearby;
}

bool orderedListEditRequiresRepair(const QString &beforeText, const QString &afterText,
                                   int start, int beforeEnd, int afterEnd)
{
    // 裸行首数字是有序列表前缀的中间状态，但在其后输入正文后就不再是列表。
    // 只有编辑前后都命中列表结构时才短路到编号修复，避免跳过常规输入处理。
    return orderedListStructureAffected(beforeText, start, beforeEnd)
        && orderedListStructureAffected(afterText, start, afterEnd);
}

MarkupKind markupKind(const QString &opening, const QString &closing)
{
    if (opening == QStringLiteral("**") && closing == opening) {
        return MarkupKind::Bold;
    }
    if (opening == QStringLiteral("*") && closing == opening) {
        return MarkupKind::Italic;
    }
    if (opening == QStringLiteral("`") && closing == opening) {
        return MarkupKind::InlineCode;
    }
    return MarkupKind::Generic;
}

QVector<MarkerRun> markerRunsForLine(const QString &text, int lineStart, int lineEnd,
                                     MarkupKind kind)
{
    QVector<MarkerRun> runs;
    const QChar marker = kind == MarkupKind::InlineCode ? QLatin1Char('`') : QLatin1Char('*');
    for (int position = lineStart; position < lineEnd;) {
        if (text.at(position) != marker) {
            ++position;
            continue;
        }
        const int runStart = position;
        while (position < lineEnd && text.at(position) == marker) {
            ++position;
        }
        const int runLength = position - runStart;
        const auto appendRun = [&runs, runStart](int offset, int length) {
            runs.append({runStart + offset, length});
        };
        if (kind == MarkupKind::Bold) {
            if (runLength == 2 || runLength == 3) {
                appendRun(0, runLength);
            } else if (runLength == 4) {
                appendRun(0, 2);
                appendRun(2, 2);
            } else if (runLength == 6) {
                appendRun(0, 3);
                appendRun(3, 3);
            }
        } else if (kind == MarkupKind::Italic) {
            if (runLength == 1 || runLength == 3) {
                appendRun(0, runLength);
            } else if (runLength == 6) {
                appendRun(0, 3);
                appendRun(3, 3);
            }
        } else if (kind == MarkupKind::InlineCode) {
            if (runLength == 1) {
                appendRun(0, 1);
            } else if (runLength == 2) {
                appendRun(0, 1);
                appendRun(1, 1);
            }
        }
    }
    return runs;
}

MarkupMarker markerComponent(const MarkerRun &run, MarkupKind kind, bool opening)
{
    MarkupMarker marker{run.start, run.length, run.start, 0};
    if (kind == MarkupKind::Bold) {
        marker.removeLength = 2;
        if (run.length == 3 && !opening) {
            ++marker.removeStart;
        }
    } else if (kind == MarkupKind::Italic) {
        marker.removeLength = 1;
        if (run.length == 3 && opening) {
            marker.removeStart += 2;
        }
    } else if (kind == MarkupKind::InlineCode) {
        marker.removeLength = 1;
    }
    return marker;
}

QVector<MarkupSpan> inlineMarkupSpans(const QString &text, MarkupKind kind)
{
    QVector<MarkupSpan> spans;
    int lineStart = 0;
    while (lineStart <= text.size()) {
        int lineEnd = text.indexOf(QLatin1Char('\n'), lineStart);
        if (lineEnd < 0) {
            lineEnd = text.size();
        }
        const QVector<MarkerRun> runs = markerRunsForLine(text, lineStart, lineEnd, kind);
        for (int index = 0; index + 1 < runs.size(); index += 2) {
            spans.append({markerComponent(runs.at(index), kind, true),
                          markerComponent(runs.at(index + 1), kind, false)});
        }
        if (lineEnd == text.size()) {
            break;
        }
        lineStart = lineEnd + 1;
    }
    return spans;
}

QVector<MarkupSpan> genericMarkupSpans(const QString &text, const QString &opening,
                                       const QString &closing)
{
    QVector<MarkupSpan> spans;
    int searchFrom = 0;
    while (searchFrom <= text.size()) {
        const int openingStart = text.indexOf(opening, searchFrom);
        if (openingStart < 0) {
            break;
        }
        const int closingStart = text.indexOf(closing, openingStart + opening.size());
        if (closingStart < 0) {
            break;
        }
        const int openingLength = static_cast<int>(opening.size());
        const int closingLength = static_cast<int>(closing.size());
        spans.append(MarkupSpan{
            {openingStart, openingLength, openingStart, openingLength},
            {closingStart, closingLength, closingStart, closingLength}});
        searchFrom = closingStart + closing.size();
    }
    return spans;
}

std::optional<MarkupSpan> containingMarkupSpan(const QVector<MarkupSpan> &spans,
                                               int selectionStart, int selectionEnd)
{
    std::optional<MarkupSpan> result;
    for (const MarkupSpan &span : spans) {
        const bool insideContent = selectionStart >= span.contentStart()
            && selectionEnd <= span.contentEnd();
        const bool selectsWholeSpan = selectionStart == span.outerStart()
            && selectionEnd == span.outerEnd();
        if (!insideContent && !selectsWholeSpan) {
            continue;
        }
        if (!result || span.outerEnd() - span.outerStart()
                           < result->outerEnd() - result->outerStart()) {
            result = span;
        }
    }
    return result;
}

int positionAfterRemovals(int position, QVector<MarkupMarker> markers)
{
    std::sort(markers.begin(), markers.end(), [](const auto &left, const auto &right) {
        return left.removeStart < right.removeStart;
    });
    int removedBefore = 0;
    for (const MarkupMarker &marker : markers) {
        const int markerEnd = marker.removeStart + marker.removeLength;
        if (position >= markerEnd) {
            removedBefore += marker.removeLength;
        } else if (position > marker.removeStart) {
            return marker.removeStart - removedBefore;
        } else {
            break;
        }
    }
    return position - removedBefore;
}

TextRange inferredWordRange(QTextDocument *document, const QString &text, int position)
{
    const bool emptyOnLeft = position == 0 || text.at(position - 1).isSpace();
    const bool emptyOnRight = position == text.size() || text.at(position).isSpace();
    if (emptyOnLeft && emptyOnRight) {
        return {};
    }

    QTextCursor wordStart(document);
    wordStart.setPosition(position);
    QTextCursor wordEnd = wordStart;
    if (!emptyOnLeft) {
        wordStart.movePosition(QTextCursor::StartOfWord);
    }
    if (!emptyOnRight) {
        wordEnd.movePosition(QTextCursor::EndOfWord);
    }
    int start = wordStart.position();
    int end = wordEnd.position();
    while (start < end && text.at(start).isSpace()) {
        ++start;
    }
    while (end > start && text.at(end - 1).isSpace()) {
        --end;
    }

    const bool containsRightCharacter = emptyOnRight
        || (start <= position && end > position);
    const bool containsLeftCharacter = emptyOnLeft
        || (start < position && end >= position);
    if (start < end && containsRightCharacter && containsLeftCharacter) {
        if (CjkText::spanContainsCjk(text, start, end)) {
            const CjkText::WordRange cjkRange =
                CjkText::wordRangeForCursor(text, position);
            if (cjkRange.start < cjkRange.end) {
                return {cjkRange.start, cjkRange.end};
            }
        }
        return {start, end};
    }

    start = position;
    end = position;
    while (start > 0 && !text.at(start - 1).isSpace()) {
        --start;
    }
    while (end < text.size() && !text.at(end).isSpace()) {
        ++end;
    }
    if (CjkText::spanContainsCjk(text, start, end)) {
        const CjkText::WordRange cjkRange =
            CjkText::wordRangeForCursor(text, position);
        if (cjkRange.start < cjkRange.end) {
            return {cjkRange.start, cjkRange.end};
        }
    }
    return {start, end};
}

const QRegularExpression &listPrefixPattern()
{
    static const QRegularExpression pattern(QStringLiteral(R"(^\s*(?:[-+*]|\d+\.)\s+)"));
    return pattern;
}

const QRegularExpression &taskPrefixPattern()
{
    static const QRegularExpression pattern(QStringLiteral(R"(^\s*[-+*]\s+\[[ xX]\]\s+)"));
    return pattern;
}

const QRegularExpression &quotePrefixPattern()
{
    static const QRegularExpression pattern(QStringLiteral(R"(^\s*>\s?)"));
    return pattern;
}

const QRegularExpression &headingPrefixPattern()
{
    static const QRegularExpression pattern(QStringLiteral(R"(^\s*(#{1,6})\s+)"));
    return pattern;
}

enum class HeadingAction {
    Set,
    Cycle,
    Increase,
    Decrease,
};

struct HeadingCommand {
    HeadingAction action = HeadingAction::Cycle;
    int targetLevel = 1;
    bool adjustOnly = false;
    bool createFromEmpty = false;
};

int targetHeadingLevel(const HeadingCommand &command, int currentLevel)
{
    if (command.action == HeadingAction::Set) {
        return command.targetLevel;
    }
    if (command.action == HeadingAction::Increase) {
        return currentLevel == 0 ? 0 : qMin(6, currentLevel + 1);
    }
    if (command.action == HeadingAction::Decrease) {
        return currentLevel == 0 ? 0 : qMax(1, currentLevel - 1);
    }
    if (currentLevel == 0) {
        return 1;
    }
    return currentLevel < 6 ? currentLevel + 1 : 0;
}

QString transformHeadingLine(const QString &line, const HeadingCommand &command)
{
    if (line.trimmed().isEmpty()) {
        return command.createFromEmpty
            ? QString(targetHeadingLevel(command, 0), QLatin1Char('#')) + QLatin1Char(' ')
            : line;
    }

    const QRegularExpressionMatch match = headingPrefixPattern().match(line);
    if (command.adjustOnly && !match.hasMatch()) {
        // Ctrl+Num+- / Ctrl+Num++ 只推进或回退已经存在的标题行。
        return line;
    }
    const int currentLevel = match.hasMatch() ? match.captured(1).size() : 0;
    int targetLevel = targetHeadingLevel(command, currentLevel);
    const bool togglesMatchingHeading = command.action == HeadingAction::Set
        && match.hasMatch() && currentLevel == command.targetLevel;
    if (togglesMatchingHeading) {
        targetLevel = 0;
    }

    QString result = line;
    if (match.hasMatch()) {
        result.remove(match.capturedStart(), match.capturedLength());
    }
    if (targetLevel > 0) {
        result.prepend(QString(targetLevel, QLatin1Char('#')) + QLatin1Char(' '));
    }
    return result;
}

QString transformListLine(QString line, bool remove)
{
    if (line.trimmed().isEmpty()) {
        return line;
    }
    if (remove) {
        line.remove(listPrefixPattern());
    } else if (!listPrefixPattern().match(line).hasMatch()) {
        line.prepend(QStringLiteral("- "));
    }
    return line;
}

QString transformTaskLine(QString line, bool remove)
{
    if (line.trimmed().isEmpty()) {
        return line;
    }
    if (remove) {
        line.remove(taskPrefixPattern());
    } else {
        line.remove(listPrefixPattern());
        line.prepend(QStringLiteral("- [ ] "));
    }
    return line;
}

QString transformQuoteLine(QString line, bool remove)
{
    if (line.trimmed().isEmpty()) {
        return remove ? line : QStringLiteral("> ");
    }
    if (remove) {
        line.remove(quotePrefixPattern());
    } else if (!quotePrefixPattern().match(line).hasMatch()) {
        line.prepend(QStringLiteral("> "));
    }
    return line;
}

std::optional<int> taskCheckboxStatePosition(const QString &line)
{
    static const QRegularExpression checkboxPattern(QStringLiteral(
        R"(^([\t ]*(?:>[\t ]*)*(?:(?:[-+*]|\d+[.)])[\t ]+)?)\[([ xX])\])"));
    const QRegularExpressionMatch match = checkboxPattern.match(line);
    return match.hasMatch() ? std::optional<int>(match.capturedStart(2)) : std::nullopt;
}

} // namespace

EditorCommandRegistry::EditorCommandRegistry(AppSettings *settings,
                                             bool clipboardHistoryAvailable,
                                             QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_headingFolds = std::make_unique<HeadingFoldManager>(this);
    connect(m_headingFolds.get(), &HeadingFoldManager::markersChanged,
            this, &EditorCommandRegistry::headingFoldMarkersChanged);
    connect(m_headingFolds.get(), &HeadingFoldManager::navigationHighlightChanged,
            this, &EditorCommandRegistry::headingNavigationHighlightChanged);
    m_definitions = {
        {QStringLiteral("toggleBold"), QStringLiteral("切换加粗"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+B"), {}, false},
        {QStringLiteral("toggleItalic"), QStringLiteral("切换斜体"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+I"), {}, false},
        {QStringLiteral("cycleHeading"), QStringLiteral("循环标题级别"),
         QStringLiteral("Markdown"), QString(), {}, false},
        {QStringLiteral("setHeading1"), QStringLiteral("设为 1 级标题"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+Num+1"), {}, false},
        {QStringLiteral("setHeading2"), QStringLiteral("设为 2 级标题"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+Num+2"), {}, false},
        {QStringLiteral("setHeading3"), QStringLiteral("设为 3 级标题"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+Num+3"), {}, false},
        {QStringLiteral("setHeading4"), QStringLiteral("设为 4 级标题"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+Num+4"), {}, false},
        {QStringLiteral("setHeading5"), QStringLiteral("设为 5 级标题"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+Num+5"), {}, false},
        {QStringLiteral("setHeading6"), QStringLiteral("设为 6 级标题"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+Num+6"), {}, false},
        {QStringLiteral("increaseHeadingLevel"), QStringLiteral("标题推进至下一级"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+Num+-"), {}, false},
        {QStringLiteral("decreaseHeadingLevel"), QStringLiteral("标题推进至上一级"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+Num++"), {}, false},
        {QStringLiteral("foldAllHeadings"), QStringLiteral("折叠所有标题"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+M"), {}, false},
        {QStringLiteral("unfoldAllHeadings"), QStringLiteral("展开所有标题"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+Shift+M"), {}, false},
        {QStringLiteral("foldCurrentHeading"), QStringLiteral("折叠所属标题"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+Shift+["), {}, false},
        {QStringLiteral("unfoldCurrentHeading"), QStringLiteral("展开所属标题"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+Shift+]"), {}, false},
        {QStringLiteral("previousHeading"), QStringLiteral("跳到上一个标题"),
         QStringLiteral("导航"), QStringLiteral("Ctrl+Up"), {}, false},
        {QStringLiteral("nextHeading"), QStringLiteral("跳到下一个标题"),
         QStringLiteral("导航"), QStringLiteral("Ctrl+Down"), {}, false},
        {QStringLiteral("deleteLine"), QStringLiteral("删除整行"),
         QStringLiteral("编辑"), QStringLiteral("Ctrl+Shift+L"), {}, false},
        {QStringLiteral("copyLine"), QStringLiteral("复制整行"),
         QStringLiteral("编辑"), QString(), {}, false},
        {QStringLiteral("cutLine"), QStringLiteral("剪切整行"),
         QStringLiteral("编辑"), QString(), {}, false},
        {QStringLiteral("pasteClipboard"), QStringLiteral("粘贴剪贴板"),
         QStringLiteral("编辑"), QString(), {}, false},
        {QStringLiteral("toggleList"), QStringLiteral("切换项目列表"),
         QStringLiteral("Markdown"), QString(), {}, false},
        {QStringLiteral("convertToOrderedList"), QStringLiteral("转换为有序列表"),
         QStringLiteral("Markdown"), QString(), {}, false},
        {QStringLiteral("toggleTask"), QStringLiteral("切换任务项"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+Alt+T"), {}, false},
        {QStringLiteral("toggleCheckbox"), QStringLiteral("切换本行复选框"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+L"), {}, false},
        {QStringLiteral("toggleQuote"), QStringLiteral("切换引用"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+Shift+Q"), {}, false},
        {QStringLiteral("wrapCode"), QStringLiteral("切换代码标记"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+Alt+C"), {}, false},
        {QStringLiteral("find"), QStringLiteral("查找"),
         QStringLiteral("导航"), QStringLiteral("Ctrl+F"), {}, true},
        {QStringLiteral("replace"), QStringLiteral("查找并替换"),
         QStringLiteral("导航"), QStringLiteral("Ctrl+H"), {}, true},
        {QStringLiteral("commandPalette"), QStringLiteral("打开命令面板"),
         QStringLiteral("界面"), QStringLiteral("Ctrl+Shift+P"), {}, true},
        {QStringLiteral("settings"), QStringLiteral("打开设置"),
         QStringLiteral("界面"), QStringLiteral("Ctrl+,"), {}, true},
        {QStringLiteral("formatSpacing"), QStringLiteral("整理选区或当前行格式"),
         QStringLiteral("编辑"), QStringLiteral("Alt+F"), {}, false},
        {QStringLiteral("clearDocument"), QStringLiteral("清空整个编辑区"),
         QStringLiteral("编辑"), QStringLiteral("Alt+X"), {}, false},
    };

    if (clipboardHistoryAvailable) {
        m_definitions.append({QStringLiteral("clipboardHistory"),
                              QStringLiteral("切换剪贴板历史"),
                              QStringLiteral("界面"), QString(), {}, true});
    }

    for (Definition &item : m_definitions) {
        item.shortcut = m_settings ? m_settings->shortcut(item.id, item.defaultShortcut)
                                   : item.defaultShortcut;
    }

    m_commandHandlers = {
        {QStringLiteral("formatSpacing"), [this] { return formatSpacing(); }},
        {QStringLiteral("toggleBold"),
         [this] { return wrapSelection(QStringLiteral("**"), QStringLiteral("**")); }},
        {QStringLiteral("toggleItalic"),
         [this] { return wrapSelection(QStringLiteral("*"), QStringLiteral("*")); }},
        {QStringLiteral("wrapCode"), [this] {
             const QString selection = selectedText();
             if (selection.contains(QLatin1Char('\n'))) {
                 return wrapSelection(QStringLiteral("```\n"), QStringLiteral("\n```"));
             }
             return wrapSelection(QStringLiteral("`"), QStringLiteral("`"));
         }},
        {QStringLiteral("deleteLine"), [this] { return deleteSelectedLines(); }},
        {QStringLiteral("clearDocument"), [this] { return clearDocument(); }},
        {QStringLiteral("copyLine"), [this] { return copyLine(); }},
        {QStringLiteral("cutLine"), [this] { return cutLine(); }},
        {QStringLiteral("pasteClipboard"), [this] { return pasteClipboard(); }},
        {QStringLiteral("toggleCheckbox"), [this] { return toggleCurrentCheckbox(); }},
        {QStringLiteral("convertToOrderedList"),
         [this] { return convertToOrderedList(); }},
        {QStringLiteral("foldAllHeadings"), [this] { return m_headingFolds->foldAll(); }},
        {QStringLiteral("unfoldAllHeadings"), [this] { return m_headingFolds->unfoldAll(); }},
        {QStringLiteral("foldCurrentHeading"),
         [this] { return m_headingFolds->foldCurrent(); }},
        {QStringLiteral("unfoldCurrentHeading"),
         [this] { return m_headingFolds->unfoldCurrent(); }},
        {QStringLiteral("previousHeading"),
         [this] { return navigateToHeading(true); }},
        {QStringLiteral("nextHeading"),
         [this] { return navigateToHeading(false); }},
    };

    m_selectionDragScrollTimer.setInterval(30);
    connect(&m_selectionDragScrollTimer, &QTimer::timeout, this, [this] {
        if (m_externalDragActive) {
            updateExternalTextDrag(m_externalDragScenePosition);
        } else {
            updateSelectionDrag(m_selectionDragScenePosition, true);
        }
    });

    m_headingScrollTimer.setSingleShot(true);
    connect(&m_headingScrollTimer, &QTimer::timeout, this, [this] {
        const int position = m_pendingHeadingScrollPosition;
        m_pendingHeadingScrollPosition = -1;
        if (position < 0) {
            return;
        }
        // 延迟期间用户若已移动光标或文档已被替换，放弃本次对齐，避免打扰用户。
        if (!m_editor || m_editor->property("cursorPosition").toInt() != position) {
            return;
        }
        scrollViewportToHeading(position);
    });
}

EditorCommandRegistry::~EditorCommandRegistry()
{
    resetExternalTextDrag();
}

void EditorCommandRegistry::setEditor(QObject *editor, QTextDocument *document)
{
    resetSelectionDrag(true);
    resetExternalTextDrag();
    if (m_document) {
        QObject::disconnect(m_document.data(), nullptr, this, nullptr);
    }
    m_editor = editor;
    m_document = document;
    m_headingFolds->setEditor(editor, document);
    m_documentTextSnapshot = document ? document->toPlainText() : QString();
    m_documentTextSnapshotPrepared = false;
    if (m_document) {
        // 快照代表上一个已完成的编辑状态。结构编辑在同一 edit block 内
        // 仅读取一次编辑后文本，而 edit block 结束后由此处统一同步。
        connect(m_document.data(), &QTextDocument::contentsChanged, this,
                [this] {
            if (m_document) {
                if (m_documentTextSnapshotPrepared) {
                    m_documentTextSnapshotPrepared = false;
                } else {
                    m_documentTextSnapshot = m_document->toPlainText();
                }
            }
        });
    }
}

void EditorCommandRegistry::setWindow(QQuickWindow *window)
{
    m_window = window;
}

void EditorCommandRegistry::setClipboardAccess(ClipboardReader reader, ClipboardWriter writer)
{
    m_clipboardReader = std::move(reader);
    m_clipboardWriter = std::move(writer);
}

QVariantList EditorCommandRegistry::commands() const
{
    QVariantList result;
    result.reserve(m_definitions.size());
    for (const Definition &item : m_definitions) {
        QVariantMap command;
        command.insert(QStringLiteral("id"), item.id);
        command.insert(QStringLiteral("title"), item.title);
        command.insert(QStringLiteral("category"), item.category);
        command.insert(QStringLiteral("shortcut"), item.shortcut);
        result.append(command);
    }
    return result;
}

QString EditorCommandRegistry::shortcut(const QString &commandId) const
{
    const Definition *item = definition(commandId);
    return item ? item->shortcut : QString();
}

QVariantList EditorCommandRegistry::headingFoldMarkers() const
{
    return m_headingFolds ? m_headingFolds->markers() : QVariantList{};
}

int EditorCommandRegistry::headingFoldVisibleEndPosition() const
{
    return m_headingFolds ? m_headingFolds->visibleEndPosition() : 0;
}

QVariantMap EditorCommandRegistry::headingNavigationHighlight() const
{
    return m_headingFolds ? m_headingFolds->navigationHighlight() : QVariantMap{};
}

QVariantMap EditorCommandRegistry::headingFoldDiagnostics() const
{
    return m_headingFolds ? m_headingFolds->diagnostics() : QVariantMap{};
}

bool EditorCommandRegistry::toggleHeadingFoldAt(int headingPosition)
{
    return m_headingFolds && m_headingFolds->toggleAt(headingPosition);
}

void EditorCommandRegistry::resetHeadingFolds()
{
    if (m_headingFolds) {
        m_headingFolds->reset();
    }
}

bool EditorCommandRegistry::setShortcut(const QString &commandId, const QString &sequence,
                                        QString *errorMessage)
{
    Definition *item = definition(commandId);
    if (!item) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未知命令：%1").arg(commandId);
        }
        return false;
    }

    const QString normalized = sequence.trimmed();
    if (!normalized.isEmpty()) {
        const QKeySequence parsed = QKeySequence::fromString(normalized, QKeySequence::PortableText);
        if (parsed.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("无效快捷键：%1").arg(normalized);
            }
            return false;
        }
        for (const Definition &other : m_definitions) {
            if (other.id != item->id && !other.shortcut.isEmpty()
                && QKeySequence::fromString(other.shortcut, QKeySequence::PortableText) == parsed) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("快捷键已被“%1”使用").arg(other.title);
                }
                return false;
            }
        }
        item->shortcut = parsed.toString(QKeySequence::PortableText);
    } else {
        item->shortcut.clear();
    }

    if (m_settings) {
        m_settings->setShortcut(item->id, item->shortcut);
    }
    emit commandsChanged();
    return true;
}

void EditorCommandRegistry::resetShortcuts()
{
    if (m_settings) {
        m_settings->resetShortcuts();
    }
    for (Definition &item : m_definitions) {
        item.shortcut = item.defaultShortcut;
    }
    emit commandsChanged();
}

bool EditorCommandRegistry::execute(const QString &commandId)
{
    const Definition *item = definition(commandId);
    if (!item) {
        return false;
    }
    if (item->uiCommand) {
        emit uiCommandRequested(commandId);
        return true;
    }
    if (!m_editor || !m_document) {
        return false;
    }

    const auto handler = m_commandHandlers.constFind(commandId);
    if (handler != m_commandHandlers.cend()) {
        // 删除整行、剪切、清空与粘贴会改变文本，与键盘输入/删除
        // 共用同一套自动滚动检查（撤销/重做同理，不预设方向）。
        const bool trackedEdit = commandId == QLatin1String("deleteLine")
            || commandId == QLatin1String("cutLine")
            || commandId == QLatin1String("clearDocument")
            || commandId == QLatin1String("pasteClipboard");
        if (trackedEdit) {
            beginInputAutoScrollTracking(commandId);
        }
        const bool handled = handler.value()();
        if (trackedEdit) {
            // 失败或无文本变化的命令也必须排队检查，以释放编辑前临时保持。
            queueInputAutoScrollCheck();
        }
        return handled;
    }
    // 标题、列表、任务与引用命令共用同一行变换管线。
    return transformSelectedLines(commandId);
}

bool EditorCommandRegistry::handleEditorEvent(QEvent *event)
{
    if (!event || !m_editor || !m_document) {
        return false;
    }

    if (handleSelectionDragEvent(event)) {
        return true;
    }

    if (event->type() == QEvent::KeyPress) {
        const auto *keyEvent = static_cast<QKeyEvent *>(event);
        const Qt::KeyboardModifiers modifiers = keyEvent->modifiers();
        const bool shiftPressed = modifiers.testFlag(Qt::ShiftModifier);
        const bool ctrlZ = modifiers.testFlag(Qt::ControlModifier)
            && keyEvent->key() == Qt::Key_Z
            && !shiftPressed
            && !modifiers.testFlag(Qt::AltModifier)
            && !modifiers.testFlag(Qt::MetaModifier);
        // 撤销/重做视为普通编辑：不预设方向，编辑后光标落在哪条视口边
        // 就按哪条规则自动滚动（撤销可能对应删除也可能对应输入，重做亦然）。
        if (ctrlZ) {
            return performUndo();
        }
        const bool ctrlRedo = modifiers.testFlag(Qt::ControlModifier)
            && (keyEvent->key() == Qt::Key_Y
                || (keyEvent->key() == Qt::Key_Z && shiftPressed))
            && !modifiers.testFlag(Qt::AltModifier)
            && !modifiers.testFlag(Qt::MetaModifier);
        if (ctrlRedo) {
            return performRedo();
        }
        // PageUp/PageDown：按一页纯滚动浏览，光标与选区保持不动；
        // Ctrl/Alt/Meta 组合不拦截（Shift 组合也按纯滚动处理）。
        const bool pageKey = (keyEvent->key() == Qt::Key_PageUp
                              || keyEvent->key() == Qt::Key_PageDown)
            && !modifiers.testFlag(Qt::ControlModifier)
            && !modifiers.testFlag(Qt::AltModifier)
            && !modifiers.testFlag(Qt::MetaModifier);
        if (pageKey) {
            if (QQuickItem *viewport = editorViewport()) {
                const qreal viewportHeight = viewport->height();
                const qreal currentY = viewport->property("contentY").toReal();
                const qreal maximumY = qMax<qreal>(
                    0.0, viewport->property("contentHeight").toReal() - viewportHeight);
                const qreal delta = keyEvent->key() == Qt::Key_PageDown
                    ? viewportHeight : -viewportHeight;
                const qreal requestedY = qBound<qreal>(0.0, currentY + delta, maximumY);
                if (!qFuzzyCompare(requestedY + 1.0, currentY + 1.0)) {
                    animateViewportScrollTo(viewport, requestedY);
                }
            }
            return true;
        }
        // 无选区时拦截 Ctrl+C / Ctrl+X / Ctrl+V，执行整行复制、剪切与智能粘贴；
        // 有选区时保持 TextEdit 标准行为（复制/剪切/替换选区）。
        const bool plainCtrl = modifiers.testFlag(Qt::ControlModifier)
            && !modifiers.testFlag(Qt::ShiftModifier)
            && !modifiers.testFlag(Qt::AltModifier)
            && !modifiers.testFlag(Qt::MetaModifier);
        if (plainCtrl && !m_editor->property("inputMethodComposing").toBool()) {
            const bool hasSelection = m_editor->property("selectionStart").toInt()
                != m_editor->property("selectionEnd").toInt();
            if (keyEvent->key() == Qt::Key_C && !hasSelection) {
                return copyLine();
            }
            if (keyEvent->key() == Qt::Key_X
                && !m_editor->property("readOnly").toBool()) {
                beginInputAutoScrollTracking(QStringLiteral("cut"));
                const bool cut = hasSelection ? cutSelection() : cutLine();
                queueInputAutoScrollCheck();
                return cut;
            }
            if (keyEvent->key() == Qt::Key_V
                && !m_editor->property("readOnly").toBool()) {
                beginInputAutoScrollTracking(QStringLiteral("paste"));
                const bool pasted = pasteClipboard();
                queueInputAutoScrollCheck();
                return pasted;
            }
        }
        // Ctrl+左/右：中英文自适应词边界。纯 ASCII 跨度沿用 Qt 原生落点，
        // 只有跨越中文的移动才使用 CjkText 的分词规则；Shift 时扩展选区。
        const bool ctrlHorizontalArrow =
            (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right)
            && modifiers.testFlag(Qt::ControlModifier)
            && !modifiers.testFlag(Qt::AltModifier)
            && !modifiers.testFlag(Qt::MetaModifier);
        if (ctrlHorizontalArrow
            && !m_editor->property("inputMethodComposing").toBool()) {
            return moveByCjkAwareWord(keyEvent->key() == Qt::Key_Left,
                                      shiftPressed);
        }
        // Ctrl+Backspace / Ctrl+Delete：按词删除。有选区时放行给原生删除选区；
        // 否则仅当原生删除跨度含中文时改用新分词边界。
        const bool ctrlWordDelete =
            (keyEvent->key() == Qt::Key_Backspace
             || keyEvent->key() == Qt::Key_Delete)
            && modifiers.testFlag(Qt::ControlModifier)
            && !modifiers.testFlag(Qt::AltModifier)
            && !modifiers.testFlag(Qt::MetaModifier);
        if (ctrlWordDelete) {
            const QString wordDeleteKind = keyEvent->key() == Qt::Key_Backspace
                ? QStringLiteral("wordBackspace")
                : QStringLiteral("wordDelete");
            beginInputAutoScrollTracking(wordDeleteKind);
            if (m_editor->property("inputMethodComposing").toBool()) {
                // IME 组合中不拦截：交给 TextEdit 原生处理，仅登记滚动检查。
                queueInputAutoScrollCheck();
                return false;
            }
            if (m_editor->property("selectionStart").toInt()
                != m_editor->property("selectionEnd").toInt()) {
                // 有选区：交给 TextEdit 原生删除选区。
                queueInputAutoScrollCheck();
                return false;
            }
            const bool deleted = deleteByCjkAwareWord(
                keyEvent->key() == Qt::Key_Backspace);
            queueInputAutoScrollCheck();
            return deleted;
        }
        const bool tabPressed = keyEvent->key() == Qt::Key_Tab
            || keyEvent->key() == Qt::Key_Backtab;
        const bool plainBackspace = keyEvent->key() == Qt::Key_Backspace
            && !(modifiers & (Qt::ShiftModifier | Qt::ControlModifier
                              | Qt::AltModifier | Qt::MetaModifier));
        const bool plainDelete = keyEvent->key() == Qt::Key_Delete
            && !(modifiers & (Qt::ShiftModifier | Qt::ControlModifier
                              | Qt::AltModifier | Qt::MetaModifier));
        if (plainBackspace) {
            beginInputAutoScrollTracking(QStringLiteral("backspace"));
            if (handleSpecialBackspace() || handleStructuralDelete(true)) {
                queueInputAutoScrollCheck();
                return true;
            }
            // 普通退格由 TextEdit 原生处理，仍需触发自动滚动检查。
            queueInputAutoScrollCheck();
            return false;
        }
        if (plainDelete) {
            beginInputAutoScrollTracking(QStringLiteral("delete"));
            if (handleStructuralDelete(false)) {
                queueInputAutoScrollCheck();
                return true;
            }
            // 普通删除由 TextEdit 原生处理，仍需触发自动滚动检查。
            queueInputAutoScrollCheck();
            return false;
        }
        const bool enterPressed = keyEvent->key() == Qt::Key_Return
            || keyEvent->key() == Qt::Key_Enter;
        const bool plainEnter = enterPressed
            && !(modifiers & (Qt::ShiftModifier | Qt::ControlModifier
                              | Qt::AltModifier | Qt::MetaModifier));
        const bool softEnter = enterPressed
            && modifiers == Qt::ShiftModifier;
        if ((plainEnter || softEnter)
            && !m_editor->property("inputMethodComposing").toBool()) {
            beginInputAutoScrollTracking(QStringLiteral("enter"));
            const int enterPosition = m_editor->property("selectionStart").toInt();
            const int enterLineStart = lineRangeAt(
                m_documentTextSnapshot, enterPosition).start;
            // 列表与引用处理共享同一次围栏分析，避免普通 Enter 候选链重复扫描全文。
            const bool insideFencedBlock = isInsideFencedBlock(enterLineStart);
            if (plainEnter && handleListEnter(insideFencedBlock)) {
                queueInputAutoScrollCheck();
                return true;
            }
            if (handleQuoteEnter(softEnter, insideFencedBlock)) {
                queueInputAutoScrollCheck();
                return true;
            }
            // 普通换行由 TextEdit 原生插入，仍需触发自动滚动检查。
            queueInputAutoScrollCheck();
        }
        if (tabPressed
            && !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            beginInputAutoScrollTracking(QStringLiteral("tab"));
            if (shiftPressed || keyEvent->key() == Qt::Key_Backtab) {
                const bool outdented = changeIndent(true);
                // 未处理时 TextEdit 仍可能接手按键；即使最终无文本变化，
                // 延迟检查也负责恢复编辑前临时保持状态。
                queueInputAutoScrollCheck();
                return outdented;
            }
            const bool handled = jumpOutOfPair() || changeIndent(false);
            queueInputAutoScrollCheck();
            return handled;
        }

        const bool plainVerticalArrow = (keyEvent->key() == Qt::Key_Down
                                         || keyEvent->key() == Qt::Key_Up)
            && !(modifiers & (Qt::ShiftModifier | Qt::ControlModifier
                              | Qt::AltModifier | Qt::MetaModifier));
        if (plainVerticalArrow) {
            QTextCursor probe(m_document);
            probe.setPosition(m_editor->property("cursorPosition").toInt());
            const QTextCursor::MoveOperation direction = keyEvent->key() == Qt::Key_Down
                ? QTextCursor::Down : QTextCursor::Up;
            if (!probe.movePosition(direction)) {
                probe.movePosition(keyEvent->key() == Qt::Key_Down
                                       ? QTextCursor::EndOfLine
                                       : QTextCursor::StartOfLine);
                selectRange(probe.position(), probe.position());
                return true;
            }
        }

        if (!(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
            && !keyEvent->text().isEmpty()) {
            beginInputAutoScrollTracking(QStringLiteral("key"));
            const int start = m_editor->property("selectionStart").toInt();
            const int end = m_editor->property("selectionEnd").toInt();
            const QString beforeText = m_documentTextSnapshot;
            const QString text = keyEvent->text();
            // 连续输入 `·`：若前一步正是上一个字面 `·`（或其生成的 `` 对），
            // 先撤销该步再在当前事件内完成转换，使一次 Ctrl+Z 能整体撤销。
            if (const auto mergedResult =
                    tryMergeMiddleDotConversion(text, start, beforeText)) {
                if (mergedResult->runAutoSpacing) {
                    applyAutoSpacing(mergedResult->footprint);
                }
                queueInputAutoScrollCheck();
                return true;
            }
            // 把字符插入与自动空格放进同一个 edit block，使一次 Ctrl+Z 能整体撤销。
            QTextCursor undoGroupCursor(m_document);
            undoGroupCursor.setPosition(start);
            undoGroupCursor.beginEditBlock();
            const TypedEditResult result = handleTypedText(text);
            if (!result.consumed) {
                QString expectedText = beforeText;
                expectedText.replace(start, end - start, text);
                if (orderedListEditRequiresRepair(
                        beforeText, expectedText, start, end, start + text.size())) {
                    QTextCursor insertionCursor(m_document);
                    insertionCursor.setPosition(start);
                    insertionCursor.setPosition(end, QTextCursor::KeepAnchor);
                    insertionCursor.insertText(text);
                    const int cursorAfter = start + text.size();
                    m_editor->setProperty("cursorPosition", cursorAfter);
                    const QString afterText = m_document->toPlainText();
                    repairOrderedLists(beforeText, afterText, false);
                    undoGroupCursor.endEditBlock();
                    focusEditor();
                    queueInputAutoScrollCheck();
                    return true;
                }
                QTimer::singleShot(0, this,
                                   [this, start, text, expectedText,
                                    undoGroupCursor]() mutable {
                                    if (!m_editor || !m_document) {
                                        return;
                                    }
                                    applyAutoSpacing(
                                        {start, start + static_cast<int>(text.size())},
                                        text.size() > 1, expectedText);
                                    undoGroupCursor.endEditBlock();
                                    // 编辑块收尾后再检查，避免判定读到瞬态文档状态。
                                    queueInputAutoScrollCheck();
                                });
                return false;
            }
            if (result.runAutoSpacing) {
                applyAutoSpacing(result.footprint);
            }
            undoGroupCursor.endEditBlock();
            queueInputAutoScrollCheck();
            return true;
        }
        return false;
    }

    // 双击按词选择：先用新边界计算词范围；若为空（纯空白/边界）放行给 Qt 原生，
    // 否则向 QML 重放副本以保持三击状态，再用新词范围覆盖原生选区。
    if (event->type() == QEvent::MouseButtonDblClick) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton
            && !mouseEvent->modifiers().testAnyFlags(
                Qt::ShiftModifier | Qt::ControlModifier
                | Qt::AltModifier | Qt::MetaModifier)
            && !m_editor->property("inputMethodComposing").toBool()) {
            return handleCjkDoubleClick(mouseEvent);
        }
    }

    if (event->type() == QEvent::InputMethod) {
        const auto *inputEvent = static_cast<QInputMethodEvent *>(event);
        const QString committedText = inputEvent->commitString();
        if (committedText.isEmpty()) {
            return false;
        }
        beginInputAutoScrollTracking(QStringLiteral("ime"));
        const bool relevant = committedText == QStringLiteral("```")
            || committedText == QStringLiteral("`")
            || committedText == QStringLiteral("·")
            || committedText == QStringLiteral(">")
            || committedText == QStringLiteral("》")
            || pairForOpening(committedText)
            || isClosingDelimiter(committedText);
        const int start = m_editor->property("selectionStart").toInt();
        const int end = m_editor->property("selectionEnd").toInt();
        const QString beforeText = m_documentTextSnapshot;
        QString expectedText = beforeText;
        expectedText.replace(start, end - start, committedText);
        const bool repairOrderedList = orderedListEditRequiresRepair(
            beforeText, expectedText, start, end, start + committedText.size());
        // 连续输入 `·`：提交文本尚未插入，若前一步正是上一个字面 `·`
        // （或其生成的 `` 对），先撤销该步再在本事件内完成转换；
        // 返回 true 阻止编辑器再次插入提交文本。
        if (const auto mergedResult =
                tryMergeMiddleDotConversion(committedText, start, beforeText)) {
            if (mergedResult->runAutoSpacing) {
                applyAutoSpacing(mergedResult->footprint);
            }
            queueInputAutoScrollCheck();
            return true;
        }
        const QString selection = selectedText();
        // 与键盘路径一致：先开启 edit block，让编辑器插入的提交文本与后续
        // 完成处理合并为一次撤销（一次 Ctrl+Z 可整体撤销 IME 提交及其转换）。
        QTextCursor undoGroupCursor(m_document);
        undoGroupCursor.setPosition(start);
        undoGroupCursor.beginEditBlock();
        if (!relevant) {
            QTimer::singleShot(0, this,
                               [this, beforeText, start, committedText, expectedText,
                                repairOrderedList, undoGroupCursor]() mutable {
                if (m_editor && m_document) {
                    if (!repairOrderedList) {
                        applyAutoSpacing(
                            {start, start + static_cast<int>(committedText.size())},
                            committedText.size() > 1, expectedText);
                    } else {
                        const QString afterText = m_document->toPlainText();
                        repairOrderedLists(beforeText, afterText, false);
                    }
                }
                undoGroupCursor.endEditBlock();
                // 编辑块收尾后再检查，避免判定读到瞬态文档状态。
                queueInputAutoScrollCheck();
            });
            return false;
        }

        QTimer::singleShot(0, this,
                           [this, committedText, beforeText, selection, start, end,
                            repairOrderedList, undoGroupCursor]() mutable {
            if (m_editor && m_document) {
                const auto completion = completeInputMethodCommit(
                    committedText, beforeText, selection, start, end);
                if (completion && completion->autoSpace) {
                    applyAutoSpacing(completion->footprint);
                }
                if (repairOrderedList) {
                    const QString afterText = m_document->toPlainText();
                    repairOrderedLists(beforeText, afterText, false);
                }
            }
            undoGroupCursor.endEditBlock();
            // 编辑块收尾后再检查，避免判定读到瞬态文档状态。
            queueInputAutoScrollCheck();
        });
    }
    return false;
}

void EditorCommandRegistry::beginInputAutoScrollTracking(const QString &kind)
{
    if (!m_editor || !m_document) {
        return;
    }
    if (!m_inputAutoScrollTrackingActive) {
        m_inputAutoScrollTrackingActive = true;
        const bool releasePending = m_window
            && m_window->property("releaseInputScrollHoldAfterAnimation").toBool();
        // 正在随输入滚动动画等待释放的 hold 属于上一轮临时状态；新编辑
        // 打断动画后不应把它误认成需要永久保留的既有缓冲。
        m_inputScrollHoldWasActive = m_window
            && m_window->property("inputScrollHoldBottom").toBool()
            && !releasePending;
    }
    // 必须在文本实际变化前扩大 Flickable 的有效内容范围；否则内容高度
    // 一旦收缩，contentY 会先被钳到新的 max，延迟检查再恢复时便产生抽动。
    // QML 入口同时停止尚未结束的输入滚动动画并停在当前帧。
    if (!m_window
        || !QMetaObject::invokeMethod(m_window, "prepareInputScrollTracking")) {
        if (m_window) {
            m_window->setProperty("inputScrollHoldBottom", true);
        }
    }
    ++m_inputScrollDiag.inputCount;
    m_inputScrollDiag.lastKind = kind;
    if (QQuickItem *viewport = editorViewport()) {
        m_inputPreScrollY = viewport->property("contentY").toReal();
    } else {
        m_inputPreScrollY = 0.0;
    }
    m_inputPreTextLength = m_document->characterCount() - 1;
    InputScrollEvent event;
    event.type = QStringLiteral("input");
    event.kind = kind;
    event.preY = m_inputPreScrollY;
    event.preLen = m_inputPreTextLength;
    recordInputScrollEvent(event);
}

void EditorCommandRegistry::queueInputAutoScrollCheck()
{
    if (m_inputAutoScrollCheckQueued) {
        return;
    }
    m_inputAutoScrollCheckQueued = true;
    // 延迟到文档/布局落定后再判定：真实输入（IME/回车）会在事件处理期间
    // 产生瞬态光标矩形与重复 contentsChanged，立即检查会读到过期几何
    // （误判触底/触顶）。该延迟足够这些瞬态收尾。
    QTimer::singleShot(layoutSettleDelayMs, this, [this] {
        m_inputAutoScrollCheckQueued = false;
        checkInputAutoScroll();
    });
}

void EditorCommandRegistry::checkInputAutoScroll()
{
    ++m_inputScrollDiag.checkCount;
    m_inputScrollDiag.overrideDetected = false;
    m_inputScrollDiag.didScroll = false;
    InputScrollEvent event;
    event.type = QStringLiteral("check");
    event.kind = m_inputScrollDiag.lastKind;
    event.preLen = m_inputPreTextLength;
    if (!m_editor || !m_document) {
        return;
    }
    const int documentLength = m_document->characterCount() - 1;
    m_inputScrollDiag.docLength = documentLength;
    m_inputScrollDiag.preLength = m_inputPreTextLength;
    event.docLen = documentLength;
    if (documentLength == m_inputPreTextLength) {
        // 本次编辑没有改变文本（如被 IME 组合状态拦截、空撤销/删除），
        // 不触发滚动。
        if (m_window) {
            m_window->setProperty("inputScrollHoldBottom", m_inputScrollHoldWasActive);
        }
        m_inputAutoScrollTrackingActive = false;
        m_inputScrollHoldWasActive = false;
        m_inputPreTextLength = -1;
        ++m_inputScrollEarlyReturnCount;
        event.earlyReturn = true;
        recordInputScrollEvent(event);
        return;
    }
    const int cursorPosition = m_editor->property("cursorPosition").toInt();
    m_inputScrollDiag.cursorPosition = cursorPosition;
    event.cursorPos = cursorPosition;
    QRectF cursorRect;
    const bool rectLocated = QMetaObject::invokeMethod(
        m_editor, "positionToRectangle", Qt::DirectConnection,
        Q_RETURN_ARG(QRectF, cursorRect), Q_ARG(int, cursorPosition));
    m_inputScrollDiag.cursorTop = rectLocated ? cursorRect.y() : -1.0;
    m_inputScrollDiag.cursorBottom = rectLocated
        ? cursorRect.y() + cursorRect.height() : -1.0;
    event.curBottom = m_inputScrollDiag.cursorBottom;
    bool didScroll = false;
    if (QQuickItem *viewport = editorViewport()) {
        const qreal viewportHeight = viewport->height();
        const qreal currentY = viewport->property("contentY").toReal();
        // 预保持会临时扩展 viewport.contentHeight；所有边界判断必须使用
        // 不含保持缓冲的自然内容高度，否则会把临时范围误当成真实滚动范围。
        const qreal naturalContentHeight = m_window
                && m_window->property("inputScrollNaturalContentHeight").isValid()
            ? m_window->property("inputScrollNaturalContentHeight").toReal()
            : viewport->property("contentHeight").toReal();
        const qreal maximumY = qMax<qreal>(0.0, naturalContentHeight - viewportHeight);
        m_inputScrollDiag.currentY = currentY;
        m_inputScrollDiag.viewportHeight = viewportHeight;
        m_inputScrollDiag.maxY = maximumY;
        event.curY = currentY;
        event.vh = viewportHeight;
        event.maxY = maximumY;
        const bool atDocumentStart = cursorPosition == 0;
        const bool atDocumentEnd = cursorPosition == documentLength;
        // 与底边判定保持同一坐标约定（不叠加 editorItem 的 y 偏移），
        // 锚定目标时再统一加上该偏移。
        const bool cursorTouchesBottomEdge = rectLocated
            && cursorRect.y() + cursorRect.height() >= currentY + viewportHeight - 0.5;
        const bool cursorTouchesTopEdge = rectLocated
            && cursorRect.y() <= currentY + 0.5;
        m_inputScrollDiag.atStart = atDocumentStart;
        m_inputScrollDiag.atEnd = atDocumentEnd;
        m_inputScrollDiag.touchedTop = cursorTouchesTopEdge;
        m_inputScrollDiag.touchedBottom = cursorTouchesBottomEdge;
        event.atStart = atDocumentStart;
        event.atEnd = atDocumentEnd;
        event.touchedTop = cursorTouchesTopEdge;
        event.touchedBottom = cursorTouchesBottomEdge;
        bool triggered = false;
        // 间歇式自动滚动：只在光标碰到/越过视口底边时触发一次，
        // 触发后不再重复，光标随输入自然下落，下次触底再触发。
        // - 段中触底：光标行滚到视口上 1/3；
        // - 段尾触底：等效于滚到底（PageDown 到底，正文末尾下方 2/3 页
        //   留白翻出，光标停在上 1/3）。
        if (cursorTouchesBottomEdge) {
            triggered = true;
            ++m_inputScrollDiag.triggerCount;
            qreal targetY = currentY;
            if (atDocumentEnd && rectLocated) {
                targetY = maximumY;
            } else if (rectLocated) {
                QQuickItem *item = editorItem();
                const qreal editorY = item ? item->y() : 0.0;
                targetY = qBound<qreal>(
                    0.0, editorY + cursorRect.y() - viewportHeight / 3.0, maximumY);
            }
            if (!qFuzzyCompare(targetY + 1.0, currentY + 1.0)) {
                animateViewportScrollTo(viewport, targetY, true);
            } else if (m_window) {
                m_window->setProperty("inputScrollHoldBottom", false);
            }
            m_inputScrollDiag.targetY = targetY;
            event.targetY = targetY;
            // 本次检查是否实际移动了视口（最小可见跟随可能已先行滚动到位）。
            didScroll = !qFuzzyCompare(targetY + 1.0, currentY + 1.0);
            m_inputScrollDiag.didScroll = didScroll;
            event.didScroll = didScroll;
        } else if (cursorTouchesTopEdge) {
            // 删除触顶的严格镜像：只在光标碰到/越过视口顶边时触发一次，
            // 触发后光标行落到视口距顶 2/3 处（下 1/3），继续删除再次
            // 触顶才再次触发；光标位于文档开头时滚到顶部（与段尾触底
            // 滚到底对称）。
            triggered = true;
            ++m_inputScrollDiag.triggerCount;
            qreal targetY = currentY;
            if (atDocumentStart && rectLocated) {
                targetY = 0.0;
            } else if (rectLocated) {
                QQuickItem *item = editorItem();
                const qreal editorY = item ? item->y() : 0.0;
                targetY = qBound<qreal>(
                    0.0, editorY + cursorRect.y() - viewportHeight * 2.0 / 3.0,
                    maximumY);
            }
            if (!qFuzzyCompare(targetY + 1.0, currentY + 1.0)) {
                animateViewportScrollTo(viewport, targetY, true);
            } else if (m_window) {
                m_window->setProperty("inputScrollHoldBottom", false);
            }
            m_inputScrollDiag.targetY = targetY;
            event.targetY = targetY;
            didScroll = !qFuzzyCompare(targetY + 1.0, currentY + 1.0);
            m_inputScrollDiag.didScroll = didScroll;
            event.didScroll = didScroll;
        }
        if (triggered) {
            // 触发后采样落定位置，检测是否被后续逻辑（如延迟的光标跟随）覆盖；
            // 动画开启时顺延到动画结束后再采样，避免把动画中间态误判为覆盖。
            const int settleDelayMs = 50
                + (m_window ? m_window->property("scrollAnimationDurationMs").toInt() : 0);
            QTimer::singleShot(settleDelayMs, this, [this] {
                if (QQuickItem *vp = editorViewport()) {
                    m_inputScrollDiag.settleY = vp->property("contentY").toReal();
                    InputScrollEvent settleEvent;
                    settleEvent.type = QStringLiteral("settle");
                    settleEvent.kind = m_inputScrollDiag.lastKind;
                    settleEvent.settleY = m_inputScrollDiag.settleY;
                    settleEvent.targetY = m_inputScrollDiag.targetY;
                    settleEvent.didScroll = m_inputScrollDiag.didScroll;
                    if (m_inputScrollDiag.didScroll
                        && !qFuzzyCompare(m_inputScrollDiag.settleY + 1.0,
                                          m_inputScrollDiag.targetY + 1.0)) {
                        ++m_inputScrollDiag.overrideCount;
                        m_inputScrollDiag.overrideDetected = true;
                        settleEvent.curY = m_inputScrollDiag.settleY;
                    }
                    recordInputScrollEvent(settleEvent);
                }
            });
        } else if (documentLength < m_inputPreTextLength
                   && currentY >= maximumY - 0.5
                   && m_inputPreScrollY > maximumY + 0.5) {
            // 删除/撤销/重做使内容收缩时，Flickable 会把视口自动下钳制到新的最大位置，
            // 导致末尾光标始终停在上 1/3、永远不触顶（镜像触发失效）。
            // 这里恢复输入前的视口位置（保持不动），让光标随删除自然上移，
            // 越过顶边时再由顶规则间歇触发。撤销/重做视为删除类编辑，
            // 与删除共用同一套保持与顶镜像规则。
            // 弹性底部缓冲已在编辑前开启，contentY 从未离开输入前位置；
            // 保持该缓冲，直到光标触顶或用户主动滚动。
            m_inputScrollDiag.heldY = m_inputPreScrollY;
            event.heldY = m_inputPreScrollY;
        } else if (m_window) {
            // 本次编辑不需要把自然边界之外的视口保持下来；恢复进入本轮
            // 跟踪前的状态，避免临时缓冲泄漏到后续普通滚动。
            m_window->setProperty("inputScrollHoldBottom", m_inputScrollHoldWasActive);
        }
    } else if (m_window) {
        m_window->setProperty("inputScrollHoldBottom", m_inputScrollHoldWasActive);
    }
    m_inputAutoScrollTrackingActive = false;
    m_inputScrollHoldWasActive = false;
    m_inputPreTextLength = -1;
    recordInputScrollEvent(event);
}

void EditorCommandRegistry::recordInputScrollEvent(InputScrollEvent event)
{
    event.seq = ++m_inputScrollEventSeq;
    if (m_inputScrollEvents.size() >= 64) {
        m_inputScrollEvents.removeFirst();
    }
    m_inputScrollEvents.append(std::move(event));
}

QVariantMap EditorCommandRegistry::inputScrollDiagnostics() const
{
    const InputScrollDiagnostics &d = m_inputScrollDiag;
    QVariantMap result{
        {QStringLiteral("LastKind"), d.lastKind},
        {QStringLiteral("InputCount"), static_cast<qint64>(d.inputCount)},
        {QStringLiteral("CheckCount"), static_cast<qint64>(d.checkCount)},
        {QStringLiteral("TriggerCount"), static_cast<qint64>(d.triggerCount)},
        {QStringLiteral("OverrideCount"), static_cast<qint64>(d.overrideCount)},
        {QStringLiteral("EarlyReturnCount"),
         static_cast<qint64>(m_inputScrollEarlyReturnCount)},
        {QStringLiteral("CheckQueued"), m_inputAutoScrollCheckQueued},
        {QStringLiteral("CurrentY"), d.currentY},
        {QStringLiteral("CursorTop"), d.cursorTop},
        {QStringLiteral("CursorBottom"), d.cursorBottom},
        {QStringLiteral("ViewportHeight"), d.viewportHeight},
        {QStringLiteral("MaxY"), d.maxY},
        {QStringLiteral("TargetY"), d.targetY},
        {QStringLiteral("SettleY"), d.settleY},
        {QStringLiteral("HeldY"), d.heldY},
        {QStringLiteral("PreLength"), d.preLength},
        {QStringLiteral("DocLength"), d.docLength},
        {QStringLiteral("CursorPosition"), d.cursorPosition},
        {QStringLiteral("AtStart"), d.atStart},
        {QStringLiteral("AtEnd"), d.atEnd},
        {QStringLiteral("TouchedTop"), d.touchedTop},
        {QStringLiteral("TouchedBottom"), d.touchedBottom},
        {QStringLiteral("DidScroll"), d.didScroll},
        {QStringLiteral("OverrideDetected"), d.overrideDetected},
    };
    QVariantList history;
    history.reserve(m_inputScrollEvents.size());
    for (const InputScrollEvent &item : m_inputScrollEvents) {
        history.append(QVariantMap{
            {QStringLiteral("seq"), static_cast<qint64>(item.seq)},
            {QStringLiteral("type"), item.type},
            {QStringLiteral("kind"), item.kind},
            {QStringLiteral("curY"), item.curY},
            {QStringLiteral("curBottom"), item.curBottom},
            {QStringLiteral("vh"), item.vh},
            {QStringLiteral("maxY"), item.maxY},
            {QStringLiteral("targetY"), item.targetY},
            {QStringLiteral("settleY"), item.settleY},
            {QStringLiteral("heldY"), item.heldY},
            {QStringLiteral("preLen"), item.preLen},
            {QStringLiteral("docLen"), item.docLen},
            {QStringLiteral("cursorPos"), item.cursorPos},
            {QStringLiteral("atStart"), item.atStart},
            {QStringLiteral("atEnd"), item.atEnd},
            {QStringLiteral("touchedTop"), item.touchedTop},
            {QStringLiteral("touchedBottom"), item.touchedBottom},
            {QStringLiteral("didScroll"), item.didScroll},
            {QStringLiteral("earlyReturn"), item.earlyReturn},
        });
    }
    result.insert(QStringLiteral("History"), history);
    return result;
}

bool EditorCommandRegistry::performUndo()
{
    if (!m_editor) {
        return false;
    }
    std::optional<SelectionUndoSnapshot> selectionSnapshot;
    if (m_selectionUndoSnapshot
        && m_documentTextSnapshot == m_selectionUndoSnapshot->formattedText) {
        selectionSnapshot = m_selectionUndoSnapshot;
    }
    m_selectionUndoSnapshot.reset();
    // 撤销视为一次普通编辑：可能是删除也可能是输入，不预设方向，
    // 撤销后光标落在哪条视口边就按哪条规则处理。
    beginInputAutoScrollTracking(QStringLiteral("undo"));
    const bool invoked = QMetaObject::invokeMethod(m_editor, "undo");
    if (selectionSnapshot
        && m_documentTextSnapshot == selectionSnapshot->originalText) {
        const int activeEnd = selectionSnapshot->cursorPosition
                == selectionSnapshot->selectionEnd
            ? selectionSnapshot->selectionEnd
            : selectionSnapshot->selectionStart;
        selectRangeWithActiveEnd(selectionSnapshot->selectionStart,
                                 selectionSnapshot->selectionEnd, activeEnd);
        focusEditor();
    }
    queueInputAutoScrollCheck();
    return invoked;
}

bool EditorCommandRegistry::insertPathText(const QString &text)
{
    if (!m_editor || !m_document || text.isEmpty()
        || m_editor->property("readOnly").toBool()
        || m_editor->property("inputMethodComposing").toBool()) {
        return false;
    }

    const QString beforeText = m_documentTextSnapshot;
    const int selectionStart = m_editor->property("selectionStart").toInt();
    const int selectionEnd = m_editor->property("selectionEnd").toInt();
    const int cursorPosition = m_editor->property("cursorPosition").toInt();
    if (selectionStart < 0 || selectionEnd < selectionStart
        || selectionEnd > beforeText.size()) {
        return false;
    }

    const bool repairOrderedList = orderedListStructureAffected(
        beforeText, selectionStart, selectionEnd);
    beginInputAutoScrollTracking(QStringLiteral("drop"));
    QTextCursor editCursor(m_document);
    editCursor.beginEditBlock();
    editCursor.setPosition(selectionStart);
    editCursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    editCursor.insertText(text);
    const int cursorAfter = selectionStart + text.size();
    m_editor->setProperty("cursorPosition", cursorAfter);
    selectRange(cursorAfter, cursorAfter);
    if (repairOrderedList) {
        // 与粘贴、剪切等结构编辑共用一次编辑后全文读取和编号修复。
        repairOrderedLists(beforeText, m_document->toPlainText(), false);
    }
    editCursor.endEditBlock();

    m_selectionUndoSnapshot = SelectionUndoSnapshot{
        beforeText, m_documentTextSnapshot, selectionStart, selectionEnd, cursorPosition};
    focusEditor();
    queueInputAutoScrollCheck();
    return true;
}

bool EditorCommandRegistry::performRedo()
{
    if (!m_editor) {
        return false;
    }
    // 重做视为一次普通编辑：可能是输入也可能是删除，不预设方向，
    // 重做后光标落在哪条视口边就按哪条规则处理。
    beginInputAutoScrollTracking(QStringLiteral("redo"));
    const bool invoked = QMetaObject::invokeMethod(m_editor, "redo");
    queueInputAutoScrollCheck();
    return invoked;
}

bool EditorCommandRegistry::moveByCjkAwareWord(bool left, bool keepSelection)
{
    if (!m_editor || !m_document) {
        return false;
    }

    const int position = m_editor->property("cursorPosition").toInt();
    const int selectionStart = m_editor->property("selectionStart").toInt();
    const int selectionEnd = m_editor->property("selectionEnd").toInt();
    const bool hasSelection = selectionStart != selectionEnd;

    QTextCursor native(m_document);
    native.setPosition(position);
    const QTextCursor::MoveOperation operation =
        left ? QTextCursor::WordLeft : QTextCursor::WordRight;
    if (!native.movePosition(operation)) {
        // 已到文档边界：光标不动；无 Shift 时按 Qt 原生行为收起选区。
        if (!keepSelection && hasSelection) {
            selectRange(position, position);
        }
        focusEditor();
        return true;
    }

    const int nativeEnd = native.position();
    const int lo = qMin(position, nativeEnd);
    const int hi = qMax(position, nativeEnd);
    const QString text = m_document->toPlainText();
    const int target = CjkText::spanContainsCjk(text, lo, hi)
        ? CjkText::moveWordBoundary(text, position, left ? -1 : 1)
        : nativeEnd;

    if (keepSelection) {
        // 锚点固定在不随 active end 移动的一端，与 QTextCursor::KeepAnchor 一致。
        const int anchor = hasSelection
            ? (position == selectionStart ? selectionEnd : selectionStart)
            : position;
        selectRangeWithActiveEnd(qMin(anchor, target), qMax(anchor, target),
                                 target);
    } else {
        selectRange(target, target);
    }
    focusEditor();
    return true;
}

bool EditorCommandRegistry::deleteByCjkAwareWord(bool backwards)
{
    if (!m_editor || !m_document
        || m_editor->property("readOnly").toBool()) {
        return false;
    }

    const int position = m_editor->property("cursorPosition").toInt();
    QTextCursor native(m_document);
    native.setPosition(position);
    const QTextCursor::MoveOperation operation =
        backwards ? QTextCursor::PreviousWord : QTextCursor::NextWord;
    if (!native.movePosition(operation, QTextCursor::KeepAnchor)) {
        // 文档边界：交给原生（无操作）。
        return false;
    }
    const QString text = m_document->toPlainText();
    if (!CjkText::spanContainsCjk(text, native.selectionStart(),
                                  native.selectionEnd())) {
        // 纯 ASCII 跨度：交给原生，保持逐字一致（含分隔符与换行细节）。
        return false;
    }

    const CjkText::WordRange range =
        CjkText::wordDeletionRange(text, position, backwards);
    if (range.start >= range.end) {
        return false;
    }
    QTextCursor edit(m_document);
    edit.beginEditBlock();
    edit.setPosition(range.start);
    edit.setPosition(range.end, QTextCursor::KeepAnchor);
    edit.removeSelectedText();
    edit.endEditBlock();
    m_editor->setProperty("cursorPosition", range.start);
    focusEditor();
    return true;
}

bool EditorCommandRegistry::handleCjkDoubleClick(QMouseEvent *event)
{
    if (!m_editor || !m_document) {
        return false;
    }

    const int position = editorPositionAt(event->position());
    if (position < 0) {
        return false;
    }
    const CjkText::WordRange range =
        CjkText::wordRangeAt(m_document->toPlainText(), position);
    if (range.start >= range.end) {
        return false;
    }

    if (m_doubleClickReplaying) {
        // 重放副本直接放行给 QML 原生，保持“双击→三击”状态机。
        return false;
    }

    QMouseEvent replay(event->type(), event->position(), event->scenePosition(),
                       event->globalPosition(), event->button(), event->buttons(),
                       event->modifiers(), event->source(), event->pointingDevice());
    replay.setTimestamp(event->timestamp());
    m_doubleClickReplaying = true;
    QCoreApplication::sendEvent(m_editor, &replay);
    m_doubleClickReplaying = false;

    selectRange(range.start, range.end);
    event->accept();
    return true;
}

bool EditorCommandRegistry::moveSelection(int selectionStart, int selectionEnd,
                                          int dropPosition)
{
    if (!m_editor || !m_document) {
        return false;
    }

    const QString beforeText = m_documentTextSnapshot;
    const int documentLength = beforeText.size();
    if (selectionStart < 0 || selectionEnd < 0 || dropPosition < 0
        || selectionStart > documentLength || selectionEnd > documentLength
        || dropPosition > documentLength) {
        return false;
    }
    if (selectionStart >= selectionEnd
        || (dropPosition >= selectionStart && dropPosition <= selectionEnd)) {
        return false;
    }
    const bool repairOrderedList = orderedListStructureAffected(
        beforeText, selectionStart, selectionEnd)
        || orderedListStructureAffected(beforeText, dropPosition, dropPosition);

    QTextCursor cursor(m_document);
    cursor.setPosition(selectionStart);
    cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    const QString movedText = normalizeSelectedText(cursor.selectedText());
    const int adjustedDrop = dropPosition > selectionEnd
        ? dropPosition - (selectionEnd - selectionStart)
        : dropPosition;

    cursor.beginEditBlock();
    cursor.removeSelectedText();
    cursor.setPosition(adjustedDrop);
    cursor.insertText(movedText);
    selectRange(adjustedDrop, adjustedDrop + movedText.size());
    if (repairOrderedList) {
        repairOrderedLists(beforeText, m_document->toPlainText(), true);
    }
    cursor.endEditBlock();
    focusEditor();
    return true;
}

bool EditorCommandRegistry::insertExternalText(const QString &text, int dropPosition)
{
    if (!m_editor || !m_document || text.isEmpty()
        || m_editor->property("readOnly").toBool()) {
        return false;
    }

    const QString beforeText = m_documentTextSnapshot;
    if (dropPosition < 0 || dropPosition > beforeText.size()) {
        return false;
    }
    const int selectionStart = m_editor->property("selectionStart").toInt();
    const int selectionEnd = m_editor->property("selectionEnd").toInt();
    const int cursorPosition = m_editor->property("cursorPosition").toInt();

    const bool repairOrderedList = text.contains(QLatin1Char('\n'))
        || orderedListStructureAffected(beforeText, dropPosition, dropPosition);
    QTextCursor cursor(m_document);
    cursor.setPosition(dropPosition);
    // 先定位临时 cursor 再开启撤销块，避免 Qt 把构造时的文档开头
    // 记录为撤销位置；原有选区及活动端由统一快照完整恢复。
    cursor.beginEditBlock();
    cursor.insertText(text);
    // QTextDocument 会把 CRLF 等外部换行规范化为文档段落；使用插入后
    // cursor 的真实位置，避免按源字符串长度计算时把后续正文也选中。
    selectRange(dropPosition, cursor.position());
    if (repairOrderedList) {
        repairOrderedLists(beforeText, m_document->toPlainText(), true);
    }
    cursor.endEditBlock();
    m_selectionUndoSnapshot = SelectionUndoSnapshot{
        beforeText, m_documentTextSnapshot, selectionStart, selectionEnd, cursorPosition};
    focusEditor();
    return true;
}

bool EditorCommandRegistry::beginExternalTextDrag(const QString &text,
                                                  const QPointF &scenePosition)
{
    if (!m_editor || !m_document || text.isEmpty()
        || m_editor->property("readOnly").toBool()
        || m_editor->property("inputMethodComposing").toBool()) {
        return false;
    }

    resetSelectionDrag(true);
    resetExternalTextDrag();
    m_externalDragText = text;
    m_externalDragPressScenePosition = scenePosition;
    m_externalDragScenePosition = scenePosition;
    return true;
}

bool EditorCommandRegistry::updateExternalTextDrag(const QPointF &scenePosition)
{
    if (m_externalDragText.isEmpty()) {
        return false;
    }

    m_externalDragScenePosition = scenePosition;
    if (!m_externalDragActive) {
        const qreal distance = (scenePosition
                                - m_externalDragPressScenePosition).manhattanLength();
        if (distance < QGuiApplication::styleHints()->startDragDistance()) {
            return false;
        }
        m_externalDragActive = true;
        m_selectionDragScrollTimer.start();
    }

    updateExternalTextDragPosition(scenePosition, true);
    updateExternalTextDragCursor(m_externalDropPosition >= 0);
    return m_externalDropPosition >= 0;
}

bool EditorCommandRegistry::finishExternalTextDrag(const QPointF &scenePosition)
{
    if (m_externalDragText.isEmpty()) {
        return false;
    }

    if (m_externalDragActive) {
        updateExternalTextDragPosition(scenePosition, false);
    }
    const bool wasActive = m_externalDragActive;
    const int dropPosition = m_externalDropPosition;
    const QString text = m_externalDragText;
    resetExternalTextDrag();
    return wasActive && dropPosition >= 0
        && insertExternalText(text, dropPosition);
}

void EditorCommandRegistry::cancelExternalTextDrag()
{
    resetExternalTextDrag();
}

bool EditorCommandRegistry::externalTextDragActive() const
{
    return m_externalDragActive;
}

bool EditorCommandRegistry::externalTextDragCursorOverridden() const
{
    return m_externalDragCursorOverridden;
}

int EditorCommandRegistry::externalTextDragCursorShape() const
{
    return static_cast<int>(m_externalDragCursorShape);
}

bool EditorCommandRegistry::handleSelectionDragEvent(QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const Qt::KeyboardModifiers selectionModifiers = Qt::ShiftModifier
            | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
        if (mouseEvent->button() != Qt::LeftButton
            || mouseEvent->modifiers().testAnyFlags(selectionModifiers)
            || m_editor->property("readOnly").toBool()
            || m_editor->property("inputMethodComposing").toBool()) {
            return false;
        }

        const int selectionStart = m_editor->property("selectionStart").toInt();
        const int selectionEnd = m_editor->property("selectionEnd").toInt();
        const int pressPosition = editorPositionAt(mouseEvent->position());
        if (selectionStart >= selectionEnd || pressPosition < selectionStart
            || pressPosition > selectionEnd) {
            return false;
        }

        beginSelectionDrag(selectionStart, selectionEnd, mouseEvent->scenePosition());
        event->accept();
        return true;
    }

    const bool dragPending = m_selectionDragStart >= 0;
    if (!dragPending) {
        return false;
    }

    if (event->type() == QEvent::MouseMove) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (!mouseEvent->buttons().testFlag(Qt::LeftButton)) {
            resetSelectionDrag(true);
            return false;
        }

        m_selectionDragScenePosition = mouseEvent->scenePosition();
        if (!m_selectionDragActive) {
            const qreal distance = (m_selectionDragScenePosition
                                    - m_selectionDragPressScenePosition).manhattanLength();
            const int threshold = QGuiApplication::styleHints()->startDragDistance();
            if (distance < threshold) {
                event->accept();
                return true;
            }
            m_selectionDragActive = true;
            if (QQuickItem *item = editorItem()) {
                m_selectionDragOriginalCursor = item->cursor();
            }
            m_selectionDragScrollTimer.start();
        }

        updateSelectionDrag(m_selectionDragScenePosition, true);
        event->accept();
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton) {
            return false;
        }

        const bool movedFarEnough = m_selectionDragActive;
        const int selectionStart = m_selectionDragStart;
        const int selectionEnd = m_selectionDragEnd;
        const int clickPosition = editorPositionAt(mouseEvent->position());
        if (movedFarEnough) {
            updateSelectionDrag(mouseEvent->scenePosition(), false);
        }
        const int dropPosition = m_selectionDropPosition;
        resetSelectionDrag(false);

        if (movedFarEnough) {
            if (dropPosition >= 0) {
                moveSelection(selectionStart, selectionEnd, dropPosition);
            } else {
                selectRange(selectionStart, selectionEnd);
                focusEditor();
            }
        } else {
            m_editor->setProperty("cursorPosition", clickPosition);
            focusEditor();
        }
        event->accept();
        return true;
    }

    if (event->type() == QEvent::KeyPress) {
        const auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            resetSelectionDrag(true);
            return true;
        }
    }

    if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::FocusOut) {
        resetSelectionDrag(false);
    }
    return false;
}

int EditorCommandRegistry::editorPositionAt(const QPointF &localPosition) const
{
    if (!m_editor || !m_document) {
        return -1;
    }

    int position = -1;
    const bool invoked = QMetaObject::invokeMethod(
        m_editor, "positionAt", Qt::DirectConnection, Q_RETURN_ARG(int, position),
        Q_ARG(double, localPosition.x()), Q_ARG(double, localPosition.y()));
    return invoked ? qBound(0, position, m_documentTextSnapshot.size()) : -1;
}

int EditorCommandRegistry::visibleEditorPositionAt(const QPointF &scenePosition) const
{
    QQuickItem *item = editorItem();
    QQuickItem *viewport = editorViewport();
    if (!item || !viewport) {
        return -1;
    }

    const QPointF viewportPosition = viewport->mapFromScene(scenePosition);
    const QPointF editorPosition = item->mapFromScene(scenePosition);
    if (!viewport->contains(viewportPosition) || !item->contains(editorPosition)) {
        return -1;
    }
    // 窄窗口中历史面板覆盖在编辑器上方；被面板遮住的 TextEdit 区域
    // 不能作为有效落点，否则在卡片尚未拖出面板时就可能误插入。
    if (m_window && m_window->property("historyPanelOpen").toBool()
        && m_window->property("historyPanelOverlay").toBool()) {
        const QPointF viewportSceneTopLeft = viewport->mapToScene(QPointF());
        const qreal panelRight = viewportSceneTopLeft.x()
            + m_window->property("historyPanelWidth").toReal();
        if (scenePosition.x() < panelRight) {
            return -1;
        }
    }
    return editorPositionAt(editorPosition);
}

QQuickItem *EditorCommandRegistry::editorItem() const
{
    return qobject_cast<QQuickItem *>(m_editor.data());
}

QQuickItem *EditorCommandRegistry::editorViewport() const
{
    QQuickItem *item = editorItem();
    for (QQuickItem *candidate = item ? item->parentItem() : nullptr;
         candidate; candidate = candidate->parentItem()) {
        if (candidate->property("contentY").isValid()
            && candidate->property("contentHeight").isValid()) {
            return candidate;
        }
    }
    return nullptr;
}

void EditorCommandRegistry::animateViewportScrollTo(QQuickItem *viewport, qreal targetY,
                                                     bool releaseInputHold)
{
    // 优先走 QML 侧轻量动画（动画开关关闭时直接落位）；
    // 若 QML 函数不可用（异常场景）则回退为瞬时滚动，保持原有行为。
    if (!m_window) {
        viewport->setProperty("contentY", targetY);
        return;
    }
    m_window->setProperty("requestedScrollY", targetY);
    m_window->setProperty("releaseInputScrollHoldAfterAnimation", releaseInputHold);
    if (!QMetaObject::invokeMethod(m_window, "animateScrollTo")) {
        viewport->setProperty("contentY", targetY);
        if (releaseInputHold) {
            m_window->setProperty("inputScrollHoldBottom", false);
            m_window->setProperty("releaseInputScrollHoldAfterAnimation", false);
        }
    }
}

bool EditorCommandRegistry::navigateToHeading(bool backwards)
{
    if (!m_headingFolds) {
        return false;
    }
    // 跳转期间抑制 QML 侧的光标瞬时贴边跟随，让整段跳转由稍后同一动画入口
    // 的平滑滚动完成，避免“先瞬移贴边、再动画”的两段式观感。
    const bool suppress = m_window != nullptr;
    if (suppress) {
        m_window->setProperty("suppressHeadingCursorFollow", true);
    }
    const int target = m_headingFolds->navigate(backwards);
    if (suppress) {
        m_window->setProperty("suppressHeadingCursorFollow", false);
    }
    if (target >= 0) {
        scheduleHeadingScroll(target);
    }
    // 与既有语义一致：命令本身总是视为已执行（边界无跳转时返回 true）。
    return true;
}

void EditorCommandRegistry::scheduleHeadingScroll(int position)
{
    m_pendingHeadingScrollPosition = position;
    // 目标标题可能刚从折叠祖先中展开，文档布局要等下一帧 polish 才落定
    // （与输入自动滚动同一约定）；延迟后重新计算目标并动画/瞬时落位。
    m_headingScrollTimer.start(layoutSettleDelayMs);
}

void EditorCommandRegistry::scrollViewportToHeading(int position)
{
    QQuickItem *viewport = editorViewport();
    QQuickItem *item = editorItem();
    if (!viewport || !item || !m_editor) {
        return;
    }
    QRectF headingRect;
    const bool rectLocated = QMetaObject::invokeMethod(
        m_editor, "positionToRectangle", Qt::DirectConnection,
        Q_RETURN_ARG(QRectF, headingRect), Q_ARG(int, position));
    if (!rectLocated || !headingRect.isValid()) {
        return;
    }
    const qreal viewportHeight = viewport->height();
    const qreal maximumY = qMax<qreal>(
        0.0, viewport->property("contentHeight").toReal() - viewportHeight);
    // 与输入触底自动滚动同一约定：标题首行锚定到视口上 1/3。
    const qreal targetY = qBound<qreal>(
        0.0, item->y() + headingRect.y() - viewportHeight / 3.0, maximumY);
    animateViewportScrollTo(viewport, targetY);
}

void EditorCommandRegistry::beginSelectionDrag(int selectionStart, int selectionEnd,
                                                const QPointF &scenePosition)
{
    resetExternalTextDrag();
    m_selectionDragStart = selectionStart;
    m_selectionDragEnd = selectionEnd;
    m_selectionDropPosition = -1;
    m_selectionDragPressScenePosition = scenePosition;
    m_selectionDragScenePosition = scenePosition;
    m_selectionDragActive = false;

    focusEditor();
    if (QQuickItem *item = editorItem()) {
        m_selectionDragPreviousKeepMouseGrab = item->keepMouseGrab();
        item->setKeepMouseGrab(true);
        item->grabMouse();
    }
}

void EditorCommandRegistry::updateSelectionDrag(const QPointF &scenePosition,
                                                bool scrollViewport)
{
    if (!m_selectionDragActive || !m_editor) {
        return;
    }

    m_selectionDragScenePosition = scenePosition;
    if (scrollViewport) {
        scrollTextDragViewport(scenePosition);
    }

    QQuickItem *item = editorItem();
    if (!item) {
        return;
    }
    const int position = editorPositionAt(item->mapFromScene(scenePosition));
    const bool validDrop = position >= 0
        && (position < m_selectionDragStart || position > m_selectionDragEnd);
    m_selectionDropPosition = validDrop ? position : -1;
    m_editor->setProperty("selectionDragPosition", m_selectionDropPosition);
    item->setCursor(QCursor(validDrop ? Qt::DragMoveCursor : Qt::ForbiddenCursor));
}

void EditorCommandRegistry::resetSelectionDrag(bool releaseMouseGrab)
{
    m_selectionDragScrollTimer.stop();
    QQuickItem *item = editorItem();
    const bool wasActive = m_selectionDragActive;
    const bool previousKeepMouseGrab = m_selectionDragPreviousKeepMouseGrab;
    const QCursor originalCursor = m_selectionDragOriginalCursor;

    m_selectionDragStart = -1;
    m_selectionDragEnd = -1;
    m_selectionDropPosition = -1;
    m_selectionDragActive = false;
    m_selectionDragPreviousKeepMouseGrab = false;

    if (m_editor) {
        m_editor->setProperty("selectionDragPosition", -1);
    }
    if (item) {
        if (wasActive) {
            item->setCursor(originalCursor);
        }
        item->setKeepMouseGrab(previousKeepMouseGrab);
        if (releaseMouseGrab) {
            item->ungrabMouse();
        }
    }
}

void EditorCommandRegistry::updateExternalTextDragPosition(
    const QPointF &scenePosition, bool scrollViewport)
{
    if (!m_externalDragActive || !m_editor) {
        return;
    }

    m_externalDragScenePosition = scenePosition;
    if (scrollViewport) {
        scrollTextDragViewport(scenePosition);
    }
    m_externalDropPosition = visibleEditorPositionAt(scenePosition);
    m_editor->setProperty("selectionDragPosition", m_externalDropPosition);
}

void EditorCommandRegistry::updateExternalTextDragCursor(bool canDrop)
{
    const Qt::CursorShape shape = canDrop ? Qt::DragCopyCursor : Qt::ForbiddenCursor;
    if (!m_externalDragCursorOverridden) {
        QGuiApplication::setOverrideCursor(QCursor(shape));
        m_externalDragCursorOverridden = true;
    } else if (m_externalDragCursorShape != shape) {
        QGuiApplication::changeOverrideCursor(QCursor(shape));
    }
    m_externalDragCursorShape = shape;
}

void EditorCommandRegistry::resetExternalTextDrag()
{
    m_selectionDragScrollTimer.stop();
    if (m_externalDragCursorOverridden) {
        QGuiApplication::restoreOverrideCursor();
    }
    m_externalDragText.clear();
    m_externalDragPressScenePosition = {};
    m_externalDragScenePosition = {};
    m_externalDropPosition = -1;
    m_externalDragActive = false;
    m_externalDragCursorOverridden = false;
    m_externalDragCursorShape = Qt::ArrowCursor;
    if (m_editor) {
        m_editor->setProperty("selectionDragPosition", -1);
    }
}

void EditorCommandRegistry::scrollTextDragViewport(const QPointF &scenePosition)
{
    QQuickItem *viewport = editorViewport();
    if (!viewport) {
        return;
    }

    const QPointF viewportPosition = viewport->mapFromScene(scenePosition);
    const qreal viewportHeight = viewport->height();
    const qreal edge = qMin<qreal>(32.0, viewportHeight / 4.0);
    const qreal currentY = viewport->property("contentY").toReal();
    const qreal maximumY = qMax<qreal>(
        0.0, viewport->property("contentHeight").toReal() - viewportHeight);
    qreal scrollDelta = 0.0;
    if (viewportPosition.x() >= -edge
        && viewportPosition.x() <= viewport->width() + edge) {
        if (viewportPosition.y() < edge) {
            scrollDelta = -qBound<qreal>(
                2.0, (edge - viewportPosition.y()) * 0.5, 24.0);
        } else if (viewportPosition.y() > viewportHeight - edge) {
            scrollDelta = qBound<qreal>(
                2.0, (viewportPosition.y() - viewportHeight + edge) * 0.5, 24.0);
        }
    }
    const qreal requestedY = qBound<qreal>(0.0, currentY + scrollDelta, maximumY);
    if (!qFuzzyCompare(requestedY + 1.0, currentY + 1.0)) {
        viewport->setProperty("contentY", requestedY);
    }
}

bool EditorCommandRegistry::findNext(const QString &query, bool caseSensitive, bool backwards)
{
    if (!m_editor || !m_document || query.isEmpty()) {
        return false;
    }

    const int selectionStart = m_editor->property("selectionStart").toInt();
    const int selectionEnd = m_editor->property("selectionEnd").toInt();
    QTextCursor start(m_document);
    start.setPosition(backwards ? selectionStart : selectionEnd);
    const QTextDocument::FindFlags flags = findFlags(caseSensitive, backwards);
    QTextCursor found = m_document->find(query, start, flags);
    if (found.isNull()) {
        start.setPosition(backwards ? m_document->characterCount() - 1 : 0);
        found = m_document->find(query, start, flags);
    }
    if (found.isNull()) {
        return false;
    }
    m_headingFolds->revealPosition(found.selectionStart());
    selectRange(found.selectionStart(), found.selectionEnd());
    focusEditor();
    return true;
}

bool EditorCommandRegistry::replaceCurrent(const QString &query, const QString &replacement,
                                           bool caseSensitive)
{
    if (!m_editor || !m_document || query.isEmpty()) {
        return false;
    }

    const Qt::CaseSensitivity sensitivity = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (QString::compare(selectedText(), query, sensitivity) != 0) {
        return findNext(query, caseSensitive, false);
    }

    const QString beforeText = m_documentTextSnapshot;
    const int selectionStart = m_editor->property("selectionStart").toInt();
    const int selectionEnd = m_editor->property("selectionEnd").toInt();
    const bool markerEdit = orderedListStructureAffected(
        beforeText, selectionStart, selectionEnd);
    QTextCursor cursor(m_document);
    cursor.setPosition(selectionStart);
    cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    const int insertedAt = cursor.selectionStart();
    cursor.beginEditBlock();
    cursor.insertText(replacement);
    selectRange(insertedAt, insertedAt + replacement.size());
    const bool structural = query.contains(QLatin1Char('\n'))
        || replacement.contains(QLatin1Char('\n'));
    if (markerEdit || structural) {
        repairOrderedLists(beforeText, m_document->toPlainText(), structural);
    }
    cursor.endEditBlock();
    return true;
}

int EditorCommandRegistry::replaceAll(const QString &query, const QString &replacement,
                                      bool caseSensitive)
{
    if (!m_document || query.isEmpty()) {
        return 0;
    }

    const QString beforeText = m_documentTextSnapshot;
    const bool structural = query.contains(QLatin1Char('\n'))
        || replacement.contains(QLatin1Char('\n'));
    bool repairOrderedList = structural;
    if (!repairOrderedList) {
        int matchStart = beforeText.indexOf(query, 0,
            caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
        while (matchStart >= 0) {
            if (orderedListStructureAffected(
                    beforeText, matchStart, matchStart + query.size())) {
                repairOrderedList = true;
                break;
            }
            matchStart = beforeText.indexOf(query, matchStart + query.size(),
                caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
        }
    }
    int replacements = 0;
    QTextCursor editCursor(m_document);
    editCursor.beginEditBlock();
    QTextCursor searchFrom(m_document);
    searchFrom.movePosition(QTextCursor::Start);
    while (true) {
        QTextCursor found = m_document->find(query, searchFrom, findFlags(caseSensitive));
        if (found.isNull()) {
            break;
        }
        const int nextPosition = found.selectionStart() + replacement.size();
        found.insertText(replacement);
        ++replacements;
        searchFrom.setPosition(std::min(nextPosition, m_document->characterCount() - 1));
    }
    if (replacements > 0 && repairOrderedList) {
        repairOrderedLists(beforeText, m_document->toPlainText(), structural);
    }
    editCursor.endEditBlock();
    focusEditor();
    return replacements;
}

EditorCommandRegistry::Definition *EditorCommandRegistry::definition(const QString &commandId)
{
    auto found = std::find_if(m_definitions.begin(), m_definitions.end(),
                              [&commandId](const Definition &item) { return item.id == commandId; });
    return found == m_definitions.end() ? nullptr : &*found;
}

const EditorCommandRegistry::Definition *
EditorCommandRegistry::definition(const QString &commandId) const
{
    auto found = std::find_if(m_definitions.cbegin(), m_definitions.cend(),
                              [&commandId](const Definition &item) { return item.id == commandId; });
    return found == m_definitions.cend() ? nullptr : &*found;
}

bool EditorCommandRegistry::wrapSelection(const QString &opening, const QString &closing)
{
    const int originalStart = m_editor->property("selectionStart").toInt();
    const int originalEnd = m_editor->property("selectionEnd").toInt();
    const bool hadSelection = originalStart != originalEnd;
    const QString documentText = m_document->toPlainText();
    const MarkupKind kind = markupKind(opening, closing);
    const QVector<MarkupSpan> spans = kind == MarkupKind::Generic
        ? genericMarkupSpans(documentText, opening, closing)
        : inlineMarkupSpans(documentText, kind);

    if (!hadSelection && originalStart >= opening.size()
        && documentText.mid(originalStart - opening.size(), opening.size()) == opening
        && documentText.mid(originalStart, closing.size()) == closing) {
        QTextCursor cursor(m_document);
        cursor.setPosition(originalStart - opening.size());
        cursor.setPosition(originalStart + closing.size(), QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        m_editor->setProperty("cursorPosition", originalStart - opening.size());
        focusEditor();
        return true;
    }

    if (const auto containing = containingMarkupSpan(spans, originalStart, originalEnd)) {
        QVector<MarkupMarker> removals{containing->opening, containing->closing};
        const int adjustedStart = positionAfterRemovals(originalStart, removals);
        const int adjustedEnd = positionAfterRemovals(originalEnd, removals);
        std::sort(removals.begin(), removals.end(), [](const auto &left, const auto &right) {
            return left.removeStart > right.removeStart;
        });
        QTextCursor editCursor(m_document);
        editCursor.beginEditBlock();
        for (const MarkupMarker &marker : removals) {
            QTextCursor removalCursor(m_document);
            removalCursor.setPosition(marker.removeStart);
            removalCursor.setPosition(marker.removeStart + marker.removeLength,
                                      QTextCursor::KeepAnchor);
            removalCursor.removeSelectedText();
        }
        editCursor.endEditBlock();
        if (hadSelection) {
            selectRange(adjustedStart, adjustedEnd);
        } else {
            m_editor->setProperty("cursorPosition", adjustedStart);
        }
        focusEditor();
        return true;
    }

    int rangeStart = originalStart;
    int rangeEnd = originalEnd;
    if (!hadSelection) {
        const TextRange wordRange = inferredWordRange(m_document, documentText, originalStart);
        if (wordRange.isValid()) {
            rangeStart = wordRange.start;
            rangeEnd = wordRange.end;
        }
    }

    QTextCursor cursor(m_document);
    cursor.setPosition(rangeStart);
    cursor.setPosition(rangeEnd, QTextCursor::KeepAnchor);
    QString text = normalizeSelectedText(cursor.selectedText());
    QVector<MarkupMarker> internalMarkers;
    if (hadSelection) {
        for (const MarkupSpan &span : spans) {
            for (const MarkupMarker &marker : {span.opening, span.closing}) {
                if (marker.removeStart >= rangeStart
                    && marker.removeStart + marker.removeLength <= rangeEnd) {
                    internalMarkers.append(marker);
                }
            }
        }
        std::sort(internalMarkers.begin(), internalMarkers.end(),
                  [](const auto &left, const auto &right) {
            return left.removeStart > right.removeStart;
        });
        for (const MarkupMarker &marker : internalMarkers) {
            text.remove(marker.removeStart - rangeStart, marker.removeLength);
        }
    }

    cursor.beginEditBlock();
    if (text.isEmpty()) {
        cursor.insertText(opening + closing);
        m_editor->setProperty("cursorPosition", rangeStart + opening.size());
    } else {
        cursor.insertText(opening + text + closing);
        if (hadSelection) {
            selectRange(rangeStart + opening.size(),
                        rangeStart + opening.size() + text.size());
        } else {
            int cursorOffset = originalStart - rangeStart;
            for (const MarkupMarker &marker : internalMarkers) {
                if (marker.removeStart < originalStart) {
                    cursorOffset -= marker.removeLength;
                }
            }
            m_editor->setProperty("cursorPosition",
                                  rangeStart + opening.size() + qMax(0, cursorOffset));
        }
    }
    cursor.endEditBlock();
    focusEditor();
    return true;
}

bool EditorCommandRegistry::transformSelectedLines(const QString &commandId)
{
    const bool setHeading = commandId.startsWith(QStringLiteral("setHeading"))
        && commandId.size() == QStringLiteral("setHeading1").size()
        && commandId.back() >= QLatin1Char('1') && commandId.back() <= QLatin1Char('6');
    const bool adjustHeading = commandId == QStringLiteral("increaseHeadingLevel")
        || commandId == QStringLiteral("decreaseHeadingLevel");
    const bool headingCommand = commandId == QStringLiteral("cycleHeading")
        || setHeading || adjustHeading;
    if (!headingCommand
        && commandId != QStringLiteral("toggleList")
        && commandId != QStringLiteral("toggleTask")
        && commandId != QStringLiteral("toggleQuote")) {
        return false;
    }

    const QString documentText = m_documentTextSnapshot;
    const int originalStart = m_editor->property("selectionStart").toInt();
    const int originalEnd = m_editor->property("selectionEnd").toInt();
    const int lineStart = lineRangeAt(documentText, originalStart).start;
    int lineEnd = documentText.indexOf(QLatin1Char('\n'), originalEnd);
    if (lineEnd < 0) {
        lineEnd = documentText.size();
    }
    const QString segment = documentText.mid(lineStart, lineEnd - lineStart);
    QStringList lines = segment.split(QLatin1Char('\n'), Qt::KeepEmptyParts);

    HeadingCommand heading;
    if (headingCommand) {
        const bool createFromEmpty = !adjustHeading && originalStart == originalEnd
            && lines.size() == 1;
        if (setHeading) {
            heading = {HeadingAction::Set, commandId.back().digitValue(), false,
                       createFromEmpty};
        } else if (commandId == QStringLiteral("increaseHeadingLevel")) {
            heading = {HeadingAction::Increase, 1, true, false};
        } else if (commandId == QStringLiteral("decreaseHeadingLevel")) {
            heading = {HeadingAction::Decrease, 1, true, false};
        } else {
            heading = {HeadingAction::Cycle, 1, false, createFromEmpty};
        }
    }

    const QRegularExpressionMatch originalHeadingMatch = headingCommand
        && originalStart == originalEnd
        ? headingPrefixPattern().match(segment)
        : QRegularExpressionMatch();
    const int originalHeadingPrefixLength = originalHeadingMatch.hasMatch()
        ? originalHeadingMatch.capturedLength() : 0;

    const auto allNonEmptyMatch = [&lines](const QRegularExpression &expression) {
        bool sawContent = false;
        for (const QString &line : lines) {
            if (line.trimmed().isEmpty()) {
                continue;
            }
            sawContent = true;
            if (!expression.match(line).hasMatch()) {
                return false;
            }
        }
        return sawContent;
    };

    const bool removeList = commandId == QStringLiteral("toggleList")
        && allNonEmptyMatch(listPrefixPattern());
    const bool removeTask = commandId == QStringLiteral("toggleTask")
        && allNonEmptyMatch(taskPrefixPattern());
    const bool removeQuote = commandId == QStringLiteral("toggleQuote")
        && allNonEmptyMatch(quotePrefixPattern());

    for (QString &line : lines) {
        if (headingCommand) {
            line = transformHeadingLine(line, heading);
        } else if (commandId == QStringLiteral("toggleList")) {
            line = transformListLine(line, removeList);
        } else if (commandId == QStringLiteral("toggleTask")) {
            line = transformTaskLine(line, removeTask);
        } else if (commandId == QStringLiteral("toggleQuote")) {
            line = transformQuoteLine(line, removeQuote);
        }
    }

    const QString transformed = lines.join(QLatin1Char('\n'));
    if (headingCommand && transformed == segment) {
        // 没有任何行被实际修改（例如标题推进命令落在普通文本行上），
        // 保持原有选区与光标不动，避免产生无意义的编辑块。
        focusEditor();
        return true;
    }
    QTextCursor cursor(m_document);
    cursor.setPosition(lineStart);
    cursor.setPosition(lineEnd, QTextCursor::KeepAnchor);
    cursor.beginEditBlock();
    cursor.insertText(transformed);
    if (headingCommand && originalStart == originalEnd) {
        const QRegularExpressionMatch transformedHeadingMatch =
            headingPrefixPattern().match(transformed);
        const int transformedHeadingPrefixLength = transformedHeadingMatch.hasMatch()
            ? transformedHeadingMatch.capturedLength() : 0;
        const int originalColumn = originalStart - lineStart;
        const int transformedColumn = originalColumn < originalHeadingPrefixLength
            ? transformedHeadingPrefixLength
            : transformedHeadingPrefixLength
                + originalColumn - originalHeadingPrefixLength;
        m_editor->setProperty("cursorPosition",
                              lineStart + qBound(0, transformedColumn,
                                                 static_cast<int>(transformed.size())));
    } else if (commandId == QStringLiteral("toggleQuote")) {
        m_editor->setProperty("cursorPosition", lineStart + transformed.size());
    } else {
        selectRange(lineStart, lineStart + transformed.size());
    }
    if (!headingCommand) {
        repairOrderedLists(documentText, m_document->toPlainText(), true);
    }
    cursor.endEditBlock();
    focusEditor();
    return true;
}

bool EditorCommandRegistry::convertToOrderedList()
{
    if (!m_editor || !m_document || m_editor->property("readOnly").toBool()) {
        return false;
    }

    const QString beforeText = m_documentTextSnapshot;
    const int originalStart = m_editor->property("selectionStart").toInt();
    const int originalEnd = m_editor->property("selectionEnd").toInt();
    const int originalCursor = m_editor->property("cursorPosition").toInt();
    OrderedListConversionPlan plan = orderedListConversionPlan(
        beforeText, originalStart, originalEnd);
    if (!plan.handled || plan.selectionStart < 0 || plan.selectionEnd < plan.selectionStart) {
        focusEditor();
        return true;
    }

    std::sort(plan.replacements.begin(), plan.replacements.end(),
              [](const TextReplacement &left, const TextReplacement &right) {
                  return left.start < right.start;
              });
    int transformedSelectionEnd = plan.selectionEnd;
    for (const TextReplacement &replacement : plan.replacements) {
        if (replacement.start < plan.selectionEnd) {
            transformedSelectionEnd += replacement.replacement.size() - replacement.length;
        }
    }

    if (plan.replacements.isEmpty()) {
        selectRange(plan.selectionStart, transformedSelectionEnd);
        focusEditor();
        return true;
    }

    QTextCursor editCursor(m_document);
    editCursor.beginEditBlock();
    for (auto replacement = plan.replacements.crbegin();
         replacement != plan.replacements.crend(); ++replacement) {
        QTextCursor cursor(m_document);
        cursor.setPosition(replacement->start);
        cursor.setPosition(replacement->start + replacement->length,
                           QTextCursor::KeepAnchor);
        cursor.insertText(replacement->replacement);
    }
    editCursor.endEditBlock();
    selectRange(plan.selectionStart, transformedSelectionEnd);
    m_selectionUndoSnapshot = SelectionUndoSnapshot{
        beforeText, m_documentTextSnapshot, originalStart, originalEnd, originalCursor};
    focusEditor();
    return true;
}

bool EditorCommandRegistry::deleteSelectedLines()
{
    const QString text = m_documentTextSnapshot;
    const int start = m_editor->property("selectionStart").toInt();
    const int end = m_editor->property("selectionEnd").toInt();
    const int lineStart = lineRangeAt(text, start).start;
    const int effectiveEnd = end > start ? end - 1 : end;
    const int followingNewline = text.indexOf(QLatin1Char('\n'), effectiveEnd);

    int removeStart = lineStart;
    int removeEnd = followingNewline >= 0 ? followingNewline + 1 : text.size();
    if (followingNewline < 0 && removeStart > 0) {
        --removeStart;
    }

    QTextCursor cursor(m_document);
    cursor.setPosition(removeStart);
    cursor.setPosition(removeEnd, QTextCursor::KeepAnchor);
    cursor.beginEditBlock();
    cursor.removeSelectedText();
    m_editor->setProperty("cursorPosition", removeStart);
    repairOrderedLists(text, m_document->toPlainText(), true);
    cursor.endEditBlock();
    focusEditor();
    return true;
}

bool EditorCommandRegistry::clearDocument()
{
    if (!m_editor || !m_document || m_editor->property("readOnly").toBool()) {
        return false;
    }

    // 整篇清空合并为一次撤销（一次 Ctrl+Z 可整体恢复原文）。
    QTextCursor cursor(m_document);
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.removeSelectedText();
    cursor.endEditBlock();
    selectRange(0, 0);
    focusEditor();
    return true;
}

bool EditorCommandRegistry::copyLine()
{
    if (!m_editor || !m_document || !m_clipboardWriter) {
        return false;
    }

    const QString text = m_documentTextSnapshot;
    const int cursor = m_editor->property("cursorPosition").toInt();
    const LineRange line = lineRangeAt(text, cursor);
    // 整行复制统一携带行尾换行符（末行/单行文档也补上），保证粘贴语义一致。
    m_clipboardWriter(text.mid(line.start, line.end - line.start) + QLatin1Char('\n'));
    focusEditor();
    return true;
}

bool EditorCommandRegistry::cutLine()
{
    if (!m_editor || !m_document || !m_clipboardWriter) {
        return false;
    }

    const QString text = m_documentTextSnapshot;
    const int cursor = m_editor->property("cursorPosition").toInt();
    const LineRange line = lineRangeAt(text, cursor);
    const bool lastLine = line.end == text.size();

    m_clipboardWriter(text.mid(line.start, line.end - line.start) + QLatin1Char('\n'));

    // 非末行删除整行含换行，后续行补位；末行只删行文本。
    QTextCursor editCursor(m_document);
    editCursor.beginEditBlock();
    editCursor.setPosition(line.start);
    editCursor.setPosition(lastLine ? line.end : line.end + 1, QTextCursor::KeepAnchor);
    editCursor.removeSelectedText();
    // 光标落在补位后一行的行首；剪切末行时落在上一行行尾。
    m_editor->setProperty("cursorPosition", line.start);
    repairOrderedLists(text, m_document->toPlainText(), true);
    editCursor.endEditBlock();
    focusEditor();
    return true;
}

bool EditorCommandRegistry::cutSelection()
{
    if (!m_editor || !m_document || !m_clipboardWriter) {
        return false;
    }
    const int start = m_editor->property("selectionStart").toInt();
    const int end = m_editor->property("selectionEnd").toInt();
    if (start == end) {
        return cutLine();
    }

    const QString beforeText = m_documentTextSnapshot;
    const bool repairOrderedList = orderedListStructureAffected(beforeText, start, end);
    QTextCursor cursor(m_document);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    if (!m_clipboardWriter(normalizeSelectedText(cursor.selectedText()))) {
        focusEditor();
        return false;
    }

    cursor.beginEditBlock();
    cursor.removeSelectedText();
    m_editor->setProperty("cursorPosition", start);
    if (repairOrderedList) {
        repairOrderedLists(beforeText, m_document->toPlainText(), true);
    }
    cursor.endEditBlock();
    focusEditor();
    return true;
}

bool EditorCommandRegistry::pasteClipboard()
{
    if (!m_editor || !m_document || !m_clipboardReader) {
        return false;
    }
    if (m_editor->property("readOnly").toBool()) {
        return false;
    }

    const QString clipboardText = m_clipboardReader();
    const QString text = m_document->toPlainText();
    const int selectionStart = m_editor->property("selectionStart").toInt();
    const int selectionEnd = m_editor->property("selectionEnd").toInt();
    const int cursorPosition = m_editor->property("cursorPosition").toInt();
    const bool smartLinePaste = selectionStart == selectionEnd
        && clipboardText.endsWith(QLatin1Char('\n'));

    int insertionPoint = selectionStart;
    QString insertion = clipboardText;
    if (smartLinePaste) {
        // 智能粘贴：剪贴板是“整行（以换行结尾）”时插入为当前行下方的新行，
        // 当前行原样保留；仅当当前行仍有内容（末行非空）时才先补一个换行。
        // 空文档、文档已以换行结尾（光标位于末尾空行）时直接插入，
        // 避免每次粘贴都新增一个前导空行。
        const LineRange line = lineRangeAt(text, selectionStart);
        const bool lastLine = line.end == text.size();
        if (lastLine) {
            if (line.start < line.end) {
                insertion.prepend(QLatin1Char('\n'));
            }
        }
        insertionPoint = lastLine ? line.end : line.end + 1;
    }

    QTextCursor editCursor(m_document);
    editCursor.setPosition(insertionPoint);
    if (selectionStart != selectionEnd) {
        editCursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    }
    // 先把临时 cursor 放到真实编辑范围，再开启撤销块；否则 Qt 可能把
    // cursor 构造时的文档开头位置记录为撤销后的光标位置。
    editCursor.beginEditBlock();
    editCursor.insertText(insertion);

    int cursorAfter = insertionPoint + insertion.size();
    if (smartLinePaste) {
        // 光标落在新粘贴行行尾（不含换行），连续 Ctrl+V 会在下方不断堆叠新行；
        // 剪贴板恰为单个空行（"\n"）时，光标落在该空行上。
        cursorAfter = insertionPoint + qMax(1, insertion.size() - 1);
    }
    m_editor->setProperty("cursorPosition", cursorAfter);
    selectRange(cursorAfter, cursorAfter);
    const bool manualNumberEdit = !smartLinePaste
        && !insertion.contains(QLatin1Char('\n'))
        && orderedListStructureAffected(text, selectionStart, selectionEnd);
    const bool repairOrderedList = smartLinePaste
        || insertion.contains(QLatin1Char('\n')) || manualNumberEdit;
    if (repairOrderedList) {
        repairOrderedLists(text, m_document->toPlainText(), !manualNumberEdit);
    }
    editCursor.endEditBlock();
    m_selectionUndoSnapshot = SelectionUndoSnapshot{
        text, m_documentTextSnapshot, selectionStart, selectionEnd, cursorPosition};
    focusEditor();
    return true;
}

bool EditorCommandRegistry::toggleCurrentCheckbox()
{
    const QString text = m_documentTextSnapshot;
    const int cursorPosition = m_editor->property("cursorPosition").toInt();
    const LineRange lineRange = lineRangeAt(text, cursorPosition);
    const QString line = text.mid(lineRange.start, lineRange.end - lineRange.start);
    const int positionInLine = cursorPosition - lineRange.start;

    const auto statePosition = taskCheckboxStatePosition(line);
    QTextCursor editCursor(m_document);
    editCursor.beginEditBlock();
    int updatedCursorPosition = cursorPosition;
    if (statePosition) {
        const int statePositionInDocument = lineRange.start + *statePosition;
        QTextCursor stateCursor(m_document);
        stateCursor.setPosition(statePositionInDocument);
        stateCursor.setPosition(statePositionInDocument + 1, QTextCursor::KeepAnchor);
        stateCursor.insertText(line.at(*statePosition) == QLatin1Char(' ')
                                   ? QStringLiteral("x") : QStringLiteral(" "));
    } else {
        const MarkdownListItem listItem = parseMarkdownListItem(line);
        int insertionOffset = 0;
        QString insertion;
        if (listItem.valid) {
            insertionOffset = listItem.contentStart;
            insertion = QStringLiteral("[ ] ");
        } else {
            static const QRegularExpression quotePrefixPattern(
                QStringLiteral(R"(^([\t ]*(?:>[\t ]*)*))"));
            const QRegularExpressionMatch prefixMatch = quotePrefixPattern.match(line);
            insertionOffset = prefixMatch.hasMatch() ? prefixMatch.capturedLength(1) : 0;
            insertion = QStringLiteral("- [ ] ");
        }

        QTextCursor insertionCursor(m_document);
        insertionCursor.setPosition(lineRange.start + insertionOffset);
        insertionCursor.insertText(insertion);
        if (positionInLine >= insertionOffset) {
            updatedCursorPosition += insertion.size();
        }
    }
    editCursor.endEditBlock();

    m_editor->setProperty("cursorPosition", updatedCursorPosition);
    focusEditor();
    return true;
}

EditorCommandRegistry::TypedEditResult EditorCommandRegistry::handleTypedText(const QString &text)
{
    TypedEditResult result;
    const int start = m_editor->property("selectionStart").toInt();
    const int end = m_editor->property("selectionEnd").toInt();
    const bool hasSelection = start != end;

    if (text == QStringLiteral("```")) {
        QString closing = QStringLiteral("\n```");
        if (!hasSelection) {
            const QString documentText = m_document->toPlainText();
            const int lineStart = lineRangeAt(documentText, start).start;
            // 围栏自动补全仅在列 0 行首触发；行中、行末、带缩进行首保持字面。
            if (start != lineStart) {
                return result;
            }
            // 光标后本行还有非空白内容时，闭合围栏后补一个换行，
            // 把后续文字移到闭合围栏下一行，避免闭合边界后紧跟文字。
            if (hasNonBlankCharacterAfter(documentText, start)) {
                closing += QLatin1Char('\n');
            }
        }
        if (const auto footprint =
                insertWrapped(QStringLiteral("```"), closing)) {
            result.consumed = true;
            result.textChanged = true;
            result.footprint = *footprint;
        } else {
            result.consumed = true;
        }
        return result;
    }
    if (text.size() != 1) {
        return result;
    }

    const QString documentText = m_documentTextSnapshot;

    if (!hasSelection
        && (text == QStringLiteral(">") || text == QStringLiteral("》"))) {
        const LineRange line = lineRangeAt(documentText, start);
        if (start == line.start && !isInsideFencedBlock(start)) {
            QTextCursor cursor(m_document);
            cursor.setPosition(start);
            cursor.insertText(QStringLiteral("> "));
            m_editor->setProperty("cursorPosition", start + 2);
            focusEditor();
            result.consumed = true;
            result.textChanged = true;
            return result;
        }
    }

    if (text == QStringLiteral("-") && !hasSelection) {
        const LineRange line = lineRangeAt(documentText, start);
        if (start == line.start && start == line.end) {
            QTextCursor cursor(m_document);
            cursor.setPosition(start);
            cursor.insertText(QStringLiteral("- "));
            m_editor->setProperty("cursorPosition", start + 2);
            focusEditor();
            result.consumed = true;
            result.textChanged = true;
            return result;
        }
    }

    // `·`（U+00B7）：有选区时等价于 `` ` `` 包裹选区；
    // 完全空行上的 `` 对后再输入 `·` 升级为大代码块围栏；
    // 连续两个 `·` 生成反引号对；空格后单点号仍走原别名；其余保持字面。
    if (text == QStringLiteral("·")) {
        if (hasSelection) {
            // 与输入 `` ` `` 一致：用反引号对包裹选区并触发自动空格。
            if (const auto footprint = insertWrapped(QStringLiteral("`"), QStringLiteral("`"))) {
                result.consumed = true;
                result.textChanged = true;
                result.runAutoSpacing = true;
                result.footprint = *footprint;
            } else {
                result.consumed = true;
            }
            return result;
        }
        if (const auto fenceResult = handleEmptyLineFenceUpgrade(start - 1, start + 1)) {
            return *fenceResult;
        }
        const bool doubleDot = isMiddleDotDoubleDot(documentText, start);
        if (doubleDot) {
            if (const auto pairResult = handleDoubleMiddleDot(start - 1, start)) {
                return *pairResult;
            }
        }
        if (const auto aliasResult = handleMiddleDotAlias(start)) {
            return *aliasResult;
        }
        // 字符直接连 `·`：视作普通输入，不做任何处理（由编辑器直接插入）。
        return result;
    }

    if (text == QStringLiteral("`") && !hasSelection) {
        if (start >= 2 && start < documentText.size()
            && documentText.mid(start - 2, 3) == QStringLiteral("```")
            && lineRangeAt(documentText, start - 2).start == start - 2) {
            QTextCursor cursor(m_document);
            cursor.setPosition(start - 2);
            cursor.setPosition(start + 1, QTextCursor::KeepAnchor);
            // 光标后本行还有非空白内容时，闭合围栏后补一个换行，
            // 把后续文字移到闭合围栏下一行，避免闭合边界后紧跟文字。
            const QString replacement = hasNonBlankCharacterAfter(documentText, start + 1)
                ? QStringLiteral("```\n```\n")
                : QStringLiteral("```\n```");
            cursor.insertText(replacement);
            m_editor->setProperty("cursorPosition", start + 1);
            result.consumed = true;
            result.textChanged = true;
            return result;
        }
        // 围栏升级仅在行首反引号 run 成立（Markdown 围栏只能在行首）；
        // 行中反引号对按行内代码处理（闭合时触发自动空格）。
        const int backtickLineStart =
            documentText.lastIndexOf(QLatin1Char('\n'), qMax(0, start - 2)) + 1;
        bool onlyWhitespaceBefore = true;
        for (int position = backtickLineStart; position < start - 1; ++position) {
            const QChar character = documentText.at(position);
            if (character != QLatin1Char(' ') && character != QLatin1Char('\t')) {
                onlyWhitespaceBefore = false;
                break;
            }
        }
        if (onlyWhitespaceBefore && start > 0 && start < documentText.size()
            && documentText.at(start - 1) == QLatin1Char('`')
            && documentText.at(start) == QLatin1Char('`')) {
            QTextCursor cursor(m_document);
            cursor.setPosition(start);
            cursor.insertText(QStringLiteral("`"));
            m_editor->setProperty("cursorPosition", start + 1);
            result.consumed = true;
            result.textChanged = true;
            return result;
        }
    }

    // 行中引号：只插入单个开符号；闭合时再收尾并格式化。
    if (!hasSelection && text.size() == 1) {
        const auto midlinePlan = buildMidlineQuotePlan(
            documentText, start, text.at(0));
        if (midlinePlan) {
            if (!midlinePlan->closer) {
                QTextCursor cursor(m_document);
                cursor.setPosition(start);
                cursor.insertText(QString(midlinePlan->opening));
                // 后输入开符号：若行内其后已有同号闭符号，视为先闭后开的
                // 包裹收尾，与先开后闭一样补两侧自动空格。
                const QString updatedText = m_document->toPlainText();
                const int existingCloser = nextSameQuoteOnLine(
                    updatedText, start + 1, midlinePlan->opening);
                if (existingCloser >= 0) {
                    const CompletionResult completion = finishMidlineQuoteOpening(
                        start, existingCloser, midlinePlan->opening);
                    result.consumed = true;
                    result.textChanged = true;
                    result.runAutoSpacing = completion.autoSpace;
                    result.footprint = completion.footprint;
                    return result;
                }
                m_editor->setProperty("cursorPosition", start + 1);
                focusEditor();
                result.consumed = true;
                result.textChanged = true;
                return result;
            }

            const int openerPosition = midlinePlan->openerPosition;
            const bool closingAtCursor =
                documentText.mid(start, 1) == QString(midlinePlan->closing);
            if (closingAtCursor) {
                // 包裹闭合（即使只是跳过已有闭符号）也要重新执行自动空格判断。
                const CompletionResult completion = finishMidlineQuoteClosure(
                    openerPosition, start, midlinePlan->opening, true);
                result.consumed = true;
                result.textChanged = true;
                result.runAutoSpacing = completion.autoSpace;
                result.footprint = completion.footprint;
                return result;
            }

            QTextCursor cursor(m_document);
            cursor.setPosition(start);
            cursor.beginEditBlock();
            cursor.insertText(QString(midlinePlan->closing));
            cursor.endEditBlock();
            // 反引号对：光标停在两个反引号中间；引号对：光标在刚插入的闭符号之后。
            const CompletionResult completion = finishMidlineQuoteClosure(
                openerPosition, start, midlinePlan->opening, false);
            result.consumed = true;
            // 闭合引号已插入，必须执行自动空格判断（无论是否发生全角转换）。
            result.textChanged = true;
            result.runAutoSpacing = completion.autoSpace;
            result.footprint = completion.footprint;
            return result;
        }
    }

    // 保护区内短路：仅对本次 CJK 自动改写候选做全文分析。
    const bool conversionCandidate = !hasSelection
        && (text == QStringLiteral(",")
            || text == QStringLiteral(".")
            || text == QStringLiteral("。")
            || text == QStringLiteral(":")
            || text == QStringLiteral("?")
            || text == QStringLiteral("!")
            || text == QStringLiteral(";")
            || text == QStringLiteral("(")
            || text == QStringLiteral("[")
            || text == QStringLiteral("\"")
            || text == QStringLiteral("'")
            || text == QStringLiteral(")")
            || text == QStringLiteral("-"));
    // 同一按键内的多处保护区判定共享一次惰性全文分析，避免重复扫描。
    std::optional<CjkText::DocumentAnalysis> lazyAnalysis;
    const auto analysis = [&lazyAnalysis, &documentText]() -> const CjkText::DocumentAnalysis & {
        if (!lazyAnalysis) {
            lazyAnalysis = CjkText::analyzeDocument(documentText);
        }
        return *lazyAnalysis;
    };
    const bool protectedPosition =
        conversionCandidate && CjkText::isPositionProtected(analysis(), start);

    // --- CJK Punctuation, Ellipsis & Em-dash Conversions ---
    if (!hasSelection && !protectedPosition) {
        const QChar ch = text.at(0);

        // 1. Ellipsis: 只转换 ...、。。。、。。.；其他混合形式保持原样。
        if (ch == u'.' || ch == u'\u3002') {
            if (start >= 2) {
                const QString prev2 = documentText.mid(start - 2, 2);
                const bool isEllipsis =
                    (ch == u'.' && prev2 == QStringLiteral(".."))
                    || (ch == u'.' && prev2 == QStringLiteral("\u3002\u3002"))
                    || (ch == u'\u3002' && prev2 == QStringLiteral("\u3002\u3002"));
                if (isEllipsis) {
                    QTextCursor cursor(m_document);
                    cursor.setPosition(start - 2);
                    cursor.setPosition(start, QTextCursor::KeepAnchor);
                    cursor.beginEditBlock();
                    cursor.insertText(QStringLiteral("\u2026\u2026"));
                    cursor.endEditBlock();
                    m_editor->setProperty("cursorPosition", start);
                    focusEditor();
                    result.consumed = true;
                    result.textChanged = true;
                    return result;
                }
            }
        }

        // 2. Em-dash: CJK-- → CJK——
        if (ch == u'-' && start >= 2) {
            if (documentText.at(start - 1) == u'-') {
                const QChar beforeDash = documentText.at(start - 2);
                if (CjkText::isCjk(beforeDash)) {
                    QTextCursor cursor(m_document);
                    cursor.setPosition(start - 1);
                    cursor.setPosition(start, QTextCursor::KeepAnchor);
                    cursor.beginEditBlock();
                    cursor.insertText(QStringLiteral("\u2014\u2014"));
                    cursor.endEditBlock();
                    m_editor->setProperty("cursorPosition", start + 1);
                    focusEditor();
                    result.consumed = true;
                    result.textChanged = true;
                    return result;
                }
            }
        }

        // 3. ASCII Punctuation after CJK / fullwidth punctuation → Fullwidth
        if (start > 0 && isCjkOrFullwidthPunctuationTrigger(documentText.at(start - 1))) {
            if (ch == u'(' || ch == u'[' || ch == u'"' || ch == u'\'') {
                static const QHash<char16_t, std::pair<QString, QString>> cjkPairs = {
                    {u'(', {QStringLiteral("（"), QStringLiteral("）")}},
                    {u'[', {QStringLiteral("【"), QStringLiteral("】")}},
                    {u'"', {QStringLiteral("“"), QStringLiteral("”")}},
                    {u'\'', {QStringLiteral("‘"), QStringLiteral("’")}},
                };
                auto pairIt = cjkPairs.find(ch.unicode());
                if (pairIt != cjkPairs.end()) {
                    if (const auto footprint =
                            insertWrapped(pairIt->first, pairIt->second)) {
                        result.consumed = true;
                        result.textChanged = true;
                        result.runAutoSpacing = true;
                        result.footprint = *footprint;
                    } else {
                        result.consumed = true;
                    }
                    return result;
                }
            }
            static const QHash<char16_t, QString> asciiToFull = {
                {u',', QStringLiteral("，")},
                {u'.', QStringLiteral("。")},
                {u':', QStringLiteral("：")},
                {u'?', QStringLiteral("？")},
                {u'!', QStringLiteral("！")},
                {u';', QStringLiteral("；")},
                {u')', QStringLiteral("）")},
            };
            auto it = asciiToFull.find(ch.unicode());
            // 前一个字符是 `。` 且再前一个是 `.` 的混合点序列（如 `.。.`）不转换，
            // 避免破坏省略号规则。
            const bool mixedDotSequence = ch == u'.' && start >= 2
                && documentText.at(start - 1) == u'\u3002'
                && documentText.at(start - 2) == u'.';
            if (it != asciiToFull.end() && !mixedDotSequence) {
                if (ch == u')' && (documentText.mid(start, 1) == QStringLiteral("）")
                                  || documentText.mid(start, 1) == QStringLiteral(")"))) {
                    m_editor->setProperty("cursorPosition", start + 1);
                    result.consumed = true;
                    return result;
                }
                QTextCursor cursor(m_document);
                cursor.setPosition(start);
                cursor.insertText(it.value());
                m_editor->setProperty("cursorPosition", start + 1);
                focusEditor();
                result.consumed = true;
                result.textChanged = true;
                return result;
            }
        }

        // 4. Halfwidth symbol + two spaces → Fullwidth（`,  ` → `，`，两个空格一并删除）
        if (ch == QLatin1Char(' ') && start >= 2
            && documentText.at(start - 1) == QLatin1Char(' ')) {
            const auto full = fullwidthPunctuationFor(documentText.at(start - 2));
            if (full) {
                if (!CjkText::isPositionProtected(analysis(), start - 2)) {
                    QTextCursor cursor(m_document);
                    cursor.setPosition(start - 2);
                    cursor.setPosition(start, QTextCursor::KeepAnchor);
                    cursor.insertText(QString(full.value()));
                    m_editor->setProperty("cursorPosition", start - 1);
                    focusEditor();
                    result.consumed = true;
                    result.textChanged = true;
                    return result;
                }
            }
        }
    }

    if (const DelimiterPair *pair = pairForOpening(text)) {
        if (!hasSelection && pair->opening == pair->closing
            && documentText.mid(start, pair->closing.size()) == pair->closing) {
            m_editor->setProperty("cursorPosition", start + pair->closing.size());
            result.consumed = true;
            return result;
        }

        const QString selection = documentText.mid(start, end - start);
        const bool selectionProtected = hasSelection
            && (CjkText::isPositionProtected(analysis(), start)
                || CjkText::isPositionProtected(analysis(), end));
        const auto resolvedPair = selectionProtected
            ? std::pair<QString, QString>{pair->opening, pair->closing}
            : CjkText::resolveSelectionPair(pair->opening, pair->closing, selection);
        if (const auto footprint =
                insertWrapped(resolvedPair.first, resolvedPair.second)) {
            result.consumed = true;
            result.textChanged = true;
            result.runAutoSpacing = true;
            result.footprint = *footprint;
        } else {
            result.consumed = true;
        }
        return result;
    }

    if (!hasSelection && isClosingDelimiter(text)
        && documentText.mid(start, text.size()) == text) {
        m_editor->setProperty("cursorPosition", start + text.size());
        result.consumed = true;
        return result;
    }

    return result;
}

std::optional<EditorCommandRegistry::EditFootprint> EditorCommandRegistry::insertWrapped(
    const QString &opening, const QString &closing)
{
    if (!m_editor || !m_document) {
        return std::nullopt;
    }
    const int start = m_editor->property("selectionStart").toInt();
    const int end = m_editor->property("selectionEnd").toInt();
    QTextCursor cursor(m_document);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    const QString selection = normalizeSelectedText(cursor.selectedText());

    cursor.beginEditBlock();
    cursor.insertText(opening + selection + closing);
    cursor.endEditBlock();
    if (selection.isEmpty()) {
        m_editor->setProperty("cursorPosition", start + opening.size());
    } else {
        selectRange(start + opening.size(), start + opening.size() + selection.size());
    }
    focusEditor();
    return EditFootprint{start, start + static_cast<int>(
        opening.size() + selection.size() + closing.size())};
}

EditorCommandRegistry::CompletionResult EditorCommandRegistry::finishMidlineQuoteClosure(
    int openerPosition, int closurePosition, QChar opening, bool closingAlreadyAtCursor)
{
    convertAsciiQuoteWrap(m_document, openerPosition, closurePosition);
    const int leftSpaces =
        spaceBacktickPairBoundaries(m_document, openerPosition, closurePosition);
    // 反引号对：光标停在两个反引号中间（closingAlreadyAtCursor 时闭符号已存在，
    // 光标直接越过它）；引号对：光标在刚插入/提交的闭符号之后。
    const int offset = (closingAlreadyAtCursor || opening != QLatin1Char('`')) ? 1 : 0;
    const int cursorAfter = closurePosition + offset + leftSpaces;
    m_editor->setProperty("cursorPosition", cursorAfter);
    focusEditor();
    return CompletionResult{{openerPosition, closurePosition + 1}};
}

EditorCommandRegistry::CompletionResult EditorCommandRegistry::finishMidlineQuoteOpening(
    int openerPosition, int closerPosition, QChar opening)
{
    convertAsciiQuoteWrap(m_document, openerPosition, closerPosition);
    const int leftSpaces =
        spaceBacktickPairBoundaries(m_document, openerPosition, closerPosition);
    // 反引号对：光标停在刚输入的开符号之后（含其左侧自动空格）；
    // 引号对：转换后由 applyAutoSpacing 统一补边界空格并微调光标。
    const int cursorAfter = openerPosition + 1 + leftSpaces;
    m_editor->setProperty("cursorPosition", cursorAfter);
    focusEditor();
    return CompletionResult{{openerPosition, closerPosition + 1}};
}

void EditorCommandRegistry::repairOrderedLists(const QString &beforeText,
                                               const QString &afterText,
                                               bool preservePreviousStart)
{
    m_documentTextSnapshot = repairedAffectedOrderedLists(
        m_document, beforeText, afterText, preservePreviousStart);
    m_documentTextSnapshotPrepared = true;
}

bool EditorCommandRegistry::handleStructuralDelete(bool backwards)
{
    const QString beforeText = m_documentTextSnapshot;
    int start = m_editor->property("selectionStart").toInt();
    int end = m_editor->property("selectionEnd").toInt();
    if (start == end) {
        if (backwards) {
            if (start <= 0) {
                return false;
            }
            --start;
        } else {
            if (end >= beforeText.size()) {
                return false;
            }
            ++end;
        }
    }
    if (!orderedListStructureAffected(beforeText, start, end)) {
        return false;
    }

    const bool manualNumberEdit = !beforeText.mid(start, end - start)
                                      .contains(QLatin1Char('\n'));
    QTextCursor cursor(m_document);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.beginEditBlock();
    cursor.removeSelectedText();
    m_editor->setProperty("cursorPosition", start);
    repairOrderedLists(beforeText, m_document->toPlainText(), !manualNumberEdit);
    cursor.endEditBlock();
    focusEditor();
    return true;
}

bool EditorCommandRegistry::handleSpecialBackspace()
{
    const int start = m_editor->property("selectionStart").toInt();
    const int end = m_editor->property("selectionEnd").toInt();
    if (start != end) {
        return false;
    }

    const QString text = m_documentTextSnapshot;
    const LineRange line = lineRangeAt(text, start);
    const int lineStart = line.start;
    const int lineEnd = line.end;
    const MarkdownListItem emptyListItem = parseMarkdownListItem(
        text.mid(lineStart, lineEnd - lineStart));
    if (emptyListItem.valid && emptyListItem.isEmpty()
        && start >= lineStart + emptyListItem.contentStart) {
        const int removeStart = lineStart > 0 ? lineStart - 1 : 0;
        const int removeEnd = lineStart > 0
            ? lineEnd
            : qMin(text.size(), lineEnd + (lineEnd < text.size() ? 1 : 0));

        QTextCursor editCursor(m_document);
        editCursor.beginEditBlock();
        QTextCursor removalCursor(m_document);
        removalCursor.setPosition(removeStart);
        removalCursor.setPosition(removeEnd, QTextCursor::KeepAnchor);
        removalCursor.removeSelectedText();
        m_editor->setProperty("cursorPosition", removeStart);
        repairOrderedLists(text, m_document->toPlainText(), true);
        editCursor.endEditBlock();
        focusEditor();
        return true;
    }

    const MarkdownQuoteLine quote = parseMarkdownQuoteLine(
        text.mid(lineStart, lineEnd - lineStart));
    if (quote.valid && start == lineStart + quote.contentStart
        && !isInsideFencedBlock(lineStart)) {
        const int removeStart = lineStart + quote.deepestPrefixStart;
        QTextCursor editCursor(m_document);
        editCursor.setPosition(start);
        editCursor.beginEditBlock();
        QTextCursor removalCursor(m_document);
        removalCursor.setPosition(removeStart);
        removalCursor.setPosition(start, QTextCursor::KeepAnchor);
        removalCursor.removeSelectedText();
        m_editor->setProperty("cursorPosition", removeStart);
        editCursor.endEditBlock();
        focusEditor();
        return true;
    }

    const int headingPrefixLength = start - lineStart;
    const int headingMarkerLength = headingPrefixLength - 1;
    bool exactHeadingPrefix = headingMarkerLength >= 1 && headingMarkerLength <= 6
        && start <= lineEnd && text.at(start - 1) == QLatin1Char(' ');
    for (int position = lineStart; exactHeadingPrefix && position < start - 1;
         ++position) {
        exactHeadingPrefix = text.at(position) == QLatin1Char('#');
    }
    if (exactHeadingPrefix && !isInsideFencedBlock(lineStart)) {
        QTextCursor cursor(m_document);
        cursor.setPosition(lineStart);
        cursor.setPosition(start, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        m_editor->setProperty("cursorPosition", lineStart);
        focusEditor();
        return true;
    }

    int removeStart = -1;
    int removeEnd = -1;
    const QStringList wholeDeleteSequences{QStringLiteral("……"), QStringLiteral("——")};
    for (const QString &sequence : wholeDeleteSequences) {
        if (start >= sequence.size()
            && text.mid(start - sequence.size(), sequence.size()) == sequence) {
            removeStart = start - sequence.size();
            removeEnd = start;
            break;
        }
    }

    const QString emptyFence = QStringLiteral("```\n```");
    if (removeStart < 0 && start >= 3
        && text.mid(start - 3, emptyFence.size()) == emptyFence) {
        removeStart = start - 3;
        removeEnd = removeStart + emptyFence.size();
    } else if (removeStart < 0) {
        const QStringList markers{QStringLiteral("***"), QStringLiteral("**"),
                                  QStringLiteral("*"), QStringLiteral("`")};
        for (const QString &marker : markers) {
            if (start >= marker.size()
                && text.mid(start - marker.size(), marker.size()) == marker
                && text.mid(start, marker.size()) == marker) {
                removeStart = start - marker.size();
                removeEnd = start + marker.size();
                break;
            }
        }
    }

    if (removeStart < 0) {
        const auto &pairs = delimiterPairs();
        for (const auto &pair : pairs) {
            const int openLen = pair.opening.size();
            const int closeLen = pair.closing.size();
            if (start >= openLen
                && start + closeLen <= text.size()
                && text.mid(start - openLen, openLen) == pair.opening
                && text.mid(start, closeLen) == pair.closing) {
                removeStart = start - openLen;
                removeEnd = start + closeLen;
                break;
            }
        }
    }
    if (removeStart < 0) {
        return false;
    }

    QTextCursor cursor(m_document);
    cursor.setPosition(removeStart);
    cursor.setPosition(removeEnd, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    m_editor->setProperty("cursorPosition", removeStart);
    focusEditor();
    return true;
}

bool EditorCommandRegistry::handleListEnter(bool insideFencedBlock)
{
    const int start = m_editor->property("selectionStart").toInt();
    const int end = m_editor->property("selectionEnd").toInt();
    if (start != end) {
        return false;
    }

    const QString text = m_documentTextSnapshot;
    const LineRange line = lineRangeAt(text, start);
    const int lineStart = line.start;
    const int lineEnd = line.end;
    if (insideFencedBlock) {
        return false;
    }

    const MarkdownListItem item = parseMarkdownListItem(
        text.mid(lineStart, lineEnd - lineStart));
    const int positionInLine = start - lineStart;
    if (!item.valid || positionInLine < item.contentStart) {
        return false;
    }

    QTextCursor editCursor(m_document);
    // QTextCursor(document) 默认位于文档开头；事务必须锚定实际输入点，
    // 否则撤销列表续行时可见光标会错误回到位置 0。
    editCursor.setPosition(start);
    editCursor.beginEditBlock();
    if (item.isEmpty()) {
        const int removeStart = lineStart + item.markerStart;
        QTextCursor removalCursor(m_document);
        removalCursor.setPosition(removeStart);
        removalCursor.setPosition(lineEnd, QTextCursor::KeepAnchor);
        removalCursor.removeSelectedText();
        m_editor->setProperty("cursorPosition", removeStart);
        repairOrderedLists(text, m_document->toPlainText(), true);
        editCursor.endEditBlock();
        focusEditor();
        return true;
    }

    const QString continuation = item.continuationPrefix();
    QTextCursor insertionCursor(m_document);
    insertionCursor.setPosition(start);
    insertionCursor.insertText(QLatin1Char('\n') + continuation);
    const int newLineStart = start + 1;
    const int cursorPosition = newLineStart + continuation.size();
    m_editor->setProperty("cursorPosition", cursorPosition);
    if (item.ordered) {
        repairOrderedLists(text, m_document->toPlainText(), true);
    }
    editCursor.endEditBlock();
    focusEditor();
    return true;
}

bool EditorCommandRegistry::handleQuoteEnter(bool preserveEmptyQuote,
                                             bool insideFencedBlock)
{
    const int start = m_editor->property("selectionStart").toInt();
    const int end = m_editor->property("selectionEnd").toInt();
    if (start != end) {
        return false;
    }

    const QString text = m_documentTextSnapshot;
    const LineRange line = lineRangeAt(text, start);
    if (insideFencedBlock) {
        return false;
    }

    const MarkdownQuoteLine quote = parseMarkdownQuoteLine(
        text.mid(line.start, line.end - line.start));
    const int positionInLine = start - line.start;
    if (!quote.valid || positionInLine < quote.contentStart) {
        return false;
    }

    QTextCursor editCursor(m_document);
    editCursor.setPosition(start);
    editCursor.beginEditBlock();
    if (quote.isEmpty() && !preserveEmptyQuote) {
        // 普通 Enter 每次只退出最内层引用；最外层退出后保留当前空行。
        const QString retainedPrefix = quote.prefix.left(quote.deepestPrefixStart);
        QTextCursor replacementCursor(m_document);
        replacementCursor.setPosition(line.start);
        replacementCursor.setPosition(line.start + quote.contentStart,
                                      QTextCursor::KeepAnchor);
        replacementCursor.insertText(retainedPrefix);
        m_editor->setProperty("cursorPosition", line.start + retainedPrefix.size());
        editCursor.endEditBlock();
        focusEditor();
        return true;
    }

    QTextCursor insertionCursor(m_document);
    insertionCursor.setPosition(start);
    insertionCursor.insertText(QLatin1Char('\n') + quote.prefix);
    m_editor->setProperty("cursorPosition", start + 1 + quote.prefix.size());
    editCursor.endEditBlock();
    focusEditor();
    return true;
}

std::optional<EditorCommandRegistry::TypedEditResult>
EditorCommandRegistry::handleMiddleDotAlias(int start)
{
    if (!m_editor || !m_document || start < 0) {
        return std::nullopt;
    }
    const QString documentText = m_document->toPlainText();
    if (start <= 0 || start > documentText.size()) {
        return std::nullopt;
    }

    // 空格后单点号：删除空格与 `·`，生成反引号对（光标在中间），
    // 由边界空格逻辑补两侧空格（如 `中文 ·` 后得到 `中文 `` `）。
    if (documentText.at(start - 1) == QLatin1Char(' ')) {
        const int removeStart = start - 1;
        QTextCursor cursor(m_document);
        cursor.setPosition(removeStart);
        cursor.setPosition(start, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        m_editor->setProperty("cursorPosition", removeStart);
        focusEditor();
        return insertBacktickPairAt(removeStart);
    }

    return std::nullopt;
}

EditorCommandRegistry::TypedEditResult EditorCommandRegistry::insertBacktickPairAt(int position)
{
    QTextCursor insertionCursor(m_document);
    insertionCursor.setPosition(position);
    insertionCursor.insertText(QStringLiteral("``"));
    const int leftSpaces =
        spaceBacktickPairBoundaries(m_document, position, position + 1);
    m_editor->setProperty("cursorPosition", position + 1 + leftSpaces);
    focusEditor();
    TypedEditResult result;
    result.consumed = true;
    result.textChanged = true;
    result.runAutoSpacing = false;
    result.footprint = {position, position + 2};
    return result;
}

bool EditorCommandRegistry::isMiddleDotDoubleDot(const QString &text, int position) const
{
    return position > 0 && position <= text.size()
        && text.at(position - 1) == u'\u00B7'
        && (position < 2
            || (text.at(position - 2) != QLatin1Char(' ')
                && text.at(position - 2) != QLatin1Char('`')))
        && !isInsideFencedBlock(position);
}

bool EditorCommandRegistry::isMiddleDotEmptyLinePair(const QString &text, int position) const
{
    if (position <= 0 || position >= text.size()
        || text.at(position - 1) != QLatin1Char('`')
        || text.at(position) != QLatin1Char('`')) {
        return false;
    }
    const int lineStart = text.lastIndexOf(QLatin1Char('\n'), qMax(0, position - 2)) + 1;
    int lineEnd = text.indexOf(QLatin1Char('\n'), position);
    if (lineEnd < 0) {
        lineEnd = text.size();
    }
    return lineStart == position - 1 && lineEnd == position + 1
        && !isInsideFencedBlock(position);
}

std::optional<EditorCommandRegistry::TypedEditResult>
EditorCommandRegistry::handleDoubleMiddleDot(int removeStart, int removeEnd)
{
    if (!m_editor || !m_document || removeStart < 0 || removeEnd <= removeStart) {
        return std::nullopt;
    }
    const QString documentText = m_document->toPlainText();
    if (removeEnd > documentText.size()) {
        return std::nullopt;
    }
    for (int position = removeStart; position < removeEnd; ++position) {
        if (documentText.at(position) != u'\u00B7') {
            return std::nullopt;
        }
    }

    // 删除两个 `·`，生成反引号对（光标在中间），由边界空格逻辑补两侧空格。
    QTextCursor cursor(m_document);
    cursor.setPosition(removeStart);
    cursor.setPosition(removeEnd, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    m_editor->setProperty("cursorPosition", removeStart);
    focusEditor();
    return insertBacktickPairAt(removeStart);
}

std::optional<EditorCommandRegistry::TypedEditResult>
EditorCommandRegistry::handleEmptyLineFenceUpgrade(int removeStart, int removeEnd)
{
    if (!m_editor || !m_document || removeStart < 0 || removeEnd <= removeStart) {
        return std::nullopt;
    }
    const QString documentText = m_document->toPlainText();
    if (removeEnd > documentText.size()) {
        return std::nullopt;
    }
    const LineRange line = lineRangeAt(documentText, removeStart);
    const int lineStart = line.start;
    const int lineEnd = line.end;
    // 仅限完全空行：该行除 `` 对（键盘路径）或 `·` 形态（IME 路径）外无任何字符。
    const QString pattern = documentText.mid(removeStart, removeEnd - removeStart);
    const bool validState = (pattern == QStringLiteral("``")
                                && isMiddleDotEmptyLinePair(documentText, removeStart + 1))
        || (pattern == QStringLiteral("`·`")
            && lineStart == removeStart && lineEnd == removeEnd
            && !isInsideFencedBlock(removeStart + 1));
    if (!validState) {
        return std::nullopt;
    }

    // 删除 `` 对（IME 路径含已提交的 `·`），生成大代码块围栏，光标置于开围栏之后。
    QTextCursor cursor(m_document);
    cursor.setPosition(removeStart);
    cursor.setPosition(removeEnd, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    m_editor->setProperty("cursorPosition", removeStart);
    focusEditor();
    QTextCursor insertionCursor(m_document);
    insertionCursor.setPosition(removeStart);
    insertionCursor.insertText(QStringLiteral("```\n```"));
    m_editor->setProperty("cursorPosition", removeStart + 3);
    focusEditor();
    TypedEditResult result;
    result.consumed = true;
    result.textChanged = true;
    result.runAutoSpacing = false;
    result.footprint = {removeStart, removeStart + 8};
    return result;
}

std::optional<EditorCommandRegistry::TypedEditResult>
EditorCommandRegistry::tryMergeMiddleDotConversion(const QString &text, int start,
                                                   const QString &currentText)
{
    if (!m_editor || !m_document || text != QStringLiteral("·")
        || m_editor->property("selectionStart").toInt() != start
        || m_editor->property("selectionEnd").toInt() != start) {
        return std::nullopt;
    }
    const bool doubleDot = isMiddleDotDoubleDot(currentText, start);
    const bool fenceUpgrade = isMiddleDotEmptyLinePair(currentText, start);
    if (!doubleDot && !fenceUpgrade) {
        return std::nullopt;
    }

    // 撤销上一步（上一个字面 `·` 的插入，或空行 `` 对的生成），
    // 校验文档确实回到转换起点；若撤销的不是预期步骤则还原并走常规路径。
    const QString expectedAfterUndo = doubleDot
        ? currentText.left(start - 1) + currentText.mid(start)
        : currentText.left(start - 1) + currentText.mid(start + 1);
    QMetaObject::invokeMethod(m_editor, "undo");
    if (m_document->toPlainText() != expectedAfterUndo) {
        QMetaObject::invokeMethod(m_editor, "redo");
        return std::nullopt;
    }

    // 在转换起点直接生成反引号对（或大代码块围栏），本次输入与上一步合并为一次撤销。
    const int insertPosition = start - 1;
    m_editor->setProperty("cursorPosition", insertPosition);
    QTextCursor undoGroupCursor(m_document);
    undoGroupCursor.setPosition(insertPosition);
    undoGroupCursor.beginEditBlock();
    TypedEditResult result;
    if (doubleDot) {
        result = insertBacktickPairAt(insertPosition);
    } else {
        QTextCursor insertionCursor(m_document);
        insertionCursor.setPosition(insertPosition);
        insertionCursor.insertText(QStringLiteral("```\n```"));
        m_editor->setProperty("cursorPosition", insertPosition + 3);
        focusEditor();
        result.consumed = true;
        result.textChanged = true;
        result.runAutoSpacing = false;
        result.footprint = {insertPosition, insertPosition + 8};
    }
    undoGroupCursor.endEditBlock();
    return result;
}

bool EditorCommandRegistry::jumpOutOfPair()
{
    const int start = m_editor->property("selectionStart").toInt();
    const int end = m_editor->property("selectionEnd").toInt();
    if (start != end) {
        return false;
    }

    const QString text = m_documentTextSnapshot;
    const LineRange line = lineRangeAt(text, start);
    const int lineStart = line.start;
    const int lineEnd = line.end;
    int jumpPosition = -1;
    const QStringList markers{
        QStringLiteral("***"), QStringLiteral("___"), QStringLiteral("**"),
        QStringLiteral("__"), QStringLiteral("`"), QStringLiteral("*"),
        QStringLiteral("_")};

    for (const QString &marker : markers) {
        if (start >= marker.size()
            && text.mid(start - marker.size(), marker.size()) == marker
            && text.mid(start, marker.size()) == marker) {
            const int candidate = start + marker.size();
            jumpPosition = jumpPosition < 0 ? candidate : qMin(jumpPosition, candidate);
            continue;
        }

        const int scopeStart = lineStart;
        int runCount = 0;
        int run = nextExactMarkerRun(text, marker, scopeStart);
        while (run >= 0 && run < start) {
            ++runCount;
            run = nextExactMarkerRun(text, marker, run + marker.size());
        }
        if ((runCount % 2) == 1) {
            const int closing = nextExactMarkerRun(text, marker, start);
            if (closing >= 0 && closing < lineEnd) {
                const int candidate = closing + marker.size();
                jumpPosition = jumpPosition < 0 ? candidate : qMin(jumpPosition, candidate);
            }
        }
    }

    const auto &pairs = delimiterPairs();
    QVector<int> stack;
    QVector<int> openerPositions;
    const auto consumeDelimiter = [&pairs](QVector<int> &delimiterStack,
                                           QVector<int> &positions,
                                           int position, QChar character) -> int {
        if (!delimiterStack.isEmpty()
            && pairs.at(delimiterStack.back()).closing.front() == character) {
            const int openerPosition = positions.isEmpty() ? -1 : positions.back();
            delimiterStack.pop_back();
            if (!positions.isEmpty()) {
                positions.pop_back();
            }
            return openerPosition;
        }
        for (int index = 0; index < pairs.size(); ++index) {
            if (pairs.at(index).opening.front() == character) {
                delimiterStack.append(index);
                positions.append(position);
                break;
            }
        }
        return -1;
    };

    for (int position = lineStart; position < start; ++position) {
        consumeDelimiter(stack, openerPositions, position, text.at(position));
    }
    int delimiterJumpPosition = -1;
    int delimiterOpenerPosition = -1;
    int delimiterCloserPosition = -1;
    if (!stack.isEmpty()) {
        const int containingDepth = stack.size();
        for (int position = start; position < text.size()
             && text.at(position) != QLatin1Char('\n'); ++position) {
            const int openerPosition =
                consumeDelimiter(stack, openerPositions, position, text.at(position));
            if (stack.size() < containingDepth) {
                const int candidate = position + 1;
                jumpPosition = jumpPosition < 0 ? candidate : qMin(jumpPosition, candidate);
                delimiterJumpPosition = candidate;
                delimiterOpenerPosition = openerPosition;
                delimiterCloserPosition = position;
                break;
            }
        }
    }

    if (jumpPosition < 0) {
        return false;
    }

    // Tab 跳出严格匹配的半角配对时，若配对内容含 CJK 则整对转为全角，
    // 与输入时 `( [ " '` 生成全角配对的映射一致；保护区内不转换。
    if (m_document && delimiterJumpPosition == jumpPosition
        && delimiterOpenerPosition >= 0 && delimiterCloserPosition >= 0) {
        const auto fullwidth = fullwidthPairFor(
            text.at(delimiterOpenerPosition), text.at(delimiterCloserPosition));
        if (fullwidth
            && wrapContentContainsCjk(text, delimiterOpenerPosition, delimiterCloserPosition)) {
            // 内容含 CJK 才做一次惰性全文分析；开符或闭符任一受保护则跳过。
            std::optional<CjkText::DocumentAnalysis> lazyAnalysis;
            const auto analysis = [&lazyAnalysis, &text]() -> const CjkText::DocumentAnalysis & {
                if (!lazyAnalysis) {
                    lazyAnalysis = CjkText::analyzeDocument(text);
                }
                return *lazyAnalysis;
            };
            const bool openerProtected =
                CjkText::isPositionProtected(analysis(), delimiterOpenerPosition);
            const bool closerProtected =
                CjkText::isPositionProtected(analysis(), delimiterCloserPosition);
            if (!openerProtected && !closerProtected) {
                QTextCursor cursor(m_document);
                cursor.beginEditBlock();
                cursor.setPosition(delimiterCloserPosition);
                cursor.setPosition(delimiterCloserPosition + 1, QTextCursor::KeepAnchor);
                cursor.insertText(fullwidth->second);
                cursor.setPosition(delimiterOpenerPosition);
                cursor.setPosition(delimiterOpenerPosition + 1, QTextCursor::KeepAnchor);
                cursor.insertText(fullwidth->first);
                cursor.endEditBlock();
            }
        }
    }

    m_editor->setProperty("cursorPosition", jumpPosition);
    focusEditor();
    return true;
}

bool EditorCommandRegistry::changeIndent(bool outdent)
{
    const QString text = m_documentTextSnapshot;
    const int start = m_editor->property("selectionStart").toInt();
    const int end = m_editor->property("selectionEnd").toInt();
    const int lineStart = lineRangeAt(text, start).start;

    const int effectiveEnd = end > start ? end - 1 : end;
    int lineEnd = text.indexOf(QLatin1Char('\n'), effectiveEnd);
    if (lineEnd < 0) {
        lineEnd = text.size();
    }
    QStringList lines = text.mid(lineStart, lineEnd - lineStart)
                            .split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    bool changed = false;
    for (QString &line : lines) {
        if (!outdent) {
            line.prepend(QStringLiteral("    "));
            changed = true;
        } else if (line.startsWith(QLatin1Char('\t'))) {
            line.remove(0, 1);
            changed = true;
        } else {
            int spaces = 0;
            while (spaces < qMin(4, line.size()) && line.at(spaces) == QLatin1Char(' ')) {
                ++spaces;
            }
            if (spaces > 0) {
                line.remove(0, spaces);
                changed = true;
            }
        }
    }
    if (!changed) {
        return true;
    }

    const QString transformed = lines.join(QLatin1Char('\n'));
    QTextCursor cursor(m_document);
    cursor.setPosition(lineStart);
    cursor.setPosition(lineEnd, QTextCursor::KeepAnchor);
    cursor.beginEditBlock();
    cursor.insertText(transformed);
    if (start == end) {
        const int delta = transformed.size() - (lineEnd - lineStart);
        m_editor->setProperty("cursorPosition", qMax(lineStart, start + delta));
    } else {
        selectRange(lineStart, lineStart + transformed.size());
    }
    repairOrderedLists(text, m_document->toPlainText(), true);
    cursor.endEditBlock();
    focusEditor();
    return true;
}

std::optional<EditorCommandRegistry::CompletionResult>
EditorCommandRegistry::completeInputMethodCommit(const QString &committedText,
                                                const QString &beforeText,
                                                const QString &selection,
                                                int selectionStart, int selectionEnd)
{
    if (!m_editor || !m_document) {
        return std::nullopt;
    }
    // `·`（U+00B7）与键盘路径一致：完全空行 `` 对后再提交 `·` 升级为围栏；
    // 连续两个 `·` 生成反引号对；空格后单点号模拟反引号；紧贴字符保持字面。
    if (selectionStart == selectionEnd && committedText == QStringLiteral("·")) {
        const QString currentText = m_document->toPlainText();
        // 提交的 `·` 已位于 selectionStart；围栏升级与双点转换都基于该位置。
        if (const auto fenceResult =
                handleEmptyLineFenceUpgrade(selectionStart - 1, selectionStart + 2)) {
            return CompletionResult{fenceResult->footprint, fenceResult->runAutoSpacing};
        }
        // IME 路径还需确认提交的 `·` 确实位于光标处（键盘路径无此步骤）。
        const bool doubleDot = isMiddleDotDoubleDot(currentText, selectionStart)
            && currentText.mid(selectionStart, 1) == QStringLiteral("·");
        if (doubleDot) {
            const auto pairResult =
                handleDoubleMiddleDot(selectionStart - 1, selectionStart + 1);
            if (!pairResult) {
                return std::nullopt;
            }
            return CompletionResult{pairResult->footprint, pairResult->runAutoSpacing};
        }
        const bool aliased = selectionStart > 0
            && selectionStart < currentText.size()
            && (currentText.mid(selectionStart, 1) == QStringLiteral("·"))
            && currentText.at(selectionStart - 1) == QLatin1Char(' ');
        if (aliased) {
            QTextCursor removalCursor(m_document);
            removalCursor.setPosition(selectionStart);
            removalCursor.setPosition(selectionStart + 1, QTextCursor::KeepAnchor);
            removalCursor.removeSelectedText();
            m_editor->setProperty("cursorPosition", selectionStart);
            const auto aliasResult = handleMiddleDotAlias(selectionStart);
            if (!aliasResult) {
                return std::nullopt;
            }
            return CompletionResult{
                aliasResult->footprint,
                aliasResult->runAutoSpacing};
        }
        // 字符直接连 `·`：保持字面量，不做任何处理。
        return CompletionResult{
            {selectionStart, selectionStart + 1},
            /*autoSpace=*/false};
    }

    QString expectedText = beforeText;
    expectedText.replace(selectionStart, selectionEnd - selectionStart, committedText);
    if (m_document->toPlainText() != expectedText) {
        return std::nullopt;
    }

    if (selectionStart == selectionEnd
        && (committedText == QStringLiteral(">")
            || committedText == QStringLiteral("》"))) {
        const LineRange line = lineRangeAt(beforeText, selectionStart);
        if (selectionStart == line.start && !isInsideFencedBlock(selectionStart)) {
            QTextCursor cursor(m_document);
            cursor.setPosition(selectionStart);
            cursor.setPosition(selectionStart + committedText.size(),
                               QTextCursor::KeepAnchor);
            cursor.insertText(QStringLiteral("> "));
            m_editor->setProperty("cursorPosition", selectionStart + 2);
            focusEditor();
            return CompletionResult{{selectionStart, selectionStart + 2},
                                    /*autoSpace=*/false};
        }
    }

    // 行中引号与键盘路径一致：只保留单个开符号；闭合时收尾并格式化。
    if (selectionStart == selectionEnd && committedText.size() == 1) {
        const QString currentText = m_document->toPlainText();
        // IME 事件到达此处时，Qt 已把 committedText 插入文档。引号角色必须基于
        // 提交前快照判断，否则行尾开引号会把自身误认成后续正文，已有闭引号前的
        // 提交也无法区分“新插入字符”和“待跳过字符”。
        const auto midlinePlan = buildMidlineQuotePlan(
            beforeText, selectionStart, committedText.at(0));
        if (midlinePlan) {
            if (!midlinePlan->closer) {
                // 后提交开符号：若行内其后已有匹配闭符号，视为先闭后开的
                // 包裹收尾，与先开后闭一样补两侧自动空格。
                const int existingCloser = nextSameQuoteOnLine(
                    currentText, selectionStart + 1, midlinePlan->closing);
                if (existingCloser >= 0) {
                    return finishMidlineQuoteOpening(
                        selectionStart, existingCloser, midlinePlan->opening);
                }
                return CompletionResult{
                    {selectionStart, selectionStart + 1},
                    /*autoSpace=*/false};
            }

            const int openerPosition = midlinePlan->openerPosition;
            const bool closingAlreadyAtCursor =
                beforeText.mid(selectionStart, 1) == QString(midlinePlan->closing);
            if (closingAlreadyAtCursor) {
                // Qt 已在原闭符号前插入本次 IME 提交。先删除新增字符，再按键盘路径
                // 越过原闭符号，避免 `””` / `’’` 等重复闭合。
                QTextCursor cursor(m_document);
                cursor.setPosition(selectionStart);
                cursor.setPosition(selectionStart + committedText.size(),
                                   QTextCursor::KeepAnchor);
                cursor.removeSelectedText();
                return finishMidlineQuoteClosure(
                    openerPosition, selectionStart, midlinePlan->opening, true);
            }
            // IME 提交的闭符号已位于光标处；若与计划闭符号不同（如 ASCII
            // 引号闭合全角引号对），先替换为计划闭符号，再与键盘路径共用
            // 同一收尾（转换、边界空格与光标）。
            if (currentText.mid(selectionStart, 1) != QString(midlinePlan->closing)) {
                QTextCursor cursor(m_document);
                cursor.setPosition(selectionStart);
                cursor.setPosition(selectionStart + 1, QTextCursor::KeepAnchor);
                cursor.beginEditBlock();
                cursor.insertText(QString(midlinePlan->closing));
                cursor.endEditBlock();
            }
            return finishMidlineQuoteClosure(
                openerPosition, selectionStart, midlinePlan->opening, false);
        }
    }

    // 先把 IME 提交的别名解析为标准开符号；有选区时 `·` 等价于 `` ` ``，
    // 用反引号对包裹选区并触发自动空格。
    QString delimiterText = (committedText == QStringLiteral("·"))
        ? QStringLiteral("`") : committedText;
    // 部分输入法会记忆弯引号的左右状态，导致新一对引号从闭符号开始。
    // 同类未闭合引号已由上方的 midlinePlan 优先收尾；仅在无选区且光标后
    // 到行末没有非空白内容时，把孤立闭符号按对应开符号交给普通配对流程。
    if (selectionStart == selectionEnd && committedText.size() == 1
        && !hasNonBlankCharacterAfter(beforeText, selectionStart)) {
        if (const QuotePair *closingPair = quotePairForClosing(committedText.at(0))) {
            delimiterText = QString(closingPair->opening);
        }
    }
    const DelimiterPair *openingPair = pairForOpening(delimiterText);
    const bool symmetricPair = openingPair && openingPair->opening == openingPair->closing;
    const bool skipExisting = selectionStart == selectionEnd
        && beforeText.mid(selectionStart, committedText.size()) == committedText
        && (symmetricPair || (!openingPair && isClosingDelimiter(committedText)));
    if (skipExisting) {
        QTextCursor cursor(m_document);
        cursor.setPosition(selectionStart);
        cursor.setPosition(selectionStart + committedText.size(), QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        m_editor->setProperty("cursorPosition", selectionStart + committedText.size());
        return std::nullopt;
    }

    QString replacement;
    int contentOffset = 0;
    if (committedText == QStringLiteral("```")) {
        if (selectionStart == selectionEnd) {
            const int lineStart = lineRangeAt(beforeText, selectionStart).start;
            // 围栏自动补全仅在列 0 行首触发；行中、行末、带缩进行首保持字面。
            if (selectionStart != lineStart) {
                return std::nullopt;
            }
            // 光标后本行还有非空白内容时，闭合围栏后补一个换行，
            // 把后续文字移到闭合围栏下一行，避免闭合边界后紧跟文字。
            replacement = QStringLiteral("```") + selection
                + (hasNonBlankCharacterAfter(beforeText, selectionStart)
                       ? QStringLiteral("\n```\n")
                       : QStringLiteral("\n```"));
        } else {
            replacement = QStringLiteral("```") + selection + QStringLiteral("\n```");
        }
        contentOffset = 3;
    } else if (openingPair) {
        const CjkText::DocumentAnalysis analysis =
            CjkText::analyzeDocument(beforeText);
        const bool selectionProtected =
            CjkText::isPositionProtected(analysis, selectionStart)
            || CjkText::isPositionProtected(analysis, selectionEnd);
        const auto resolvedPair = selectionProtected
            ? std::pair<QString, QString>{openingPair->opening,
                                          openingPair->closing}
            : CjkText::resolveSelectionPair(openingPair->opening,
                                            openingPair->closing, selection);
        replacement = resolvedPair.first + selection + resolvedPair.second;
        contentOffset = resolvedPair.first.size();
    } else {
        return std::nullopt;
    }

    QTextCursor cursor(m_document);
    cursor.setPosition(selectionStart);
    cursor.setPosition(selectionStart + committedText.size(), QTextCursor::KeepAnchor);
    cursor.beginEditBlock();
    cursor.insertText(replacement);
    cursor.endEditBlock();
    if (selection.isEmpty()) {
        m_editor->setProperty("cursorPosition", selectionStart + contentOffset);
    } else {
        selectRange(selectionStart + contentOffset,
                    selectionStart + contentOffset + selection.size());
    }
    return CompletionResult{{selectionStart,
                             selectionStart + static_cast<int>(replacement.size())}};
}

void EditorCommandRegistry::selectRange(int start, int end)
{
    if (m_editor) {
        QMetaObject::invokeMethod(m_editor, "select", Q_ARG(int, start), Q_ARG(int, end));
    }
}

void EditorCommandRegistry::selectRangeWithActiveEnd(int start, int end, int activeEnd)
{
    if (!m_editor) {
        return;
    }
    if (activeEnd <= start) {
        // 反向选区：先让光标落在 end，再把 active end 移到 start。
        m_editor->setProperty("cursorPosition", end);
        QMetaObject::invokeMethod(m_editor, "moveCursorSelection", Q_ARG(int, start));
    } else {
        selectRange(start, end);
    }
}

void EditorCommandRegistry::focusEditor()
{
    if (m_editor) {
        QMetaObject::invokeMethod(m_editor, "forceActiveFocus");
    }
}

QString EditorCommandRegistry::selectedText() const
{
    if (!m_editor || !m_document) {
        return {};
    }
    QTextCursor cursor(m_document);
    cursor.setPosition(m_editor->property("selectionStart").toInt());
    cursor.setPosition(m_editor->property("selectionEnd").toInt(), QTextCursor::KeepAnchor);
    return normalizeSelectedText(cursor.selectedText());
}

bool EditorCommandRegistry::formatSpacing()
{
    if (!m_editor || !m_document) {
        return false;
    }
    const QString documentText = m_document->toPlainText();
    const CjkText::DocumentAnalysis analysis = CjkText::analyzeDocument(documentText);
    const int start = m_editor->property("selectionStart").toInt();
    const int end = m_editor->property("selectionEnd").toInt();

    int rangeStart = start;
    int rangeEnd = end;
    if (start == end) {
        const LineRange line = lineRangeAt(documentText, start);
        rangeStart = line.start;
        rangeEnd = line.end;
    }
    if (rangeStart < 0 || rangeEnd > documentText.size() || rangeStart >= rangeEnd) {
        focusEditor();
        return true;
    }

    const QVector<int> insertions = CjkText::collectSpacingInsertions(
        documentText,
        CjkText::BoundaryRange{rangeStart + 1, rangeEnd - 1},
        analysis,
        /*allowPendingDelimiterSpacing=*/false);
    if (insertions.isEmpty()) {
        focusEditor();
        return true;
    }

    const int cursorPosition = m_editor->property("cursorPosition").toInt();
    QTextCursor editCursor(m_document);
    if (start != end) {
        if (cursorPosition == end) {
            editCursor.setPosition(start);
            editCursor.setPosition(end, QTextCursor::KeepAnchor);
        } else {
            editCursor.setPosition(end);
            editCursor.setPosition(start, QTextCursor::KeepAnchor);
        }
    } else {
        editCursor.setPosition(cursorPosition);
    }
    editCursor.beginEditBlock();
    for (auto it = insertions.crbegin(); it != insertions.crend(); ++it) {
        QTextCursor insertionCursor(m_document);
        insertionCursor.setPosition(*it);
        insertionCursor.insertText(QStringLiteral(" "));
    }
    editCursor.endEditBlock();
    if (start != end) {
        m_selectionUndoSnapshot = SelectionUndoSnapshot{
            documentText, m_document->toPlainText(), start, end, cursorPosition};
    }

    const int newSelectionStart =
        CjkText::positionAfterInsertions(start, insertions, false);
    const int newSelectionEnd =
        CjkText::positionAfterInsertions(end, insertions, true);
    if (start != end) {
        selectRangeWithActiveEnd(newSelectionStart, newSelectionEnd,
                                 cursorPosition == end ? newSelectionEnd
                                                       : newSelectionStart);
    } else {
        const int newCursor =
            CjkText::positionAfterInsertions(cursorPosition, insertions, true);
        m_editor->setProperty("cursorPosition", newCursor);
    }
    focusEditor();
    return true;
}

void EditorCommandRegistry::applyAutoSpacing(
    EditFootprint footprint, bool includeInternalBoundaries,
    const std::optional<QString> &expectedText)
{
    if (!m_editor || !m_document || footprint.start > footprint.end) {
        return;
    }
    const QString text = m_document->toPlainText();
    if (expectedText && text != *expectedText) {
        return;
    }
    const CjkText::DocumentAnalysis analysis = CjkText::analyzeDocument(text);
    int rangeFirst = footprint.start;
    int rangeLast = footprint.end;
    QSet<int> allowedBoundaries;
    if (!includeInternalBoundaries) {
        allowedBoundaries.insert(footprint.start);
        allowedBoundaries.insert(footprint.end);
        for (const CjkText::ProtectedSpan &span : analysis.inlineSpans) {
            if (span.outerStart <= footprint.end && span.outerEnd >= footprint.start) {
                rangeFirst = qMin(rangeFirst, span.outerStart);
                rangeLast = qMax(rangeLast, span.outerEnd);
                allowedBoundaries.insert(span.outerStart);
                allowedBoundaries.insert(span.outerEnd);
            }
        }
    }
    QVector<int> insertions = CjkText::collectSpacingInsertions(
        text,
        CjkText::BoundaryRange{rangeFirst, rangeLast},
        analysis);
    if (!includeInternalBoundaries) {
        insertions.erase(
            std::remove_if(insertions.begin(), insertions.end(),
                           [&allowedBoundaries](int position) {
                               return !allowedBoundaries.contains(position);
                           }),
            insertions.end());
    }

    // 回收“悬空”右边界空格：ASCII 自动空格把光标停在右外侧空格之前，若本次输入
    // 以 CJK 结尾且该空格两侧已变成 CJK-CJK，则空格不再需要，随本次输入一并清除
    // （例如 `中文 abc` 后输入 `新的中文` 得到 `中文 abc 新的中文`，而不是
    // `中文 abc 新的中文 中文`）。保护区与未闭合分隔符段内不执行清除。
    bool trailingInsideUnclosed = false;
    for (const CjkText::ProtectedSpan &span : analysis.unclosedInlineSpans) {
        if (span.outerStart < footprint.end && footprint.end < span.outerEnd) {
            trailingInsideUnclosed = true;
            break;
        }
    }
    const bool removeTrailingSpace =
        footprint.start < footprint.end
        && footprint.end + 1 < text.size()
        && text.at(footprint.end) == QLatin1Char(' ')
        && CjkText::isCjk(text.at(footprint.end - 1))
        && CjkText::isCjk(text.at(footprint.end + 1))
        && !CjkText::isPositionProtected(analysis, footprint.end)
        && !trailingInsideUnclosed;
    if (insertions.isEmpty() && !removeTrailingSpace) {
        return;
    }

    const int cursorPosition = m_editor->property("cursorPosition").toInt();
    const int selectionStart = m_editor->property("selectionStart").toInt();
    const int selectionEnd = m_editor->property("selectionEnd").toInt();
    for (auto it = insertions.crbegin(); it != insertions.crend(); ++it) {
        QTextCursor insertionCursor(m_document);
        insertionCursor.setPosition(*it);
        insertionCursor.insertText(QStringLiteral(" "));
    }

    int trailingRemovalIndex = -1;
    if (removeTrailingSpace) {
        trailingRemovalIndex =
            CjkText::positionAfterInsertions(footprint.end, insertions, false);
        QTextCursor removalCursor(m_document);
        removalCursor.setPosition(trailingRemovalIndex);
        removalCursor.setPosition(trailingRemovalIndex + 1, QTextCursor::KeepAnchor);
        removalCursor.removeSelectedText();
    }

    // 光标恰位于插入点时保持在其左侧：右外侧自动空格属于“已输入片段之后”的边界，
    // 光标停在空格之前可让后续连续 ASCII 输入并入同一片段，而不是被逐个空格拆开。
    int newCursor =
        CjkText::positionAfterInsertions(cursorPosition, insertions, false);
    int newSelectionStart =
        CjkText::positionAfterInsertions(selectionStart, insertions, false);
    int newSelectionEnd =
        CjkText::positionAfterInsertions(selectionEnd, insertions, true);
    if (trailingRemovalIndex >= 0) {
        if (newCursor > trailingRemovalIndex) {
            --newCursor;
        }
        if (newSelectionStart > trailingRemovalIndex) {
            --newSelectionStart;
        }
        if (newSelectionEnd > trailingRemovalIndex) {
            --newSelectionEnd;
        }
    }
    if (selectionStart != selectionEnd) {
        selectRangeWithActiveEnd(newSelectionStart, newSelectionEnd,
                                 cursorPosition == selectionEnd ? newSelectionEnd
                                                                : newSelectionStart);
    } else {
        m_editor->setProperty("cursorPosition", newCursor);
    }
    focusEditor();
}

bool EditorCommandRegistry::isInsideFencedBlock(int position) const
{
    if (!m_document) {
        return false;
    }
    const QString text = m_document->toPlainText();
    const CjkText::DocumentAnalysis analysis = CjkText::analyzeDocument(text);
    for (const CjkText::ProtectedSpan &span : analysis.blockSpans) {
        if (span.kind == CjkText::ProtectedKind::FencedCode
            && span.outerStart < position && position < span.outerEnd) {
            return true;
        }
    }
    return false;
}
