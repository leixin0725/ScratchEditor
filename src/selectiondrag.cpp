#include "selectiondrag.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStyleHints>
#include <QTextDocument>

EditorCommandRegistry::SelectionDrag::SelectionDrag(EditorCommandRegistry &registry)
    : m_registry(registry), m_editor(registry.m_editor), m_window(registry.m_window)
{
    m_selectionDragScrollTimer.setInterval(30);
    connect(&m_selectionDragScrollTimer, &QTimer::timeout, this, [this] {
        if (m_externalDragActive) {
            updateExternalTextDrag(m_externalDragScenePosition);
        } else if (m_selectionDragActive) {
            updateSelectionDrag(m_selectionDragScenePosition, true);
        } else {
            updateNativeSelectionDrag();
        }
    });
}

EditorCommandRegistry::SelectionDrag::~SelectionDrag()
{
    resetNativeSelectionDrag();
    resetExternalTextDrag();
}

void EditorCommandRegistry::SelectionDrag::resetForEditorChange()
{
    resetSelectionDrag(true);
    resetNativeSelectionDrag();
    resetExternalTextDrag();
}

bool EditorCommandRegistry::SelectionDrag::beginExternalTextDrag(const QString &text,
                                                  const QPointF &scenePosition)
{
    if (!m_editor || !m_registry.m_document || text.isEmpty()
        || m_editor->property("readOnly").toBool()
        || m_editor->property("inputMethodComposing").toBool()) {
        return false;
    }

    resetSelectionDrag(true);
    resetNativeSelectionDrag();
    resetExternalTextDrag();
    m_externalDragText = text;
    m_externalDragPressScenePosition = scenePosition;
    m_externalDragScenePosition = scenePosition;
    return true;
}

bool EditorCommandRegistry::SelectionDrag::updateExternalTextDrag(const QPointF &scenePosition)
{
    if (m_externalDragText.isEmpty()) {
        return false;
    }

    m_externalDragScenePosition = scenePosition;
    if (!m_externalDragActive) {
        const qreal distance = (scenePosition
                                - m_externalDragPressScenePosition).manhattanLength();
        if (distance < QGuiApplication::styleHints()->startDragDistance()) {
            return false;
        }
        m_externalDragActive = true;
        m_selectionDragScrollTimer.start();
    }

    updateExternalTextDragPosition(scenePosition, true);
    updateExternalTextDragCursor(m_externalDropPosition >= 0);
    return m_externalDropPosition >= 0;
}

bool EditorCommandRegistry::SelectionDrag::finishExternalTextDrag(const QPointF &scenePosition)
{
    if (m_externalDragText.isEmpty()) {
        return false;
    }

    if (m_externalDragActive) {
        updateExternalTextDragPosition(scenePosition, false);
    }
    const bool wasActive = m_externalDragActive;
    const int dropPosition = m_externalDropPosition;
    const QString text = m_externalDragText;
    resetExternalTextDrag();
    return wasActive && dropPosition >= 0
        && m_registry.insertExternalText(text, dropPosition);
}

void EditorCommandRegistry::SelectionDrag::cancelExternalTextDrag()
{
    resetExternalTextDrag();
}

bool EditorCommandRegistry::SelectionDrag::externalTextDragActive() const
{
    return m_externalDragActive;
}

bool EditorCommandRegistry::SelectionDrag::externalTextDragCursorOverridden() const
{
    return m_externalDragCursorOverridden;
}

int EditorCommandRegistry::SelectionDrag::externalTextDragCursorShape() const
{
    return static_cast<int>(m_externalDragCursorShape);
}

