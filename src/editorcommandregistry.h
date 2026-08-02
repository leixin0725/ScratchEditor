#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVector>

class QTextDocument;
class AppSettings;

class EditorCommandRegistry final : public QObject
{
    Q_OBJECT

public:
    explicit EditorCommandRegistry(AppSettings *settings, QObject *parent = nullptr);

    void setEditor(QObject *editor, QTextDocument *document);
    QVariantList commands() const;
    QString shortcut(const QString &commandId) const;

    bool setShortcut(const QString &commandId, const QString &sequence, QString *errorMessage);
    void resetShortcuts();
    bool execute(const QString &commandId);

    bool findNext(const QString &query, bool caseSensitive, bool backwards);
    bool replaceCurrent(const QString &query, const QString &replacement, bool caseSensitive);
    int replaceAll(const QString &query, const QString &replacement, bool caseSensitive);

signals:
    void commandsChanged();
    void uiCommandRequested(const QString &commandId);

private:
    struct Definition {
        QString id;
        QString title;
        QString category;
        QString defaultShortcut;
        QString shortcut;
        bool uiCommand = false;
    };

    Definition *definition(const QString &commandId);
    const Definition *definition(const QString &commandId) const;
    bool wrapSelection(const QString &opening, const QString &closing);
    bool transformSelectedLines(const QString &commandId);
    void selectRange(int start, int end);
    void focusEditor();
    QString selectedText() const;

    AppSettings *m_settings = nullptr;
    QPointer<QObject> m_editor;
    QPointer<QTextDocument> m_document;
    QVector<Definition> m_definitions;
};
