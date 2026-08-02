#include "editorcommandregistry.h"
#include "appsettings.h"

#include <QEvent>
#include <QInputMethodEvent>
#include <QKeySequence>
#include <QKeyEvent>
#include <QMetaObject>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>

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
        {QStringLiteral("toggleList"), QStringLiteral("切换项目列表"),
         QStringLiteral("Markdown"), QString(), {}, false},
        {QStringLiteral("toggleTask"), QStringLiteral("切换任务项"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+Alt+T"), {}, false},
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
    };

    for (Definition &item : m_definitions) {
        item.shortcut = m_settings ? m_settings->shortcut(item.id, item.defaultShortcut)
                                   : item.defaultShortcut;
    }
}

void EditorCommandRegistry::setEditor(QObject *editor, QTextDocument *document)
{
    m_editor = editor;
    m_document = document;
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
    return transformSelectedLines(commandId);
}

bool EditorCommandRegistry::handleEditorEvent(QEvent *event)
{
    if (!event || !m_editor || !m_document) {
        return false;
    }

    if (event->type() == QEvent::KeyPress) {
        const auto *keyEvent = static_cast<QKeyEvent *>(event);
        const Qt::KeyboardModifiers modifiers = keyEvent->modifiers();
        const bool shiftPressed = modifiers.testFlag(Qt::ShiftModifier);
        const bool tabPressed = keyEvent->key() == Qt::Key_Tab
            || keyEvent->key() == Qt::Key_Backtab;
        if (tabPressed
            && !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            if (shiftPressed || keyEvent->key() == Qt::Key_Backtab) {
                return changeIndent(true);
            }
            return jumpOutOfPair() || changeIndent(false);
        }

        if (!(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
            && !keyEvent->text().isEmpty()) {
            return handleTypedText(keyEvent->text());
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
        if (!relevant) {
            return false;
        }

        const int start = m_editor->property("selectionStart").toInt();
        const int end = m_editor->property("selectionEnd").toInt();
        const QString beforeText = m_document->toPlainText();
        const QString selection = selectedText();
        QTimer::singleShot(0, this,
                           [this, committedText, beforeText, selection, start, end] {
            completeInputMethodCommit(committedText, beforeText, selection, start, end);
        });
    }
    return false;
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
    const int start = m_editor->property("selectionStart").toInt();
    const int end = m_editor->property("selectionEnd").toInt();
    QTextCursor cursor(m_document);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    QString text = normalizeSelectedText(cursor.selectedText());
    const QString documentText = m_document->toPlainText();
    const bool surrounded = !text.isEmpty() && start >= opening.size()
        && documentText.mid(start - opening.size(), opening.size()) == opening
        && documentText.mid(end, closing.size()) == closing;

    cursor.beginEditBlock();
    if (surrounded) {
        cursor.setPosition(start - opening.size());
        cursor.setPosition(end + closing.size(), QTextCursor::KeepAnchor);
        cursor.insertText(text);
        selectRange(start - opening.size(), start - opening.size() + text.size());
    } else if (!text.isEmpty() && text.startsWith(opening) && text.endsWith(closing)
        && text.size() >= opening.size() + closing.size()) {
        text = text.mid(opening.size(), text.size() - opening.size() - closing.size());
        cursor.insertText(text);
        selectRange(start, start + text.size());
    } else if (text.isEmpty()) {
        cursor.insertText(opening + closing);
        m_editor->setProperty("cursorPosition", start + opening.size());
    } else {
        cursor.insertText(opening + text + closing);
        selectRange(start + opening.size(), start + opening.size() + text.size());
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
    if (commandId != QStringLiteral("cycleHeading") && !setHeading && !adjustHeading
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
            if (commandId == QStringLiteral("toggleQuote") && !removeQuote) {
                line = QStringLiteral("> ");
            }
            continue;
        }
        if (commandId == QStringLiteral("cycleHeading") || setHeading || adjustHeading) {
            const QRegularExpressionMatch match = headingPrefix.match(line);
            const int currentLevel = match.hasMatch() ? match.captured(1).size() : 0;
            int targetLevel = 0;
            if (setHeading) {
                targetLevel = commandId.back().digitValue();
            } else if (commandId == QStringLiteral("increaseHeadingLevel")) {
                targetLevel = currentLevel == 0 ? 1 : qMin(6, currentLevel + 1);
            } else if (commandId == QStringLiteral("decreaseHeadingLevel")) {
                targetLevel = currentLevel == 0 ? 6 : qMax(1, currentLevel - 1);
            } else if (currentLevel == 0) {
                targetLevel = 1;
            } else if (currentLevel < 6) {
                targetLevel = currentLevel + 1;
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
    QTextCursor cursor(m_document);
    cursor.setPosition(lineStart);
    cursor.setPosition(lineEnd, QTextCursor::KeepAnchor);
    cursor.beginEditBlock();
    cursor.insertText(transformed);
    cursor.endEditBlock();
    if (commandId == QStringLiteral("toggleQuote")) {
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

bool EditorCommandRegistry::handleTypedText(const QString &text)
{
    if (text == QStringLiteral("```")) {
        return insertFenceBlock();
    }
    if (text.size() != 1) {
        return false;
    }

    const QString documentText = m_document->toPlainText();
    const int start = m_editor->property("selectionStart").toInt();
    const int end = m_editor->property("selectionEnd").toInt();
    const bool hasSelection = start != end;

    if (text == QStringLiteral("`") && !hasSelection) {
        if (start >= 2 && start < documentText.size()
            && documentText.mid(start - 2, 3) == QStringLiteral("```")) {
            QTextCursor cursor(m_document);
            cursor.setPosition(start - 2);
            cursor.setPosition(start + 1, QTextCursor::KeepAnchor);
            cursor.insertText(QStringLiteral("```\n```"));
            m_editor->setProperty("cursorPosition", start + 1);
            return true;
        }
        if (start > 0 && start < documentText.size()
            && documentText.at(start - 1) == QLatin1Char('`')
            && documentText.at(start) == QLatin1Char('`')) {
            QTextCursor cursor(m_document);
            cursor.setPosition(start);
            cursor.insertText(QStringLiteral("`"));
            m_editor->setProperty("cursorPosition", start + 1);
            return true;
        }
    }

    if (const DelimiterPair *pair = pairForOpening(text)) {
        if (!hasSelection && pair->opening == pair->closing
            && documentText.mid(start, pair->closing.size()) == pair->closing) {
            m_editor->setProperty("cursorPosition", start + pair->closing.size());
            return true;
        }
        return insertPair(pair->opening, pair->closing);
    }

    if (!hasSelection && isClosingDelimiter(text)
        && documentText.mid(start, text.size()) == text) {
        m_editor->setProperty("cursorPosition", start + text.size());
        return true;
    }
    return false;
}

bool EditorCommandRegistry::insertPair(const QString &opening, const QString &closing)
{
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
    return true;
}

bool EditorCommandRegistry::insertFenceBlock()
{
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

void EditorCommandRegistry::completeInputMethodCommit(const QString &committedText,
                                                       const QString &beforeText,
                                                       const QString &selection,
                                                       int selectionStart, int selectionEnd)
{
    if (!m_editor || !m_document) {
        return;
    }
    QString expectedText = beforeText;
    expectedText.replace(selectionStart, selectionEnd - selectionStart, committedText);
    if (m_document->toPlainText() != expectedText) {
        return;
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
        return;
    }

    QString replacement;
    int contentOffset = 0;
    if (committedText == QStringLiteral("```")) {
        replacement = QStringLiteral("```") + selection + QStringLiteral("\n```");
        contentOffset = 3;
    } else if (openingPair) {
        replacement = openingPair->opening + selection + openingPair->closing;
        contentOffset = openingPair->opening.size();
    } else {
        return;
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
}

void EditorCommandRegistry::selectRange(int start, int end)
{
    if (m_editor) {
        QMetaObject::invokeMethod(m_editor, "select", Q_ARG(int, start), Q_ARG(int, end));
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
