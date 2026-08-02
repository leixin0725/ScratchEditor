#include "editorcontroller.h"
#include "appsettings.h"
#include "editorcommandregistry.h"
#include "markdownhighlighter.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImage>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QLocalSocket>
#include <QMetaObject>
#include <QMutexLocker>
#include <QParallelAnimationGroup>
#include <QQuickWindow>
#include <QQuickTextDocument>
#include <QScreen>
#include <QSignalBlocker>
#include <QThread>
#include <QTimer>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <QVariantAnimation>
#include <QtMath>

#include <algorithm>
#include <cstring>
#include <numeric>

#ifdef Q_OS_WIN
#  include <dwmapi.h>
#  include <windows.h>
#endif

namespace {

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

QString plainCommand(const QByteArray &line)
{
    return QString::fromUtf8(line).trimmed();
}

#ifdef Q_OS_WIN
bool openClipboardWithRetry(HWND owner, DWORD *lastError)
{
    constexpr int attempts = 6;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (OpenClipboard(owner)) {
            if (lastError) {
                *lastError = ERROR_SUCCESS;
            }
            return true;
        }
        if (lastError) {
            *lastError = GetLastError();
        }
        if (attempt + 1 < attempts) {
            QThread::msleep(2);
        }
    }
    return false;
}
#endif

} // namespace

EditorController::EditorController(bool testMode, QElapsedTimer *startupTimer, QObject *parent)
    : QObject(parent)
    , m_startupTimer(startupTimer)
    , m_testMode(testMode)
{
    m_settings = std::make_unique<AppSettings>(m_testMode);
    reloadAppearance();
    m_commands = std::make_unique<EditorCommandRegistry>(m_settings.get(), this);
    connect(m_commands.get(), &EditorCommandRegistry::commandsChanged,
            this, &EditorController::commandsChanged);
    connect(m_commands.get(), &EditorCommandRegistry::uiCommandRequested,
            this, &EditorController::uiCommandRequested);
    m_monotonic.start();
    connect(&m_server, &QLocalServer::newConnection, this, &EditorController::acceptConnections);
}

EditorController::~EditorController() = default;

QString EditorController::serverName()
{
    const QByteArray testOverride = qgetenv("SCRATCHEDITOR_SERVER_NAME");
    if (!testOverride.isEmpty()) {
        return QString::fromUtf8(testOverride);
    }
    return QStringLiteral("ScratchEditor.Stage1.v1");
}

bool EditorController::forwardToExistingInstance(const QString &command, int timeoutMs)
{
    QLocalSocket socket;
    socket.connectToServer(serverName(), QIODevice::WriteOnly);
    if (!socket.waitForConnected(timeoutMs)) {
        return false;
    }

    const QString actualCommand = command.isEmpty() ? QStringLiteral("ping") : command;
    socket.write(actualCommand.toUtf8() + '\n');
    socket.waitForBytesWritten(timeoutMs);
    socket.disconnectFromServer();
    return true;
}

bool EditorController::startServer()
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (m_server.listen(serverName())) {
        return true;
    }

    QLocalSocket probe;
    probe.connectToServer(serverName());
    if (probe.waitForConnected(150)) {
        return false;
    }

    QLocalServer::removeServer(serverName());
    return m_server.listen(serverName());
}

bool EditorController::isReady() const
{
    return m_ready;
}

bool EditorController::isVisible() const
{
    return m_window && m_window->isVisible() && !m_hiding;
}

bool EditorController::testMode() const
{
    return m_testMode;
}

QString EditorController::statusMessage() const
{
    return m_statusMessage;
}

bool EditorController::clipboardHealthy() const
{
    return m_clipboardHealthy;
}

QVariantList EditorController::commands() const
{
    return m_commands ? m_commands->commands() : QVariantList{};
}

bool EditorController::markdownHighlighting() const
{
    return !m_markdownHighlighter.isNull();
}

QString EditorController::theme() const
{
    return m_theme;
}

QString EditorController::editorFontFamily() const
{
    return m_editorFontFamily;
}

int EditorController::editorFontPointSize() const
{
    return m_editorFontPointSize;
}

bool EditorController::animationsEnabled() const
{
    return m_animationsEnabled;
}

QString EditorController::settingsFile() const
{
    return m_settings ? m_settings->fileName() : QString();
}

QString EditorController::settingsError() const
{
    return m_settingsError;
}

void EditorController::registerWindow(QQuickWindow *window)
{
    m_window = window;
    if (m_window) {
        m_windowTransitionGroup = new QParallelAnimationGroup(this);
        m_windowOpacityAnimation = new QVariantAnimation;
        m_windowGeometryAnimation = new QVariantAnimation;
        connect(m_windowOpacityAnimation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) {
                    if (m_window) {
                        m_window->setOpacity(value.toDouble());
                    }
                });
        connect(m_windowGeometryAnimation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) {
                    if (m_window) {
                        m_window->setGeometry(value.toRect());
                    }
                });
        m_windowTransitionGroup->addAnimation(m_windowOpacityAnimation);
        m_windowTransitionGroup->addAnimation(m_windowGeometryAnimation);
        connect(m_windowTransitionGroup, &QParallelAnimationGroup::finished, this, [this] {
            if (m_hideWhenAnimationFinishes && m_hiding) {
                finishWindowHide();
            }
        });
        m_positioned = restoreWindowGeometry();
        m_window->create();
        applyNativeWindowStyle();
    }
    updateReadyState();
}

void EditorController::registerEditor(QObject *editor)
{
    if (m_editor) {
        m_editor->removeEventFilter(this);
    }
    m_editor = editor;
    if (m_editor) {
        m_editor->installEventFilter(this);
    }

    if (m_markdownHighlighter) {
        delete m_markdownHighlighter;
        m_markdownHighlighter = nullptr;
    }
    QTextDocument *document = nullptr;
    if (m_editor) {
        if (auto *quickDocument = qvariant_cast<QQuickTextDocument *>(
                m_editor->property("textDocument"))) {
            document = quickDocument->textDocument();
        }
    }
    if (document) {
        m_markdownHighlighter = new MarkdownHighlighter(document);
        m_markdownHighlighter->setDarkTheme(m_theme == QStringLiteral("dark"));
    }
    if (m_commands) {
        m_commands->setEditor(m_editor, document);
    }
    emit markdownHighlightingChanged();
    updateReadyState();
}

bool EditorController::executeCommand(const QString &commandId)
{
    return m_commands && m_commands->execute(commandId);
}

QString EditorController::shortcutFor(const QString &commandId) const
{
    return m_commands ? m_commands->shortcut(commandId) : QString();
}

bool EditorController::setShortcut(const QString &commandId, const QString &sequence)
{
    if (!m_commands) {
        return false;
    }
    QString errorMessage;
    return m_commands->setShortcut(commandId, sequence, &errorMessage);
}

