#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {

#ifdef Q_OS_WIN
struct FindWindowContext
{
    DWORD pid = 0;
    HWND hwnd = nullptr;
};

BOOL CALLBACK findWindowByPid(HWND hwnd, LPARAM lParam)
{
    auto *context = reinterpret_cast<FindWindowContext *>(lParam);
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid == context->pid && IsWindowVisible(hwnd)) {
        context->hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND findTopLevelWindow(DWORD pid)
{
    FindWindowContext context;
    context.pid = pid;
    EnumWindows(findWindowByPid, reinterpret_cast<LPARAM>(&context));
    return context.hwnd;
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
#endif

bool check(bool condition, const QString &message)
{
    if (!condition) {
        QTextStream(stderr) << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

bool writeText(const QString &path, const QString &text)
{
    QFile file(path);
    const QByteArray bytes = text.toUtf8();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

bool runInvalidArgumentsTest(const QString &editor, const QStringList &arguments,
                             const QString &description)
{
    QProcess process;
    process.setProgram(editor);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted(5000) || !process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(1000);
        return check(false, description + QStringLiteral("：进程未及时结束"));
    }
    return check(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 4,
                 description + QStringLiteral("：应以参数错误状态 4 退出，实际为 %1")
                                   .arg(process.exitCode()));
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QString editor = app.arguments().size() > 1
        ? QFileInfo(app.arguments().at(1)).absoluteFilePath()
        : QDir(QCoreApplication::applicationDirPath()).filePath(
              QStringLiteral("ScratchEditor.exe"));

    if (!check(QFileInfo::exists(editor),
               QStringLiteral("ScratchEditor executable does not exist: %1").arg(editor))) {
        return 1;
    }

    QTemporaryDir temporaryDirectory(
        QDir::tempPath() + QStringLiteral("/ScratchEditor CLI process test-XXXXXX"));
    if (!check(temporaryDirectory.isValid(), QStringLiteral("无法创建 CLI 生命周期测试目录"))) {
        return 1;
    }

    int failures = 0;
    QProcessEnvironment residentEnvironment = QProcessEnvironment::systemEnvironment();
    residentEnvironment.insert(
        QStringLiteral("SCRATCHEDITOR_SERVER_NAME"),
        QStringLiteral("ScratchEditor.ExternalProcessTests.%1")
            .arg(QCoreApplication::applicationPid()));
    QProcess residentProcess;
    residentProcess.setProcessEnvironment(residentEnvironment);
    residentProcess.setProgram(editor);
    residentProcess.setArguments({QStringLiteral("--test-mode"), QStringLiteral("--background")});
    residentProcess.start();
    failures += !check(residentProcess.waitForStarted(5000),
                       QStringLiteral("隔离的常驻编辑器进程无法启动"));

    const QString editedFile = temporaryDirectory.filePath(
        QStringLiteral("Codex pi 临时提示 with spaces.md"));
    const QString secondEditedFile = temporaryDirectory.filePath(
        QStringLiteral("second concurrent prompt.md"));
    failures += !check(writeText(editedFile, QStringLiteral("# original\n")),
                       QStringLiteral("无法创建 CLI 生命周期夹具"));
    failures += !check(writeText(secondEditedFile, QStringLiteral("second original\n")),
                       QStringLiteral("无法创建第二个 CLI 生命周期夹具"));

    const QString replacementText = QStringLiteral(
        "# CLI edited\n\nUnicode: 你好 🧪\n");
    const QString secondReplacementText = QStringLiteral("second session result\n");
    QProcess process;
    QProcess secondProcess;
    QProcessEnvironment environment = residentEnvironment;
    environment.insert(QStringLiteral("SCRATCHEDITOR_EXTERNAL_TEST_TEXT"), replacementText);
    process.setProcessEnvironment(environment);
    process.setProgram(editor);
    process.setArguments({QStringLiteral("--test-mode"), QStringLiteral("--wait"), editedFile});
    QProcessEnvironment secondEnvironment = residentEnvironment;
    secondEnvironment.insert(QStringLiteral("SCRATCHEDITOR_EXTERNAL_TEST_TEXT"),
                             secondReplacementText);
    secondProcess.setProcessEnvironment(secondEnvironment);
    secondProcess.setProgram(editor);
    secondProcess.setArguments(
        {QStringLiteral("--test-mode"), QStringLiteral("--wait"), secondEditedFile});
    process.start();
    secondProcess.start();

    failures += !check(process.waitForStarted(5000),
                       QStringLiteral("ScratchEditor 外部文件进程无法启动：%1")
                           .arg(process.errorString()));
    failures += !check(secondProcess.waitForStarted(5000),
                       QStringLiteral("第二个外部文件进程无法启动：%1")
                           .arg(secondProcess.errorString()));
    if (process.state() != QProcess::NotRunning
        && secondProcess.state() != QProcess::NotRunning) {
        QThread::msleep(150);
        failures += !check(process.state() != QProcess::NotRunning,
                           QStringLiteral("外部编辑进程未等待编辑完成便提前退出"));
        failures += !check(secondProcess.state() != QProcess::NotRunning,
                           QStringLiteral("并发外部编辑进程未等待编辑完成便提前退出"));
        failures += !check(process.waitForFinished(10000),
                           QStringLiteral("外部编辑进程未在测试保存后退出"));
        failures += !check(secondProcess.waitForFinished(10000),
                           QStringLiteral("并发外部编辑进程未在测试保存后退出"));
    }
    failures += !check(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
                       QStringLiteral("外部编辑进程应正常退出，实际退出码为 %1")
                           .arg(process.exitCode()));
    failures += !check(readText(editedFile) == replacementText,
                       QStringLiteral("外部编辑进程未按 UTF-8 写回完整内容"));
    failures += !check(secondProcess.exitStatus() == QProcess::NormalExit
                           && secondProcess.exitCode() == 0,
                       QStringLiteral("并发外部编辑进程应正常退出，实际退出码为 %1")
                           .arg(secondProcess.exitCode()));
    failures += !check(readText(secondEditedFile) == secondReplacementText,
                       QStringLiteral("两个并发文件会话发生覆盖或丢失"));
    failures += !check(residentProcess.state() != QProcess::NotRunning,
                       QStringLiteral("外部文件会话不得结束或复用既有常驻实例"));

#ifdef Q_OS_WIN
    const QString ctrlSFile = temporaryDirectory.filePath(
        QStringLiteral("ctrl-s-save-and-exit.md"));
    failures += !check(writeText(ctrlSFile, QStringLiteral("# ctrl-s original\n")),
                       QStringLiteral("无法创建 Ctrl+S 生命周期夹具"));
    QProcess ctrlSProcess;
    ctrlSProcess.setProcessEnvironment(residentEnvironment);
    ctrlSProcess.setProgram(editor);
    ctrlSProcess.setArguments(
        {QStringLiteral("--test-mode"), QStringLiteral("--wait"), ctrlSFile});
    ctrlSProcess.start();
    failures += !check(ctrlSProcess.waitForStarted(5000),
                       QStringLiteral("Ctrl+S 外部编辑进程无法启动"));
    HWND ctrlSWindow = nullptr;
    for (int attempt = 0; attempt < 200 && !ctrlSWindow; ++attempt) {
        ctrlSWindow = findTopLevelWindow(static_cast<DWORD>(ctrlSProcess.processId()));
        if (!ctrlSWindow) {
            QThread::msleep(25);
        }
    }
    failures += !check(ctrlSWindow != nullptr,
                       QStringLiteral("Ctrl+S 外部编辑窗口未在预期时间内出现"));
    if (ctrlSWindow) {
        forceForeground(ctrlSWindow);
        QThread::msleep(50);
        const bool ctrlSSent = sendCtrlS(ctrlSWindow);
        failures += !check(ctrlSProcess.waitForFinished(10000),
                           QStringLiteral("Ctrl+S 后外部编辑进程未保存并退出"));
        failures += !check(
            ctrlSSent && ctrlSProcess.exitStatus() == QProcess::NormalExit
                && ctrlSProcess.exitCode() == 0,
            QStringLiteral("Ctrl+S 应保存并正常退出，实际退出码为 %1")
                .arg(ctrlSProcess.exitCode()));
        failures += !check(readText(ctrlSFile) == QStringLiteral("# ctrl-s original\n"),
                           QStringLiteral("Ctrl+S 未按 UTF-8 保存文件内容"));
    } else {
        ctrlSProcess.kill();
        ctrlSProcess.waitForFinished(5000);
    }
#endif

    if (residentProcess.state() != QProcess::NotRunning) {
        residentProcess.kill();
        residentProcess.waitForFinished(5000);
    }

    const QString removedParent = temporaryDirectory.filePath(
        QStringLiteral("removed-before-save"));
    QDir().mkpath(removedParent);
    const QString unsavableFile = QDir(removedParent).filePath(QStringLiteral("prompt.md"));
    failures += !check(writeText(unsavableFile, QStringLiteral("unsaved original\n")),
                       QStringLiteral("无法创建保存失败夹具"));
    QProcess unsavableProcess;
    QProcessEnvironment unsavableEnvironment = QProcessEnvironment::systemEnvironment();
    unsavableEnvironment.insert(QStringLiteral("SCRATCHEDITOR_EXTERNAL_TEST_TEXT"),
                                QStringLiteral("must remain in editor\n"));
    unsavableProcess.setProcessEnvironment(unsavableEnvironment);
    unsavableProcess.setProgram(editor);
    unsavableProcess.setArguments(
        {QStringLiteral("--test-mode"), QStringLiteral("--wait"), unsavableFile});
    unsavableProcess.start();
    failures += !check(unsavableProcess.waitForStarted(5000),
                       QStringLiteral("保存失败生命周期进程无法启动"));
    QThread::msleep(150);
    failures += !check(QFile::remove(unsavableFile) && QDir().rmdir(removedParent),
                       QStringLiteral("无法移除保存目标目录以触发保存失败"));
    QThread::msleep(900);
    failures += !check(unsavableProcess.state() != QProcess::NotRunning,
                       QStringLiteral("保存失败时外部编辑器不得伪报成功并退出"));
    if (unsavableProcess.state() != QProcess::NotRunning) {
        unsavableProcess.kill();
        unsavableProcess.waitForFinished(5000);
    }

    failures += !runInvalidArgumentsTest(editor, {QStringLiteral("--wait")},
                                         QStringLiteral("--wait 缺少文件"));
    failures += !runInvalidArgumentsTest(
        editor, {temporaryDirectory.filePath(QStringLiteral("missing.md"))},
        QStringLiteral("文件不存在"));

    if (failures != 0) {
        QTextStream(stderr) << failures << " external-editor process test(s) failed"
                            << Qt::endl;
        return 1;
    }

    QTextStream(stdout) << "All external-editor process tests passed." << Qt::endl;
    return 0;
}
