#include "editorcommandregistry.h"
#include "appsettings.h"
#include "cjktextprocessor.h"

#include <QEvent>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeySequence>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QQuickItem>
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
    const int lineStart = text.lastIndexOf(QLatin1Char('\n'), qMax(0, position - 1)) + 1;
    if (position <= lineStart) {
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

    const QuotePair *pair = quotePairFor(typed);
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
        QVector<QChar> openStack;
        int lastOpenerPosition = -1;
        for (int i = lineStart; i < position; ++i) {
            const QChar character = text.at(i);
            const QuotePair *currentPair = quotePairFor(character);
            if (!currentPair) {
                continue;
            }
            if (!openStack.isEmpty() && currentPair->closing == openStack.back()) {
                openStack.pop_back();
                continue;
            }
            openStack.append(currentPair->opening);
            lastOpenerPosition = i;
        }
        plan.closer = !openStack.isEmpty() && pair->closing == openStack.back();
        plan.openerPosition = plan.closer ? lastOpenerPosition : -1;
    }
    return plan;
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

void renumberFollowingOrderedItems(QTextDocument *document, int currentLineStart,
                                   const MarkdownListItem &currentItem)
{
    if (!document || !currentItem.ordered) {
        return;
    }

    const QString text = document->toPlainText();
    int scanStart = text.indexOf(QLatin1Char('\n'), currentLineStart);
    if (scanStart < 0) {
        return;
    }
    ++scanStart;
    qlonglong expectedNumber = currentItem.number + 1;
    QVector<TextReplacement> replacements;
    while (scanStart <= text.size()) {
        int scanEnd = text.indexOf(QLatin1Char('\n'), scanStart);
        if (scanEnd < 0) {
            scanEnd = text.size();
        }
        const QString line = text.mid(scanStart, scanEnd - scanStart);
        if (line.trimmed().isEmpty()) {
            break;
        }

        const MarkdownListItem candidate = parseMarkdownListItem(line);
        if (!candidate.valid) {
            break;
        }
        const bool sameLevel = candidate.indentColumns == currentItem.indentColumns
            && candidate.quoteDepth == currentItem.quoteDepth;
        const bool deeperLevel = candidate.indentColumns > currentItem.indentColumns
            && candidate.quoteDepth == currentItem.quoteDepth;
        if (deeperLevel) {
            if (scanEnd == text.size()) {
                break;
            }
            scanStart = scanEnd + 1;
            continue;
        }
        if (!sameLevel || !candidate.ordered
            || candidate.delimiter != currentItem.delimiter) {
            break;
        }

        const QString expectedText = QString::number(expectedNumber);
        if (candidate.numberText != expectedText) {
            replacements.append({scanStart + candidate.numberStart,
                                 static_cast<int>(candidate.numberText.size()), expectedText});
        }
        ++expectedNumber;
        if (scanEnd == text.size()) {
            break;
        }
        scanStart = scanEnd + 1;
    }

    for (auto replacement = replacements.crbegin(); replacement != replacements.crend();
         ++replacement) {
        QTextCursor cursor(document);
        cursor.setPosition(replacement->start);
        cursor.setPosition(replacement->start + replacement->length,
                           QTextCursor::KeepAnchor);
        cursor.insertText(replacement->replacement);
    }
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
    return {start, end};
}

} // namespace

EditorCommandRegistry::EditorCommandRegistry(AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
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
    };

    for (Definition &item : m_definitions) {
        item.shortcut = m_settings ? m_settings->shortcut(item.id, item.defaultShortcut)
                                   : item.defaultShortcut;
    }

    m_selectionDragScrollTimer.setInterval(30);
    connect(&m_selectionDragScrollTimer, &QTimer::timeout, this, [this] {
        updateSelectionDrag(m_selectionDragScenePosition, true);
    });
}

