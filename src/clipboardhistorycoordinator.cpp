#include "clipboardhistorycoordinator.h"

#include "clipboardhistorystore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <utility>

ClipboardHistoryCoordinator::ClipboardHistoryCoordinator(bool testMode, bool historyEnabled,
                                                         QString storePath, QObject *parent)
    : QObject(parent)
    , m_gateway(ClipboardGateway::create(testMode))
{
    m_monotonic.start();
    if (!historyEnabled) {
        return;
    }

    m_model = std::make_unique<ClipboardHistoryModel>();
    m_store = std::make_unique<ClipboardHistoryStore>(std::move(storePath));
    connect(m_model.get(), &ClipboardHistoryModel::historyChanged, this, [this] {
        persist();
        emit stateChanged();
    });
    connect(m_store.get(), &ClipboardHistoryStore::stateChanged, this, [this] {
        m_storeError = m_store->error();
        updateError();
    });

    QPointer<ClipboardHistoryCoordinator> guard(this);
    m_store->loadAsync(
        [guard](bool loaded, ClipboardHistorySnapshot snapshot, QString error) mutable {
            if (!guard) {
                return;
            }
            QMetaObject::invokeMethod(
                guard,
                [guard, loaded, snapshot = std::move(snapshot), error = std::move(error)]() mutable {
                    if (!guard) {
                        return;
                    }
                    if (loaded) {
                        guard->m_model->mergePersisted(snapshot.items, snapshot.revision);
                    } else {
                        guard->m_storeError = error;
                        guard->updateError();
                    }
                },
                Qt::QueuedConnection);
        });
}

ClipboardHistoryCoordinator::~ClipboardHistoryCoordinator()
{
    if (m_gateway) {
        m_gateway->stopMonitoring();
    }
    if (m_store && !m_store->flushForShutdown()) {
        // aboutToQuit 后事件循环不再承担业务工作。极端超时下保留对象到进程退出，
        // 避免 QThreadPool 析构再次无限等待；worker 已被标记为不得 commit。
        m_store.release();
    }
}

bool ClipboardHistoryCoordinator::available() const
{
    return bool(m_model);
}

bool ClipboardHistoryCoordinator::healthy() const
{
    return available() && m_store && m_store->healthy() && m_error.isEmpty();
}

QString ClipboardHistoryCoordinator::error() const
{
    return m_error;
}

QAbstractItemModel *ClipboardHistoryCoordinator::model() const
{
    return m_model.get();
}

bool ClipboardHistoryCoordinator::readText(QString *text, QString *errorMessage)
{
    return m_gateway && m_gateway->readText(text, errorMessage);
}

bool ClipboardHistoryCoordinator::writeText(const QString &text, QString *errorMessage)
{
    if (!m_gateway || !m_gateway->writeText(text, errorMessage)) {
        return false;
    }
    if (available() && !text.isEmpty()) {
        const quint32 sequence = m_gateway->sequenceNumber();
        captureText(text, QDateTime::currentMSecsSinceEpoch());
        if (sequence != 0) {
            m_selfWriteSequence = sequence;
            m_selfWriteFingerprint = QCryptographicHash::hash(
                text.toUtf8(), QCryptographicHash::Sha256);
            m_selfWriteExpiresAtMs = m_monotonic.elapsed() + 2000;
        }
    }
    return true;
}

bool ClipboardHistoryCoordinator::deliverText(quintptr editorWindowHandle,
                                              QString *errorMessage)
{
    return m_gateway && m_gateway->deliverText(editorWindowHandle, errorMessage);
}

bool ClipboardHistoryCoordinator::startMonitoring(quintptr windowHandle)
{
    if (!available() || !m_gateway) {
        return false;
    }
    QString error;
    const bool started = m_gateway->startMonitoring(windowHandle, &error);
    setMonitorError(error);
    return started;
}

void ClipboardHistoryCoordinator::processClipboardChange(int attempt)
{
    if (!available() || !m_gateway || !m_gateway->monitoring()) {
        return;
    }
    const quint32 before = m_gateway->sequenceNumber();
    QString error;
    const ClipboardCaptureCandidate candidate = m_gateway->readHistoryCandidate(&error);
    const quint32 after = m_gateway->sequenceNumber();
    if (before != 0 && after != 0 && before != after) {
        constexpr int maximumAttempts = 6;
        if (attempt + 1 < maximumAttempts) {
            QTimer::singleShot(12, this, [this, attempt] {
                processClipboardChange(attempt + 1);
            });
        } else {
            setMonitorError(QStringLiteral("剪贴板在读取期间持续变化，已暂停本次捕获"));
        }
        return;
    }
    if (candidate.kind == ClipboardCaptureCandidate::Kind::ReadFailure) {
        if (!error.isEmpty()) {
            setMonitorError(error);
        }
        return;
    }
    setMonitorError({});
    captureTestCandidate(candidate);
}

