#pragma once

#include "inputautoscroll.h"

#include <QCursor>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>

class QTextDocument;
class AppSettings;
class HeadingFoldManager;
class QEvent;
class QMouseEvent;
class QQuickItem;
class QQuickWindow;

class EditorCommandRegistry final : public QObject
{
    Q_OBJECT

public:
    explicit EditorCommandRegistry(AppSettings *settings, bool clipboardHistoryAvailable = true,
                                   QObject *parent = nullptr);
    ~EditorCommandRegistry() override;

    void setEditor(QObject *editor, QTextDocument *document);
    void setWindow(QQuickWindow *window);
    using ClipboardReader = std::function<QString()>;
    using ClipboardWriter = std::function<bool(const QString &text)>;
    void setClipboardAccess(ClipboardReader reader, ClipboardWriter writer);
    QVariantList commands() const;
    QString shortcut(const QString &commandId) const;
    QVariantList headingFoldMarkers() const;
    int headingFoldVisibleEndPosition() const;
    QVariantMap headingNavigationHighlight() const;
    QVariantMap headingFoldDiagnostics() const;

    bool setShortcut(const QString &commandId, const QString &sequence, QString *errorMessage);
    void resetShortcuts();
    bool execute(const QString &commandId);
    bool toggleHeadingFoldAt(int headingPosition);
    void resetHeadingFolds();
    bool handleEditorEvent(QEvent *event);
    bool performUndo();
    bool performRedo();
    bool insertPathText(const QString &text);
    QVariantMap inputScrollDiagnostics() const;
    bool beginExternalTextDrag(const QString &text, const QPointF &scenePosition);
    bool updateExternalTextDrag(const QPointF &scenePosition);
    bool finishExternalTextDrag(const QPointF &scenePosition);
    void cancelExternalTextDrag();
    bool externalTextDragActive() const;
    bool externalTextDragCursorOverridden() const;
    int externalTextDragCursorShape() const;

    bool findNext(const QString &query, bool caseSensitive, bool backwards);
    bool replaceCurrent(const QString &query, const QString &replacement, bool caseSensitive);
    int replaceAll(const QString &query, const QString &replacement, bool caseSensitive);
    QQuickItem *editorViewport() const;

signals:
    void commandsChanged();
    void headingFoldMarkersChanged();
    void headingNavigationHighlightChanged();
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

    struct SelectionUndoSnapshot {
        QString originalText;
        QString formattedText;
        int selectionStart = 0;
        int selectionEnd = 0;
        int cursorPosition = 0;
        int formattedSelectionStart = -1;
        int formattedSelectionEnd = -1;
        int formattedCursorPosition = -1;
    };

    struct UndoCursorRecord {
        int undoStepsBefore;
        int selectionStart;
        int selectionEnd;
        int cursorPosition;
    };
    UndoCursorRecord captureUndoCursor() const;
    void rememberUndoCursor(const UndoCursorRecord &record);

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
    bool convertToOrderedList();
    bool deleteSelectedLines();
    bool clearDocument();
    bool copyLine();
    bool cutLine();
    bool cutSelection();
    bool pasteClipboard();
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
    std::optional<EditFootprint> insertWrapped(const QString &opening, const QString &closing);
    CompletionResult finishMidlineQuoteClosure(int openerPosition, int closurePosition,
                                               QChar opening, bool closingAlreadyAtCursor);
    CompletionResult finishMidlineQuoteOpening(int openerPosition, int closerPosition,
                                               QChar opening);
    bool handleSpecialBackspace();
    bool handleStructuralDelete(bool backwards);
    bool handleListEnter(bool insideFencedBlock);
    bool handleQuoteEnter(bool preserveEmptyQuote, bool insideFencedBlock);
    void repairOrderedLists(const QString &beforeText, const QString &afterText,
                            bool preservePreviousStart,
                            bool resetNewSequenceStart = false);
    bool jumpOutOfPair();
    bool changeIndent(bool outdent);
    bool formatSpacing();
    void applyAutoSpacing(EditFootprint footprint, bool includeInternalBoundaries = false,
                          const std::optional<QString> &expectedText = std::nullopt);
    bool isInsideFencedBlock(int position) const;
    std::optional<CompletionResult> completeInputMethodCommit(
        const QString &committedText, const QString &beforeText,
        const QString &selectedText, int selectionStart, int selectionEnd);
    void selectRange(int start, int end);
    void selectRangeWithActiveEnd(int start, int end, int activeEnd);
    void focusEditor();
    QString selectedText() const;
    bool moveByCjkAwareWord(bool left, bool keepSelection);
    bool deleteByCjkAwareWord(bool backwards);
    bool handleCjkDoubleClick(QMouseEvent *event);
    bool moveSelection(int selectionStart, int selectionEnd, int dropPosition);
    bool insertExternalText(const QString &text, int dropPosition);
    int editorPositionAt(const QPointF &localPosition) const;
    QQuickItem *editorItem() const;
    bool navigateToHeading(bool backwards);
    void scheduleHeadingScroll(int position);
    void scrollViewportToHeading(int position);
    void beginFindNavigation();
    void finishFindNavigation();

    class SelectionDrag;

    AppSettings *m_settings = nullptr;
    std::unique_ptr<HeadingFoldManager> m_headingFolds;
    QPointer<QObject> m_editor;
    QPointer<QTextDocument> m_document;
    QPointer<QQuickWindow> m_window;
    InputAutoScroll m_inputScroll;
    QString m_documentTextSnapshot;
    bool m_documentTextSnapshotPrepared = false;
    QVector<Definition> m_definitions;
    QHash<QString, std::function<bool()>> m_commandHandlers;
    ClipboardReader m_clipboardReader;
    ClipboardWriter m_clipboardWriter;
    std::unique_ptr<SelectionDrag> m_drag;
    bool m_doubleClickReplaying = false;
    std::optional<SelectionUndoSnapshot> m_selectionUndoSnapshot;
    std::optional<SelectionUndoSnapshot> m_selectionRedoSnapshot;
    QMap<int, UndoCursorRecord> m_undoCursorRecords;
    int m_observedUndoSteps = 0;
    QTimer m_headingScrollTimer;
    int m_pendingHeadingScrollPosition = -1;
};
