#include "editorcommandregistry.h"
#include "appsettings.h"

#include <QKeySequence>
#include <QMetaObject>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTextDocument>
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
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+Alt+H"), {}, false},
        {QStringLiteral("toggleList"), QStringLiteral("切换项目列表"),
         QStringLiteral("Markdown"), QStringLiteral("Ctrl+Shift+L"), {}, false},
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
    return transformSelectedLines(commandId);
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
    if (commandId != QStringLiteral("cycleHeading")
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
            continue;
        }
        if (commandId == QStringLiteral("cycleHeading")) {
            const QRegularExpressionMatch match = headingPrefix.match(line);
            if (!match.hasMatch()) {
                line.prepend(QStringLiteral("# "));
            } else {
                const int level = match.captured(1).size();
                line.remove(match.capturedStart(), match.capturedLength());
                if (level < 6) {
                    line.prepend(QString(level + 1, QLatin1Char('#')) + QLatin1Char(' '));
                }
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
    selectRange(lineStart, lineStart + transformed.size());
    focusEditor();
    return true;
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