bool ClipboardHistoryCoordinator::shutdown(int timeoutMs)
{
    if (m_gateway) {
        m_gateway->stopMonitoring();
    }
    return !m_store || m_store->waitForIdle(timeoutMs);
}

void ClipboardHistoryCoordinator::setFilter(const QString &query)
{
    if (m_model) {
        m_model->setFilter(query);
    }
}

void ClipboardHistoryCoordinator::setSelectedId(const QString &id)
{
    if (m_model) {
        m_model->setSelectedId(id);
    }
}

QString ClipboardHistoryCoordinator::textById(const QString &id) const
{
    return m_model ? m_model->textById(id) : QString();
}

bool ClipboardHistoryCoordinator::deleteById(const QString &id)
{
    return m_model && m_model->deleteById(id);
}

bool ClipboardHistoryCoordinator::canClear() const
{
    return m_model && (!m_model->items().isEmpty()
        || (m_store && m_store->state() == ClipboardHistoryStore::State::ReadLocked));
}

bool ClipboardHistoryCoordinator::clearHistory()
{
    if (!m_model) {
        return false;
    }
    const bool resetUnreadableStore = m_store
        && m_store->state() == ClipboardHistoryStore::State::ReadLocked;
    if (resetUnreadableStore) {
        m_store->resetUnreadableStore();
    }
    return m_model->clearHistory(resetUnreadableStore);
}

QVector<ClipboardHistoryItem> ClipboardHistoryCoordinator::items() const
{
    return m_model ? m_model->items() : QVector<ClipboardHistoryItem>{};
}

QVector<QString> ClipboardHistoryCoordinator::visibleIds() const
{
    return m_model ? m_model->visibleIds() : QVector<QString>{};
}

QString ClipboardHistoryCoordinator::filter() const
{
    return m_model ? m_model->filter() : QString();
}

QString ClipboardHistoryCoordinator::selectedId() const
{
    return m_model ? m_model->selectedId() : QString();
}

quint64 ClipboardHistoryCoordinator::revision() const
{
    return m_model ? m_model->revision() : 0;
}

quint64 ClipboardHistoryCoordinator::persistedRevision() const
{
    return m_store ? m_store->persistedRevision() : 0;
}

QString ClipboardHistoryCoordinator::storeFilePath() const
{
    return m_store ? m_store->filePath() : QString();
}

QString ClipboardHistoryCoordinator::storeStateName() const
{
    return m_store ? m_store->stateName() : QStringLiteral("Unavailable");
}

QString ClipboardHistoryCoordinator::backendName() const
{
    return m_gateway ? m_gateway->backendName() : QStringLiteral("unavailable");
}

quint64 ClipboardHistoryCoordinator::nativeAccessAttempts() const
{
    return m_gateway ? m_gateway->nativeAccessAttempts() : 0;
}

quint32 ClipboardHistoryCoordinator::selfWriteSequence() const
{
    return m_selfWriteSequence;
}

QString ClipboardHistoryCoordinator::testClipboardText() const
{
    return m_gateway ? m_gateway->testClipboardText() : QString();
}

QString ClipboardHistoryCoordinator::testDeliveredText() const
{
    return m_gateway ? m_gateway->testDeliveredText() : QString();
}

void ClipboardHistoryCoordinator::setTestClipboardText(const QString &text)
{
    if (m_gateway) {
        m_gateway->setTestClipboardText(text);
    }
}

ClipboardCaptureCandidate ClipboardHistoryCoordinator::injectTestChange(
    const ClipboardCaptureCandidate &candidate)
{
    return m_gateway ? m_gateway->injectTestChange(candidate) : candidate;
}

