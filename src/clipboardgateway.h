#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>

struct ClipboardCaptureCandidate
{
    enum class Kind { Text, Empty, NonText, ReadFailure };
    enum class IncludeInHistory { Allow, Deny, Missing, Malformed };

    Kind kind = Kind::ReadFailure;
    QString text;
    quint32 sequenceNumber = 0;
    qint64 capturedAtUtcMs = 0;
    bool excludeFromMonitor = false;
    IncludeInHistory includeInHistory = IncludeInHistory::Missing;
};

class ClipboardGateway : public QObject
{
    Q_OBJECT

public:
    explicit ClipboardGateway(QObject *parent = nullptr);
    ~ClipboardGateway() override;

    virtual QString backendName() const = 0;
    virtual quint64 nativeAccessAttempts() const = 0;
    virtual bool startMonitoring(quintptr windowHandle, QString *errorMessage) = 0;
    virtual void stopMonitoring() = 0;
    virtual bool monitoring() const = 0;
    virtual quint32 sequenceNumber() = 0;
    virtual ClipboardCaptureCandidate readHistoryCandidate(QString *errorMessage) = 0;
    virtual bool readText(QString *text, QString *errorMessage) = 0;
    virtual bool writeText(const QString &text, QString *errorMessage) = 0;
    virtual bool deliverText(quintptr editorWindowHandle, QString *errorMessage) = 0;

    virtual void setTestClipboardText(const QString &text);
    virtual QString testClipboardText() const;
    virtual QString testDeliveredText() const;
    virtual ClipboardCaptureCandidate injectTestChange(const ClipboardCaptureCandidate &candidate);
    virtual void setTestFault(const QString &operation, bool enabled);

    static std::unique_ptr<ClipboardGateway> create(bool testMode, QObject *parent = nullptr);
    static bool decodeUnicodeTextBuffer(const QByteArray &bytes, QString *text,
                                        QString *errorMessage);
};