void EditorController::resetShortcuts()
{
    if (m_commands) {
        m_commands->resetShortcuts();
    }
}

bool EditorController::findNext(const QString &query, bool caseSensitive, bool backwards)
{
    return m_commands && m_commands->findNext(query, caseSensitive, backwards);
}

bool EditorController::replaceCurrent(const QString &query, const QString &replacement,
                                      bool caseSensitive)
{
    return m_commands && m_commands->replaceCurrent(query, replacement, caseSensitive);
}

int EditorController::replaceAll(const QString &query, const QString &replacement,
                                 bool caseSensitive)
{
    return m_commands ? m_commands->replaceAll(query, replacement, caseSensitive) : 0;
}

bool EditorController::applyAppearance(const QString &theme, const QString &fontFamily,
                                       int fontPointSize, bool animationsEnabled)
{
    if (!m_settings) {
        return false;
    }
    QString error;
    if (!m_settings->setAppearance(theme, fontFamily, fontPointSize, animationsEnabled, &error)) {
        if (m_settingsError != error) {
            m_settingsError = error;
            emit settingsErrorChanged();
        }
        return false;
    }
    if (!m_settingsError.isEmpty()) {
        m_settingsError.clear();
        emit settingsErrorChanged();
    }
    reloadAppearance();
    return true;
}

void EditorController::resetAppearance()
{
    if (!m_settings) {
        return;
    }
    m_settings->resetAppearance();
    if (!m_settingsError.isEmpty()) {
        m_settingsError.clear();
        emit settingsErrorChanged();
    }
    reloadAppearance();
}

void EditorController::reloadAppearance()
{
    if (!m_settings) {
        return;
    }
    const AppSettings::Appearance appearance = m_settings->appearance();
    const bool changed = m_theme != appearance.theme
        || m_editorFontFamily != appearance.fontFamily
        || m_editorFontPointSize != appearance.fontPointSize
        || m_animationsEnabled != appearance.animationsEnabled;
    m_theme = appearance.theme;
    m_editorFontFamily = appearance.fontFamily;
    m_editorFontPointSize = appearance.fontPointSize;
    m_animationsEnabled = appearance.animationsEnabled;
    if (m_markdownHighlighter) {
        m_markdownHighlighter->setDarkTheme(m_theme == QStringLiteral("dark"));
    }
    if (m_window) {
        applyNativeWindowStyle();
    }
    if (changed) {
        emit appearanceChanged();
    }
}

void EditorController::updateReadyState()
{
    m_ready = m_window && m_editor;
    if (m_ready && m_readyStartupMs < 0 && m_startupTimer) {
        m_readyStartupMs = m_startupTimer->elapsed();
    }
}

