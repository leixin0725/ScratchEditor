#pragma once

#include "editorcontroller.h"

#include <QMutex>

class QKeyEvent;

class EditorController::TestSupport final : public QObject
{
public:
    explicit TestSupport(EditorController &controller);
    void registerCommands(QHash<QString, CommandEntry> &handlers);
    void appendDiagnostics(QJsonObject &status) const;
    bool needsFirstFrame() const { return !m_firstFrameCaptured; }
    QString firstFrameColor() const { return m_firstFrameColor; }
    void captureFirstFrame();
    void observeKeyPress(QKeyEvent *event);
    void animationBenchmarkFinished();

private:
    struct PendingRequest {
        QPointer<QLocalSocket> socket;
        qint64 startedNs = 0;
        QString requestId;
    };

    void captureEditorRenderSample(QLocalSocket *socket, QJsonObject response,
                                   const QRectF &editorLocalRect, qint64 startedNs,
                                   const QString &requestId);
    void captureStatusPanelBorderSample(QLocalSocket *socket, QJsonObject response,
                                        qint64 startedNs, const QString &requestId);
    void runLargeDocumentBenchmark(QLocalSocket *socket, qint64 startedNs,
                                   const QString &requestId);
    void runImeBenchmark(QLocalSocket *socket, qint64 startedNs, const QString &requestId);
    void runAnimationBenchmark(QLocalSocket *socket, qint64 startedNs,
                               const QString &requestId);
    void restoreTestDocument();

    EditorController &m_controller;
    const QPointer<QQuickWindow> &m_window;
    const QPointer<QObject> &m_editor;
    EditorCommandRegistry *const m_commands;
    ClipboardHistoryCoordinator *const m_clipboardHistory;
    AppSettings *const m_settings;
    bool m_firstFrameCaptured = false;
    QString m_firstFrameColor;
    QString m_savedTestText;
    bool m_hasSavedTestText = false;
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
