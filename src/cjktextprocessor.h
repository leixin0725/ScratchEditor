#pragma once

#include <QChar>
#include <QString>
#include <QVector>

#include <utility>

namespace CjkText {

enum class ProtectedKind {
    FencedCode,
    BlockFormula,
    InlineCode,
    InlineFormula,
    InlineQuote,
};

struct ProtectedSpan {
    int outerStart = 0;   // opening delimiter 的首位置
    int outerEnd = 0;     // closing delimiter 后的 exclusive 位置
    int contentStart = 0;
    int contentEnd = 0;
    ProtectedKind kind = ProtectedKind::FencedCode;
};

struct DocumentAnalysis {
    QVector<ProtectedSpan> blockSpans;  // 整块禁止编辑
    QVector<ProtectedSpan> inlineSpans; // 内部禁止，outerStart/outerEnd 是外围空格边界
    QVector<ProtectedSpan> unclosedInlineSpans; // 未闭合行内分隔符段，禁止格式化改写
};

struct BoundaryRange {
    int first = 1; // 字符间插入位置，inclusive
    int last = 0;  // inclusive；first > last 表示空
};

struct WordRange {
    int start = 0; // inclusive
    int end = 0;   // exclusive；start == end 表示空
};

bool isCjk(QChar ch);
bool isAsciiAlnum(QChar ch);
bool isSoftSeparator(QChar ch);
// 统计 [start, end)（end < 0 表示到文本末尾）内汉字表意字符个数：
// 按 Unicode 码点计数，代理对视为一个字符，只统计完整落在区间内的码点；
// 不含日文假名、韩文谚文与中文标点。
int countHanCharacters(const QString &text, int start = 0, int end = -1);
DocumentAnalysis analyzeDocument(const QString &text);
QVector<int> collectSpacingInsertions(
    const QString &text,
    BoundaryRange allowedBoundaries,
    const DocumentAnalysis &analysis,
    bool allowPendingDelimiterSpacing = true);
bool isPositionProtected(const DocumentAnalysis &analysis, int position);
std::pair<QString, QString> resolveSelectionPair(
    const QString &opening, const QString &closing, const QString &selection);
int positionAfterInsertions(int originalPosition,
                            const QVector<int> &insertions,
                            bool includeInsertionAtPosition);
int moveWordBoundary(const QString &text, int position, int direction);
WordRange wordRangeAt(const QString &text, int position);
WordRange wordRangeForCursor(const QString &text, int position);
WordRange wordDeletionRange(const QString &text, int position, bool backwards);
bool spanContainsCjk(const QString &text, int start, int end);

} // namespace CjkText
