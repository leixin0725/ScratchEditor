#include "inputautoscroll.h"
#include "editorviewport.h"

#include <QQuickItem>
#include <QQuickWindow>
#include <QTextDocument>
#include <QTimer>

#include <utility>

InputAutoScroll::InputAutoScroll(const QPointer<QObject> &editor,
                                 const QPointer<QTextDocument> &document,
                                 const QPointer<QQuickWindow> &window)
    : m_editor(editor), m_document(document), m_window(window)
{
}

void InputAutoScroll::beginTracking(const QString &kind)
{
    if (!m_editor || !m_document) {
        return;
    }
    if (!m_inputAutoScrollTrackingActive) {
        m_inputAutoScrollTrackingActive = true;
        const bool releasePending = m_window
            && m_window->property("releaseInputScrollHoldAfterAnimation").toBool();
        // 正在随输入滚动动画等待释放的 hold 属于上一轮临时状态；新编辑
        // 打断动画后不应把它误认成需要永久保留的既有缓冲。
        m_inputScrollHoldWasActive = m_window
            && m_window->property("inputScrollHoldBottom").toBool()
            && !releasePending;
    }
    // 必须在文本实际变化前扩大 Flickable 的有效内容范围；否则内容高度
    // 一旦收缩，contentY 会先被钳到新的 max，延迟检查再恢复时便产生抽动。
    // QML 入口同时停止尚未结束的输入滚动动画并停在当前帧。
    if (!m_window
        || !QMetaObject::invokeMethod(m_window, "prepareInputScrollTracking")) {
        if (m_window) {
            m_window->setProperty("inputScrollHoldBottom", true);
        }
    }
    ++m_inputScrollDiag.inputCount;
    m_inputScrollDiag.lastKind = kind;
    if (QQuickItem *viewport = EditorViewport::find(m_editor)) {
        m_inputPreScrollY = viewport->property("contentY").toReal();
    } else {
        m_inputPreScrollY = 0.0;
    }
    m_inputPreTextLength = m_document->characterCount() - 1;
    InputScrollEvent event;
    event.type = QStringLiteral("input");
    event.kind = kind;
    event.preY = m_inputPreScrollY;
    event.preLen = m_inputPreTextLength;
    recordInputScrollEvent(event);
}

void InputAutoScroll::queueCheck()
{
    if (m_inputAutoScrollCheckQueued) {
        return;
    }
    m_inputAutoScrollCheckQueued = true;
    // 延迟到文档/布局落定后再判定：真实输入（IME/回车）会在事件处理期间
    // 产生瞬态光标矩形与重复 contentsChanged，立即检查会读到过期几何
    // （误判触底/触顶）。该延迟足够这些瞬态收尾。
    QTimer::singleShot(EditorViewport::layoutSettleDelayMs, this, [this] {
        m_inputAutoScrollCheckQueued = false;
        checkAfterLayout();
    });
}

