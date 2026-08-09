#pragma once

#include "clipboardhistorymodel.h"

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThreadPool>

#include <functional>
#include <atomic>

struct ClipboardHistorySnapshot
{
    quint64 revision = 0;
    QVector<ClipboardHistoryItem> items;
};

class ClipboardHistoryStore final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(quint64 persistedRevision READ persistedRevision NOTIFY stateChanged)

public:
    static constexpr int ShutdownWaitTimeoutMs = 10000;
    enum class State { Loading, Ready, WritePending, WriteFailed, ReadLocked };
    Q_ENUM(State)

    explicit ClipboardHistoryStore(QString filePath, QObject *parent = nullptr);
    ~ClipboardHistoryStore() override;

    QString filePath() const;
    State state() const;
    QString stateName() const;
    QString error() const;
    quint64 persistedRevision() const;
    bool healthy() const;

    bool load(ClipboardHistorySnapshot *snapshot, QString *errorMessage = nullptr);
    void loadAsync(std::function<void(bool, ClipboardHistorySnapshot, QString)> completion);
    void save(const ClipboardHistorySnapshot &snapshot);
    bool waitForIdle(int timeoutMs = 10000);
    bool flushForShutdown(int timeoutMs = ShutdownWaitTimeoutMs);
    void setTestWriteDelayMs(int delayMs);
    bool resetUnreadableStore(QString *errorMessage = nullptr);
    bool removeIsolatedFile(const QString &allowedDirectory, QString *errorMessage = nullptr);
    void setFault(const QString &operation, bool enabled);

    static QByteArray encodeEnvelope(const ClipboardHistorySnapshot &snapshot,
                                     QString *errorMessage = nullptr);
    static bool decodeEnvelope(const QByteArray &envelope, ClipboardHistorySnapshot *snapshot,
                               QString *errorMessage = nullptr);
    static bool protectForCurrentUser(const QByteArray &plain, QByteArray *cipher,
                                      QString *errorMessage = nullptr);
    static bool unprotectForCurrentUser(const QByteArray &cipher, QByteArray *plain,
                                        QString *errorMessage = nullptr);

signals:
    void stateChanged();

private:
    bool saveNow(const ClipboardHistorySnapshot &snapshot, QString *errorMessage);
    void runSaveWorker();
    bool faultEnabled(const QString &operation) const;
    void updateState(State state, const QString &error = {}, quint64 persistedRevision = 0);

    QString m_filePath;
    mutable QMutex m_mutex;
    State m_state = State::Loading;
    QString m_error;
    quint64 m_persistedRevision = 0;
    ClipboardHistorySnapshot m_latestSnapshot;
    bool m_workerRunning = false;
    QSet<QString> m_faults;
    QThreadPool m_pool;
    std::atomic_bool m_abandonRequested = false;
    int m_testWriteDelayMs = 0;
};