bool EditorCommandRegistry::SelectionDrag::handleSelectionDragEvent(QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const Qt::KeyboardModifiers selectionModifiers = Qt::ShiftModifier
            | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
        if (mouseEvent->button() != Qt::LeftButton
            || mouseEvent->modifiers().testAnyFlags(selectionModifiers)
            || m_editor->property("readOnly").toBool()
            || m_editor->property("inputMethodComposing").toBool()) {
            return false;
        }

        const int selectionStart = m_editor->property("selectionStart").toInt();
        const int selectionEnd = m_editor->property("selectionEnd").toInt();
        const int pressPosition = m_registry.editorPositionAt(mouseEvent->position());
        if (selectionStart >= selectionEnd || pressPosition < selectionStart
            || pressPosition > selectionEnd) {
            return false;
        }

        beginSelectionDrag(selectionStart, selectionEnd, mouseEvent->scenePosition());
        event->accept();
        return true;
    }

    const bool dragPending = m_selectionDragStart >= 0;
    if (!dragPending) {
        return false;
    }

    if (event->type() == QEvent::MouseMove) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (!mouseEvent->buttons().testFlag(Qt::LeftButton)) {
            resetSelectionDrag(true);
            return false;
        }

        m_selectionDragScenePosition = mouseEvent->scenePosition();
        if (!m_selectionDragActive) {
            const qreal distance = (m_selectionDragScenePosition
                                    - m_selectionDragPressScenePosition).manhattanLength();
            const int threshold = QGuiApplication::styleHints()->startDragDistance();
            if (distance < threshold) {
                event->accept();
                return true;
            }
            m_selectionDragActive = true;
            if (QQuickItem *item = m_registry.editorItem()) {
                m_selectionDragOriginalCursor = item->cursor();
            }
            m_selectionDragScrollTimer.start();
        }

        updateSelectionDrag(m_selectionDragScenePosition, true);
        event->accept();
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton) {
            return false;
        }

        const bool movedFarEnough = m_selectionDragActive;
        const int selectionStart = m_selectionDragStart;
        const int selectionEnd = m_selectionDragEnd;
        const int clickPosition = m_registry.editorPositionAt(mouseEvent->position());
        if (movedFarEnough) {
            updateSelectionDrag(mouseEvent->scenePosition(), false);
        }
        const int dropPosition = m_selectionDropPosition;
        resetSelectionDrag(false);

        if (movedFarEnough) {
            if (dropPosition >= 0) {
                m_registry.moveSelection(selectionStart, selectionEnd, dropPosition);
            } else {
                m_registry.selectRange(selectionStart, selectionEnd);
                m_registry.focusEditor();
            }
        } else {
            m_editor->setProperty("cursorPosition", clickPosition);
            m_registry.focusEditor();
        }
        event->accept();
        return true;
    }

    if (event->type() == QEvent::KeyPress) {
        const auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            resetSelectionDrag(true);
            return true;
        }
    }

    if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::FocusOut) {
        resetSelectionDrag(false);
    }
    return false;
}

void EditorCommandRegistry::SelectionDrag::trackNativeSelectionDragEvent(QEvent *event)
{
    if (!event) {
        return;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const Qt::KeyboardModifiers unsupportedModifiers = Qt::ControlModifier
            | Qt::AltModifier | Qt::MetaModifier;
        resetNativeSelectionDrag();
        if (mouseEvent->button() != Qt::LeftButton
            || mouseEvent->modifiers().testAnyFlags(unsupportedModifiers)
            || m_editor->property("readOnly").toBool()
            || m_editor->property("inputMethodComposing").toBool()) {
            return;
        }
        m_nativeSelectionDragPressScenePosition = mouseEvent->scenePosition();
        m_nativeSelectionDragScenePosition = mouseEvent->scenePosition();
        m_nativeSelectionDragModifiers = mouseEvent->modifiers();
        m_nativeSelectionDragPending = true;
        return;
    }

    if (!m_nativeSelectionDragPending) {
        return;
    }
    if (event->type() == QEvent::MouseMove) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (!mouseEvent->buttons().testFlag(Qt::LeftButton)) {
            resetNativeSelectionDrag();
            return;
        }
        m_nativeSelectionDragScenePosition = mouseEvent->scenePosition();
        m_nativeSelectionDragModifiers = mouseEvent->modifiers();
        if (!m_nativeSelectionDragActive) {
            const qreal distance = (m_nativeSelectionDragScenePosition
                                    - m_nativeSelectionDragPressScenePosition)
                                       .manhattanLength();
            if (distance < QGuiApplication::styleHints()->startDragDistance()) {
                return;
            }
            m_nativeSelectionDragActive = true;
            m_selectionDragScrollTimer.start();
        }
        return;
    }

    if (event->type() == QEvent::MouseButtonRelease
        || event->type() == QEvent::UngrabMouse
        || event->type() == QEvent::FocusOut) {
        resetNativeSelectionDrag();
    }
}

