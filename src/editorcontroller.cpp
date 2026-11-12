#include "editorcontroller.h"
#include "editortestsupport.h"
#include "appsettings.h"
#include "cjktextprocessor.h"
#include "clipboardhistorycommandgate.h"
#include "clipboardhistorycoordinator.h"
#include "editorcommandregistry.h"
#include "externalfilesession.h"
#include "markdownhighlighter.h"
#include "markdownstyle.h"
#include "statuspanelhints.h"
#include "uiconfig.h"
#include "windowplacement.h"

#include <QColor>
#include <QCoreApplication>
#include <QCursor>
#include <QDesktopServices>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QLocalSocket>
#include <QMetaObject>
#include <QMouseEvent>
#include <QParallelAnimationGroup>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickTextDocument>
#include <QScreen>
#include <QSignalBlocker>
#include <QStyleHints>
#include <QThread>
#include <QTimer>
#include <QTextDocument>
#include <QVariantAnimation>
#include <QtMath>

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>

#ifdef Q_OS_WIN
#  include <dwmapi.h>
#  include <tlhelp32.h>
#  include <windows.h>
#endif

namespace {

QString plainCommand(const QByteArray &line)
{
    return QString::fromUtf8(line).trimmed();
}

#ifdef Q_OS_WIN
QRect nativeWindowLogicalRect(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd) || IsIconic(hwnd)) {
        return {};
    }

    RECT nativeRect{};
    if (!GetWindowRect(hwnd, &nativeRect)) {
        return {};
    }
    const QRect physical(nativeRect.left, nativeRect.top,
                         nativeRect.right - nativeRect.left,
                         nativeRect.bottom - nativeRect.top);
    if (physical.width() <= 0 || physical.height() <= 0) {
        return {};
    }

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(MONITORINFO);
    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) {
        return {};
    }
    const RECT &nativeMonitor = monitorInfo.rcMonitor;
    const QRect monitorRect(nativeMonitor.left, nativeMonitor.top,
                            nativeMonitor.right - nativeMonitor.left,
                            nativeMonitor.bottom - nativeMonitor.top);

    // Windows 上 Qt 的多屏全局原点仍使用原生屏幕位置，只有各屏的尺寸
    // 按 DPR 缩放。因此不能把非主屏的 x/y 再乘 DPR，否则会在混合
    // 缩放布局中匹配失败并回退到主屏。
    QScreen *screen = nullptr;
    bool exactMatch = false;
    qint64 bestIntersectionArea = 0;
    for (QScreen *candidate : QGuiApplication::screens()) {
        const qreal dpr = candidate->devicePixelRatio();
        const QRect logical = candidate->geometry();
        const QRect candidatePhysical(
            logical.topLeft(),
            QSize(qRound(logical.width() * dpr), qRound(logical.height() * dpr)));
        if (candidatePhysical == monitorRect) {
            screen = candidate;
            exactMatch = true;
            break;
        }
        const QRect intersection = candidatePhysical.intersected(monitorRect);
        const qint64 area = intersection.isValid()
            ? static_cast<qint64>(intersection.width()) * intersection.height()
            : 0;
        if (area > bestIntersectionArea) {
            bestIntersectionArea = area;
            screen = candidate;
        }
    }
    if (!screen || (!exactMatch && bestIntersectionArea == 0)) {
        return {};
    }

    return WindowPlacement::nativeToLogicalRect(
        physical, monitorRect, screen->geometry());
}

QRect logicalToNativeRect(const QRect &logicalRect, QScreen *screen)
{
    if (!screen || !logicalRect.isValid()) {
        return {};
    }
    const QRect logicalScreen = screen->geometry();
    const qreal dpr = screen->devicePixelRatio();
    if (logicalScreen.isEmpty() || dpr <= 0.0) {
        return {};
    }
    return QRect(
        qRound(logicalScreen.x() + (logicalRect.x() - logicalScreen.x()) * dpr),
        qRound(logicalScreen.y() + (logicalRect.y() - logicalScreen.y()) * dpr),
        qMax(1, qRound(logicalRect.width() * dpr)),
        qMax(1, qRound(logicalRect.height() * dpr)));
}

QString parentProcessExeName()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return {};
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    const DWORD currentPid = GetCurrentProcessId();
    DWORD parentPid = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == currentPid) {
                parentPid = entry.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    QString result;
    if (parentPid != 0 && Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == parentPid) {
                result = QString::fromWCharArray(entry.szExeFile);
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

QString cliLabelForParentProcess()
{
    const QString exe = parentProcessExeName().toLower();
    if (exe.contains("codex")) {
        return QStringLiteral("Codex");
    }
    if (exe == QStringLiteral("pi.exe") || exe.startsWith(QStringLiteral("pi."))) {
        return QStringLiteral("pi");
    }
    if (exe.contains("claude")) {
        return QStringLiteral("Claude Code");
    }
    // Node 承载的 CLI 通过环境变量进一步区分。
    if (exe == QStringLiteral("node.exe")
        && !qEnvironmentVariableIsEmpty("CODEX_HOME")) {
        return QStringLiteral("Codex");
    }
    return {};
}

#endif

} // namespace

EditorController::EditorController(bool testMode, QElapsedTimer *startupTimer,
                                   const QString &externalFilePath, QObject *parent)
    : QObject(parent)
    , m_startupTimer(startupTimer)
    , m_testMode(testMode)
{
#ifdef Q_OS_WIN
    // 外部编辑进程由 CLI 同步启动。在 QML 和原生窗口创建前立即快照
    // 前台窗口，避免后续初始化期间焦点变化后错把另一个 CLI/IDE 窗口当成唤起者。
    if (!externalFilePath.isEmpty()) {
        const HWND foreground = GetForegroundWindow();
        if (foreground && IsWindow(foreground)) {
            m_startupForegroundWindow = reinterpret_cast<quintptr>(foreground);
        }
        m_externalCliType = cliLabelForParentProcess();
    }
#endif
    if (!externalFilePath.isEmpty()) {
        m_externalFileSession = std::make_unique<ExternalFileSession>(externalFilePath);
        m_externalFileReady = m_externalFileSession->load(&m_externalFileText,
                                                          &m_externalFileError);
        m_statusMessage = m_externalFileReady
            ? QStringLiteral("Ctrl+S / Esc 保存并返回 CLI · Ctrl+W 不保存退出")
            : m_externalFileError;
        m_statusHealthy = m_externalFileReady;
    }
    m_uiConfig = std::make_unique<UiConfig>(UiConfig::load(m_testMode));
    m_settings = std::make_unique<AppSettings>(m_testMode, m_uiConfig.get());
    const QString historyPath = QDir(QFileInfo(m_settings->fileName()).absolutePath())
                                    .filePath(QStringLiteral("clipboard-history.dat"));
    m_clipboardHistory = std::make_unique<ClipboardHistoryCoordinator>(
        m_testMode, !externalFileMode(), historyPath);
    connect(m_clipboardHistory.get(), &ClipboardHistoryCoordinator::stateChanged,
            this, &EditorController::clipboardHistoryStateChanged);
    m_markdownStyle = std::make_unique<MarkdownStyle>(MarkdownStyle::load(m_testMode));
    m_markdownStyleReloadTimer.setSingleShot(true);
    m_markdownStyleReloadTimer.setInterval(150);
    connect(&m_markdownStyleWatcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &) { m_markdownStyleReloadTimer.start(); });
    connect(&m_markdownStyleWatcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString &) { m_markdownStyleReloadTimer.start(); });
    connect(&m_markdownStyleReloadTimer, &QTimer::timeout,
            this, &EditorController::reloadMarkdownStyle);
    configureMarkdownStyleWatcher();
    reloadAppearance();
    m_commands = std::make_unique<EditorCommandRegistry>(
        m_settings.get(), clipboardHistoryAvailable(), this);
    connect(m_commands.get(), &EditorCommandRegistry::commandsChanged,
            this, &EditorController::commandsChanged);
    connect(m_commands.get(), &EditorCommandRegistry::commandsChanged,
            this, &EditorController::statusPanelHintsChanged);
    connect(m_commands.get(), &EditorCommandRegistry::headingFoldMarkersChanged,
            this, &EditorController::headingFoldMarkersChanged);
    connect(m_commands.get(), &EditorCommandRegistry::headingNavigationHighlightChanged,
            this, &EditorController::headingNavigationHighlightChanged);
    connect(m_commands.get(), &EditorCommandRegistry::uiCommandRequested,
            this, &EditorController::uiCommandRequested);
    m_commands->setClipboardAccess(
        [this] {
            QString clipboardText;
            QString errorMessage;
            return readClipboardText(&clipboardText, &errorMessage)
                ? clipboardText : QString();
        },
        [this](const QString &text) {
            QString errorMessage;
            return writeClipboardText(text, &errorMessage);
        });
    m_monotonic.start();
    connect(&m_server, &QLocalServer::newConnection, this, &EditorController::acceptConnections);
    m_screenConfigurationTimer.setSingleShot(true);
    m_screenConfigurationTimer.setInterval(160);
    connect(&m_screenConfigurationTimer, &QTimer::timeout,
            this, &EditorController::handleScreenConfigurationChanged);
    m_displayChangeSettleTimer.setSingleShot(true);
    m_displayChangeSettleTimer.setInterval(2000);
    connect(&m_displayChangeSettleTimer, &QTimer::timeout, this, [this] {
        m_nativeDisplayChangeActive = false;
    });
    QGuiApplication::instance()->installNativeEventFilter(this);
    for (QScreen *screen : QGuiApplication::screens()) {
        watchScreen(screen);
    }
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) {
        watchScreen(screen);
        m_screenAdditionPending = true;
        scheduleScreenConfigurationUpdate();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *removed) {
        m_screenAdditionPending = false;
#ifdef Q_OS_WIN
        // 用窗口实际原生矩形与“被移除屏幕”的物理范围做重叠判断来快照锚点。
        // 副屏被移除时 Qt 会先把窗口屏幕改到主屏，直接比较 window->screen()
        // 会漏掉快照时机，导致恢复双屏后窗口回不到原副屏。
        const HWND hwnd = m_window ? reinterpret_cast<HWND>(m_window->winId())
                                   : nullptr;
        RECT nativeRect{};
        if (m_window && removed && hwnd && IsWindow(hwnd)
            && GetWindowRect(hwnd, &nativeRect)) {
            const QRect physical(nativeRect.left, nativeRect.top,
                                 nativeRect.right - nativeRect.left,
                                 nativeRect.bottom - nativeRect.top);
            const QRect logicalScreen = removed->geometry();
            const qreal dpr = removed->devicePixelRatio();
            const QRect nativeScreen(
                logicalScreen.topLeft(),
                QSize(qRound(logicalScreen.width() * dpr),
                      qRound(logicalScreen.height() * dpr)));
            if (nativeScreen.intersects(physical)) {
                const QRect logicalRect = WindowPlacement::nativeToLogicalRect(
                    physical, nativeScreen, logicalScreen);
                if (logicalRect.isValid()) {
                    m_windowScreenName = removed->name();
                    m_windowScreenOffset =
                        logicalRect.topLeft() - logicalScreen.topLeft();
                }
            }
        }
#else
        if (m_window && removed && m_window->screen() == removed) {
            m_windowScreenName = removed->name();
            m_windowScreenOffset =
                m_window->geometry().topLeft() - removed->geometry().topLeft();
        }
#endif
        scheduleScreenConfigurationUpdate();
    });
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, [this](QScreen *) {
        scheduleScreenConfigurationUpdate();
    });
    buildCommandHandlers();
    if (m_testMode) {
        m_testSupport = std::make_unique<TestSupport>(*this);
        m_testSupport->registerCommands(m_commandHandlers);
    }
}

