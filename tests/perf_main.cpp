#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QThread>
#include <QtMath>

#include <algorithm>
#include <numeric>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {

QString serverName()
{
    const QByteArray overrideName = qgetenv("SCRATCHEDITOR_SERVER_NAME");
    return overrideName.isEmpty() ? QStringLiteral("ScratchEditor.Perf.Validation")
                                  : QString::fromUtf8(overrideName);
}

struct CommandResult {
    QJsonObject object;
    double externalMs = 0.0;
    QString error;
};

CommandResult readResponse(QLocalSocket &socket, QElapsedTimer &timer, int timeoutMs)
{
    while (!socket.canReadLine() && timer.elapsed() < timeoutMs) {
        socket.waitForReadyRead(qMax(1, timeoutMs - static_cast<int>(timer.elapsed())));
    }

    CommandResult result;
    result.externalMs = timer.nsecsElapsed() / 1'000'000.0;
    if (!socket.canReadLine()) {
        result.error = QStringLiteral("response timeout: %1").arg(socket.errorString());
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(socket.readLine().trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("invalid JSON response");
        return result;
    }
    result.object = document.object();
    return result;
}

CommandResult sendCommand(const QString &command, int timeoutMs = 3000)
{
    QLocalSocket socket;
    QElapsedTimer timer;
    timer.start();
#ifdef Q_OS_WIN
    LARGE_INTEGER clientQpc{};
    QueryPerformanceCounter(&clientQpc);
#endif
    socket.connectToServer(serverName());
    if (!socket.waitForConnected(timeoutMs)) {
        return {{}, timer.nsecsElapsed() / 1'000'000.0, socket.errorString()};
    }

    QJsonObject request;
    request.insert(QStringLiteral("command"), command);
#ifdef Q_OS_WIN
    request.insert(QStringLiteral("clientQpc"), QString::number(clientQpc.QuadPart));
#endif
    const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    if (socket.write(payload) != payload.size()) {
        return {{}, timer.nsecsElapsed() / 1'000'000.0, socket.errorString()};
    }
    if (socket.bytesToWrite() > 0 && !socket.waitForBytesWritten(timeoutMs)) {
        return {{}, timer.nsecsElapsed() / 1'000'000.0, socket.errorString()};
    }
    return readResponse(socket, timer, timeoutMs);
}

double percentile(QVector<double> values, double fraction)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const qsizetype index = qBound<qsizetype>(
        0, static_cast<qsizetype>(qCeil(fraction * values.size()) - 1), values.size() - 1);
    return values.at(index);
}

QJsonObject summarize(const QVector<double> &values)
{
    QJsonObject result;
    result.insert(QStringLiteral("samples"), values.size());
    if (values.isEmpty()) {
        return result;
    }
    const double sum = std::accumulate(values.cbegin(), values.cend(), 0.0);
    result.insert(QStringLiteral("averageMs"), sum / values.size());
    result.insert(QStringLiteral("p50Ms"), percentile(values, 0.50));
    result.insert(QStringLiteral("p95Ms"), percentile(values, 0.95));
    result.insert(QStringLiteral("maxMs"), percentile(values, 1.0));
    return result;
}

