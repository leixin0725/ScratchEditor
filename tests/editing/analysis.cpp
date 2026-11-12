#include "suites.h"
#include "support.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QStringList>
#include "cjktextprocessor.h"

namespace EditingTests {

namespace {

struct ExpectedSpan {
    int outerStart = 0;
    int outerEnd = 0;
    int contentStart = 0;
    int contentEnd = 0;
    CjkText::ProtectedKind kind = CjkText::ProtectedKind::FencedCode;
};

bool spansMatch(const QVector<CjkText::ProtectedSpan> &actual,
                const QVector<ExpectedSpan> &expected)
{
    if (actual.size() != expected.size()) {
        return false;
    }
    for (int i = 0; i < actual.size(); ++i) {
        const CjkText::ProtectedSpan &span = actual.at(i);
        const ExpectedSpan &wanted = expected.at(i);
        if (span.outerStart != wanted.outerStart
            || span.outerEnd != wanted.outerEnd
            || span.contentStart != wanted.contentStart
            || span.contentEnd != wanted.contentEnd
            || span.kind != wanted.kind) {
            return false;
        }
    }
    return true;
}

QString spanSummary(const QVector<CjkText::ProtectedSpan> &spans)
{
    QStringList parts;
    for (const CjkText::ProtectedSpan &span : spans) {
        parts << QStringLiteral("[%1,%2) content[%3,%4) kind=%5")
            .arg(span.outerStart).arg(span.outerEnd)
            .arg(span.contentStart).arg(span.contentEnd)
            .arg(static_cast<int>(span.kind));
    }
    return parts.join(QStringLiteral("; "));
}

QString expectedSpanSummary(const QVector<ExpectedSpan> &spans)
{
    QStringList parts;
    for (const ExpectedSpan &span : spans) {
        parts << QStringLiteral("[%1,%2) content[%3,%4) kind=%5")
            .arg(span.outerStart).arg(span.outerEnd)
            .arg(span.contentStart).arg(span.contentEnd)
            .arg(static_cast<int>(span.kind));
    }
    return parts.join(QStringLiteral("; "));
}

void parseExpect(QJsonObject &checks, QJsonObject &details, const QString &name,
                 const QString &text, const QVector<ExpectedSpan> &expectedBlockSpans,
                 const QVector<ExpectedSpan> &expectedInlineSpans,
                 const QVector<ExpectedSpan> &expectedUnclosedSpans = {})
{
    const CjkText::DocumentAnalysis analysis = CjkText::analyzeDocument(text);
    const bool passed = spansMatch(analysis.blockSpans, expectedBlockSpans)
        && spansMatch(analysis.inlineSpans, expectedInlineSpans)
        && spansMatch(analysis.unclosedInlineSpans, expectedUnclosedSpans);
    QJsonObject detail{
        {QStringLiteral("input"), text},
        {QStringLiteral("expectedBlockSpans"), expectedSpanSummary(expectedBlockSpans)},
        {QStringLiteral("expectedInlineSpans"), expectedSpanSummary(expectedInlineSpans)},
        {QStringLiteral("expectedUnclosedSpans"), expectedSpanSummary(expectedUnclosedSpans)},
        {QStringLiteral("actualBlockSpans"), spanSummary(analysis.blockSpans)},
        {QStringLiteral("actualInlineSpans"), spanSummary(analysis.inlineSpans)},
        {QStringLiteral("actualUnclosedSpans"), spanSummary(analysis.unclosedInlineSpans)},
    };
    addCheck(checks, details, name, passed, detail);
}

QString applyInsertions(const QString &text, const QVector<int> &insertions)
{
    QString result = text;
    for (auto it = insertions.crbegin(); it != insertions.crend(); ++it) {
        result.insert(*it, QLatin1Char(' '));
    }
    return result;
}

QString buildPerfDocument(int minimumLength)
{
    QString document;
    document.reserve(minimumLength + 256);
    const QString normalLine = QStringLiteral("中文ABC123`code`中文$x+1$结束\n");
    const QString mixedBlock = QStringLiteral(
        "```\n$$\n中文ABC\n```\n"
        "$$\n```\n中文ABC\n$$\n");
    int blockIndex = 0;
    while (document.size() < minimumLength) {
        document += normalLine;
        if (++blockIndex % 5 == 0) {
            document += mixedBlock;
        }
    }
    return document;
}

} // namespace

void checkParser(QJsonObject &checks, QJsonObject &details)
{
    // --- CJK Fix: Pure Parser Tests (PARSE-001..019) ---
    using CjkText::ProtectedKind;
    parseExpect(checks, details, QStringLiteral("parseFencedBlock"),
                QStringLiteral("```js\n中文ABC\n```"),
                {{0, 15, 6, 12, ProtectedKind::FencedCode}}, {});
    parseExpect(checks, details, QStringLiteral("parseInvalidFenceTrailing"),
                QStringLiteral("```js\n中文ABC\n```oops\n后续ABC\n```"),
                {{0, 29, 6, 26, ProtectedKind::FencedCode}}, {});
    parseExpect(checks, details, QStringLiteral("parseLongerOpeningRun"),
                QStringLiteral("````\nabc\n```\nxyz\n````"),
                {{0, 21, 5, 17, ProtectedKind::FencedCode}}, {});
    parseExpect(checks, details, QStringLiteral("parseLongerClosingRun"),
                QStringLiteral("```\nabc\n````"),
                {{0, 12, 4, 8, ProtectedKind::FencedCode}}, {});
    parseExpect(checks, details, QStringLiteral("parseTildeDoesNotClose"),
                QStringLiteral("```\n~~~\n```"),
                {{0, 11, 4, 8, ProtectedKind::FencedCode}}, {});
    parseExpect(checks, details, QStringLiteral("parseFormulaMarkerInsideFence"),
                QStringLiteral("```\n$$\n```\n中文A"),
                {{0, 10, 4, 7, ProtectedKind::FencedCode}}, {});
    parseExpect(checks, details, QStringLiteral("parseFenceMarkerInsideFormula"),
                QStringLiteral("$$\n```\n$$\n中文A"),
                {{0, 9, 3, 7, ProtectedKind::BlockFormula}}, {});
    parseExpect(checks, details, QStringLiteral("parseSingleLineFormula"),
                QStringLiteral("$$x+1$$\n中文A"),
                {{0, 7, 2, 5, ProtectedKind::BlockFormula}}, {});
    parseExpect(checks, details, QStringLiteral("parseSingleLineFormulaSpaces"),
                QStringLiteral("$$ x $$"),
                {{0, 7, 2, 5, ProtectedKind::BlockFormula}}, {});
    parseExpect(checks, details, QStringLiteral("parseMultiLineFormula"),
                QStringLiteral("$$\n中文\n$$"),
                {{0, 8, 3, 6, ProtectedKind::BlockFormula}}, {});
    parseExpect(checks, details, QStringLiteral("parseUnclosedFormula"),
                QStringLiteral("$$\n中文"),
                {{0, 5, 3, 5, ProtectedKind::BlockFormula}}, {});
    parseExpect(checks, details, QStringLiteral("parseUnclosedFormulaLikeLine"),
                QStringLiteral("$$x+1"), {}, {});
    parseExpect(checks, details, QStringLiteral("parseInlineCode"),
                QStringLiteral("`code`"),
                {}, {{0, 6, 1, 5, ProtectedKind::InlineCode}});
    parseExpect(checks, details, QStringLiteral("parseInlineCodeDoubleRun"),
                QStringLiteral("``code``"),
                {}, {{0, 8, 2, 6, ProtectedKind::InlineCode}});
    parseExpect(checks, details, QStringLiteral("parseInlineCodeLongerRunRejected"),
                QStringLiteral("`a``"), {}, {},
                {{0, 4, 1, 4, ProtectedKind::InlineCode}});
    parseExpect(checks, details, QStringLiteral("parseInlineCodeUnclosed"),
                QStringLiteral("`unclosed"), {}, {},
                {{0, 9, 1, 9, ProtectedKind::InlineCode}});
    parseExpect(checks, details, QStringLiteral("parseInlineFormula"),
                QStringLiteral("$x+1$"),
                {}, {{0, 5, 1, 4, ProtectedKind::InlineFormula}});
    parseExpect(checks, details, QStringLiteral("parseEscapedFormulaOpening"),
                QStringLiteral("\\$x$"), {}, {},
                {{3, 4, 4, 4, ProtectedKind::InlineFormula}});
    parseExpect(checks, details, QStringLiteral("parseInlineInsideBlockIgnored"),
                QStringLiteral("```\n`code` $x$\n```"),
                {{0, 18, 4, 15, ProtectedKind::FencedCode}}, {});
    parseExpect(checks, details, QStringLiteral("parseUnclosedFormulaTail"),
                QStringLiteral("末$中文ABC"), {}, {},
                {{1, 7, 2, 7, ProtectedKind::InlineFormula}});
    parseExpect(checks, details, QStringLiteral("parseUnclosedBacktickTail"),
                QStringLiteral("末`中文ABC"), {}, {},
                {{1, 7, 2, 7, ProtectedKind::InlineCode}});
    parseExpect(checks, details, QStringLiteral("parseClosedFormulaNoUnclosed"),
                QStringLiteral("$x$"), {}, {{0, 3, 1, 2, ProtectedKind::InlineFormula}}, {});
    parseExpect(checks, details, QStringLiteral("parseUnclosedAfterClosed"),
                QStringLiteral("$x$末$中文ABC"),
                {}, {{0, 3, 1, 2, ProtectedKind::InlineFormula}},
                {{4, 10, 5, 10, ProtectedKind::InlineFormula}});
}

void checkSpacingPlanner(QJsonObject &checks, QJsonObject &details)
{
    // --- CJK Fix: Pure Spacing Planner Tests (SPACE-001..018) ---
    const auto spaceExpect = [&](const QString &name, const QString &text,
                                 CjkText::BoundaryRange range,
                                 const QVector<int> &expected,
                                 const QString &expectedText,
                                 bool allowPendingDelimiterSpacing = true) {
        const CjkText::DocumentAnalysis analysis = CjkText::analyzeDocument(text);
        const QVector<int> actual = CjkText::collectSpacingInsertions(
            text, range, analysis, allowPendingDelimiterSpacing);
        const QString actualText = applyInsertions(text, actual);
        const auto joinInts = [](const QVector<int> &values) {
            QStringList parts;
            for (int value : values) {
                parts << QString::number(value);
            }
            return parts.join(QLatin1Char(','));
        };
        QJsonObject detail{
            {QStringLiteral("input"), text},
            {QStringLiteral("range"), QStringLiteral("[%1,%2]").arg(range.first).arg(range.last)},
            {QStringLiteral("expectedInsertions"), joinInts(expected)},
            {QStringLiteral("actualInsertions"), joinInts(actual)},
            {QStringLiteral("expectedText"), expectedText},
            {QStringLiteral("actualText"), actualText},
        };
        addCheck(checks, details, name,
                 actual == expected && actualText == expectedText, detail);
    };

    spaceExpect(QStringLiteral("spaceCjkAscii"), QStringLiteral("中文ABC"),
                {1, 4}, {2}, QStringLiteral("中文 ABC"));
    spaceExpect(QStringLiteral("spaceAsciiCjk"), QStringLiteral("ABC中文"),
                {1, 4}, {3}, QStringLiteral("ABC 中文"));
    spaceExpect(QStringLiteral("spaceCjkNumber"), QStringLiteral("中文123"),
                {1, 4}, {2}, QStringLiteral("中文 123"));
    spaceExpect(QStringLiteral("spaceAlnumOnly"), QStringLiteral("Python3"),
                {1, 6}, {}, QStringLiteral("Python3"));
    spaceExpect(QStringLiteral("spaceExistingSpace"), QStringLiteral("中文 ABC"),
                {1, 5}, {}, QStringLiteral("中文 ABC"));
    spaceExpect(QStringLiteral("spaceSoftSeparator"), QStringLiteral("中文，ABC"),
                {1, 5}, {}, QStringLiteral("中文，ABC"));
    spaceExpect(QStringLiteral("spaceHyphen"), QStringLiteral("中文-ABC"),
                {1, 5}, {}, QStringLiteral("中文-ABC"));
    spaceExpect(QStringLiteral("spaceSlash"), QStringLiteral("ABC/中文"),
                {1, 5}, {}, QStringLiteral("ABC/中文"));
    spaceExpect(QStringLiteral("spaceInlineCodeBoundaries"),
                QStringLiteral("中文`code`中文"), {1, 8}, {2, 8},
                QStringLiteral("中文 `code` 中文"));
    spaceExpect(QStringLiteral("spaceInlineFormulaBoundaries"),
                QStringLiteral("公式$x+1$成立"), {1, 7}, {2, 7},
                QStringLiteral("公式 $x+1$ 成立"));
    spaceExpect(QStringLiteral("spaceInlineCodeContent"),
                QStringLiteral("`中文ABC`"), {1, 7}, {},
                QStringLiteral("`中文ABC`"));
    spaceExpect(QStringLiteral("spaceInlineFormulaContent"),
                QStringLiteral("$中文ABC$"), {1, 6}, {},
                QStringLiteral("$中文ABC$"));
    spaceExpect(QStringLiteral("spaceInlineContentOnly"),
                QStringLiteral("`中文ABC`"), {2, 6}, {},
                QStringLiteral("`中文ABC`"));
    spaceExpect(QStringLiteral("spaceTruncatedDelimiter"),
                QStringLiteral("中文`code`文"), {1, 3}, {2},
                QStringLiteral("中文 `code`文"));
    spaceExpect(QStringLiteral("spaceInsideFence"),
                QStringLiteral("```\n中文ABC\n```"), {1, 13}, {},
                QStringLiteral("```\n中文ABC\n```"));
    spaceExpect(QStringLiteral("spaceAfterFence"),
                QStringLiteral("```\n```\n中文ABC"), {1, 13}, {10},
                QStringLiteral("```\n```\n中文 ABC"));
    spaceExpect(QStringLiteral("spaceAfterSingleLineFormula"),
                QStringLiteral("$$x$$\n中文ABC"), {1, 10}, {8},
                QStringLiteral("$$x$$\n中文 ABC"));
    spaceExpect(QStringLiteral("spaceEmptyAndSingleCharRange"),
                QStringLiteral("中文ABC"), {2, 1}, {},
                QStringLiteral("中文ABC"));
    spaceExpect(QStringLiteral("spacePendingBacktickStart"),
                QStringLiteral("中文``"), {1, 3}, {2},
                QStringLiteral("中文 ``"));
    spaceExpect(QStringLiteral("spacePendingDollarStart"),
                QStringLiteral("中文$"), {1, 2}, {2},
                QStringLiteral("中文 $"));
    spaceExpect(QStringLiteral("spaceEscapedDollarStart"),
                QStringLiteral("中文\\$"), {1, 3}, {},
                QStringLiteral("中文\\$"));
    spaceExpect(QStringLiteral("spaceUnclosedFormulaAltF"),
                QStringLiteral("末$中文ABC"), {1, 6}, {},
                QStringLiteral("末$中文ABC"), /*allowPendingDelimiterSpacing=*/false);
    {
        const QString unclosedText = QStringLiteral("末$中文ABC");
        const CjkText::DocumentAnalysis unclosedAnalysis =
            CjkText::analyzeDocument(unclosedText);
        const QVector<int> realtimeInsertions = CjkText::collectSpacingInsertions(
            unclosedText, {1, 6}, unclosedAnalysis, /*allowPendingDelimiterSpacing=*/true);
        addCheck(checks, details, QStringLiteral("spaceUnclosedFormulaRealtime"),
                 realtimeInsertions == QVector<int>{1},
                 QJsonObject{{QStringLiteral("insertions"),
                              [&realtimeInsertions] {
                                  QStringList values;
                                  for (int value : realtimeInsertions) {
                                      values << QString::number(value);
                                  }
                                  return values.join(QLatin1Char(','));
                              }()}});
    }
}

void checkScaling(QJsonObject &checks, QJsonObject &details)
{
    // --- CJK Fix: Performance Scaling Record ---
    const QString perfSmall = buildPerfDocument(20000);
    const QString perfLarge = buildPerfDocument(100000);
    QElapsedTimer perfTimer;
    perfTimer.start();
    const CjkText::DocumentAnalysis smallAnalysis = CjkText::analyzeDocument(perfSmall);
    const qint64 smallAnalyzeNs = perfTimer.nsecsElapsed();
    perfTimer.restart();
    const QVector<int> smallInsertions = CjkText::collectSpacingInsertions(
        perfSmall, {1, static_cast<int>(perfSmall.size() - 1)}, smallAnalysis);
    const qint64 smallCollectNs = perfTimer.nsecsElapsed();
    perfTimer.restart();
    const CjkText::DocumentAnalysis largeAnalysis = CjkText::analyzeDocument(perfLarge);
    const qint64 largeAnalyzeNs = perfTimer.nsecsElapsed();
    perfTimer.restart();
    const QVector<int> largeInsertions = CjkText::collectSpacingInsertions(
        perfLarge, {1, static_cast<int>(perfLarge.size() - 1)}, largeAnalysis);
    const qint64 largeCollectNs = perfTimer.nsecsElapsed();
    perfTimer.restart();
    const CjkText::DocumentAnalysis imeAnalysis = CjkText::analyzeDocument(perfLarge);
    const qint64 imeAnalyzeNs = perfTimer.nsecsElapsed();
    perfTimer.restart();
    const int footprintStart = perfLarge.size() / 2;
    const QVector<int> imeInsertions = CjkText::collectSpacingInsertions(
        perfLarge, {footprintStart, footprintStart + 100}, imeAnalysis);
    const qint64 imeCollectNs = perfTimer.nsecsElapsed();
    const bool perfScalingOk = largeAnalyzeNs < 25 * qMax<qint64>(1, smallAnalyzeNs)
        && largeCollectNs < 25 * qMax<qint64>(1, smallCollectNs);
    const bool perfStructureOk = largeAnalysis.blockSpans.size() >= 3
        && largeAnalysis.inlineSpans.size() >= 100;
    addCheck(checks, details, QStringLiteral("cjkPerfScaling"),
             perfScalingOk && perfStructureOk,
             QJsonObject{
                 {QStringLiteral("smallLength"), perfSmall.size()},
                 {QStringLiteral("largeLength"), perfLarge.size()},
                 {QStringLiteral("smallAnalyzeMs"),
                  QString::number(smallAnalyzeNs / 1000000.0, 'f', 3)},
                 {QStringLiteral("smallCollectMs"),
                  QString::number(smallCollectNs / 1000000.0, 'f', 3)},
                 {QStringLiteral("largeAnalyzeMs"),
                  QString::number(largeAnalyzeNs / 1000000.0, 'f', 3)},
                 {QStringLiteral("largeCollectMs"),
                  QString::number(largeCollectNs / 1000000.0, 'f', 3)},
                 {QStringLiteral("imeAnalyzeMs"),
                  QString::number(imeAnalyzeNs / 1000000.0, 'f', 3)},
                 {QStringLiteral("imeCollectMs"),
                  QString::number(imeCollectNs / 1000000.0, 'f', 3)},
                 {QStringLiteral("blockSpans"), largeAnalysis.blockSpans.size()},
                 {QStringLiteral("inlineSpans"), largeAnalysis.inlineSpans.size()},
                 {QStringLiteral("smallInsertions"), smallInsertions.size()},
                 {QStringLiteral("largeInsertions"), largeInsertions.size()},
                 {QStringLiteral("imeInsertions"), imeInsertions.size()},
             });
}

} // namespace EditingTests
