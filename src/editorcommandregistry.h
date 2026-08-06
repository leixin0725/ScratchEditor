#pragma once

#include <QCursor>
#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector>

#include <optional>

class QTextDocument;
class AppSettings;
class QEvent;
class QQuickItem;

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
    bool handleEditorEvent(QEvent *event);

    bool findNext(const QString &query, bool caseSensitive, bool backwards);
    bool replaceCurrent(const QString &query, const QString &replacement, bool caseSensitive);
    int replaceAll(const QString &query, const QString &replacement, bool caseSensitive);

signals:
    void commandsChanged();
    void uiCommandRequested(const QString &commandId);

private:
    struct EditFootprint {
        int start = 0;
        int end = 0;
    };

    struct TypedEditResult {
        bool consumed = false;
        bool textChanged = false;
        bool runAutoSpacing = false;
        EditFootprint footprint;
    };

    struct CompletionResult {
        EditFootprint footprint;
        bool autoSpace = true;
    };

    struct FormatUndoSnapshot {
        QString originalText;
        QString formattedText;
        int selectionStart = 0;
        int selectionEnd = 0;
        int cursorPosition = 0;
    };

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
    bool deleteSelectedLines();
    bool toggleCurrentCheckbox();
    TypedEditResult handleTypedText(const QString &text);
    std::optional<TypedEditResult> handleMiddleDotAlias(int start);
    std::optional<TypedEditResult> handleDoubleMiddleDot(int removeStart, int removeEnd);
    std::optional<TypedEditResult> handleEmptyLineFenceUpgrade(int removeStart, int removeEnd);
    std::optional<TypedEditResult> tryMergeMiddleDotConversion(
        const QString &text, int start, const QString &currentText);
    TypedEditResult insertBacktickPairAt(int position);
    bool isMiddleDotDoubleDot(const QString &text, int position) const;
    bool isMiddleDotEmptyLinePair(const QString &text, int position) const;
    std::optional<EditFootprint> insertPair(const QString &opening, const QString &closing);
    std::optional<EditFootprint> insertFenceBlock();
    bool handleSpecialBackspace();
    bool handleListEnter();
    bool jumpOutOfPair();
    bool changeIndent(bool outdent);
    bool formatSpacing();
    void applyAutoSpacing(EditFootprint footprint, bool includeInternalBoundaries = false);
    bool isInsideFencedBlock(int position) const;
    std::optional<CompletionResult> completeInputMethodCommit(
        const QString &committedText, const QString &beforeText,
        const QString &selectedText, int selectionStart, int selectionEnd);
    void selectRange(int start, int end);
    void selectRangeWithActiveEnd(int start, int end, int activeEnd);
    void focusEditor();
    QString selectedText() const;
    bool handleSelectionDragEvent(QEvent *event);
    bool moveSelection(int selectionStart, int selectionEnd, int dropPosition);
    int editorPositionAt(const QPointF &localPosition) const;
    QQuickItem *editorItem() const;
    QQuickItem *editorViewport() const;
    void beginSelectionDrag(int selectionStart, int selectionEnd,
                            const QPointF &scenePosition);
    void updateSelectionDrag(const QPointF &scenePosition, bool scrollViewport);
    void resetSelectionDrag(bool releaseMouseGrab);

    AppSettings *m_settings = nullptr;
    QPointer<QObject> m_editor;
    QPointer<QTextDocument> m_document;
    QVector<Definition> m_definitions;
    QTimer m_selectionDragScrollTimer;
    QPointF m_selectionDragPressScenePosition;
    QPointF m_selectionDragScenePosition;
    int m_selectionDragStart = -1;
    int m_selectionDragEnd = -1;
    int m_selectionDropPosition = -1;
    bool m_selectionDragActive = false;
    bool m_selectionDragPreviousKeepMouseGrab = false;
    QCursor m_selectionDragOriginalCursor;
    std::optional<FormatUndoSnapshot> m_formatUndoSnapshot;
};