#ifdef Q_OS_WIN
bool sendUnicodeCharacter(wchar_t character)
{
    INPUT events[2]{};
    events[0].type = INPUT_KEYBOARD;
    events[0].ki.wScan = character;
    events[0].ki.dwFlags = KEYEVENTF_UNICODE;
    events[1] = events[0];
    events[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    return SendInput(2, events, sizeof(INPUT)) == 2;
}

bool sendVirtualKey(WORD virtualKey)
{
    INPUT events[2]{};
    events[0].type = INPUT_KEYBOARD;
    events[0].ki.wVk = virtualKey;
    events[1] = events[0];
    events[1].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(2, events, sizeof(INPUT)) == 2;
}

bool sendWinSpace()
{
    INPUT events[4]{};
    events[0].type = INPUT_KEYBOARD;
    events[0].ki.wVk = VK_LWIN;
    events[1].type = INPUT_KEYBOARD;
    events[1].ki.wVk = VK_SPACE;
    events[2] = events[1];
    events[2].ki.dwFlags = KEYEVENTF_KEYUP;
    events[3] = events[0];
    events[3].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(4, events, sizeof(INPUT)) == 4;
}

bool sendPinyinNihao()
{
    const WORD keys[] = {'N', 'I', 'H', 'A', 'O', VK_SPACE};
    for (const WORD key : keys) {
        if (!sendVirtualKey(key)) {
            return false;
        }
        QThread::msleep(50);
    }
    return true;
}
#endif

QJsonObject runHotBenchmark(int sampleCount)
{
    QVector<double> external;
    QVector<double> clientToFrame;
    QVector<double> server;
    QString error;

    QLocalSocket persistentSocket;
    persistentSocket.connectToServer(serverName());
    if (!persistentSocket.waitForConnected(1000)) {
        QJsonObject result;
        result.insert(QStringLiteral("passed"), false);
        result.insert(QStringLiteral("error"), persistentSocket.errorString());
        return result;
    }

    const auto persistentCommand = [&persistentSocket](const QString &command) {
        QElapsedTimer timer;
        timer.start();
        QJsonObject request;
        request.insert(QStringLiteral("command"), command);
#ifdef Q_OS_WIN
        LARGE_INTEGER clientQpc{};
        QueryPerformanceCounter(&clientQpc);
        request.insert(QStringLiteral("clientQpc"), QString::number(clientQpc.QuadPart));
#endif
        const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        if (persistentSocket.write(payload) != payload.size()) {
            return CommandResult{{}, timer.nsecsElapsed() / 1'000'000.0,
                                 persistentSocket.errorString()};
        }
        // readResponse() drives the local socket until the reply arrives, which also
        // flushes pending request bytes. An extra waitForBytesWritten() here can sleep
        // until a delayed notifier even after the named pipe has accepted the request,
        // inflating the client-to-frame measurement without corresponding server work.
        persistentSocket.flush();
        return readResponse(persistentSocket, timer, 3000);
    };

    persistentCommand(QStringLiteral("hide"));
    for (int i = 0; i < sampleCount; ++i) {
        const CommandResult shown = persistentCommand(QStringLiteral("show"));
        if (!shown.error.isEmpty() || !shown.object.value(QStringLiteral("ok")).toBool()) {
            error = shown.error.isEmpty() ? shown.object.value(QStringLiteral("error")).toString()
                                          : shown.error;
            break;
        }
        external.append(shown.externalMs);
        clientToFrame.append(shown.object.value(QStringLiteral("clientToFrameMs")).toDouble());
        server.append(shown.object.value(QStringLiteral("frameMs")).toDouble());
        const CommandResult hidden = persistentCommand(QStringLiteral("hide"));
        if (!hidden.error.isEmpty()) {
            error = hidden.error;
            break;
        }
    }

    QJsonObject result;
    result.insert(QStringLiteral("external"), summarize(external));
    result.insert(QStringLiteral("clientToFrame"), summarize(clientToFrame));
    result.insert(QStringLiteral("serverToFrame"), summarize(server));
    result.insert(QStringLiteral("thresholdMs"), 50.0);
    result.insert(QStringLiteral("passed"), !clientToFrame.isEmpty()
                      && percentile(clientToFrame, 0.95) <= 50.0);
    if (!error.isEmpty()) {
        result.insert(QStringLiteral("error"), error);
    }
    persistentSocket.disconnectFromServer();
    return result;
}

QJsonObject runInputBenchmark(int sampleCount, quintptr hwnd)
{
    QVector<double> external;
    QVector<double> internal;
    QString error;

#ifdef Q_OS_WIN
    SetForegroundWindow(reinterpret_cast<HWND>(hwnd));
#endif
    for (int i = 0; i < sampleCount; ++i) {
        QLocalSocket socket;
        socket.connectToServer(serverName());
        if (!socket.waitForConnected(1000)) {
            error = socket.errorString();
            break;
        }

        QJsonObject request;
        request.insert(QStringLiteral("command"), QStringLiteral("awaitInputFrame"));
        const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        if (socket.write(payload) != payload.size()) {
            error = socket.errorString();
            break;
        }
        if (socket.bytesToWrite() > 0 && !socket.waitForBytesWritten(1000)) {
            error = socket.errorString();
            break;
        }

        // Cover the whole refresh cycle instead of repeatedly sampling the same
        // phase immediately after the previous frame response.
        QThread::msleep(1 + ((i * 7) % 16));
        QElapsedTimer timer;
        timer.start();
#ifdef Q_OS_WIN
        if (!sendUnicodeCharacter(static_cast<wchar_t>(L'测' + (i % 2)))) {
            error = QStringLiteral("SendInput failed");
            break;
        }
#endif
        CommandResult response = readResponse(socket, timer, 2000);
        if (!response.error.isEmpty() || !response.object.value(QStringLiteral("ok")).toBool()) {
            error = response.error.isEmpty() ? response.object.value(QStringLiteral("error")).toString()
                                             : response.error;
            break;
        }
        external.append(response.externalMs);
        internal.append(response.object.value(QStringLiteral("inputFrameMs")).toDouble());
    }

    QJsonObject result;
    result.insert(QStringLiteral("externalSendInputToResponse"), summarize(external));
    result.insert(QStringLiteral("eventToFrame"), summarize(internal));
    result.insert(QStringLiteral("thresholdMs"), 16.667);
    result.insert(QStringLiteral("passed"), !internal.isEmpty() && percentile(internal, 0.95) <= 16.667);
    if (!error.isEmpty()) {
        result.insert(QStringLiteral("error"), error);
    }
    return result;
}

QJsonObject commandObject(const QString &command, int timeoutMs = 3000)
{
    const CommandResult result = sendCommand(command, timeoutMs);
    QJsonObject object = result.object;
    object.insert(QStringLiteral("externalMs"), result.externalMs);
    if (!result.error.isEmpty()) {
        object.insert(QStringLiteral("ok"), false);
        object.insert(QStringLiteral("clientError"), result.error);
    }
    return object;
}

QJsonObject runSystemImeBenchmark(quintptr hwnd)
{
    QJsonArray attempts;
    bool passed = false;
    QString committedText;

#ifdef Q_OS_WIN
    SetForegroundWindow(reinterpret_cast<HWND>(hwnd));
    commandObject(QStringLiteral("clearTestText"));
    QThread::msleep(500);

    const auto attempt = [&](const QString &label) {
        SetForegroundWindow(reinterpret_cast<HWND>(hwnd));
        const bool injected = sendPinyinNihao();
        QThread::msleep(500);
        const QJsonObject textResponse = commandObject(QStringLiteral("testText"));
        const QString text = textResponse.value(QStringLiteral("text")).toString();
        QJsonObject item;
        item.insert(QStringLiteral("mode"), label);
        item.insert(QStringLiteral("injected"), injected);
        item.insert(QStringLiteral("text"), text);
        const bool exactCommit = text.trimmed() == QStringLiteral("你好");
        item.insert(QStringLiteral("committedChinese"), text.contains(QStringLiteral("你好")));
        item.insert(QStringLiteral("exactCommit"), exactCommit);
        attempts.append(item);
        if (exactCommit) {
            passed = true;
            committedText = text;
        }
    };

    attempt(QStringLiteral("current input method"));
    if (!passed) {
        commandObject(QStringLiteral("clearTestText"));
        QThread::msleep(300);
        attempt(QStringLiteral("current input method retry after IME initialization"));
    }
    if (!passed) {
        commandObject(QStringLiteral("clearTestText"));
        sendVirtualKey(VK_SHIFT);
        QThread::msleep(250);
        attempt(QStringLiteral("current input method after Shift mode toggle"));
        sendVirtualKey(VK_SHIFT);
        QThread::msleep(150);
    }
    if (!passed) {
        commandObject(QStringLiteral("clearTestText"));
        sendWinSpace();
        QThread::msleep(500);
        attempt(QStringLiteral("next input method"));
        if (!passed) {
            commandObject(QStringLiteral("clearTestText"));
            sendVirtualKey(VK_SHIFT);
            QThread::msleep(250);
            attempt(QStringLiteral("next input method after Shift mode toggle"));
            sendVirtualKey(VK_SHIFT);
            QThread::msleep(150);
        }
        sendWinSpace();
        QThread::msleep(300);
    }
#endif

    commandObject(QStringLiteral("clearTestText"));
    QJsonObject result;
    result.insert(QStringLiteral("installedIme"), QStringLiteral("Microsoft Pinyin / zh-Hans-CN"));
    result.insert(QStringLiteral("attempts"), attempts);
    result.insert(QStringLiteral("committedText"), committedText);
    result.insert(QStringLiteral("passed"), passed);
    return result;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    int hotSamples = 40;
    int inputSamples = 60;
    for (int i = 1; i + 1 < arguments.size(); ++i) {
        if (arguments.at(i) == QStringLiteral("--hot-samples")) {
            hotSamples = arguments.at(i + 1).toInt();
        } else if (arguments.at(i) == QStringLiteral("--input-samples")) {
            inputSamples = arguments.at(i + 1).toInt();
        }
    }

    QJsonObject report;
    report.insert(QStringLiteral("statusBefore"), commandObject(QStringLiteral("status")));
    report.insert(QStringLiteral("hotWake"), runHotBenchmark(hotSamples));

    const QJsonObject shown = commandObject(QStringLiteral("show"));
    report.insert(QStringLiteral("showForTests"), shown);
    const quintptr hwnd = shown.value(QStringLiteral("hwnd")).toString().toULongLong();
    report.insert(QStringLiteral("systemIme"), runSystemImeBenchmark(hwnd));
    report.insert(QStringLiteral("ime"), commandObject(QStringLiteral("benchmarkIme")));
    report.insert(QStringLiteral("animation"),
                  commandObject(QStringLiteral("benchmarkAnimation"), 5000));
    report.insert(QStringLiteral("largeDocument"),
                  commandObject(QStringLiteral("benchmarkLargeDocument"), 5000));
    report.insert(QStringLiteral("largeDocumentInput"), runInputBenchmark(inputSamples, hwnd));
    report.insert(QStringLiteral("restore"), commandObject(QStringLiteral("restoreTestDocument")));
    report.insert(QStringLiteral("statusAfter"), commandObject(QStringLiteral("status")));

    const QByteArray output = QJsonDocument(report).toJson(QJsonDocument::Indented);
    fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout);
    return 0;
}
