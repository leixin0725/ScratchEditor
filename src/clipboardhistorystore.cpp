#include "clipboardhistorystore.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QSaveFile>
#include <QThread>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dpapi.h>
#endif

namespace {
constexpr quint32 Magic = 0x53434831; // SCH1
constexpr quint16 SchemaVersion = 1;
constexpr qsizetype DigestSize = 32;
constexpr qsizetype MaximumIdBytes = 1024;

void clearSensitive(QByteArray *bytes)
{
    if (bytes && !bytes->isEmpty()) {
        volatile char *data = bytes->data();
        for (qsizetype index = 0; index < bytes->size(); ++index) {
            data[index] = 0;
        }
        bytes->clear();
    }
}
}

ClipboardHistoryStore::ClipboardHistoryStore(QString filePath, QObject *parent)
    : QObject(parent)
    , m_filePath(QFileInfo(std::move(filePath)).absoluteFilePath())
{
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);
}

ClipboardHistoryStore::~ClipboardHistoryStore()
{
    m_pool.clear();
    m_pool.waitForDone(ShutdownWaitTimeoutMs);
}

QString ClipboardHistoryStore::filePath() const { return m_filePath; }

ClipboardHistoryStore::State ClipboardHistoryStore::state() const
{
    QMutexLocker locker(&m_mutex);
    return m_state;
}

QString ClipboardHistoryStore::stateName() const
{
    switch (state()) {
    case State::Loading: return QStringLiteral("Loading");
    case State::Ready: return QStringLiteral("Ready");
    case State::WritePending: return QStringLiteral("WritePending");
    case State::WriteFailed: return QStringLiteral("WriteFailed");
    case State::ReadLocked: return QStringLiteral("ReadLocked");
    }
    return {};
}

QString ClipboardHistoryStore::error() const
{
    QMutexLocker locker(&m_mutex);
    return m_error;
}

quint64 ClipboardHistoryStore::persistedRevision() const
{
    QMutexLocker locker(&m_mutex);
    return m_persistedRevision;
}

bool ClipboardHistoryStore::healthy() const
{
    const State current = state();
    return current == State::Ready || current == State::WritePending;
}

bool ClipboardHistoryStore::load(ClipboardHistorySnapshot *snapshot, QString *errorMessage)
{
    if (!snapshot) {
        return false;
    }
    if (faultEnabled(QStringLiteral("read"))) {
        const QString error = QStringLiteral("模拟历史读取失败");
        updateState(State::ReadLocked, error);
        if (errorMessage) *errorMessage = error;
        return false;
    }
    QFile file(m_filePath);
    if (!file.exists()) {
        *snapshot = {};
        updateState(State::Ready);
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        const QString error = QStringLiteral("无法读取剪贴板历史：%1").arg(file.errorString());
        updateState(State::ReadLocked, error);
        if (errorMessage) *errorMessage = error;
        return false;
    }
    const QByteArray cipher = file.readAll();
    QByteArray plain;
    QString error;
    if (faultEnabled(QStringLiteral("decrypt"))
        || !unprotectForCurrentUser(cipher, &plain, &error)
        || !decodeEnvelope(plain, snapshot, &error)) {
        clearSensitive(&plain);
        if (error.isEmpty()) error = QStringLiteral("模拟历史解密失败");
        updateState(State::ReadLocked, error);
        if (errorMessage) *errorMessage = error;
        return false;
    }
    clearSensitive(&plain);
    updateState(State::Ready, {}, snapshot->revision);
    return true;
}

void ClipboardHistoryStore::loadAsync(
    std::function<void(bool, ClipboardHistorySnapshot, QString)> completion)
{
    m_pool.start([this, completion = std::move(completion)]() mutable {
        ClipboardHistorySnapshot snapshot;
        QString error;
        const bool loaded = load(&snapshot, &error);
        completion(loaded, std::move(snapshot), std::move(error));
    });
}