void EditorCommandRegistry::setEditor(QObject *editor, QTextDocument *document)
{
    resetSelectionDrag(true);
    m_editor = editor;
    m_document = document;
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

    if (commandId == QStringLiteral("formatSpacing")) {
        return formatSpacing();
    }
    if (commandId == QStringLiteral("toggleBold")) {
        return wrapSelection(QStringLiteral("**"), QStringLiteral("**"));
    }
    if (commandId == QStringLiteral("toggleItalic")) {
        return wrapSelection(QStringLiteral("*"), QStringLiteral("*"));
    }
    if (commandId == QStringLiteral("wrapCode")) {
        const QString selection = selectedText();
        if (selection.contains(QLatin1Char('\n'))) {
            return wrapSelection(QStringLiteral("```\n"), QStringLiteral("\n```"));
        }
        return wrapSelection(QStringLiteral("`"), QStringLiteral("`"));
    }
    if (commandId == QStringLiteral("deleteLine")) {
        return deleteSelectedLines();
    }
    if (commandId == QStringLiteral("copyLine")) {
        return copyLine();
    }
    if (commandId == QStringLiteral("cutLine")) {
        return cutLine();
    }
    if (commandId == QStringLiteral("pasteClipboard")) {
        return pasteClipboard();
    }
    if (commandId == QStringLiteral("toggleCheckbox")) {
        return toggleCurrentCheckbox();
    }
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
        if (ctrlZ && m_formatUndoSnapshot) {
            const QString currentText = m_document->toPlainText();
            if (currentText == m_formatUndoSnapshot->formattedText) {
                QMetaObject::invokeMethod(m_editor, "undo");
                if (m_document->toPlainText() == m_formatUndoSnapshot->originalText) {
                    const int activeEnd = m_formatUndoSnapshot->cursorPosition
                            == m_formatUndoSnapshot->selectionEnd
                        ? m_formatUndoSnapshot->selectionEnd
                        : m_formatUndoSnapshot->selectionStart;
                    selectRangeWithActiveEnd(m_formatUndoSnapshot->selectionStart,
                                             m_formatUndoSnapshot->selectionEnd,
                                             activeEnd);
                }
                m_formatUndoSnapshot.reset();
                focusEditor();
                return true;
            }
            m_formatUndoSnapshot.reset();
        }
        // 无选区时拦截 Ctrl+C / Ctrl+X / Ctrl+V，执行整行复制、剪切与智能粘贴；
        // 有选区时保持 TextEdit 标准行为（复制/剪切/替换选区）。
        const bool plainCtrl = modifiers.testFlag(Qt::ControlModifier)
            && !modifiers.testFlag(Qt::ShiftModifier)
            && !modifiers.testFlag(Qt::AltModifier)
            && !modifiers.testFlag(Qt::MetaModifier);
        if (plainCtrl && !m_editor->property("inputMethodComposing").toBool()
            && m_editor->property("selectionStart").toInt()
                == m_editor->property("selectionEnd").toInt()) {
            if (keyEvent->key() == Qt::Key_C) {
                return copyLine();
            }
            if (keyEvent->key() == Qt::Key_X
                && !m_editor->property("readOnly").toBool()) {
                return cutLine();
            }
            if (keyEvent->key() == Qt::Key_V
                && !m_editor->property("readOnly").toBool()) {
                return pasteClipboard();
            }
        }
        const bool tabPressed = keyEvent->key() == Qt::Key_Tab
            || keyEvent->key() == Qt::Key_Backtab;
        const bool plainBackspace = keyEvent->key() == Qt::Key_Backspace
            && !(modifiers & (Qt::ShiftModifier | Qt::ControlModifier
                              | Qt::AltModifier | Qt::MetaModifier));
        if (plainBackspace && handleSpecialBackspace()) {
            return true;
        }
        const bool plainEnter = (keyEvent->key() == Qt::Key_Return
                                 || keyEvent->key() == Qt::Key_Enter)
            && !(modifiers & (Qt::ShiftModifier | Qt::ControlModifier
                              | Qt::AltModifier | Qt::MetaModifier));
        if (plainEnter && !m_editor->property("inputMethodComposing").toBool()
            && handleListEnter()) {
            return true;
        }
        if (tabPressed
            && !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            if (shiftPressed || keyEvent->key() == Qt::Key_Backtab) {
                return changeIndent(true);
            }
            return jumpOutOfPair() || changeIndent(false);
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
            const int start = m_editor->property("selectionStart").toInt();
            const int end = m_editor->property("selectionEnd").toInt();
            const QString beforeText = m_document->toPlainText();
            const QString text = keyEvent->text();
            // 把字符插入与自动空格放进同一个 edit block，使一次 Ctrl+Z 能整体撤销。
            QTextCursor undoGroupCursor(m_document);
            undoGroupCursor.beginEditBlock();
            const TypedEditResult result = handleTypedText(text);
            if (!result.consumed) {
                QTimer::singleShot(0, this,
                                   [this, beforeText, start, end, text, undoGroupCursor]() mutable {
                                    if (!m_editor || !m_document) {
                                        return;
                                    }
                                    QString expected = beforeText;
                                    expected.replace(start, end - start, text);
                                    if (m_document->toPlainText() == expected) {
                                        applyAutoSpacing({start, start + static_cast<int>(text.size())},
                                                         text.size() > 1);
                                    }
                                    undoGroupCursor.endEditBlock();
                                });
                return false;
            }
            if (result.textChanged && result.runAutoSpacing) {
                applyAutoSpacing(result.footprint);
            }
            undoGroupCursor.endEditBlock();
            return true;
        }
        return false;
    }

    if (event->type() == QEvent::InputMethod) {
        const auto *inputEvent = static_cast<QInputMethodEvent *>(event);
        const QString committedText = inputEvent->commitString();
        if (committedText.isEmpty()) {
            return false;
        }
        const bool relevant = committedText == QStringLiteral("```")
            || committedText == QStringLiteral("`")
            || pairForOpening(committedText)
            || isClosingDelimiter(committedText);
        const int start = m_editor->property("selectionStart").toInt();
        const int end = m_editor->property("selectionEnd").toInt();
        const QString beforeText = m_document->toPlainText();
        const QString selection = selectedText();
        if (!relevant) {
            QTimer::singleShot(0, this,
                               [this, beforeText, start, end, committedText] {
                if (!m_editor || !m_document) {
                    return;
                }
                QTextCursor undoGroupCursor(m_document);
                undoGroupCursor.beginEditBlock();
                QString expected = beforeText;
                expected.replace(start, end - start, committedText);
                if (m_document->toPlainText() == expected) {
                    applyAutoSpacing(
                        {start, start + static_cast<int>(committedText.size())},
                        committedText.size() > 1);
                }
                undoGroupCursor.endEditBlock();
            });
            return false;
        }

        QTimer::singleShot(0, this,
                           [this, committedText, beforeText, selection, start, end] {
            if (!m_editor || !m_document) {
                return;
            }
            QTextCursor undoGroupCursor(m_document);
            undoGroupCursor.beginEditBlock();
            const auto completion = completeInputMethodCommit(
                committedText, beforeText, selection, start, end);
            if (completion && completion->autoSpace) {
                applyAutoSpacing(completion->footprint);
            }
            undoGroupCursor.endEditBlock();
        });
    }
    return false;
}