void EditorController::acceptConnections()
{
    while (QLocalSocket *socket = m_server.nextPendingConnection()) {
        m_buffers.insert(socket, {});
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readSocket(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
        if (socket->bytesAvailable() > 0) {
            readSocket(socket);
        }
    }
}

void EditorController::readSocket(QLocalSocket *socket)
{
    QByteArray &buffer = m_buffers[socket];
    buffer += socket->readAll();

    while (true) {
        const qsizetype newline = buffer.indexOf('\n');
        if (newline < 0) {
            if (buffer.size() > 1024 * 1024) {
                sendError(socket, QStringLiteral("unknown"), QStringLiteral("request too large"),
                          m_monotonic.nsecsElapsed());
            }
            return;
        }

        const QByteArray line = buffer.left(newline).trimmed();
        buffer.remove(0, newline + 1);
        const qint64 startedNs = m_monotonic.nsecsElapsed();

        QJsonObject request;
        if (line.startsWith('{')) {
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(line, &error);
            if (error.error != QJsonParseError::NoError || !document.isObject()) {
                sendError(socket, QStringLiteral("unknown"), QStringLiteral("invalid JSON request"),
                          startedNs);
                return;
            }
            request = document.object();
        } else {
            request.insert(QStringLiteral("command"), plainCommand(line));
            request.insert(QStringLiteral("noReply"), true);
        }

        dispatchCommand(socket, request, startedNs);
    }
}

void EditorController::dispatchCommand(QLocalSocket *socket, const QJsonObject &request,
                                       qint64 startedNs)
{
    const QString command = request.value(QStringLiteral("command")).toString().trimmed();
    const QString requestId = request.value(QStringLiteral("requestId")).toString();
    const bool noReply = request.value(QStringLiteral("noReply")).toBool();

    if (command == QStringLiteral("ping") || command == QStringLiteral("status")) {
        if (noReply) {
            return;
        }
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        sendResponse(socket, response, startedNs, requestId);
        return;
    }

    if (!m_ready) {
        sendError(socket, command, QStringLiteral("QML window is not ready"), startedNs, requestId);
        return;
    }

    if (command == QStringLiteral("toggle")) {
        if (noReply) {
            toggleEditor();
            return;
        }
        if (isVisible()) {
            commitAndHide();
            QJsonObject response = statusObject();
            response.insert(QStringLiteral("command"), command);
            sendResponse(socket, response, startedNs, requestId);
        } else {
            showForRequest(socket, command, startedNs, requestId, request);
        }
        return;
    }

    if (command == QStringLiteral("show")) {
        if (noReply) {
            showEditor();
            return;
        }
        if (isVisible()) {
            m_window->raise();
            m_window->requestActivate();
            QJsonObject response = statusObject();
            response.insert(QStringLiteral("command"), command);
            sendResponse(socket, response, startedNs, requestId);
        } else {
            showForRequest(socket, command, startedNs, requestId, request);
        }
        return;
    }

    if (command == QStringLiteral("hide")) {
        if (noReply) {
            commitAndHide();
            return;
        }
        commitAndHide();
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        sendResponse(socket, response, startedNs, requestId);
        return;
    }

    if (!m_testMode) {
        sendError(socket, command, QStringLiteral("unsupported command"), startedNs, requestId);
        return;
    }

    if (command == QStringLiteral("quit")) {
        QJsonObject response;
        response.insert(QStringLiteral("command"), command);
        sendResponse(socket, response, startedNs, requestId);
        QTimer::singleShot(25, qApp, [] { QCoreApplication::exit(0); });
    } else if (command == QStringLiteral("awaitInputFrame")) {
        if (m_pendingInput.socket) {
            sendError(socket, command, QStringLiteral("input benchmark already armed"), startedNs,
                      requestId);
        } else {
            m_pendingInput = {socket, startedNs, requestId};
        }
    } else if (command == QStringLiteral("benchmarkLargeDocument")) {
        runLargeDocumentBenchmark(socket, startedNs, requestId);
    } else if (command == QStringLiteral("restoreTestDocument")) {
        restoreTestDocument();
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        sendResponse(socket, response, startedNs, requestId);
    } else if (command == QStringLiteral("benchmarkIme")) {
        runImeBenchmark(socket, startedNs, requestId);
    } else if (command == QStringLiteral("benchmarkAnimation")) {
        runAnimationBenchmark(socket, startedNs, requestId);
    } else if (command == QStringLiteral("clearTestText")) {
        m_editor->setProperty("text", QString());
        m_editor->setProperty("cursorPosition", 0);
        QJsonObject response;
        response.insert(QStringLiteral("command"), command);
        sendResponse(socket, response, startedNs, requestId);
    } else if (command == QStringLiteral("testText")) {
        QJsonObject response;
        response.insert(QStringLiteral("command"), command);
        response.insert(QStringLiteral("text"), m_editor->property("text").toString());
        sendResponse(socket, response, startedNs, requestId);
    } else if (command == QStringLiteral("testSetText")) {
        const QString text = request.value(QStringLiteral("text")).toString();
        m_editor->setProperty("text", text);
        m_editor->setProperty("cursorPosition", text.size());
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        sendResponse(socket, response, startedNs, requestId);
    } else if (command == QStringLiteral("testSetSelection")) {
        const int start = request.value(QStringLiteral("start")).toInt();
        const int end = request.value(QStringLiteral("end")).toInt();
        const bool invoked = QMetaObject::invokeMethod(m_editor, "select",
                                                       Q_ARG(int, start), Q_ARG(int, end));
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        response.insert(QStringLiteral("invoked"), invoked);
        sendResponse(socket, response, startedNs, requestId);
    } else if (command == QStringLiteral("testExecuteCommand")) {
        const QString commandId = request.value(QStringLiteral("commandId")).toString();
        const bool executed = executeCommand(commandId);
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        response.insert(QStringLiteral("commandId"), commandId);
        response.insert(QStringLiteral("executed"), executed);
        response.insert(QStringLiteral("text"), m_editor->property("text").toString());
        sendResponse(socket, response, startedNs, requestId);
    } else if (command == QStringLiteral("testFindNext")) {
        const bool found = findNext(request.value(QStringLiteral("query")).toString(),
                                    request.value(QStringLiteral("caseSensitive")).toBool(),
                                    request.value(QStringLiteral("backwards")).toBool());
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        response.insert(QStringLiteral("found"), found);
        sendResponse(socket, response, startedNs, requestId);
    } else if (command == QStringLiteral("testReplaceCurrent")) {
        const bool replaced = replaceCurrent(
            request.value(QStringLiteral("query")).toString(),
            request.value(QStringLiteral("replacement")).toString(),
            request.value(QStringLiteral("caseSensitive")).toBool());
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        response.insert(QStringLiteral("replaced"), replaced);
        response.insert(QStringLiteral("text"), m_editor->property("text").toString());
        sendResponse(socket, response, startedNs, requestId);
    } else if (command == QStringLiteral("testReplaceAll")) {
        const int count = replaceAll(request.value(QStringLiteral("query")).toString(),
                                     request.value(QStringLiteral("replacement")).toString(),
                                     request.value(QStringLiteral("caseSensitive")).toBool());
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        response.insert(QStringLiteral("replacementCount"), count);
        response.insert(QStringLiteral("text"), m_editor->property("text").toString());
        sendResponse(socket, response, startedNs, requestId);
    } else if (command == QStringLiteral("testSetShortcut")) {
        const QString commandId = request.value(QStringLiteral("commandId")).toString();
        const QString sequence = request.value(QStringLiteral("sequence")).toString();
        const bool configured = setShortcut(commandId, sequence);
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        response.insert(QStringLiteral("configured"), configured);
        response.insert(QStringLiteral("commandId"), commandId);
        response.insert(QStringLiteral("shortcut"), shortcutFor(commandId));
        sendResponse(socket, response, startedNs, requestId);
    } else if (command == QStringLiteral("testShortcut")) {
        const QString commandId = request.value(QStringLiteral("commandId")).toString();
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        response.insert(QStringLiteral("commandId"), commandId);
        response.insert(QStringLiteral("shortcut"), shortcutFor(commandId));
        sendResponse(socket, response, startedNs, requestId);
    } else if (command == QStringLiteral("testHighlightSummary")) {
        int blocks = 0;
        int formattedRanges = 0;
        int fencedBlocks = 0;
        if (auto *quickDocument = qvariant_cast<QQuickTextDocument *>(
                m_editor->property("textDocument"))) {
            for (QTextBlock block = quickDocument->textDocument()->begin();
                 block.isValid(); block = block.next()) {
                ++blocks;
                if (block.layout()) {
                    formattedRanges += block.layout()->formats().size();
                }
                if (block.userState() == 1) {
                    ++fencedBlocks;
                }
            }
        }
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        response.insert(QStringLiteral("blocks"), blocks);
        response.insert(QStringLiteral("formattedRanges"), formattedRanges);
        response.insert(QStringLiteral("fencedBlocks"), fencedBlocks);
        sendResponse(socket, response, startedNs, requestId);
    } else if (command == QStringLiteral("testCloseOverlays")) {
        const bool paletteClosed = QMetaObject::invokeMethod(m_window, "closeCommandPalette");
        const bool findClosed = QMetaObject::invokeMethod(m_window, "hideFindPanel");
        const bool settingsClosed = QMetaObject::invokeMethod(m_window, "closeSettings");
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        response.insert(QStringLiteral("paletteClosed"), paletteClosed);
        response.insert(QStringLiteral("findClosed"), findClosed);
        response.insert(QStringLiteral("settingsClosed"), settingsClosed);
        sendResponse(socket, response, startedNs, requestId);
    } else if (command == QStringLiteral("testResetShortcuts")) {
        resetShortcuts();
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        response.insert(QStringLiteral("boldShortcut"), shortcutFor(QStringLiteral("toggleBold")));
        sendResponse(socket, response, startedNs, requestId);
    } else if (command == QStringLiteral("testApplyAppearance")) {
        const bool applied = applyAppearance(
            request.value(QStringLiteral("theme")).toString(),
            request.value(QStringLiteral("fontFamily")).toString(),
            request.value(QStringLiteral("fontPointSize")).toInt(),
            request.value(QStringLiteral("animationsEnabled")).toBool());
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        response.insert(QStringLiteral("applied"), applied);
        sendResponse(socket, response, startedNs, requestId);
    } else if (command == QStringLiteral("testResetAppearance")) {
        resetAppearance();
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        sendResponse(socket, response, startedNs, requestId);
    } else if (command == QStringLiteral("testConfigKeys")) {
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        response.insert(QStringLiteral("keys"),
                        QJsonArray::fromStringList(m_settings ? m_settings->allKeys()
                                                             : QStringList{}));
        sendResponse(socket, response, startedNs, requestId);
    } else if (command == QStringLiteral("testSetGeometry")) {
        const QRect requested(request.value(QStringLiteral("x")).toInt(),
                              request.value(QStringLiteral("y")).toInt(),
                              request.value(QStringLiteral("width")).toInt(),
                              request.value(QStringLiteral("height")).toInt());
        m_window->setGeometry(validatedWindowGeometry(requested));
        m_windowRestingGeometry = m_window->geometry();
        m_positioned = true;
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        sendResponse(socket, response, startedNs, requestId);
    } else if (command == QStringLiteral("testResetSettings")) {
        m_settings->resetAll();
        resetShortcuts();
        reloadAppearance();
        m_windowRestingGeometry = {};
        m_positioned = false;
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), command);
        sendResponse(socket, response, startedNs, requestId);
    } else {
        sendError(socket, command, QStringLiteral("unsupported test command"), startedNs, requestId);
    }
}