ClipboardHistoryCoordinator::CaptureOutcome
ClipboardHistoryCoordinator::captureTestCandidate(const ClipboardCaptureCandidate &candidate)
{
    if (candidate.excludeFromMonitor) {
        return CaptureOutcome::ExcludedFromMonitor;
    }
    if (candidate.includeInHistory == ClipboardCaptureCandidate::IncludeInHistory::Deny
        || candidate.includeInHistory == ClipboardCaptureCandidate::IncludeInHistory::Malformed) {
        return CaptureOutcome::ExcludedFromHistory;
    }
    if (candidate.kind == ClipboardCaptureCandidate::Kind::Empty) {
        return CaptureOutcome::Empty;
    }
    if (candidate.kind == ClipboardCaptureCandidate::Kind::NonText) {
        return CaptureOutcome::NonText;
    }
    if (candidate.kind == ClipboardCaptureCandidate::Kind::ReadFailure) {
        return CaptureOutcome::ReadFailure;
    }

    const QByteArray fingerprint = QCryptographicHash::hash(
        candidate.text.toUtf8(), QCryptographicHash::Sha256);
    const bool selfNotification = m_selfWriteSequence != 0
        && candidate.sequenceNumber == m_selfWriteSequence
        && fingerprint == m_selfWriteFingerprint
        && m_monotonic.elapsed() <= m_selfWriteExpiresAtMs;
    return captureText(candidate.text, candidate.capturedAtUtcMs, selfNotification);
}

bool ClipboardHistoryCoordinator::resetForTest(const QString &allowedDirectory,
                                               QString *errorMessage)
{
    if (m_model) {
        m_model->reset();
    }
    if (!m_store) {
        return true;
    }
    m_store->waitForIdle(5000);
    return m_store->removeIsolatedFile(allowedDirectory, errorMessage);
}

void ClipboardHistoryCoordinator::setTestFault(const QString &operation, bool enabled)
{
    if (m_gateway) {
        m_gateway->setTestFault(operation, enabled);
    }
    if (m_store) {
        m_store->setFault(operation, enabled);
    }
}

bool ClipboardHistoryCoordinator::restartMonitoring(quintptr windowHandle)
{
    if (!m_gateway) {
        return false;
    }
    m_gateway->stopMonitoring();
    QString error;
    const bool started = m_gateway->startMonitoring(windowHandle, &error);
    setMonitorError(error);
    return started;
}

bool ClipboardHistoryCoordinator::waitForIdle(int timeoutMs)
{
    return !m_store || m_store->waitForIdle(timeoutMs);
}

ClipboardHistoryCoordinator::CaptureOutcome ClipboardHistoryCoordinator::captureText(
    const QString &text, qint64 capturedAtUtcMs, bool selfNotification)
{
    if (!m_model) {
        return CaptureOutcome::Unavailable;
    }
    if (selfNotification) {
        m_selfWriteSequence = 0;
        m_selfWriteFingerprint.clear();
        m_selfWriteExpiresAtMs = 0;
        return CaptureOutcome::SelfWriteNotification;
    }
    const auto outcome = m_model->capture(
        text, capturedAtUtcMs > 0 ? capturedAtUtcMs : QDateTime::currentMSecsSinceEpoch());
    switch (outcome) {
    case ClipboardHistoryModel::CaptureOutcome::Inserted:
        return CaptureOutcome::Inserted;
    case ClipboardHistoryModel::CaptureOutcome::DuplicateRefreshed:
        return CaptureOutcome::DuplicateRefreshed;
    case ClipboardHistoryModel::CaptureOutcome::Empty:
        return CaptureOutcome::Empty;
    case ClipboardHistoryModel::CaptureOutcome::Oversize:
        return CaptureOutcome::Oversize;
    }
    return CaptureOutcome::ReadFailure;
}

void ClipboardHistoryCoordinator::persist()
{
    if (!m_store || !m_model
        || m_store->state() == ClipboardHistoryStore::State::Loading) {
        return;
    }
    ClipboardHistorySnapshot snapshot;
    snapshot.revision = m_model->revision();
    snapshot.items = m_model->items();
    m_store->save(snapshot);
}

void ClipboardHistoryCoordinator::setMonitorError(const QString &error)
{
    if (m_monitorError == error) {
        return;
    }
    m_monitorError = error;
    updateError();
}

void ClipboardHistoryCoordinator::updateError()
{
    const QString combined = !m_monitorError.isEmpty() ? m_monitorError : m_storeError;
    if (m_error == combined) {
        return;
    }
    m_error = combined;
    emit stateChanged();
}
