#include "headingfoldmanager.h"

#include <QDebug>
#include <QMetaObject>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextBlockUserData>
#include <QTextDocument>

#include <algorithm>

namespace {

class HeadingFoldBlockData final : public QTextBlockUserData
{
public:
    bool collapsed = false;
};

int headingLevel(const QString &text, bool insideFence)
{
    if (insideFence) {
        return 0;
    }
    static const QRegularExpression heading(QStringLiteral(R"(^\s{0,3}(#{1,6})\s+.*$)"));
    const QRegularExpressionMatch match = heading.match(text);
    return match.hasMatch() ? match.capturedLength(1) : 0;
}

bool isFenceLine(const QString &text)
{
    qsizetype firstContent = 0;
    while (firstContent < text.size() && text.at(firstContent).isSpace()) {
        ++firstContent;
    }
    return firstContent + 2 < text.size()
        && text.at(firstContent) == QLatin1Char('`')
        && text.at(firstContent + 1) == QLatin1Char('`')
        && text.at(firstContent + 2) == QLatin1Char('`');
}

HeadingFoldBlockData *foldData(const QTextBlock &block)
{
    return dynamic_cast<HeadingFoldBlockData *>(block.userData());
}

HeadingFoldBlockData *ensureFoldData(QTextBlock block)
{
    if (HeadingFoldBlockData *data = foldData(block)) {
        return data;
    }
    auto *data = new HeadingFoldBlockData;
    block.setUserData(data);
    return data;
}

} // namespace

HeadingFoldManager::HeadingFoldManager(QObject *parent)
    : QObject(parent)
{
}

void HeadingFoldManager::setEditor(QObject *editor, QTextDocument *document)
{
    if (m_document) {
        QObject::disconnect(m_document.data(), nullptr, this, nullptr);
        makeAllBlocksVisible();
    }
    m_editor = editor;
    m_document = document;
    m_headings.clear();
    m_markers.clear();
    m_visibleEndPosition = 0;
    m_rebuildQueued = false;
    m_renderInvalidationWarningIssued = false;
    if (m_document) {
        connect(m_document.data(), &QTextDocument::contentsChanged,
                this, &HeadingFoldManager::scheduleRebuild);
        rebuild();
    } else {
        emit markersChanged();
    }
}

void HeadingFoldManager::reset()
{
    if (!m_document) {
        return;
    }
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        if (HeadingFoldBlockData *data = foldData(block)) {
            data->collapsed = false;
        }
    }
    makeAllBlocksVisible();
    rebuild();
}

QVariantList HeadingFoldManager::markers() const
{
    return m_markers;
}

int HeadingFoldManager::visibleEndPosition() const
{
    return m_visibleEndPosition;
}

QVariantMap HeadingFoldManager::diagnostics() const
{
    QVariantList visibleTexts;
    QVariantList visiblePositions;
    int visibleCount = 0;
    if (m_document) {
        for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
            if (!block.isVisible()) {
                continue;
            }
            ++visibleCount;
            visibleTexts.append(block.text());
            visiblePositions.append(block.position());
        }
    }

    int collapsedCount = 0;
    for (const Heading &heading : m_headings) {
        if (heading.collapsed) {
            ++collapsedCount;
        }
    }
    return {
        {QStringLiteral("headingCount"), m_headings.size()},
        {QStringLiteral("collapsedHeadingCount"), collapsedCount},
        {QStringLiteral("visibleBlockCount"), visibleCount},
        {QStringLiteral("visibleBlockTexts"), visibleTexts},
        {QStringLiteral("visibleBlockPositions"), visiblePositions},
        {QStringLiteral("markers"), m_markers},
    };
}

bool HeadingFoldManager::foldAll()
{
    rebuild();
    for (Heading &heading : m_headings) {
        HeadingFoldBlockData *data = ensureFoldData(heading.block);
        data->collapsed = heading.foldable;
        heading.collapsed = data->collapsed;
    }
    applyVisibility();
    return true;
}

bool HeadingFoldManager::unfoldAll()
{
    rebuild();
    for (Heading &heading : m_headings) {
        if (HeadingFoldBlockData *data = foldData(heading.block)) {
            data->collapsed = false;
        }
        heading.collapsed = false;
    }
    applyVisibility();
    return true;
}