void EditorController::sendResponse(QLocalSocket *socket, QJsonObject response, qint64 startedNs,
                                    const QString &requestId)
{
    if (!socket || socket->state() == QLocalSocket::UnconnectedState) {
        return;
    }

    response.insert(QStringLiteral("ok"), true);
    response.insert(QStringLiteral("serverElapsedMs"),
                    (m_monotonic.nsecsElapsed() - startedNs) / 1'000'000.0);
    if (!requestId.isEmpty()) {
        response.insert(QStringLiteral("requestId"), requestId);
    }

    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
    socket->flush();
}

void EditorController::sendError(QLocalSocket *socket, const QString &command,
                                 const QString &message, qint64 startedNs,
                                 const QString &requestId)
{
    QJsonObject response;
    response.insert(QStringLiteral("ok"), false);
    response.insert(QStringLiteral("command"), command);
    response.insert(QStringLiteral("error"), message);
    response.insert(QStringLiteral("serverElapsedMs"),
                    (m_monotonic.nsecsElapsed() - startedNs) / 1'000'000.0);
    if (!requestId.isEmpty()) {
        response.insert(QStringLiteral("requestId"), requestId);
    }

    if (socket && socket->state() != QLocalSocket::UnconnectedState) {
        socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
        socket->flush();
    }
}

void EditorController::showForRequest(QLocalSocket *socket, const QString &command,
                                      qint64 startedNs, const QString &requestId,
                                      const QJsonObject &request)
{
    QJsonObject response;
    response.insert(QStringLiteral("command"), command);
    if (request.contains(QStringLiteral("clientQpc"))) {
        response.insert(QStringLiteral("clientQpc"), request.value(QStringLiteral("clientQpc")));
    }
    waitForNextFrame(socket, response, startedNs, requestId);
    showEditor();
}

void EditorController::waitForNextFrame(QLocalSocket *socket, QJsonObject response,
                                        qint64 startedNs, const QString &requestId)
{
    if (!m_window) {
        sendError(socket, response.value(QStringLiteral("command")).toString(),
                  QStringLiteral("window unavailable"), startedNs, requestId);
        return;
    }

    QPointer<QLocalSocket> guardedSocket(socket);
    connect(m_window, &QQuickWindow::frameSwapped, this,
            [this, guardedSocket, response, startedNs, requestId]() mutable {
                response.insert(QStringLiteral("frameMs"),
                                (m_monotonic.nsecsElapsed() - startedNs) / 1'000'000.0);
#ifdef Q_OS_WIN
                const qint64 clientQpc = response.value(QStringLiteral("clientQpc"))
                                             .toString().toLongLong();
                if (clientQpc > 0) {
                    LARGE_INTEGER now{};
                    LARGE_INTEGER frequency{};
                    QueryPerformanceCounter(&now);
                    QueryPerformanceFrequency(&frequency);
                    response.insert(QStringLiteral("clientToFrameMs"),
                                    (now.QuadPart - clientQpc) * 1000.0 / frequency.QuadPart);
                }
#endif

                auto finish = [this, guardedSocket, response, startedNs, requestId]() mutable {
                    if (m_testMode && !m_firstFrameCaptured && m_window) {
                        const QImage image = m_window->grabWindow();
                        if (!image.isNull()) {
                            const QColor pixel = image.pixelColor(
                                qBound(0, 4, image.width() - 1),
                                qBound(0, 4, image.height() - 1));
                            m_firstFrameColor = pixel.name(QColor::HexRgb);
                        }
                        m_firstFrameCaptured = true;
                    }
                    QJsonObject finalResponse = response;
                    const QJsonObject status = statusObject();
                    for (auto it = status.begin(); it != status.end(); ++it) {
                        finalResponse.insert(it.key(), it.value());
                    }
                    sendResponse(guardedSocket, finalResponse, startedNs, requestId);
                };

                if (m_testMode && !m_firstFrameCaptured) {
                    QTimer::singleShot(0, this, finish);
                } else {
                    finish();
                }
            }, Qt::SingleShotConnection);
    m_window->update();
}

void EditorController::showEditor()
{
    if (!m_ready || isVisible()) {
        return;
    }
    ++m_focusGeneration;

#ifdef Q_OS_WIN
    const HWND editorHwnd = reinterpret_cast<HWND>(m_window->winId());
    const HWND foreground = GetForegroundWindow();
    if (foreground && foreground != editorHwnd && IsWindow(foreground)) {
        m_previousForegroundWindow = reinterpret_cast<quintptr>(foreground);
    }
#endif

    QString clipboardText;
    QString clipboardError;
    if (readClipboardText(&clipboardText, &clipboardError)) {
        if (m_editor->property("text").toString() != clipboardText) {
            m_editor->setProperty("text", clipboardText);
        }
        setClipboardState(true);
    } else {
        setClipboardState(false, clipboardError);
    }

    if (!m_positioned) {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            const QRect available = screen->availableGeometry();
            m_window->setPosition(
                available.x() + (available.width() - m_window->width()) / 2,
                available.y() + (available.height() - m_window->height()) / 2);
        }
        m_positioned = true;
        m_windowRestingGeometry = m_window->geometry();
    }

    const bool wasNativeVisible = m_window->isVisible();
    const QRect restingGeometry = m_windowRestingGeometry.isValid()
        ? m_windowRestingGeometry
        : m_window->geometry();
    if (m_windowTransitionGroup) {
        m_windowTransitionGroup->stop();
    }
    m_hideWhenAnimationFinishes = false;
    m_hiding = false;
    m_windowRestingGeometry = restingGeometry;
    if (!wasNativeVisible) {
        m_window->setOpacity(m_animationsEnabled ? 0.0 : 1.0);
        m_window->setGeometry(m_animationsEnabled
                                  ? scaledWindowGeometry(restingGeometry)
                                  : restingGeometry);
    }
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
    emit visibleChanged();
    startWindowTransition(1.0, restingGeometry, false);

    QTimer::singleShot(0, this, [this] {
        if (!m_window || !m_editor || !m_window->isVisible()) {
            return;
        }
        QMetaObject::invokeMethod(m_editor, "forceActiveFocus");
        m_window->raise();
        m_window->requestActivate();
#ifdef Q_OS_WIN
        const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
        SetForegroundWindow(hwnd);
#endif
    });
}

