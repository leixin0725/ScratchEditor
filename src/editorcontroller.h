#pragma once

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonObject>
#include <QLocalServer>
#include <QMutex>
#include <QPointer>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <functional>
#include <memory>

class QLocalSocket;
class QParallelAnimationGroup;
class QQuickWindow;
class QScreen;
class QVariantAnimation;
class QAbstractItemModel;
class AppSettings;
class ClipboardHistoryCoordinator;
class EditorCommandRegistry;
class ExternalFileSession;
class MarkdownHighlighter;
class MarkdownStyle;
class UiConfig;

class EditorController final : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    Q_PROPERTY(bool visible READ isVisible NOTIFY visibleChanged)
    Q_PROPERTY(bool testMode READ testMode CONSTANT)
    Q_PROPERTY(bool externalFileMode READ externalFileMode CONSTANT)
    Q_PROPERTY(QString externalFileName READ externalFileName CONSTANT)
    Q_PROPERTY(QString externalCliType READ externalCliType CONSTANT)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool statusHealthy READ statusHealthy NOTIFY statusMessageChanged)
    Q_PROPERTY(int statusPanelFontSize READ statusPanelFontSize NOTIFY statusPanelSettingsChanged)
    Q_PROPERTY(int statusPanelShowDelayMs READ statusPanelShowDelayMs
                   NOTIFY statusPanelSettingsChanged)
    Q_PROPERTY(int statusPanelHideDelayMs READ statusPanelHideDelayMs
                   NOTIFY statusPanelSettingsChanged)
    Q_PROPERTY(int statusPanelMaxWidth READ statusPanelMaxWidth
                   NOTIFY statusPanelSettingsChanged)
    Q_PROPERTY(QStringList statusPanelHints READ statusPanelHints CONSTANT)
    Q_PROPERTY(QString statusPanelSummary READ statusPanelSummary
                   NOTIFY statusPanelSummaryChanged)
    Q_PROPERTY(bool clipboardHealthy READ clipboardHealthy NOTIFY clipboardStateChanged)
    Q_PROPERTY(QAbstractItemModel* clipboardHistoryModel READ clipboardHistoryModel CONSTANT)
    Q_PROPERTY(bool clipboardHistoryAvailable READ clipboardHistoryAvailable CONSTANT)
    Q_PROPERTY(bool clipboardHistoryHealthy READ clipboardHistoryHealthy NOTIFY clipboardHistoryStateChanged)
    Q_PROPERTY(QString clipboardHistoryError READ clipboardHistoryError NOTIFY clipboardHistoryStateChanged)
    Q_PROPERTY(bool historyLoadConfirmationVisible READ historyLoadConfirmationVisible NOTIFY clipboardHistoryUiStateChanged)
    Q_PROPERTY(bool historyClearConfirmationVisible READ historyClearConfirmationVisible NOTIFY clipboardHistoryUiStateChanged)
    Q_PROPERTY(int historyCardHeight READ historyCardHeight CONSTANT)
    Q_PROPERTY(QVariantList commands READ commands NOTIFY commandsChanged)
    Q_PROPERTY(QVariantList headingFoldMarkers READ headingFoldMarkers
                   NOTIFY headingFoldMarkersChanged)
    Q_PROPERTY(int headingFoldVisibleEndPosition READ headingFoldVisibleEndPosition
                   NOTIFY headingFoldMarkersChanged)
    Q_PROPERTY(QVariantMap headingNavigationHighlight READ headingNavigationHighlight
                   NOTIFY headingNavigationHighlightChanged)
    Q_PROPERTY(bool markdownHighlighting READ markdownHighlighting NOTIFY markdownHighlightingChanged)
    Q_PROPERTY(QString theme READ theme NOTIFY appearanceChanged)
    Q_PROPERTY(QString editorFontFamily READ editorFontFamily NOTIFY appearanceChanged)
    Q_PROPERTY(int editorFontPointSize READ editorFontPointSize NOTIFY appearanceChanged)
    Q_PROPERTY(bool animationsEnabled READ animationsEnabled NOTIFY appearanceChanged)
    Q_PROPERTY(QString settingsFile READ settingsFile CONSTANT)
    Q_PROPERTY(QString settingsError READ settingsError NOTIFY settingsErrorChanged)
    Q_PROPERTY(QString markdownTextColor READ markdownTextColor NOTIFY markdownStyleChanged)
    Q_PROPERTY(QString themeAccentColor READ themeAccentColor NOTIFY markdownStyleChanged)
    Q_PROPERTY(QString themeAccentTextColor READ themeAccentTextColor NOTIFY markdownStyleChanged)
    Q_PROPERTY(QString markdownStyleFile READ markdownStyleFile CONSTANT)
    Q_PROPERTY(bool markdownStyleLoaded READ markdownStyleLoaded NOTIFY markdownStyleChanged)
    Q_PROPERTY(QVariant uiConfig READ uiConfig CONSTANT)

