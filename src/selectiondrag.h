#pragma once

#include "editorcommandregistry.h"

class EditorCommandRegistry::SelectionDrag final : public QObject
{
public:
    explicit SelectionDrag(EditorCommandRegistry &registry);
    ~SelectionDrag() override;
    void resetForEditorChange();
    bool beginExternalTextDrag(const QString &text, const QPointF &scenePosition);
    bool updateExternalTextDrag(const QPointF &scenePosition);
    bool finishExternalTextDrag(const QPointF &scenePosition);
    void cancelExternalTextDrag();
    bool externalTextDragActive() const;
    bool externalTextDragCursorOverridden() const;
    int externalTextDragCursorShape() const;
    bool handleSelectionDragEvent(QEvent *event);
    void trackNativeSelectionDragEvent(QEvent *event);

private:
    int visibleEditorPositionAt(const QPointF &scenePosition) const;
    void beginSelectionDrag(int selectionStart, int selectionEnd,
                            const QPointF &scenePosition);
    void updateSelectionDrag(const QPointF &scenePosition, bool scrollViewport);
    void resetSelectionDrag(bool releaseMouseGrab);
    void updateNativeSelectionDrag();
    void resetNativeSelectionDrag();
    void updateExternalTextDragPosition(const QPointF &scenePosition,
                                        bool scrollViewport);
    void updateExternalTextDragCursor(bool canDrop);
    void resetExternalTextDrag();
    bool scrollTextDragViewport(const QPointF &scenePosition);

    EditorCommandRegistry &m_registry;
    const QPointer<QObject> &m_editor;
    const QPointer<QQuickWindow> &m_window;
    QTimer m_selectionDragScrollTimer;
    QPointF m_selectionDragPressScenePosition;
    QPointF m_selectionDragScenePosition;
    int m_selectionDragStart = -1;
    int m_selectionDragEnd = -1;
    int m_selectionDropPosition = -1;
    bool m_selectionDragActive = false;
    bool m_selectionDragPreviousKeepMouseGrab = false;
    QCursor m_selectionDragOriginalCursor;
    QPointF m_nativeSelectionDragPressScenePosition;
    QPointF m_nativeSelectionDragScenePosition;
    Qt::KeyboardModifiers m_nativeSelectionDragModifiers = Qt::NoModifier;
    bool m_nativeSelectionDragPending = false;
    bool m_nativeSelectionDragActive = false;
    QString m_externalDragText;
    QPointF m_externalDragPressScenePosition;
    QPointF m_externalDragScenePosition;
    int m_externalDropPosition = -1;
    bool m_externalDragActive = false;
    bool m_externalDragCursorOverridden = false;
    Qt::CursorShape m_externalDragCursorShape = Qt::ArrowCursor;
};
