#pragma once

#include <QObject>
#include <QPointer>
#include <QTextBlock>
#include <QTextCursor>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class QTextDocument;

class HeadingFoldManager final : public QObject
{
    Q_OBJECT

public:
    explicit HeadingFoldManager(QObject *parent = nullptr);

    void setEditor(QObject *editor, QTextDocument *document);
    void reset();

    QVariantList markers() const;
    int visibleEndPosition() const;
    QVariantMap navigationHighlight() const;
    QVariantMap diagnostics() const;

    bool foldAll();
    bool unfoldAll();
    bool foldCurrent();
    bool unfoldCurrent();
    bool navigate(bool backwards);
    bool toggleAt(int headingPosition);
    bool revealPosition(int position);

signals:
    void markersChanged();
    void navigationHighlightChanged();

private slots:
    void handleEditorCursorPositionChanged();
    void handleEditorTextChanged();

private:
    struct Heading {
        QTextBlock block;
        int position = 0;
        int level = 0;
        int endPosition = 0;
        int highlightStart = 0;
        int highlightEnd = 0;
        bool foldable = false;
        bool collapsed = false;
    };

    void scheduleRebuild();
    void rebuild();
    void makeAllBlocksVisible();
    void applyVisibility();
    bool invalidateEditorRendering();
    void ensureCursorVisible();
    void updateMarkers();
    int currentHeadingIndex() const;
    int headingIndexAt(int position) const;
    bool setCollapsed(int index, bool collapsed);
    bool revealHeading(int index);
    void moveCursorToHeadingEndForFold(const Heading &heading);
    void rememberCursorForRestore(int position);
    void updateRestorePlaceholder(int position);
    void clearPendingCursorRestore();
    int headingTextEnd(const Heading &heading) const;
    void showNavigationHighlight(const Heading &heading);
    void clearNavigationHighlight();
    void moveCursorTo(int position);
    bool selectionIntersects(int start, int end) const;

    QPointer<QObject> m_editor;
    QPointer<QTextDocument> m_document;
    QVector<Heading> m_headings;
    QVariantList m_markers;
    QVariantMap m_navigationHighlight;
    QTextCursor m_restoreCursor;
    QTextCursor m_restorePlaceholderCursor;
    int m_visibleEndPosition = 0;
    int m_navigationHighlightRevision = 0;
    bool m_rebuildQueued = false;
    bool m_applyingVisibility = false;
    bool m_hasPendingCursorRestore = false;
    bool m_internalCursorMove = false;
    bool m_renderInvalidationWarningIssued = false;
};