void ClipboardHistoryStore::save(const ClipboardHistorySnapshot &snapshot)
{
    if (m_abandonRequested.load(std::memory_order_acquire)) {
        return;
    }
    bool startWorker = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_state == State::ReadLocked) {
            return;
        }
        if (snapshot.revision <= m_persistedRevision
            && snapshot.revision <= m_latestSnapshot.revision) {
            return;
        }
        m_latestSnapshot = snapshot;
        m_state = State::WritePending;
        m_error.clear();
        if (m_workerRunning) {
            startWorker = false;
        } else {
            m_workerRunning = true;
            startWorker = true;
        }
    }
    emit stateChanged();
    if (startWorker) {
        m_pool.start([this] { runSaveWorker(); });
    }
}

bool ClipboardHistoryStore::waitForIdle(int timeoutMs)
{
    return m_pool.waitForDone(timeoutMs);
}

bool ClipboardHistoryStore::flushForShutdown(int timeoutMs)
{
    if (m_pool.waitForDone(timeoutMs)) {
        return true;
    }
    m_abandonRequested.store(true, std::memory_order_release);
    m_pool.clear();
    return false;
}

void ClipboardHistoryStore::setTestWriteDelayMs(int delayMs)
{
    QMutexLocker locker(&m_mutex);
    m_testWriteDelayMs = qMax(0, delayMs);
}

bool ClipboardHistoryStore::resetUnreadableStore(QString *errorMessage)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_state != State::ReadLocked) {
            return true;
        }
        m_state = State::Ready;
        m_error.clear();
        m_persistedRevision = 0;
    }
    emit stateChanged();
    Q_UNUSED(errorMessage);
    return true;
}

bool ClipboardHistoryStore::removeIsolatedFile(const QString &allowedDirectory,
                                               QString *errorMessage)
{
    const QString allowed = QFileInfo(allowedDirectory).canonicalFilePath();
    const QString targetDirectory = QFileInfo(m_filePath).absoluteDir().canonicalPath();
    if (allowed.isEmpty() || targetDirectory.isEmpty() || targetDirectory != allowed) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("历史文件不在已验证的隔离目录中");
        }
        return false;
    }
    if (QFile::exists(m_filePath) && !QFile::remove(m_filePath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法删除隔离历史文件");
        }
        return false;
    }
    updateState(State::Ready, {}, 0);
    return true;
}

void ClipboardHistoryStore::setFault(const QString &operation, bool enabled)
{
    QMutexLocker locker(&m_mutex);
    if (enabled) m_faults.insert(operation); else m_faults.remove(operation);
}

QByteArray ClipboardHistoryStore::encodeEnvelope(const ClipboardHistorySnapshot &snapshot,
                                                 QString *errorMessage)
{
    if (snapshot.items.size() > ClipboardHistoryModel::MaximumItems) {
        if (errorMessage) *errorMessage = QStringLiteral("历史条目数量超限");
        return {};
    }
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << snapshot.revision << quint32(snapshot.items.size());
    QSet<QString> ids;
    QSet<QString> texts;
    for (const ClipboardHistoryItem &item : snapshot.items) {
        const QByteArray id = item.id.toUtf8();
        const QByteArray text = item.text.toUtf8();
        if (id.isEmpty() || id.size() > MaximumIdBytes || text.isEmpty()
            || text.size() > ClipboardHistoryModel::MaximumItemUtf8Bytes
            || ids.contains(item.id) || texts.contains(item.text)) {
            if (errorMessage) *errorMessage = QStringLiteral("历史条目字段无效或重复");
            return {};
        }
        ids.insert(item.id);
        texts.insert(item.text);
        stream << quint32(id.size());
        stream.writeRawData(id.constData(), id.size());
        stream << item.capturedAtUtcMs << quint32(text.size());
        stream.writeRawData(text.constData(), text.size());
    }
    QByteArray envelope;
    QDataStream outer(&envelope, QIODevice::WriteOnly);
    outer.setByteOrder(QDataStream::BigEndian);
    outer << Magic << SchemaVersion << quint64(payload.size());
    outer.writeRawData(payload.constData(), payload.size());
    const QByteArray digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    outer.writeRawData(digest.constData(), digest.size());
    return envelope;
}