public:
    explicit EditorController(bool testMode, QElapsedTimer *startupTimer,
                              const QString &externalFilePath = {}, QObject *parent = nullptr);
    ~EditorController() override;

    bool nativeEventFilter(const QByteArray &eventType, void *message,
                           qintptr *result) override;

    static QString serverName();
    static bool forwardToExistingInstance(const QString &command, int timeoutMs = 250);

    bool startServer();
    bool isReady() const;
    bool isVisible() const;
    bool testMode() const;
    bool externalFileMode() const;
    bool externalFileReady() const;
    QString externalFileName() const;
    QString externalFileError() const;
    QString externalCliType() const;
    bool completeExternalFileTest(const QString &text);
    QString statusMessage() const;
    bool statusHealthy() const;
    int statusPanelFontSize() const;
    int statusPanelShowDelayMs() const;
    int statusPanelHideDelayMs() const;
    int statusPanelMaxWidth() const;
    QStringList statusPanelHints() const;
    QString statusPanelSummary() const;
    bool clipboardHealthy() const;
    QAbstractItemModel *clipboardHistoryModel() const;
    bool clipboardHistoryAvailable() const;
    bool clipboardHistoryHealthy() const;
    QString clipboardHistoryError() const;
    bool historyLoadConfirmationVisible() const;
    bool historyClearConfirmationVisible() const;
    int historyCardHeight() const;
    QVariantList commands() const;
    QVariantList headingFoldMarkers() const;
    int headingFoldVisibleEndPosition() const;
    QVariantMap headingNavigationHighlight() const;
    bool markdownHighlighting() const;
    QString theme() const;
    QString editorFontFamily() const;
    int editorFontPointSize() const;
    bool animationsEnabled() const;
    QString settingsFile() const;
    QString settingsError() const;
    QString markdownTextColor() const;
    QString themeAccentColor() const;
    QString themeAccentTextColor() const;
    QString markdownStyleFile() const;
    bool markdownStyleLoaded() const;
    QVariant uiConfig() const;

    Q_INVOKABLE void registerWindow(QQuickWindow *window);
    Q_INVOKABLE void registerEditor(QObject *editor);
    Q_INVOKABLE void hideEditor();
    Q_INVOKABLE void deliverAndHide();
    Q_INVOKABLE void discardAndHide();
    Q_INVOKABLE bool saveExternalFile();
    Q_INVOKABLE void animationBenchmarkFinished();
    Q_INVOKABLE bool executeCommand(const QString &commandId);
    Q_INVOKABLE bool toggleHeadingFoldAt(int headingPosition);
    Q_INVOKABLE QString shortcutFor(const QString &commandId) const;
    Q_INVOKABLE bool setShortcut(const QString &commandId, const QString &sequence);
    Q_INVOKABLE void resetShortcuts();
    Q_INVOKABLE bool findNext(const QString &query, bool caseSensitive = false,
                              bool backwards = false);
    Q_INVOKABLE bool replaceCurrent(const QString &query, const QString &replacement,
                                    bool caseSensitive = false);
    Q_INVOKABLE int replaceAll(const QString &query, const QString &replacement,
                               bool caseSensitive = false);
    Q_INVOKABLE bool applyAppearance(const QString &theme, const QString &fontFamily,
                                     int fontPointSize, bool animationsEnabled);
    Q_INVOKABLE void resetAppearance();
    Q_INVOKABLE bool applyStatusPanelSettings(int fontSize, int showDelayMs,
                                              int hideDelayMs, int maxWidth);
    Q_INVOKABLE void resetStatusPanelSettings();
    Q_INVOKABLE bool copyToClipboard(const QString &text);
    Q_INVOKABLE void setClipboardHistoryFilter(const QString &query);
    Q_INVOKABLE void selectClipboardHistoryItem(const QString &id);
    Q_INVOKABLE void requestLoadClipboardHistory(const QString &id);
    Q_INVOKABLE void confirmLoadClipboardHistory();
    Q_INVOKABLE void cancelLoadClipboardHistory();
    Q_INVOKABLE void deleteClipboardHistoryItem(const QString &id);
    Q_INVOKABLE void requestClearClipboardHistory();
    Q_INVOKABLE void confirmClearClipboardHistory();
    Q_INVOKABLE void cancelClearClipboardHistory();

    void showEditor();
    void toggleEditor();
    void shutdown();

