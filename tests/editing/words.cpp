#include "suites.h"
#include "support.h"

#include <QJsonObject>
#include <QStringList>
#include "cjktextprocessor.h"

namespace EditingTests {

void checkWordBoundaries(QJsonObject &checks, QJsonObject &details)
{
    // --- 中英文词边界：纯函数测试（SEG-001..） ---
    const auto wordBoundaryExpect = [&](const QString &name, const QString &text,
                                        int position, int direction, int expected) {
        const int actual = CjkText::moveWordBoundary(text, position, direction);
        addCheck(checks, details, name, actual == expected,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("position"), position},
                             {QStringLiteral("direction"), direction},
                             {QStringLiteral("expected"), expected},
                             {QStringLiteral("actual"), actual}});
    };
    const auto wordRangeExpect = [&](const QString &name, const QString &text,
                                     int position, int expectedStart,
                                     int expectedEnd) {
        const CjkText::WordRange actual = CjkText::wordRangeAt(text, position);
        const QString expected =
            QStringLiteral("[%1,%2)").arg(expectedStart).arg(expectedEnd);
        const QString actualRange =
            QStringLiteral("[%1,%2)").arg(actual.start).arg(actual.end);
        addCheck(checks, details, name,
                 actual.start == expectedStart && actual.end == expectedEnd,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("position"), position},
                             {QStringLiteral("expected"), expected},
                             {QStringLiteral("actual"), actualRange}});
    };
    const auto spanCjkExpect = [&](const QString &name, const QString &text,
                                   int start, int end, bool expected) {
        const bool actual = CjkText::spanContainsCjk(text, start, end);
        addCheck(checks, details, name, actual == expected,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("start"), start},
                             {QStringLiteral("end"), end},
                             {QStringLiteral("expected"), expected},
                             {QStringLiteral("actual"), actual}});
    };
    const auto hanCountExpect = [&](const QString &name, const QString &text,
                                    int expected) {
        const int actual = CjkText::countHanCharacters(text);
        addCheck(checks, details, name, actual == expected,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("expected"), expected},
                             {QStringLiteral("actual"), actual}});
    };
    const auto ucs4 = [](char32_t codePoint) {
        return QString::fromUcs4(&codePoint, 1);
    };
    const auto wordRangeCursorExpect = [&](const QString &name, const QString &text,
                                           int position, int expectedStart,
                                           int expectedEnd) {
        const CjkText::WordRange actual =
            CjkText::wordRangeForCursor(text, position);
        const QString expected =
            QStringLiteral("[%1,%2)").arg(expectedStart).arg(expectedEnd);
        const QString actualRange =
            QStringLiteral("[%1,%2)").arg(actual.start).arg(actual.end);
        addCheck(checks, details, name,
                 actual.start == expectedStart && actual.end == expectedEnd,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("position"), position},
                             {QStringLiteral("expected"), expected},
                             {QStringLiteral("actual"), actualRange}});
    };

    const QString cjkRunText = QStringLiteral("今天天气真好");
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkRunRightFromStart"),
                       cjkRunText, 0, 1, 6);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkRunRightInside"),
                       cjkRunText, 3, 1, 6);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkRunRightAtEnd"),
                       cjkRunText, 6, 1, 6);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkRunLeftFromEnd"),
                       cjkRunText, 6, -1, 0);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkRunLeftInside"),
                       cjkRunText, 2, -1, 0);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkRunLeftAtStart"),
                       cjkRunText, 0, -1, 0);
    wordRangeExpect(QStringLiteral("wordRangeCjkRunMiddle"), cjkRunText, 3, 0, 6);
    wordRangeExpect(QStringLiteral("wordRangeCjkRunEnd"), cjkRunText, 6, 0, 6);
    spanCjkExpect(QStringLiteral("spanCjkRunAll"), cjkRunText, 0, 6, true);

    // --- 汉字字符数统计（HAN-001..）：只计汉字表意字符，按码点计数 ---
    hanCountExpect(QStringLiteral("hanCountPlainCjk"), QStringLiteral("你好世界"), 4);
    hanCountExpect(QStringLiteral("hanCountMixedLatin"), QStringLiteral("你好 World 123"), 2);
    hanCountExpect(QStringLiteral("hanCountExtensionA"), ucs4(0x3400), 1);
    hanCountExpect(QStringLiteral("hanCountCompatibilityIdeograph"), ucs4(0xFA0E), 1);
    hanCountExpect(QStringLiteral("hanCountSupplementaryPlane"), ucs4(0x20000), 1);
    hanCountExpect(QStringLiteral("hanCountExcludesKana"), QStringLiteral("こんにちは"), 0);
    hanCountExpect(QStringLiteral("hanCountExcludesHangul"), QStringLiteral("한글"), 0);
    hanCountExpect(QStringLiteral("hanCountExcludesCjkPunctuation"),
                   QStringLiteral("，。！？、"), 0);
    hanCountExpect(QStringLiteral("hanCountExcludesAscii"), QStringLiteral("abc 123 !"), 0);
    hanCountExpect(QStringLiteral("hanCountEmpty"), QString(), 0);

    const QString hanRangeText = QStringLiteral("你好abc好");
    addCheck(checks, details, QStringLiteral("hanCountRangeSubset"),
             CjkText::countHanCharacters(hanRangeText, 0, 2) == 2
                 && CjkText::countHanCharacters(hanRangeText, 2, 5) == 0
                 && CjkText::countHanCharacters(hanRangeText, 5, 6) == 1
                 && CjkText::countHanCharacters(hanRangeText, 1, 6) == 2
                 && CjkText::countHanCharacters(hanRangeText, 3, 3) == 0
                 && CjkText::countHanCharacters(hanRangeText, -3, 99) == 3
                 && CjkText::countHanCharacters(hanRangeText, 4, 2) == 0
                 && CjkText::countHanCharacters(hanRangeText, 0, -1) == 3,
             QJsonObject{{QStringLiteral("input"), hanRangeText}});

    const char32_t supplementaryCodes[] = {0x20000, 0x4E00};
    const QString hanPairText = QString::fromUcs4(supplementaryCodes, 2);
    addCheck(checks, details, QStringLiteral("hanCountRangeSurrogateBoundary"),
             CjkText::countHanCharacters(hanPairText) == 2
                 && CjkText::countHanCharacters(hanPairText, 0, 2) == 1
                 && CjkText::countHanCharacters(hanPairText, 0, 1) == 0
                 && CjkText::countHanCharacters(hanPairText, 1, 3) == 1,
             QJsonObject{{QStringLiteral("input"), hanPairText}});

    const QString cjkPunctText = QStringLiteral("今天，天气真好！我们走吧");
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctRightRun"),
                       cjkPunctText, 0, 1, 2);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctRightSkipComma"),
                       cjkPunctText, 2, 1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctRightRun2"),
                       cjkPunctText, 3, 1, 7);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctRightSkipBang"),
                       cjkPunctText, 7, 1, 8);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctRightLastRun"),
                       cjkPunctText, 8, 1, 12);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctLeftLastRun"),
                       cjkPunctText, 12, -1, 8);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctLeftSkipBang"),
                       cjkPunctText, 8, -1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctLeftSkipComma"),
                       cjkPunctText, 3, -1, 0);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctLeftInsideFirst"),
                       cjkPunctText, 2, -1, 0);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkPunctLeftInsideSecond"),
                       cjkPunctText, 7, -1, 3);
    wordRangeExpect(QStringLiteral("wordRangeCjkPunctRunFirst"),
                    cjkPunctText, 1, 0, 2);
    wordRangeExpect(QStringLiteral("wordRangeCjkPunctComma"),
                    cjkPunctText, 2, 2, 3);
    wordRangeExpect(QStringLiteral("wordRangeCjkPunctRunSecond"),
                    cjkPunctText, 4, 3, 7);
    wordRangeExpect(QStringLiteral("wordRangeCjkPunctBang"),
                    cjkPunctText, 7, 7, 8);
    wordRangeExpect(QStringLiteral("wordRangeCjkPunctRunLast"),
                    cjkPunctText, 9, 8, 12);

    const QString mixedText = QStringLiteral("abc今天天气 good 123");
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedRightLatin"),
                       mixedText, 0, 1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedRightLatinInside"),
                       mixedText, 1, 1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedRightCjk"),
                       mixedText, 3, 1, 7);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedRightSpace"),
                       mixedText, 7, 1, 8);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedRightWord"),
                       mixedText, 8, 1, 12);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedRightSpaceNum"),
                       mixedText, 12, 1, 13);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedRightNumber"),
                       mixedText, 13, 1, 16);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedLeftNumber"),
                       mixedText, 16, -1, 13);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedLeftWord"),
                       mixedText, 13, -1, 8);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedLeftSpaceCjk"),
                       mixedText, 8, -1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedLeftLatin"),
                       mixedText, 3, -1, 0);
    wordBoundaryExpect(QStringLiteral("wordBoundaryMixedLeftCjkInside"),
                       mixedText, 5, -1, 3);
    wordRangeExpect(QStringLiteral("wordRangeMixedLatin"),
                    mixedText, 1, 0, 3);
    wordRangeExpect(QStringLiteral("wordRangeMixedCjk"),
                    mixedText, 4, 3, 7);
    wordRangeExpect(QStringLiteral("wordRangeMixedWord"),
                    mixedText, 8, 8, 12);
    wordRangeExpect(QStringLiteral("wordRangeMixedNumber"),
                    mixedText, 14, 13, 16);
    spanCjkExpect(QStringLiteral("spanCjkMixedLatinOnly"), mixedText, 0, 3, false);
    spanCjkExpect(QStringLiteral("spanCjkMixedLatinAndCjk"), mixedText, 0, 4, true);

    const QString cjkAsciiText = QStringLiteral("中文，ABC");
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkAsciiRightRun"),
                       cjkAsciiText, 0, 1, 2);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkAsciiRightSkipComma"),
                       cjkAsciiText, 2, 1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkAsciiRightWord"),
                       cjkAsciiText, 3, 1, 6);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkAsciiLeftWord"),
                       cjkAsciiText, 6, -1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkAsciiLeftSkipComma"),
                       cjkAsciiText, 3, -1, 0);
    wordBoundaryExpect(QStringLiteral("wordBoundaryCjkAsciiLeftRun"),
                       cjkAsciiText, 2, -1, 0);

    QString emojiText = QStringLiteral("中");
    emojiText += QChar(0xD83D);
    emojiText += QChar(0xDE00);
    emojiText += QStringLiteral("文");
    wordBoundaryExpect(QStringLiteral("wordBoundaryEmojiRightCjk"),
                       emojiText, 0, 1, 1);
    wordBoundaryExpect(QStringLiteral("wordBoundaryEmojiRightSkipPair"),
                       emojiText, 1, 1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryEmojiRightTail"),
                       emojiText, 3, 1, 4);
    wordBoundaryExpect(QStringLiteral("wordBoundaryEmojiLeftTail"),
                       emojiText, 4, -1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryEmojiLeftSkipPair"),
                       emojiText, 3, -1, 0);
    wordBoundaryExpect(QStringLiteral("wordBoundaryEmojiLeftInsidePair"),
                       emojiText, 2, -1, 0);
    wordRangeExpect(QStringLiteral("wordRangeEmojiHead"), emojiText, 0, 0, 1);
    wordRangeExpect(QStringLiteral("wordRangeEmojiPair"), emojiText, 1, 1, 3);
    wordRangeExpect(QStringLiteral("wordRangeEmojiTail"), emojiText, 3, 3, 4);
    wordRangeExpect(QStringLiteral("wordRangeEmojiEnd"), emojiText, 4, 3, 4);
    spanCjkExpect(QStringLiteral("spanCjkEmojiOnly"), emojiText, 1, 3, false);
    spanCjkExpect(QStringLiteral("spanCjkEmojiAndCjk"), emojiText, 0, 4, true);

    QString extBText;
    extBText += QChar(0xD840);
    extBText += QChar(0xDC00);
    extBText += QStringLiteral("中");
    wordBoundaryExpect(QStringLiteral("wordBoundaryExtBRight"),
                       extBText, 0, 1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryExtBLeft"),
                       extBText, 3, -1, 0);
    wordRangeExpect(QStringLiteral("wordRangeExtB"), extBText, 0, 0, 3);
    spanCjkExpect(QStringLiteral("spanCjkExtB"), extBText, 0, 2, true);

    const QString newlineText = QStringLiteral("中文\ndef");
    wordBoundaryExpect(QStringLiteral("wordBoundaryNewlineRightRun"),
                       newlineText, 0, 1, 2);
    wordBoundaryExpect(QStringLiteral("wordBoundaryNewlineRightSkip"),
                       newlineText, 2, 1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryNewlineRightWord"),
                       newlineText, 3, 1, 6);
    wordBoundaryExpect(QStringLiteral("wordBoundaryNewlineLeftWord"),
                       newlineText, 6, -1, 3);
    wordBoundaryExpect(QStringLiteral("wordBoundaryNewlineLeftSkip"),
                       newlineText, 3, -1, 0);

    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkRunInside"),
                          cjkRunText, 3, 0, 6);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkRunStart"),
                          cjkRunText, 0, 0, 6);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkRunEnd"),
                          cjkRunText, 6, 0, 6);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkPunctRunFirst"),
                          cjkPunctText, 1, 0, 2);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkPunctBeforeComma"),
                          cjkPunctText, 2, 0, 2);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkPunctAfterComma"),
                          cjkPunctText, 3, 3, 7);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkPunctRunSecond"),
                          cjkPunctText, 4, 3, 7);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkPunctBeforeBang"),
                          cjkPunctText, 7, 3, 7);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkPunctAfterBang"),
                          cjkPunctText, 8, 8, 12);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkPunctRunLast"),
                          cjkPunctText, 9, 8, 12);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorMixedLatinInside"),
                          mixedText, 1, 0, 3);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorMixedCjkLatinBoundary"),
                          mixedText, 3, 0, 3);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorMixedCjkInside"),
                          mixedText, 4, 3, 7);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorMixedWordStart"),
                          mixedText, 8, 8, 12);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorMixedWordEnd"),
                          mixedText, 12, 8, 12);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkAsciiBeforeComma"),
                          cjkAsciiText, 2, 0, 2);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorCjkAsciiAfterComma"),
                          cjkAsciiText, 3, 3, 6);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorLatinEndOfWord"),
                          QStringLiteral("a big"), 5, 2, 5);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorLatinStartOfWord"),
                          QStringLiteral("a big rat"), 6, 6, 9);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorLatinEmptyBoundary"),
                          QStringLiteral("a big "), 6, 6, 6);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorEmojiBetween"),
                          emojiText, 1, 0, 1);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorEmojiPair"),
                          emojiText, 2, 1, 3);
    wordRangeCursorExpect(QStringLiteral("wordRangeCursorEmojiTail"),
                          emojiText, 3, 3, 4);

    const auto wordDeletionExpect = [&](const QString &name, const QString &text,
                                        int position, bool backwards,
                                        int expectedStart, int expectedEnd) {
        const CjkText::WordRange actual =
            CjkText::wordDeletionRange(text, position, backwards);
        const QString expected =
            QStringLiteral("[%1,%2)").arg(expectedStart).arg(expectedEnd);
        const QString actualRange =
            QStringLiteral("[%1,%2)").arg(actual.start).arg(actual.end);
        addCheck(checks, details, name,
                 actual.start == expectedStart && actual.end == expectedEnd,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("position"), position},
                             {QStringLiteral("backwards"), backwards},
                             {QStringLiteral("expected"), expected},
                             {QStringLiteral("actual"), actualRange}});
    };

    wordDeletionExpect(QStringLiteral("wordDeletionCjkRunBackFromEnd"),
                       cjkRunText, 6, true, 0, 6);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkRunBackInside"),
                       cjkRunText, 3, true, 0, 3);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkRunBackAtStart"),
                       cjkRunText, 0, true, 0, 0);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkRunFwdFromStart"),
                       cjkRunText, 0, false, 0, 6);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkRunFwdInside"),
                       cjkRunText, 3, false, 3, 6);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkRunFwdAtEnd"),
                       cjkRunText, 6, false, 6, 6);
    wordDeletionExpect(QStringLiteral("wordDeletionMixedBackCjkFromEnd"),
                       mixedText, 8, true, 3, 8);
    wordDeletionExpect(QStringLiteral("wordDeletionMixedBackInsideCjk"),
                       mixedText, 4, true, 3, 4);
    wordDeletionExpect(QStringLiteral("wordDeletionMixedFwdLatin"),
                       mixedText, 0, false, 0, 3);
    wordDeletionExpect(QStringLiteral("wordDeletionMixedFwdCjk"),
                       mixedText, 3, false, 3, 8);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkPunctFwdRunAndComma"),
                       cjkPunctText, 0, false, 0, 3);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkPunctFwdCommaOnly"),
                       cjkPunctText, 2, false, 2, 3);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkPunctFwdSecondRun"),
                       cjkPunctText, 3, false, 3, 8);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkPunctBackWordAndComma"),
                       cjkPunctText, 3, true, 0, 3);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkPunctBackSecondRun"),
                       cjkPunctText, 7, true, 3, 7);
    wordDeletionExpect(QStringLiteral("wordDeletionLatinBackWordAndSpace"),
                       QStringLiteral("hello world"), 6, true, 0, 6);
    wordDeletionExpect(QStringLiteral("wordDeletionLatinBackInsideWord"),
                       QStringLiteral("hello world"), 11, true, 6, 11);
    wordDeletionExpect(QStringLiteral("wordDeletionLatinFwdWordAndSpace"),
                       QStringLiteral("hello world"), 0, false, 0, 6);
    wordDeletionExpect(QStringLiteral("wordDeletionLatinFwdSpaceOnly"),
                       QStringLiteral("hello world"), 5, false, 5, 6);
    wordDeletionExpect(QStringLiteral("wordDeletionLatinBackAtStart"),
                       QStringLiteral("hello world"), 0, true, 0, 0);
    wordDeletionExpect(QStringLiteral("wordDeletionLatinFwdAtEnd"),
                       QStringLiteral("hello world"), 11, false, 11, 11);
    wordDeletionExpect(QStringLiteral("wordDeletionNewlineBackOnlyNewline"),
                       newlineText, 3, true, 2, 3);
    wordDeletionExpect(QStringLiteral("wordDeletionNewlineFwdOnlyNewline"),
                       newlineText, 2, false, 2, 3);
    wordDeletionExpect(QStringLiteral("wordDeletionCjkSpaceBackWordAndSpace"),
                       QStringLiteral("中文 test"), 3, true, 0, 3);

    // --- 中英文词边界：端到端（Ctrl+方向键 / 双击选词） ---
    const auto wordNavExpect = [&](const QString &name, const QString &text,
                                   int start, const QString &key, bool shift,
                                   const QString &modifiers, int expectedCursor,
                                   int expectedSelStart, int expectedSelEnd) {
        setTextAndSelection(text, start, start);
        const QJsonObject result = keyPress({}, key, shift, modifiers);
        const int cursor = result.value(QStringLiteral("cursorPosition")).toInt();
        const int selStart = result.value(QStringLiteral("selectionStart")).toInt();
        const int selEnd = result.value(QStringLiteral("selectionEnd")).toInt();
        addCheck(checks, details, name,
                 cursor == expectedCursor && selStart == expectedSelStart
                     && selEnd == expectedSelEnd,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("start"), start},
                             {QStringLiteral("key"), key},
                             {QStringLiteral("shift"), shift},
                             {QStringLiteral("modifiers"), modifiers},
                             {QStringLiteral("expectedCursor"), expectedCursor},
                             {QStringLiteral("actualCursor"), cursor},
                             {QStringLiteral("expectedSelection"),
                              QStringLiteral("[%1,%2)").arg(expectedSelStart)
                                  .arg(expectedSelEnd)},
                             {QStringLiteral("actualSelection"),
                              QStringLiteral("[%1,%2)").arg(selStart).arg(selEnd)}});
    };

    wordNavExpect(QStringLiteral("e2eCtrlRightCjkRun"),
                  cjkRunText, 0, QStringLiteral("Right"), false,
                  QStringLiteral("ctrl"), 6, 6, 6);
    wordNavExpect(QStringLiteral("e2eCtrlLeftCjkRun"),
                  cjkRunText, 6, QStringLiteral("Left"), false,
                  QStringLiteral("ctrl"), 0, 0, 0);
    wordNavExpect(QStringLiteral("e2eCtrlRightCjkPunctFirstRun"),
                  cjkPunctText, 0, QStringLiteral("Right"), false,
                  QStringLiteral("ctrl"), 2, 2, 2);
    wordNavExpect(QStringLiteral("e2eCtrlRightCjkPunctSkipComma"),
                  cjkPunctText, 2, QStringLiteral("Right"), false,
                  QStringLiteral("ctrl"), 3, 3, 3);
    wordNavExpect(QStringLiteral("e2eCtrlLeftCjkPunctSkipBang"),
                  cjkPunctText, 8, QStringLiteral("Left"), false,
                  QStringLiteral("ctrl"), 3, 3, 3);
    wordNavExpect(QStringLiteral("e2eCtrlLeftCjkPunctSkipComma"),
                  cjkPunctText, 3, QStringLiteral("Left"), false,
                  QStringLiteral("ctrl"), 0, 0, 0);
    wordNavExpect(QStringLiteral("e2eCtrlRightMixedCjkBoundary"),
                  mixedText, 3, QStringLiteral("Right"), false,
                  QStringLiteral("ctrl"), 7, 7, 7);
    wordNavExpect(QStringLiteral("e2eCtrlRightMixedSpace"),
                  mixedText, 7, QStringLiteral("Right"), false,
                  QStringLiteral("ctrl"), 8, 8, 8);
    wordNavExpect(QStringLiteral("e2eCtrlLeftMixedCjkStart"),
                  mixedText, 8, QStringLiteral("Left"), false,
                  QStringLiteral("ctrl"), 3, 3, 3);
    wordNavExpect(QStringLiteral("e2eCtrlLeftMixedLatinStart"),
                  mixedText, 3, QStringLiteral("Left"), false,
                  QStringLiteral("ctrl"), 0, 0, 0);

    // Ctrl+Shift 扩选：锚点固定，active end 逐词移动；活动端追平锚点按
    // Qt 原生语义收起选区。
    setTextAndSelection(cjkPunctText, 12, 12);
    const QJsonObject shiftLeft1 = keyPress({}, QStringLiteral("Left"), true,
                                            QStringLiteral("ctrl"));
    const QJsonObject shiftLeft2 = keyPress({}, QStringLiteral("Left"), true,
                                            QStringLiteral("ctrl"));
    const QJsonObject shiftRight1 = keyPress({}, QStringLiteral("Right"), true,
                                             QStringLiteral("ctrl"));
    addCheck(checks, details, QStringLiteral("e2eCtrlShiftSelection"),
             shiftLeft1.value(QStringLiteral("cursorPosition")).toInt() == 8
                 && shiftLeft1.value(QStringLiteral("selectionStart")).toInt() == 8
                 && shiftLeft1.value(QStringLiteral("selectionEnd")).toInt() == 12
                 && shiftLeft2.value(QStringLiteral("cursorPosition")).toInt() == 3
                 && shiftLeft2.value(QStringLiteral("selectionStart")).toInt() == 3
                 && shiftLeft2.value(QStringLiteral("selectionEnd")).toInt() == 12
                 && shiftRight1.value(QStringLiteral("cursorPosition")).toInt() == 7
                 && shiftRight1.value(QStringLiteral("selectionStart")).toInt() == 7
                 && shiftRight1.value(QStringLiteral("selectionEnd")).toInt() == 12,
             QJsonObject{{QStringLiteral("shiftLeft1"), shiftLeft1},
                         {QStringLiteral("shiftLeft2"), shiftLeft2},
                         {QStringLiteral("shiftRight1"), shiftRight1}});

    // 纯 ASCII 文本：Ctrl+方向键落点必须与 Qt 原生逐位置一致（不回归）。
    const QString latinText = QStringLiteral("Hello, world! This is a test.");
    setTextAndSelection(latinText, 0, 0);
    const QVector<int> expectedLatinRight{5, 7, 12, 14, 19, 22, 24, 28, 29};
    bool latinRightOk = true;
    QVector<int> latinRightActual;
    for (const int expected : expectedLatinRight) {
        const QJsonObject result = keyPress({}, QStringLiteral("Right"), false,
                                            QStringLiteral("ctrl"));
        const int cursor = result.value(QStringLiteral("cursorPosition")).toInt();
        latinRightActual.append(cursor);
        latinRightOk = latinRightOk && cursor == expected;
    }
    addCheck(checks, details, QStringLiteral("e2eCtrlRightLatinParity"),
             latinRightOk,
             QJsonObject{{QStringLiteral("expected"),
                          [&expectedLatinRight] {
                              QStringList values;
                              for (int value : expectedLatinRight) {
                                  values << QString::number(value);
                              }
                              return values.join(QLatin1Char(','));
                          }()},
                         {QStringLiteral("actual"),
                          [&latinRightActual] {
                              QStringList values;
                              for (int value : latinRightActual) {
                                  values << QString::number(value);
                              }
                              return values.join(QLatin1Char(','));
                          }()}});
    setTextAndSelection(latinText, latinText.size(), latinText.size());
    const QVector<int> expectedLatinLeft{28, 24, 22, 19, 14, 12, 7, 5, 0};
    bool latinLeftOk = true;
    QVector<int> latinLeftActual;
    for (const int expected : expectedLatinLeft) {
        const QJsonObject result = keyPress({}, QStringLiteral("Left"), false,
                                            QStringLiteral("ctrl"));
        const int cursor = result.value(QStringLiteral("cursorPosition")).toInt();
        latinLeftActual.append(cursor);
        latinLeftOk = latinLeftOk && cursor == expected;
    }
    addCheck(checks, details, QStringLiteral("e2eCtrlLeftLatinParity"),
             latinLeftOk,
             QJsonObject{{QStringLiteral("expected"),
                          [&expectedLatinLeft] {
                              QStringList values;
                              for (int value : expectedLatinLeft) {
                                  values << QString::number(value);
                              }
                              return values.join(QLatin1Char(','));
                          }()},
                         {QStringLiteral("actual"),
                          [&latinLeftActual] {
                              QStringList values;
                              for (int value : latinLeftActual) {
                                  values << QString::number(value);
                              }
                              return values.join(QLatin1Char(','));
                          }()}});

    const auto dblClickExpect = [&](const QString &name, const QString &text,
                                    int position, int expectedStart,
                                    int expectedEnd) {
        setTextAndSelection(text, 0, 0);
        const QJsonObject result = doubleClick(position);
        const int selStart = result.value(QStringLiteral("selectionStart")).toInt();
        const int selEnd = result.value(QStringLiteral("selectionEnd")).toInt();
        addCheck(checks, details, name,
                 result.value(QStringLiteral("eventsAccepted")).toBool()
                     && selStart == expectedStart && selEnd == expectedEnd,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("position"), position},
                             {QStringLiteral("expected"),
                              QStringLiteral("[%1,%2)").arg(expectedStart)
                                  .arg(expectedEnd)},
                             {QStringLiteral("actual"),
                              QStringLiteral("[%1,%2)").arg(selStart).arg(selEnd)},
                             {QStringLiteral("response"), result}});
    };

    dblClickExpect(QStringLiteral("e2eDoubleClickCjkRunMiddle"),
                   cjkPunctText, 4, 3, 7);
    dblClickExpect(QStringLiteral("e2eDoubleClickCjkRunWhole"),
                   cjkRunText, 2, 0, 6);
    dblClickExpect(QStringLiteral("e2eDoubleClickMixedLatin"),
                   mixedText, 1, 0, 3);
    dblClickExpect(QStringLiteral("e2eDoubleClickMixedCjk"),
                   mixedText, 4, 3, 7);
    dblClickExpect(QStringLiteral("e2eDoubleClickLatinWord"),
                   latinText, 8, 7, 12);
    dblClickExpect(QStringLiteral("e2eDoubleClickPunctWithSpaces"),
                   QStringLiteral("今天 ， 天气"), 3, 3, 4);

    // Markdown 快捷键（Ctrl+B / Ctrl+I / Ctrl+Alt+C）无选区时同样使用
    // 中英文自适应词边界，不再把“英文+中文”混合串整体包裹。
    const auto markdownWordExpect = [&](const QString &name, const QString &text,
                                        int position, const QString &commandId,
                                        const QString &expectedText,
                                        int expectedCursor) {
        setTextAndSelection(text, position, position);
        const QJsonObject result = execute(commandId);
        const int cursor = result.value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, name,
                 result.value(QStringLiteral("text")).toString() == expectedText
                     && cursor == expectedCursor,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("position"), position},
                             {QStringLiteral("commandId"), commandId},
                             {QStringLiteral("expectedText"), expectedText},
                             {QStringLiteral("actualText"),
                              result.value(QStringLiteral("text")).toString()},
                             {QStringLiteral("expectedCursor"), expectedCursor},
                             {QStringLiteral("actualCursor"), cursor}});
    };

    markdownWordExpect(QStringLiteral("e2eBoldCjkRunWhole"),
                       cjkRunText, 3, QStringLiteral("toggleBold"),
                       QStringLiteral("**今天天气真好**"), 5);
    markdownWordExpect(QStringLiteral("e2eBoldMixedLatinSide"),
                       mixedText, 1, QStringLiteral("toggleBold"),
                       QStringLiteral("**abc**今天天气 good 123"), 3);
    markdownWordExpect(QStringLiteral("e2eBoldMixedCjkSide"),
                       mixedText, 4, QStringLiteral("toggleBold"),
                       QStringLiteral("abc**今天天气** good 123"), 6);
    markdownWordExpect(QStringLiteral("e2eBoldMixedBoundaryTakesLeft"),
                       mixedText, 3, QStringLiteral("toggleBold"),
                       QStringLiteral("**abc**今天天气 good 123"), 5);
    markdownWordExpect(QStringLiteral("e2eBoldCjkPunctAfterComma"),
                       cjkPunctText, 3, QStringLiteral("toggleBold"),
                       QStringLiteral("今天，**天气真好**！我们走吧"), 5);
    markdownWordExpect(QStringLiteral("e2eItalicCjkRun"),
                       cjkRunText, 2, QStringLiteral("toggleItalic"),
                       QStringLiteral("*今天天气真好*"), 3);
    markdownWordExpect(QStringLiteral("e2eCodeCjkRun"),
                       cjkRunText, 2, QStringLiteral("wrapCode"),
                       QStringLiteral("`今天天气真好`"), 3);

    // Ctrl+Backspace / Ctrl+Delete 按词删除：纯 ASCII 跨度保持 Qt 原生行为，
    // 跨中文时使用同一套中英文分词边界。
    const auto wordDeleteExpect = [&](const QString &name, const QString &text,
                                      int position, const QString &key,
                                      const QString &expectedText,
                                      int expectedCursor) {
        setTextAndSelection(text, position, position);
        const QJsonObject result = keyPress({}, key, false,
                                            QStringLiteral("ctrl"));
        const int cursor = result.value(QStringLiteral("cursorPosition")).toInt();
        addCheck(checks, details, name,
                 result.value(QStringLiteral("text")).toString() == expectedText
                     && cursor == expectedCursor,
                 QJsonObject{{QStringLiteral("input"), text},
                             {QStringLiteral("position"), position},
                             {QStringLiteral("key"), key},
                             {QStringLiteral("expectedText"), expectedText},
                             {QStringLiteral("actualText"),
                              result.value(QStringLiteral("text")).toString()},
                             {QStringLiteral("expectedCursor"), expectedCursor},
                             {QStringLiteral("actualCursor"), cursor}});
    };

    wordDeleteExpect(QStringLiteral("e2eCtrlBackspaceCjkRun"),
                     cjkRunText, 6, QStringLiteral("Backspace"),
                     QString(), 0);
    wordDeleteExpect(QStringLiteral("e2eCtrlBackspaceMixedCjk"),
                     mixedText, 7, QStringLiteral("Backspace"),
                     QStringLiteral("abc good 123"), 3);
    wordDeleteExpect(QStringLiteral("e2eCtrlBackspaceInsideCjk"),
                     mixedText, 4, QStringLiteral("Backspace"),
                     QStringLiteral("abc天天气 good 123"), 3);
    wordDeleteExpect(QStringLiteral("e2eCtrlDeleteMixedLatin"),
                     mixedText, 0, QStringLiteral("Delete"),
                     QStringLiteral("今天天气 good 123"), 0);
    wordDeleteExpect(QStringLiteral("e2eCtrlDeleteCjkPunct"),
                     cjkPunctText, 0, QStringLiteral("Delete"),
                     QStringLiteral("天气真好！我们走吧"), 0);
    wordDeleteExpect(QStringLiteral("e2eCtrlBackspaceLatinParity"),
                     QStringLiteral("hello world"), 6, QStringLiteral("Backspace"),
                     QStringLiteral("world"), 0);
    wordDeleteExpect(QStringLiteral("e2eCtrlDeleteLatinParity"),
                     QStringLiteral("hello world"), 0, QStringLiteral("Delete"),
                     QStringLiteral("world"), 0);
    wordDeleteExpect(QStringLiteral("e2eCtrlBackspaceLineJoin"),
                     QStringLiteral("hello\nworld"), 6, QStringLiteral("Backspace"),
                     QStringLiteral("helloworld"), 5);
    wordDeleteExpect(QStringLiteral("e2eCtrlBackspaceNoOpAtStart"),
                     QStringLiteral("hello"), 0, QStringLiteral("Backspace"),
                     QStringLiteral("hello"), 0);
    wordDeleteExpect(QStringLiteral("e2eCtrlDeleteNoOpAtEnd"),
                     QStringLiteral("hello"), 5, QStringLiteral("Delete"),
                     QStringLiteral("hello"), 5);

    // 连续 Ctrl+Backspace 沿标点逐段删除中文词。
    setTextAndSelection(cjkPunctText, 12, 12);
    const QJsonObject deleteStep1 = keyPress({}, QStringLiteral("Backspace"),
                                             false, QStringLiteral("ctrl"));
    const QJsonObject deleteStep2 = keyPress({}, QStringLiteral("Backspace"),
                                             false, QStringLiteral("ctrl"));
    const QJsonObject deleteStep3 = keyPress({}, QStringLiteral("Backspace"),
                                             false, QStringLiteral("ctrl"));
    addCheck(checks, details, QStringLiteral("e2eCtrlBackspaceCjkPunctChain"),
             deleteStep1.value(QStringLiteral("text")).toString()
                    == QStringLiteral("今天，天气真好！")
                 && deleteStep1.value(QStringLiteral("cursorPosition")).toInt() == 8
                 && deleteStep2.value(QStringLiteral("text")).toString()
                    == QStringLiteral("今天，")
                 && deleteStep2.value(QStringLiteral("cursorPosition")).toInt() == 3
                 && deleteStep3.value(QStringLiteral("text")).toString().isEmpty()
                 && deleteStep3.value(QStringLiteral("cursorPosition")).toInt() == 0,
             QJsonObject{{QStringLiteral("step1"), deleteStep1},
                         {QStringLiteral("step2"), deleteStep2},
                         {QStringLiteral("step3"), deleteStep3}});

    // 有选区时 Ctrl+Backspace 与原生一致：删除选区。
    setTextAndSelection(mixedText, 3, 7);
    const QJsonObject deleteSelection = keyPress({}, QStringLiteral("Backspace"),
                                                 false, QStringLiteral("ctrl"));
    addCheck(checks, details, QStringLiteral("e2eCtrlWordDeleteWithSelection"),
             deleteSelection.value(QStringLiteral("text")).toString()
                    == QStringLiteral("abc good 123")
                 && deleteSelection.value(QStringLiteral("cursorPosition")).toInt() == 3,
             deleteSelection);
}

} // namespace EditingTests