EditorController::~EditorController()
    = default;

bool EditorController::nativeEventFilter(const QByteArray &eventType, void *message,
                                         qintptr *result)
{
    Q_UNUSED(result);
#ifdef Q_OS_WIN
    if (eventType == QByteArrayLiteral("windows_generic_MSG")
        || eventType == QByteArrayLiteral("windows_dispatcher_MSG")) {
        const auto *msg = static_cast<const MSG *>(message);
        if (msg && msg->message == WM_DISPLAYCHANGE) {
            // 显示拓扑变化的第一时间冻结锚点更新：此后 Qt/Windows 的窗口迁移
            // 事件（screenChanged/Move/Resize）都不再改写窗口的“原屏幕记忆”，
            // 直到布局稳定（2 秒）后恢复实时跟踪。
            m_nativeDisplayChangeActive = true;
            m_displayChangeSettleTimer.start();
            scheduleScreenConfigurationUpdate();
        } else if (msg && msg->message == WM_CLIPBOARDUPDATE
                   && clipboardHistoryAvailable()) {
            QTimer::singleShot(0, m_clipboardHistory.get(), [this] {
                m_clipboardHistory->processClipboardChange();
            });
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
#endif
    return false;
}

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

QJsonObject EditorController::queryExistingInstance(const QString &command, int timeoutMs) const
{
    QLocalSocket socket;
    socket.connectToServer(serverName(), QIODevice::ReadWrite);
    if (!socket.waitForConnected(timeoutMs)) {
        return {};
    }

    const QJsonObject body{{QStringLiteral("command"), command},
                           {QStringLiteral("requestId"), QStringLiteral("external-query")}};
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact) + '\n';
    if (socket.write(payload) != payload.size() || !socket.waitForBytesWritten(timeoutMs)) {
        return {};
    }

    QByteArray response;
    while (!response.contains('\n')) {
        if (!socket.waitForReadyRead(timeoutMs)) {
            return {};
        }
        response += socket.readAll();
    }
    const QJsonDocument document =
        QJsonDocument::fromJson(response.left(response.indexOf('\n')));
    return document.isObject() ? document.object() : QJsonObject{};
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

bool EditorController::externalFileMode() const
{
    return bool(m_externalFileSession);
}

bool EditorController::externalFileReady() const
{
    return !externalFileMode() || m_externalFileReady;
}

QString EditorController::externalFileName() const
{
    return m_externalFileSession
        ? QFileInfo(m_externalFileSession->filePath()).fileName()
        : QString();
}

QString EditorController::externalFileError() const
{
    return m_externalFileError;
}

QString EditorController::externalCliType() const
{
    return m_externalCliType;
}

bool EditorController::completeExternalFileTest(const QString &text)
{
    if (!m_testMode || !externalFileMode() || !m_editor) {
        return false;
    }
    m_editor->setProperty("text", text);
    return commitExternalFileAndExit();
}

QString EditorController::statusMessage() const
{
    return m_statusMessage;
}

bool EditorController::statusHealthy() const
{
    return m_statusHealthy;
}

int EditorController::statusPanelFontSize() const
{
    return m_settings ? m_settings->statusPanel().fontSize
                      : (m_uiConfig ? m_uiConfig->statusPanelDefaultFontSize() : 10);
}

int EditorController::statusPanelShowDelayMs() const
{
    return m_settings ? m_settings->statusPanel().showDelayMs
                      : (m_uiConfig ? m_uiConfig->statusPanelDefaultShowDelayMs() : 300);
}

int EditorController::statusPanelHideDelayMs() const
{
    return m_settings ? m_settings->statusPanel().hideDelayMs
                      : (m_uiConfig ? m_uiConfig->statusPanelDefaultHideDelayMs() : 250);
}

int EditorController::statusPanelMaxWidth() const
{
    return m_settings ? m_settings->statusPanel().maxWidth
                      : (m_uiConfig ? m_uiConfig->statusPanelDefaultMaxWidth() : 360);
}

QStringList EditorController::statusPanelHints() const
{
    return StatusPanelHints::forMode(
        externalFileMode(),
        m_commands ? m_commands->shortcut(QStringLiteral("commandPalette")) : QString(),
        m_commands ? m_commands->shortcut(QStringLiteral("settings")) : QString());
}

QString EditorController::statusPanelSummary() const
{
    return m_statusPanelSummary;
}

bool EditorController::applyStatusPanelSettings(int fontSize, int showDelayMs,
                                                int hideDelayMs, int maxWidth)
{
    if (!m_settings) {
        return false;
    }
    QString error;
    if (!m_settings->setStatusPanel(fontSize, showDelayMs, hideDelayMs, maxWidth, &error)) {
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
    emit statusPanelSettingsChanged();
    return true;
}

void EditorController::resetStatusPanelSettings()
{
    if (!m_settings) {
        return;
    }
    m_settings->resetStatusPanel();
    if (!m_settingsError.isEmpty()) {
        m_settingsError.clear();
        emit settingsErrorChanged();
    }
    emit statusPanelSettingsChanged();
}

bool EditorController::copyToClipboard(const QString &text)
{
    QString clipboardError;
    if (writeClipboardText(text, &clipboardError)) {
        return true;
    }
    setClipboardState(false, clipboardError);
    return false;
}

bool EditorController::clipboardHealthy() const
{
    return m_clipboardHealthy;
}

QAbstractItemModel *EditorController::clipboardHistoryModel() const
{
    return m_clipboardHistory ? m_clipboardHistory->model() : nullptr;
}

bool EditorController::clipboardHistoryAvailable() const
{
    return m_clipboardHistory && m_clipboardHistory->available();
}

bool EditorController::clipboardHistoryHealthy() const
{
    return m_clipboardHistory && m_clipboardHistory->healthy();
}

QString EditorController::clipboardHistoryError() const
{
    return m_clipboardHistory ? m_clipboardHistory->error() : QString();
}

bool EditorController::historyLoadConfirmationVisible() const
{
    return m_historyLoadConfirmationVisible;
}

bool EditorController::historyClearConfirmationVisible() const
{
    return m_historyClearConfirmationVisible;
}

int EditorController::historyCardHeight() const
{
    return m_settings ? m_settings->historyCardHeight()
                      : (m_uiConfig ? m_uiConfig->historyCardHeightDefault() : 58);
}

QVariantList EditorController::commands() const
{
    return m_commands ? m_commands->commands() : QVariantList{};
}

QVariantList EditorController::headingFoldMarkers() const
{
    return m_commands ? m_commands->headingFoldMarkers() : QVariantList{};
}

int EditorController::headingFoldVisibleEndPosition() const
{
    return m_commands ? m_commands->headingFoldVisibleEndPosition() : 0;
}

QVariantMap EditorController::headingNavigationHighlight() const
{
    return m_commands ? m_commands->headingNavigationHighlight() : QVariantMap{};
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

QString EditorController::editorFallbackFontFamily() const
{
    return m_editorFallbackFontFamily;
}

QFont EditorController::editorFont() const
{
    QFont font(QStringList{m_editorFontFamily, m_editorFallbackFontFamily},
               m_editorFontPointSize);
    font.setWeight(static_cast<QFont::Weight>(m_editorFontWeight));
    return font;
}

int EditorController::editorFontPointSize() const
{
    return m_editorFontPointSize;
}

int EditorController::editorFontWeight() const
{
    return m_editorFontWeight;
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

QString EditorController::markdownTextColor() const
{
    return m_markdownStyle
        ? m_markdownStyle->baseText.foreground.name(QColor::HexRgb)
        : QStringLiteral("#c2c0b6");
}

QString EditorController::themeAccentColor() const
{
    return m_markdownStyle
        ? m_markdownStyle->accentColor.name(QColor::HexRgb)
        : QString();
}

QString EditorController::themeAccentTextColor() const
{
    return m_markdownStyle
        ? m_markdownStyle->accentTextColor.name(QColor::HexRgb)
        : QString();
}

QString EditorController::markdownStyleFile() const
{
    return m_markdownStyle ? m_markdownStyle->filePath() : QString();
}

bool EditorController::markdownStyleLoaded() const
{
    return m_markdownStyle && m_markdownStyle->loadedFromFile();
}

QVariant EditorController::uiConfig() const
{
    return m_uiConfig ? m_uiConfig->map() : QVariantMap{};
}

void EditorController::registerWindow(QQuickWindow *window)
{
    if (m_window) {
        m_window->removeEventFilter(this);
    }
    m_window = window;
    if (m_commands) {
        m_commands->setWindow(window);
    }
    if (m_window) {
        m_window->installEventFilter(this);
        connect(m_window, &QQuickWindow::screenChanged, this, [this](QScreen *screen) {
            if (screen && m_window && m_window->isVisible() && !m_hiding
                && m_windowRestingGeometry.isValid()
                && !(m_windowTransitionGroup
                     && m_windowTransitionGroup->state() == QAbstractAnimation::Running)
                && !m_screenConfigurationTimer.isActive()
                && !m_nativeDisplayChangeActive) {
                const QString screenName = screen->name();
                const QPoint screenOffset =
                    m_window->geometry().topLeft() - screen->geometry().topLeft();
                if (m_windowScreenName != screenName
                    || m_windowScreenOffset != screenOffset) {
                    m_windowScreenName = screenName;
                    m_windowScreenOffset = screenOffset;
                }
            }
        });
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
        if (clipboardHistoryAvailable()) {
            m_clipboardHistory->startMonitoring(
                reinterpret_cast<quintptr>(m_window->winId()));
        }
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
    if (document && m_markdownStyle) {
        m_markdownHighlighter = new MarkdownHighlighter(
            document, *m_markdownStyle, m_editorFontWeight);
    }
    if (m_commands) {
        m_commands->setEditor(m_editor, document);
    }
    if (m_editor) {
        QObject::connect(m_editor, SIGNAL(textChanged()),
                         this, SLOT(updateStatusPanelSummary()));
        QObject::connect(m_editor, SIGNAL(selectionStartChanged()),
                         this, SLOT(updateStatusPanelSummary()));
        QObject::connect(m_editor, SIGNAL(selectionEndChanged()),
                         this, SLOT(updateStatusPanelSummary()));
        updateStatusPanelSummary();
    }
    emit markdownHighlightingChanged();
    updateReadyState();
}

bool EditorController::executeCommand(const QString &commandId)
{
    return m_commands && m_commands->execute(commandId);
}

bool EditorController::insertDroppedUrls(const QList<QUrl> &urls)
{
    if (!m_commands || urls.isEmpty()) {
        return false;
    }

    QStringList paths;
    paths.reserve(urls.size());
    for (const QUrl &url : urls) {
        if (!url.isValid() || !url.isLocalFile()) {
            return false;
        }
        const QString localFile = url.toLocalFile();
        if (localFile.isEmpty()) {
            return false;
        }
        const QFileInfo fileInfo(localFile);
        if (!fileInfo.exists() || (!fileInfo.isFile() && !fileInfo.isDir())) {
            return false;
        }

        QString path = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
        const bool containsWhitespace = std::any_of(
            path.cbegin(), path.cend(), [](QChar character) { return character.isSpace(); });
        if (containsWhitespace) {
            path = QStringLiteral("\"") + path + QStringLiteral("\"");
        }
        paths.append(std::move(path));
    }
    return m_commands->insertPathText(paths.join(QLatin1Char(' ')));
}

bool EditorController::toggleHeadingFoldAt(int headingPosition)
{
    return m_commands && m_commands->toggleHeadingFoldAt(headingPosition);
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
                                       const QString &fallbackFontFamily,
                                       int fontPointSize, int fontWeight,
                                       bool animationsEnabled)
{
    if (!m_settings) {
        return false;
    }
    QString error;
    if (!m_settings->setAppearance(theme, fontFamily, fallbackFontFamily,
                                   fontPointSize, fontWeight, animationsEnabled, &error)) {
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

bool EditorController::setMarkdownLinkHoverPosition(int position)
{
    if (!m_markdownHighlighter) {
        return false;
    }
    return m_markdownHighlighter->setHoveredPosition(position);
}

bool EditorController::openMarkdownLinkAt(int position)
{
    if (!m_markdownHighlighter) {
        return false;
    }
    QUrl url = m_markdownHighlighter->externalLinkAt(position);
    if (url.isEmpty() && position > 0) {
        url = m_markdownHighlighter->externalLinkAt(position - 1);
    }
    return !url.isEmpty() && QDesktopServices::openUrl(url);
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
    const bool fontWeightChanged = m_editorFontWeight != appearance.fontWeight;
    const bool changed = m_theme != appearance.theme
        || m_editorFontFamily != appearance.fontFamily
        || m_editorFallbackFontFamily != appearance.fallbackFontFamily
        || m_editorFontPointSize != appearance.fontPointSize
        || m_editorFontWeight != appearance.fontWeight
        || m_animationsEnabled != appearance.animationsEnabled;
    m_theme = appearance.theme;
    m_editorFontFamily = appearance.fontFamily;
    m_editorFallbackFontFamily = appearance.fallbackFontFamily;
    m_editorFontPointSize = appearance.fontPointSize;
    m_editorFontWeight = appearance.fontWeight;
    m_animationsEnabled = appearance.animationsEnabled;
    if (fontWeightChanged && m_markdownHighlighter && m_markdownStyle) {
        m_markdownHighlighter->setStyle(*m_markdownStyle, m_editorFontWeight);
    }
    if (m_window) {
        applyNativeWindowStyle();
    }
    if (changed) {
        emit appearanceChanged();
    }
}

void EditorController::configureMarkdownStyleWatcher()
{
    if (!m_markdownStyle) {
        return;
    }
    const QString filePath = QFileInfo(m_markdownStyle->filePath()).absoluteFilePath();
    const QString directoryPath = QFileInfo(filePath).absolutePath();
    for (const QString &watched : m_markdownStyleWatcher.files()) {
        if (QFileInfo(watched).absoluteFilePath() != filePath) {
            m_markdownStyleWatcher.removePath(watched);
        }
    }
    for (const QString &watched : m_markdownStyleWatcher.directories()) {
        if (QFileInfo(watched).absoluteFilePath() != directoryPath) {
            m_markdownStyleWatcher.removePath(watched);
        }
    }
    if (QFileInfo::exists(filePath) && !m_markdownStyleWatcher.files().contains(filePath)) {
        m_markdownStyleWatcher.addPath(filePath);
    }
    if (QFileInfo::exists(directoryPath)
        && !m_markdownStyleWatcher.directories().contains(directoryPath)) {
        m_markdownStyleWatcher.addPath(directoryPath);
    }
}

void EditorController::reloadMarkdownStyle()
{
    MarkdownStyle reloaded = MarkdownStyle::load(m_testMode);
    if (!reloaded.loadedFromFile()) {
        configureMarkdownStyleWatcher();
        return;
    }
    m_markdownStyle = std::make_unique<MarkdownStyle>(std::move(reloaded));
    configureMarkdownStyleWatcher();
    if (m_markdownHighlighter) {
        m_markdownHighlighter->setStyle(*m_markdownStyle, m_editorFontWeight);
    }
    emit markdownStyleChanged();
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

    if (isClipboardHistoryTestCommand(command)
        && !clipboardHistoryTestCommandAllowed(command, m_testMode)) {
        sendError(socket, command, QStringLiteral("unsupported command"),
                  startedNs, requestId);
        return;
    }

    const auto it = m_commandHandlers.constFind(command);
    if (it == m_commandHandlers.constEnd()) {
        if (!m_ready) {
            sendError(socket, command, QStringLiteral("QML window is not ready"),
                      startedNs, requestId);
        } else {
            sendError(socket, command,
                      m_testMode ? QStringLiteral("unsupported test command")
                                 : QStringLiteral("unsupported command"),
                      startedNs, requestId);
        }
        return;
    }

    if (it->gate == CommandEntry::Gate::Ready || it->gate == CommandEntry::Gate::Test) {
        if (!m_ready) {
            sendError(socket, command, QStringLiteral("QML window is not ready"),
                      startedNs, requestId);
            return;
        }
        if (it->gate == CommandEntry::Gate::Test && !m_testMode) {
            sendError(socket, command, QStringLiteral("unsupported command"),
                      startedNs, requestId);
            return;
        }
    }

    it->handler({socket, request, command, requestId, noReply, startedNs});
}

void EditorController::buildCommandHandlers()
{
    using Gate = CommandEntry::Gate;

    const auto respondWithStatus = [this](const DispatchRequest &r) {
        if (r.noReply) {
            return;
        }
        QJsonObject response = statusObject();
        response.insert(QStringLiteral("command"), r.command);
        sendResponse(r.socket, response, r.startedNs, r.requestId);
    };

    m_commandHandlers = {
        {QStringLiteral("ping"), {Gate::PreReady, respondWithStatus}},
        {QStringLiteral("status"), {Gate::PreReady, respondWithStatus}},
        {QStringLiteral("getWindowGeometry"), {Gate::PreReady, [this](const DispatchRequest &r) {
            if (r.noReply) {
                return;
            }
            // 只读查询：供外部编辑进程获取常驻临时编辑器的 resting 几何，
            // 用于避免唤起时窗口重叠。
            QJsonObject response;
            const bool positioned = m_positioned && m_window;
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("valid"), positioned);
            if (positioned) {
                const QRect geometry = m_windowRestingGeometry.isValid()
                    ? m_windowRestingGeometry
                    : m_window->geometry();
                response.insert(QStringLiteral("x"), geometry.x());
                response.insert(QStringLiteral("y"), geometry.y());
                response.insert(QStringLiteral("width"), geometry.width());
                response.insert(QStringLiteral("height"), geometry.height());
            }
            sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},

        {QStringLiteral("toggle"), {Gate::Ready, [this](const DispatchRequest &r) {
            if (r.noReply) {
                toggleEditor();
                return;
            }
            if (isVisible()) {
                commitAndHide();
                QJsonObject response = statusObject();
                response.insert(QStringLiteral("command"), r.command);
                sendResponse(r.socket, response, r.startedNs, r.requestId);
            } else {
                showForRequest(r.socket, r.command, r.startedNs, r.requestId, r.request);
            }
        }}},
        {QStringLiteral("show"), {Gate::Ready, [this](const DispatchRequest &r) {
            if (r.noReply) {
                showEditor();
                return;
            }
            if (isVisible()) {
                m_window->raise();
                m_window->requestActivate();
                QJsonObject response = statusObject();
                response.insert(QStringLiteral("command"), r.command);
                sendResponse(r.socket, response, r.startedNs, r.requestId);
            } else {
                showForRequest(r.socket, r.command, r.startedNs, r.requestId, r.request);
            }
        }}},
        {QStringLiteral("hide"), {Gate::Ready, [this](const DispatchRequest &r) {
            if (r.noReply) {
                commitAndHide();
                return;
            }
            commitAndHide();
            QJsonObject response = statusObject();
            response.insert(QStringLiteral("command"), r.command);
            sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("shutdownForUpdate"), {Gate::Ready, [this](const DispatchRequest &r) {
            commitAndHide();
            QJsonObject response;
            response.insert(QStringLiteral("command"), r.command);
            sendResponse(r.socket, response, r.startedNs, r.requestId);
            QTimer::singleShot(25, qApp, [] { QCoreApplication::exit(0); });
        }}},

    };
}

void EditorController::sendResponse(QLocalSocket *socket, QJsonObject response, qint64 startedNs,
                                    const QString &requestId)
{
    if (!socket || socket->state() == QLocalSocket::UnconnectedState) {
        return;
    }

    if (!response.contains(QStringLiteral("ok"))) {
        response.insert(QStringLiteral("ok"), true);
    }
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
                    if (m_testSupport) {
                        m_testSupport->captureFirstFrame();
                    }
                    QJsonObject finalResponse = response;
                    const QJsonObject status = statusObject();
                    for (auto it = status.begin(); it != status.end(); ++it) {
                        finalResponse.insert(it.key(), it.value());
                    }
                    sendResponse(guardedSocket, finalResponse, startedNs, requestId);
                };

                if (m_testSupport && m_testSupport->needsFirstFrame()) {
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
    m_discardClose = false;
    ++m_focusGeneration;

    // 记录唤起窗口：外部模式使用进程初始化时的前台快照，常驻模式
    // 使用每次唤起时的前台窗口。Windows Terminal/ConPTY 不保证终端窗口
    // 出现在 CLI 的进程祖先链中，因此不用祖先进程的其他窗口覆盖前台快照。
    std::optional<QRect> referenceRect;
#ifdef Q_OS_WIN
    {
        const HWND editorHwnd = reinterpret_cast<HWND>(m_window->winId());
        HWND invoker = externalFileMode()
            ? reinterpret_cast<HWND>(m_startupForegroundWindow)
            : GetForegroundWindow();
        if ((!invoker || !IsWindow(invoker)) && externalFileMode()) {
            invoker = GetForegroundWindow();
        }
        if (invoker && invoker != editorHwnd && IsWindow(invoker)) {
            if (externalFileMode()) {
                m_previousForegroundWindow = reinterpret_cast<quintptr>(invoker);
            }
            const QRect reference = nativeWindowLogicalRect(invoker);
            if (reference.isValid()) {
                referenceRect = reference;
            }
        }
    }
#endif

    if (externalFileMode()) {
        if (!m_externalFileReady) {
            setExternalFileState(false, m_externalFileError);
            return;
        }
        if (!m_externalFileLoadedIntoEditor) {
            if (m_commands) {
                m_commands->resetHeadingFolds();
            }
            m_editor->setProperty("text", m_externalFileText);
            m_editor->setProperty("cursorPosition", m_externalFileText.size());
            m_editorBaselineText = m_externalFileText;
            m_externalFileLoadedIntoEditor = true;
        }
        setExternalFileState(true,
                             QStringLiteral("Ctrl+S / Esc 保存并返回 CLI · Ctrl+W 不保存退出"));
    } else {
        QString clipboardText;
        QString clipboardError;
        const bool clipboardReady = readClipboardText(&clipboardText, &clipboardError);
        if (clipboardReady) {
            if (m_editor->property("text").toString() != clipboardText) {
                if (m_commands) {
                    m_commands->resetHeadingFolds();
                }
                m_editor->setProperty("text", clipboardText);
                // 新剪贴板内容默认光标落在文档末尾，与外部文件模式一致。
                m_editor->setProperty("cursorPosition", clipboardText.size());
            }
            m_editorBaselineText = clipboardText;
            setClipboardState(true);
        } else {
            setClipboardState(false, clipboardError);
        }
    }

    const QVector<QRect> screens = availableScreenGeometries();
    const QSize defaultSize = windowDefaultSize();
    const QSize minimumSize = windowMinimumSize();
    if (externalFileMode()) {
        // 外部提示词编辑器：只记忆大小，每次唤起都靠近调用它的窗口重新摆放，
        // 并尽量避开可能已打开的临时编辑器窗口。
        QRect obstacle;
        const QJsonObject response =
            queryExistingInstance(QStringLiteral("getWindowGeometry"));
        if (response.value(QStringLiteral("valid")).toBool()) {
            obstacle = QRect(response.value(QStringLiteral("x")).toInt(),
                             response.value(QStringLiteral("y")).toInt(),
                             response.value(QStringLiteral("width")).toInt(),
                             response.value(QStringLiteral("height")).toInt());
        }
        const QSize rememberedSize = m_settings
            ? m_settings->externalWindowGeometry().size()
            : QSize();
        m_windowRestingGeometry = WindowPlacement::placeNearWindow(
            rememberedSize, defaultSize, minimumSize, screens, referenceRect, obstacle,
            m_uiConfig ? m_uiConfig->placementAnchorGap() : WindowPlacement::AnchorGap);
        m_positioned = true;
    } else if (!m_positioned) {
        // 临时编辑器无记忆：在焦点窗口附近唤出。
        m_windowRestingGeometry = WindowPlacement::placeNearWindow(
            QSize(), defaultSize, minimumSize, screens, referenceRect, QRect(),
            m_uiConfig ? m_uiConfig->placementAnchorGap() : WindowPlacement::AnchorGap);
        m_positioned = true;
    } else if (m_windowRestingGeometry.isValid()) {
        // 临时编辑器有进程内记忆：每次打开按当前屏幕布局重新校正（兼容热插拔），
        // 记忆完全失效时回退到焦点附近。
        const auto fitted = WindowPlacement::fitRestoredGeometry(
            m_windowRestingGeometry, minimumSize, screens);
        m_windowRestingGeometry = fitted
            ? *fitted
            : WindowPlacement::placeNearWindow(
                  QSize(), defaultSize, minimumSize, screens, referenceRect, QRect(),
                  m_uiConfig ? m_uiConfig->placementAnchorGap()
                             : WindowPlacement::AnchorGap);
    }
    updateWindowAnchor();

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
    if (externalFileMode()) {
        commitExternalFileAndExit();
    } else {
        commitAndHide();
    }
}

void EditorController::deliverAndHide()
{
    if (externalFileMode()) {
        // 提示词临时编辑器：Ctrl+S 与 Esc 一样，保存并关闭本次编辑。
        commitExternalFileAndExit();
    } else {
        // 普通剪贴板临时编辑器：关闭并把内容输入到系统接下来交给的窗口。
        commitAndHide(true);
    }
}

void EditorController::discardAndHide()
{
    if (externalFileMode()) {
        // Ctrl+W：不写回文件，也不记忆窗口几何，完全回退到打开之前的状态。
        if (m_externalFileCompleted) {
            return;
        }
        m_externalFileCompleted = true;
        m_discardClose = true;
        restorePreviousFocus();
        QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(0); });
        return;
    }
    // 普通剪贴板临时编辑器：只关闭窗口，不回写剪贴板，也不持久化任何状态。
    commitAndHide(false, false);
}

bool EditorController::saveExternalFile()
{
    if (!externalFileMode() || !m_editor) {
        return false;
    }

    const QFileInfo fileInfo(m_externalFileSession->filePath());
    if (!fileInfo.exists() || !fileInfo.dir().exists()) {
        // 唤起它的终端已关闭并清理了临时文件：没有可写回的目标，静默关闭，
        // 不保存内容，也不把“找不到文件”当作需要用户处理的错误。
        m_externalFileError.clear();
        setExternalFileState(true, QStringLiteral("外部文件已不存在，直接退出"));
        return true;
    }
    if (!m_externalFileReady) {
        return false;
    }

    QString errorMessage;
    if (!m_externalFileSession->save(m_editor->property("text").toString(), &errorMessage)) {
        m_externalFileError = errorMessage;
        setExternalFileState(false, errorMessage);
        if (m_window) {
            m_window->raise();
            m_window->requestActivate();
        }
        QMetaObject::invokeMethod(m_editor, "forceActiveFocus");
        return false;
    }

    m_externalFileError.clear();
    setExternalFileState(true,
                         QStringLiteral("已保存 · Ctrl+S / Esc 保存并返回 CLI · Ctrl+W 不保存退出"));
    return true;
}

bool EditorController::commitExternalFileAndExit()
{
    if (m_externalFileCompleted) {
        return true;
    }
    if (!saveExternalFile()) {
        return false;
    }

    m_externalFileCompleted = true;
    saveWindowGeometry();
    restorePreviousFocus();
    QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(0); });
    return true;
}

bool EditorController::commitAndHide(bool deliverAfterHide, bool persistState)
{
    if (!m_window || (!m_window->isVisible() && !m_hiding) || !m_editor) {
        return true;
    }
    if (m_hiding) {
        return true;
    }
    cancelClipboardHistoryDrag();

    if (persistState) {
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
    } else {
        m_discardClose = true;
    }
    m_deliverAfterHide = deliverAfterHide;
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
            finishHideFocusHandoff();
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
        m_windowOpacityAnimation->setDuration(
            m_uiConfig ? m_uiConfig->transitionDuration() : 120);
        m_windowOpacityAnimation->setStartValue(startOpacity);
        m_windowOpacityAnimation->setEndValue(targetOpacity);
        m_windowOpacityAnimation->setEasingCurve(hideWhenFinished ? QEasingCurve::InCubic
                                                                  : QEasingCurve::OutCubic);
        m_windowGeometryAnimation->setDuration(
            m_uiConfig ? m_uiConfig->transitionDuration() : 120);
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
            finishHideFocusHandoff();
        }
    });
}

void EditorController::finishHideFocusHandoff()
{
    if (m_deliverAfterHide) {
        m_deliverAfterHide = false;
        deliverTextToNextWindow();
    } else {
        restorePreviousFocus();
    }
}

void EditorController::deliverTextToNextWindow()
{
    const quintptr editorWindow = m_window
        ? static_cast<quintptr>(m_window->winId()) : 0;
    QTimer::singleShot(m_testMode ? 0 : 150, this, [this, editorWindow] {
        if (!m_clipboardHistory) {
            return;
        }
        QString error;
        if (!m_clipboardHistory->deliverText(editorWindow, &error) && !error.isEmpty()) {
            setClipboardState(false, error);
        }
    });
}

QRect EditorController::scaledWindowGeometry(const QRect &restingGeometry) const
{
    if (!restingGeometry.isValid()) {
        return restingGeometry;
    }

    const qreal shapeScale =
        m_uiConfig ? m_uiConfig->windowShapeScale() : 0.98;
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
    if (!m_testMode && !externalFileMode() && isVisible()) {
        commitAndHide();
    }
    if (m_clipboardHistory) {
        m_clipboardHistory->shutdown();
    }
    m_server.close();
}

bool EditorController::readClipboardText(QString *text, QString *errorMessage)
{
    return m_clipboardHistory && m_clipboardHistory->readText(text, errorMessage);
}

bool EditorController::writeClipboardText(const QString &text, QString *errorMessage)
{
    return m_clipboardHistory && m_clipboardHistory->writeText(text, errorMessage);
}

void EditorController::setClipboardHistoryFilter(const QString &query)
{
    if (m_clipboardHistory) {
        m_clipboardHistory->setFilter(query);
    }
}

void EditorController::selectClipboardHistoryItem(const QString &id)
{
    if (m_clipboardHistory) {
        m_clipboardHistory->setSelectedId(id);
        emit clipboardHistoryUiStateChanged();
    }
}

bool EditorController::beginClipboardHistoryDrag(const QString &id,
                                                 qreal sceneX, qreal sceneY)
{
    if (!m_clipboardHistory || !m_commands) {
        return false;
    }
    const QString text = m_clipboardHistory->textById(id);
    return !text.isEmpty()
        && m_commands->beginExternalTextDrag(text, QPointF(sceneX, sceneY));
}

QVariantMap EditorController::updateClipboardHistoryDrag(qreal sceneX, qreal sceneY)
{
    const bool canDrop = m_commands
        && m_commands->updateExternalTextDrag(QPointF(sceneX, sceneY));
    return {{QStringLiteral("active"),
             m_commands && m_commands->externalTextDragActive()},
            {QStringLiteral("canDrop"), canDrop}};
}

bool EditorController::finishClipboardHistoryDrag(qreal sceneX, qreal sceneY)
{
    return m_commands
        && m_commands->finishExternalTextDrag(QPointF(sceneX, sceneY));
}

void EditorController::cancelClipboardHistoryDrag()
{
    if (m_commands) {
        m_commands->cancelExternalTextDrag();
    }
}

void EditorController::requestLoadClipboardHistory(const QString &id)
{
    if (!m_clipboardHistory || !m_editor) {
        return;
    }
    const QString target = m_clipboardHistory->textById(id);
    if (target.isNull()) {
        return;
    }
    const QString current = m_editor->property("text").toString();
    if (current != m_editorBaselineText && current != target) {
        m_pendingHistoryId = id;
        m_historyLoadConfirmationVisible = true;
        emit clipboardHistoryUiStateChanged();
        return;
    }
    m_pendingHistoryId = id;
    confirmLoadClipboardHistory();
}

void EditorController::confirmLoadClipboardHistory()
{
    if (!m_clipboardHistory || !m_editor || m_pendingHistoryId.isEmpty()) {
        return;
    }
    const QString text = m_clipboardHistory->textById(m_pendingHistoryId);
    if (text.isNull()) {
        cancelLoadClipboardHistory();
        return;
    }
    if (m_commands) {
        m_commands->resetHeadingFolds();
    }
    m_editor->setProperty("text", text);
    m_editor->setProperty("cursorPosition", text.size());
    if (auto *quickDocument = qvariant_cast<QQuickTextDocument *>(
            m_editor->property("textDocument"))) {
        quickDocument->textDocument()->clearUndoRedoStacks();
    }
    m_editorBaselineText = text;
    m_pendingHistoryId.clear();
    m_historyLoadConfirmationVisible = false;
    emit clipboardHistoryUiStateChanged();
    emit clipboardHistoryLoaded();
}

void EditorController::cancelLoadClipboardHistory()
{
    if (!m_historyLoadConfirmationVisible && m_pendingHistoryId.isEmpty()) {
        return;
    }
    m_pendingHistoryId.clear();
    m_historyLoadConfirmationVisible = false;
    emit clipboardHistoryUiStateChanged();
}

void EditorController::deleteClipboardHistoryItem(const QString &id)
{
    if (m_clipboardHistory) {
        m_clipboardHistory->deleteById(id);
    }
}

void EditorController::requestClearClipboardHistory()
{
    if (!m_clipboardHistory) {
        return;
    }
    if (!m_clipboardHistory->canClear()) {
        return;
    }
    m_historyClearConfirmationVisible = true;
    emit clipboardHistoryUiStateChanged();
}

void EditorController::confirmClearClipboardHistory()
{
    if (!m_clipboardHistory || !m_historyClearConfirmationVisible) {
        return;
    }
    m_historyClearConfirmationVisible = false;
    m_clipboardHistory->clearHistory();
    emit clipboardHistoryUiStateChanged();
}

void EditorController::cancelClearClipboardHistory()
{
    if (!m_historyClearConfirmationVisible) {
        return;
    }
    m_historyClearConfirmationVisible = false;
    emit clipboardHistoryUiStateChanged();
}

void EditorController::setClipboardState(bool healthy, const QString &message)
{
    const QString newMessage = healthy
        ? QStringLiteral("Esc 关闭并复制 · Ctrl+S 关闭并输入 · Ctrl+W 关闭不保存")
        : message;
    const bool healthChanged = m_clipboardHealthy != healthy;
    const bool statusHealthChanged = m_statusHealthy != healthy;
    const bool messageChanged = m_statusMessage != newMessage;
    m_clipboardHealthy = healthy;
    m_statusHealthy = healthy;
    m_statusMessage = newMessage;
    if (healthChanged) {
        emit clipboardStateChanged();
    }
    if (messageChanged || statusHealthChanged) {
        emit statusMessageChanged();
    }
}

void EditorController::setExternalFileState(bool healthy, const QString &message)
{
    const bool changed = m_statusHealthy != healthy || m_statusMessage != message;
    m_statusHealthy = healthy;
    m_statusMessage = message;
    if (changed) {
        emit statusMessageChanged();
    }
}

void EditorController::updateStatusPanelSummary()
{
    if (!m_editor) {
        return;
    }
    int total = 0;
    QTextDocument *document = nullptr;
    if (auto *quickDocument = qvariant_cast<QQuickTextDocument *>(
            m_editor->property("textDocument"))) {
        document = quickDocument->textDocument();
        total = qMax(0, document->characterCount() - 1);
    }
    const int selectionStart = m_editor->property("selectionStart").toInt();
    const int selectionEnd = m_editor->property("selectionEnd").toInt();
    int totalHan = 0;
    int selectionHan = 0;
    if (document) {
        const QString text = document->toPlainText();
        totalHan = CjkText::countHanCharacters(text);
        if (selectionStart >= 0 && selectionEnd > selectionStart) {
            selectionHan =
                CjkText::countHanCharacters(text, selectionStart, selectionEnd);
        }
    }
    const QString summary =
        selectionStart >= 0 && selectionEnd > selectionStart
            ? QStringLiteral("%1 / %2 字 · %3 / %4 汉字")
                  .arg(selectionEnd - selectionStart).arg(total)
                  .arg(selectionHan).arg(totalHan)
            : QStringLiteral("共 %1 字 · %2 汉字").arg(total).arg(totalHan);
    if (m_statusPanelSummary != summary) {
        m_statusPanelSummary = summary;
        emit statusPanelSummaryChanged();
    }
}

QRect EditorController::validatedWindowGeometry(const QRect &requested) const
{
    const QSize minimum = windowMinimumSize();
    QRect candidate = requested;
    candidate.setWidth(qMax(minimum.width(), candidate.width()));
    candidate.setHeight(qMax(minimum.height(), candidate.height()));

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
    candidate.setWidth(qMax(minimum.width(), qMin(candidate.width(), available.width())));
    candidate.setHeight(qMax(minimum.height(), qMin(candidate.height(), available.height())));
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
    if (externalFileMode()) {
        // 外部模式只记忆尺寸，位置在每次 showEditor() 时按唤起窗口重新计算。
        return false;
    }
    const QRect stored = m_settings->windowGeometry();
    if (!stored.isValid()) {
        return false;
    }
    const auto fitted = WindowPlacement::fitRestoredGeometry(
        stored, windowMinimumSize(), availableScreenGeometries());
    if (!fitted) {
        return false;
    }
    m_window->setGeometry(*fitted);
    m_windowRestingGeometry = *fitted;
    return true;
}

void EditorController::updateWindowAnchor()
{
    m_windowScreenName.clear();
    m_windowScreenOffset = QPoint();
    if (!m_windowRestingGeometry.isValid()) {
        return;
    }
    QScreen *screen = QGuiApplication::screenAt(m_windowRestingGeometry.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }
    m_windowScreenName = screen->name();
    m_windowScreenOffset =
        m_windowRestingGeometry.topLeft() - screen->geometry().topLeft();
}

void EditorController::saveWindowGeometry()
{
    if (m_discardClose || !m_window || !m_settings || !m_positioned) {
        return;
    }
    const bool transitionRunning = m_windowTransitionGroup
        && m_windowTransitionGroup->state() == QAbstractAnimation::Running;
    const bool useRestingGeometry = m_windowRestingGeometry.isValid()
        && (transitionRunning || m_hiding || !m_window->isVisible());
    const QRect geometry = useRestingGeometry ? m_windowRestingGeometry
                                              : m_window->geometry();
    if (externalFileMode()) {
        // 外部提示词编辑器只保存大小；父 CLI 窗口销毁后位置记忆没有意义。
        m_settings->setExternalWindowGeometry(QRect(QPoint(0, 0), geometry.size()));
    } else {
        m_settings->setWindowGeometry(geometry);
    }
}

QSize EditorController::windowDefaultSize() const
{
    // 与 config/ui.json 的 window 令牌保持一致，QML 侧同源读取。
    return m_uiConfig
        ? QSize(m_uiConfig->windowDefaultWidth(), m_uiConfig->windowDefaultHeight())
        : QSize(920, 640);
}

QSize EditorController::windowMinimumSize() const
{
    if (m_uiConfig) {
        return QSize(m_uiConfig->windowMinimumWidth(),
                     m_uiConfig->windowMinimumHeight());
    }
    return m_window
        ? QSize(m_window->minimumWidth(), m_window->minimumHeight())
        : QSize(500, 320);
}

QVector<QRect> EditorController::availableScreenGeometries() const
{
    QVector<QRect> screens;
    const QList<QScreen *> allScreens = QGuiApplication::screens();
    screens.reserve(allScreens.size());
    QScreen *primary = QGuiApplication::primaryScreen();
    if (primary) {
        screens.append(primary->availableGeometry());
    }
    for (QScreen *screen : allScreens) {
        if (screen && screen != primary) {
            screens.append(screen->availableGeometry());
        }
    }
    return screens;
}

void EditorController::watchScreen(QScreen *screen)
{
    if (!screen) {
        return;
    }
    connect(screen, &QScreen::geometryChanged, this, [this](const QRect &) {
        scheduleScreenConfigurationUpdate();
    });
    connect(screen, &QScreen::availableGeometryChanged, this, [this](const QRect &) {
        scheduleScreenConfigurationUpdate();
    });
    connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this](qreal) {
        scheduleScreenConfigurationUpdate();
    });
}

void EditorController::scheduleScreenConfigurationUpdate()
{
    // 屏幕移除时 Qt 会自行迁移窗口，Windows 也会紧接着发送 DPI/工作区
    // 变化。合并这些信号，等布局稳定后再做一次最终校正，避免与 Qt 的
    // 原生迁移争用旧坐标/旧 DPR。
    m_screenConfigurationTimer.start();
}

void EditorController::handleScreenConfigurationChanged()
{
    if (!m_window || !m_positioned) {
        return;
    }
    const QVector<QRect> screens = availableScreenGeometries();
    if (screens.isEmpty()) {
        return;
    }

    // 屏幕增删期间，Qt/Windows 的坐标空间可能短暂重叠（例如副屏仍报告原点 0、
    // 主屏已重新加入），此时计算原生坐标会落到错误的屏幕上。等布局不再重叠
    // 再校正；设置重试上限避免克隆/镜像等持续重叠场景下无限循环。
    bool overlapping = false;
    const QList<QScreen *> allScreens = QGuiApplication::screens();
    for (int i = 0; i < allScreens.size() && !overlapping; ++i) {
        for (int j = i + 1; j < allScreens.size(); ++j) {
            if (allScreens[i]->geometry().intersects(allScreens[j]->geometry())) {
                overlapping = true;
                break;
            }
        }
    }
    if (overlapping && m_screenOverlapRetries < 20) {
        ++m_screenOverlapRetries;
        m_screenConfigurationTimer.start(100);
        return;
    }
    m_screenOverlapRetries = 0;

    // 显示器热插拔后，把当前（或 resting）几何重新校正到仍然存在的屏幕上。
    // 主屏被拔除时 Qt 不会像非主屏那样主动迁移窗口，且 QWindow 的屏幕/DPR
    // 缓存可能已经失效（screen() 为 null 时 geometry() 会按错误的缩放系数
    // 换算），因此优先用原生 GetWindowRect 反推真实逻辑位置，避免把坏几何
    // 写进记忆或作为下次摆放依据。
    const bool visible = m_window->isVisible() && !m_hiding;
    const bool transitionRunning = m_windowTransitionGroup
        && m_windowTransitionGroup->state() == QAbstractAnimation::Running;
    QRect current;
    if (transitionRunning && m_windowRestingGeometry.isValid()) {
        current = m_windowRestingGeometry;
    } else if (visible) {
#ifdef Q_OS_WIN
        current = nativeWindowLogicalRect(reinterpret_cast<HWND>(m_window->winId()));
#endif
        if (!current.isValid()) {
            current = m_window->geometry();
        }
    } else {
        current = m_windowRestingGeometry.isValid() ? m_windowRestingGeometry
                                                    : m_window->geometry();
    }
    // 尺寸优先使用记忆中的逻辑尺寸：跨 DPR 迁移时原生尺寸可能没有同步缩放，
    // 直接用当前 geometry 会把“被压缩”的错误尺寸当成合法尺寸。
    const QSize preferredSize = m_windowRestingGeometry.isValid()
        ? m_windowRestingGeometry.size()
        : (current.isValid() ? current.size() : windowDefaultSize());
    const QSize minimumSize = windowMinimumSize();

    QScreen *anchorScreen = nullptr;
    if (!m_windowScreenName.isEmpty()) {
        for (QScreen *candidate : QGuiApplication::screens()) {
            if (candidate->name() == m_windowScreenName) {
                anchorScreen = candidate;
                break;
            }
        }
    }
    QScreen *positionScreen = current.isValid()
        ? QGuiApplication::screenAt(current.center())
        : nullptr;
    // 单→多切换时，Qt 可能在坐标空间尚未重排前把窗口错误关联到新加回的屏幕
    // （例如窗口物理位置短暂落在主屏矩形内），导致窗口不回到原副屏。此时以
    // 锚定屏幕为准，而不是以瞬时原生位置为准。
    QScreen *targetScreen = positionScreen ? positionScreen
                                           : (anchorScreen ? anchorScreen
                                                           : QGuiApplication::primaryScreen());
    if (m_screenAdditionPending && anchorScreen && anchorScreen != positionScreen) {
        targetScreen = anchorScreen;
    }
    if (!targetScreen) {
        return;
    }

    QRect candidate(QPoint(), preferredSize);
    if (targetScreen == anchorScreen) {
        candidate.moveTopLeft(targetScreen->geometry().topLeft() + m_windowScreenOffset);
    } else if (current.isValid()) {
        candidate.moveTopLeft(current.topLeft());
    } else {
        candidate.moveCenter(targetScreen->geometry().center());
    }
    const auto fitted =
        WindowPlacement::fitRestoredGeometry(candidate, minimumSize, screens);
    const QRect target = fitted
        ? *fitted
        : WindowPlacement::placeNearWindow(
              preferredSize, windowDefaultSize(), minimumSize, screens,
              std::nullopt, QRect());

    m_windowRestingGeometry = target;
    if (anchorScreen) {
        updateWindowAnchor();
    }
    m_screenAdditionPending = false;
    if (!visible) {
        return;
    }

    // 仅 update() 不会重建 RHI 交换链；若 QWindow 的 DPR/屏幕缓存失效，渲染缓冲
    // 尺寸会与窗口原生尺寸错位（多→单时缓冲大于窗口、单→多时缓冲小于窗口，
    // 边框出现在错误位置）。用原生坐标把窗口真实移动到目标屏幕并触发一次真实
    // resize：Windows 会在跨 DPR 移动时发送 WM_DPICHANGED，Qt 借此重新关联屏幕
    // 并刷新 DPR 缓存，等价于用户拖动窗口后的恢复路径。
    auto reconcile = [this, target](bool force) {
        if (!m_window || !m_window->isVisible() || m_hiding) {
            return;
        }
        if (m_windowTransitionGroup
            && m_windowTransitionGroup->state() == QAbstractAnimation::Running) {
            m_window->update();
            return;
        }
        QScreen *screen = QGuiApplication::screenAt(target.center());
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        if (!screen) {
            m_window->update();
            return;
        }
        if (m_window->screen() && m_window->screen() != screen) {
            m_window->setScreen(screen);
        }
        const qreal expectedDpr = screen->devicePixelRatio();
        const bool dprMismatch =
            !qFuzzyCompare(m_window->devicePixelRatio(), expectedDpr);
        const bool screenMismatch = m_window->screen() != screen;
        const bool geometryMismatch = m_window->geometry() != target;
        if (force || geometryMismatch || dprMismatch || screenMismatch) {
#ifdef Q_OS_WIN
            const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
            const QRect nativeTarget = logicalToNativeRect(target, screen);
            if (hwnd && IsWindow(hwnd) && nativeTarget.isValid()) {
                const QRect nudge = nativeTarget.adjusted(0, 0, 1, 1);
                SetWindowPos(hwnd, nullptr, nudge.x(), nudge.y(), nudge.width(),
                             nudge.height(), SWP_NOZORDER | SWP_NOACTIVATE);
                SetWindowPos(hwnd, nullptr, nativeTarget.x(), nativeTarget.y(),
                             nativeTarget.width(), nativeTarget.height(),
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
#else
            const QRect nudge = target.adjusted(0, 0, 1, 1);
            m_window->setGeometry(nudge);
            m_window->setGeometry(target);
#endif
        }
        m_window->update();
    };
    reconcile(false);

    // Windows/Qt 的 DPI 迁移事件可能在原生移动之后仍然覆盖窗口位置，因此分
    // 三批（0ms/600ms/1400ms）重复断言目标位置与屏幕关联，直到系统稳定；每批
    // 都会重新执行原生坐标校正，并兜底重设屏幕（平台窗口若被重建而隐藏则恢复
    // 显示并重刷原生样式）。
    auto finalize = [this, target, reconcile] {
        if (!m_window || !m_window->isVisible() || m_hiding) {
            return;
        }
        reconcile(true);
        if (!m_window || !m_window->isVisible() || m_hiding) {
            return;
        }
        QScreen *screen = QGuiApplication::screenAt(target.center());
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        if (!screen) {
            return;
        }
        const qreal expectedDpr = screen->devicePixelRatio();
        if (m_window->screen() != screen
            && !qFuzzyCompare(m_window->devicePixelRatio(), expectedDpr)) {
            const bool wasVisible = m_window->isVisible();
            m_window->setScreen(screen);
            if (wasVisible && !m_window->isVisible()) {
                m_window->show();
                m_window->raise();
                m_window->requestActivate();
            }
            applyNativeWindowStyle();
            m_window->update();
        }
    };
    QTimer::singleShot(0, this, [finalize] { finalize(); });
    QTimer::singleShot(600, this, [finalize] { finalize(); });
    QTimer::singleShot(1400, this, [finalize] { finalize(); });
}

void EditorController::restorePreviousFocus()
{
    // 普通剪贴板模式是独立临时窗口：关闭后由系统把前台交给最近活跃的窗口，
    // 不把焦点强拉回打开编辑器之前的窗口。仅外部 CLI（提示词临时编辑器）模式
    // 保留该行为，把焦点还给启动它的终端窗口。
    if (!externalFileMode()) {
        m_previousForegroundWindow = 0;
        return;
    }
#ifdef Q_OS_WIN
    const HWND previous = reinterpret_cast<HWND>(m_previousForegroundWindow);
    m_previousForegroundWindow = 0;
    if (previous && IsWindow(previous)) {
        SetForegroundWindow(previous);
    }
#endif
}

bool EditorController::handleClipboardHistoryWindowLeave(const QPointF &localPosition,
                                                         bool mouseButtonPressed)
{
    if (!clipboardHistoryAvailable() || !m_window || !m_window->isVisible()
        || m_hiding || mouseButtonPressed) {
        return false;
    }
    const qreal top = m_window->property("dragZoneHeight").toDouble();
    const qreal bottomMargin = m_window->property("marginSize").toDouble();
    if (localPosition.x() > 0.0 || localPosition.y() < top
        || localPosition.y() >= m_window->height() - bottomMargin) {
        return false;
    }
    emit clipboardHistoryLeftEdgeExited();
    return true;
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
    status.insert(QStringLiteral("executableFile"), QCoreApplication::applicationFilePath());
    status.insert(QStringLiteral("startupMs"), m_readyStartupMs);
    status.insert(QStringLiteral("firstFrameColor"),
                  m_testSupport ? m_testSupport->firstFrameColor() : QString());
    status.insert(QStringLiteral("serverName"), serverName());
    status.insert(QStringLiteral("renderLoop"),
                  QString::fromLocal8Bit(qgetenv("QSG_RENDER_LOOP")));
    status.insert(QStringLiteral("resizePresentationUnthrottled"),
                  qEnvironmentVariableIntValue("QSG_NO_VSYNC") != 0);
    status.insert(QStringLiteral("simpleAnimationDriver"),
                  qEnvironmentVariableIntValue("QSG_USE_SIMPLE_ANIMATION_DRIVER") != 0);
    status.insert(QStringLiteral("statusMessage"), m_statusMessage);
    status.insert(QStringLiteral("statusPanelFontSize"), statusPanelFontSize());
    status.insert(QStringLiteral("statusPanelShowDelayMs"), statusPanelShowDelayMs());
    status.insert(QStringLiteral("statusPanelHideDelayMs"), statusPanelHideDelayMs());
    status.insert(QStringLiteral("statusPanelMaxWidth"), statusPanelMaxWidth());
    status.insert(QStringLiteral("statusPanelHints"),
                  QJsonArray::fromStringList(statusPanelHints()));
    status.insert(QStringLiteral("statusPanelSummary"), statusPanelSummary());
    status.insert(QStringLiteral("clipboardHealthy"), m_clipboardHealthy);
    status.insert(QStringLiteral("historyCardHeight"), historyCardHeight());
    status.insert(QStringLiteral("settingsFile"), m_settings ? m_settings->fileName() : QString());
    status.insert(QStringLiteral("settingsStatus"),
                  m_settings ? m_settings->status() : -1);
    status.insert(QStringLiteral("settingsSchemaVersion"),
                  m_settings ? m_settings->schemaVersion() : 0);
    status.insert(QStringLiteral("theme"), m_theme);
    status.insert(QStringLiteral("editorFontFamily"), m_editorFontFamily);
    status.insert(QStringLiteral("editorFallbackFontFamily"),
                  m_editorFallbackFontFamily);
    status.insert(QStringLiteral("editorFontFamilies"),
                  QJsonArray::fromStringList(editorFont().families()));
    status.insert(QStringLiteral("editorFontPointSize"), m_editorFontPointSize);
    status.insert(QStringLiteral("editorFontWeight"), editorFont().weight());
    status.insert(QStringLiteral("animationsEnabled"), m_animationsEnabled);
    status.insert(QStringLiteral("settingsError"), m_settingsError);
    status.insert(QStringLiteral("markdownHighlighting"), markdownHighlighting());
    status.insert(QStringLiteral("markdownTextColor"), markdownTextColor());
    status.insert(QStringLiteral("themeAccentColor"), themeAccentColor());
    status.insert(QStringLiteral("themeAccentTextColor"), themeAccentTextColor());
    status.insert(QStringLiteral("markdownStyleFile"), markdownStyleFile());
    status.insert(QStringLiteral("markdownStyleLoaded"), markdownStyleLoaded());
    status.insert(QStringLiteral("uiConfigFile"),
                  m_uiConfig ? m_uiConfig->filePath() : QString());
    status.insert(QStringLiteral("uiConfigLoaded"),
                  m_uiConfig && m_uiConfig->loadedFromFile());
    status.insert(QStringLiteral("commandCount"), commands().size());
    status.insert(QStringLiteral("headingNavigationHighlight"),
                  QJsonObject::fromVariantMap(headingNavigationHighlight()));
    status.insert(QStringLiteral("historyAvailable"), clipboardHistoryAvailable());
    status.insert(QStringLiteral("historyHealthy"), clipboardHistoryHealthy());
    status.insert(QStringLiteral("historyError"), clipboardHistoryError());
    status.insert(QStringLiteral("historyCount"), m_clipboardHistory
                      ? m_clipboardHistory->items().size() : 0);
    status.insert(QStringLiteral("historyStoreFile"),
                  m_testMode && m_clipboardHistory
                      ? m_clipboardHistory->storeFilePath() : QString());
    status.insert(QStringLiteral("historyStoreState"), m_clipboardHistory
                      ? m_clipboardHistory->storeStateName() : QStringLiteral("Unavailable"));
    status.insert(QStringLiteral("historyLoadConfirmationVisible"),
                  m_historyLoadConfirmationVisible);
    status.insert(QStringLiteral("historyClearConfirmationVisible"),
                  m_historyClearConfirmationVisible);
    status.insert(QStringLiteral("historySelectedId"), m_clipboardHistory
                      ? m_clipboardHistory->selectedId() : QString());
    status.insert(QStringLiteral("historyHoveredId"),
                  m_window ? m_window->property("historyHoveredId").toString() : QString());
    status.insert(QStringLiteral("clipboardBackend"), m_clipboardHistory
                      ? m_clipboardHistory->backendName() : QStringLiteral("unavailable"));
    status.insert(QStringLiteral("nativeClipboardAccessAttempts"),
                  static_cast<qint64>(m_clipboardHistory
                      ? m_clipboardHistory->nativeAccessAttempts() : 0));

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
        if (m_commands) {
            const QVariantMap scrollDiagnostics = m_commands->inputScrollDiagnostics();
            for (auto it = scrollDiagnostics.constBegin();
                 it != scrollDiagnostics.constEnd(); ++it) {
                status.insert(QStringLiteral("inputScroll") + it.key(),
                              QJsonValue::fromVariant(it.value()));
            }
        }
        if (QQuickItem *viewport = m_commands ? m_commands->editorViewport() : nullptr) {
            status.insert(QStringLiteral("scrollContentY"),
                          viewport->property("contentY").toDouble());
            status.insert(QStringLiteral("scrollContentHeight"),
                          viewport->property("contentHeight").toDouble());
            status.insert(QStringLiteral("scrollViewportHeight"), viewport->height());
            status.insert(QStringLiteral("scrollViewportY"), viewport->y());
        }
        status.insert(QStringLiteral("commandPaletteLoaded"),
                      m_window->property("commandPaletteLoaded").toBool());
        status.insert(QStringLiteral("findPanelVisible"),
                      m_window->property("findPanelVisible").toBool());
        status.insert(QStringLiteral("findPanelBottom"),
                      m_window->property("findPanelBottom").toDouble());
        status.insert(QStringLiteral("settingsPageLoaded"),
                      m_window->property("settingsPageLoaded").toBool());
        status.insert(QStringLiteral("settingsPageVisible"),
                      m_window->property("settingsPageVisible").toBool());
        status.insert(QStringLiteral("fileDropEnabled"),
                      m_window->property("fileDropEnabled").toBool());
        status.insert(QStringLiteral("themeBackgroundColor"),
                      m_window->property("themeBackgroundColor").toString());
        status.insert(QStringLiteral("themeEditorSurfaceColor"),
                      m_window->property("themeEditorSurfaceColor").toString());
        status.insert(QStringLiteral("themeHeaderColor"),
                      m_window->property("themeHeaderColor").toString());
        status.insert(QStringLiteral("themeBorderColor"),
                      m_window->property("themeBorderColor").toString());
        status.insert(QStringLiteral("panelAccentColor"),
                      m_window->property("panelAccentColor").toString());
        status.insert(QStringLiteral("themeSelectionColor"),
                      m_window->property("themeSelectionColor").toString());
        status.insert(QStringLiteral("selectionDragColor"),
                      m_window->property("selectionDragColor").toString());
        status.insert(QStringLiteral("commandPaletteMaximumWidth"),
                      m_window->property("commandPaletteMaximumWidth").toInt());
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
        status.insert(QStringLiteral("historyPanelOpen"),
                      m_window->property("historyPanelOpen").toBool());
        status.insert(QStringLiteral("historyPanelOverlay"),
                      m_window->property("historyPanelOverlay").toBool());
        status.insert(QStringLiteral("historyPanelWidth"),
                      m_window->property("historyPanelWidth").toDouble());
        status.insert(QStringLiteral("historyPanelEdgeIntrusion"),
                      m_window->property("historyPanelEdgeIntrusion").toDouble());
        status.insert(QStringLiteral("editorVisibleWidth"),
                      m_window->property("editorVisibleWidth").toDouble());
        status.insert(QStringLiteral("editorViewportWidth"),
                      m_window->property("editorViewportWidth").toDouble());
        status.insert(QStringLiteral("headingFoldGutterWidth"),
                      m_window->property("headingFoldGutterWidth").toInt());
        status.insert(QStringLiteral("headingFoldIconSize"),
                      m_window->property("headingFoldIconSize").toInt());
        status.insert(QStringLiteral("headingFoldExpandedIconName"),
                      m_window->property("headingFoldExpandedIconName").toString());
        status.insert(QStringLiteral("headingFoldCollapsedIconName"),
                      m_window->property("headingFoldCollapsedIconName").toString());
        status.insert(QStringLiteral("headingFoldExpandedColor"),
                      m_window->property("headingFoldExpandedColor").toString());
        status.insert(QStringLiteral("headingFoldCollapsedColor"),
                      m_window->property("headingFoldCollapsedColor").toString());
        status.insert(QStringLiteral("headingFoldMarkerCount"),
                      m_window->property("headingFoldMarkerCount").toInt());
        status.insert(QStringLiteral("headingNavigationHighlightOpacity"),
                      m_window->property("headingNavigationHighlightOpacity").toDouble());
        status.insert(QStringLiteral("headingNavigationHighlightHoldDurationMs"),
                      m_window->property(
                          "headingNavigationHighlightHoldDurationMs").toInt());
        status.insert(QStringLiteral("headingNavigationHighlightFadeDurationMs"),
                      m_window->property(
                          "headingNavigationHighlightFadeDurationMs").toInt());
        status.insert(QStringLiteral("headingNavigationHighlightVisible"),
                      m_window->property("headingNavigationHighlightVisible").toBool());
        status.insert(QStringLiteral("headingNavigationHighlightEffectiveOpacity"),
                      m_window->property(
                          "headingNavigationHighlightEffectiveOpacity").toDouble());
        status.insert(QStringLiteral("headingNavigationHighlightRectCount"),
                      m_window->property("headingNavigationHighlightRectCount").toInt());
        status.insert(QStringLiteral("headingNavigationHighlightDrawnBeforeText"),
                      m_window->property(
                          "headingNavigationHighlightDrawnBeforeText").toBool());
        status.insert(QStringLiteral("headingNavigationHighlightMaxWidth"),
                      m_window->property("headingNavigationHighlightMaxWidth").toDouble());
        status.insert(QStringLiteral("editorVisibleContentHeight"),
                      m_window->property("editorVisibleContentHeight").toDouble());
        status.insert(QStringLiteral("historyQueryFocused"),
                      m_window->property("historyQueryFocused").toBool());
        status.insert(QStringLiteral("historyCardDragActive"),
                      m_window->property("historyCardDragActive").toBool());
        status.insert(QStringLiteral("historyCardDropAllowed"),
                      m_window->property("historyCardDropAllowed").toBool());
        status.insert(QStringLiteral("historyPanelLoaded"),
                      m_window->property("historyPanelLoaded").toBool());
        status.insert(QStringLiteral("historyTriggerWidth"),
                      m_window->property("historyTriggerWidth").toInt());
        status.insert(QStringLiteral("historyRevealZoneX"),
                      m_window->property("historyRevealZoneX").toInt());
        status.insert(QStringLiteral("historyRevealZoneWidth"),
                      m_window->property("historyRevealZoneWidth").toInt());
        status.insert(QStringLiteral("historyPanelClipped"),
                      m_window->property("historyPanelClipped").toBool());
        status.insert(QStringLiteral("historyRevealBlocksPointer"),
                      m_window->property("historyRevealBlocksPointer").toBool());
        status.insert(QStringLiteral("historyHoverOpenDelayMs"),
                      m_window->property("historyHoverOpenDelayMs").toInt());
        status.insert(QStringLiteral("historyHoverCloseDelayMs"),
                      m_window->property("historyHoverCloseDelayMs").toInt());
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
        status.insert(QStringLiteral("editorContentOffsetY"), m_editor->property("y").toDouble());
        status.insert(QStringLiteral("editorContentHeight"),
                      m_editor->property("contentHeight").toDouble());
        status.insert(QStringLiteral("cursorRectY"),
                      m_editor->property("cursorRectangle").toRectF().y());
        status.insert(QStringLiteral("cursorRectHeight"),
                      m_editor->property("cursorRectangle").toRectF().height());
    }
    if (m_testSupport) {
        m_testSupport->appendDiagnostics(status);
    }
    return status;
}

void EditorController::animationBenchmarkFinished()
{
    if (m_testSupport) {
        m_testSupport->animationBenchmarkFinished();
    }
}

bool EditorController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && event->type() == QEvent::Resize
        && m_window && m_window->isVisible()
        && m_windowRestingGeometry.isValid() && !m_hiding
        && !(m_windowTransitionGroup
             && m_windowTransitionGroup->state() == QAbstractAnimation::Running)
        && !m_screenConfigurationTimer.isActive()
        && !m_nativeDisplayChangeActive) {
        // 实时跟踪窗口尺寸，热插拔校正时按记忆尺寸恢复，避免丢失用户刚调整的大小。
        m_windowRestingGeometry.setSize(m_window->size());
    }
    if (watched == m_window && event->type() == QEvent::Move
        && m_window && m_window->isVisible() && m_window->screen()
        && m_windowRestingGeometry.isValid() && !m_hiding
        && !(m_windowTransitionGroup
             && m_windowTransitionGroup->state() == QAbstractAnimation::Running)
        && !m_screenConfigurationTimer.isActive()
        && !m_nativeDisplayChangeActive) {
        // 实时跟踪窗口所在屏幕与屏内偏移，拖动跨屏后锚点仍指向实际位置，
        // 拔屏恢复时才能回到原屏幕。
        m_windowRestingGeometry.moveTopLeft(m_window->geometry().topLeft());
        const QString screenName = m_window->screen()->name();
        const QPoint screenOffset =
            m_window->geometry().topLeft() - m_window->screen()->geometry().topLeft();
        if (m_windowScreenName != screenName || m_windowScreenOffset != screenOffset) {
            m_windowScreenName = screenName;
            m_windowScreenOffset = screenOffset;
        }
    }

    if (watched == m_window && event->type() == QEvent::Leave && m_window) {
        const QPointF localPosition = m_window->mapFromGlobal(QPointF(QCursor::pos()));
        handleClipboardHistoryWindowLeave(
            localPosition, QGuiApplication::mouseButtons() != Qt::NoButton);
    }

    // 多重点击跟踪：Windows 上第二次点击以 MouseButtonDblClick 到达，第三次仍是
    // MouseButtonPress。识别多重点击后放行给 QML TextEdit 原生处理，避免选区拖拽
    // 吞掉三击导致“三击选中整行”失效。
    if (watched == m_window && m_editor
        && (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonDblClick)) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton) {
            m_lastMouseClickElapsedMs = -1;
        } else {
            const qint64 now = m_mouseClickTimer.isValid()
                ? m_mouseClickTimer.elapsed() : 0;
            if (!m_mouseClickTimer.isValid()) {
                m_mouseClickTimer.start();
            }
            const bool withinDoubleClick = m_lastMouseClickElapsedMs >= 0
                && (now - m_lastMouseClickElapsedMs)
                    <= QGuiApplication::styleHints()->mouseDoubleClickInterval()
                && (mouseEvent->position() - m_lastMouseClickScenePosition).manhattanLength()
                    <= QGuiApplication::styleHints()->startDragDistance();
            m_lastMouseClickElapsedMs = m_mouseClickTimer.elapsed();
            m_lastMouseClickScenePosition = mouseEvent->position();
            if (event->type() == QEvent::MouseButtonPress) {
                m_multiClickPress = withinDoubleClick;
            }
        }
    }

    if (watched == m_window && m_commands && m_editor
        && (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseMove
            || event->type() == QEvent::MouseButtonRelease)) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (auto *item = qobject_cast<QQuickItem *>(m_editor.data())) {
            const QPointF scenePosition = mouseEvent->position();
            const QPointF editorPosition = item->mapFromScene(scenePosition);
            bool insideViewport = item->contains(editorPosition);
            if (insideViewport) {
                if (QQuickItem *viewport = m_commands->editorViewport()) {
                    insideViewport = viewport->contains(
                        viewport->mapFromScene(scenePosition));
                }
            }
            if ((event->type() != QEvent::MouseButtonPress || insideViewport)
                && !(event->type() == QEvent::MouseButtonPress && m_multiClickPress)) {
                QMouseEvent editorEvent(
                    event->type(), editorPosition, scenePosition,
                    mouseEvent->globalPosition(), mouseEvent->button(),
                    mouseEvent->buttons(), mouseEvent->modifiers(), mouseEvent->source(),
                    mouseEvent->pointingDevice());
                if (m_commands->handleEditorEvent(&editorEvent)) {
                    if (event->type() == QEvent::MouseButtonRelease) {
                        item->ungrabMouse();
                    }
                    event->accept();
                    return true;
                }
            }
        }
    }
    if (watched == m_window && m_commands && event->type() == QEvent::FocusOut) {
        m_commands->handleEditorEvent(event);
    }
    if (m_testSupport && watched == m_editor && event->type() == QEvent::KeyPress) {
        m_testSupport->observeKeyPress(static_cast<QKeyEvent *>(event));
    }
    // editor item 上也装有事件过滤器，第三次 Press 若在这里仍交给
    // handleEditorEvent，会被选区拖拽吞掉导致三击选整行失效，因此与窗口层
    // 一样对多重点击放行给 QML TextEdit 原生处理。
    if (watched == m_editor && m_commands
        && !(event->type() == QEvent::MouseButtonPress && m_multiClickPress)
        && m_commands->handleEditorEvent(event)) {
        return true;
    }
    return QObject::eventFilter(watched, event);
}