int EditorCommandRegistry::SelectionDrag::visibleEditorPositionAt(const QPointF &scenePosition) const
{
    QQuickItem *item = m_registry.editorItem();
    QQuickItem *viewport = m_registry.editorViewport();
    if (!item || !viewport) {
        return -1;
    }

    const QPointF viewportPosition = viewport->mapFromScene(scenePosition);
    const QPointF editorPosition = item->mapFromScene(scenePosition);
    if (!viewport->contains(viewportPosition) || !item->contains(editorPosition)) {
        return -1;
    }
    // 窄窗口中历史面板覆盖在编辑器上方；被面板遮住的 TextEdit 区域
    // 不能作为有效落点，否则在卡片尚未拖出面板时就可能误插入。
    if (m_window && m_window->property("historyPanelOpen").toBool()
        && m_window->property("historyPanelOverlay").toBool()) {
        const QPointF viewportSceneTopLeft = viewport->mapToScene(QPointF());
        const qreal panelRight = viewportSceneTopLeft.x()
            + m_window->property("historyPanelWidth").toReal();
        if (scenePosition.x() < panelRight) {
            return -1;
        }
    }
    return m_registry.editorPositionAt(editorPosition);
}

void EditorCommandRegistry::SelectionDrag::beginSelectionDrag(int selectionStart, int selectionEnd,
                                                const QPointF &scenePosition)
{
    resetNativeSelectionDrag();
    resetExternalTextDrag();
    m_selectionDragStart = selectionStart;
    m_selectionDragEnd = selectionEnd;
    m_selectionDropPosition = -1;
    m_selectionDragPressScenePosition = scenePosition;
    m_selectionDragScenePosition = scenePosition;
    m_selectionDragActive = false;

    m_registry.focusEditor();
    if (QQuickItem *item = m_registry.editorItem()) {
        m_selectionDragPreviousKeepMouseGrab = item->keepMouseGrab();
        item->setKeepMouseGrab(true);
        item->grabMouse();
    }
}

void EditorCommandRegistry::SelectionDrag::updateSelectionDrag(const QPointF &scenePosition,
                                                bool scrollViewport)
{
    if (!m_selectionDragActive || !m_editor) {
        return;
    }

    m_selectionDragScenePosition = scenePosition;
    if (scrollViewport) {
        scrollTextDragViewport(scenePosition);
    }

    QQuickItem *item = m_registry.editorItem();
    if (!item) {
        return;
    }
    const int position = m_registry.editorPositionAt(item->mapFromScene(scenePosition));
    const bool validDrop = position >= 0
        && (position < m_selectionDragStart || position > m_selectionDragEnd);
    m_selectionDropPosition = validDrop ? position : -1;
    m_editor->setProperty("selectionDragPosition", m_selectionDropPosition);
    item->setCursor(QCursor(validDrop ? Qt::DragMoveCursor : Qt::ForbiddenCursor));
}

void EditorCommandRegistry::SelectionDrag::resetSelectionDrag(bool releaseMouseGrab)
{
    m_selectionDragScrollTimer.stop();
    QQuickItem *item = m_registry.editorItem();
    const bool wasActive = m_selectionDragActive;
    const bool previousKeepMouseGrab = m_selectionDragPreviousKeepMouseGrab;
    const QCursor originalCursor = m_selectionDragOriginalCursor;

    m_selectionDragStart = -1;
    m_selectionDragEnd = -1;
    m_selectionDropPosition = -1;
    m_selectionDragActive = false;
    m_selectionDragPreviousKeepMouseGrab = false;

    if (m_editor) {
        m_editor->setProperty("selectionDragPosition", -1);
    }
    if (item) {
        if (wasActive) {
            item->setCursor(originalCursor);
        }
        item->setKeepMouseGrab(previousKeepMouseGrab);
        if (releaseMouseGrab) {
            item->ungrabMouse();
        }
    }
}

