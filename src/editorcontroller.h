#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QLocalServer>
#include <QMutex>
#include <QPointer>
#include <QRect>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>

class QLocalSocket;
class QParallelAnimationGroup;
class QQuickWindow;
class QVariantAnimation;
class AppSettings;
class EditorCommandRegistry;
class ExternalFileSession;
class MarkdownHighlighter;
class MarkdownStyle;

class EditorController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool visible READ isVisible NOTIFY visibleChanged)
    Q_PROPERTY(bool testMode READ testMode CONSTANT)
    Q_PROPERTY(bool externalFileMode READ externalFileMode CONSTANT)
    Q_PROPERTY(QString externalFileName READ externalFileName CONSTANT)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool statusHealthy READ statusHealthy NOTIFY statusMessageChanged)
    Q_PROPERTY(bool clipboardHealthy READ clipboardHealthy NOTIFY clipboardStateChanged)
    Q_PROPERTY(QVariantList commands READ commands NOTIFY commandsChanged)
    Q_PROPERTY(bool markdownHighlighting READ markdownHighlighting NOTIFY markdownHighlightingChanged)
    Q_PROPERTY(QString theme READ theme NOTIFY appearanceChanged)
    Q_PROPERTY(QString editorFontFamily READ editorFontFamily NOTIFY appearanceChanged)
    Q_PROPERTY(int editorFontPointSize READ editorFontPointSize NOTIFY appearanceChanged)
    Q_PROPERTY(bool animationsEnabled READ animationsEnabled NOTIFY appearanceChanged)
    Q_PROPERTY(QString settingsFile READ settingsFile CONSTANT)
    Q_PROPERTY(QString settingsError READ settingsError NOTIFY settingsErrorChanged)
    Q_PROPERTY(QString markdownTextColor READ markdownTextColor CONSTANT)
    Q_PROPERTY(QString markdownStyleFile READ markdownStyleFile CONSTANT)
    Q_PROPERTY(bool markdownStyleLoaded READ markdownStyleLoaded CONSTANT)

public:
    explicit EditorController(bool testMode, QElapsedTimer *startupTimer,
                              const QString &externalFilePath = {}, QObject *parent = nullptr);
    ~EditorController() override;

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
    bool completeExternalFileTest(const QString &text);
    QString statusMessage() const;
    bool statusHealthy() const;
    bool clipboardHealthy() const;
    QVariantList commands() const;
    bool markdownHighlighting() const;
    QString theme() const;
    QString editorFontFamily() const;
    int editorFontPointSize() const;
    bool animationsEnabled() const;
    QString settingsFile() const;
    QString settingsError() const;
    QString markdownTextColor() const;
    QString markdownStyleFile() const;
    bool markdownStyleLoaded() const;

    Q_INVOKABLE void registerWindow(QQuickWindow *window);
    Q_INVOKABLE void registerEditor(QObject *editor);
    Q_INVOKABLE void hideEditor();
    Q_INVOKABLE bool saveExternalFile();
    Q_INVOKABLE void animationBenchmarkFinished();
    Q_INVOKABLE bool executeCommand(const QString &commandId);
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

    void showEditor();
    void toggleEditor();
    void shutdown();

signals:
    void visibleChanged();
    void statusMessageChanged();
    void clipboardStateChanged();
    void commandsChanged();
    void markdownHighlightingChanged();
    void appearanceChanged();
    void settingsErrorChanged();
    void uiCommandRequested(const QString &commandId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct PendingRequest {
        QPointer<QLocalSocket> socket;
        qint64 startedNs = 0;
        QString requestId;
    };

    void acceptConnections();
    void readSocket(QLocalSocket *socket);
    void dispatchCommand(QLocalSocket *socket, const QJsonObject &request, qint64 startedNs);
    void sendResponse(QLocalSocket *socket, QJsonObject response, qint64 startedNs,
                      const QString &requestId = {});
    void sendError(QLocalSocket *socket, const QString &command, const QString &message,
                   qint64 startedNs, const QString &requestId = {});
    void showForRequest(QLocalSocket *socket, const QString &command, qint64 startedNs,
                        const QString &requestId, const QJsonObject &request);
    void waitForNextFrame(QLocalSocket *socket, QJsonObject response, qint64 startedNs,
                          const QString &requestId);
    bool commitAndHide();
    bool commitExternalFileAndExit();
    void setExternalFileState(bool healthy, const QString &message);
    void startWindowTransition(qreal targetOpacity, const QRect &targetGeometry,
                               bool hideWhenFinished);
    void finishWindowHide();
    QRect scaledWindowGeometry(const QRect &restingGeometry) const;
    void applyNativeWindowStyle();
    void updateReadyState();
    bool readClipboardText(QString *text, QString *errorMessage);
    bool writeClipboardText(const QString &text, QString *errorMessage);
    void setClipboardState(bool healthy, const QString &message = {});
    bool restoreWindowGeometry();
    void saveWindowGeometry();
    QRect validatedWindowGeometry(const QRect &requested) const;
    void restorePreviousFocus();
    QJsonObject statusObject() const;
    void runLargeDocumentBenchmark(QLocalSocket *socket, qint64 startedNs,
                                   const QString &requestId);
    void runImeBenchmark(QLocalSocket *socket, qint64 startedNs, const QString &requestId);
    void runAnimationBenchmark(QLocalSocket *socket, qint64 startedNs,
                               const QString &requestId);
    void restoreTestDocument();
    void reloadAppearance();

    QLocalServer m_server;
    QHash<QLocalSocket *, QByteArray> m_buffers;
    QPointer<QQuickWindow> m_window;
    QPointer<QObject> m_editor;
    std::unique_ptr<AppSettings> m_settings;
    std::unique_ptr<EditorCommandRegistry> m_commands;
    std::unique_ptr<ExternalFileSession> m_externalFileSession;
    std::unique_ptr<MarkdownStyle> m_markdownStyle;
    QPointer<MarkdownHighlighter> m_markdownHighlighter;
    QElapsedTimer *m_startupTimer = nullptr;
    QElapsedTimer m_monotonic;
    bool m_testMode = false;
    bool m_externalFileReady = false;
    bool m_externalFileLoadedIntoEditor = false;
    bool m_externalFileCompleted = false;
    QString m_externalFileText;
    QString m_externalFileError;
    bool m_ready = false;
    qint64 m_readyStartupMs = -1;
    bool m_positioned = false;
    bool m_firstFrameCaptured = false;
    QString m_firstFrameColor;
    QString m_statusMessage = QStringLiteral("Esc 关闭并复制");
    bool m_statusHealthy = true;
    bool m_clipboardHealthy = true;
    quintptr m_previousForegroundWindow = 0;
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
    bool m_hiding = false;
    bool m_hideWhenAnimationFinishes = false;
    bool m_windowTransitionPreparationStable = true;
    QString m_settingsError;

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