bool HeadingFoldManager::foldCurrent()
{
    rebuild();
    const int index = currentHeadingIndex();
    if (index < 0 || !m_headings.at(index).foldable) {
        return true;
    }
    const Heading &heading = m_headings.at(index);
    if (selectionIntersects(heading.block.next().position(), heading.endPosition)) {
        moveCursorTo(heading.position);
    }
    setCollapsed(index, true);
    return true;
}

bool HeadingFoldManager::unfoldCurrent()
{
    rebuild();
    const int index = currentHeadingIndex();
    if (index >= 0) {
        setCollapsed(index, false);
    }
    return true;
}

bool HeadingFoldManager::navigate(bool backwards)
{
    rebuild();
    if (!m_editor || m_headings.isEmpty()) {
        return true;
    }

    const int cursorPosition = m_editor->property("cursorPosition").toInt();
    const QTextBlock cursorBlock = m_document->findBlock(cursorPosition);
    const int blockPosition = cursorBlock.isValid() ? cursorBlock.position() : cursorPosition;
    int target = -1;
    if (backwards) {
        for (int index = m_headings.size() - 1; index >= 0; --index) {
            if (m_headings.at(index).position < blockPosition) {
                target = index;
                break;
            }
        }
    } else {
        for (int index = 0; index < m_headings.size(); ++index) {
            if (m_headings.at(index).position > blockPosition) {
                target = index;
                break;
            }
        }
    }
    if (target >= 0) {
        revealHeading(target);
        moveCursorTo(m_headings.at(target).position);
    }
    return true;
}

bool HeadingFoldManager::toggleAt(int headingPosition)
{
    rebuild();
    const int index = headingIndexAt(headingPosition);
    if (index < 0 || !m_headings.at(index).foldable) {
        return false;
    }
    const Heading &heading = m_headings.at(index);
    if (!heading.collapsed
        && selectionIntersects(heading.block.next().position(), heading.endPosition)) {
        moveCursorTo(heading.position);
    }
    return setCollapsed(index, !heading.collapsed);
}

bool HeadingFoldManager::revealPosition(int position)
{
    rebuild();
    if (!m_document || m_document->findBlock(position).isVisible()) {
        return false;
    }
    int target = -1;
    for (int index = 0; index < m_headings.size(); ++index) {
        if (m_headings.at(index).position > position) {
            break;
        }
        target = index;
    }
    if (target < 0) {
        return false;
    }
    bool changed = revealHeading(target);
    Heading &owner = m_headings[target];
    if (owner.collapsed && position >= owner.block.position() + owner.block.length()) {
        ensureFoldData(owner.block)->collapsed = false;
        owner.collapsed = false;
        changed = true;
        applyVisibility();
    }
    return changed;
}

void HeadingFoldManager::scheduleRebuild()
{
    if (m_applyingVisibility || m_rebuildQueued) {
        return;
    }
    m_rebuildQueued = true;
    QMetaObject::invokeMethod(this, [this] {
        if (!m_rebuildQueued) {
            return;
        }
        m_rebuildQueued = false;
        rebuild();
    }, Qt::QueuedConnection);
}

void HeadingFoldManager::rebuild()
{
    if (!m_document) {
        return;
    }

    m_rebuildQueued = false;
    QVector<Heading> headings;
    bool insideFence = false;
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        const QString text = block.text();
        const bool fenceLine = isFenceLine(text);
        const int level = headingLevel(text, insideFence || fenceLine);
        if (level > 0) {
            HeadingFoldBlockData *data = ensureFoldData(block);
            headings.append({block, block.position(), level, m_document->characterCount() - 1,
                             false, data->collapsed});
        } else if (HeadingFoldBlockData *data = foldData(block)) {
            Q_UNUSED(data);
            block.setUserData(nullptr);
        }
        if (fenceLine) {
            insideFence = !insideFence;
        }
    }

    // 每个标题只进出栈一次，在线性时间内找到首个同级或更高级标题。
    QVector<int> openHeadings;
    for (int index = 0; index < headings.size(); ++index) {
        while (!openHeadings.isEmpty()
               && headings.at(openHeadings.back()).level >= headings.at(index).level) {
            headings[openHeadings.takeLast()].endPosition = headings.at(index).position;
        }
        openHeadings.append(index);
    }

    for (Heading &heading : headings) {
        const int boundary = heading.endPosition;
        const QTextBlock contentBlock = heading.block.next();
        heading.foldable = contentBlock.isValid() && contentBlock.position() < boundary;
        if (!heading.foldable && heading.collapsed) {
            if (HeadingFoldBlockData *data = foldData(heading.block)) {
                data->collapsed = false;
            }
            heading.collapsed = false;
        }
    }
    m_headings = std::move(headings);
    applyVisibility();
}