bool ClipboardHistoryStore::decodeEnvelope(const QByteArray &envelope,
                                           ClipboardHistorySnapshot *snapshot,
                                           QString *errorMessage)
{
    if (!snapshot || envelope.size() < 4 + 2 + 8 + DigestSize) {
        if (errorMessage) *errorMessage = QStringLiteral("历史文件长度无效");
        return false;
    }
    QDataStream outer(envelope);
    outer.setByteOrder(QDataStream::BigEndian);
    quint32 magic = 0;
    quint16 version = 0;
    quint64 payloadLength = 0;
    outer >> magic >> version >> payloadLength;
    const qsizetype headerSize = 4 + 2 + 8;
    if (magic != Magic || version != SchemaVersion
        || payloadLength > static_cast<quint64>(envelope.size())
        || headerSize + static_cast<qsizetype>(payloadLength) + DigestSize != envelope.size()) {
        if (errorMessage) *errorMessage = QStringLiteral("历史文件 schema 或长度无效");
        return false;
    }
    const QByteArray payload = envelope.mid(headerSize, payloadLength);
    const QByteArray digest = envelope.right(DigestSize);
    if (QCryptographicHash::hash(payload, QCryptographicHash::Sha256) != digest) {
        if (errorMessage) *errorMessage = QStringLiteral("历史文件完整性校验失败");
        return false;
    }
    QDataStream stream(payload);
    stream.setByteOrder(QDataStream::BigEndian);
    quint64 revision = 0;
    quint32 count = 0;
    stream >> revision >> count;
    if (count > ClipboardHistoryModel::MaximumItems) {
        if (errorMessage) *errorMessage = QStringLiteral("历史条目数量超限");
        return false;
    }
    ClipboardHistorySnapshot decoded;
    decoded.revision = revision;
    QSet<QString> ids;
    QSet<QString> texts;
    for (quint32 index = 0; index < count; ++index) {
        quint32 idLength = 0;
        stream >> idLength;
        if (idLength == 0 || idLength > MaximumIdBytes
            || stream.device()->bytesAvailable() < idLength) return false;
        QByteArray id(idLength, Qt::Uninitialized);
        if (stream.readRawData(id.data(), idLength) != int(idLength)) return false;
        qint64 capturedAt = 0;
        quint32 textLength = 0;
        stream >> capturedAt >> textLength;
        if (textLength == 0 || textLength > ClipboardHistoryModel::MaximumItemUtf8Bytes
            || stream.device()->bytesAvailable() < textLength) return false;
        QByteArray text(textLength, Qt::Uninitialized);
        if (stream.readRawData(text.data(), textLength) != int(textLength)) return false;
        const QString itemId = QString::fromUtf8(id);
        const QString itemText = QString::fromUtf8(text);
        if (itemId.isEmpty() || itemText.isEmpty() || ids.contains(itemId)
            || texts.contains(itemText) || itemId.toUtf8() != id || itemText.toUtf8() != text) {
            return false;
        }
        ids.insert(itemId);
        texts.insert(itemText);
        decoded.items.append({itemId, itemText, capturedAt});
    }
    if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
        if (errorMessage) *errorMessage = QStringLiteral("历史文件包含尾随或截断数据");
        return false;
    }
    *snapshot = std::move(decoded);
    return true;
}