void EditorController::toggleEditor()
{
    if (isVisible()) {
        commitAndHide();
    } else {
        showEditor();
    }
}

void EditorController::hideEditor()
{
    commitAndHide();
}

bool EditorController::commitAndHide()
{
    if (!m_window || (!m_window->isVisible() && !m_hiding) || !m_editor) {
        return true;
    }
    if (m_hiding) {
        return true;
    }

    const QString text = m_editor->property("text").toString();
    if (!text.isEmpty()) {
        QString clipboardError;
        if (!writeClipboardText(text, &clipboardError)) {
            setClipboardState(false, clipboardError);
            m_window->raise();
            m_window->requestActivate();
            QMetaObject::invokeMethod(m_editor, "forceActiveFocus");
            return false;
        }
    }
    setClipboardState(true);
    const bool transitionRunning = m_windowTransitionGroup
        && m_windowTransitionGroup->state() == QAbstractAnimation::Running;
    if (!transitionRunning || !m_windowRestingGeometry.isValid()) {
        m_windowRestingGeometry = m_window->geometry();
    }
    saveWindowGeometry();
    if (m_animationsEnabled) {
        m_hiding = true;
        emit visibleChanged();
        startWindowTransition(0.0, scaledWindowGeometry(m_windowRestingGeometry), true);
        return true;
    }

    m_window->hide();
    m_window->setOpacity(1.0);
    m_window->setGeometry(m_windowRestingGeometry);
    emit visibleChanged();
    const quint64 focusGeneration = ++m_focusGeneration;
    QTimer::singleShot(0, this, [this, focusGeneration] {
        if (focusGeneration == m_focusGeneration && !isVisible()) {
            restorePreviousFocus();
        }
    });
    return true;
}

void EditorController::startWindowTransition(qreal targetOpacity, const QRect &targetGeometry,
                                             bool hideWhenFinished)
{
    if (!m_window) {
        return;
    }

    m_hideWhenAnimationFinishes = hideWhenFinished;
    if (!m_animationsEnabled || !m_windowTransitionGroup || !m_windowOpacityAnimation
        || !m_windowGeometryAnimation) {
        m_window->setOpacity(targetOpacity);
        m_window->setGeometry(targetGeometry);
        if (hideWhenFinished) {
            finishWindowHide();
        }
        return;
    }

    const qreal startOpacity = m_window->opacity();
    const QRect startGeometry = m_window->geometry();
    m_windowTransitionGroup->stop();
    {
        // A completed QVariantAnimation remains at its end time. Reconfiguring
        // its end value in that state emits valueChanged immediately, which
        // used to apply the next transition's target geometry for one frame
        // before start() reset the timeline to zero. Block both animations
        // while rewinding and replacing their endpoints.
        const QSignalBlocker opacityBlocker(m_windowOpacityAnimation);
        const QSignalBlocker geometryBlocker(m_windowGeometryAnimation);
        m_windowTransitionGroup->setCurrentTime(0);
        m_windowOpacityAnimation->setDuration(120);
        m_windowOpacityAnimation->setStartValue(startOpacity);
        m_windowOpacityAnimation->setEndValue(targetOpacity);
        m_windowOpacityAnimation->setEasingCurve(hideWhenFinished ? QEasingCurve::InCubic
                                                                  : QEasingCurve::OutCubic);
        m_windowGeometryAnimation->setDuration(120);
        m_windowGeometryAnimation->setStartValue(startGeometry);
        m_windowGeometryAnimation->setEndValue(targetGeometry);
        m_windowGeometryAnimation->setEasingCurve(hideWhenFinished ? QEasingCurve::InCubic
                                                                   : QEasingCurve::OutCubic);
    }
    m_windowTransitionPreparationStable = m_windowTransitionPreparationStable
        && qAbs(m_window->opacity() - startOpacity) <= 0.0001
        && m_window->geometry() == startGeometry;
    // Preserve the exact captured state across animation reconfiguration.
    m_window->setOpacity(startOpacity);
    m_window->setGeometry(startGeometry);
    m_windowTransitionGroup->start();
}

void EditorController::finishWindowHide()
{
    if (!m_window || !m_hiding) {
        return;
    }

    if (m_windowTransitionGroup) {
        m_windowTransitionGroup->stop();
    }
    m_hideWhenAnimationFinishes = false;
    // Commit the fully transparent frame before unmapping the native window.
    // Without this compositor barrier, DWM can replay the last full-size Qt
    // Quick surface while processing ShowWindow(SW_HIDE), producing a visible
    // one-frame expansion even though QWindow::geometry() never rebounds.
    m_window->setOpacity(0.0);
#ifdef Q_OS_WIN
    DwmFlush();
#endif
    m_window->hide();
    // Keep the final shrunken geometry while hidden. showEditor() restores the
    // logical resting geometry as part of the next opening animation.
    m_hiding = false;

    const quint64 focusGeneration = ++m_focusGeneration;
    QTimer::singleShot(0, this, [this, focusGeneration] {
        if (focusGeneration == m_focusGeneration && !isVisible()) {
            restorePreviousFocus();
        }
    });
}

QRect EditorController::scaledWindowGeometry(const QRect &restingGeometry) const
{
    if (!restingGeometry.isValid()) {
        return restingGeometry;
    }

    constexpr qreal shapeScale = 0.98;
    const int minimumWidth = m_window ? m_window->minimumWidth() : 1;
    const int minimumHeight = m_window ? m_window->minimumHeight() : 1;
    const QSize scaledSize(qMax(minimumWidth, qRound(restingGeometry.width() * shapeScale)),
                           qMax(minimumHeight, qRound(restingGeometry.height() * shapeScale)));
    QRect scaled(QPoint(), scaledSize);
    scaled.moveCenter(restingGeometry.center());
    return scaled;
}

void EditorController::shutdown()
{
    saveWindowGeometry();
    if (isVisible()) {
        commitAndHide();
    }
    m_server.close();
}

bool EditorController::readClipboardText(QString *text, QString *errorMessage)
{
    if (!text) {
        return false;
    }

#ifdef Q_OS_WIN
    DWORD nativeError = ERROR_SUCCESS;
    const HWND owner = m_window ? reinterpret_cast<HWND>(m_window->winId()) : nullptr;
    if (!openClipboardWithRetry(owner, &nativeError)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("剪贴板正被其他程序占用（错误 %1）").arg(nativeError);
        }
        return false;
    }

    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        *text = QString();
        CloseClipboard();
        return true;
    }

    const HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (!handle) {
        nativeError = GetLastError();
        CloseClipboard();
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法读取剪贴板文本（错误 %1）").arg(nativeError);
        }
        return false;
    }

    const auto *characters = static_cast<const wchar_t *>(GlobalLock(handle));
    if (!characters) {
        nativeError = GetLastError();
        CloseClipboard();
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法锁定剪贴板文本（错误 %1）").arg(nativeError);
        }
        return false;
    }

    const qsizetype maximumCharacters = static_cast<qsizetype>(GlobalSize(handle) / sizeof(wchar_t));
    qsizetype length = 0;
    while (length < maximumCharacters && characters[length] != L'\0') {
        ++length;
    }
    *text = QString::fromWCharArray(characters, length);
    GlobalUnlock(handle);
    CloseClipboard();
    return true;