void HeadingFoldManager::makeAllBlocksVisible()
{
    if (!m_document) {
        return;
    }
    bool visibilityChanged = false;
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        if (!block.isVisible()) {
            block.setVisible(true);
            visibilityChanged = true;
        }
    }
    if (visibilityChanged) {
        m_document->markContentsDirty(0, m_document->characterCount());
        invalidateEditorRendering();
    }
}

void HeadingFoldManager::applyVisibility()
{
    if (!m_document || m_applyingVisibility) {
        return;
    }
    m_applyingVisibility = true;
    int collapsedLevel = 0;
    int headingIndex = 0;
    int visibleEndPosition = 0;
    bool visibilityChanged = false;
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        Heading *heading = nullptr;
        if (headingIndex < m_headings.size()
            && m_headings.at(headingIndex).position == block.position()) {
            heading = &m_headings[headingIndex++];
        }
        if (heading && collapsedLevel > 0 && heading->level <= collapsedLevel) {
            collapsedLevel = 0;
        }
        const bool visible = collapsedLevel == 0;
        if (block.isVisible() != visible) {
            block.setVisible(visible);
            visibilityChanged = true;
        }
        if (visible) {
            visibleEndPosition = block.position() + qMax(0, block.length() - 1);
        }
        if (visible && heading && heading->collapsed) {
            collapsedLevel = heading->level;
        }
    }
    if (visibilityChanged) {
        m_document->markContentsDirty(0, m_document->characterCount());
        invalidateEditorRendering();
    }
    m_applyingVisibility = false;
    ensureCursorVisible();
    const bool visibleEndChanged = m_visibleEndPosition != visibleEndPosition;
    m_visibleEndPosition = visibleEndPosition;
    const QVariantList previousMarkers = m_markers;
    updateMarkers();
    if (visibleEndChanged && previousMarkers == m_markers) {
        emit markersChanged();
    }
}

bool HeadingFoldManager::invalidateEditorRendering()
{
    if (!m_editor) {
        return false;
    }

    // QTextBlock 可见性不会触发 QQuickTextEdit 的 contentsChange 或 geometryChange
    // 路径。Qt 6.10.2 需要同时刷新文档尺寸和全部文本节点；通过元对象调用可
    // 将固定版本的私有实现依赖限制在这里，避免引入 QuickPrivate 构建依赖。
    constexpr const char *slotSignatures[] = {
        "q_invalidate()", "updateSize()", "updateWholeDocument()"
    };
    for (const char *signature : slotSignatures) {
        if (m_editor->metaObject()->indexOfSlot(signature) >= 0) {
            continue;
        }
        if (!m_renderInvalidationWarningIssued) {
            qWarning() << "Heading folding could not invalidate the Qt Quick text renderer";
            m_renderInvalidationWarningIssued = true;
        }
        return false;
    }

    const bool invalidated = QMetaObject::invokeMethod(
        m_editor, "q_invalidate", Qt::DirectConnection);
    const bool sizeUpdated = QMetaObject::invokeMethod(
        m_editor, "updateSize", Qt::DirectConnection);
    const bool documentUpdated = QMetaObject::invokeMethod(
        m_editor, "updateWholeDocument", Qt::DirectConnection);
    if (invalidated && sizeUpdated && documentUpdated) {
        return true;
    }
    if (!m_renderInvalidationWarningIssued) {
        qWarning() << "Heading folding could not invalidate the Qt Quick text renderer";
        m_renderInvalidationWarningIssued = true;
    }
    return false;
}

