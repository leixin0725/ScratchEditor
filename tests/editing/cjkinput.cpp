#include "suites.h"
#include "support.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QStringList>
#include <QThread>
#include <QUrl>

namespace EditingTests {

void checkPunctuationAndFormatting(QJsonObject &checks, QJsonObject &details)
{
    // --- CJK Punctuation Auto Conversion ---
    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    keyPress(QStringLiteral(","), QStringLiteral(","));
    const bool commaConverted = editorText() == QStringLiteral("中文，");

    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    keyPress(QStringLiteral("."), QStringLiteral("."));
    const bool periodConverted = editorText() == QStringLiteral("中文。");

    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    keyPress(QStringLiteral(":"), QStringLiteral(":"));
    const bool colonConverted = editorText() == QStringLiteral("中文：");

    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    keyPress(QStringLiteral("?"), QStringLiteral("?"));
    const bool questionConverted = editorText() == QStringLiteral("中文？");

    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    keyPress(QStringLiteral("!"), QStringLiteral("!"));
    const bool exclaimConverted = editorText() == QStringLiteral("中文！");

    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    keyPress(QStringLiteral(";"), QStringLiteral(";"));
    const bool semiConverted = editorText() == QStringLiteral("中文；");

    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    keyPress(QStringLiteral("("), QStringLiteral("("));
    const bool parenPairConverted = editorText() == QStringLiteral("中文（）");

    setTextAndSelection(QStringLiteral("中文.."), 4, 4);
    keyPress(QStringLiteral("."), QStringLiteral("."));
    const bool ellipsisConverted = editorText() == QStringLiteral("中文……");

    setTextAndSelection(QStringLiteral("中文-"), 3, 3);
    keyPress(QStringLiteral("-"), QStringLiteral("-"));
    const bool emdashConverted = editorText() == QStringLiteral("中文——");

    setTextAndSelection(QStringLiteral("中文，"), 3, 3);
    keyPress(QStringLiteral(","), QStringLiteral(","));
    const bool chainedCommaConverted = editorText() == QStringLiteral("中文，，");

    addCheck(checks, details, QStringLiteral("cjkPunctuationAutoConversion"),
             commaConverted && periodConverted && colonConverted && questionConverted
                 && exclaimConverted && semiConverted && parenPairConverted
                 && ellipsisConverted && emdashConverted && chainedCommaConverted,
             {});

    // --- CJK Selection Wrapping ---
    setTextAndSelection(QStringLiteral("中文"), 0, 2);
    keyPress(QStringLiteral("("), QStringLiteral("("));
    const bool cjkParenWrap = editorText() == QStringLiteral("（中文）");

    setTextAndSelection(QStringLiteral("中文"), 0, 2);
    keyPress(QStringLiteral("["), QStringLiteral("["));
    const bool cjkBracketWrap = editorText() == QStringLiteral("【中文】");

    setTextAndSelection(QStringLiteral("ABC"), 0, 3);
    keyPress(QStringLiteral("("), QStringLiteral("("));
    const bool asciiParenWrap = editorText() == QStringLiteral("(ABC)");

    addCheck(checks, details, QStringLiteral("cjkSelectionWrapping"),
             cjkParenWrap && cjkBracketWrap && asciiParenWrap, {});

    // --- CJK Pair Backspace ---
    setTextAndSelection(QStringLiteral("（）"), 1, 1);
    keyPress({}, QStringLiteral("Backspace"));
    const bool fullParenDeleted = editorText().isEmpty();

    setTextAndSelection(QStringLiteral("……"), 2, 2);
    keyPress({}, QStringLiteral("Backspace"));
    const bool ellipsisDeleted = editorText().isEmpty();
    setTextAndSelection(QStringLiteral("——"), 2, 2);
    keyPress({}, QStringLiteral("Backspace"));
    const bool emdashDeleted = editorText().isEmpty();

    addCheck(checks, details, QStringLiteral("cjkPairBackspace"),
             fullParenDeleted && ellipsisDeleted && emdashDeleted, {});

    // --- 全配对退格参数化（验收用例三 F） ---
    {
        const QVector<std::pair<QString, QString>> allPairs{
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
        QStringList failures;
        for (const auto &pair : allPairs) {
            const QString pairText = pair.first + pair.second;
            setTextAndSelection(pairText, pair.first.size(), pair.first.size());
            keyPress({}, QStringLiteral("Backspace"));
            const QString afterBackspace = editorText();
            request(QStringLiteral("testUndo"));
            const QString afterUndo = editorText();
            if (!afterBackspace.isEmpty() || afterUndo != pairText) {
                failures << QStringLiteral("%1(backspace=%2,undo=%3)")
                    .arg(pairText, afterBackspace, afterUndo);
            }
        }
        addCheck(checks, details, QStringLiteral("backspaceAllDelimiterPairs"),
                 failures.isEmpty(),
                 QJsonObject{{QStringLiteral("failures"), failures.join(QLatin1Char(';'))},
                             {QStringLiteral("pairCount"), allPairs.size()}});
    }

    // --- CJK Auto Spacing & Cursor Following ---
    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    keyPress(QStringLiteral("A"), QStringLiteral("A"));
    // Allow small event loop delay for async auto-space timer
    QThread::msleep(50);
    QCoreApplication::processEvents();
    const bool cjkAsciiSpaced = editorText() == QStringLiteral("中文 A");

    setTextAndSelection(QStringLiteral("中文"), 2, 2);
    inputMethodCommit(QStringLiteral("测试"));
    QThread::msleep(50);
    const QJsonObject imeStatus = request(QStringLiteral("status"));
    const bool cjkImeCursorPassed = (editorText() == QStringLiteral("中文测试"))
                                 && (imeStatus.value(QStringLiteral("cursorPosition")).toInt() == 4);

    setTextAndSelection(QStringLiteral("第一行"), 3, 3);
    inputMethodCommit(QStringLiteral("ABC"));
    QThread::msleep(50);
    QCoreApplication::processEvents();
    const bool cjkImeMultiCharSpaced = (editorText() == QStringLiteral("第一行 ABC"));

    addCheck(checks, details, QStringLiteral("cjkAutoSpacing"),
             cjkAsciiSpaced && cjkImeCursorPassed && cjkImeMultiCharSpaced,
             QJsonObject{{QStringLiteral("cjkAsciiSpaced"), cjkAsciiSpaced},
                         {QStringLiteral("cjkImeCursorPassed"), cjkImeCursorPassed},
                         {QStringLiteral("cjkImeMultiCharSpaced"), cjkImeMultiCharSpaced},
                         {QStringLiteral("text"), editorText()},
                         {QStringLiteral("imeStatus"), imeStatus}});

    // --- Format Spacing Command (Alt+F) ---
    setTextAndSelection(QStringLiteral("中文ABC"), 0, 0);
    execute(QStringLiteral("formatSpacing"));
    const bool fmtCjkAscii = editorText() == QStringLiteral("中文 ABC");

    setTextAndSelection(QStringLiteral("ABC中文"), 0, 0);
    execute(QStringLiteral("formatSpacing"));
    const bool fmtAsciiCjk = editorText() == QStringLiteral("ABC 中文");

    setTextAndSelection(QStringLiteral("中文123"), 0, 0);
    execute(QStringLiteral("formatSpacing"));
    const bool fmtCjkNum = editorText() == QStringLiteral("中文 123");

    setTextAndSelection(QStringLiteral("Python3"), 0, 0);
    execute(QStringLiteral("formatSpacing"));
    const bool fmtAlnumNoSpace = editorText() == QStringLiteral("Python3");

    setTextAndSelection(QStringLiteral("中文，ABC"), 0, 0);
    execute(QStringLiteral("formatSpacing"));
    const bool fmtSoftSepNoSpace = editorText() == QStringLiteral("中文，ABC");

    setTextAndSelection(QStringLiteral("中文`code`中文"), 0, 0);
    execute(QStringLiteral("formatSpacing"));
    const bool fmtInlineCodeBoundaries = editorText() == QStringLiteral("中文 `code` 中文");

    setTextAndSelection(QStringLiteral("```\n中文ABC\n```"), 0, 0);
    execute(QStringLiteral("formatSpacing"));
    const bool fmtFencedCodeIgnored = editorText() == QStringLiteral("```\n中文ABC\n```");

    setTextAndSelection(QStringLiteral("中文ABC\nDEF"), 0, 5);
    execute(QStringLiteral("formatSpacing"));
    const bool fmtSelectionOnly = editorText() == QStringLiteral("中文 ABC\nDEF");

    setTextAndSelection(QStringLiteral("第一行ABC中文"), 3, 8);
    execute(QStringLiteral("formatSpacing"));
    const bool fmtPartialLineSelection = editorText() == QStringLiteral("第一行ABC 中文");

    // Full User Benchmark Document Test
    const QString userDocInput = QStringLiteral(
"# 基础边界\n\n"
"中文ABC\n"
"ABC中文\n"
"中文123\n"
"123中文\n"
"使用Python3编程\n"
"Python3中文\n"
"版本GPT4已经发布\n"
"学习HTML5和CSS3\n"
"中文 ABC\n"
"ABC 中文\n\n"
"# 标点与符号\n\n"
"中文，ABC\n"
"中文。ABC\n"
"中文-ABC\n"
"ABC/中文\n"
"中文（ABC）\n"
"中文[ABC]\n"
"ABC：中文\n\n"
"# 行内代码\n\n"
"中文`code`文本\n"
"使用`printf`函数\n"
"中文，`code`\n"
"`code`，中文\n"
"中文(`code`)文本\n"
"测试`中文ABC123`结束\n"
"已有 `code` 空格\n\n"
"# 行内公式\n\n"
"公式$x+1$成立\n"
"中文，$x+1$\n"
"$x+1$，中文\n"
"中文($x+1$)文本\n"
"测试$中文ABC123$结束\n"
"已有 $x+1$ 空格\n\n"
"# 围栏代码块：内部必须保持原样\n\n"
"```javascript\n"
"const title=\"中文ABC123\";\n"
"const value=\"ABC中文\";\n"
"console.log(\"测试code123\");\n"
"```\n\n"
"~~~text\n"
"中文ABC\n"
"ABC中文\n"
"中文123\n"
"~~~\n\n"
"# 块级公式：内部必须保持原样\n\n"
"$$\n"
"中文ABC+x1\n"
"ABC中文+123中文\n"
"$$\n\n"
"# 代码块之后继续整理\n\n"
"代码块结束后继续使用Python3编程\n"
"最后测试ABC中文123结束"
    );

    const QString userDocExpected = QStringLiteral(
"# 基础边界\n\n"
"中文 ABC\n"
"ABC 中文\n"
"中文 123\n"
"123 中文\n"
"使用 Python3 编程\n"
"Python3 中文\n"
"版本 GPT4 已经发布\n"
"学习 HTML5 和 CSS3\n"
"中文 ABC\n"
"ABC 中文\n\n"
"# 标点与符号\n\n"
"中文，ABC\n"
"中文。ABC\n"
"中文-ABC\n"
"ABC/中文\n"
"中文（ABC）\n"
"中文[ABC]\n"
"ABC：中文\n\n"
"# 行内代码\n\n"
"中文 `code` 文本\n"
"使用 `printf` 函数\n"
"中文，`code`\n"
"`code`，中文\n"
"中文(`code`)文本\n"
"测试 `中文ABC123` 结束\n"
"已有 `code` 空格\n\n"
"# 行内公式\n\n"
"公式 $x+1$ 成立\n"
"中文，$x+1$\n"
"$x+1$，中文\n"
"中文($x+1$)文本\n"
"测试 $中文ABC123$ 结束\n"
"已有 $x+1$ 空格\n\n"
"# 围栏代码块：内部必须保持原样\n\n"
"```javascript\n"
"const title=\"中文ABC123\";\n"
"const value=\"ABC中文\";\n"
"console.log(\"测试code123\");\n"
"```\n\n"
"~~~text\n"
"中文ABC\n"
"ABC中文\n"
"中文123\n"
"~~~\n\n"
"# 块级公式：内部必须保持原样\n\n"
"$$\n"
"中文ABC+x1\n"
"ABC中文+123中文\n"
"$$\n\n"
"# 代码块之后继续整理\n\n"
"代码块结束后继续使用 Python3 编程\n"
"最后测试 ABC 中文 123 结束"
    );

    setTextAndSelection(userDocInput, 0, userDocInput.size());
    execute(QStringLiteral("formatSpacing"));
    const bool fmtUserBenchmarkPassed = (editorText() == userDocExpected);

    addCheck(checks, details, QStringLiteral("formatSpacingCommand"),
             fmtCjkAscii && fmtAsciiCjk && fmtCjkNum && fmtAlnumNoSpace
                 && fmtSoftSepNoSpace && fmtInlineCodeBoundaries
                 && fmtFencedCodeIgnored && fmtSelectionOnly
                 && fmtUserBenchmarkPassed,
             QJsonObject{{QStringLiteral("benchmarkPassed"), fmtUserBenchmarkPassed},
                         {QStringLiteral("actualOutput"), editorText()}});
}

void checkInputTransactions(QJsonObject &checks, QJsonObject &details, const QString &plainFilePath)
{
    const QString plainUrl = QUrl::fromLocalFile(plainFilePath).toString(QUrl::FullyEncoded);
    const QString plainPathText = droppedPathText(plainFilePath);
    // --- CJK Fix: Individual KeyPress Punctuation Checks (KEY-001..013) ---
    cjkExpect(checks, details, QStringLiteral("cjkKeyComma"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key ,"),
              keyAction(QStringLiteral(",")), QStringLiteral("中文，"), 3);
    cjkExpect(checks, details, QStringLiteral("cjkKeyPeriod"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key ."),
              keyAction(QStringLiteral(".")), QStringLiteral("中文。"), 3);
    cjkExpect(checks, details, QStringLiteral("cjkKeyColon"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key :"),
              keyAction(QStringLiteral(":")), QStringLiteral("中文："), 3);
    cjkExpect(checks, details, QStringLiteral("cjkKeyQuestion"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key ?"),
              keyAction(QStringLiteral("?")), QStringLiteral("中文？"), 3);
    cjkExpect(checks, details, QStringLiteral("cjkKeyExclaim"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key !"),
              keyAction(QStringLiteral("!")), QStringLiteral("中文！"), 3);
    cjkExpect(checks, details, QStringLiteral("cjkKeySemi"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key ;"),
              keyAction(QStringLiteral(";")), QStringLiteral("中文；"), 3);
    cjkExpect(checks, details, QStringLiteral("cjkKeyParenPair"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key ("),
              keyAction(QStringLiteral("(")), QStringLiteral("中文（）"), 3);
    cjkExpect(checks, details, QStringLiteral("cjkKeyBracketPair"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key [ after CJK"),
              keyAction(QStringLiteral("[")), QStringLiteral("中文【】"), 3);
    cjkExpect(checks, details, QStringLiteral("cjkKeyDoubleQuotePair"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key \" after CJK"),
              keyAction(QStringLiteral("\"")), QStringLiteral("中文 “”"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeySingleQuotePair"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key ' after CJK"),
              keyAction(QStringLiteral("'")), QStringLiteral("中文 ‘’"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyChainComma"),
              QStringLiteral("中文，"), 3, 3, QStringLiteral("key ,"),
              keyAction(QStringLiteral(",")), QStringLiteral("中文，，"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyChainPeriod"),
              QStringLiteral("中文。"), 3, 3, QStringLiteral("key ."),
              keyAction(QStringLiteral(".")), QStringLiteral("中文。。"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyChainColon"),
              QStringLiteral("中文："), 3, 3, QStringLiteral("key :"),
              keyAction(QStringLiteral(":")), QStringLiteral("中文：："), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyChainSemicolon"),
              QStringLiteral("中文；"), 3, 3, QStringLiteral("key ;"),
              keyAction(QStringLiteral(";")), QStringLiteral("中文；；"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyChainQuestion"),
              QStringLiteral("中文？"), 3, 3, QStringLiteral("key ?"),
              keyAction(QStringLiteral("?")), QStringLiteral("中文？？"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyChainExclaim"),
              QStringLiteral("中文！"), 3, 3, QStringLiteral("key !"),
              keyAction(QStringLiteral("!")), QStringLiteral("中文！！"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyChainAsciiPrevNegative"),
              QStringLiteral("a:"), 2, 2, QStringLiteral("key : after ASCII"),
              keyAction(QStringLiteral(":")), QStringLiteral("a::"), 3);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossColonAfterQuestion"),
              QStringLiteral("中文？"), 3, 3, QStringLiteral("key : after ？"),
              keyAction(QStringLiteral(":")), QStringLiteral("中文？："), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossCommaAfterColon"),
              QStringLiteral("中文："), 3, 3, QStringLiteral("key , after ："),
              keyAction(QStringLiteral(",")), QStringLiteral("中文：，"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossQuestionAfterPeriod"),
              QStringLiteral("中文。"), 3, 3, QStringLiteral("key ? after 。"),
              keyAction(QStringLiteral("?")), QStringLiteral("中文。？"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossExclaimAfterSemi"),
              QStringLiteral("中文；"), 3, 3, QStringLiteral("key ! after ；"),
              keyAction(QStringLiteral("!")), QStringLiteral("中文；！"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossPeriodAfterExclaim"),
              QStringLiteral("中文！"), 3, 3, QStringLiteral("key . after ！"),
              keyAction(QStringLiteral(".")), QStringLiteral("中文！。"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossCloseParen"),
              QStringLiteral("中文？"), 3, 3, QStringLiteral("key ) after ？"),
              keyAction(QStringLiteral(")")), QStringLiteral("中文？）"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossCloseParenSkip"),
              QStringLiteral("中文？）"), 3, 3, QStringLiteral("key ) skips existing ）"),
              keyAction(QStringLiteral(")")), QStringLiteral("中文？）"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossOpenParenPair"),
              QStringLiteral("中文？"), 3, 3, QStringLiteral("key ( after ？"),
              keyAction(QStringLiteral("(")), QStringLiteral("中文？（）"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossOpenBracketPair"),
              QStringLiteral("中文？"), 3, 3, QStringLiteral("key [ after ？"),
              keyAction(QStringLiteral("[")), QStringLiteral("中文？【】"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossColonAfterCloseParen"),
              QStringLiteral("中文）"), 3, 3, QStringLiteral("key : after ）"),
              keyAction(QStringLiteral(":")), QStringLiteral("中文）："), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCrossCommaAfterCloseParen"),
              QStringLiteral("中文）"), 3, 3, QStringLiteral("key , after ）"),
              keyAction(QStringLiteral(",")), QStringLiteral("中文），"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyChainCloseParen"),
              QStringLiteral("中文）"), 3, 3, QStringLiteral("key ) after ）"),
              keyAction(QStringLiteral(")")), QStringLiteral("中文））"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyCloseParenSkipAfterCloseParen"),
              QStringLiteral("中文））"), 3, 3, QStringLiteral("key ) skips existing ）"),
              keyAction(QStringLiteral(")")), QStringLiteral("中文））"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyTwoSpacesComma"),
              QStringLiteral("abc,"), 4, 4, QStringLiteral("key space twice after ,"),
              [] {
                  keyPress(QStringLiteral(" "), QStringLiteral(" "));
                  return keyPress(QStringLiteral(" "), QStringLiteral(" "));
              },
              QStringLiteral("abc，"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyTwoSpacesColon"),
              QStringLiteral("abc:"), 4, 4, QStringLiteral("key space twice after :"),
              [] {
                  keyPress(QStringLiteral(" "), QStringLiteral(" "));
                  return keyPress(QStringLiteral(" "), QStringLiteral(" "));
              },
              QStringLiteral("abc："), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyTwoSpacesSemicolon"),
              QStringLiteral("abc;"), 4, 4, QStringLiteral("key space twice after ;"),
              [] {
                  keyPress(QStringLiteral(" "), QStringLiteral(" "));
                  return keyPress(QStringLiteral(" "), QStringLiteral(" "));
              },
              QStringLiteral("abc；"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyTwoSpacesQuestion"),
              QStringLiteral("abc?"), 4, 4, QStringLiteral("key space twice after ?"),
              [] {
                  keyPress(QStringLiteral(" "), QStringLiteral(" "));
                  return keyPress(QStringLiteral(" "), QStringLiteral(" "));
              },
              QStringLiteral("abc？"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyTwoSpacesExclaim"),
              QStringLiteral("abc!"), 4, 4, QStringLiteral("key space twice after !"),
              [] {
                  keyPress(QStringLiteral(" "), QStringLiteral(" "));
                  return keyPress(QStringLiteral(" "), QStringLiteral(" "));
              },
              QStringLiteral("abc！"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyTwoSpacesPeriod"),
              QStringLiteral("abc."), 4, 4, QStringLiteral("key space twice after ."),
              [] {
                  keyPress(QStringLiteral(" "), QStringLiteral(" "));
                  return keyPress(QStringLiteral(" "), QStringLiteral(" "));
              },
              QStringLiteral("abc。"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyTwoSpacesDotSequence"),
              QStringLiteral("abc.."), 5, 5, QStringLiteral("key space twice after .."),
              [] {
                  keyPress(QStringLiteral(" "), QStringLiteral(" "));
                  return keyPress(QStringLiteral(" "), QStringLiteral(" "));
              },
              QStringLiteral("abc.。"), 5);
    cjkExpect(checks, details, QStringLiteral("cjkKeyTwoSpacesSingleNegative"),
              QStringLiteral("abc,"), 4, 4, QStringLiteral("key space once after ,"),
              keyAction(QStringLiteral(" ")), QStringLiteral("abc, "), 5);
    cjkExpect(checks, details, QStringLiteral("protectKeyTwoSpacesComma"),
              QStringLiteral("`abc,`"), 5, 5, QStringLiteral("key space twice inside inline code"),
              [] {
                  keyPress(QStringLiteral(" "), QStringLiteral(" "));
                  return keyPress(QStringLiteral(" "), QStringLiteral(" "));
              },
              QStringLiteral("`abc,  `"), 7);
    cjkExpect(checks, details, QStringLiteral("cjkKeyEllipsisAscii"),
              QStringLiteral("中文.."), 4, 4, QStringLiteral("key ."),
              keyAction(QStringLiteral(".")), QStringLiteral("中文……"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyEllipsisFull"),
              QStringLiteral("中文。。"), 4, 4, QStringLiteral("key ."),
              keyAction(QStringLiteral(".")), QStringLiteral("中文……"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkKeyEllipsisFullFull"),
              QStringLiteral("中文。。"), 4, 4, QStringLiteral("key 。"),
              keyAction(QStringLiteral("。")), QStringLiteral("中文……"), 4);
    cjkExpect(checks, details, QStringLiteral("cjkImeEllipsisFullFull"),
              QStringLiteral("中文。。"), 4, 4, QStringLiteral("IME commit 。"),
              [] { return inputMethodCommit(QStringLiteral("。")); },
              QStringLiteral("中文……"), 4);
    const QJsonObject imeEllipsisUndone = request(QStringLiteral("testUndo"));
    const QJsonObject imeEllipsisRedone = request(QStringLiteral("testRedo"));
    addCheck(checks, details, QStringLiteral("cjkImeEllipsisUndoRedo"),
             imeEllipsisUndone.value(QStringLiteral("text")).toString()
                    == QStringLiteral("中文。。")
                 && imeEllipsisUndone.value(QStringLiteral("cursorPosition")).toInt() == 4
                 && imeEllipsisRedone.value(QStringLiteral("text")).toString()
                    == QStringLiteral("中文……")
                 && imeEllipsisRedone.value(QStringLiteral("cursorPosition")).toInt() == 4,
             QJsonObject{{QStringLiteral("undo"), imeEllipsisUndone},
                         {QStringLiteral("redo"), imeEllipsisRedone}});
    cjkExpect(checks, details, QStringLiteral("protectImeEllipsisFullFull"),
              QStringLiteral("```\n。。\n```"), 6, 6,
              QStringLiteral("IME commit 。 in fenced code"),
              [] { return inputMethodCommit(QStringLiteral("。")); },
              QStringLiteral("```\n。。。\n```"), 7);
    cjkExpect(checks, details, QStringLiteral("cjkKeyEllipsisMixedDotFullNegative"),
              QStringLiteral("中文.。"), 4, 4, QStringLiteral("key ."),
              keyAction(QStringLiteral(".")), QStringLiteral("中文.。."), 5);
    cjkExpect(checks, details, QStringLiteral("cjkKeyEllipsisMixedFullDotNegative"),
              QStringLiteral("中文.."), 4, 4, QStringLiteral("key 。"),
              keyAction(QStringLiteral("。")), QStringLiteral("中文..。"), 5);
    cjkExpect(checks, details, QStringLiteral("cjkKeyEllipsisFullDotNegative"),
              QStringLiteral("中文。."), 4, 4, QStringLiteral("key 。"),
              keyAction(QStringLiteral("。")), QStringLiteral("中文。.。"), 5);
    cjkExpect(checks, details, QStringLiteral("cjkKeyEllipsisHalfDotTailNegative"),
              QStringLiteral("中文。."), 4, 4, QStringLiteral("key ."),
              keyAction(QStringLiteral(".")), QStringLiteral("中文。.."), 5);
    cjkExpect(checks, details, QStringLiteral("cjkKeyEmdash"),
              QStringLiteral("中文-"), 3, 3, QStringLiteral("key -"),
              keyAction(QStringLiteral("-")), QStringLiteral("中文——"), 4);

    // 连续发送 Tab，不在两阶段之间重置文本或光标。
    const auto tabStateExpect = [&](const QString &name, QString expected) {
        const int expectedCursor = expected.indexOf(QLatin1Char('|'));
        expected.remove(expectedCursor, 1);
        const QJsonObject actual = keyPress({}, QStringLiteral("Tab"));
        addCheck(checks, details, name,
                 actual.value(QStringLiteral("text")).toString() == expected
                     && actual.value(QStringLiteral("cursorPosition")).toInt() == expectedCursor
                     && actual.value(QStringLiteral("selectionStart")).toInt() == expectedCursor
                     && actual.value(QStringLiteral("selectionEnd")).toInt() == expectedCursor,
                 QJsonObject{{QStringLiteral("actual"), actual},
                             {QStringLiteral("expectedText"), expected},
                             {QStringLiteral("expectedCursor"), expectedCursor}});
    };
    const auto tabStagesExpect = [&](const QString &name, QString input,
                                     const QStringList &stages) {
        const int position = input.indexOf(QLatin1Char('|'));
        input.remove(position, 1);
        setTextAndSelection(input, position, position);
        for (int i = 0; i < stages.size(); ++i) {
            tabStateExpect(name + QString::number(i + 1), stages.at(i));
        }
    };
    const QList<QPair<QString, QString>> tabPairs{
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
        {QStringLiteral("\""), QStringLiteral("\"")},
        {QStringLiteral("'"), QStringLiteral("'")},
        {QStringLiteral("＂"), QStringLiteral("＂")},
        {QStringLiteral("＇"), QStringLiteral("＇")},
        {QStringLiteral("`"), QStringLiteral("`")},
        {QStringLiteral("*"), QStringLiteral("*")},
        {QStringLiteral("**"), QStringLiteral("**")},
        {QStringLiteral("***"), QStringLiteral("***")},
        {QStringLiteral("_"), QStringLiteral("_")},
        {QStringLiteral("__"), QStringLiteral("__")},
        {QStringLiteral("___"), QStringLiteral("___")},
    };
    for (int i = 0; i < tabPairs.size(); ++i) {
        const auto &[opening, closing] = tabPairs.at(i);
        tabStagesExpect(QStringLiteral("tabPair%1Stage").arg(i), opening + "a|bc" + closing,
                        {opening + "abc|" + closing, opening + "abc" + closing + "|"});
    }
    tabStagesExpect(QStringLiteral("tabEmptyParen"), QStringLiteral("(|)"),
                    {QStringLiteral("()|")});
    tabStagesExpect(QStringLiteral("tabAdjacentParen"), QStringLiteral("(abc|)"),
                    {QStringLiteral("(abc)|")});
    tabStagesExpect(QStringLiteral("tabNested"), QStringLiteral("(a[b|c]d)"),
                    {QStringLiteral("(a[bc|]d)"), QStringLiteral("(a[bc]|d)"),
                     QStringLiteral("(a[bc]d|)"), QStringLiteral("(a[bc]d)|")});
    tabStagesExpect(QStringLiteral("tabDeepNested"), QStringLiteral("{a(b[c|d]e)f}"),
                    {QStringLiteral("{a(b[cd|]e)f}"), QStringLiteral("{a(b[cd]|e)f}")});
    tabStagesExpect(QStringLiteral("tabQuoteNested"), QStringLiteral("\"a(b|c)d\""),
                    {QStringLiteral("\"a(bc|)d\""), QStringLiteral("\"a(bc)|d\"")});
    tabStagesExpect(QStringLiteral("tabMixedMarkerPriority"), QStringLiteral("*(a|b*c)"),
                    {QStringLiteral("*(ab|*c)"), QStringLiteral("*(ab*|c)")});
    tabStagesExpect(QStringLiteral("tabUtf16"), QStringLiteral("(a|😀b)"),
                    {QStringLiteral("(a😀b|)"), QStringLiteral("(a😀b)|")});
    tabStagesExpect(QStringLiteral("tabEscapedInnerCloser"), QStringLiteral(R"((a|\)b))"),
                    {QStringLiteral(R"((a\)b|))"), QStringLiteral(R"((a\)b)|)")});
    tabStagesExpect(QStringLiteral("tabEscapedQuoteStages"), QStringLiteral(R"("a|\"b")"),
                    {QStringLiteral(R"("a\"b|")"), QStringLiteral(R"("a\"b"|)")});
    tabStagesExpect(QStringLiteral("tabEscapedBoldCloser"), QStringLiteral(R"(**a|\**b**)"),
                    {QStringLiteral(R"(**a\**b|**)"), QStringLiteral(R"(**a\**b**|)")});
    tabStagesExpect(QStringLiteral("tabEscapedBacktickCloser"), QStringLiteral(R"(`a|\`b`)"),
                    {QStringLiteral(R"(`a\`b|`)"), QStringLiteral(R"(`a\`b`|)")});
    tabStagesExpect(QStringLiteral("tabNoPair"), QStringLiteral("abc|"),
                    {QStringLiteral("    abc|")});
    tabStagesExpect(QStringLiteral("tabUnclosedPair"), QStringLiteral("(a|bc"),
                    {QStringLiteral("    (a|bc")});
    tabStagesExpect(QStringLiteral("tabNoCrossLine"), QStringLiteral("(a|\nb)"),
                    {QStringLiteral("    (a|\nb)")});
    tabStagesExpect(QStringLiteral("tabCjkParenStages"), QStringLiteral("(中|文)"),
                    {QStringLiteral("(中文|)"), QStringLiteral("（中文）|")});
    tabStagesExpect(QStringLiteral("tabCjkBracketStages"), QStringLiteral("[中|文]"),
                    {QStringLiteral("[中文|]"), QStringLiteral("【中文】|")});
    tabStagesExpect(QStringLiteral("tabCjkDoubleStages"), QStringLiteral("\"中|文\""),
                    {QStringLiteral("\"中文|\""), QStringLiteral("“中文”|")});
    tabStagesExpect(QStringLiteral("tabCjkSingleStages"), QStringLiteral("'中|文'"),
                    {QStringLiteral("'中文|'"), QStringLiteral("‘中文’|")});
    tabStagesExpect(QStringLiteral("tabCjkNestedStages"), QStringLiteral("(\"中|文\")"),
                    {QStringLiteral("(\"中文|\")"), QStringLiteral("(“中文”|)"),
                     QStringLiteral("（“中文”）|")});
    tabStagesExpect(QStringLiteral("tabCjkNoMapping"), QStringLiteral("{中|文}"),
                    {QStringLiteral("{中文|}"), QStringLiteral("{中文}|")});
    tabStagesExpect(QStringLiteral("tabCjkInlineProtected"), QStringLiteral("`(中|文)`"),
                    {QStringLiteral("`(中文|)`"), QStringLiteral("`(中文)|`")});
    tabStagesExpect(QStringLiteral("tabCjkFenceProtected"), QStringLiteral("```\n(中|文)\n```"),
                    {QStringLiteral("```\n(中文|)\n```"), QStringLiteral("```\n(中文)|\n```")});
    cjkExpect(checks, details, QStringLiteral("tabPairSelectionIndents"),
              QStringLiteral("(ab[cd]ef)"), 4, 6, QStringLiteral("Tab with selection"),
              [] { return keyPress({}, QStringLiteral("Tab")); },
              QStringLiteral("    (ab[cd]ef)"), 14, 0, 14);

    // 纯移动不能占据 Undo 步骤，也不能清除已有 Redo。
    setTextAndSelection(QStringLiteral("(abc)"), 2, 2);
    keyPress(QStringLiteral("x"));
    tabStateExpect(QStringLiteral("tabUndoMoveBefore"), QStringLiteral("(axbc|)"));
    tabStateExpect(QStringLiteral("tabUndoMoveAfter"), QStringLiteral("(axbc)|"));
    request(QStringLiteral("testUndo"));
    addCheck(checks, details, QStringLiteral("tabPureMovesSingleUndo"),
             editorText() == QStringLiteral("(abc)"),
             QJsonObject{{QStringLiteral("actual"), editorText()},
                         {QStringLiteral("expected"), QStringLiteral("(abc)")}});
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 2}, {QStringLiteral("end"), 2}});
    tabStateExpect(QStringLiteral("tabRedoMoveBefore"), QStringLiteral("(abc|)"));
    tabStateExpect(QStringLiteral("tabRedoMoveAfter"), QStringLiteral("(abc)|"));
    request(QStringLiteral("testRedo"));
    addCheck(checks, details, QStringLiteral("tabPureMovesPreserveRedo"),
             editorText() == QStringLiteral("(axbc)"),
             QJsonObject{{QStringLiteral("actual"), editorText()},
                         {QStringLiteral("expected"), QStringLiteral("(axbc)")}});
    tabStagesExpect(QStringLiteral("tabConversionUndoStages"), QStringLiteral("(中|文)"),
                    {QStringLiteral("(中文|)"), QStringLiteral("（中文）|")});
    request(QStringLiteral("testUndo"));
    const QString tabConversionUndone = editorText();
    request(QStringLiteral("testRedo"));
    addCheck(checks, details, QStringLiteral("tabConversionUndoRedo"),
             tabConversionUndone == QStringLiteral("(中文)")
                 && editorText() == QStringLiteral("（中文）"),
             QJsonObject{{QStringLiteral("actualUndo"), tabConversionUndone},
                         {QStringLiteral("expectedUndo"), QStringLiteral("(中文)")},
                         {QStringLiteral("actualRedo"), editorText()},
                         {QStringLiteral("expectedRedo"), QStringLiteral("（中文）")}});

    // Tab 转义只影响目标识别；此处从真实闭符号前出发，独立于跳转阶段。
    const auto tabEscapeExpect = [&](const QString &name, QString marked,
                                     bool matched) {
        const int position = marked.indexOf(QLatin1Char('|'));
        marked.remove(position, 1);
        const QString expected = matched ? marked : QStringLiteral("    ") + marked;
        cjkExpect(checks, details, name, marked, position, position,
                  QStringLiteral("Tab escape recognition"),
                  [] { return keyPress({}, QStringLiteral("Tab")); },
                  expected, matched ? position + 1 : position + 4);
    };
    tabEscapeExpect(QStringLiteral("tabEscapedQuoteCloser"),
                    QStringLiteral(R"("a\"b|")"), true);
    tabEscapeExpect(QStringLiteral("tabEscapedParenOpener"),
                    QStringLiteral(R"(\(ab|))"), false);
    tabEscapeExpect(QStringLiteral("tabEvenBackslashesParen"),
                    QStringLiteral(R"(\\(ab|))"), true);
    tabEscapeExpect(QStringLiteral("tabEscapedQuoteOpener"),
                    QStringLiteral(R"(\"ab|")"), false);
    tabEscapeExpect(QStringLiteral("tabEscapedCurlyQuoteOpener"),
                    QStringLiteral("\\“ab|”"), false);
    tabEscapeExpect(QStringLiteral("tabEscapedEmphasisOpener"),
                    QStringLiteral(R"(\*ab|*)"), false);
    tabEscapeExpect(QStringLiteral("tabEscapedBacktickOpener"),
                    QStringLiteral(R"(\`ab|`)"), false);
    tabEscapeExpect(QStringLiteral("tabEscapedEmptyEmphasis"),
                    QStringLiteral(R"(\*|*)"), false);
    tabEscapeExpect(QStringLiteral("tabEscapedEmptyBackticks"),
                    QStringLiteral(R"(\`|`)"), false);
    tabEscapeExpect(QStringLiteral("tabEscapedEmptyBoldRun"),
                    QStringLiteral(R"(\**|**)"), false);

    // 撤销事务必须记录实际编辑点，不能使用临时 QTextCursor 默认的文档位置 0。
    const QList<QPair<QString, QString>> undoPairs{
        {QStringLiteral("()"), QStringLiteral("（）")},
        {QStringLiteral("[]"), QStringLiteral("【】")},
        {QStringLiteral("\"\""), QStringLiteral("“”")},
        {QStringLiteral("''"), QStringLiteral("‘’")},
    };
    for (int i = 0; i < undoPairs.size(); ++i) {
        const auto &[ascii, fullwidth] = undoPairs.at(i);
        const QString prefix = QStringLiteral("head\n😀x");
        undoCursorExpect(checks, details, QStringLiteral("transactionTabPair%1").arg(i),
                         prefix + ascii.front() + QStringLiteral("中文|") + ascii.back(),
                         [] { keyPress({}, QStringLiteral("Tab")); },
                         prefix + fullwidth.front() + QStringLiteral("中文") + fullwidth.back());
    }
    undoCursorExpect(checks, details, QStringLiteral("transactionTabTwoStages"),
                     QStringLiteral("head\nx\"中|文\""),
                     [] { keyPress({}, QStringLiteral("Tab")); keyPress({}, QStringLiteral("Tab")); },
                     QStringLiteral("head\nx“中文”"), 9);
    undoCursorExpect(checks, details, QStringLiteral("transactionCjkBackspace"),
                     QStringLiteral("head\n中文|测试"),
                     [] { keyPress({}, QStringLiteral("Backspace"), false, QStringLiteral("ctrl")); },
                     QStringLiteral("head\n测试"));
    undoCursorExpect(checks, details, QStringLiteral("transactionCjkDelete"),
                     QStringLiteral("head\n中文|测试"),
                     [] { keyPress({}, QStringLiteral("Delete"), false, QStringLiteral("ctrl")); },
                     QStringLiteral("head\n中文"));
    undoCursorExpect(checks, details, QStringLiteral("transactionUnwrapBold"),
                     QStringLiteral("head\n**a|bc**"),
                     [] { execute(QStringLiteral("toggleBold")); }, QStringLiteral("head\nabc"));
    undoCursorExpect(checks, details, QStringLiteral("transactionReplaceAll"),
                     QStringLiteral("cat\nca|t cat"),
                     [] { request(QStringLiteral("testReplaceAll"),
                                  {{QStringLiteral("query"), QStringLiteral("cat")},
                                   {QStringLiteral("replacement"), QStringLiteral("dog")},
                                   {QStringLiteral("caseSensitive"), true}}); },
                     QStringLiteral("dog\ndog dog"));
    undoCursorExpect(checks, details, QStringLiteral("transactionClear"),
                     QStringLiteral("head\na|bc"),
                     [] { execute(QStringLiteral("clearDocument")); }, QString());
    undoCursorExpect(checks, details, QStringLiteral("transactionCheckboxToggle"),
                     QStringLiteral("head\n- [ ] a|bc"),
                     [] { execute(QStringLiteral("toggleCheckbox")); }, QStringLiteral("head\n- [x] abc"));
    undoCursorExpect(checks, details, QStringLiteral("transactionCheckboxInsert"),
                     QStringLiteral("head\na|bc"),
                     [] { execute(QStringLiteral("toggleCheckbox")); }, QStringLiteral("head\n- [ ] abc"));
    undoCursorExpect(checks, details, QStringLiteral("transactionEmptyListBackspace"),
                     QStringLiteral("head\n- |"),
                     [] { keyPress({}, QStringLiteral("Backspace")); }, QStringLiteral("head"));
    undoCursorExpect(checks, details, QStringLiteral("transactionOrderedConversion"),
                     QStringLiteral("head\n- a|bc"),
                     [] { execute(QStringLiteral("convertToOrderedList")); }, QStringLiteral("head\n1. abc"));
    undoCursorExpect(checks, details, QStringLiteral("transactionPathDrop"),
                     QStringLiteral("head\na|bc"),
                     [&] { dropUrls({plainUrl}); }, QStringLiteral("head\na") + plainPathText + QStringLiteral("bc"));
    undoCursorExpect(checks, details, QStringLiteral("transactionClearAtStart"),
                     QStringLiteral("|head\nabc"),
                     [] { execute(QStringLiteral("clearDocument")); }, QString());
    undoCursorExpect(checks, details, QStringLiteral("transactionReplaceAtStart"),
                     QStringLiteral("|cat\ncat"),
                     [] { request(QStringLiteral("testReplaceAll"),
                                  {{QStringLiteral("query"), QStringLiteral("cat")},
                                   {QStringLiteral("replacement"), QStringLiteral("dog")},
                                   {QStringLiteral("caseSensitive"), true}}); },
                     QStringLiteral("dog\ndog"));
    undoCursorExpect(checks, details, QStringLiteral("transactionUnwrapAtCloser"),
                     QStringLiteral("head\n**abc|**"),
                     [] { execute(QStringLiteral("toggleBold")); }, QStringLiteral("head\nabc"));
    undoCursorExpect(checks, details, QStringLiteral("transactionOrderedAtStart"),
                     QStringLiteral("|- abc"),
                     [] { execute(QStringLiteral("convertToOrderedList")); }, QStringLiteral("1. abc"));
    undoCursorExpect(checks, details, QStringLiteral("transactionCheckboxAtState"),
                     QStringLiteral("head\n- [| ] abc"),
                     [] { execute(QStringLiteral("toggleCheckbox")); }, QStringLiteral("head\n- [x] abc"));
    undoCursorExpect(checks, details, QStringLiteral("transactionPathDropReverseSelection"),
                     QStringLiteral("head\n|abc"),
                     [&] { dropUrls({plainUrl}); }, QStringLiteral("head\n") + plainPathText, -1, 8);

    const auto historyUndoExpect = [&](const QString &name, QString marked) {
        const int expectedCursor = marked.indexOf(QLatin1Char('|'));
        marked.remove(expectedCursor, 1);
        const QJsonObject actual = request(QStringLiteral("testUndo"));
        addCheck(checks, details, name,
                 editorText() == marked
                     && actual.value(QStringLiteral("cursorPosition")).toInt() == expectedCursor,
                 QJsonObject{{QStringLiteral("actualText"), editorText()},
                             {QStringLiteral("expectedText"), marked},
                             {QStringLiteral("actualCursor"), actual.value(QStringLiteral("cursorPosition"))},
                             {QStringLiteral("expectedCursor"), expectedCursor}});
    };
    setTextAndSelection(QStringLiteral("head\n(\"中文\")"), 9, 9);
    keyPress({}, QStringLiteral("Tab"));
    keyPress({}, QStringLiteral("Tab"));
    historyUndoExpect(QStringLiteral("undoHistoryNestedOuter"), QStringLiteral("head\n(“中文”|)"));
    historyUndoExpect(QStringLiteral("undoHistoryNestedInner"), QStringLiteral("head\n(\"中文|\")"));
    request(QStringLiteral("testRedo"));
    request(QStringLiteral("testRedo"));
    historyUndoExpect(QStringLiteral("undoHistoryNestedOuterAfterRedo"), QStringLiteral("head\n(“中文”|)"));
    historyUndoExpect(QStringLiteral("undoHistoryNestedInnerAfterRedo"), QStringLiteral("head\n(\"中文|\")"));

    setTextAndSelection(QStringLiteral("head\nx\"中文\""), 9, 9);
    keyPress({}, QStringLiteral("Tab"));
    keyPress(QStringLiteral("z"));
    historyUndoExpect(QStringLiteral("undoHistoryInterleavedTyping"), QStringLiteral("head\nx“中文”|"));
    historyUndoExpect(QStringLiteral("undoHistoryBeforeTyping"), QStringLiteral("head\nx\"中文|\""));
    request(QStringLiteral("testRedo"));
    request(QStringLiteral("testRedo"));
    historyUndoExpect(QStringLiteral("undoHistoryTypingAfterRedo"), QStringLiteral("head\nx“中文”|"));
    historyUndoExpect(QStringLiteral("undoHistoryConversionAfterRedo"), QStringLiteral("head\nx\"中文|\""));

    setTextAndSelection(QStringLiteral("head\nabc"), 6, 6);
    execute(QStringLiteral("clearDocument"));
    historyUndoExpect(QStringLiteral("undoHistoryOriginalBranch"), QStringLiteral("head\na|bc"));
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 7}, {QStringLiteral("end"), 7}});
    execute(QStringLiteral("clearDocument"));
    historyUndoExpect(QStringLiteral("undoHistoryReplacementBranch"), QStringLiteral("head\nab|c"));
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), 5}, {QStringLiteral("end"), 5}});
    keyPress(QStringLiteral("q"));
    historyUndoExpect(QStringLiteral("undoHistoryUntrackedBranch"), QStringLiteral("head\n|abc"));

    setTextAndSelection(QStringLiteral("head\nxyz"), 6, 6);
    keyPress(QStringLiteral("q"));
    historyUndoExpect(QStringLiteral("undoHistoryDocumentReset"), QStringLiteral("head\nx|yz"));

    // --- Tab 跳出配对的 CJK 全角转换（TAB-CJK-001..008） ---
    const auto tabAction = [] { return keyPress({}, QStringLiteral("Tab")); };
    cjkExpect(checks, details, QStringLiteral("tabCjkParenPair"),
              QStringLiteral("x(中文)"), 4, 4, QStringLiteral("Tab"),
              tabAction, QStringLiteral("x（中文）"), 5);
    cjkExpect(checks, details, QStringLiteral("tabCjkBracketPair"),
              QStringLiteral("x[中文]"), 4, 4, QStringLiteral("Tab"),
              tabAction, QStringLiteral("x【中文】"), 5);
    cjkExpect(checks, details, QStringLiteral("tabCjkDoubleQuotePair"),
              QStringLiteral("x\"中文\""), 4, 4, QStringLiteral("Tab"),
              tabAction, QStringLiteral("x“中文”"), 5);
    cjkExpect(checks, details, QStringLiteral("tabCjkSingleQuotePair"),
              QStringLiteral("x'中文'"), 4, 4, QStringLiteral("Tab"),
              tabAction, QStringLiteral("x‘中文’"), 5);
    cjkExpect(checks, details, QStringLiteral("tabCjkNestedOuterPair"),
              QStringLiteral("x([中文])"), 6, 6, QStringLiteral("Tab"),
              tabAction, QStringLiteral("x（[中文]）"), 7);
    cjkExpect(checks, details, QStringLiteral("tabCjkAsciiContentNegative"),
              QStringLiteral("x(abc)"), 5, 5, QStringLiteral("Tab"),
              tabAction, QStringLiteral("x(abc)"), 6);
    cjkExpect(checks, details, QStringLiteral("tabCjkFullwidthPairUnchanged"),
              QStringLiteral("x（中文）"), 4, 4, QStringLiteral("Tab"),
              tabAction, QStringLiteral("x（中文）"), 5);
    cjkExpect(checks, details, QStringLiteral("tabCjkProtectedInlineCode"),
              QStringLiteral("`x(中文)`"), 5, 5, QStringLiteral("Tab"),
              tabAction, QStringLiteral("`x(中文)`"), 6);
    {
        // 转换与跳转合并为一次撤销（UNDO-GROUP-…）。
        setTextAndSelection(QStringLiteral("x(中文)"), 4, 4);
        keyPress({}, QStringLiteral("Tab"));
        QThread::msleep(30);
        const QString converted = editorText();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString undone = editorText();
        addCheck(checks, details, QStringLiteral("tabCjkConversionSingleUndo"),
                 converted == QStringLiteral("x（中文）")
                     && undone == QStringLiteral("x(中文)"),
                 QJsonObject{{QStringLiteral("converted"), converted},
                             {QStringLiteral("undone"), undone}});
    }

    // --- CJK Fix: Protected Region KeyPress (PROTECT-KEY-001..005) ---
    cjkExpect(checks, details, QStringLiteral("protectKeyInlineCode"),
              QStringLiteral("`中文`"), 2, 2, QStringLiteral("key , inside inline code"),
              keyAction(QStringLiteral(",")), QStringLiteral("`中,文`"), 3);
    cjkExpect(checks, details, QStringLiteral("protectKeyInlineQuote"),
              QStringLiteral("`中文`"), 3, 3, QStringLiteral("key \" inside inline code"),
              keyAction(QStringLiteral("\"")), QStringLiteral("`中文\"`"), 4);
    cjkExpect(checks, details, QStringLiteral("protectKeyChainColon"),
              QStringLiteral("`中文：`"), 4, 4, QStringLiteral("key : inside inline code"),
              keyAction(QStringLiteral(":")), QStringLiteral("`中文：:`"), 5);

    // --- 行中引号：单开符 + 闭合收尾（MID-QUOTE-001..011） ---
    cjkExpect(checks, details, QStringLiteral("midQuoteAsciiSingleOpen"),
              QStringLiteral("abc"), 1, 1, QStringLiteral("key \" at midline"),
              keyAction(QStringLiteral("\"")), QStringLiteral("a\"bc"), 2);
    cjkExpect(checks, details, QStringLiteral("midQuoteSingleQuoteSingleOpen"),
              QStringLiteral("abc"), 1, 1, QStringLiteral("key ' at midline"),
              keyAction(QStringLiteral("'")), QStringLiteral("a'bc"), 2);
    cjkExpect(checks, details, QStringLiteral("midQuoteBacktickSingleOpen"),
              QStringLiteral("abc"), 1, 1, QStringLiteral("key ` at midline"),
              keyAction(QStringLiteral("`")), QStringLiteral("a`bc"), 2);
    cjkExpect(checks, details, QStringLiteral("midQuoteCurlySingleOpen"),
              QStringLiteral("abc"), 1, 1, QStringLiteral("key “ at midline"),
              keyAction(QStringLiteral("“")), QStringLiteral("a“bc"), 2);
    cjkExpect(checks, details, QStringLiteral("midQuoteAsciiCloseNoCjk"),
              QStringLiteral("a\"bc"), 3, 3, QStringLiteral("key \" to close wrap"),
              keyAction(QStringLiteral("\"")), QStringLiteral("a\"b\"c"), 4);
    cjkExpect(checks, details, QStringLiteral("midQuoteAsciiCloseCjkConverts"),
              QStringLiteral("a\"中文x"), 4, 4, QStringLiteral("key \" closes CJK wrap"),
              keyAction(QStringLiteral("\"")), QStringLiteral("a “中文” x"), 6);
    cjkExpect(checks, details, QStringLiteral("midQuoteCloseSkipsExisting"),
              QStringLiteral("a\"b\"c"), 3, 3, QStringLiteral("key \" skips existing closer"),
              keyAction(QStringLiteral("\"")), QStringLiteral("a\"b\"c"), 4);
    cjkExpect(checks, details, QStringLiteral("midQuoteCloseTriggersAutoSpacing"),
              QStringLiteral("中文“Ax"), 4, 4, QStringLiteral("key ” closes wrap"),
              keyAction(QStringLiteral("”")), QStringLiteral("中文 “A” x"), 6);
    cjkExpect(checks, details, QStringLiteral("midQuoteLineEndKeepsAutoPair"),
              QStringLiteral("abc"), 3, 3, QStringLiteral("key \" at line end"),
              keyAction(QStringLiteral("\"")), QStringLiteral("abc\"\""), 4);
    cjkExpect(checks, details, QStringLiteral("lineEndAsciiQuoteClosesUnclosed"),
              QStringLiteral("a\"bc"), 4, 4,
              QStringLiteral("key \" closes unclosed quote at line end"),
              keyAction(QStringLiteral("\"")), QStringLiteral("a\"bc\""), 5);
    cjkExpect(checks, details, QStringLiteral("lineEndCjkQuoteClosesBeforePairing"),
              QStringLiteral("中文“内容"), 5, 5,
              QStringLiteral("key \" closes CJK quote at line end"),
              keyAction(QStringLiteral("\"")), QStringLiteral("中文 “内容”"), 7);
    cjkExpect(checks, details, QStringLiteral("lineEndCurlyOpeningKeyClosesUnclosed"),
              QStringLiteral("a“bc"), 4, 4,
              QStringLiteral("key “ closes unclosed curly quote at line end"),
              keyAction(QStringLiteral("“")), QStringLiteral("a “bc”"), 6);
    cjkExpect(checks, details, QStringLiteral("lineEndFullwidthQuoteClosesUnclosed"),
              QStringLiteral("a＂bc"), 4, 4,
              QStringLiteral("key ＂ closes unclosed fullwidth quote at line end"),
              keyAction(QStringLiteral("＂")), QStringLiteral("a＂bc＂"), 5);
    cjkExpect(checks, details, QStringLiteral("lineEndBacktickClosesUnclosed"),
              QStringLiteral("a`code"), 6, 6,
              QStringLiteral("key ` closes unclosed code span at line end"),
              keyAction(QStringLiteral("`")), QStringLiteral("a `code`"), 7);
    cjkExpect(checks, details, QStringLiteral("lineEndImeQuoteClosesUnclosed"),
              QStringLiteral("a\"中文"), 4, 4,
              QStringLiteral("IME commit \" closes quote at line end"),
              [] { return inputMethodCommit(QStringLiteral("\"")); },
              QStringLiteral("a “中文”"), 6);
    cjkExpect(checks, details, QStringLiteral("lineEndEscapedQuoteDoesNotClose"),
              QStringLiteral("abc\\\"def"), 8, 8,
              QStringLiteral("escaped quote is not an opener"),
              keyAction(QStringLiteral("\"")), QStringLiteral("abc\\\"def\"\""), 9);
    cjkExpect(checks, details, QStringLiteral("lineEndQuoteDoesNotCrossLine"),
              QStringLiteral("\"first\nsecond"), 13, 13,
              QStringLiteral("quote opener on previous line is ignored"),
              keyAction(QStringLiteral("\"")), QStringLiteral("\"first\nsecond\"\""), 14);
    {
        // 输入与自动空格必须合并为一次撤销（UNDO-GROUP-001）。
        setTextAndSelection(QStringLiteral("中文"), 2, 2);
        keyPress(QStringLiteral("\""), QStringLiteral("\""));
        QThread::msleep(30);
        const QString spaced = editorText();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString undone = editorText();
        addCheck(checks, details, QStringLiteral("undoGroupsQuoteAndSpacing"),
                 spaced == QStringLiteral("中文 “”") && undone == QStringLiteral("中文"),
                 QJsonObject{{QStringLiteral("spaced"), spaced},
                             {QStringLiteral("undone"), undone}});
    }
    cjkExpect(checks, details, QStringLiteral("midQuoteImeCurlySingleOpen"),
              QStringLiteral("abc"), 1, 1, QStringLiteral("IME commit “ at midline"),
              [] { return inputMethodCommit(QStringLiteral("“")); },
              QStringLiteral("a“bc"), 2);
    cjkExpect(checks, details, QStringLiteral("midQuoteImeCurlyCloseCjk"),
              QStringLiteral("a“中文x"), 4, 4, QStringLiteral("IME commit ” closes wrap"),
              [] { return inputMethodCommit(QStringLiteral("”")); },
              QStringLiteral("a “中文” x"), 6);
    cjkExpect(checks, details, QStringLiteral("lineEndImeCurlyDoubleQuotePairsAndSpaces"),
              QStringLiteral("中文"), 2, 2,
              QStringLiteral("IME commit “ after CJK at line end"),
              [] { return inputMethodCommit(QStringLiteral("“")); },
              QStringLiteral("中文 “”"), 4);
    cjkExpect(checks, details, QStringLiteral("lineEndImeCurlySingleQuotePairsAndSpaces"),
              QStringLiteral("中文"), 2, 2,
              QStringLiteral("IME commit ‘ after CJK at line end"),
              [] { return inputMethodCommit(QStringLiteral("‘")); },
              QStringLiteral("中文 ‘’"), 4);
    cjkExpect(checks, details, QStringLiteral("lineEndImeCurlyDoubleCloserStartsPair"),
              QStringLiteral("中文"), 2, 2,
              QStringLiteral("IME commit ” without a matching opener at line end"),
              [] { return inputMethodCommit(QStringLiteral("”")); },
              QStringLiteral("中文 “”"), 4);
    cjkExpect(checks, details, QStringLiteral("lineEndImeCurlySingleCloserStartsPair"),
              QStringLiteral("中文"), 2, 2,
              QStringLiteral("IME commit ’ without a matching opener at line end"),
              [] { return inputMethodCommit(QStringLiteral("’")); },
              QStringLiteral("中文 ‘’"), 4);
    cjkExpect(checks, details, QStringLiteral("emptyLineImeCurlyCloserStartsPair"),
              QString(), 0, 0,
              QStringLiteral("IME commit ” on an empty line"),
              [] { return inputMethodCommit(QStringLiteral("”")); },
              QStringLiteral("“”"), 1);
    cjkExpect(checks, details, QStringLiteral("asciiLineEndImeCurlyCloserStartsPair"),
              QStringLiteral("abc"), 3, 3,
              QStringLiteral("IME commit ” after ASCII at line end"),
              [] { return inputMethodCommit(QStringLiteral("”")); },
              QStringLiteral("abc “”"), 5);
    cjkExpect(checks, details, QStringLiteral("trailingWhitespaceImeCurlyCloserStartsPair"),
              QStringLiteral("中文 \t"), 2, 2,
              QStringLiteral("IME commit ” before trailing whitespace"),
              [] { return inputMethodCommit(QStringLiteral("”")); },
              QStringLiteral("中文 “” \t"), 4);
    cjkExpect(checks, details, QStringLiteral("midlineImeCurlyCloserRemainsLiteral"),
              QStringLiteral("中文正文"), 2, 2,
              QStringLiteral("IME commit ” before line content"),
              [] { return inputMethodCommit(QStringLiteral("”")); },
              QStringLiteral("中文”正文"), 3);
    cjkExpect(checks, details, QStringLiteral("lineEndKeyCurlyCloserRemainsLiteral"),
              QStringLiteral("中文"), 2, 2,
              QStringLiteral("key ” without a matching opener at line end"),
              keyAction(QStringLiteral("”")), QStringLiteral("中文”"), 3);
    cjkExpect(checks, details, QStringLiteral("lineEndImeCurlyCloserClosesMatchingOpener"),
              QStringLiteral("中文“内容"), 5, 5,
              QStringLiteral("IME commit ” closes a matching opener"),
              [] { return inputMethodCommit(QStringLiteral("”")); },
              QStringLiteral("中文 “内容”"), 7);
    cjkExpect(checks, details, QStringLiteral("lineEndImeMismatchedCloserStartsNestedPair"),
              QStringLiteral("“abc"), 4, 4,
              QStringLiteral("IME commit ’ after an unmatched double opener"),
              [] { return inputMethodCommit(QStringLiteral("’")); },
              QStringLiteral("“abc ‘’"), 6);
    cjkExpect(checks, details, QStringLiteral("lineEndImeCloserIgnoresEscapedOpener"),
              QStringLiteral("\\“abc"), 5, 5,
              QStringLiteral("IME commit ” after an escaped opener"),
              [] { return inputMethodCommit(QStringLiteral("”")); },
              QStringLiteral("\\“abc“”"), 6);
    cjkExpect(checks, details, QStringLiteral("lineEndImeCloserDoesNotCrossLine"),
              QStringLiteral("“first\n中文"), 9, 9,
              QStringLiteral("IME commit ” with an opener on the previous line"),
              [] { return inputMethodCommit(QStringLiteral("”")); },
              QStringLiteral("“first\n中文 “”"), 11);
    cjkExpect(checks, details, QStringLiteral("selectionImeCurlyCloserRemainsLiteral"),
              QStringLiteral("中文"), 0, 2,
              QStringLiteral("IME commit ” over a selection"),
              [] { return inputMethodCommit(QStringLiteral("”")); },
              QStringLiteral("”"), 1);
    cjkExpect(checks, details, QStringLiteral("imeCurlyDoubleQuoteSkipsExistingCloser"),
              QStringLiteral("中文 “内容”"), 6, 6,
              QStringLiteral("IME commit ” before existing closer"),
              [] { return inputMethodCommit(QStringLiteral("”")); },
              QStringLiteral("中文 “内容”"), 7);
    cjkExpect(checks, details, QStringLiteral("imeCurlySingleQuoteSkipsExistingCloser"),
              QStringLiteral("中文 ‘内容’"), 6, 6,
              QStringLiteral("IME commit ’ before existing closer"),
              [] { return inputMethodCommit(QStringLiteral("’")); },
              QStringLiteral("中文 ‘内容’"), 7);
    cjkExpect(checks, details, QStringLiteral("midQuoteImeCurlySingleCloseCjk"),
              QStringLiteral("a‘中文x"), 4, 4,
              QStringLiteral("IME commit ’ closes single-quote wrap"),
              [] { return inputMethodCommit(QStringLiteral("’")); },
              QStringLiteral("a ‘中文’ x"), 6);
    cjkExpect(checks, details, QStringLiteral("midQuoteImeCurlyReverseOrderSpacing"),
              QStringLiteral("前中文”后"), 1, 1,
              QStringLiteral("IME commit “ before an existing closer"),
              [] { return inputMethodCommit(QStringLiteral("“")); },
              QStringLiteral("前 “中文” 后"), 3);
    cjkExpect(checks, details, QStringLiteral("midQuoteImeAsciiCloseCjkConverts"),
              QStringLiteral("a\"中文x"), 4, 4, QStringLiteral("IME commit \" closes CJK wrap"),
              [] { return inputMethodCommit(QStringLiteral("\"")); },
              QStringLiteral("a “中文” x"), 6);
    cjkExpect(checks, details, QStringLiteral("midQuoteImeBacktickSingleOpen"),
              QStringLiteral("abc"), 1, 1, QStringLiteral("IME commit ` at midline"),
              [] { return inputMethodCommit(QStringLiteral("`")); },
              QStringLiteral("a`bc"), 2);
    cjkExpect(checks, details, QStringLiteral("midQuoteKeyBacktickCloseCjkSpacing"),
              QStringLiteral("a`中文x"), 4, 4,
              QStringLiteral("key ` closes wrap with boundary spacing"),
              keyAction(QStringLiteral("`")), QStringLiteral("a `中文` x"), 5);
    cjkExpect(checks, details, QStringLiteral("midQuoteImeBacktickCloseCjkSpacing"),
              QStringLiteral("a`中文x"), 4, 4,
              QStringLiteral("IME commit ` closes wrap with boundary spacing"),
              [] { return inputMethodCommit(QStringLiteral("`")); },
              QStringLiteral("a `中文` x"), 5);
    {
        setTextAndSelection(QStringLiteral("中文"), 2, 2);
        inputMethodCommit(QStringLiteral("“"));
        QThread::msleep(30);
        const QString completed = editorText();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString undone = editorText();
        addCheck(checks, details, QStringLiteral("imeCurlyQuoteCompletionSingleUndo"),
                 completed == QStringLiteral("中文 “”")
                     && undone == QStringLiteral("中文"),
                 QJsonObject{{QStringLiteral("completed"), completed},
                             {QStringLiteral("undone"), undone}});
    }
    {
        setTextAndSelection(QStringLiteral("中文"), 2, 2);
        inputMethodCommit(QStringLiteral("”"));
        QThread::msleep(30);
        const QString completed = editorText();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString undone = editorText();
        addCheck(checks, details, QStringLiteral("imeCurlyCloserCompatibilitySingleUndo"),
                 completed == QStringLiteral("中文 “”")
                     && undone == QStringLiteral("中文"),
                 QJsonObject{{QStringLiteral("completed"), completed},
                             {QStringLiteral("undone"), undone}});
    }

    // --- `·`（U+00B7）反引号别名（DOT-ALIAS-001..012） ---
    // 紧贴字符输入单个 `·` 保持字面；连续两个 `·` 生成反引号对（光标居中）。
    cjkExpect(checks, details, QStringLiteral("dotAliasLiteralAfterChar"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key · directly after CJK"),
              keyAction(QStringLiteral("·")), QStringLiteral("中文·"), 3);
    cjkExpect(checks, details, QStringLiteral("dotAliasImeLiteral"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("IME commit · directly after CJK"),
              [] { return inputMethodCommit(QStringLiteral("·")); },
              QStringLiteral("中文·"), 3);
    cjkExpect(checks, details, QStringLiteral("dotAliasDoubleLineEndPair"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("key · twice directly after CJK"),
              [] {
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  return keyPress(QStringLiteral("·"), QStringLiteral("·"));
              },
              QStringLiteral("中文 ``"), 4);
    cjkExpect(checks, details, QStringLiteral("dotAliasImeDoubleLineEndPair"),
              QStringLiteral("中文"), 2, 2, QStringLiteral("IME commit · twice directly after CJK"),
              [] {
                  inputMethodCommit(QStringLiteral("·"));
                  return inputMethodCommit(QStringLiteral("·"));
              },
              QStringLiteral("中文 ``"), 4);
    cjkExpect(checks, details, QStringLiteral("dotAliasDoubleMidlinePair"),
              QStringLiteral("中文"), 1, 1, QStringLiteral("key · twice midline"),
              [] {
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  return keyPress(QStringLiteral("·"), QStringLiteral("·"));
              },
              QStringLiteral("中 `` 文"), 3);
    cjkExpect(checks, details, QStringLiteral("dotAliasImeDoubleMidlinePair"),
              QStringLiteral("中文"), 1, 1, QStringLiteral("IME commit · twice midline"),
              [] {
                  inputMethodCommit(QStringLiteral("·"));
                  return inputMethodCommit(QStringLiteral("·"));
              },
              QStringLiteral("中 `` 文"), 3);
    // 仅在空格后单点号时触发反引号转换。
    cjkExpect(checks, details, QStringLiteral("dotAliasAfterSpaceBecomesBacktick"),
              QStringLiteral("中文 "), 3, 3, QStringLiteral("key · after space"),
              keyAction(QStringLiteral("·")), QStringLiteral("中文 ``"), 4);
    cjkExpect(checks, details, QStringLiteral("dotAliasAsciiAfterSpace"),
              QStringLiteral("abc "), 4, 4, QStringLiteral("key · after space (ASCII)"),
              keyAction(QStringLiteral("·")), QStringLiteral("abc ``"), 5);
    cjkExpect(checks, details, QStringLiteral("dotAliasImeAfterSpace"),
              QStringLiteral("中文 "), 3, 3, QStringLiteral("IME commit · after space"),
              [] { return inputMethodCommit(QStringLiteral("·")); },
              QStringLiteral("中文 ``"), 4);
    cjkExpect(checks, details, QStringLiteral("dotAliasSpaceAfterDotKeepsLiteral"),
              QStringLiteral("中文·"), 3, 3, QStringLiteral("key space after ·"),
              keyAction(QStringLiteral(" ")), QStringLiteral("中文· "), 4);
    // 行首与空行的连续双 `·` 同样生成反引号对（DOT2-001..004）。
    cjkExpect(checks, details, QStringLiteral("dotAliasDoubleLineStartPair"),
              QStringLiteral("中文"), 0, 0, QStringLiteral("key · twice at line start"),
              [] {
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  return keyPress(QStringLiteral("·"), QStringLiteral("·"));
              },
              QStringLiteral("`` 中文"), 1);
    cjkExpect(checks, details, QStringLiteral("dotAliasImeDoubleLineStartPair"),
              QStringLiteral("中文"), 0, 0, QStringLiteral("IME commit · twice at line start"),
              [] {
                  inputMethodCommit(QStringLiteral("·"));
                  return inputMethodCommit(QStringLiteral("·"));
              },
              QStringLiteral("`` 中文"), 1);
    cjkExpect(checks, details, QStringLiteral("dotAliasDoubleEmptyLinePair"),
              QString(), 0, 0, QStringLiteral("key · twice on empty line"),
              [] {
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  return keyPress(QStringLiteral("·"), QStringLiteral("·"));
              },
              QStringLiteral("``"), 1);
    cjkExpect(checks, details, QStringLiteral("dotAliasImeDoubleEmptyLinePair"),
              QString(), 0, 0, QStringLiteral("IME commit · twice on empty line"),
              [] {
                  inputMethodCommit(QStringLiteral("·"));
                  return inputMethodCommit(QStringLiteral("·"));
              },
              QStringLiteral("``"), 1);
    // 完全空行上连按三个 `·`：`` 对升级为大代码块围栏（DOT2-FENCE-001..002）。
    cjkExpect(checks, details, QStringLiteral("dotAliasTripleEmptyLineFence"),
              QString(), 0, 0, QStringLiteral("key · three times on empty line"),
              [] {
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  return keyPress(QStringLiteral("·"), QStringLiteral("·"));
              },
              QStringLiteral("```\n```"), 3);
    cjkExpect(checks, details, QStringLiteral("dotAliasImeTripleEmptyLineFence"),
              QString(), 0, 0, QStringLiteral("IME commit · three times on empty line"),
              [] {
                  inputMethodCommit(QStringLiteral("·"));
                  inputMethodCommit(QStringLiteral("·"));
                  return inputMethodCommit(QStringLiteral("·"));
              },
              QStringLiteral("```\n```"), 3);
    // 行尾 ASCII 双 `·` 同样成对（DOT2-005）。
    cjkExpect(checks, details, QStringLiteral("dotAliasDoubleLineEndAsciiPair"),
              QStringLiteral("abc"), 3, 3, QStringLiteral("key · twice after ASCII"),
              [] {
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  return keyPress(QStringLiteral("·"), QStringLiteral("·"));
              },
              QStringLiteral("abc ``"), 5);
    // 围栏代码块内：双 `·` 与三连点均保持字面（DOT2-FENCE-003..005）。
    cjkExpect(checks, details, QStringLiteral("dotAliasDoubleInsideFenceLiteral"),
              QStringLiteral("```\n\n```"), 4, 4,
              QStringLiteral("key · twice inside fenced code"),
              [] {
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  return keyPress(QStringLiteral("·"), QStringLiteral("·"));
              },
              QStringLiteral("```\n··\n```"), 6);
    cjkExpect(checks, details, QStringLiteral("dotAliasImeDoubleInsideFenceLiteral"),
              QStringLiteral("```\n\n```"), 4, 4,
              QStringLiteral("IME commit · twice inside fenced code"),
              [] {
                  inputMethodCommit(QStringLiteral("·"));
                  return inputMethodCommit(QStringLiteral("·"));
              },
              QStringLiteral("```\n··\n```"), 6);
    cjkExpect(checks, details, QStringLiteral("dotAliasTripleInsideFenceLiteral"),
              QStringLiteral("```\n\n```"), 4, 4,
              QStringLiteral("key · three times inside fenced code"),
              [] {
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  keyPress(QStringLiteral("·"), QStringLiteral("·"));
                  return keyPress(QStringLiteral("·"), QStringLiteral("·"));
              },
              QStringLiteral("```\n···\n```"), 7);
    // 有选区时输入单个 `·` 等价于 `` ` ``：包裹选区并触发自动空格（DOT2-WRAP-001）。
    cjkExpect(checks, details, QStringLiteral("dotAliasSelectionWrapsBackticks"),
              QStringLiteral("中文"), 1, 2, QStringLiteral("key · with selection"),
              keyAction(QStringLiteral("·")),
              QStringLiteral("中 `文`"), -1, 3, 4);
    {
        // 撤销：光标回到输入 `·` 之前的位置（DOT-UNDO-CURSOR-001..003）。
        setTextAndSelection(QStringLiteral("中文 "), 3, 3);
        keyPress(QStringLiteral("·"), QStringLiteral("·"));
        QThread::msleep(30);
        const QString spaced = editorText();
        const int spacedCursor = editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString undone = editorText();
        const int undoneCursor = editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("dotAliasUndoRestoresPosition"),
                 spaced == QStringLiteral("中文 ``") && spacedCursor == 4
                     && undone == QStringLiteral("中文 ") && undoneCursor == 3,
                 QJsonObject{{QStringLiteral("spaced"), spaced},
                             {QStringLiteral("spacedCursor"), spacedCursor},
                             {QStringLiteral("undone"), undone},
                             {QStringLiteral("undoneCursor"), undoneCursor}});

        // IME 提交的插入与转换/自动空格必须合并为一次撤销（DOT-UNDO-IME-001..002）。
        setTextAndSelection(QStringLiteral("中文 "), 3, 3);
        inputMethodCommit(QStringLiteral("·"));
        QThread::msleep(30);
        const QString imeSpaced = editorText();
        const int imeSpacedCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString imeUndone = editorText();
        const int imeUndoneCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("dotAliasImeUndoRestoresPosition"),
                 imeSpaced == QStringLiteral("中文 ``") && imeSpacedCursor == 4
                     && imeUndone == QStringLiteral("中文 ") && imeUndoneCursor == 3,
                 QJsonObject{{QStringLiteral("imeSpaced"), imeSpaced},
                             {QStringLiteral("imeSpacedCursor"), imeSpacedCursor},
                             {QStringLiteral("imeUndone"), imeUndone},
                             {QStringLiteral("imeUndoneCursor"), imeUndoneCursor}});

        setTextAndSelection(QStringLiteral("中文"), 2, 2);
        inputMethodCommit(QStringLiteral("A"));
        QThread::msleep(30);
        const QString imeAutoSpaced = editorText();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString imeAutoUndone = editorText();
        addCheck(checks, details, QStringLiteral("imeCommitAutoSpacingUndoRestores"),
                 imeAutoSpaced == QStringLiteral("中文 A")
                     && imeAutoUndone == QStringLiteral("中文"),
                 QJsonObject{{QStringLiteral("imeAutoSpaced"), imeAutoSpaced},
                             {QStringLiteral("imeAutoUndone"), imeAutoUndone}});
    }
    {
        // 双 `·` 成对、空行围栏升级与选区包裹均合并为一次撤销（DOT2-UNDO-001..005）。
        setTextAndSelection(QStringLiteral("中文"), 1, 1);
        keyPress(QStringLiteral("·"), QStringLiteral("·"));
        keyPress(QStringLiteral("·"), QStringLiteral("·"));
        QThread::msleep(30);
        const QString paired = editorText();
        const int pairedCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString pairedUndone = editorText();
        const int pairedUndoneCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("dotAliasDoubleUndoRestoresPosition"),
                 paired == QStringLiteral("中 `` 文") && pairedCursor == 3
                     && pairedUndone == QStringLiteral("中文") && pairedUndoneCursor == 1,
                 QJsonObject{{QStringLiteral("paired"), paired},
                             {QStringLiteral("pairedCursor"), pairedCursor},
                             {QStringLiteral("undone"), pairedUndone},
                             {QStringLiteral("undoneCursor"), pairedUndoneCursor}});

        // IME 双 `·` 同样合并为一次撤销。
        setTextAndSelection(QStringLiteral("中文"), 2, 2);
        inputMethodCommit(QStringLiteral("·"));
        inputMethodCommit(QStringLiteral("·"));
        QThread::msleep(30);
        const QString imePaired = editorText();
        const int imePairedCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString imePairedUndone = editorText();
        const int imePairedUndoneCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("dotAliasImeDoubleUndoRestoresAll"),
                 imePaired == QStringLiteral("中文 ``") && imePairedCursor == 4
                     && imePairedUndone == QStringLiteral("中文")
                     && imePairedUndoneCursor == 2,
                 QJsonObject{{QStringLiteral("paired"), imePaired},
                             {QStringLiteral("pairedCursor"), imePairedCursor},
                             {QStringLiteral("undone"), imePairedUndone},
                             {QStringLiteral("undoneCursor"), imePairedUndoneCursor}});

        setTextAndSelection(QString(), 0, 0);
        keyPress(QStringLiteral("·"), QStringLiteral("·"));
        keyPress(QStringLiteral("·"), QStringLiteral("·"));
        keyPress(QStringLiteral("·"), QStringLiteral("·"));
        QThread::msleep(30);
        const QString fenced = editorText();
        const int fencedCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString fencedUndone = editorText();
        const int fencedUndoneCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("dotAliasTripleFenceUndoRestoresAll"),
                 fenced == QStringLiteral("```\n```") && fencedCursor == 3
                     && fencedUndone == QString() && fencedUndoneCursor == 0,
                 QJsonObject{{QStringLiteral("fenced"), fenced},
                             {QStringLiteral("fencedCursor"), fencedCursor},
                             {QStringLiteral("undone"), fencedUndone},
                             {QStringLiteral("undoneCursor"), fencedUndoneCursor}});

        // IME 空行三连点围栏同样合并为一次撤销。
        setTextAndSelection(QString(), 0, 0);
        inputMethodCommit(QStringLiteral("·"));
        inputMethodCommit(QStringLiteral("·"));
        inputMethodCommit(QStringLiteral("·"));
        QThread::msleep(30);
        const QString imeFenced = editorText();
        const int imeFencedCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString imeFencedUndone = editorText();
        const int imeFencedUndoneCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("dotAliasImeTripleFenceUndoRestoresAll"),
                 imeFenced == QStringLiteral("```\n```") && imeFencedCursor == 3
                     && imeFencedUndone == QString() && imeFencedUndoneCursor == 0,
                 QJsonObject{{QStringLiteral("fenced"), imeFenced},
                             {QStringLiteral("fencedCursor"), imeFencedCursor},
                             {QStringLiteral("undone"), imeFencedUndone},
                             {QStringLiteral("undoneCursor"), imeFencedUndoneCursor}});

        setTextAndSelection(QStringLiteral("中文"), 1, 2);
        keyPress(QStringLiteral("·"), QStringLiteral("·"));
        QThread::msleep(30);
        const QString wrapped = editorText();
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString wrappedUndone = editorText();
        const int wrappedUndoneCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("dotAliasSelectionWrapUndoRestores"),
                 wrapped == QStringLiteral("中 `文`") && wrappedUndone == QStringLiteral("中文")
                     && wrappedUndoneCursor == 2,
                 QJsonObject{{QStringLiteral("wrapped"), wrapped},
                             {QStringLiteral("undone"), wrappedUndone},
                             {QStringLiteral("undoneCursor"), wrappedUndoneCursor}});
    }
    {
        // 行中双反引号：光标位于两个反引号中间，两侧自动空格（BT-PAIR-001..002）。
        setTextAndSelection(QStringLiteral("中文xyz"), 4, 4);
        keyPress(QStringLiteral("`"), QStringLiteral("`"));
        QThread::msleep(30);
        keyPress(QStringLiteral("`"), QStringLiteral("`"));
        QThread::msleep(30);
        const QString pairText = editorText();
        const int pairCursor = editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("backtickPairMidlineSpacing"),
                 pairText == QStringLiteral("中文xy `` z") && pairCursor == 6,
                 QJsonObject{{QStringLiteral("text"), pairText},
                             {QStringLiteral("cursor"), pairCursor}});

        setTextAndSelection(QString(), 0, 0);
        keyPress(QStringLiteral("`"), QStringLiteral("`"));
        QThread::msleep(30);
        keyPress(QStringLiteral("`"), QStringLiteral("`"));
        QThread::msleep(30);
        keyPress(QStringLiteral("`"), QStringLiteral("`"));
        QThread::msleep(30);
        const QString fenceText = editorText();
        addCheck(checks, details, QStringLiteral("backtickFenceUpgradeLineStart"),
                 fenceText == QStringLiteral("```\n```"),
                 QJsonObject{{QStringLiteral("text"), fenceText}});
    }
    {
        // 行首围栏自动补全：光标后本行有文字时，闭合围栏必须单独成行，
        // 后续文字移到闭合围栏下一行，避免闭合边界后紧跟文字
        // （FENCE-SUFFIX-001..003：键盘三次 / IME / 单次整串）。
        cjkExpect(checks, details, QStringLiteral("fenceSuffixOwnLineKeyboard"),
                  QStringLiteral("测试文字"), 0, 0,
                  QStringLiteral("key ` three times at line start before text"),
                  [] {
                      keyPress(QStringLiteral("`"), QStringLiteral("`"));
                      keyPress(QStringLiteral("`"), QStringLiteral("`"));
                      return keyPress(QStringLiteral("`"), QStringLiteral("`"));
                  },
                  QStringLiteral("```\n```\n测试文字"), 3);
        cjkExpect(checks, details, QStringLiteral("fenceSuffixOwnLineIme"),
                  QStringLiteral("测试文字"), 0, 0,
                  QStringLiteral("IME commit ``` at line start before text"),
                  [] { return inputMethodCommit(QStringLiteral("```")); },
                  QStringLiteral("```\n```\n测试文字"), 3);
        cjkExpect(checks, details, QStringLiteral("fenceSuffixOwnLineSingleEvent"),
                  QStringLiteral("测试文字"), 0, 0,
                  QStringLiteral("single key ``` at line start before text"),
                  [] { return keyPress(QStringLiteral("```"), QStringLiteral("```")); },
                  QStringLiteral("```\n```\n测试文字"), 3);
        // 行首围栏自动补全：光标后无文字时不补多余换行（FENCE-EMPTY-001..002）。
        cjkExpect(checks, details, QStringLiteral("fenceEmptyLineNoExtraNewlineIme"),
                  QString(), 0, 0,
                  QStringLiteral("IME commit ``` on empty line"),
                  [] { return inputMethodCommit(QStringLiteral("```")); },
                  QStringLiteral("```\n```"), 3);
        cjkExpect(checks, details, QStringLiteral("fenceNextLineTextStays"),
                  QStringLiteral("\n测试文字"), 0, 0,
                  QStringLiteral("key ` three times on empty line above text"),
                  [] {
                      keyPress(QStringLiteral("`"), QStringLiteral("`"));
                      keyPress(QStringLiteral("`"), QStringLiteral("`"));
                      return keyPress(QStringLiteral("`"), QStringLiteral("`"));
                  },
                  QStringLiteral("```\n```\n测试文字"), 3);
        cjkExpect(checks, details, QStringLiteral("fenceTrailingEmptyLineStays"),
                  QStringLiteral("测试文字\n"), 5, 5,
                  QStringLiteral("key ` three times on trailing empty line"),
                  [] {
                      keyPress(QStringLiteral("`"), QStringLiteral("`"));
                      keyPress(QStringLiteral("`"), QStringLiteral("`"));
                      return keyPress(QStringLiteral("`"), QStringLiteral("`"));
                  },
                  QStringLiteral("测试文字\n```\n```"), 8);
        // 光标后只有空白：空白不算文字，不加换行（FENCE-WHITESPACE-001）。
        cjkExpect(checks, details, QStringLiteral("fenceWhitespaceSuffixNoNewline"),
                  QStringLiteral("  "), 0, 0,
                  QStringLiteral("key ` three times before spaces only"),
                  [] {
                      keyPress(QStringLiteral("`"), QStringLiteral("`"));
                      keyPress(QStringLiteral("`"), QStringLiteral("`"));
                      return keyPress(QStringLiteral("`"), QStringLiteral("`"));
                  },
                  QStringLiteral("```\n```  "), 3);
        // 仅列 0 行首触发：行中、行末、带缩进行首均不自动补全围栏，
        // IME 提交保持字面（FENCE-SCOPE-001..003）。
        cjkExpect(checks, details, QStringLiteral("fenceMidLineNoCompletionIme"),
                  QStringLiteral("中文字"), 1, 1,
                  QStringLiteral("IME commit ``` in line middle"),
                  [] { return inputMethodCommit(QStringLiteral("```")); },
                  QStringLiteral("中```文字"), 4);
        cjkExpect(checks, details, QStringLiteral("fenceLineEndNoCompletionIme"),
                  QStringLiteral("测试文字"), 4, 4,
                  QStringLiteral("IME commit ``` at line end"),
                  [] { return inputMethodCommit(QStringLiteral("```")); },
                  QStringLiteral("测试文字```"), 7);
        cjkExpect(checks, details, QStringLiteral("fenceIndentedLineStartNoCompletionIme"),
                  QStringLiteral("  测试文字"), 2, 2,
                  QStringLiteral("IME commit ``` on indented line start"),
                  [] { return inputMethodCommit(QStringLiteral("```")); },
                  QStringLiteral("  ```测试文字"), 5);
        // 键盘三次单独输入在行中、行末、带缩进行首同样不产生围栏（回归保护）。
        const auto keyTriple = [] {
            keyPress(QStringLiteral("`"), QStringLiteral("`"));
            keyPress(QStringLiteral("`"), QStringLiteral("`"));
            return keyPress(QStringLiteral("`"), QStringLiteral("`"));
        };
        setTextAndSelection(QStringLiteral("中文字"), 1, 1);
        keyTriple();
        QThread::msleep(30);
        const QString midLineText = editorText();
        setTextAndSelection(QStringLiteral("测试文字"), 4, 4);
        keyTriple();
        QThread::msleep(30);
        const QString lineEndText = editorText();
        setTextAndSelection(QStringLiteral("  测试文字"), 2, 2);
        keyTriple();
        QThread::msleep(30);
        const QString indentedText = editorText();
        addCheck(checks, details,
                 QStringLiteral("fenceNoCompletionOutsideLineStartKeyboard"),
                 !midLineText.contains(QStringLiteral("\n```"))
                     && !lineEndText.contains(QStringLiteral("\n```"))
                     && !indentedText.contains(QStringLiteral("\n```")),
                 QJsonObject{{QStringLiteral("midLine"), midLineText},
                             {QStringLiteral("lineEnd"), lineEndText},
                             {QStringLiteral("indented"), indentedText}});
        // 行首带字围栏补全后一次撤销恢复原文（FENCE-UNDO-001）。
        setTextAndSelection(QStringLiteral("测试文字"), 0, 0);
        inputMethodCommit(QStringLiteral("```"));
        QThread::msleep(30);
        request(QStringLiteral("testUndo"));
        QThread::msleep(30);
        const QString undoneText = editorText();
        const int undoneCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("fenceSuffixOwnLineImeUndo"),
                 undoneText == QStringLiteral("测试文字") && undoneCursor == 0,
                 QJsonObject{{QStringLiteral("undoneText"), undoneText},
                             {QStringLiteral("undoneCursor"), undoneCursor}});
    }
    {
        // 先输后引号、再输前引号也必须与先前后后一样触发两侧自动空格
        // （BT-REVERSE-001..004：键盘/IME × CJK/ASCII，引号全角转换）。
        const auto moveCursor = [](int position) {
            return request(QStringLiteral("testSetSelection"),
                           {{QStringLiteral("start"), position},
                            {QStringLiteral("end"), position}});
        };

        setTextAndSelection(QStringLiteral("前中文后"), 3, 3);
        keyPress(QStringLiteral("`"), QStringLiteral("`"));
        QThread::msleep(30);
        moveCursor(1);
        keyPress(QStringLiteral("`"), QStringLiteral("`"));
        QThread::msleep(30);
        const QString reverseKeyboardText = editorText();
        const int reverseKeyboardCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("backtickReverseOrderKeyboardSpacing"),
                 reverseKeyboardText == QStringLiteral("前 `中文` 后")
                     && reverseKeyboardCursor == 3,
                 QJsonObject{{QStringLiteral("text"), reverseKeyboardText},
                             {QStringLiteral("cursor"), reverseKeyboardCursor}});

        setTextAndSelection(QStringLiteral("前中文后"), 3, 3);
        inputMethodCommit(QStringLiteral("`"));
        QThread::msleep(30);
        moveCursor(1);
        inputMethodCommit(QStringLiteral("`"));
        QThread::msleep(30);
        const QString reverseImeText = editorText();
        const int reverseImeCursor =
            editorStatus().value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, QStringLiteral("backtickReverseOrderImeSpacing"),
                 reverseImeText == QStringLiteral("前 `中文` 后")
                     && reverseImeCursor == 3,
                 QJsonObject{{QStringLiteral("text"), reverseImeText},
                             {QStringLiteral("cursor"), reverseImeCursor}});

        setTextAndSelection(QStringLiteral("abcd"), 3, 3);
        keyPress(QStringLiteral("`"), QStringLiteral("`"));
        QThread::msleep(30);
        moveCursor(1);
        keyPress(QStringLiteral("`"), QStringLiteral("`"));
        QThread::msleep(30);
        const QString reverseAsciiText = editorText();
        addCheck(checks, details, QStringLiteral("backtickReverseOrderAsciiSpacing"),
                 reverseAsciiText == QStringLiteral("a `bc` d"),
                 QJsonObject{{QStringLiteral("text"), reverseAsciiText}});

        setTextAndSelection(QStringLiteral("前中文后"), 3, 3);
        keyPress(QStringLiteral("\""), QStringLiteral("\""));
        QThread::msleep(30);
        moveCursor(1);
        keyPress(QStringLiteral("\""), QStringLiteral("\""));
        QThread::msleep(30);
        const QString reverseQuoteText = editorText();
        addCheck(checks, details, QStringLiteral("quoteReverseOrderFullwidthSpacing"),
                 reverseQuoteText == QStringLiteral("前 “中文” 后"),
                 QJsonObject{{QStringLiteral("text"), reverseQuoteText}});
    }
    cjkExpect(checks, details, QStringLiteral("protectKeyInlineFormula"),
              QStringLiteral("$中文..$"), 5, 5, QStringLiteral("key . inside inline formula"),
              keyAction(QStringLiteral(".")), QStringLiteral("$中文...$"), 6);
    cjkExpect(checks, details, QStringLiteral("protectKeyFence"),
              QStringLiteral("```\n中文-\n```"), 7, 7,
              QStringLiteral("key - inside fenced code"),
              keyAction(QStringLiteral("-")), QStringLiteral("```\n中文--\n```"), 8);
    cjkExpect(checks, details, QStringLiteral("protectKeyBlockFormula"),
              QStringLiteral("$$\n中文\n$$"), 5, 5,
              QStringLiteral("key , inside block formula"),
              keyAction(QStringLiteral(",")), QStringLiteral("$$\n中文,\n$$"), 6);
    setTextAndSelection(QStringLiteral("$$中文$$"), 4, 4);
    keyPress(QStringLiteral(","), QStringLiteral(","));
    QThread::msleep(30);
    const QString singleLineFormulaCommaText = editorText();
    const bool singleLineFormulaCommaKept =
        singleLineFormulaCommaText == QStringLiteral("$$中文,$$");
    setTextAndSelection(QStringLiteral("$$中文$$"), 3, 3);
    keyPress(QStringLiteral("A"), QStringLiteral("A"));
    QThread::msleep(30);
    const QString singleLineFormulaSpacingText = editorText();
    const bool singleLineFormulaSpacingKept =
        singleLineFormulaSpacingText == QStringLiteral("$$中A文$$");
    const QJsonObject singleLineFormulaStatus = editorStatus();
    addCjkCheck(checks, details, QStringLiteral("protectKeySingleLineFormula"),
                singleLineFormulaCommaKept && singleLineFormulaSpacingKept,
                QStringLiteral("$$中文$$"), QStringLiteral("key , / key A inside $$...$$"),
                QStringLiteral("$$中文,$$ 与 $$中A文$$"),
                singleLineFormulaCommaText + QStringLiteral(" 与 ")
                    + singleLineFormulaSpacingText,
                singleLineFormulaStatus);
}

} // namespace EditingTests