#else
    *text = QGuiApplication::clipboard()->text(QClipboard::Clipboard);
    Q_UNUSED(errorMessage);
    return true;
#endif
}

bool EditorController::writeClipboardText(const QString &text, QString *errorMessage)
{
#ifdef Q_OS_WIN
    const SIZE_T byteCount = static_cast<SIZE_T>(text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, byteCount);
    if (!memory) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法分配剪贴板内存（错误 %1）").arg(GetLastError());
        }
        return false;
    }

    auto *destination = static_cast<wchar_t *>(GlobalLock(memory));
    if (!destination) {
        const DWORD nativeError = GetLastError();
        GlobalFree(memory);
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法写入剪贴板内存（错误 %1）").arg(nativeError);
        }
        return false;
    }
    std::memcpy(destination, text.utf16(), static_cast<size_t>(text.size()) * sizeof(char16_t));
    destination[text.size()] = L'\0';
    GlobalUnlock(memory);

    DWORD nativeError = ERROR_SUCCESS;
    const HWND owner = m_window ? reinterpret_cast<HWND>(m_window->winId()) : nullptr;
    if (!openClipboardWithRetry(owner, &nativeError)) {
        GlobalFree(memory);
        if (errorMessage) {
            *errorMessage = QStringLiteral("剪贴板正被其他程序占用，内容尚未关闭（错误 %1）")
                                .arg(nativeError);
        }
        return false;
    }

    if (!EmptyClipboard()) {
        nativeError = GetLastError();
        CloseClipboard();
        GlobalFree(memory);
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法清空剪贴板（错误 %1）").arg(nativeError);
        }
        return false;
    }

    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        nativeError = GetLastError();
        CloseClipboard();
        GlobalFree(memory);
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法写回剪贴板，内容仍保留在编辑器中（错误 %1）")
                                .arg(nativeError);
        }
        return false;
    }

    CloseClipboard();
    return true;
#else
    QGuiApplication::clipboard()->setText(text, QClipboard::Clipboard);
    Q_UNUSED(errorMessage);
    return true;
#endif
}

void EditorController::setClipboardState(bool healthy, const QString &message)
{
    const QString newMessage = healthy ? QStringLiteral("Esc 关闭并复制") : message;
    const bool healthChanged = m_clipboardHealthy != healthy;
    const bool messageChanged = m_statusMessage != newMessage;
    m_clipboardHealthy = healthy;
    m_statusMessage = newMessage;
    if (healthChanged) {
        emit clipboardStateChanged();
    }
    if (messageChanged) {
        emit statusMessageChanged();
    }
}

QRect EditorController::validatedWindowGeometry(const QRect &requested) const
{
    QRect candidate = requested;
    candidate.setWidth(qMax(500, candidate.width()));
    candidate.setHeight(qMax(320, candidate.height()));

    QScreen *targetScreen = nullptr;
    qint64 largestIntersection = 0;
    for (QScreen *screen : QGuiApplication::screens()) {
        const QRect intersection = candidate.intersected(screen->availableGeometry());
        const qint64 area = static_cast<qint64>(intersection.width()) * intersection.height();
        if (area > largestIntersection) {
            largestIntersection = area;
            targetScreen = screen;
        }
    }
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    if (!targetScreen) {
        return candidate;
    }

    const QRect available = targetScreen->availableGeometry();
    candidate.setWidth(qMin(candidate.width(), available.width()));
    candidate.setHeight(qMin(candidate.height(), available.height()));
    candidate.moveLeft(qBound(available.left(), candidate.left(),
                              available.right() - candidate.width() + 1));
    candidate.moveTop(qBound(available.top(), candidate.top(),
                             available.bottom() - candidate.height() + 1));
    return candidate;
}

bool EditorController::restoreWindowGeometry()
{
    if (!m_window || !m_settings) {
        return false;
    }
    const QRect stored = m_settings->windowGeometry();
    if (!stored.isValid()) {
        return false;
    }
    m_window->setGeometry(validatedWindowGeometry(stored));
    return true;
}

void EditorController::saveWindowGeometry()
{
    if (!m_window || !m_settings || !m_positioned) {
        return;
    }
    const bool transitionRunning = m_windowTransitionGroup
        && m_windowTransitionGroup->state() == QAbstractAnimation::Running;
    const bool useRestingGeometry = m_windowRestingGeometry.isValid()
        && (transitionRunning || m_hiding || !m_window->isVisible());
    m_settings->setWindowGeometry(useRestingGeometry ? m_windowRestingGeometry
                                                     : m_window->geometry());
}

void EditorController::restorePreviousFocus()
{
#ifdef Q_OS_WIN
    const HWND previous = reinterpret_cast<HWND>(m_previousForegroundWindow);
    m_previousForegroundWindow = 0;
    if (previous && IsWindow(previous)) {
        SetForegroundWindow(previous);
    }
#endif
}

void EditorController::applyNativeWindowStyle()
{
#ifdef Q_OS_WIN
    if (!m_window) {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
    const BOOL darkMode = m_theme == QStringLiteral("dark") ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode));
    const DWORD cornerPreference = 2;
    DwmSetWindowAttribute(hwnd, 33, &cornerPreference, sizeof(cornerPreference));
    const COLORREF borderColor = 0xFFFFFFFE;
    DwmSetWindowAttribute(hwnd, 34, &borderColor, sizeof(borderColor));
#endif
}

