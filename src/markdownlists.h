#pragma once

#include <QString>
#include <QVector>

#include <optional>

namespace MarkdownLists {

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

struct TextReplacement {
    int start = 0;
    int length = 0;
    QString replacement;
};

struct OrderedListConversionPlan {
    QVector<TextReplacement> replacements;
    int selectionStart = -1;
    int selectionEnd = -1;
    bool handled = false;
};

MarkdownQuoteLine parseMarkdownQuoteLine(const QString &line);
int oneLevelOutdentLength(const QString &line);
MarkdownListItem parseMarkdownListItem(const QString &line);
QString childMarkerForParent(const MarkdownListItem &parent);
std::optional<MarkdownListItem> parentListItemForOutdent(
    const QString &text, int lineStart, const MarkdownListItem &item,
    int targetIndentColumns);
QVector<TextReplacement> orderedListNumberReplacements(const QString &beforeText,
                                                       const QString &afterText,
                                                       bool preservePreviousStart,
                                                       bool resetNewSequenceStart);
OrderedListConversionPlan orderedListConversionPlan(const QString &text,
                                                    int selectionStart,
                                                    int selectionEnd);
bool orderedListStructureAffected(const QString &text, int start, int end);
bool orderedListEditRequiresRepair(const QString &beforeText, const QString &afterText,
                                   int start, int beforeEnd, int afterEnd);

} // namespace MarkdownLists
