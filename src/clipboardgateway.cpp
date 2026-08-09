#include "clipboardgateway.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QSet>
#include <QThread>
#include <QtEndian>

#include <cstring>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

ClipboardGateway::ClipboardGateway(QObject *parent)
    : QObject(parent)
{
}

ClipboardGateway::~ClipboardGateway() = default;

void ClipboardGateway::setTestClipboardText(const QString &) {}
QString ClipboardGateway::testClipboardText() const { return {}; }
QString ClipboardGateway::testDeliveredText() const { return {}; }
ClipboardCaptureCandidate ClipboardGateway::injectTestChange(
    const ClipboardCaptureCandidate &candidate)
{
    return candidate;
}
void ClipboardGateway::setTestFault(const QString &, bool) {}

bool ClipboardGateway::decodeUnicodeTextBuffer(const QByteArray &bytes, QString *text,
                                               QString *errorMessage)
{
    if (!text) {
        return false;
    }
    if (bytes.size() < 2 || (bytes.size() % 2) != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("剪贴板文本缓冲区长度无效");
        }
        return false;
    }
    qsizetype terminatorByte = -1;
    for (qsizetype offset = 0; offset + 1 < bytes.size(); offset += 2) {
        if (bytes.at(offset) == 0 && bytes.at(offset + 1) == 0) {
            terminatorByte = offset;
            break;
        }
    }
    if (terminatorByte < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("剪贴板文本缺少终止符");
        }
        return false;
    }
    text->clear();
    text->reserve(terminatorByte / 2);
    for (qsizetype offset = 0; offset < terminatorByte; offset += 2) {
        const auto *unit = reinterpret_cast<const uchar *>(bytes.constData() + offset);
        text->append(QChar(qFromLittleEndian<quint16>(unit)));
    }
    if (text->toUtf8().size() > 1024 * 1024) {
        text->clear();
        if (errorMessage) {
            *errorMessage = QStringLiteral("剪贴板文本超过 1 MiB");
        }
        return false;
    }
    return true;
}

namespace {

class MemoryClipboardGateway final : public ClipboardGateway
{
public:
    using ClipboardGateway::ClipboardGateway;

    QString backendName() const override { return QStringLiteral("memory"); }
    quint64 nativeAccessAttempts() const override { return 0; }
    bool startMonitoring(quintptr, QString *errorMessage) override
    {
        if (m_faults.contains(QStringLiteral("listenerRegistration"))) {
            m_monitoring = false;
            if (errorMessage) {
                *errorMessage = QStringLiteral("模拟监听器注册失败");
            }
            return false;
        }
        m_monitoring = true;
        return true;
    }
    void stopMonitoring() override { m_monitoring = false; }
    bool monitoring() const override { return m_monitoring; }
    quint32 sequenceNumber() override
    {
        if (m_faults.contains(QStringLiteral("sequenceRace"))) {
            ++m_sequenceRaceReads;
            if (m_sequenceRaceReads >= 2) {
                m_faults.remove(QStringLiteral("sequenceRace"));
                m_sequenceRaceReads = 0;
                ++m_sequence;
            }
        }
        return m_sequence;
    }
    ClipboardCaptureCandidate readHistoryCandidate(QString *errorMessage) override
    {
        if (m_faults.contains(QStringLiteral("read"))) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("模拟剪贴板读取失败");
            }
            ClipboardCaptureCandidate failed;
            failed.kind = ClipboardCaptureCandidate::Kind::ReadFailure;
            return failed;
        }
        return m_candidate;
    }
    bool readText(QString *text, QString *) override
    {
        if (!text || m_faults.contains(QStringLiteral("read"))) {
            return false;
        }
        *text = m_clipboardText;
        return true;
    }
    bool writeText(const QString &text, QString *errorMessage) override
    {
        if (m_faults.contains(QStringLiteral("write"))) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("模拟剪贴板写入失败");
            }
            return false;
        }
        m_clipboardText = text;
        if (++m_sequence == 0) {
            ++m_sequence;
        }
        return true;
    }
    bool deliverText(quintptr, QString *) override
    {
        m_deliveredText = m_clipboardText;
        return true;
    }
    void setTestClipboardText(const QString &text) override { m_clipboardText = text; }
    QString testClipboardText() const override { return m_clipboardText; }
    QString testDeliveredText() const override { return m_deliveredText; }
    ClipboardCaptureCandidate injectTestChange(
        const ClipboardCaptureCandidate &candidate) override
    {
        m_candidate = candidate;
        if (m_candidate.sequenceNumber == 0) {
            if (++m_sequence == 0) {
                ++m_sequence;
            }
            m_candidate.sequenceNumber = m_sequence;
        } else {
            m_sequence = m_candidate.sequenceNumber;
        }
        if (m_candidate.kind == ClipboardCaptureCandidate::Kind::Text) {
            m_clipboardText = m_candidate.text;
        } else if (m_candidate.kind == ClipboardCaptureCandidate::Kind::Empty) {
            m_clipboardText.clear();
        }
        return m_candidate;
    }
    void setTestFault(const QString &operation, bool enabled) override
    {
        if (enabled) {
            m_faults.insert(operation);
            if (operation == QStringLiteral("sequenceRace")) {
                m_sequenceRaceReads = 0;
            }
        } else {
            m_faults.remove(operation);
        }
    }

