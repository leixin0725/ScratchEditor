#pragma once

#include "clipboardgateway.h"
#include "clipboardhistorymodel.h"

#include <QAbstractItemModel>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

class ClipboardHistoryStore;

class ClipboardHistoryCoordinator final : public QObject
{
    Q_OBJECT

public:
    enum class CaptureOutcome {
        Unavailable,
        Inserted,
        DuplicateRefreshed,
        Empty,
        Oversize,
        SelfWriteNotification,
        ExcludedFromMonitor,
        ExcludedFromHistory,
        NonText,
        ReadFailure,
    };

    explicit ClipboardHistoryCoordinator(bool testMode, bool historyEnabled,
                                         QString storePath, QObject *parent = nullptr);
    ~ClipboardHistoryCoordinator() override;

    bool available() const;
    bool healthy() const;
    QString error() const;
    QAbstractItemModel *model() const;

    bool readText(QString *text, QString *errorMessage);
    bool writeText(const QString &text, QString *errorMessage);
    bool deliverText(quintptr editorWindowHandle, QString *errorMessage);

    bool startMonitoring(quintptr windowHandle);
    void processClipboardChange(int attempt = 0);
    bool shutdown(int timeoutMs = 10000);

    void setFilter(const QString &query);
    void setSelectedId(const QString &id);
    QString textById(const QString &id) const;
    bool deleteById(const QString &id);
    bool canClear() const;
    bool clearHistory();

    QVector<ClipboardHistoryItem> items() const;
    QVector<QString> visibleIds() const;
    QString filter() const;
    QString selectedId() const;
    quint64 revision() const;
    quint64 persistedRevision() const;
    QString storeFilePath() const;
    QString storeStateName() const;
    QString backendName() const;
    quint64 nativeAccessAttempts() const;
    quint32 selfWriteSequence() const;

    QString testClipboardText() const;
    QString testDeliveredText() const;
    void setTestClipboardText(const QString &text);
    ClipboardCaptureCandidate injectTestChange(const ClipboardCaptureCandidate &candidate);
    CaptureOutcome captureTestCandidate(const ClipboardCaptureCandidate &candidate);
    bool resetForTest(const QString &allowedDirectory, QString *errorMessage = nullptr);
    void setTestFault(const QString &operation, bool enabled);
    bool restartMonitoring(quintptr windowHandle);
    bool waitForIdle(int timeoutMs = 10000);

signals:
    void stateChanged();

private:
    CaptureOutcome captureText(const QString &text, qint64 capturedAtUtcMs,
                               bool selfNotification = false);
    void persist();
    void setMonitorError(const QString &error);
    void updateError();

    std::unique_ptr<ClipboardGateway> m_gateway;
    std::unique_ptr<ClipboardHistoryModel> m_model;
    std::unique_ptr<ClipboardHistoryStore> m_store;
    QElapsedTimer m_monotonic;
    QString m_error;
    QString m_monitorError;
    QString m_storeError;
    quint32 m_selfWriteSequence = 0;
    QByteArray m_selfWriteFingerprint;
    qint64 m_selfWriteExpiresAtMs = 0;
};