void EditorCommandRegistry::SelectionDrag::updateNativeSelectionDrag()
{
    if (!m_nativeSelectionDragActive || !m_editor || !m_window) {
        return;
    }
    if (!scrollTextDragViewport(m_nativeSelectionDragScenePosition)) {
        return;
    }

    QQuickItem *item = m_registry.editorItem();
    if (!item) {
        return;
    }
    // Flickable 移动后，静止指针对应的文档位置已经变化。补发一次移动事件，
    // 让 TextEdit 沿用原生锚点和活动端语义继续扩展选区。
    const QPointF localPosition = item->mapFromScene(m_nativeSelectionDragScenePosition);
    const QPointF globalPosition = item->mapToGlobal(localPosition);
    QMouseEvent moveEvent(QEvent::MouseMove, localPosition,
                          m_nativeSelectionDragScenePosition, globalPosition,
                          Qt::NoButton, Qt::LeftButton,
                          m_nativeSelectionDragModifiers);
    QCoreApplication::sendEvent(m_editor, &moveEvent);
}

void EditorCommandRegistry::SelectionDrag::resetNativeSelectionDrag()
{
    if (m_nativeSelectionDragPending || m_nativeSelectionDragActive) {
        m_selectionDragScrollTimer.stop();
    }
    m_nativeSelectionDragPressScenePosition = {};
    m_nativeSelectionDragScenePosition = {};
    m_nativeSelectionDragModifiers = Qt::NoModifier;
    m_nativeSelectionDragPending = false;
    m_nativeSelectionDragActive = false;
}

void EditorCommandRegistry::SelectionDrag::updateExternalTextDragPosition(
    const QPointF &scenePosition, bool scrollViewport)
{
    if (!m_externalDragActive || !m_editor) {
        return;
    }

    m_externalDragScenePosition = scenePosition;
    if (scrollViewport) {
        scrollTextDragViewport(scenePosition);
    }
    m_externalDropPosition = visibleEditorPositionAt(scenePosition);
    m_editor->setProperty("selectionDragPosition", m_externalDropPosition);
}

void EditorCommandRegistry::SelectionDrag::updateExternalTextDragCursor(bool canDrop)
{
    const Qt::CursorShape shape = canDrop ? Qt::DragCopyCursor : Qt::ForbiddenCursor;
    if (!m_externalDragCursorOverridden) {
        QGuiApplication::setOverrideCursor(QCursor(shape));
        m_externalDragCursorOverridden = true;
    } else if (m_externalDragCursorShape != shape) {
        QGuiApplication::changeOverrideCursor(QCursor(shape));
    }
    m_externalDragCursorShape = shape;
}

void EditorCommandRegistry::SelectionDrag::resetExternalTextDrag()
{
    m_selectionDragScrollTimer.stop();
    if (m_externalDragCursorOverridden) {
        QGuiApplication::restoreOverrideCursor();
    }
    m_externalDragText.clear();
    m_externalDragPressScenePosition = {};
    m_externalDragScenePosition = {};
    m_externalDropPosition = -1;
    m_externalDragActive = false;
    m_externalDragCursorOverridden = false;
    m_externalDragCursorShape = Qt::ArrowCursor;
    if (m_editor) {
        m_editor->setProperty("selectionDragPosition", -1);
    }
}

bool EditorCommandRegistry::SelectionDrag::scrollTextDragViewport(const QPointF &scenePosition)
{
    QQuickItem *viewport = m_registry.editorViewport();
    if (!viewport) {
        return false;
    }

    const QPointF viewportPosition = viewport->mapFromScene(scenePosition);
    const qreal viewportHeight = viewport->height();
    const qreal edge = qMin<qreal>(32.0, viewportHeight / 4.0);
    const qreal currentY = viewport->property("contentY").toReal();
    const qreal maximumY = qMax<qreal>(
        0.0, viewport->property("contentHeight").toReal() - viewportHeight);
    qreal scrollDelta = 0.0;
    if (viewportPosition.x() >= -edge
        && viewportPosition.x() <= viewport->width() + edge) {
        if (viewportPosition.y() < edge) {
            scrollDelta = -qBound<qreal>(
                2.0, (edge - viewportPosition.y()) * 0.5, 24.0);
        } else if (viewportPosition.y() > viewportHeight - edge) {
            scrollDelta = qBound<qreal>(
                2.0, (viewportPosition.y() - viewportHeight + edge) * 0.5, 24.0);
        }
    }
    const qreal requestedY = qBound<qreal>(0.0, currentY + scrollDelta, maximumY);
    if (!qFuzzyCompare(requestedY + 1.0, currentY + 1.0)) {
        viewport->setProperty("contentY", requestedY);
        return true;
    }
    return false;
}