bool ClipboardHistoryStore::protectForCurrentUser(const QByteArray &plain, QByteArray *cipher,
                                                  QString *errorMessage)
{
    if (!cipher) return false;
#ifdef Q_OS_WIN
    DATA_BLOB input{static_cast<DWORD>(plain.size()),
                    reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"ScratchEditor clipboard history", nullptr, nullptr,
                          nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法加密剪贴板历史（错误 %1）").arg(GetLastError());
        return false;
    }
    *cipher = QByteArray(reinterpret_cast<const char *>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return true;
#else
    *cipher = plain;
    Q_UNUSED(errorMessage);
    return true;
#endif
}

bool ClipboardHistoryStore::unprotectForCurrentUser(const QByteArray &cipher, QByteArray *plain,
                                                    QString *errorMessage)
{
    if (!plain) return false;
#ifdef Q_OS_WIN
    DATA_BLOB input{static_cast<DWORD>(cipher.size()),
                    reinterpret_cast<BYTE *>(const_cast<char *>(cipher.constData()))};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法解密剪贴板历史（错误 %1）").arg(GetLastError());
        return false;
    }
    *plain = QByteArray(reinterpret_cast<const char *>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return true;
#else
    *plain = cipher;
    Q_UNUSED(errorMessage);
    return true;
#endif
}

bool ClipboardHistoryStore::saveNow(const ClipboardHistorySnapshot &snapshot,
                                    QString *errorMessage)
{
    int testDelayMs = 0;
    {
        QMutexLocker locker(&m_mutex);
        testDelayMs = m_testWriteDelayMs;
    }
    if (testDelayMs > 0) {
        QThread::msleep(static_cast<unsigned long>(testDelayMs));
    }
    if (m_abandonRequested.load(std::memory_order_acquire)) {
        if (errorMessage) *errorMessage = QStringLiteral("退出等待超时，已放弃未提交历史 revision");
        return false;
    }
    if (faultEnabled(QStringLiteral("encrypt"))) {
        if (errorMessage) *errorMessage = QStringLiteral("模拟历史加密失败");
        return false;
    }
    QByteArray plain = encodeEnvelope(snapshot, errorMessage);
    if (plain.isEmpty()) return false;
    QByteArray cipher;
    const bool encrypted = protectForCurrentUser(plain, &cipher, errorMessage);
    clearSensitive(&plain);
    if (!encrypted) return false;
    if (faultEnabled(QStringLiteral("write"))) {
        if (errorMessage) *errorMessage = QStringLiteral("模拟历史写入失败");
        return false;
    }
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QSaveFile file(m_filePath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(cipher) != cipher.size()
        || m_abandonRequested.load(std::memory_order_acquire)
        || !file.commit()) {
        if (errorMessage) *errorMessage = QStringLiteral("无法原子保存剪贴板历史：%1").arg(file.errorString());
        file.cancelWriting();
        return false;
    }
    return true;
}

void ClipboardHistoryStore::runSaveWorker()
{
    while (true) {
        ClipboardHistorySnapshot snapshot;
        {
            QMutexLocker locker(&m_mutex);
            snapshot = m_latestSnapshot;
        }
        QString error;
        const bool success = saveNow(snapshot, &error);
        bool repeat = false;
        {
            QMutexLocker locker(&m_mutex);
            if (success) {
                m_persistedRevision = qMax(m_persistedRevision, snapshot.revision);
            }
            repeat = success && m_latestSnapshot.revision > snapshot.revision;
            if (!repeat) {
                m_workerRunning = false;
                m_state = success ? State::Ready : State::WriteFailed;
                m_error = success ? QString() : error;
            }
        }
        QMetaObject::invokeMethod(this, [this] { emit stateChanged(); }, Qt::QueuedConnection);
        if (!repeat) return;
    }
}

bool ClipboardHistoryStore::faultEnabled(const QString &operation) const
{
    QMutexLocker locker(&m_mutex);
    return m_faults.contains(operation);
}

void ClipboardHistoryStore::updateState(State state, const QString &error,
                                        quint64 persistedRevision)
{
    {
        QMutexLocker locker(&m_mutex);
        m_state = state;
        m_error = error;
        m_persistedRevision = persistedRevision;
    }
    emit stateChanged();
}