QJsonObject EditorController::statusObject() const
{
    QJsonObject status;
    status.insert(QStringLiteral("ready"), m_ready);
    status.insert(QStringLiteral("visible"), isVisible());
    status.insert(QStringLiteral("testMode"), m_testMode);
    status.insert(QStringLiteral("pid"), static_cast<qint64>(QCoreApplication::applicationPid()));
    status.insert(QStringLiteral("startupMs"), m_readyStartupMs);
    status.insert(QStringLiteral("firstFrameColor"), m_firstFrameColor);
    status.insert(QStringLiteral("serverName"), serverName());
    status.insert(QStringLiteral("renderLoop"),
                  QString::fromLocal8Bit(qgetenv("QSG_RENDER_LOOP")));
    status.insert(QStringLiteral("statusMessage"), m_statusMessage);
    status.insert(QStringLiteral("clipboardHealthy"), m_clipboardHealthy);
    status.insert(QStringLiteral("settingsFile"), m_settings ? m_settings->fileName() : QString());
    status.insert(QStringLiteral("settingsStatus"),
                  m_settings ? m_settings->status() : -1);
    status.insert(QStringLiteral("settingsSchemaVersion"),
                  m_settings ? m_settings->schemaVersion() : 0);
    status.insert(QStringLiteral("theme"), m_theme);
    status.insert(QStringLiteral("editorFontFamily"), m_editorFontFamily);
    status.insert(QStringLiteral("editorFontPointSize"), m_editorFontPointSize);
    status.insert(QStringLiteral("animationsEnabled"), m_animationsEnabled);
    status.insert(QStringLiteral("settingsError"), m_settingsError);
    status.insert(QStringLiteral("markdownHighlighting"), markdownHighlighting());
    status.insert(QStringLiteral("commandCount"), commands().size());

    if (m_window) {
        status.insert(QStringLiteral("width"), m_window->width());
        status.insert(QStringLiteral("height"), m_window->height());
        status.insert(QStringLiteral("x"), m_window->x());
        status.insert(QStringLiteral("y"), m_window->y());
        status.insert(QStringLiteral("minimumWidth"), m_window->minimumWidth());
        status.insert(QStringLiteral("minimumHeight"), m_window->minimumHeight());
        status.insert(QStringLiteral("devicePixelRatio"), m_window->effectiveDevicePixelRatio());
        status.insert(QStringLiteral("active"), m_window->isActive());
        status.insert(QStringLiteral("alwaysOnTop"),
                      bool(m_window->flags() & Qt::WindowStaysOnTopHint));
        status.insert(QStringLiteral("frameless"), bool(m_window->flags() & Qt::FramelessWindowHint));
        status.insert(QStringLiteral("graphicsApi"),
                      static_cast<int>(m_window->rendererInterface()->graphicsApi()));
        status.insert(QStringLiteral("verticalScrollBarVisible"),
                      m_window->property("verticalScrollBarVisible").toBool());
        status.insert(QStringLiteral("commandPaletteLoaded"),
                      m_window->property("commandPaletteLoaded").toBool());
        status.insert(QStringLiteral("findPanelVisible"),
                      m_window->property("findPanelVisible").toBool());
        status.insert(QStringLiteral("settingsPageLoaded"),
                      m_window->property("settingsPageLoaded").toBool());
        status.insert(QStringLiteral("settingsPageVisible"),
                      m_window->property("settingsPageVisible").toBool());
        status.insert(QStringLiteral("themeBackgroundColor"),
                      m_window->property("themeBackgroundColor").toString());
        status.insert(QStringLiteral("themeEditorSurfaceColor"),
                      m_window->property("themeEditorSurfaceColor").toString());
        status.insert(QStringLiteral("transitionDuration"),
                      m_window->property("transitionDuration").toInt());
        status.insert(QStringLiteral("resizeMargin"),
                      m_window->property("resizeMargin").toInt());
        status.insert(QStringLiteral("edgeDragWidth"),
                      m_window->property("edgeDragWidth").toInt());
        status.insert(QStringLiteral("cornerResizeEnabled"),
                      m_window->property("cornerResizeEnabled").toBool());
        status.insert(QStringLiteral("edgeDragEnabled"),
                      m_window->property("edgeDragEnabled").toBool());
        status.insert(QStringLiteral("windowOpacity"), m_window->opacity());
        status.insert(QStringLiteral("windowTransitionActive"),
                      m_windowTransitionGroup
                          && m_windowTransitionGroup->state() == QAbstractAnimation::Running);
        status.insert(QStringLiteral("windowShapeAnimationEnabled"), m_animationsEnabled);
        status.insert(QStringLiteral("windowTransitionPreparationStable"),
                      m_windowTransitionPreparationStable);
        status.insert(QStringLiteral("windowRestingWidth"), m_windowRestingGeometry.width());
        status.insert(QStringLiteral("windowRestingHeight"), m_windowRestingGeometry.height());
#ifdef Q_OS_WIN
        status.insert(QStringLiteral("hwnd"),
                      QString::number(reinterpret_cast<quintptr>(m_window->winId())));
        status.insert(QStringLiteral("foregroundHwnd"),
                      QString::number(reinterpret_cast<quintptr>(GetForegroundWindow())));
        status.insert(QStringLiteral("previousForegroundHwnd"),
                      QString::number(m_previousForegroundWindow));
#endif
    }

    const QFont cjkFont(QStringLiteral("Microsoft YaHei UI"));
    const QFontMetrics metrics(cjkFont);
    status.insert(QStringLiteral("cjkFontFamily"), cjkFont.family());
    status.insert(QStringLiteral("cjkGlyphAvailable"), metrics.inFontUcs4(0x4E2D));
    if (m_editor) {
        status.insert(QStringLiteral("textLength"), m_editor->property("text").toString().size());
        status.insert(QStringLiteral("inputMethodComposing"),
                      m_editor->property("inputMethodComposing").toBool());
        status.insert(QStringLiteral("editorHasFocus"), m_editor->property("activeFocus").toBool());
        status.insert(QStringLiteral("selectionStart"), m_editor->property("selectionStart").toInt());
        status.insert(QStringLiteral("selectionEnd"), m_editor->property("selectionEnd").toInt());
        status.insert(QStringLiteral("cursorPosition"), m_editor->property("cursorPosition").toInt());
    }
    return status;
}

void EditorController::runLargeDocumentBenchmark(QLocalSocket *socket, qint64 startedNs,
                                                 const QString &requestId)
{
    if (!isVisible()) {
        sendError(socket, QStringLiteral("benchmarkLargeDocument"),
                  QStringLiteral("editor must be visible"), startedNs, requestId);
        return;
    }

    if (!m_hasSavedTestText) {
        m_savedTestText = m_editor->property("text").toString();
        m_hasSavedTestText = true;
    }

    QString largeText;
    largeText.reserve(100000);
    const QString line = QStringLiteral("中文输入与纯文本性能 abcdefghijklmnopqrstuvwxyz 0123456789\n");
    while (largeText.size() < 100000) {
        largeText += line;
    }
    largeText.truncate(100000);

    QElapsedTimer phase;
    phase.start();
    m_editor->setProperty("text", largeText);
    const double loadMs = phase.nsecsElapsed() / 1'000'000.0;

    phase.restart();
    const bool selected = QMetaObject::invokeMethod(m_editor, "select", Q_ARG(int, 40000),
                                                     Q_ARG(int, 60000));
    const double selectionMs = phase.nsecsElapsed() / 1'000'000.0;

    phase.restart();
    const bool scrolled = QMetaObject::invokeMethod(m_window, "scrollToBottom");
    const double scrollMs = phase.nsecsElapsed() / 1'000'000.0;

    QJsonObject response;
    response.insert(QStringLiteral("command"), QStringLiteral("benchmarkLargeDocument"));
    response.insert(QStringLiteral("characters"), largeText.size());
    response.insert(QStringLiteral("loadMs"), loadMs);
    response.insert(QStringLiteral("selectionMs"), selectionMs);
    response.insert(QStringLiteral("scrollMs"), scrollMs);
    response.insert(QStringLiteral("selectionInvoked"), selected);
    response.insert(QStringLiteral("scrollInvoked"), scrolled);
    waitForNextFrame(socket, response, startedNs, requestId);
}