void HeadingFoldManager::ensureCursorVisible()
{
    if (!m_editor || !m_document) {
        return;
    }
    const int cursorPosition = m_editor->property("cursorPosition").toInt();
    const QTextBlock cursorBlock = m_document->findBlock(cursorPosition);
    if (!cursorBlock.isValid() || cursorBlock.isVisible()) {
        return;
    }
    for (auto iterator = m_headings.crbegin(); iterator != m_headings.crend(); ++iterator) {
        if (iterator->position <= cursorPosition && iterator->block.isVisible()) {
            moveCursorTo(iterator->position);
            return;
        }
    }
}

void HeadingFoldManager::updateMarkers()
{
    QVariantList markers;
    for (const Heading &heading : m_headings) {
        if (!heading.foldable || !heading.block.isVisible()) {
            continue;
        }
        markers.append(QVariantMap{
            {QStringLiteral("position"), heading.position},
            {QStringLiteral("level"), heading.level},
            {QStringLiteral("collapsed"), heading.collapsed},
        });
    }
    if (markers != m_markers) {
        m_markers = std::move(markers);
        emit markersChanged();
    }
}

int HeadingFoldManager::currentHeadingIndex() const
{
    if (!m_editor || !m_document) {
        return -1;
    }
    const QTextBlock cursorBlock = m_document->findBlock(
        m_editor->property("cursorPosition").toInt());
    if (!cursorBlock.isValid()) {
        return -1;
    }
    int result = -1;
    for (int index = 0; index < m_headings.size(); ++index) {
        if (m_headings.at(index).position > cursorBlock.position()) {
            break;
        }
        result = index;
    }
    return result;
}

int HeadingFoldManager::headingIndexAt(int position) const
{
    for (int index = 0; index < m_headings.size(); ++index) {
        if (m_headings.at(index).position == position) {
            return index;
        }
    }
    return -1;
}

bool HeadingFoldManager::setCollapsed(int index, bool collapsed)
{
    if (index < 0 || index >= m_headings.size()) {
        return false;
    }
    Heading &heading = m_headings[index];
    collapsed = collapsed && heading.foldable;
    HeadingFoldBlockData *data = ensureFoldData(heading.block);
    const bool changed = data->collapsed != collapsed;
    data->collapsed = collapsed;
    heading.collapsed = collapsed;
    applyVisibility();
    return changed || !collapsed;
}

bool HeadingFoldManager::revealHeading(int index)
{
    if (index < 0 || index >= m_headings.size()) {
        return false;
    }
    QVector<int> ancestors;
    for (int candidate = 0; candidate < index; ++candidate) {
        while (!ancestors.isEmpty()
               && m_headings.at(ancestors.back()).level >= m_headings.at(candidate).level) {
            ancestors.pop_back();
        }
        ancestors.append(candidate);
    }
    while (!ancestors.isEmpty()
           && m_headings.at(ancestors.back()).level >= m_headings.at(index).level) {
        ancestors.pop_back();
    }

    bool changed = false;
    for (int ancestor : ancestors) {
        Heading &heading = m_headings[ancestor];
        if (!heading.collapsed) {
            continue;
        }
        ensureFoldData(heading.block)->collapsed = false;
        heading.collapsed = false;
        changed = true;
    }
    if (changed) {
        applyVisibility();
    }
    return changed;
}

void HeadingFoldManager::moveCursorTo(int position)
{
    if (!m_editor) {
        return;
    }
    m_editor->setProperty("cursorPosition", position);
    QMetaObject::invokeMethod(m_editor, "deselect");
    QMetaObject::invokeMethod(m_editor, "forceActiveFocus");
}

bool HeadingFoldManager::selectionIntersects(int start, int end) const
{
    if (!m_editor || start < 0 || start >= end) {
        return false;
    }
    const int selectionStart = m_editor->property("selectionStart").toInt();
    const int selectionEnd = m_editor->property("selectionEnd").toInt();
    const int cursor = m_editor->property("cursorPosition").toInt();
    return (selectionStart < end && selectionEnd > start)
        || (cursor >= start && cursor < end);
}