signals:
    void visibleChanged();
    void statusMessageChanged();
    void statusPanelSettingsChanged();
    void statusPanelSummaryChanged();
    void clipboardStateChanged();
    void clipboardHistoryStateChanged();
    void clipboardHistoryUiStateChanged();
    void clipboardHistoryLoaded();
    void clipboardHistoryLeftEdgeExited();
    void commandsChanged();
    void headingFoldMarkersChanged();
    void headingNavigationHighlightChanged();
    void markdownHighlightingChanged();
    void markdownStyleChanged();
    void appearanceChanged();
    void settingsErrorChanged();
    void uiCommandRequested(const QString &commandId);

private slots:
    void updateStatusPanelSummary();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct PendingRequest {
        QPointer<QLocalSocket> socket;
        qint64 startedNs = 0;
        QString requestId;
    };

    struct DispatchRequest {
        QLocalSocket *socket = nullptr;
        QJsonObject request;
        QString command;
        QString requestId;
        bool noReply = false;
        qint64 startedNs = 0;
    };

    struct CommandEntry {
        enum class Gate {
            PreReady,
            Ready,
            Test,
        };
        Gate gate = Gate::Test;
        std::function<void(const DispatchRequest &)> handler;
    };

    void acceptConnections();
    void readSocket(QLocalSocket *socket);
    void dispatchCommand(QLocalSocket *socket, const QJsonObject &request, qint64 startedNs);
    void buildCommandHandlers();
    void sendResponse(QLocalSocket *socket, QJsonObject response, qint64 startedNs,
                      const QString &requestId = {});
    void sendError(QLocalSocket *socket, const QString &command, const QString &message,
                   qint64 startedNs, const QString &requestId = {});
    void showForRequest(QLocalSocket *socket, const QString &command, qint64 startedNs,
                        const QString &requestId, const QJsonObject &request);
    void waitForNextFrame(QLocalSocket *socket, QJsonObject response, qint64 startedNs,
                          const QString &requestId);
    void captureEditorRenderSample(QLocalSocket *socket, QJsonObject response,
                                   const QRectF &editorLocalRect, qint64 startedNs,
                                   const QString &requestId);
    bool commitAndHide(bool deliverAfterHide = false, bool persistState = true);
    bool commitExternalFileAndExit();
    void setExternalFileState(bool healthy, const QString &message);
    void startWindowTransition(qreal targetOpacity, const QRect &targetGeometry,
                               bool hideWhenFinished);
    void finishWindowHide();
    void finishHideFocusHandoff();
    void deliverTextToNextWindow();
    QRect scaledWindowGeometry(const QRect &restingGeometry) const;
    void applyNativeWindowStyle();
    void updateReadyState();
    bool readClipboardText(QString *text, QString *errorMessage);
    bool writeClipboardText(const QString &text, QString *errorMessage);
    void setClipboardState(bool healthy, const QString &message = {});
    bool restoreWindowGeometry();
    void saveWindowGeometry();
    void updateWindowAnchor();
    QRect validatedWindowGeometry(const QRect &requested) const;
    QJsonObject queryExistingInstance(const QString &command, int timeoutMs = 300) const;
    QSize windowDefaultSize() const;
    QSize windowMinimumSize() const;
    QVector<QRect> availableScreenGeometries() const;
    void watchScreen(QScreen *screen);
    void scheduleScreenConfigurationUpdate();
    void handleScreenConfigurationChanged();
    void restorePreviousFocus();
    bool handleClipboardHistoryWindowLeave(const QPointF &localPosition,
                                           bool mouseButtonPressed);
    QJsonObject statusObject() const;
    void runLargeDocumentBenchmark(QLocalSocket *socket, qint64 startedNs,
                                   const QString &requestId);
    void runImeBenchmark(QLocalSocket *socket, qint64 startedNs, const QString &requestId);
    void runAnimationBenchmark(QLocalSocket *socket, qint64 startedNs,
                               const QString &requestId);
    void restoreTestDocument();
    void reloadAppearance();
    void configureMarkdownStyleWatcher();
    void reloadMarkdownStyle();

    QLocalServer m_server;
    QHash<QLocalSocket *, QByteArray> m_buffers;
    QHash<QString, CommandEntry> m_commandHandlers;
    QPointer<QQuickWindow> m_window;
    QPointer<QObject> m_editor;
    std::unique_ptr<AppSettings> m_settings;
    std::unique_ptr<EditorCommandRegistry> m_commands;
    std::unique_ptr<ClipboardHistoryCoordinator> m_clipboardHistory;
    std::unique_ptr<ExternalFileSession> m_externalFileSession;
    std::unique_ptr<MarkdownStyle> m_markdownStyle;
    std::unique_ptr<UiConfig> m_uiConfig;
    QFileSystemWatcher m_markdownStyleWatcher;
    QTimer m_markdownStyleReloadTimer;
    QPointer<MarkdownHighlighter> m_markdownHighlighter;
    QElapsedTimer *m_startupTimer = nullptr;
    QElapsedTimer m_monotonic;
    bool m_testMode = false;
    bool m_externalFileReady = false;
    bool m_externalFileLoadedIntoEditor = false;
    bool m_externalFileCompleted = false;
    QString m_externalFileText;
    QString m_externalFileError;
    QString m_externalCliType;
    bool m_ready = false;
    qint64 m_readyStartupMs = -1;
    bool m_positioned = false;
    bool m_firstFrameCaptured = false;
    QString m_firstFrameColor;
    QString m_statusMessage = QStringLiteral("Esc 关闭并复制 · Ctrl+S 关闭并输入 · Ctrl+W 关闭不保存");
    bool m_statusHealthy = true;
    QString m_statusPanelSummary = QStringLiteral("共 0 字");
    bool m_clipboardHealthy = true;
    quintptr m_startupForegroundWindow = 0;
    quintptr m_previousForegroundWindow = 0;
    bool m_deliverAfterHide = false;
    quint64 m_focusGeneration = 0;
    QString m_savedTestText;
    bool m_hasSavedTestText = false;
    QString m_theme = QStringLiteral("dark");
    QString m_editorFontFamily = QStringLiteral("Microsoft YaHei UI");
    int m_editorFontPointSize = 13;
    bool m_animationsEnabled = true;
    QParallelAnimationGroup *m_windowTransitionGroup = nullptr;
    QVariantAnimation *m_windowOpacityAnimation = nullptr;
    QVariantAnimation *m_windowGeometryAnimation = nullptr;
    QRect m_windowRestingGeometry;
    QString m_windowScreenName;
    QPoint m_windowScreenOffset;
    bool m_screenAdditionPending = false;
    int m_screenOverlapRetries = 0;
    bool m_hiding = false;
    bool m_hideWhenAnimationFinishes = false;
    bool m_discardClose = false;
    bool m_windowTransitionPreparationStable = true;
    QElapsedTimer m_mouseClickTimer;
    qint64 m_lastMouseClickElapsedMs = -1;
    QPointF m_lastMouseClickScenePosition;
    bool m_multiClickPress = false;
    QString m_editorBaselineText;
    QString m_pendingHistoryId;
    bool m_historyLoadConfirmationVisible = false;
    bool m_historyClearConfirmationVisible = false;
    QString m_settingsError;
    QTimer m_screenConfigurationTimer;
    QTimer m_displayChangeSettleTimer;
    bool m_nativeDisplayChangeActive = false;

    PendingRequest m_pendingInput;
    QElapsedTimer m_inputTimer;
    quint64 m_inputSequence = 0;

    PendingRequest m_pendingAnimation;
    QMetaObject::Connection m_animationFrameConnection;
    QMutex m_animationMutex;
    QElapsedTimer m_animationTimer;
    QVector<double> m_animationFrameIntervals;
    qint64 m_lastAnimationFrameNs = 0;
};