void EditorController::restoreTestDocument()
{
    if (!m_hasSavedTestText || !m_editor) {
        return;
    }
    m_editor->setProperty("text", m_savedTestText);
    if (m_window) {
        QMetaObject::invokeMethod(m_window, "resetScroll");
    }
    m_savedTestText.clear();
    m_hasSavedTestText = false;
}

void EditorController::runImeBenchmark(QLocalSocket *socket, qint64 startedNs,
                                      const QString &requestId)
{
    if (!isVisible()) {
        sendError(socket, QStringLiteral("benchmarkIme"), QStringLiteral("editor must be visible"),
                  startedNs, requestId);
        return;
    }

    const QString before = m_editor->property("text").toString();
    m_editor->setProperty("cursorPosition", before.size());
    QMetaObject::invokeMethod(m_editor, "forceActiveFocus");

    QInputMethodEvent preedit(QStringLiteral("拼"), {});
    const bool preeditAccepted = QCoreApplication::sendEvent(m_editor, &preedit);
    const bool composingObserved = m_editor->property("inputMethodComposing").toBool();

    QInputMethodEvent commit;
    commit.setCommitString(QStringLiteral("中文"));
    const bool commitAccepted = QCoreApplication::sendEvent(m_editor, &commit);
    const QString after = m_editor->property("text").toString();
    const bool committed = after.endsWith(QStringLiteral("中文"));
    const bool compositionEnded = !m_editor->property("inputMethodComposing").toBool();
    m_editor->setProperty("text", before);

    QJsonObject response;
    response.insert(QStringLiteral("command"), QStringLiteral("benchmarkIme"));
    response.insert(QStringLiteral("preeditAccepted"), preeditAccepted);
    response.insert(QStringLiteral("composingObserved"), composingObserved);
    response.insert(QStringLiteral("commitAccepted"), commitAccepted);
    response.insert(QStringLiteral("committedCjk"), committed);
    response.insert(QStringLiteral("compositionEnded"), compositionEnded);
    sendResponse(socket, response, startedNs, requestId);
}

void EditorController::runAnimationBenchmark(QLocalSocket *socket, qint64 startedNs,
                                             const QString &requestId)
{
    if (!isVisible()) {
        sendError(socket, QStringLiteral("benchmarkAnimation"),
                  QStringLiteral("editor must be visible"), startedNs, requestId);
        return;
    }
    if (m_pendingAnimation.socket) {
        sendError(socket, QStringLiteral("benchmarkAnimation"),
                  QStringLiteral("animation benchmark already running"), startedNs, requestId);
        return;
    }

    m_pendingAnimation = {socket, startedNs, requestId};
    m_animationFrameIntervals.clear();
    m_lastAnimationFrameNs = 0;
    m_animationTimer.restart();
    m_animationFrameConnection = connect(
        m_window, &QQuickWindow::frameSwapped, this,
        [this] {
            const qint64 now = m_animationTimer.nsecsElapsed();
            const QMutexLocker locker(&m_animationMutex);
            if (m_lastAnimationFrameNs > 0) {
                m_animationFrameIntervals.append((now - m_lastAnimationFrameNs) / 1'000'000.0);
            }
            m_lastAnimationFrameNs = now;
        },
        Qt::DirectConnection);

    if (!QMetaObject::invokeMethod(m_window, "runBenchmarkAnimation")) {
        disconnect(m_animationFrameConnection);
        const PendingRequest pending = m_pendingAnimation;
        m_pendingAnimation = {};
        sendError(pending.socket, QStringLiteral("benchmarkAnimation"),
                  QStringLiteral("could not start QML animation"), pending.startedNs,
                  pending.requestId);
    }
}

void EditorController::animationBenchmarkFinished()
{
    if (!m_pendingAnimation.socket) {
        return;
    }

    disconnect(m_animationFrameConnection);
    const PendingRequest pending = m_pendingAnimation;
    m_pendingAnimation = {};

    QVector<double> frameIntervals;
    {
        const QMutexLocker locker(&m_animationMutex);
        frameIntervals = m_animationFrameIntervals;
    }
    const double sum = std::accumulate(frameIntervals.cbegin(), frameIntervals.cend(), 0.0);
    const double average = frameIntervals.isEmpty()
        ? 0.0
        : sum / frameIntervals.size();

    QJsonObject response;
    response.insert(QStringLiteral("command"), QStringLiteral("benchmarkAnimation"));
    response.insert(QStringLiteral("durationMs"), sum);
    response.insert(QStringLiteral("frameCount"), frameIntervals.size() + 1);
    response.insert(QStringLiteral("fps"),
                    sum > 0.0 ? frameIntervals.size() * 1000.0 / sum : 0.0);
    response.insert(QStringLiteral("averageFrameMs"), average);
    response.insert(QStringLiteral("p95FrameMs"), percentile(frameIntervals, 0.95));
    response.insert(QStringLiteral("maxFrameMs"), percentile(frameIntervals, 1.0));
    sendResponse(pending.socket, response, pending.startedNs, pending.requestId);
}

bool EditorController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_editor && event->type() == QEvent::KeyPress && m_pendingInput.socket) {
        const auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (!keyEvent->isAutoRepeat()) {
            const PendingRequest pending = m_pendingInput;
            m_pendingInput = {};
            m_inputTimer.restart();

            QPointer<QLocalSocket> guardedSocket = pending.socket;
            connect(m_window, &QQuickWindow::frameSwapped, this,
                    [this, guardedSocket, pending] {
                        const double frameMs = m_inputTimer.nsecsElapsed() / 1'000'000.0;
                        QMetaObject::invokeMethod(this, [this, guardedSocket, pending, frameMs] {
                            ++m_inputSequence;
                            QJsonObject response;
                            response.insert(QStringLiteral("command"),
                                            QStringLiteral("awaitInputFrame"));
                            response.insert(QStringLiteral("inputFrameMs"), frameMs);
                            response.insert(QStringLiteral("sequence"),
                                            static_cast<qint64>(m_inputSequence));
                            response.insert(QStringLiteral("textLength"),
                                            m_editor
                                                ? m_editor->property("text").toString().size()
                                                : -1);
                            sendResponse(guardedSocket, response, pending.startedNs,
                                         pending.requestId);
                        }, Qt::QueuedConnection);
                    }, Qt::ConnectionType(Qt::DirectConnection | Qt::SingleShotConnection));
        }
    }
    return QObject::eventFilter(watched, event);
}
