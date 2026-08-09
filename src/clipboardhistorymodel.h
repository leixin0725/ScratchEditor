#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QString>
#include <QVector>

#include <optional>

struct ClipboardHistoryItem
{
    QString id;
    QString text;
    qint64 capturedAtUtcMs = 0;

    qsizetype characterCount() const { return text.size(); }
};

class ClipboardHistoryModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        HistoryIdRole = Qt::UserRole + 1,
        PreviewTextRole,
        CapturedAtMsRole,
        CharacterCountRole,
    };

    enum class CaptureOutcome {
        Inserted,
        DuplicateRefreshed,
        Empty,
        Oversize,
    };

    explicit ClipboardHistoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    CaptureOutcome capture(const QString &text, qint64 capturedAtUtcMs,
                           const QString &preferredId = {});
    void mergePersisted(const QVector<ClipboardHistoryItem> &items, quint64 revision);
    bool deleteById(const QString &id);
    bool clearHistory(bool forceRevision = false);
    void reset();
    void setFilter(const QString &query);

    QString filter() const;
    QString selectedId() const;
    void setSelectedId(const QString &id);
    QString textById(const QString &id) const;
    QVector<ClipboardHistoryItem> items() const;
    QVector<QString> visibleIds() const;
    quint64 revision() const;

    static constexpr qsizetype MaximumItemUtf8Bytes = 1024 * 1024;
    static constexpr qsizetype MaximumItems = 100;

signals:
    void historyChanged();

private:
    static QString previewFor(const QString &text);
    void rebuildVisibleIndices(const QString &preferredSelection = {});
    int itemIndexById(const QString &id) const;

    QVector<ClipboardHistoryItem> m_items;
    QVector<int> m_visibleIndices;
    QString m_filter;
    QString m_selectedId;
    quint64 m_revision = 0;
};
