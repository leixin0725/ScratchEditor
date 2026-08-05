#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QThread>

#include <functional>
#include <atomic>
#include <string>
#include <thread>

#ifdef Q_OS_WIN
#  include <ole2.h>
#  include <windows.h>
#endif

namespace {

QString serverName()
{
    const QByteArray overrideName = qgetenv("SCRATCHEDITOR_SERVER_NAME");
    return overrideName.isEmpty() ? QStringLiteral("ScratchEditor.Stage1.v1")
                                  : QString::fromUtf8(overrideName);
}

QJsonObject sendRequest(QJsonObject request, int timeoutMs = 3000)
{
    QLocalSocket socket;
    socket.connectToServer(serverName(), QIODevice::ReadWrite);
    if (!socket.waitForConnected(timeoutMs)) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("IPC connect failed: %1")
                                                   .arg(socket.errorString())}};
    }

    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    if (!socket.waitForBytesWritten(timeoutMs)) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("IPC write timed out")}};
    }

    QByteArray response;
    while (!response.contains('\n')) {
        if (!socket.waitForReadyRead(timeoutMs)) {
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("IPC read timed out: %1")
                                                       .arg(socket.errorString())}};
        }
        response += socket.readAll();
    }

    const QJsonDocument document = QJsonDocument::fromJson(response.left(response.indexOf('\n')));
    if (!document.isObject()) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("IPC returned invalid JSON")}};
    }
    return document.object();
}

QJsonObject command(const QString &name, const QJsonObject &arguments = {})
{
    QJsonObject request = arguments;
    request.insert(QStringLiteral("command"), name);
    return sendRequest(request);
}

QJsonObject waitForStatus(const std::function<bool(const QJsonObject &)> &predicate,
                          int timeoutMs = 1500)
{
    const int attempts = qMax(1, timeoutMs / 10);
    QJsonObject status;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        status = command(QStringLiteral("status"));
        if (status.value(QStringLiteral("ok")).toBool() && predicate(status)) {
            return status;
        }
        QThread::msleep(10);
    }
    return status;
}

