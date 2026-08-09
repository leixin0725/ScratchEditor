#include "clipboardhistorymodel.h"

#include <QUuid>

#include <algorithm>

ClipboardHistoryModel::ClipboardHistoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ClipboardHistoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visibleIndices.size();
}

QVariant ClipboardHistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleIndices.size()) {
        return {};
    }
    const ClipboardHistoryItem &item = m_items.at(m_visibleIndices.at(index.row()));
    switch (role) {
    case HistoryIdRole:
        return item.id;
    case PreviewTextRole:
        return previewFor(item.text);
    case CapturedAtMsRole:
        return item.capturedAtUtcMs;
    case CharacterCountRole:
        return item.characterCount();
    default:
        return {};
    }
}

QHash<int, QByteArray> ClipboardHistoryModel::roleNames() const
{
    return {{HistoryIdRole, "historyId"},
            {PreviewTextRole, "previewText"},
            {CapturedAtMsRole, "capturedAtMs"},
            {CharacterCountRole, "characterCount"}};
}

ClipboardHistoryModel::CaptureOutcome ClipboardHistoryModel::capture(
    const QString &text, qint64 capturedAtUtcMs, const QString &preferredId)
{
    if (text.isEmpty()) {
        return CaptureOutcome::Empty;
    }
    if (text.toUtf8().size() > MaximumItemUtf8Bytes) {
        return CaptureOutcome::Oversize;
    }

    const QString previousSelection = m_selectedId;
    const auto found = std::find_if(m_items.begin(), m_items.end(),
                                    [&text](const ClipboardHistoryItem &item) {
                                        return item.text == text;
                                    });
    const bool duplicate = found != m_items.end();
    beginResetModel();
    if (duplicate) {
        ClipboardHistoryItem item = *found;
        m_items.erase(found);
        item.capturedAtUtcMs = capturedAtUtcMs;
        m_items.prepend(std::move(item));
    } else {
        ClipboardHistoryItem item;
        item.id = preferredId.isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
            : preferredId;
        item.text = text;
        item.capturedAtUtcMs = capturedAtUtcMs;
        m_items.prepend(std::move(item));
        if (m_items.size() > MaximumItems) {
            m_items.resize(MaximumItems);
        }
    }
    ++m_revision;
    rebuildVisibleIndices(previousSelection);
    endResetModel();
    emit historyChanged();
    return duplicate ? CaptureOutcome::DuplicateRefreshed : CaptureOutcome::Inserted;
}

void ClipboardHistoryModel::mergePersisted(const QVector<ClipboardHistoryItem> &items,
                                           quint64 revision)
{
    const bool hasSessionCaptures = !m_items.isEmpty();
    const QString previousSelection = m_selectedId;
    beginResetModel();
    for (const ClipboardHistoryItem &item : items) {
        if (item.id.isEmpty() || item.text.isEmpty()
            || item.text.toUtf8().size() > MaximumItemUtf8Bytes) {
            continue;
        }
        const bool exists = std::any_of(m_items.cbegin(), m_items.cend(),
                                        [&item](const ClipboardHistoryItem &current) {
                                            return current.text == item.text;
                                        });
        if (!exists) {
            m_items.append(item);
        }
        if (m_items.size() >= MaximumItems) {
            break;
        }
    }
    m_revision = qMax(m_revision, revision) + (hasSessionCaptures ? 1 : 0);
    rebuildVisibleIndices(previousSelection);
    endResetModel();
    emit historyChanged();
}

bool ClipboardHistoryModel::deleteById(const QString &id)
{
    const int index = itemIndexById(id);
    if (index < 0) {
        return false;
    }
    const QString previousSelection = m_selectedId;
    beginResetModel();
    m_items.removeAt(index);
    ++m_revision;
    rebuildVisibleIndices(previousSelection);
    endResetModel();
    emit historyChanged();
    return true;
}

bool ClipboardHistoryModel::clearHistory(bool forceRevision)
{
    if (m_items.isEmpty() && !forceRevision) {
        return false;
    }
    beginResetModel();
    m_items.clear();
    m_visibleIndices.clear();
    m_selectedId.clear();
    ++m_revision;
    endResetModel();
    emit historyChanged();
    return true;
}

void ClipboardHistoryModel::reset()
{
    beginResetModel();
    m_items.clear();
    m_visibleIndices.clear();
    m_filter.clear();
    m_selectedId.clear();
    m_revision = 0;
    endResetModel();
    emit historyChanged();
}

void ClipboardHistoryModel::setFilter(const QString &query)
{
    if (m_filter == query) {
        return;
    }
    const QString previousSelection = m_selectedId;
    beginResetModel();
    m_filter = query;
    rebuildVisibleIndices(previousSelection);
    endResetModel();
}

QString ClipboardHistoryModel::filter() const { return m_filter; }
QString ClipboardHistoryModel::selectedId() const { return m_selectedId; }

void ClipboardHistoryModel::setSelectedId(const QString &id)
{
    if (m_selectedId == id) {
        return;
    }
    const bool visible = std::any_of(m_visibleIndices.cbegin(), m_visibleIndices.cend(),
                                     [this, &id](int index) {
                                         return m_items.at(index).id == id;
                                     });
    if (id.isEmpty() || visible) {
        m_selectedId = id;
    }
}

QString ClipboardHistoryModel::textById(const QString &id) const
{
    const int index = itemIndexById(id);
    return index >= 0 ? m_items.at(index).text : QString();
}

QVector<ClipboardHistoryItem> ClipboardHistoryModel::items() const { return m_items; }

QVector<QString> ClipboardHistoryModel::visibleIds() const
{
    QVector<QString> ids;
    ids.reserve(m_visibleIndices.size());
    for (int index : m_visibleIndices) {
        ids.append(m_items.at(index).id);
    }
    return ids;
}

quint64 ClipboardHistoryModel::revision() const { return m_revision; }

QString ClipboardHistoryModel::previewFor(const QString &text)
{
    QString preview = text.left(240);
    preview.replace(QLatin1Char('\r'), QChar());
    return preview;
}

void ClipboardHistoryModel::rebuildVisibleIndices(const QString &preferredSelection)
{
    m_visibleIndices.clear();
    m_visibleIndices.reserve(m_items.size());
    for (int index = 0; index < m_items.size(); ++index) {
        if (m_filter.isEmpty()
            || m_items.at(index).text.contains(m_filter, Qt::CaseInsensitive)) {
            m_visibleIndices.append(index);
        }
    }
    const bool selectedStillVisible = std::any_of(
        m_visibleIndices.cbegin(), m_visibleIndices.cend(),
        [this, &preferredSelection](int index) {
            return m_items.at(index).id == preferredSelection;
        });
    m_selectedId = selectedStillVisible
        ? preferredSelection
        : (m_visibleIndices.isEmpty() ? QString()
                                      : m_items.at(m_visibleIndices.first()).id);
}

int ClipboardHistoryModel::itemIndexById(const QString &id) const
{
    for (int index = 0; index < m_items.size(); ++index) {
        if (m_items.at(index).id == id) {
            return index;
        }
    }
    return -1;
}