private:
    bool m_monitoring = false;
    quint32 m_sequence = 0;
    QString m_clipboardText;
    QString m_deliveredText;
    ClipboardCaptureCandidate m_candidate;
    QSet<QString> m_faults;
    int m_sequenceRaceReads = 0;
};

#ifdef Q_OS_WIN
class NativeClipboardGateway final : public ClipboardGateway
{
public:
    using ClipboardGateway::ClipboardGateway;
    ~NativeClipboardGateway() override { stopMonitoring(); }

    QString backendName() const override { return QStringLiteral("win32"); }
    quint64 nativeAccessAttempts() const override { return m_nativeAccessAttempts; }
    bool startMonitoring(quintptr windowHandle, QString *errorMessage) override
    {
        stopMonitoring();
        m_window = reinterpret_cast<HWND>(windowHandle);
        ++m_nativeAccessAttempts;
        if (!m_window || !AddClipboardFormatListener(m_window)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("无法注册剪贴板监听器（错误 %1）")
                                    .arg(GetLastError());
            }
            m_window = nullptr;
            return false;
        }
        m_monitoring = true;
        m_baselineSequence = sequenceNumber();
        return true;
    }
    void stopMonitoring() override
    {
        if (m_monitoring && m_window) {
            ++m_nativeAccessAttempts;
            RemoveClipboardFormatListener(m_window);
        }
        m_monitoring = false;
        m_window = nullptr;
    }
    bool monitoring() const override { return m_monitoring; }
    quint32 sequenceNumber() override
    {
        ++m_nativeAccessAttempts;
        return GetClipboardSequenceNumber();
    }
    ClipboardCaptureCandidate readHistoryCandidate(QString *errorMessage) override
    {
        ClipboardCaptureCandidate candidate;
        candidate.sequenceNumber = sequenceNumber();
        candidate.capturedAtUtcMs = QDateTime::currentMSecsSinceEpoch();
        ++m_nativeAccessAttempts;
        if (!openClipboard(errorMessage)) {
            candidate.kind = ClipboardCaptureCandidate::Kind::ReadFailure;
            return candidate;
        }

        const UINT excludeMonitor = RegisterClipboardFormatW(
            L"ExcludeClipboardContentFromMonitorProcessing");
        const UINT includeHistory = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
        candidate.excludeFromMonitor = excludeMonitor
            && IsClipboardFormatAvailable(excludeMonitor);
        candidate.includeInHistory = ClipboardCaptureCandidate::IncludeInHistory::Missing;
        if (includeHistory && IsClipboardFormatAvailable(includeHistory)) {
            const HANDLE includeHandle = GetClipboardData(includeHistory);
            const SIZE_T includeSize = includeHandle ? GlobalSize(includeHandle) : 0;
            const void *includeData = includeHandle ? GlobalLock(includeHandle) : nullptr;
            if (!includeData || includeSize != sizeof(DWORD)) {
                candidate.includeInHistory = ClipboardCaptureCandidate::IncludeInHistory::Malformed;
            } else {
                DWORD value = 0;
                std::memcpy(&value, includeData, sizeof(value));
                if (value == 0) {
                    candidate.includeInHistory = ClipboardCaptureCandidate::IncludeInHistory::Deny;
                } else if (value == 1) {
                    candidate.includeInHistory = ClipboardCaptureCandidate::IncludeInHistory::Allow;
                } else {
                    candidate.includeInHistory = ClipboardCaptureCandidate::IncludeInHistory::Malformed;
                }
            }
            if (includeData) {
                GlobalUnlock(includeHandle);
            }
        }
        if (candidate.excludeFromMonitor
            || candidate.includeInHistory == ClipboardCaptureCandidate::IncludeInHistory::Deny
            || candidate.includeInHistory == ClipboardCaptureCandidate::IncludeInHistory::Malformed) {
            CloseClipboard();
            candidate.kind = ClipboardCaptureCandidate::Kind::NonText;
            return candidate;
        }
        if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            CloseClipboard();
            candidate.kind = ClipboardCaptureCandidate::Kind::NonText;
            return candidate;
        }
        const HANDLE handle = GetClipboardData(CF_UNICODETEXT);
        const SIZE_T size = handle ? GlobalSize(handle) : 0;
        const void *data = handle ? GlobalLock(handle) : nullptr;
        if (!data || size > static_cast<SIZE_T>(std::numeric_limits<int>::max())) {
            if (data) {
                GlobalUnlock(handle);
            }
            CloseClipboard();
            candidate.kind = ClipboardCaptureCandidate::Kind::ReadFailure;
            return candidate;
        }
        const QByteArray bytes(static_cast<const char *>(data), static_cast<int>(size));
        GlobalUnlock(handle);
        CloseClipboard();
        QString decodeError;
        if (!decodeUnicodeTextBuffer(bytes, &candidate.text, &decodeError)) {
            candidate.kind = decodeError.contains(QStringLiteral("1 MiB"))
                ? ClipboardCaptureCandidate::Kind::Text
                : ClipboardCaptureCandidate::Kind::ReadFailure;
            if (errorMessage) {
                *errorMessage = decodeError;
            }
            return candidate;
        }
        candidate.kind = candidate.text.isEmpty()
            ? ClipboardCaptureCandidate::Kind::Empty
            : ClipboardCaptureCandidate::Kind::Text;
        return candidate;
    }
    bool readText(QString *text, QString *errorMessage) override
    {
        if (!text) {
            return false;
        }
        ++m_nativeAccessAttempts;
        if (!openClipboard(errorMessage)) {
            return false;
        }
        if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            text->clear();
            CloseClipboard();
            return true;
        }
        const HANDLE handle = GetClipboardData(CF_UNICODETEXT);
        const SIZE_T size = handle ? GlobalSize(handle) : 0;
        const void *data = handle ? GlobalLock(handle) : nullptr;
        if (!data || size > static_cast<SIZE_T>(std::numeric_limits<int>::max())) {
            if (data) {
                GlobalUnlock(handle);
            }
            CloseClipboard();
            if (errorMessage) {
                *errorMessage = QStringLiteral("无法读取剪贴板文本");
            }
            return false;
        }
        const QByteArray bytes(static_cast<const char *>(data), static_cast<int>(size));
        GlobalUnlock(handle);
        CloseClipboard();
        return decodeUnicodeTextBuffer(bytes, text, errorMessage);
    }
    bool writeText(const QString &text, QString *errorMessage) override
    {
        ++m_nativeAccessAttempts;
        const SIZE_T byteCount = static_cast<SIZE_T>(text.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, byteCount);
        if (!memory) {
            return false;
        }
        auto *destination = static_cast<wchar_t *>(GlobalLock(memory));
        if (!destination) {
            GlobalFree(memory);
            return false;
        }
        std::memcpy(destination, text.utf16(), static_cast<size_t>(text.size()) * 2);
        destination[text.size()] = L'\0';
        GlobalUnlock(memory);
        if (!openClipboard(errorMessage)) {
            GlobalFree(memory);
            return false;
        }
        if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, memory)) {
            const DWORD error = GetLastError();
            CloseClipboard();
            GlobalFree(memory);
            if (errorMessage) {
                *errorMessage = QStringLiteral("无法写回剪贴板（错误 %1）").arg(error);
            }
            return false;
        }
        CloseClipboard();
        return true;
    }
    bool deliverText(quintptr editorWindowHandle, QString *errorMessage) override
    {
        Q_UNUSED(errorMessage);
        const HWND editor = reinterpret_cast<HWND>(editorWindowHandle);
        const HWND foreground = GetForegroundWindow();
        if (!foreground || foreground == editor || !IsWindow(foreground)) {
            return false;
        }
        keybd_event(VK_CONTROL, 0, 0, 0);
        keybd_event(0x56, 0, 0, 0);
        keybd_event(0x56, 0, KEYEVENTF_KEYUP, 0);
        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
        return true;
    }

private:
    bool openClipboard(QString *errorMessage)
    {
        constexpr int attempts = 6;
        DWORD lastError = ERROR_SUCCESS;
        for (int attempt = 0; attempt < attempts; ++attempt) {
            if (OpenClipboard(m_window)) {
                return true;
            }
            lastError = GetLastError();
            if (attempt + 1 < attempts) {
                QThread::msleep(2);
            }
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("剪贴板正被其他程序占用（错误 %1）").arg(lastError);
        }
        return false;
    }

    HWND m_window = nullptr;
    bool m_monitoring = false;
    quint32 m_baselineSequence = 0;
    quint64 m_nativeAccessAttempts = 0;
};
#endif

} // namespace

std::unique_ptr<ClipboardGateway> ClipboardGateway::create(bool testMode, QObject *parent)
{
    if (testMode) {
        return std::make_unique<MemoryClipboardGateway>(parent);
    }
#ifdef Q_OS_WIN
    return std::make_unique<NativeClipboardGateway>(parent);
#else
    return std::make_unique<MemoryClipboardGateway>(parent);
#endif
}
