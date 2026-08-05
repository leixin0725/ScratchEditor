#include "windowplacement.h"

#include <limits>

namespace WindowPlacement {

namespace {

bool usableSize(const QSize &size)
{
    return size.isValid() && size.width() > 0 && size.height() > 0;
}

qint64 intersectionArea(const QRect &first, const QRect &second)
{
    const QRect intersection = first.intersected(second);
    if (intersection.width() <= 0 || intersection.height() <= 0) {
        return 0;
    }
    return static_cast<qint64>(intersection.width()) * intersection.height();
}

bool fits(const QSize &size, const QRect &screen)
{
    return size.width() <= screen.width() && size.height() <= screen.height();
}

// 尺寸阶梯等级：0 = 记忆尺寸可容纳，1 = 默认尺寸可容纳，2 = 需缩小。
int sizeTier(const QSize &rememberedSize, const QSize &defaultSize, const QRect &screen)
{
    if (usableSize(rememberedSize) && fits(rememberedSize, screen)) {
        return 0;
    }
    if (fits(defaultSize, screen)) {
        return 1;
    }
    return 2;
}

QSize shrunkSize(const QSize &base, const QRect &screen, const QSize &minimumSize)
{
    return QSize(qMax(minimumSize.width(), qMin(screen.width(), base.width())),
                 qMax(minimumSize.height(), qMin(screen.height(), base.height())));
}

QRect centeredIn(const QRect &screen, const QSize &size)
{
    QRect centered(QPoint(0, 0), size);
    centered.moveCenter(screen.center());
    if (size.width() <= screen.width()) {
        centered.moveLeft(qBound(screen.left(), centered.left(),
                                 screen.right() - size.width() + 1));
    } else {
        centered.moveLeft(screen.left());
    }
    if (size.height() <= screen.height()) {
        centered.moveTop(qBound(screen.top(), centered.top(),
                                screen.bottom() - size.height() + 1));
    } else {
        centered.moveTop(screen.top());
    }
    return centered;
}

} // namespace

QRect nativeToLogicalRect(const QRect &nativeRect,
                          const QRect &nativeScreen,
                          const QRect &logicalScreen)
{
    if (!nativeRect.isValid() || !nativeScreen.isValid() || !logicalScreen.isValid()) {
        return {};
    }
    const qreal scaleX = static_cast<qreal>(nativeScreen.width()) / logicalScreen.width();
    const qreal scaleY = static_cast<qreal>(nativeScreen.height()) / logicalScreen.height();
    if (scaleX <= 0.0 || scaleY <= 0.0) {
        return {};
    }
    return QRect(
        logicalScreen.x() + qRound((nativeRect.x() - nativeScreen.x()) / scaleX),
        logicalScreen.y() + qRound((nativeRect.y() - nativeScreen.y()) / scaleY),
        qRound(nativeRect.width() / scaleX), qRound(nativeRect.height() / scaleY));
}

std::optional<QRect> fitRestoredGeometry(const QRect &remembered,
                                         const QSize &minimumSize,
                                         const QVector<QRect> &screens)
{
    if (!remembered.isValid() || screens.isEmpty()) {
        return std::nullopt;
    }

    QRect candidate = remembered;
    candidate.setWidth(qMax(minimumSize.width(), remembered.width()));
    candidate.setHeight(qMax(minimumSize.height(), remembered.height()));

    const QRect *bestScreen = nullptr;
    qint64 bestArea = 0;
    for (const QRect &screen : screens) {
        const qint64 area = intersectionArea(candidate, screen);
        if (area > bestArea) {
            bestArea = area;
            bestScreen = &screen;
        }
    }
    if (!bestScreen || bestArea <= 0) {
        return std::nullopt;
    }

    candidate.setWidth(qMax(minimumSize.width(),
                            qMin(bestScreen->width(), candidate.width())));
    candidate.setHeight(qMax(minimumSize.height(),
                             qMin(bestScreen->height(), candidate.height())));
    candidate.moveLeft(qBound(bestScreen->left(), candidate.left(),
                              bestScreen->right() - candidate.width() + 1));
    candidate.moveTop(qBound(bestScreen->top(), candidate.top(),
                             bestScreen->bottom() - candidate.height() + 1));
    return candidate;
}

QRect placeNearWindow(const QSize &rememberedSize,
                      const QSize &defaultSize,
                      const QSize &minimumSize,
                      const QVector<QRect> &screens,
                      const std::optional<QRect> &referenceRect,
                      const QRect &obstacleRect)
{
    if (screens.isEmpty()) {
        return QRect(QPoint(0, 0),
                     usableSize(rememberedSize) ? rememberedSize : defaultSize);
    }

    const QRect *referenceScreen = nullptr;
    if (referenceRect) {
        for (const QRect &screen : screens) {
            if (screen.contains(referenceRect->center())) {
                referenceScreen = &screen;
                break;
            }
        }
    }

    // 目标屏幕：优先参考窗口所在屏幕；否则取尺寸阶梯等级最优的屏幕（平局取主屏）。
    const QRect *targetScreen = referenceScreen;
    if (!targetScreen) {
        int bestTier = std::numeric_limits<int>::max();
        for (const QRect &screen : screens) {
            bestTier = qMin(bestTier, sizeTier(rememberedSize, defaultSize, screen));
        }
        for (const QRect &screen : screens) {
            if (sizeTier(rememberedSize, defaultSize, screen) == bestTier) {
                targetScreen = &screen;
                break;
            }
        }
    }

    const QSize targetSize = [&] {
        if (usableSize(rememberedSize) && fits(rememberedSize, *targetScreen)) {
            return rememberedSize;
        }
        if (fits(defaultSize, *targetScreen)) {
            return defaultSize;
        }
        const QSize base = usableSize(rememberedSize) ? rememberedSize : defaultSize;
        return shrunkSize(base, *targetScreen, minimumSize);
    }();

    // 锚定候选：右侧 → 下方 → 左侧 → 上方，均需完整落在目标屏幕内。
    struct Candidate {
        QRect rect;
        int anchorOrder = 0;
    };
    QVector<Candidate> candidates;
    if (referenceScreen && referenceRect) {
        const QRect &reference = *referenceRect;
        const auto addCandidate = [&](const QRect &rect, int anchorOrder) {
            if (targetScreen->contains(rect)) {
                candidates.append({rect, anchorOrder});
            }
        };
        addCandidate(QRect(reference.x() + reference.width() + AnchorGap,
                           reference.y(), targetSize.width(), targetSize.height()),
                     0);
        addCandidate(QRect(reference.x(),
                           reference.y() + reference.height() + AnchorGap,
                           targetSize.width(), targetSize.height()),
                     1);
        addCandidate(QRect(reference.x() - AnchorGap - targetSize.width(),
                           reference.y(), targetSize.width(), targetSize.height()),
                     2);
        addCandidate(QRect(reference.x(),
                           reference.y() - AnchorGap - targetSize.height(),
                           targetSize.width(), targetSize.height()),
                     3);
    }

    if (candidates.isEmpty()) {
        return centeredIn(*targetScreen, targetSize);
    }

    // 重叠面积最小者优先；平局按锚定顺序（候选已按顺序追加）。
    qint64 bestOverlap = std::numeric_limits<qint64>::max();
    QRect best = candidates.first().rect;
    for (const Candidate &candidate : candidates) {
        const qint64 overlap = obstacleRect.isValid()
            ? intersectionArea(candidate.rect, obstacleRect)
            : 0;
        if (overlap < bestOverlap) {
            bestOverlap = overlap;
            best = candidate.rect;
        }
    }
    return best;
}

} // namespace WindowPlacement