#ifdef Q_OS_WIN
QString readClipboardText()
{
    if (!OpenClipboard(nullptr)) {
        return {};
    }
    QString result;
    if (const HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto *characters = static_cast<const wchar_t *>(GlobalLock(handle))) {
            result = QString::fromWCharArray(characters);
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return result;
}

bool sendEscape(HWND window)
{
    return PostMessageW(window, WM_KEYDOWN, VK_ESCAPE, 0) != FALSE
        && PostMessageW(window, WM_KEYUP, VK_ESCAPE, 0xC0000001) != FALSE;
}

bool sendCtrlS(HWND window)
{
    Q_UNUSED(window);
    // PostMessage 不会更新全局修饰键状态，Qt 无法识别 Ctrl+S；改用真实注入，
    // 调用前需确保 window 是前台窗口。
    keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event('S', 0, 0, 0);
    keybd_event('S', 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    return true;
}

bool sendCtrlW(HWND window)
{
    Q_UNUSED(window);
    // 与 Ctrl+S 相同，PostMessage 无法携带全局修饰键状态，改用真实注入。
    keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event('W', 0, 0, 0);
    keybd_event('W', 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    return true;
}

void forceForeground(HWND window)
{
    const DWORD currentThread = GetCurrentThreadId();
    const DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    if (foregroundThread && foregroundThread != currentThread) {
        AttachThreadInput(currentThread, foregroundThread, TRUE);
    }
    SetForegroundWindow(window);
    if (foregroundThread && foregroundThread != currentThread) {
        AttachThreadInput(currentThread, foregroundThread, FALSE);
    }
}

void pumpMessages(int durationMs)
{
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + durationMs;
    MSG message{};
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        QThread::msleep(5);
    }
}

class ClipboardLock final
{
public:
    ClipboardLock()
        : m_thread([this] {
            // Clipboard ownership can remain transiently unavailable just after the
            // preceding write. Retry for a bounded interval so this fixture tests
            // the editor's locked-clipboard behavior instead of a one-shot race.
            for (int attempt = 0; attempt < 500 && !m_acquired; ++attempt) {
                m_acquired = OpenClipboard(nullptr) != FALSE;
                if (!m_acquired) {
                    Sleep(1);
                }
            }
            m_ready = true;
            while (!m_release) {
                Sleep(1);
            }
            if (m_acquired) {
                CloseClipboard();
            }
        })
    {
        while (!m_ready) {
            Sleep(1);
        }
    }

    ~ClipboardLock()
    {
        m_release = true;
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    bool acquired() const { return m_acquired; }

private:
    std::atomic_bool m_acquired = false;
    std::atomic_bool m_ready = false;
    std::atomic_bool m_release = false;
    std::thread m_thread;
};
#endif

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QJsonObject checks;
    QJsonObject details;

    const QJsonObject initialStatus = command(QStringLiteral("status"));
    if (!initialStatus.value(QStringLiteral("ok")).toBool()
        || !initialStatus.value(QStringLiteral("testMode")).toBool()) {
        const QJsonObject failure{
            {QStringLiteral("allPassed"), false},
            {QStringLiteral("error"), QStringLiteral("A test-mode ScratchEditor must be running")},
            {QStringLiteral("status"), initialStatus},
        };
        fputs(QJsonDocument(failure).toJson(QJsonDocument::Indented).constData(), stdout);
        return 2;
    }

#ifdef Q_OS_WIN
    const HRESULT oleResult = OleInitialize(nullptr);
    IDataObject *originalClipboard = nullptr;
    if (SUCCEEDED(oleResult)) {
        OleGetClipboard(&originalClipboard);
    }

    const DWORD editorPid = static_cast<DWORD>(initialStatus.value(QStringLiteral("pid")).toInteger());
    AllowSetForegroundWindow(editorPid);
    const HWND focusWindow = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC",
                                             L"ScratchEditorFocusRestoreTest",
                                             WS_OVERLAPPEDWINDOW, 80, 80, 320, 180,
                                             nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    const HWND recentWindow = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC",
                                              L"ScratchEditorRecentFocusTest",
                                              WS_OVERLAPPEDWINDOW, 420, 80, 320, 180,
                                              nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    const HWND editWindow = CreateWindowExW(WS_EX_TOOLWINDOW, L"EDIT", L"",
                                            WS_OVERLAPPEDWINDOW | ES_MULTILINE
                                                | ES_AUTOVSCROLL,
                                            80, 300, 320, 180,
                                            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    const QJsonObject shown = command(QStringLiteral("show"));
    checks.insert(QStringLiteral("showReady"), shown.value(QStringLiteral("visible")).toBool());

    command(QStringLiteral("testSetText"), {{QStringLiteral("text"), QStringLiteral("短文本")}});
    const QJsonObject shortStatus = waitForStatus([](const QJsonObject &status) {
        return !status.value(QStringLiteral("verticalScrollBarVisible")).toBool();
    });

    QString largeText;
    largeText.reserve(100000);
    const QString line = QStringLiteral("智能滚动条测试 abcdefghijklmnopqrstuvwxyz 0123456789\n");
    while (largeText.size() < 100000) {
        largeText += line;
    }
    largeText.truncate(100000);
    command(QStringLiteral("testSetText"), {{QStringLiteral("text"), largeText}});
    const QJsonObject largeStatus = waitForStatus([](const QJsonObject &status) {
        return status.value(QStringLiteral("verticalScrollBarVisible")).toBool();
    });
    checks.insert(QStringLiteral("smartScrollBar"),
                  !shortStatus.value(QStringLiteral("verticalScrollBarVisible")).toBool()
                      && largeStatus.value(QStringLiteral("verticalScrollBarVisible")).toBool());
    details.insert(QStringLiteral("shortDocumentStatus"), shortStatus);
    details.insert(QStringLiteral("largeDocumentStatus"), largeStatus);

    const QString escapeText = QStringLiteral("系统回归 Escape 写回测试：中文");
    command(QStringLiteral("testSetText"), {{QStringLiteral("text"), escapeText}});
    const HWND editorWindow = reinterpret_cast<HWND>(
        shown.value(QStringLiteral("hwnd")).toString().toULongLong());
    SetForegroundWindow(editorWindow);
    QThread::msleep(50);
    const bool escapeSent = sendEscape(editorWindow);
    const QJsonObject escapedStatus = waitForStatus([](const QJsonObject &status) {
        return !status.value(QStringLiteral("visible")).toBool();
    });
    const QString escapedClipboard = readClipboardText();
    checks.insert(QStringLiteral("escapeClosesAndCopies"),
                  escapeSent && !escapedStatus.value(QStringLiteral("visible")).toBool()
                      && escapedClipboard == escapeText);
    details.insert(QStringLiteral("escapeStatus"), escapedStatus);

    ShowWindow(focusWindow, SW_SHOWNORMAL);
    forceForeground(focusWindow);
    QThread::msleep(75);
    const HWND focusBeforeShow = GetForegroundWindow();
    command(QStringLiteral("show"));
    // 普通模式不应记录打开编辑器之前的窗口。
    const QJsonObject shownFocusStatus = command(QStringLiteral("status"));
    // 编辑器打开后把焦点切到另一个最近活跃的窗口，模拟用户在编辑期间切换窗口。
    ShowWindow(recentWindow, SW_SHOWNORMAL);
    forceForeground(recentWindow);
    QThread::msleep(75);
    const QJsonObject hiddenAfterFocus = command(QStringLiteral("hide"));
    waitForStatus([](const QJsonObject &status) {
        return !status.value(QStringLiteral("visible")).toBool()
            && !status.value(QStringLiteral("windowTransitionActive")).toBool();
    });
    // 窗口隐藏后给系统的前台切换留出稳定时间，再读取实际前台。
    QThread::msleep(300);
    const QJsonObject focusStatus = command(QStringLiteral("status"));
    const HWND focusAfterHide = reinterpret_cast<HWND>(
        focusStatus.value(QStringLiteral("foregroundHwnd")).toString().toULongLong());
    checks.insert(QStringLiteral("focusNotPulledBack"),
                  shownFocusStatus.value(QStringLiteral("previousForegroundHwnd")).toString()
                          == QStringLiteral("0")
                      && focusBeforeShow && focusAfterHide != focusBeforeShow
                      && !hiddenAfterFocus.value(QStringLiteral("visible")).toBool());
    details.insert(QStringLiteral("focusBeforeShow"),
                   QString::number(reinterpret_cast<quintptr>(focusBeforeShow)));
    details.insert(QStringLiteral("recentWindow"),
                   QString::number(reinterpret_cast<quintptr>(recentWindow)));
    details.insert(QStringLiteral("focusAfterHide"),
                   QString::number(reinterpret_cast<quintptr>(focusAfterHide)));

    const QString deliverText = QStringLiteral("Ctrl+S 输入回归测试：中文");
    command(QStringLiteral("show"));
    command(QStringLiteral("testSetText"), {{QStringLiteral("text"), deliverText}});
    ShowWindow(editWindow, SW_SHOWNORMAL);
    forceForeground(editWindow);
    SetFocus(editWindow);
    QThread::msleep(75);
    forceForeground(editorWindow);
    QThread::msleep(50);
    const bool ctrlSSent = sendCtrlS(editorWindow);
    const QJsonObject deliverStatus = waitForStatus([](const QJsonObject &status) {
        return !status.value(QStringLiteral("visible")).toBool();
    });
    // 等待编辑器隐藏后的前台切换和 Ctrl+V 输入落到目标窗口。
    pumpMessages(400);
    const int deliveredLength = GetWindowTextLengthW(editWindow);
    std::wstring deliveredBuffer(static_cast<size_t>(deliveredLength) + 1, L'\0');
    GetWindowTextW(editWindow, deliveredBuffer.data(),
                   static_cast<int>(deliveredBuffer.size()));
    const QString deliveredText = QString::fromWCharArray(deliveredBuffer.data());
    const QString deliveredClipboard = readClipboardText();
    checks.insert(QStringLiteral("ctrlSClosesAndDelivers"),
                  ctrlSSent && !deliverStatus.value(QStringLiteral("visible")).toBool()
                      && deliveredText == deliverText
                      && deliveredClipboard == deliverText);
    details.insert(QStringLiteral("ctrlSDeliveredText"), deliveredText);
    details.insert(QStringLiteral("ctrlSDeliveredClipboard"), deliveredClipboard);
    details.insert(QStringLiteral("ctrlSDeliverStatus"), deliverStatus);
    details.insert(QStringLiteral("ctrlSEditWindow"),
                   QString::number(reinterpret_cast<quintptr>(editWindow)));
    const QJsonObject afterDeliverStatus = command(QStringLiteral("status"));
    details.insert(QStringLiteral("ctrlSForegroundAfterHide"),
                   afterDeliverStatus.value(QStringLiteral("foregroundHwnd")).toString());

    const QString clipboardBeforeDiscard = readClipboardText();
    const QString discardText = QStringLiteral("Ctrl+W 关闭不保存回归测试：中文");
    command(QStringLiteral("show"));
    command(QStringLiteral("testSetText"), {{QStringLiteral("text"), discardText}});
    SetForegroundWindow(editorWindow);
    QThread::msleep(50);
    const bool ctrlWSent = sendCtrlW(editorWindow);
    const QJsonObject discardedStatus = waitForStatus([](const QJsonObject &status) {
        return !status.value(QStringLiteral("visible")).toBool();
    });
    const QString discardedClipboard = readClipboardText();
    checks.insert(QStringLiteral("ctrlWClosesWithoutSaving"),
                  ctrlWSent && !discardedStatus.value(QStringLiteral("visible")).toBool()
                      && discardedClipboard == clipboardBeforeDiscard);
    details.insert(QStringLiteral("ctrlWStatus"), discardedStatus);
    details.insert(QStringLiteral("ctrlWClipboardBefore"), clipboardBeforeDiscard);
    details.insert(QStringLiteral("ctrlWClipboardAfter"), discardedClipboard);

    // 无前台焦点环境下键盘注入不可用；用测试专用 IPC 再次确定性验证
    // Ctrl+W 语义：窗口关闭且不回写剪贴板。
    command(QStringLiteral("show"));
    command(QStringLiteral("testSetText"), {{QStringLiteral("text"), discardText}});
    const QJsonObject discardedViaIpc = command(QStringLiteral("testDiscardClose"));
    const QString discardedViaIpcClipboard = readClipboardText();
    checks.insert(QStringLiteral("ctrlWIpcClosesWithoutSaving"),
                  !discardedViaIpc.value(QStringLiteral("visible")).toBool()
                      && discardedViaIpcClipboard == clipboardBeforeDiscard);
    details.insert(QStringLiteral("ctrlWIpcStatus"), discardedViaIpc);
    details.insert(QStringLiteral("ctrlWIpcClipboardAfter"), discardedViaIpcClipboard);

    command(QStringLiteral("show"));
    const QString lockedWriteText = QStringLiteral("系统回归剪贴板写入异常保留内容");
    command(QStringLiteral("testSetText"), {{QStringLiteral("text"), lockedWriteText}});
    QJsonObject failedHide;
    bool writeLockAcquired = false;
    {
        ClipboardLock lock;
        writeLockAcquired = lock.acquired();
        failedHide = command(QStringLiteral("hide"));
    }
    checks.insert(QStringLiteral("clipboardWriteFailureKeepsEditor"),
                  writeLockAcquired && failedHide.value(QStringLiteral("visible")).toBool()
                      && !failedHide.value(QStringLiteral("clipboardHealthy")).toBool()
                      && !failedHide.value(QStringLiteral("statusMessage")).toString().isEmpty());
    details.insert(QStringLiteral("failedWriteStatus"), failedHide);

    const QJsonObject recoveredHide = command(QStringLiteral("hide"));
    checks.insert(QStringLiteral("clipboardWriteRecovers"),
                  !recoveredHide.value(QStringLiteral("visible")).toBool()
                      && recoveredHide.value(QStringLiteral("clipboardHealthy")).toBool()
                      && readClipboardText() == lockedWriteText);

    QJsonObject failedReadShow;
    bool readLockAcquired = false;
    {
        ClipboardLock lock;
        readLockAcquired = lock.acquired();
        failedReadShow = command(QStringLiteral("show"));
    }
    checks.insert(QStringLiteral("clipboardReadFailureIsVisible"),
                  readLockAcquired && failedReadShow.value(QStringLiteral("visible")).toBool()
                      && !failedReadShow.value(QStringLiteral("clipboardHealthy")).toBool()
                      && !failedReadShow.value(QStringLiteral("statusMessage")).toString().isEmpty());
    details.insert(QStringLiteral("failedReadStatus"), failedReadShow);

    command(QStringLiteral("hide"));
    DestroyWindow(focusWindow);
    DestroyWindow(recentWindow);
    DestroyWindow(editWindow);

    if (originalClipboard) {
        if (SUCCEEDED(OleSetClipboard(originalClipboard))) {
            OleFlushClipboard();
        }
        originalClipboard->Release();
    }
    if (SUCCEEDED(oleResult)) {
        OleUninitialize();
    }
#else
    checks.insert(QStringLiteral("platform"), false);
#endif

    bool allPassed = true;
    for (auto it = checks.constBegin(); it != checks.constEnd(); ++it) {
        allPassed = allPassed && it.value().toBool();
    }

    const QJsonObject result{
        {QStringLiteral("allPassed"), allPassed},
        {QStringLiteral("checks"), checks},
        {QStringLiteral("details"), details},
    };
    fputs(QJsonDocument(result).toJson(QJsonDocument::Indented).constData(), stdout);
    return allPassed ? 0 : 1;
}