void InputAutoScroll::checkAfterLayout()
{
    ++m_inputScrollDiag.checkCount;
    m_inputScrollDiag.overrideDetected = false;
    m_inputScrollDiag.didScroll = false;
    InputScrollEvent event;
    event.type = QStringLiteral("check");
    event.kind = m_inputScrollDiag.lastKind;
    event.preLen = m_inputPreTextLength;
    if (!m_editor || !m_document) {
        return;
    }
    const int documentLength = m_document->characterCount() - 1;
    m_inputScrollDiag.docLength = documentLength;
    m_inputScrollDiag.preLength = m_inputPreTextLength;
    event.docLen = documentLength;
    if (documentLength == m_inputPreTextLength) {
        // 本次编辑没有改变文本（如被 IME 组合状态拦截、空撤销/删除），
        // 不触发滚动。
        if (m_window) {
            m_window->setProperty("inputScrollHoldBottom", m_inputScrollHoldWasActive);
        }
        m_inputAutoScrollTrackingActive = false;
        m_inputScrollHoldWasActive = false;
        m_inputPreTextLength = -1;
        ++m_inputScrollEarlyReturnCount;
        event.earlyReturn = true;
        recordInputScrollEvent(event);
        return;
    }
    const int cursorPosition = m_editor->property("cursorPosition").toInt();
    m_inputScrollDiag.cursorPosition = cursorPosition;
    event.cursorPos = cursorPosition;
    QRectF cursorRect;
    const bool rectLocated = QMetaObject::invokeMethod(
        m_editor, "positionToRectangle", Qt::DirectConnection,
        Q_RETURN_ARG(QRectF, cursorRect), Q_ARG(int, cursorPosition));
    m_inputScrollDiag.cursorTop = rectLocated ? cursorRect.y() : -1.0;
    m_inputScrollDiag.cursorBottom = rectLocated
        ? cursorRect.y() + cursorRect.height() : -1.0;
    event.curBottom = m_inputScrollDiag.cursorBottom;
    bool didScroll = false;
    if (QQuickItem *viewport = EditorViewport::find(m_editor)) {
        const qreal viewportHeight = viewport->height();
        const qreal currentY = viewport->property("contentY").toReal();
        // 预保持会临时扩展 viewport.contentHeight；所有边界判断必须使用
        // 不含保持缓冲的自然内容高度，否则会把临时范围误当成真实滚动范围。
        const qreal naturalContentHeight = m_window
                && m_window->property("inputScrollNaturalContentHeight").isValid()
            ? m_window->property("inputScrollNaturalContentHeight").toReal()
            : viewport->property("contentHeight").toReal();
        const qreal maximumY = qMax<qreal>(0.0, naturalContentHeight - viewportHeight);
        m_inputScrollDiag.currentY = currentY;
        m_inputScrollDiag.viewportHeight = viewportHeight;
        m_inputScrollDiag.maxY = maximumY;
        event.curY = currentY;
        event.vh = viewportHeight;
        event.maxY = maximumY;
        const bool atDocumentStart = cursorPosition == 0;
        const bool atDocumentEnd = cursorPosition == documentLength;
        // 与底边判定保持同一坐标约定（不叠加 editorItem 的 y 偏移），
        // 锚定目标时再统一加上该偏移。
        const bool cursorTouchesBottomEdge = rectLocated
            && cursorRect.y() + cursorRect.height() >= currentY + viewportHeight - 0.5;
        const bool cursorTouchesTopEdge = rectLocated
            && cursorRect.y() <= currentY + 0.5;
        m_inputScrollDiag.atStart = atDocumentStart;
        m_inputScrollDiag.atEnd = atDocumentEnd;
        m_inputScrollDiag.touchedTop = cursorTouchesTopEdge;
        m_inputScrollDiag.touchedBottom = cursorTouchesBottomEdge;
        event.atStart = atDocumentStart;
        event.atEnd = atDocumentEnd;
        event.touchedTop = cursorTouchesTopEdge;
        event.touchedBottom = cursorTouchesBottomEdge;
        bool triggered = false;
        // 间歇式自动滚动：只在光标碰到/越过视口底边时触发一次，
        // 触发后不再重复，光标随输入自然下落，下次触底再触发。
        // - 段中触底：光标行滚到视口上 1/3；
        // - 段尾触底：等效于滚到底（PageDown 到底，正文末尾下方 2/3 页
        //   留白翻出，光标停在上 1/3）。
        if (cursorTouchesBottomEdge) {
            triggered = true;
            ++m_inputScrollDiag.triggerCount;
            qreal targetY = currentY;
            if (atDocumentEnd && rectLocated) {
                targetY = maximumY;
            } else if (rectLocated) {
                QQuickItem *item = qobject_cast<QQuickItem *>(m_editor.data());
                const qreal editorY = item ? item->y() : 0.0;
                targetY = qBound<qreal>(
                    0.0, editorY + cursorRect.y() - viewportHeight / 3.0, maximumY);
            }
            if (!qFuzzyCompare(targetY + 1.0, currentY + 1.0)) {
                EditorViewport::animateScrollTo(m_window, viewport, targetY, true);
            } else if (m_window) {
                m_window->setProperty("inputScrollHoldBottom", false);
            }
            m_inputScrollDiag.targetY = targetY;
            event.targetY = targetY;
            // 本次检查是否实际移动了视口（最小可见跟随可能已先行滚动到位）。
            didScroll = !qFuzzyCompare(targetY + 1.0, currentY + 1.0);
            m_inputScrollDiag.didScroll = didScroll;
            event.didScroll = didScroll;
        } else if (cursorTouchesTopEdge) {
            // 删除触顶：只在光标碰到/越过视口顶边时触发一次。通常让光标行
            // 落到视口距顶 2/3 处（下 1/3）；若该目标距文档顶部不足 1/3 屏，
            // 则直接滚到顶部。继续删除再次触顶才再次触发。
            triggered = true;
            ++m_inputScrollDiag.triggerCount;
            qreal targetY = currentY;
            if (atDocumentStart && rectLocated) {
                targetY = 0.0;
            } else if (rectLocated) {
                QQuickItem *item = qobject_cast<QQuickItem *>(m_editor.data());
                const qreal editorY = item ? item->y() : 0.0;
                targetY = qBound<qreal>(
                    0.0, editorY + cursorRect.y() - viewportHeight * 2.0 / 3.0,
                    maximumY);
                if (targetY < viewportHeight / 3.0) {
                    targetY = 0.0;
                }
            }
            if (!qFuzzyCompare(targetY + 1.0, currentY + 1.0)) {
                EditorViewport::animateScrollTo(m_window, viewport, targetY, true);
            } else if (m_window) {
                m_window->setProperty("inputScrollHoldBottom", false);
            }
            m_inputScrollDiag.targetY = targetY;
            event.targetY = targetY;
            didScroll = !qFuzzyCompare(targetY + 1.0, currentY + 1.0);
            m_inputScrollDiag.didScroll = didScroll;
            event.didScroll = didScroll;
        }
        if (triggered) {
            // 触发后采样落定位置，检测是否被后续逻辑（如延迟的光标跟随）覆盖；
            // 动画开启时顺延到动画结束后再采样，避免把动画中间态误判为覆盖。
            const int settleDelayMs = 50
                + (m_window ? m_window->property("scrollAnimationDurationMs").toInt() : 0);
            QTimer::singleShot(settleDelayMs, this, [this] {
                if (QQuickItem *vp = EditorViewport::find(m_editor)) {
                    m_inputScrollDiag.settleY = vp->property("contentY").toReal();
                    InputScrollEvent settleEvent;
                    settleEvent.type = QStringLiteral("settle");
                    settleEvent.kind = m_inputScrollDiag.lastKind;
                    settleEvent.settleY = m_inputScrollDiag.settleY;
                    settleEvent.targetY = m_inputScrollDiag.targetY;
                    settleEvent.didScroll = m_inputScrollDiag.didScroll;
                    if (m_inputScrollDiag.didScroll
                        && !qFuzzyCompare(m_inputScrollDiag.settleY + 1.0,
                                          m_inputScrollDiag.targetY + 1.0)) {
                        ++m_inputScrollDiag.overrideCount;
                        m_inputScrollDiag.overrideDetected = true;
                        settleEvent.curY = m_inputScrollDiag.settleY;
                    }
                    recordInputScrollEvent(settleEvent);
                }
            });
        } else if (documentLength < m_inputPreTextLength
                   && currentY >= maximumY - 0.5
                   && m_inputPreScrollY > maximumY + 0.5) {
            // 删除/撤销/重做使内容收缩时，Flickable 会把视口自动下钳制到新的最大位置，
            // 导致末尾光标始终停在上 1/3、永远不触顶（镜像触发失效）。
            // 这里恢复输入前的视口位置（保持不动），让光标随删除自然上移，
            // 越过顶边时再由顶规则间歇触发。撤销/重做视为删除类编辑，
            // 与删除共用同一套保持与顶镜像规则。
            // 弹性底部缓冲已在编辑前开启，contentY 从未离开输入前位置；
            // 保持该缓冲，直到光标触顶或用户主动滚动。
            m_inputScrollDiag.heldY = m_inputPreScrollY;
            event.heldY = m_inputPreScrollY;
        } else if (m_window) {
            // 本次编辑不需要把自然边界之外的视口保持下来；恢复进入本轮
            // 跟踪前的状态，避免临时缓冲泄漏到后续普通滚动。
            m_window->setProperty("inputScrollHoldBottom", m_inputScrollHoldWasActive);
        }
    } else if (m_window) {
        m_window->setProperty("inputScrollHoldBottom", m_inputScrollHoldWasActive);
    }
    m_inputAutoScrollTrackingActive = false;
    m_inputScrollHoldWasActive = false;
    m_inputPreTextLength = -1;
    recordInputScrollEvent(event);
}

