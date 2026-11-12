#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QVector>

class QQuickWindow;
class QTextDocument;

class InputAutoScroll final : public QObject
{
public:
    InputAutoScroll(const QPointer<QObject> &editor, const QPointer<QTextDocument> &document,
                    const QPointer<QQuickWindow> &window);
    void beginTracking(const QString &kind);
    void queueCheck();
    QVariantMap diagnostics() const;

private:
    void checkAfterLayout();
    struct InputScrollDiagnostics {
        QString lastKind;
        qint64 inputCount = 0;
        qint64 checkCount = 0;
        qint64 triggerCount = 0;
        qint64 overrideCount = 0;
        qreal currentY = 0.0;
        qreal cursorTop = 0.0;
        qreal cursorBottom = 0.0;
        qreal viewportHeight = 0.0;
        qreal maxY = 0.0;
        qreal targetY = 0.0;
        qreal settleY = 0.0;
        qreal heldY = 0.0;
        int preLength = -1;
        int docLength = -1;
        int cursorPosition = -1;
        bool atStart = false;
        bool atEnd = false;
        bool touchedTop = false;
        bool touchedBottom = false;
        bool didScroll = false;
        bool overrideDetected = false;
    };

    struct InputScrollEvent {
        QString type;
        QString kind;
        qint64 seq = 0;
        qreal preY = 0.0;
        qreal curY = 0.0;
        qreal curBottom = 0.0;
        qreal vh = 0.0;
        qreal maxY = 0.0;
        qreal targetY = 0.0;
        qreal settleY = 0.0;
        qreal heldY = 0.0;
        int preLen = -1;
        int docLen = -1;
        int cursorPos = -1;
        bool atStart = false;
        bool atEnd = false;
        bool touchedTop = false;
        bool touchedBottom = false;
        bool didScroll = false;
        bool earlyReturn = false;
    };

    void recordInputScrollEvent(InputScrollEvent event);

    const QPointer<QObject> &m_editor;
    const QPointer<QTextDocument> &m_document;
    const QPointer<QQuickWindow> &m_window;
    bool m_inputAutoScrollCheckQueued = false;
    bool m_inputAutoScrollTrackingActive = false;
    bool m_inputScrollHoldWasActive = false;
    int m_inputPreTextLength = -1;
    qreal m_inputPreScrollY = 0.0;
    InputScrollDiagnostics m_inputScrollDiag;
    QVector<InputScrollEvent> m_inputScrollEvents;
    qint64 m_inputScrollEventSeq = 0;
    qint64 m_inputScrollEarlyReturnCount = 0;
};
