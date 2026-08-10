#pragma once

#include <QRect>
#include <QSize>
#include <QVector>

#include <optional>

namespace WindowPlacement {

// 参考窗口与编辑器窗口之间的锚定间距（逻辑像素）。
// 应用运行时会从 config/ui.json 的 placement.anchorGap 传入实际值，
// 此常量仅作为算法默认值与单元测试基线。
inline constexpr int AnchorGap = 16;

// 把 Windows 原生物理像素矩形映射到 Qt 的多屏逻辑坐标。屏幕全局原点
// 保持不变，只缩放相对于该屏幕原点的偏移与尺寸。
QRect nativeToLogicalRect(const QRect &nativeRect,
                          const QRect &nativeScreen,
                          const QRect &logicalScreen);

// 把记忆的几何恢复到当前屏幕布局：按最大交集选屏并完整钳入其可用区域，
// 尺寸超过屏幕时缩小（不低于 minimumSize）。与所有屏幕都无交集时返回无效。
std::optional<QRect> fitRestoredGeometry(const QRect &remembered,
                                         const QSize &minimumSize,
                                         const QVector<QRect> &screens);

// 在参考窗口（唤起者/焦点窗口）附近摆放窗口，按以下优先级：
//   1. 完整落在单个屏幕内（不跨屏、不溢出）；
//   2. 尺寸阶梯：记忆尺寸 → 默认尺寸 → 缩小到不小于最小尺寸 → 最小尺寸视为合法；
//   3. 锚定顺序：右侧 → 下方 → 左侧 → 上方，间距 AnchorGap；
//   4. 在满足前序条件的候选中，优先与 obstacleRect 重叠面积最小者。
// 无参考窗口或没有锚定候选时，在目标屏幕可用区域内居中（必要时允许最小尺寸溢出）。
QRect placeNearWindow(const QSize &rememberedSize,
                      const QSize &defaultSize,
                      const QSize &minimumSize,
                      const QVector<QRect> &screens,
                      const std::optional<QRect> &referenceRect,
                      const QRect &obstacleRect,
                      int anchorGap = AnchorGap);

} // namespace WindowPlacement
