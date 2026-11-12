#pragma once

#include <QtGlobal>

class QObject;
class QQuickItem;
class QQuickWindow;

namespace EditorViewport {

// 文档/布局瞬态落定等待：真实输入与标题折叠展开都会产生延迟到下一帧 polish
// 才落定的几何，立即读取会得到过期值；输入自动滚动与标题跳转滚动共用此约定。
constexpr int layoutSettleDelayMs = 40;

QQuickItem *find(QObject *editor);
void animateScrollTo(QQuickWindow *window, QQuickItem *viewport, qreal targetY,
                     bool releaseInputHold = false);

} // namespace EditorViewport