void InputAutoScroll::recordInputScrollEvent(InputScrollEvent event)
{
    event.seq = ++m_inputScrollEventSeq;
    if (m_inputScrollEvents.size() >= 64) {
        m_inputScrollEvents.removeFirst();
    }
    m_inputScrollEvents.append(std::move(event));
}

QVariantMap InputAutoScroll::diagnostics() const
{
    const InputScrollDiagnostics &d = m_inputScrollDiag;
    QVariantMap result{
        {QStringLiteral("LastKind"), d.lastKind},
        {QStringLiteral("InputCount"), static_cast<qint64>(d.inputCount)},
        {QStringLiteral("CheckCount"), static_cast<qint64>(d.checkCount)},
        {QStringLiteral("TriggerCount"), static_cast<qint64>(d.triggerCount)},
        {QStringLiteral("OverrideCount"), static_cast<qint64>(d.overrideCount)},
        {QStringLiteral("EarlyReturnCount"),
         static_cast<qint64>(m_inputScrollEarlyReturnCount)},
        {QStringLiteral("CheckQueued"), m_inputAutoScrollCheckQueued},
        {QStringLiteral("CurrentY"), d.currentY},
        {QStringLiteral("CursorTop"), d.cursorTop},
        {QStringLiteral("CursorBottom"), d.cursorBottom},
        {QStringLiteral("ViewportHeight"), d.viewportHeight},
        {QStringLiteral("MaxY"), d.maxY},
        {QStringLiteral("TargetY"), d.targetY},
        {QStringLiteral("SettleY"), d.settleY},
        {QStringLiteral("HeldY"), d.heldY},
        {QStringLiteral("PreLength"), d.preLength},
        {QStringLiteral("DocLength"), d.docLength},
        {QStringLiteral("CursorPosition"), d.cursorPosition},
        {QStringLiteral("AtStart"), d.atStart},
        {QStringLiteral("AtEnd"), d.atEnd},
        {QStringLiteral("TouchedTop"), d.touchedTop},
        {QStringLiteral("TouchedBottom"), d.touchedBottom},
        {QStringLiteral("DidScroll"), d.didScroll},
        {QStringLiteral("OverrideDetected"), d.overrideDetected},
    };
    QVariantList history;
    history.reserve(m_inputScrollEvents.size());
    for (const InputScrollEvent &item : m_inputScrollEvents) {
        history.append(QVariantMap{
            {QStringLiteral("seq"), static_cast<qint64>(item.seq)},
            {QStringLiteral("type"), item.type},
            {QStringLiteral("kind"), item.kind},
            {QStringLiteral("curY"), item.curY},
            {QStringLiteral("curBottom"), item.curBottom},
            {QStringLiteral("vh"), item.vh},
            {QStringLiteral("maxY"), item.maxY},
            {QStringLiteral("targetY"), item.targetY},
            {QStringLiteral("settleY"), item.settleY},
            {QStringLiteral("heldY"), item.heldY},
            {QStringLiteral("preLen"), item.preLen},
            {QStringLiteral("docLen"), item.docLen},
            {QStringLiteral("cursorPos"), item.cursorPos},
            {QStringLiteral("atStart"), item.atStart},
            {QStringLiteral("atEnd"), item.atEnd},
            {QStringLiteral("touchedTop"), item.touchedTop},
            {QStringLiteral("touchedBottom"), item.touchedBottom},
            {QStringLiteral("didScroll"), item.didScroll},
            {QStringLiteral("earlyReturn"), item.earlyReturn},
        });
    }
    result.insert(QStringLiteral("History"), history);
    return result;
}
