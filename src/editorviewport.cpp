#include "editorviewport.h"

#include <QQuickItem>
#include <QQuickWindow>

QQuickItem *EditorViewport::find(QObject *editor)
{
    QQuickItem *item = qobject_cast<QQuickItem *>(editor);
    for (QQuickItem *candidate = item ? item->parentItem() : nullptr;
         candidate; candidate = candidate->parentItem()) {
        if (candidate->property("contentY").isValid()
            && candidate->property("contentHeight").isValid()) {
            return candidate;
        }
    }
    return nullptr;
}

void EditorViewport::animateScrollTo(QQuickWindow *window, QQuickItem *viewport, qreal targetY,
                                     bool releaseInputHold)
{
    // 优先走 QML 侧轻量动画（动画开关关闭时直接落位）；
    // 若 QML 函数不可用（异常场景）则回退为瞬时滚动，保持原有行为。
    if (!window) {
        viewport->setProperty("contentY", targetY);
        return;
    }
    window->setProperty("requestedScrollY", targetY);
    window->setProperty("releaseInputScrollHoldAfterAnimation", releaseInputHold);
    if (!QMetaObject::invokeMethod(window, "animateScrollTo")) {
        viewport->setProperty("contentY", targetY);
        if (releaseInputHold) {
            window->setProperty("inputScrollHoldBottom", false);
            window->setProperty("releaseInputScrollHoldAfterAnimation", false);
        }
    }
}
