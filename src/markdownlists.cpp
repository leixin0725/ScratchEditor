#include "markdownlists.h"
#include "cjktextprocessor.h"
#include "textlinerange.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <limits>
#include <utility>

namespace MarkdownLists {

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

static int indentationColumns(const QString &prefix)
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

int oneLevelOutdentLength(const QString &line)
{
    if (line.startsWith(QLatin1Char('\t'))) {
        return 1;
    }

    int spaces = 0;
    while (spaces < qMin(4, line.size()) && line.at(spaces) == QLatin1Char(' ')) {
        ++spaces;
    }
    return spaces;
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

QString childMarkerForParent(const MarkdownListItem &parent)
{
    return parent.ordered
        ? QString::number(parent.number + 1) + parent.delimiter
        : parent.marker;
}

std::optional<MarkdownListItem> parentListItemForOutdent(
    const QString &text, int lineStart, const MarkdownListItem &item,
    int targetIndentColumns)
{
    int previousStart = lineStart;
    while (previousStart > 0) {
        const LineRange previousRange = lineRangeAt(text, previousStart - 1);
        const MarkdownListItem candidate = parseMarkdownListItem(
            text.mid(previousRange.start, previousRange.end - previousRange.start));
        if (!candidate.valid || candidate.quoteDepth != item.quoteDepth) {
            break;
        }
        if (candidate.indentColumns == targetIndentColumns) {
            return candidate;
        }
        if (candidate.indentColumns < targetIndentColumns) {
            break;
        }
        previousStart = previousRange.start;
    }
    return std::nullopt;
}

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

static QString orderedListSignature(const MarkdownListItem &item)
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

static AffectedListArea affectedListArea(const QString &text, int changeStart, int changeEnd)
{
    AffectedListArea area;
    if (text.isEmpty()) {
        return area;
    }

    const int boundedStart = qBound(0, qMin(changeStart, changeEnd), text.size());
    const int boundedEnd = qBound(boundedStart, qMax(changeStart, changeEnd), text.size());
    const LineRange firstRange = lineRangeAt(text, boundedStart);
    const LineRange lastRange = lineRangeAt(text, boundedEnd);
    // Include both neighbors of the edit and extend through adjoining list lines.
    LineRange range = firstRange.start > 0 ? lineRangeAt(text, firstRange.start - 1)
                                         : firstRange;
    const int lastSeed = lastRange.end < text.size() ? lastRange.end + 1 : lastRange.start;
    const auto itemAt = [&text](LineRange line) {
        return parseMarkdownListItem(text.mid(line.start, line.end - line.start));
    };
    while (range.start > 0 && itemAt(range).valid) {
        range = lineRangeAt(text, range.start - 1);
    }
    for (;;) {
        const MarkdownListItem item = itemAt(range);
        if (item.valid) {
            area.lines.append({range.start, range.end, item});
        }
        if (range.end >= text.size() || (range.start >= lastSeed && !item.valid)) {
            break;
        }
        range = lineRangeAt(text, range.end + 1);
    }
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

static QVector<OrderedListSequence> orderedSequences(
    const QVector<ParsedListLine> &lines,
    const QVector<std::pair<int, int>> &regions)
{
    struct ActiveLevel {
        int indent;
        int sequence;
        QChar delimiter;
    };
    QVector<OrderedListSequence> sequences;
    for (const auto &[regionStart, regionEnd] : regions) {
        QVector<ActiveLevel> levels;
        int quoteDepth = -1;
        for (int index = regionStart; index <= regionEnd; ++index) {
            const MarkdownListItem &item = lines.at(index).item;
            if (item.quoteDepth != quoteDepth) {
                levels.clear();
                quoteDepth = item.quoteDepth;
            }
            while (!levels.isEmpty() && levels.back().indent > item.indentColumns) {
                levels.pop_back();
            }
            int sequence = -1;
            if (!levels.isEmpty() && levels.back().indent == item.indentColumns) {
                const ActiveLevel previous = levels.takeLast();
                if (previous.delimiter == item.delimiter) {
                    sequence = previous.sequence;
                }
            }
            if (item.ordered) {
                if (sequence < 0) {
                    sequence = sequences.size();
                    sequences.append({orderedListSignature(item), item.number, {}});
                }
                sequences[sequence].lineIndexes.append(index);
            }
            levels.append({item.indentColumns, sequence, item.delimiter});
        }
    }
    return sequences;
}

QVector<TextReplacement> orderedListNumberReplacements(const QString &beforeText,
                                                       const QString &afterText,
                                                       bool preservePreviousStart,
                                                       bool resetNewSequenceStart)
{
    if (beforeText == afterText) {
        return {};
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
        } else if (resetNewSequenceStart) {
            // Tab 新建的缩进层没有可继承的旧同层序列时，从 Markdown 常规起点 1 开始。
            expectedNumber = 1;
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

    return replacements;
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

} // namespace MarkdownLists