bool EditorCommandRegistry::moveSelection(int selectionStart, int selectionEnd,
                                          int dropPosition)
{
    if (!m_editor || !m_document) {
        return false;
    }

    const int documentLength = m_document->toPlainText().size();
    if (selectionStart < 0 || selectionEnd < 0 || dropPosition < 0
        || selectionStart > documentLength || selectionEnd > documentLength
        || dropPosition > documentLength) {
        return false;
    }
    if (selectionStart >= selectionEnd
        || (dropPosition >= selectionStart && dropPosition <= selectionEnd)) {
        return false;
    }

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
    cursor.endEditBlock();

    selectRange(adjustedDrop, adjustedDrop + movedText.size());
    focusEditor();
    return true;
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
    return invoked ? qBound(0, position, m_document->toPlainText().size()) : -1;
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

void EditorCommandRegistry::beginSelectionDrag(int selectionStart, int selectionEnd,
                                                const QPointF &scenePosition)
{
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
        if (QQuickItem *viewport = editorViewport()) {
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
                    scrollDelta = -qBound<qreal>(2.0,
                                                 (edge - viewportPosition.y()) * 0.5,
                                                 24.0);
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

    QTextCursor cursor(m_document);
    cursor.setPosition(m_editor->property("selectionStart").toInt());
    cursor.setPosition(m_editor->property("selectionEnd").toInt(), QTextCursor::KeepAnchor);
    const int insertedAt = cursor.selectionStart();
    cursor.insertText(replacement);
    selectRange(insertedAt, insertedAt + replacement.size());
    return true;
}

int EditorCommandRegistry::replaceAll(const QString &query, const QString &replacement,
                                      bool caseSensitive)
{
    if (!m_document || query.isEmpty()) {
        return 0;
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

    const QString documentText = m_document->toPlainText();
    const int originalStart = m_editor->property("selectionStart").toInt();
    const int originalEnd = m_editor->property("selectionEnd").toInt();
    const int lineStart = documentText.lastIndexOf(QLatin1Char('\n'), qMax(0, originalStart - 1)) + 1;
    int lineEnd = documentText.indexOf(QLatin1Char('\n'), originalEnd);
    if (lineEnd < 0) {
        lineEnd = documentText.size();
    }
    const QString segment = documentText.mid(lineStart, lineEnd - lineStart);
    QStringList lines = segment.split(QLatin1Char('\n'), Qt::KeepEmptyParts);

    static const QRegularExpression listPrefix(QStringLiteral(R"(^\s*(?:[-+*]|\d+\.)\s+)"));
    static const QRegularExpression taskPrefix(QStringLiteral(R"(^\s*[-+*]\s+\[[ xX]\]\s+)"));
    static const QRegularExpression quotePrefix(QStringLiteral(R"(^\s*>\s?)"));
    static const QRegularExpression headingPrefix(QStringLiteral(R"(^\s*(#{1,6})\s+)"));
    const QRegularExpressionMatch originalHeadingMatch = headingCommand
        && originalStart == originalEnd ? headingPrefix.match(segment)
                                        : QRegularExpressionMatch();
    const int originalHeadingPrefixLength = originalHeadingMatch.hasMatch()
        ? originalHeadingMatch.capturedLength() : 0;
    const auto targetHeadingLevel = [&commandId, setHeading](int currentLevel) {
        if (setHeading) {
            return commandId.back().digitValue();
        }
        if (commandId == QStringLiteral("increaseHeadingLevel")) {
            return currentLevel == 0 ? 0 : qMin(6, currentLevel + 1);
        }
        if (commandId == QStringLiteral("decreaseHeadingLevel")) {
            return currentLevel == 0 ? 0 : qMax(1, currentLevel - 1);
        }
        if (currentLevel == 0) {
            return 1;
        }
        return currentLevel < 6 ? currentLevel + 1 : 0;
    };

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
        && allNonEmptyMatch(listPrefix);
    const bool removeTask = commandId == QStringLiteral("toggleTask")
        && allNonEmptyMatch(taskPrefix);
    const bool removeQuote = commandId == QStringLiteral("toggleQuote")
        && allNonEmptyMatch(quotePrefix);

    for (QString &line : lines) {
        if (line.trimmed().isEmpty()) {
            if (headingCommand && !adjustHeading && originalStart == originalEnd
                && lines.size() == 1) {
                const int targetLevel = targetHeadingLevel(0);
                line = QString(targetLevel, QLatin1Char('#')) + QLatin1Char(' ');
            } else if (commandId == QStringLiteral("toggleQuote") && !removeQuote) {
                line = QStringLiteral("> ");
            }
            continue;
        }
        if (headingCommand) {
            const QRegularExpressionMatch match = headingPrefix.match(line);
            if (adjustHeading && !match.hasMatch()) {
                // Ctrl+Num+- / Ctrl+Num++ 只推进或回退已经存在的标题行。
                continue;
            }
            const int currentLevel = match.hasMatch() ? match.captured(1).size() : 0;
            int targetLevel = targetHeadingLevel(currentLevel);
            const bool togglesMatchingHeading = setHeading && match.hasMatch()
                && currentLevel == commandId.back().digitValue();
            if (togglesMatchingHeading) {
                targetLevel = 0;
            }

            if (match.hasMatch()) {
                line.remove(match.capturedStart(), match.capturedLength());
            }
            if (targetLevel > 0) {
                line.prepend(QString(targetLevel, QLatin1Char('#')) + QLatin1Char(' '));
            }
        } else if (commandId == QStringLiteral("toggleList")) {
            if (removeList) {
                line.remove(listPrefix);
            } else if (!listPrefix.match(line).hasMatch()) {
                line.prepend(QStringLiteral("- "));
            }
        } else if (commandId == QStringLiteral("toggleTask")) {
            if (removeTask) {
                line.remove(taskPrefix);
            } else {
                line.remove(listPrefix);
                line.prepend(QStringLiteral("- [ ] "));
            }
        } else if (commandId == QStringLiteral("toggleQuote")) {
            if (removeQuote) {
                line.remove(quotePrefix);
            } else if (!quotePrefix.match(line).hasMatch()) {
                line.prepend(QStringLiteral("> "));
            }
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
    cursor.endEditBlock();
    if (headingCommand && originalStart == originalEnd) {
        const QRegularExpressionMatch transformedHeadingMatch = headingPrefix.match(transformed);
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
    focusEditor();
    return true;
}

bool EditorCommandRegistry::deleteSelectedLines()
{
    const QString text = m_document->toPlainText();
    const int start = m_editor->property("selectionStart").toInt();
    const int end = m_editor->property("selectionEnd").toInt();
    const int lineStart = text.lastIndexOf(QLatin1Char('\n'), qMax(0, start - 1)) + 1;
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
    cursor.endEditBlock();
    m_editor->setProperty("cursorPosition", removeStart);
    focusEditor();
    return true;
}

bool EditorCommandRegistry::copyLine()
{
    if (!m_editor || !m_document || !m_clipboardWriter) {
        return false;
    }

    const QString text = m_document->toPlainText();
    const int cursor = m_editor->property("cursorPosition").toInt();
    const int lineStart = text.lastIndexOf(QLatin1Char('\n'), qMax(0, cursor - 1)) + 1;
    int lineEnd = text.indexOf(QLatin1Char('\n'), cursor);
    if (lineEnd < 0) {
        lineEnd = text.size();
    }
    // 整行复制统一携带行尾换行符（末行/单行文档也补上），保证粘贴语义一致。
    m_clipboardWriter(text.mid(lineStart, lineEnd - lineStart) + QLatin1Char('\n'));
    focusEditor();
    return true;
}

bool EditorCommandRegistry::cutLine()
{
    if (!m_editor || !m_document || !m_clipboardWriter) {
        return false;
    }

    const QString text = m_document->toPlainText();
    const int cursor = m_editor->property("cursorPosition").toInt();
    const int lineStart = text.lastIndexOf(QLatin1Char('\n'), qMax(0, cursor - 1)) + 1;
    int lineEnd = text.indexOf(QLatin1Char('\n'), cursor);
    const bool lastLine = lineEnd < 0;
    if (lastLine) {
        lineEnd = text.size();
    }

    m_clipboardWriter(text.mid(lineStart, lineEnd - lineStart) + QLatin1Char('\n'));

    // 非末行删除整行含换行，后续行补位；末行只删行文本。
    QTextCursor editCursor(m_document);
    editCursor.beginEditBlock();
    editCursor.setPosition(lineStart);
    editCursor.setPosition(lastLine ? lineEnd : lineEnd + 1, QTextCursor::KeepAnchor);
    editCursor.removeSelectedText();
    editCursor.endEditBlock();
    // 光标落在补位后一行的行首；剪切末行时落在上一行行尾。
    m_editor->setProperty("cursorPosition", lineStart);
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
    const bool smartLinePaste = selectionStart == selectionEnd
        && clipboardText.endsWith(QLatin1Char('\n'));

    int insertionPoint = selectionStart;
    QString insertion = clipboardText;
    if (smartLinePaste) {
        // 智能粘贴：剪贴板是“整行（以换行结尾）”时插入为当前行下方的新行，
        // 当前行原样保留；仅当当前行仍有内容（末行非空）时才先补一个换行。
        // 空文档、文档已以换行结尾（光标位于末尾空行）时直接插入，
        // 避免每次粘贴都新增一个前导空行。
        const int lineStart = text.lastIndexOf(
            QLatin1Char('\n'), qMax(0, selectionStart - 1)) + 1;
        int lineEnd = text.indexOf(QLatin1Char('\n'), selectionStart);
        const bool lastLine = lineEnd < 0;
        if (lastLine) {
            lineEnd = text.size();
            if (lineStart < lineEnd) {
                insertion.prepend(QLatin1Char('\n'));
            }
        }
        insertionPoint = lastLine ? lineEnd : lineEnd + 1;
    }

    QTextCursor editCursor(m_document);
    editCursor.beginEditBlock();
    editCursor.setPosition(insertionPoint);
    if (selectionStart != selectionEnd) {
        editCursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    }
    editCursor.insertText(insertion);
    editCursor.endEditBlock();

    int cursorAfter = insertionPoint + insertion.size();
    if (smartLinePaste) {
        // 光标落在新粘贴行行尾（不含换行），连续 Ctrl+V 会在下方不断堆叠新行；
        // 剪贴板恰为单个空行（"\n"）时，光标落在该空行上。
        cursorAfter = insertionPoint + qMax(1, insertion.size() - 1);
    }
    m_editor->setProperty("cursorPosition", cursorAfter);
    selectRange(cursorAfter, cursorAfter);
    focusEditor();
    return true;
}

bool EditorCommandRegistry::toggleCurrentCheckbox()
{
    const QString text = m_document->toPlainText();
    const int cursorPosition = m_editor->property("cursorPosition").toInt();
    const int lineStart = text.lastIndexOf(
        QLatin1Char('\n'), qMax(0, cursorPosition - 1)) + 1;
    int lineEnd = text.indexOf(QLatin1Char('\n'), cursorPosition);
    if (lineEnd < 0) {
        lineEnd = text.size();
    }
    const QString line = text.mid(lineStart, lineEnd - lineStart);
    const int positionInLine = cursorPosition - lineStart;

    static const QRegularExpression checkboxPattern(QStringLiteral(
        R"(^([\t ]*(?:>[\t ]*)*(?:(?:[-+*]|\d+[.)])[\t ]+)?)\[([ xX])\])"));
    const QRegularExpressionMatch checkboxMatch = checkboxPattern.match(line);
    QTextCursor editCursor(m_document);
    editCursor.beginEditBlock();
    int updatedCursorPosition = cursorPosition;
    if (checkboxMatch.hasMatch()) {
        const int statePosition = lineStart + checkboxMatch.capturedStart(2);
        QTextCursor stateCursor(m_document);
        stateCursor.setPosition(statePosition);
        stateCursor.setPosition(statePosition + 1, QTextCursor::KeepAnchor);
        stateCursor.insertText(checkboxMatch.captured(2) == QStringLiteral(" ")
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
        insertionCursor.setPosition(lineStart + insertionOffset);
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
    if (text == QStringLiteral("```")) {
        if (const auto footprint = insertFenceBlock()) {
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

    const QString documentText = m_document->toPlainText();
    const int start = m_editor->property("selectionStart").toInt();
    const int end = m_editor->property("selectionEnd").toInt();
    const bool hasSelection = start != end;

    if (text == QStringLiteral("-") && !hasSelection) {
        const int lineStart =
            documentText.lastIndexOf(QLatin1Char('\n'), qMax(0, start - 1)) + 1;
        int lineEnd = documentText.indexOf(QLatin1Char('\n'), start);
        if (lineEnd < 0) {
            lineEnd = documentText.size();
        }
        if (start == lineStart && start == lineEnd) {
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

    if (text == QStringLiteral("`") && !hasSelection) {
        if (start >= 2 && start < documentText.size()
            && documentText.mid(start - 2, 3) == QStringLiteral("```")) {
            QTextCursor cursor(m_document);
            cursor.setPosition(start - 2);
            cursor.setPosition(start + 1, QTextCursor::KeepAnchor);
            cursor.insertText(QStringLiteral("```\n```"));
            m_editor->setProperty("cursorPosition", start + 1);
            result.consumed = true;
            result.textChanged = true;
            return result;
        }
        if (start > 0 && start < documentText.size()
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
                convertAsciiQuoteWrap(m_document, openerPosition, start);
                m_editor->setProperty("cursorPosition", start + 1);
                result.consumed = true;
                // 包裹闭合（即使只是跳过已有闭符号）也要重新执行自动空格判断。
                result.textChanged = true;
                result.runAutoSpacing = true;
                result.footprint = {openerPosition, start + 1};
                return result;
            }

            QTextCursor cursor(m_document);
            cursor.setPosition(start);
            cursor.beginEditBlock();
            cursor.insertText(QString(midlinePlan->closing));
            cursor.endEditBlock();
            convertAsciiQuoteWrap(m_document, openerPosition, start);
            m_editor->setProperty("cursorPosition", start + 1);
            focusEditor();
            result.consumed = true;
            // 闭合引号已插入，必须执行自动空格判断（无论是否发生全角转换）。
            result.textChanged = true;
            result.runAutoSpacing = true;
            result.footprint = {openerPosition, start + 1};
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
    const CjkText::DocumentAnalysis analysis = conversionCandidate
        ? CjkText::analyzeDocument(documentText)
        : CjkText::DocumentAnalysis{};
    const bool protectedPosition =
        conversionCandidate && CjkText::isPositionProtected(analysis, start);

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

        // 3. ASCII Punctuation after CJK → Fullwidth
        if (start > 0 && CjkText::isCjk(documentText.at(start - 1))) {
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
                            insertPair(pairIt->first, pairIt->second)) {
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
            if (it != asciiToFull.end()) {
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

        // 4. Chained Fullwidth: ，, → ，，   。. → 。。
        if (start > 0) {
            const QChar prev = documentText.at(start - 1);
            if ((prev == u'\uFF0C' && ch == u',')
                || (prev == u'\u3002' && ch == u'.'
                    && (start < 2 || documentText.at(start - 2) != u'.'))) {
                const QString full =
                    (ch == u',') ? QStringLiteral("，") : QStringLiteral("。");
                QTextCursor cursor(m_document);
                cursor.setPosition(start);
                cursor.insertText(full);
                m_editor->setProperty("cursorPosition", start + 1);
                focusEditor();
                result.consumed = true;
                result.textChanged = true;
                return result;
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
        const CjkText::DocumentAnalysis selectionAnalysis = hasSelection
            ? CjkText::analyzeDocument(documentText)
            : CjkText::DocumentAnalysis{};
        const bool selectionProtected = hasSelection
            && (CjkText::isPositionProtected(selectionAnalysis, start)
                || CjkText::isPositionProtected(selectionAnalysis, end));
        const auto resolvedPair = selectionProtected
            ? std::pair<QString, QString>{pair->opening, pair->closing}
            : CjkText::resolveSelectionPair(pair->opening, pair->closing, selection);
        if (const auto footprint = insertPair(resolvedPair.first, resolvedPair.second)) {
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

std::optional<EditorCommandRegistry::EditFootprint> EditorCommandRegistry::insertPair(
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

std::optional<EditorCommandRegistry::EditFootprint> EditorCommandRegistry::insertFenceBlock()
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
    const QString opening = QStringLiteral("```");
    const QString closing = QStringLiteral("\n```");

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

bool EditorCommandRegistry::handleSpecialBackspace()
{
    const int start = m_editor->property("selectionStart").toInt();
    const int end = m_editor->property("selectionEnd").toInt();
    if (start != end) {
        return false;
    }

    const QString text = m_document->toPlainText();
    const int lineStart = text.lastIndexOf(QLatin1Char('\n'), qMax(0, start - 1)) + 1;
    int lineEnd = text.indexOf(QLatin1Char('\n'), start);
    if (lineEnd < 0) {
        lineEnd = text.size();
    }
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

        if (emptyListItem.ordered && lineStart > 0) {
            const QString updatedText = m_document->toPlainText();
            const int previousLineStart = updatedText.lastIndexOf(
                QLatin1Char('\n'), qMax(0, removeStart - 1)) + 1;
            int previousLineEnd = updatedText.indexOf(QLatin1Char('\n'), previousLineStart);
            if (previousLineEnd < 0) {
                previousLineEnd = updatedText.size();
            }
            const MarkdownListItem previousItem = parseMarkdownListItem(
                updatedText.mid(previousLineStart, previousLineEnd - previousLineStart));
            if (previousItem.valid && previousItem.ordered
                && previousItem.indentColumns == emptyListItem.indentColumns
                && previousItem.quoteDepth == emptyListItem.quoteDepth
                && previousItem.delimiter == emptyListItem.delimiter) {
                renumberFollowingOrderedItems(m_document, previousLineStart, previousItem);
            }
        }
        editCursor.endEditBlock();
        m_editor->setProperty("cursorPosition", removeStart);
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

bool EditorCommandRegistry::handleListEnter()
{
    const int start = m_editor->property("selectionStart").toInt();
    const int end = m_editor->property("selectionEnd").toInt();
    if (start != end) {
        return false;
    }

    const QString text = m_document->toPlainText();
    const int lineStart = text.lastIndexOf(QLatin1Char('\n'), qMax(0, start - 1)) + 1;
    int lineEnd = text.indexOf(QLatin1Char('\n'), start);
    if (lineEnd < 0) {
        lineEnd = text.size();
    }
    if (isInsideFencedBlock(lineStart)) {
        return false;
    }

    const MarkdownListItem item = parseMarkdownListItem(
        text.mid(lineStart, lineEnd - lineStart));
    const int positionInLine = start - lineStart;
    if (!item.valid || positionInLine < item.contentStart) {
        return false;
    }

    QTextCursor editCursor(m_document);
    editCursor.beginEditBlock();
    if (item.isEmpty()) {
        const int removeStart = lineStart + item.markerStart;
        QTextCursor removalCursor(m_document);
        removalCursor.setPosition(removeStart);
        removalCursor.setPosition(lineEnd, QTextCursor::KeepAnchor);
        removalCursor.removeSelectedText();
        editCursor.endEditBlock();
        m_editor->setProperty("cursorPosition", removeStart);
        focusEditor();
        return true;
    }

    const QString continuation = item.continuationPrefix();
    QTextCursor insertionCursor(m_document);
    insertionCursor.setPosition(start);
    insertionCursor.insertText(QLatin1Char('\n') + continuation);
    const int newLineStart = start + 1;
    const int cursorPosition = newLineStart + continuation.size();

    if (item.ordered) {
        const QString updatedText = m_document->toPlainText();
        int newLineEnd = updatedText.indexOf(QLatin1Char('\n'), newLineStart);
        if (newLineEnd < 0) {
            newLineEnd = updatedText.size();
        }
        const MarkdownListItem insertedItem = parseMarkdownListItem(
            updatedText.mid(newLineStart, newLineEnd - newLineStart));
        renumberFollowingOrderedItems(m_document, newLineStart, insertedItem);
    }
    editCursor.endEditBlock();

    m_editor->setProperty("cursorPosition", cursorPosition);
    focusEditor();
    return true;
}

bool EditorCommandRegistry::jumpOutOfPair()
{
    const int start = m_editor->property("selectionStart").toInt();
    const int end = m_editor->property("selectionEnd").toInt();
    if (start != end) {
        return false;
    }

    const QString text = m_document->toPlainText();
    const int lineStart = text.lastIndexOf(QLatin1Char('\n'), qMax(0, start - 1)) + 1;
    int lineEnd = text.indexOf(QLatin1Char('\n'), start);
    if (lineEnd < 0) {
        lineEnd = text.size();
    }
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
    const auto consumeDelimiter = [&pairs](QVector<int> &delimiterStack, QChar character) {
        if (!delimiterStack.isEmpty()
            && pairs.at(delimiterStack.back()).closing.front() == character) {
            delimiterStack.pop_back();
            return;
        }
        for (int index = 0; index < pairs.size(); ++index) {
            if (pairs.at(index).opening.front() == character) {
                delimiterStack.append(index);
                return;
            }
        }
    };

    for (int position = lineStart; position < start; ++position) {
        consumeDelimiter(stack, text.at(position));
    }
    if (!stack.isEmpty()) {
        const int containingDepth = stack.size();
        for (int position = start; position < text.size()
             && text.at(position) != QLatin1Char('\n'); ++position) {
            consumeDelimiter(stack, text.at(position));
            if (stack.size() < containingDepth) {
                const int candidate = position + 1;
                jumpPosition = jumpPosition < 0 ? candidate : qMin(jumpPosition, candidate);
                break;
            }
        }
    }

    if (jumpPosition < 0) {
        return false;
    }
    m_editor->setProperty("cursorPosition", jumpPosition);
    focusEditor();
    return true;
}

bool EditorCommandRegistry::changeIndent(bool outdent)
{
    const QString text = m_document->toPlainText();
    const int start = m_editor->property("selectionStart").toInt();
    const int end = m_editor->property("selectionEnd").toInt();
    const int lineStart = text.lastIndexOf(QLatin1Char('\n'), qMax(0, start - 1)) + 1;

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
    cursor.endEditBlock();
    if (start == end) {
        const int delta = transformed.size() - (lineEnd - lineStart);
        m_editor->setProperty("cursorPosition", qMax(lineStart, start + delta));
    } else {
        selectRange(lineStart, lineStart + transformed.size());
    }
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
    QString expectedText = beforeText;
    expectedText.replace(selectionStart, selectionEnd - selectionStart, committedText);
    if (m_document->toPlainText() != expectedText) {
        return std::nullopt;
    }

    // 行中引号与键盘路径一致：只保留单个开符号；闭合时收尾并格式化。
    if (selectionStart == selectionEnd && committedText.size() == 1) {
        const QString currentText = m_document->toPlainText();
        const auto midlinePlan = buildMidlineQuotePlan(
            currentText, selectionStart, committedText.at(0));
        if (midlinePlan) {
            if (!midlinePlan->closer) {
                return CompletionResult{
                    {selectionStart, selectionStart + 1},
                    /*autoSpace=*/false};
            }

            const int openerPosition = midlinePlan->openerPosition;
            const bool closingAtCursor =
                currentText.mid(selectionStart, 1) == QString(midlinePlan->closing);
            if (closingAtCursor) {
                // 已提交的闭引号与光标处已有闭引号是同一个字符，
                // 保留该字符并直接对包裹两端做转换，再跳过到其后。
                convertAsciiQuoteWrap(m_document, openerPosition, selectionStart);
                m_editor->setProperty("cursorPosition", selectionStart + 1);
                return CompletionResult{{openerPosition, selectionStart + 1}};
            }

            QTextCursor cursor(m_document);
            cursor.setPosition(selectionStart);
            cursor.setPosition(selectionStart + 1, QTextCursor::KeepAnchor);
            cursor.beginEditBlock();
            cursor.insertText(QString(midlinePlan->closing));
            cursor.endEditBlock();
            convertAsciiQuoteWrap(m_document, openerPosition, selectionStart);
            m_editor->setProperty("cursorPosition", selectionStart + 1);
            return CompletionResult{{openerPosition, selectionStart + 1}};
        }
    }

    const DelimiterPair *openingPair = pairForOpening(committedText);
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
        replacement = QStringLiteral("```") + selection + QStringLiteral("\n```");
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
        rangeStart = documentText.lastIndexOf(
            QLatin1Char('\n'), qMax(0, start - 1)) + 1;
        int lineEnd = documentText.indexOf(QLatin1Char('\n'), start);
        if (lineEnd < 0) {
            lineEnd = documentText.size();
        }
        rangeEnd = lineEnd;
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
        m_formatUndoSnapshot = FormatUndoSnapshot{
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

void EditorCommandRegistry::applyAutoSpacing(EditFootprint footprint,
                                             bool includeInternalBoundaries)
{
    if (!m_editor || !m_document || footprint.start > footprint.end) {
        return;
    }
    const QString text = m_document->toPlainText();
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
    if (insertions.isEmpty()) {
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

    // 光标恰位于插入点时保持在其左侧：右外侧自动空格属于“已输入片段之后”的边界，
    // 光标停在空格之前可让后续连续 ASCII 输入并入同一片段，而不是被逐个空格拆开。
    const int newCursor =
        CjkText::positionAfterInsertions(cursorPosition, insertions, false);
    const int newSelectionStart =
        CjkText::positionAfterInsertions(selectionStart, insertions, false);
    const int newSelectionEnd =
        CjkText::positionAfterInsertions(selectionEnd, insertions, true);
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
