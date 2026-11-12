#include "editortestsupport.h"
#include "markdownfence.h"
#include "appsettings.h"
#include "clipboardhistorycoordinator.h"
#include "editorcommandregistry.h"
#include "markdownhighlighter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLocalSocket>
#include <QMouseEvent>
#include <QMutexLocker>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickTextDocument>
#include <QStyleHints>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QtMath>

#include <algorithm>
#include <numeric>

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

QString clipboardCaptureOutcomeName(ClipboardHistoryCoordinator::CaptureOutcome outcome)
{
    using Outcome = ClipboardHistoryCoordinator::CaptureOutcome;
    switch (outcome) {
    case Outcome::Unavailable: return QStringLiteral("unavailable");
    case Outcome::Inserted: return QStringLiteral("inserted");
    case Outcome::DuplicateRefreshed: return QStringLiteral("duplicateRefreshed");
    case Outcome::Empty: return QStringLiteral("empty");
    case Outcome::Oversize: return QStringLiteral("oversize");
    case Outcome::SelfWriteNotification: return QStringLiteral("selfWriteNotification");
    case Outcome::ExcludedFromMonitor: return QStringLiteral("excludedFromMonitor");
    case Outcome::ExcludedFromHistory: return QStringLiteral("excludedFromHistory");
    case Outcome::NonText: return QStringLiteral("nonText");
    case Outcome::ReadFailure: return QStringLiteral("readFailure");
    }
    return QStringLiteral("readFailure");
}

} // namespace

EditorController::TestSupport::TestSupport(EditorController &controller)
    : m_controller(controller)
    , m_window(controller.m_window)
    , m_editor(controller.m_editor)
    , m_commands(controller.m_commands.get())
    , m_clipboardHistory(controller.m_clipboardHistory.get())
    , m_settings(controller.m_settings.get())
{
}

void EditorController::TestSupport::registerCommands(QHash<QString, CommandEntry> &handlers)
{
    using Gate = CommandEntry::Gate;
    handlers.insert(QHash<QString, CommandEntry>{
        {QStringLiteral("quit"), {Gate::Test, [this](const DispatchRequest &r) {
            QJsonObject response;
            response.insert(QStringLiteral("command"), r.command);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
            QTimer::singleShot(25, qApp, [] { QCoreApplication::exit(0); });
        }}},
        {QStringLiteral("awaitInputFrame"), {Gate::Test, [this](const DispatchRequest &r) {
            if (m_pendingInput.socket) {
                m_controller.sendError(r.socket, r.command, QStringLiteral("input benchmark already armed"),
                          r.startedNs, r.requestId);
            } else {
                m_pendingInput = {r.socket, r.startedNs, r.requestId};
            }
        }}},
        {QStringLiteral("benchmarkLargeDocument"), {Gate::Test, [this](const DispatchRequest &r) {
            runLargeDocumentBenchmark(r.socket, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("restoreTestDocument"), {Gate::Test, [this](const DispatchRequest &r) {
            restoreTestDocument();
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("benchmarkIme"), {Gate::Test, [this](const DispatchRequest &r) {
            runImeBenchmark(r.socket, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("benchmarkAnimation"), {Gate::Test, [this](const DispatchRequest &r) {
            runAnimationBenchmark(r.socket, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("clearTestText"), {Gate::Test, [this](const DispatchRequest &r) {
            if (m_commands) {
                m_commands->resetHeadingFolds();
            }
            m_editor->setProperty("text", QString());
            m_editor->setProperty("cursorPosition", 0);
            QJsonObject response;
            response.insert(QStringLiteral("command"), r.command);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testText"), {Gate::Test, [this](const DispatchRequest &r) {
            QJsonObject response;
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("text"), m_editor->property("text").toString());
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testSetText"), {Gate::Test, [this](const DispatchRequest &r) {
            const QString text = r.request.value(QStringLiteral("text")).toString();
            if (m_commands) {
                m_commands->resetHeadingFolds();
            }
            m_editor->setProperty("text", text);
            m_editor->setProperty("cursorPosition", text.size());
            if (auto *quickDocument = qvariant_cast<QQuickTextDocument *>(
                    m_editor->property("textDocument"))) {
                quickDocument->textDocument()->clearUndoRedoStacks();
            }
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testSetSelection"), {Gate::Test, [this](const DispatchRequest &r) {
            const int start = r.request.value(QStringLiteral("start")).toInt();
            const int end = r.request.value(QStringLiteral("end")).toInt();
            const bool hasCursor = r.request.contains(QStringLiteral("cursor"));
            const int cursor = r.request.value(QStringLiteral("cursor")).toInt();
            bool invoked = false;
            if (hasCursor && cursor < end) {
                // 反向选区：先让光标落在 end，再移动 active end 到 start。
                m_editor->setProperty("cursorPosition", end);
                invoked = QMetaObject::invokeMethod(m_editor, "moveCursorSelection",
                                                    Q_ARG(int, start));
            } else {
                invoked = QMetaObject::invokeMethod(m_editor, "select",
                                                    Q_ARG(int, start), Q_ARG(int, end));
                if (hasCursor && cursor != end) {
                    m_editor->setProperty("cursorPosition", cursor);
                }
            }
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("invoked"), invoked);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testInsertDroppedUrls"), {Gate::Test, [this](const DispatchRequest &r) {
            QList<QUrl> urls;
            const QJsonArray values = r.request.value(QStringLiteral("urls")).toArray();
            urls.reserve(values.size());
            for (const QJsonValue &value : values) {
                urls.append(QUrl(value.toString()));
            }
            const bool inserted = m_controller.insertDroppedUrls(urls);
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("inserted"), inserted);
            response.insert(QStringLiteral("text"), m_editor->property("text").toString());
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testSetScrollY"), {Gate::Test, [this](const DispatchRequest &r) {
            // 确定性构造滚动初始状态，供翻页/自动滚动测试使用。
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            bool applied = false;
            if (QQuickItem *viewport = m_commands ? m_commands->editorViewport() : nullptr) {
                const qreal maximumY = qMax<qreal>(
                    0.0, viewport->property("contentHeight").toReal() - viewport->height());
                const qreal requestedY = qBound<qreal>(
                    0.0, r.request.value(QStringLiteral("contentY")).toDouble(), maximumY);
                viewport->setProperty("contentY", requestedY);
                response.insert(QStringLiteral("contentY"), requestedY);
                applied = true;
            }
            response.insert(QStringLiteral("applied"), applied);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testClipboard"), {Gate::Test, [this](const DispatchRequest &r) {
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("text"), m_clipboardHistory
                                ? m_clipboardHistory->testClipboardText() : QString());
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testSetClipboard"), {Gate::Test, [this](const DispatchRequest &r) {
            if (m_clipboardHistory) {
                m_clipboardHistory->setTestClipboardText(
                    r.request.value(QStringLiteral("text")).toString());
            }
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("ok"), true);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testDeliveredText"), {Gate::Test, [this](const DispatchRequest &r) {
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("text"), m_clipboardHistory
                                ? m_clipboardHistory->testDeliveredText() : QString());
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testEmitClipboardChange"), {Gate::Test, [this](const DispatchRequest &r) {
            ClipboardCaptureCandidate candidate;
            const QString kind = r.request.value(QStringLiteral("kind")).toString();
            if (kind == QStringLiteral("text")) {
                candidate.kind = ClipboardCaptureCandidate::Kind::Text;
                candidate.text = r.request.value(QStringLiteral("text")).toString();
            } else if (kind == QStringLiteral("empty")) {
                candidate.kind = ClipboardCaptureCandidate::Kind::Empty;
            } else if (kind == QStringLiteral("nonText")) {
                candidate.kind = ClipboardCaptureCandidate::Kind::NonText;
            } else {
                candidate.kind = ClipboardCaptureCandidate::Kind::ReadFailure;
            }
            candidate.sequenceNumber = static_cast<quint32>(
                r.request.value(QStringLiteral("sequenceNumber")).toInteger());
            candidate.capturedAtUtcMs = r.request.contains(QStringLiteral("capturedAtMs"))
                ? r.request.value(QStringLiteral("capturedAtMs")).toInteger()
                : QDateTime::currentMSecsSinceEpoch();
            candidate.excludeFromMonitor =
                r.request.value(QStringLiteral("excludeFromMonitor")).toBool();
            candidate.includeInHistory =
                r.request.value(QStringLiteral("historyFormat")).toString()
                    == QStringLiteral("malformed")
                ? ClipboardCaptureCandidate::IncludeInHistory::Malformed
                : (r.request.value(QStringLiteral("excludeFromHistory")).toBool()
                    ? ClipboardCaptureCandidate::IncludeInHistory::Deny
                    : ClipboardCaptureCandidate::IncludeInHistory::Allow);
            if (m_clipboardHistory) {
                candidate = m_clipboardHistory->injectTestChange(candidate);
            }
            if (r.request.value(QStringLiteral("processThroughMonitor")).toBool()) {
                const quint64 beforeRevision = m_clipboardHistory
                    ? m_clipboardHistory->revision() : 0;
                if (m_clipboardHistory) {
                    m_clipboardHistory->processClipboardChange();
                }
                const QPointer<QLocalSocket> socket = r.socket;
                const qint64 startedNs = r.startedNs;
                const QString requestId = r.requestId;
                const QString command = r.command;
                QTimer::singleShot(100, this, [this, socket, startedNs, requestId,
                                                command, beforeRevision] {
                    QJsonObject response = m_controller.statusObject();
                    const quint64 revision = m_clipboardHistory
                        ? m_clipboardHistory->revision() : 0;
                    const bool captured = revision > beforeRevision;
                    response.insert(QStringLiteral("command"), command);
                    response.insert(QStringLiteral("captured"), captured);
                    response.insert(QStringLiteral("outcome"), captured
                                        ? QStringLiteral("inserted")
                                        : QStringLiteral("notCaptured"));
                    response.insert(QStringLiteral("visibleRevision"),
                                    static_cast<qint64>(revision));
                    m_controller.sendResponse(socket, response, startedNs, requestId);
                });
                return;
            }
            const QString outcome = m_clipboardHistory
                ? clipboardCaptureOutcomeName(
                      m_clipboardHistory->captureTestCandidate(candidate))
                : QStringLiteral("unavailable");
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("ok"), true);
            response.insert(QStringLiteral("captured"),
                            outcome == QStringLiteral("inserted")
                                || outcome == QStringLiteral("duplicateRefreshed"));
            response.insert(QStringLiteral("outcome"), outcome);
            response.insert(QStringLiteral("historyCount"), m_clipboardHistory
                                ? m_clipboardHistory->items().size() : 0);
            response.insert(QStringLiteral("visibleRevision"),
                            static_cast<qint64>(m_clipboardHistory
                                ? m_clipboardHistory->revision() : 0));
            response.insert(QStringLiteral("sequenceNumber"),
                            static_cast<qint64>(candidate.sequenceNumber));
            if (m_controller.isVisible()) {
                m_controller.waitForNextFrame(r.socket, response, r.startedNs, r.requestId);
            } else {
                m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
            }
        }}},
        {QStringLiteral("testClipboardHistoryState"), {Gate::Test, [this](const DispatchRequest &r) {
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            QJsonArray items;
            if (m_clipboardHistory) {
                for (const ClipboardHistoryItem &item : m_clipboardHistory->items()) {
                    QJsonObject value;
                    value.insert(QStringLiteral("id"), item.id);
                    value.insert(QStringLiteral("text"), item.text);
                    value.insert(QStringLiteral("capturedAtMs"), item.capturedAtUtcMs);
                    value.insert(QStringLiteral("characterCount"), item.text.size());
                    items.append(value);
                }
                QJsonArray visibleIds;
                for (const QString &id : m_clipboardHistory->visibleIds()) {
                    visibleIds.append(id);
                }
                response.insert(QStringLiteral("visibleIds"), visibleIds);
                response.insert(QStringLiteral("query"), m_clipboardHistory->filter());
                response.insert(QStringLiteral("selectedId"),
                                m_clipboardHistory->selectedId());
                response.insert(QStringLiteral("revision"),
                                static_cast<qint64>(m_clipboardHistory->revision()));
            }
            response.insert(QStringLiteral("items"), items);
            response.insert(QStringLiteral("persistedRevision"),
                            static_cast<qint64>(m_clipboardHistory
                                ? m_clipboardHistory->persistedRevision() : 0));
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testResetClipboardHistory"), {Gate::Test, [this](const DispatchRequest &r) {
            QString error;
            if (m_clipboardHistory) {
                const QString directory = QFileInfo(m_settings->fileName()).absolutePath();
                m_clipboardHistory->resetForTest(directory, &error);
            }
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("ok"), error.isEmpty());
            if (!error.isEmpty()) response.insert(QStringLiteral("error"), error);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testSetClipboardHistoryFault"), {Gate::Test, [this](const DispatchRequest &r) {
            const QString operation = r.request.value(QStringLiteral("operation")).toString();
            const bool enabled = r.request.value(QStringLiteral("enabled")).toBool();
            if (m_clipboardHistory) m_clipboardHistory->setTestFault(operation, enabled);
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("ok"), true);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testRestartClipboardMonitoring"), {Gate::Test, [this](const DispatchRequest &r) {
            const bool restarted = m_clipboardHistory
                && m_clipboardHistory->restartMonitoring(0);
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("ok"), restarted);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testWaitForClipboardHistoryIdle"), {Gate::Test, [this](const DispatchRequest &r) {
            const int timeoutMs = qBound(1, r.request.value(QStringLiteral("timeoutMs")).toInt(5000), 10000);
            const bool idle = !m_clipboardHistory
                || m_clipboardHistory->waitForIdle(timeoutMs);
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("ok"), idle);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testClipboardHistoryUiAction"), {Gate::Test, [this](const DispatchRequest &r) {
            QVariant accepted;
            const bool invoked = m_window && QMetaObject::invokeMethod(
                m_window, "dispatchHistoryTestAction", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, accepted),
                Q_ARG(QVariant, r.request.value(QStringLiteral("action")).toVariant()),
                Q_ARG(QVariant, r.request.value(QStringLiteral("value")).toVariant()));
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("invoked"), invoked && accepted.toBool());
            m_controller.waitForNextFrame(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testFindPanelUiAction"), {Gate::Test, [this](const DispatchRequest &r) {
            QVariant accepted;
            const bool invoked = m_window && QMetaObject::invokeMethod(
                m_window, "dispatchFindTestAction", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, accepted),
                Q_ARG(QVariant, r.request.value(QStringLiteral("action")).toVariant()),
                Q_ARG(QVariant, r.request.value(QStringLiteral("value")).toVariant()));
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("invoked"), invoked && accepted.toBool());
            m_controller.waitForNextFrame(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testClipboardHistoryWindowLeave"), {Gate::Test, [this](const DispatchRequest &r) {
            const QPointF localPosition(
                r.request.value(QStringLiteral("x")).toDouble(),
                r.request.value(QStringLiteral("y")).toDouble());
            const bool emitted = m_controller.handleClipboardHistoryWindowLeave(
                localPosition, r.request.value(QStringLiteral("buttonDown")).toBool());
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("edgeExitEmitted"), emitted);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testDragClipboardHistory"), {Gate::Test, [this](const DispatchRequest &r) {
            const QString id = r.request.value(QStringLiteral("id")).toString();
            const int dropPosition = r.request.value(QStringLiteral("dropPosition")).toInt();
            const bool outsideEditor = r.request.value(
                QStringLiteral("outsideEditor")).toBool();
            QRectF dropRectangle;
            const bool dropLocated = m_editor && QMetaObject::invokeMethod(
                m_editor, "positionToRectangle", Qt::DirectConnection,
                Q_RETURN_ARG(QRectF, dropRectangle), Q_ARG(int, dropPosition));
            bool armed = false;
            bool dragActivated = false;
            bool canDrop = false;
            bool inserted = false;
            if (dropLocated) {
                if (auto *item = qobject_cast<QQuickItem *>(m_editor.data())) {
                    const QPointF dropScene = item->mapToScene(dropRectangle.center());
                    const QPointF pressScene = dropScene - QPointF(100.0, 100.0);
                    const QPointF releaseScene = outsideEditor
                        ? QPointF(-500.0, -500.0) : dropScene;
                    armed = m_controller.beginClipboardHistoryDrag(id, pressScene.x(), pressScene.y());
                    const QVariantMap update = m_controller.updateClipboardHistoryDrag(
                        releaseScene.x(), releaseScene.y());
                    dragActivated = update.value(QStringLiteral("active")).toBool();
                    canDrop = update.value(QStringLiteral("canDrop")).toBool();
                    inserted = m_controller.finishClipboardHistoryDrag(
                        releaseScene.x(), releaseScene.y());
                }
            }
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("armed"), armed);
            response.insert(QStringLiteral("dragActivated"), dragActivated);
            response.insert(QStringLiteral("canDrop"), canDrop);
            response.insert(QStringLiteral("inserted"), inserted);
            response.insert(QStringLiteral("text"),
                            m_editor ? m_editor->property("text").toString() : QString());
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testClipboardHistoryDragUi"), {Gate::Test, [this](const DispatchRequest &r) {
            QVariant accepted;
            const bool invoked = m_window && QMetaObject::invokeMethod(
                m_window, "dispatchHistoryTestDrag", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, accepted),
                Q_ARG(QVariant, r.request.value(QStringLiteral("id")).toVariant()),
                Q_ARG(QVariant, r.request.value(QStringLiteral("dropPosition")).toVariant()),
                Q_ARG(QVariant, r.request.value(QStringLiteral("activateDrag")).toVariant()),
                Q_ARG(QVariant, r.request.value(QStringLiteral("outsideEditor")).toVariant()),
                Q_ARG(QVariant, r.request.value(QStringLiteral("finishDrag")).toVariant()),
                Q_ARG(QVariant, r.request.value(QStringLiteral("previewText")).toVariant()));
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("eventsAccepted"), invoked && accepted.toBool());
            response.insert(QStringLiteral("text"),
                            m_editor ? m_editor->property("text").toString() : QString());
            m_controller.waitForNextFrame(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testNativeSelectionDrag"), {Gate::Test, [this](const DispatchRequest &r) {
            const QString phase = r.request.value(QStringLiteral("phase")).toString();
            const QString edge = r.request.value(QStringLiteral("edge")).toString();
            QQuickItem *item = qobject_cast<QQuickItem *>(m_editor.data());
            QQuickItem *viewport = m_commands ? m_commands->editorViewport() : nullptr;
            bool eventsAccepted = false;
            if (item && viewport && (edge == QLatin1String("top")
                                     || edge == QLatin1String("bottom"))) {
                const QPointF viewportSceneTopLeft = viewport->mapToScene(QPointF());
                const qreal edgeSceneY = edge == QLatin1String("top")
                    ? viewportSceneTopLeft.y() - 16.0
                    : viewportSceneTopLeft.y() + viewport->height() + 16.0;
                const QPointF edgeScene(viewportSceneTopLeft.x() + viewport->width() / 2.0,
                                        edgeSceneY);
                const auto sendMouseEvent = [this, item](QEvent::Type type,
                                                          const QPointF &scenePosition,
                                                          Qt::MouseButton button,
                                                          Qt::MouseButtons buttons) {
                    const QPointF localPosition = item->mapFromScene(scenePosition);
                    const QPointF globalPosition = item->mapToGlobal(localPosition);
                    QMouseEvent mouseEvent(type, localPosition, scenePosition, globalPosition,
                                           button, buttons, Qt::NoModifier);
                    const bool handled = m_commands->handleEditorEvent(&mouseEvent);
                    return handled || QCoreApplication::sendEvent(m_editor, &mouseEvent);
                };
                if (phase == QLatin1String("begin")) {
                    const int position = r.request.value(QStringLiteral("position")).toInt();
                    QRectF pressRectangle;
                    const bool located = QMetaObject::invokeMethod(
                        m_editor, "positionToRectangle", Qt::DirectConnection,
                        Q_RETURN_ARG(QRectF, pressRectangle), Q_ARG(int, position));
                    if (located) {
                        const QPointF pressScene = item->mapToScene(pressRectangle.center());
                        const bool pressed = sendMouseEvent(QEvent::MouseButtonPress, pressScene,
                                                            Qt::LeftButton, Qt::LeftButton);
                        const bool moved = sendMouseEvent(QEvent::MouseMove, edgeScene,
                                                          Qt::NoButton, Qt::LeftButton);
                        eventsAccepted = pressed && moved;
                    }
                } else if (phase == QLatin1String("finish")) {
                    eventsAccepted = sendMouseEvent(QEvent::MouseButtonRelease, edgeScene,
                                                    Qt::LeftButton, Qt::NoButton);
                }
            }
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("eventsAccepted"), eventsAccepted);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testDragSelection"), {Gate::Test, [this](const DispatchRequest &r) {
            // 合成的拖拽按压事件视为一次全新的单击，避免与多重点击放行逻辑互相干扰。
            m_controller.m_lastMouseClickElapsedMs = -1;
            m_controller.m_multiClickPress = false;
            const int start = r.request.value(QStringLiteral("start")).toInt();
            const int end = r.request.value(QStringLiteral("end")).toInt();
            const int dropPosition = r.request.value(QStringLiteral("dropPosition")).toInt();
            const QString before = m_editor->property("text").toString();
            QRectF pressRectangle;
            QRectF dropRectangle;
            const int pressPosition = start + (end - start) / 2;
            const bool pressLocated = QMetaObject::invokeMethod(
                m_editor, "positionToRectangle", Qt::DirectConnection,
                Q_RETURN_ARG(QRectF, pressRectangle), Q_ARG(int, pressPosition));
            const bool dropLocated = QMetaObject::invokeMethod(
                m_editor, "positionToRectangle", Qt::DirectConnection,
                Q_RETURN_ARG(QRectF, dropRectangle), Q_ARG(int, dropPosition));
            bool eventsAccepted = false;
            if (pressLocated && dropLocated) {
                if (auto *item = qobject_cast<QQuickItem *>(m_editor.data())) {
                    const QPointF pressLocal = pressRectangle.center();
                    const QPointF dropLocal = dropRectangle.center();
                    const QPointF activationLocal = pressLocal + QPointF(100.0, 100.0);
                    const QPointF pressScene = item->mapToScene(pressLocal);
                    const QPointF dropScene = item->mapToScene(dropLocal);
                    const QPointF activationScene = item->mapToScene(activationLocal);
                    const QPointF pressGlobal = item->mapToGlobal(pressLocal);
                    const QPointF dropGlobal = item->mapToGlobal(dropLocal);
                    const QPointF activationGlobal = item->mapToGlobal(activationLocal);
                    QMouseEvent pressEvent(QEvent::MouseButtonPress, pressScene, pressScene,
                                           pressGlobal, Qt::LeftButton, Qt::LeftButton,
                                           Qt::NoModifier);
                    QMouseEvent activationEvent(QEvent::MouseMove, activationScene,
                                                activationScene, activationGlobal, Qt::NoButton,
                                                Qt::LeftButton, Qt::NoModifier);
                    QMouseEvent moveEvent(QEvent::MouseMove, dropScene, dropScene, dropGlobal,
                                          Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                    QMouseEvent releaseEvent(QEvent::MouseButtonRelease, dropScene, dropScene,
                                             dropGlobal, Qt::LeftButton, Qt::NoButton,
                                             Qt::NoModifier);
                    const bool pressAccepted = QCoreApplication::sendEvent(m_window, &pressEvent);
                    const bool activationAccepted = QCoreApplication::sendEvent(
                        m_window, &activationEvent);
                    const bool moveAccepted = QCoreApplication::sendEvent(m_window, &moveEvent);
                    const bool releaseAccepted = QCoreApplication::sendEvent(
                        m_window, &releaseEvent);
                    eventsAccepted = pressAccepted && activationAccepted
                        && moveAccepted && releaseAccepted;
                    item->ungrabMouse();
                }
            }
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("eventsAccepted"), eventsAccepted);
            response.insert(QStringLiteral("moved"),
                            m_editor->property("text").toString() != before);
            response.insert(QStringLiteral("text"), m_editor->property("text").toString());
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testTripleClick"), {Gate::Test, [this](const DispatchRequest &r) {
            // 合成 Press → DblClick → Press 序列模拟三击：时间戳统一为 0 使第三次
            // Press 落在双击间隔内，坐标相同满足拖拽距离限制，交给 QML TextEdit
            // 原生逻辑选中整行。开始时重置多重点击状态，避免与放行逻辑互相干扰。
            m_controller.m_lastMouseClickElapsedMs = -1;
            m_controller.m_multiClickPress = false;
            const int position = r.request.value(QStringLiteral("position")).toInt();
            QRectF clickRectangle;
            const bool clickLocated = QMetaObject::invokeMethod(
                m_editor, "positionToRectangle", Qt::DirectConnection,
                Q_RETURN_ARG(QRectF, clickRectangle), Q_ARG(int, position));
            QJsonObject response = m_controller.statusObject();
            bool eventsAccepted = false;
            if (clickLocated) {
                if (auto *item = qobject_cast<QQuickItem *>(m_editor.data())) {
                    const QPointF local = clickRectangle.center();
                    const QPointF scene = item->mapToScene(local);
                    const QPointF global = item->mapToGlobal(local);
                    QMouseEvent pressEvent(QEvent::MouseButtonPress, scene, scene,
                                           global, Qt::LeftButton, Qt::LeftButton,
                                           Qt::NoModifier);
                    QMouseEvent doubleClickEvent(QEvent::MouseButtonDblClick, local, scene,
                                                 global, Qt::LeftButton, Qt::LeftButton,
                                                 Qt::NoModifier);
                    QMouseEvent thirdPressEvent(QEvent::MouseButtonPress, scene, scene,
                                                global, Qt::LeftButton, Qt::LeftButton,
                                                Qt::NoModifier);
                    QMouseEvent releaseEvent(QEvent::MouseButtonRelease, scene, scene,
                                             global, Qt::LeftButton, Qt::NoButton,
                                             Qt::NoModifier);
                    pressEvent.setTimestamp(0);
                    doubleClickEvent.setTimestamp(0);
                    thirdPressEvent.setTimestamp(0);
                    releaseEvent.setTimestamp(0);
                    const bool pressAccepted = QCoreApplication::sendEvent(m_window,
                                                                           &pressEvent);
                    // 隔离测试的窗口隐藏启动，Qt 6 的 MouseButtonDblClick 属于“更新
                    // 事件”，只投递给已有独占抓取者，而隐藏窗口下 Press 不会建立抓取。
                    // 因此把 DblClick 直接投递给编辑器 item（与 testKeyPress 同类），
                    // 让控件进入“双击”状态；第三次 Press 仍走完整窗口投递路径，用于
                    // 回归验证多重点击放行逻辑。
                    const bool doubleClickAccepted = QCoreApplication::sendEvent(
                        m_editor, &doubleClickEvent);
                    const bool thirdPressAccepted = QCoreApplication::sendEvent(
                        m_window, &thirdPressEvent);
                    const bool releaseAccepted = QCoreApplication::sendEvent(
                        m_window, &releaseEvent);
                    eventsAccepted = pressAccepted && doubleClickAccepted
                        && thirdPressAccepted && releaseAccepted;
                    item->ungrabMouse();
                }
            }
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("eventsAccepted"), eventsAccepted);
            response.insert(QStringLiteral("selectionStart"),
                            m_editor->property("selectionStart").toInt());
            response.insert(QStringLiteral("selectionEnd"),
                            m_editor->property("selectionEnd").toInt());
            response.insert(QStringLiteral("text"), m_editor->property("text").toString());
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testDoubleClick"), {Gate::Test, [this](const DispatchRequest &r) {
            // 合成一次左键双击：坐标取自 position 的光标矩形中心，直接投递给
            // 编辑器 item（与 testKeyPress 同类），用于验证中英文词选区的落点。
            const int position = r.request.value(QStringLiteral("position")).toInt();
            QRectF clickRectangle;
            const bool clickLocated = QMetaObject::invokeMethod(
                m_editor, "positionToRectangle", Qt::DirectConnection,
                Q_RETURN_ARG(QRectF, clickRectangle), Q_ARG(int, position));
            QJsonObject response = m_controller.statusObject();
            bool eventsAccepted = false;
            if (clickLocated) {
                if (auto *item = qobject_cast<QQuickItem *>(m_editor.data())) {
                    const QPointF local = clickRectangle.center();
                    const QPointF scene = item->mapToScene(local);
                    const QPointF global = item->mapToGlobal(local);
                    QMouseEvent doubleClickEvent(QEvent::MouseButtonDblClick, local, scene,
                                                 global, Qt::LeftButton, Qt::LeftButton,
                                                 Qt::NoModifier);
                    doubleClickEvent.setTimestamp(0);
                    eventsAccepted = QCoreApplication::sendEvent(m_editor,
                                                                 &doubleClickEvent);
                    item->ungrabMouse();
                }
            }
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("eventsAccepted"), eventsAccepted);
            response.insert(QStringLiteral("selectionStart"),
                            m_editor->property("selectionStart").toInt());
            response.insert(QStringLiteral("selectionEnd"),
                            m_editor->property("selectionEnd").toInt());
            response.insert(QStringLiteral("cursorPosition"),
                            m_editor->property("cursorPosition").toInt());
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testUndo"), {Gate::Test, [this](const DispatchRequest &r) {
            // 与真实 Ctrl+Z 走同一路径：撤销视为普通编辑，共用自动滚动检查。
            const bool invoked = m_commands ? m_commands->performUndo() : false;
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("invoked"), invoked);
            response.insert(QStringLiteral("text"), m_editor->property("text").toString());
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testRedo"), {Gate::Test, [this](const DispatchRequest &r) {
            // 与真实重做快捷键走同一路径：重做视为普通编辑，共用自动滚动检查。
            const bool invoked = m_commands ? m_commands->performRedo() : false;
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("invoked"), invoked);
            response.insert(QStringLiteral("text"), m_editor->property("text").toString());
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testKeyPress"), {Gate::Test, [this](const DispatchRequest &r) {
            const QString text = r.request.value(QStringLiteral("text")).toString();
            const QString keyName = r.request.value(QStringLiteral("key")).toString();
            const bool shift = r.request.value(QStringLiteral("shift")).toBool();
            const QString modifierText = r.request.value(QStringLiteral("modifiers")).toString();
            int key = Qt::Key_unknown;
            if (keyName == QStringLiteral("Tab")) {
                key = shift ? Qt::Key_Backtab : Qt::Key_Tab;
            } else if (keyName == QStringLiteral("Down")) {
                key = Qt::Key_Down;
            } else if (keyName == QStringLiteral("Up")) {
                key = Qt::Key_Up;
            } else if (keyName == QStringLiteral("PageUp")) {
                key = Qt::Key_PageUp;
            } else if (keyName == QStringLiteral("PageDown")) {
                key = Qt::Key_PageDown;
            } else if (keyName == QStringLiteral("Left")) {
                key = Qt::Key_Left;
            } else if (keyName == QStringLiteral("Right")) {
                key = Qt::Key_Right;
            } else if (keyName == QStringLiteral("Backspace")) {
                key = Qt::Key_Backspace;
            } else if (keyName == QStringLiteral("Delete")) {
                key = Qt::Key_Delete;
            } else if (keyName == QStringLiteral("Enter")) {
                key = Qt::Key_Return;
            } else if (keyName == QStringLiteral("Escape")) {
                key = Qt::Key_Escape;
            } else if (keyName.size() == 1) {
                key = keyName.front().unicode();
            } else if (!text.isEmpty()) {
                key = text.front().unicode();
            }
            Qt::KeyboardModifiers modifiers = shift ? Qt::ShiftModifier
                                                    : Qt::NoModifier;
            const QStringList modifierParts = modifierText.split(
                QLatin1Char('+'), Qt::SkipEmptyParts);
            for (const QString &part : modifierParts) {
                if (part == QStringLiteral("ctrl")) {
                    modifiers |= Qt::ControlModifier;
                } else if (part == QStringLiteral("shift")) {
                    modifiers |= Qt::ShiftModifier;
                } else if (part == QStringLiteral("alt")) {
                    modifiers |= Qt::AltModifier;
                } else if (part == QStringLiteral("meta")) {
                    modifiers |= Qt::MetaModifier;
                }
            }
            QKeyEvent keyEvent(QEvent::KeyPress, key, modifiers, text);
            QObject *keyTarget = r.request.value(QStringLiteral("activeFocus")).toBool()
                && m_window ? m_window->activeFocusItem() : m_editor.data();
            const bool accepted = keyTarget && QCoreApplication::sendEvent(keyTarget, &keyEvent);
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("accepted"), accepted);
            response.insert(QStringLiteral("text"), m_editor->property("text").toString());
            response.insert(QStringLiteral("cursorPosition"),
                            m_editor->property("cursorPosition").toInt());
            response.insert(QStringLiteral("selectionStart"),
                            m_editor->property("selectionStart").toInt());
            response.insert(QStringLiteral("selectionEnd"),
                            m_editor->property("selectionEnd").toInt());
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testInputMethodCommit"), {Gate::Test, [this](const DispatchRequest &r) {
            QInputMethodEvent inputEvent(
                r.request.value(QStringLiteral("preedit")).toString(), {});
            inputEvent.setCommitString(r.request.value(QStringLiteral("text")).toString());
            QObject *target = r.request.value(QStringLiteral("activeFocus")).toBool()
                && m_window ? m_window->activeFocusItem() : m_editor.data();
            const bool accepted = target && QCoreApplication::sendEvent(target, &inputEvent);
            const bool composing = target && target->property("inputMethodComposing").toBool();
            const QString targetText = target ? target->property("text").toString() : QString();
            QPointer<QLocalSocket> guardedSocket = r.socket;
            QTimer::singleShot(0, this, [this, guardedSocket, accepted, composing, targetText, r] {
                QJsonObject response = m_controller.statusObject();
                response.insert(QStringLiteral("command"), r.command);
                response.insert(QStringLiteral("accepted"), accepted);
                response.insert(QStringLiteral("composing"), composing);
                response.insert(QStringLiteral("targetText"), targetText);
                response.insert(QStringLiteral("text"), m_editor->property("text").toString());
                m_controller.sendResponse(guardedSocket, response, r.startedNs, r.requestId);
            });
        }}},
        {QStringLiteral("testExecuteCommand"), {Gate::Test, [this](const DispatchRequest &r) {
            const QString commandId = r.request.value(QStringLiteral("commandId")).toString();
            const bool executed = m_controller.executeCommand(commandId);
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("commandId"), commandId);
            response.insert(QStringLiteral("executed"), executed);
            response.insert(QStringLiteral("text"), m_editor->property("text").toString());
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testFindNext"), {Gate::Test, [this](const DispatchRequest &r) {
            const bool found = m_controller.findNext(r.request.value(QStringLiteral("query")).toString(),
                                        r.request.value(QStringLiteral("caseSensitive")).toBool(),
                                        r.request.value(QStringLiteral("backwards")).toBool());
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("found"), found);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testReplaceCurrent"), {Gate::Test, [this](const DispatchRequest &r) {
            const bool replaced = m_controller.replaceCurrent(
                r.request.value(QStringLiteral("query")).toString(),
                r.request.value(QStringLiteral("replacement")).toString(),
                r.request.value(QStringLiteral("caseSensitive")).toBool());
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("replaced"), replaced);
            response.insert(QStringLiteral("text"), m_editor->property("text").toString());
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testReplaceAll"), {Gate::Test, [this](const DispatchRequest &r) {
            const int count = m_controller.replaceAll(r.request.value(QStringLiteral("query")).toString(),
                                         r.request.value(QStringLiteral("replacement")).toString(),
                                         r.request.value(QStringLiteral("caseSensitive")).toBool());
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("replacementCount"), count);
            response.insert(QStringLiteral("text"), m_editor->property("text").toString());
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testSetShortcut"), {Gate::Test, [this](const DispatchRequest &r) {
            const QString commandId = r.request.value(QStringLiteral("commandId")).toString();
            const QString sequence = r.request.value(QStringLiteral("sequence")).toString();
            const bool configured = m_controller.setShortcut(commandId, sequence);
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("configured"), configured);
            response.insert(QStringLiteral("commandId"), commandId);
            response.insert(QStringLiteral("shortcut"), m_controller.shortcutFor(commandId));
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testShortcut"), {Gate::Test, [this](const DispatchRequest &r) {
            const QString commandId = r.request.value(QStringLiteral("commandId")).toString();
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("commandId"), commandId);
            response.insert(QStringLiteral("shortcut"), m_controller.shortcutFor(commandId));
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testHeadingFoldState"), {Gate::Test, [this](const DispatchRequest &r) {
            QJsonObject response = m_commands
                ? QJsonObject::fromVariantMap(m_commands->headingFoldDiagnostics())
                : QJsonObject{};
            response.insert(QStringLiteral("command"), r.command);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testHeadingFoldMarkerState"), {Gate::Test,
         [this](const DispatchRequest &r) {
             const int position = r.request.value(QStringLiteral("position")).toInt();
             QVariant markerState;
             const bool invoked = m_window && QMetaObject::invokeMethod(
                 m_window, "headingFoldMarkerStateForTest", Qt::DirectConnection,
                 Q_RETURN_ARG(QVariant, markerState), Q_ARG(QVariant, QVariant(position)));
             QJsonObject response = m_controller.statusObject();
             response.insert(QStringLiteral("command"), r.command);
             response.insert(QStringLiteral("markerFound"),
                             invoked && !markerState.toMap().isEmpty());
             response.insert(QStringLiteral("marker"),
                             QJsonObject::fromVariantMap(markerState.toMap()));
             m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
         }}},
        {QStringLiteral("testClickHeadingFoldMarker"), {Gate::Test, [this](const DispatchRequest &r) {
            const int position = r.request.value(QStringLiteral("position")).toInt();
            QVariant markerResult;
            const bool invoked = m_window && QMetaObject::invokeMethod(
                m_window, "clickHeadingFoldMarkerForTest", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, markerResult), Q_ARG(QVariant, QVariant(position)));
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("markerFound"), invoked && markerResult.toBool());
            QVariant markerState;
            if (invoked && markerResult.toBool()
                && QMetaObject::invokeMethod(
                    m_window, "headingFoldMarkerStateForTest", Qt::DirectConnection,
                    Q_RETURN_ARG(QVariant, markerState), Q_ARG(QVariant, QVariant(position)))) {
                const QVariantMap state = markerState.toMap();
                response.insert(QStringLiteral("markerIconName"),
                                state.value(QStringLiteral("iconName")).toString());
                response.insert(QStringLiteral("markerIconValid"),
                                state.value(QStringLiteral("iconValid")).toBool());
                response.insert(QStringLiteral("markerIconSize"),
                                state.value(QStringLiteral("iconSize")).toInt());
            }
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testEditorRenderSample"), {Gate::Test, [this](const DispatchRequest &r) {
            QRectF sampleRect;
            const QJsonObject requestedRect =
                r.request.value(QStringLiteral("sampleRect")).toObject();
            if (!requestedRect.isEmpty()) {
                sampleRect = QRectF(requestedRect.value(QStringLiteral("x")).toDouble(),
                                    requestedRect.value(QStringLiteral("y")).toDouble(),
                                    requestedRect.value(QStringLiteral("width")).toDouble(),
                                    requestedRect.value(QStringLiteral("height")).toDouble());
            } else {
                QRectF positionRect;
                const int position = r.request.value(QStringLiteral("position")).toInt();
                const bool invoked = m_editor && QMetaObject::invokeMethod(
                    m_editor, "positionToRectangle", Qt::DirectConnection,
                    Q_RETURN_ARG(QRectF, positionRect), Q_ARG(int, position));
                if (!invoked || !positionRect.isValid()) {
                    m_controller.sendError(r.socket, r.command, QStringLiteral("editor position unavailable"),
                              r.startedNs, r.requestId);
                    return;
                }
                sampleRect = QRectF(positionRect.x(), positionRect.y(),
                                    r.request.value(QStringLiteral("width")).toDouble(240.0),
                                    positionRect.height());
            }

            if (!sampleRect.isValid()) {
                m_controller.sendError(r.socket, r.command, QStringLiteral("invalid sample rectangle"),
                          r.startedNs, r.requestId);
                return;
            }
            QJsonObject response;
            response.insert(QStringLiteral("command"), r.command);
            captureEditorRenderSample(r.socket, response, sampleRect,
                                      r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testStatusPanelBorderSample"), {Gate::Test,
          [this](const DispatchRequest &r) {
            QJsonObject response;
            response.insert(QStringLiteral("command"), r.command);
            captureStatusPanelBorderSample(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testHighlightSummary"), {Gate::Test, [this](const DispatchRequest &r) {
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
                    if (MarkdownFence(block.userState()).active()) {
                        ++fencedBlocks;
                    }
                }
            }
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("blocks"), blocks);
            response.insert(QStringLiteral("formattedRanges"), formattedRanges);
            response.insert(QStringLiteral("fencedBlocks"), fencedBlocks);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testFormatAt"), {Gate::Test, [this](const DispatchRequest &r) {
            const int position = r.request.value(QStringLiteral("position")).toInt();
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            if (auto *quickDocument = qvariant_cast<QQuickTextDocument *>(
                    m_editor->property("textDocument"))) {
                const QTextBlock block = quickDocument->textDocument()->findBlock(position);
                const int positionInBlock = position - block.position();
                if (block.isValid() && block.layout()) {
                    for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
                        if (positionInBlock < range.start
                            || positionInBlock >= range.start + range.length) {
                            continue;
                        }
                        const QTextCharFormat &format = range.format;
                        response.insert(QStringLiteral("formatted"), true);
                        response.insert(QStringLiteral("foreground"),
                                        format.foreground().color().name(QColor::HexRgb));
                        response.insert(QStringLiteral("background"),
                                        format.background().color().name(QColor::HexRgb));
                        response.insert(QStringLiteral("bold"),
                                        format.fontWeight() > m_controller.editorFontWeight());
                        response.insert(QStringLiteral("fontWeight"), format.fontWeight());
                        response.insert(QStringLiteral("italic"), format.fontItalic());
                        response.insert(QStringLiteral("strikeThrough"),
                                        format.fontStrikeOut());
                        response.insert(QStringLiteral("underline"), format.fontUnderline());
                        break;
                    }
                }
            }
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testCloseOverlays"), {Gate::Test, [this](const DispatchRequest &r) {
            const bool paletteClosed = QMetaObject::invokeMethod(m_window, "closeCommandPalette");
            const bool findClosed = QMetaObject::invokeMethod(m_window, "hideFindPanel");
            const bool settingsClosed = QMetaObject::invokeMethod(m_window, "closeSettings");
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("paletteClosed"), paletteClosed);
            response.insert(QStringLiteral("findClosed"), findClosed);
            response.insert(QStringLiteral("settingsClosed"), settingsClosed);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testDiscardClose"), {Gate::Test, [this](const DispatchRequest &r) {
            m_controller.discardAndHide();
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testResetShortcuts"), {Gate::Test, [this](const DispatchRequest &r) {
            m_controller.resetShortcuts();
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("boldShortcut"), m_controller.shortcutFor(QStringLiteral("toggleBold")));
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testApplyAppearance"), {Gate::Test, [this](const DispatchRequest &r) {
            const bool applied = m_controller.applyAppearance(
                r.request.value(QStringLiteral("theme")).toString(),
                r.request.value(QStringLiteral("fontFamily")).toString(),
                r.request.value(QStringLiteral("fallbackFontFamily")).toString(),
                r.request.value(QStringLiteral("fontPointSize")).toInt(),
                r.request.value(QStringLiteral("fontWeight")).toInt(),
                r.request.value(QStringLiteral("animationsEnabled")).toBool());
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("applied"), applied);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testResetAppearance"), {Gate::Test, [this](const DispatchRequest &r) {
            m_controller.resetAppearance();
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testApplyStatusPanelSettings"), {Gate::Test, [this](const DispatchRequest &r) {
            const bool applied = m_controller.applyStatusPanelSettings(
                r.request.value(QStringLiteral("fontSize")).toInt(),
                r.request.value(QStringLiteral("showDelayMs")).toInt(),
                r.request.value(QStringLiteral("hideDelayMs")).toInt(),
                r.request.value(QStringLiteral("maxWidth")).toInt());
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("applied"), applied);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testResetStatusPanelSettings"), {Gate::Test, [this](const DispatchRequest &r) {
            m_controller.resetStatusPanelSettings();
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testConfigKeys"), {Gate::Test, [this](const DispatchRequest &r) {
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            response.insert(QStringLiteral("keys"),
                            QJsonArray::fromStringList(m_settings ? m_settings->allKeys()
                                                                 : QStringList{}));
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testSetGeometry"), {Gate::Test, [this](const DispatchRequest &r) {
            const QRect requested(r.request.value(QStringLiteral("x")).toInt(),
                                  r.request.value(QStringLiteral("y")).toInt(),
                                  r.request.value(QStringLiteral("width")).toInt(),
                                  r.request.value(QStringLiteral("height")).toInt());
            m_window->setGeometry(m_controller.validatedWindowGeometry(requested));
            m_controller.m_windowRestingGeometry = m_window->geometry();
            m_controller.m_positioned = true;
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
        {QStringLiteral("testResetSettings"), {Gate::Test, [this](const DispatchRequest &r) {
            m_settings->resetAll();
            m_controller.resetShortcuts();
            m_controller.reloadAppearance();
            m_controller.m_windowRestingGeometry = {};
            m_controller.m_positioned = false;
            QJsonObject response = m_controller.statusObject();
            response.insert(QStringLiteral("command"), r.command);
            m_controller.sendResponse(r.socket, response, r.startedNs, r.requestId);
        }}},
    });
}

void EditorController::TestSupport::captureEditorRenderSample(QLocalSocket *socket, QJsonObject response,
                                                 const QRectF &editorLocalRect,
                                                 qint64 startedNs,
                                                 const QString &requestId)
{
    auto *editorItem = qobject_cast<QQuickItem *>(m_editor.data());
    if (!m_window || !editorItem) {
        m_controller.sendError(socket, response.value(QStringLiteral("command")).toString(),
                  QStringLiteral("editor unavailable"), startedNs, requestId);
        return;
    }

    const QRectF sceneRect(editorItem->mapToScene(editorLocalRect.topLeft()),
                           editorItem->mapToScene(editorLocalRect.bottomRight()));
    QPointer<QLocalSocket> guardedSocket(socket);
    connect(m_window, &QQuickWindow::frameSwapped, this,
            [this, guardedSocket, response, editorLocalRect, sceneRect,
             startedNs, requestId]() mutable {
                QTimer::singleShot(0, this,
                    [this, guardedSocket, response, editorLocalRect, sceneRect,
                     startedNs, requestId]() mutable {
                        if (!m_window) {
                            m_controller.sendError(guardedSocket,
                                      response.value(QStringLiteral("command")).toString(),
                                      QStringLiteral("window unavailable"),
                                      startedNs, requestId);
                            return;
                        }
                        const QImage image = m_window->grabWindow();
                        if (image.isNull() || m_window->width() <= 0 || m_window->height() <= 0) {
                            m_controller.sendError(guardedSocket,
                                      response.value(QStringLiteral("command")).toString(),
                                      QStringLiteral("window capture failed"),
                                      startedNs, requestId);
                            return;
                        }

                        const qreal scaleX = static_cast<qreal>(image.width()) / m_window->width();
                        const qreal scaleY = static_cast<qreal>(image.height()) / m_window->height();
                        const QRect pixelRect = QRectF(sceneRect.x() * scaleX,
                                                       sceneRect.y() * scaleY,
                                                       sceneRect.width() * scaleX,
                                                       sceneRect.height() * scaleY)
                                                    .toAlignedRect()
                                                    .intersected(image.rect());
                        const QColor surfaceColor(
                            m_window->property("themeEditorSurfaceColor").toString());
                        int nonSurfacePixelCount = 0;
                        for (int y = pixelRect.top(); y <= pixelRect.bottom(); ++y) {
                            for (int x = pixelRect.left(); x <= pixelRect.right(); ++x) {
                                const QColor pixel = image.pixelColor(x, y);
                                const int channelDistance = std::max(
                                    {qAbs(pixel.red() - surfaceColor.red()),
                                     qAbs(pixel.green() - surfaceColor.green()),
                                     qAbs(pixel.blue() - surfaceColor.blue())});
                                if (channelDistance > 8) {
                                    ++nonSurfacePixelCount;
                                }
                            }
                        }

                        response.insert(QStringLiteral("sampleRect"), QJsonObject{
                            {QStringLiteral("x"), editorLocalRect.x()},
                            {QStringLiteral("y"), editorLocalRect.y()},
                            {QStringLiteral("width"), editorLocalRect.width()},
                            {QStringLiteral("height"), editorLocalRect.height()}});
                        response.insert(QStringLiteral("samplePixelCount"),
                                        pixelRect.width() * pixelRect.height());
                        response.insert(QStringLiteral("nonSurfacePixelCount"),
                                        nonSurfacePixelCount);
                        const QJsonObject status = m_controller.statusObject();
                        response.insert(QStringLiteral("editorVisibleWidth"),
                                        status.value(QStringLiteral("editorVisibleWidth")));
                        response.insert(QStringLiteral("historyPanelOpen"),
                                        status.value(QStringLiteral("historyPanelOpen")));
                        m_controller.sendResponse(guardedSocket, response, startedNs, requestId);
                    });
            }, Qt::SingleShotConnection);
    m_window->update();
}

void EditorController::TestSupport::captureStatusPanelBorderSample(QLocalSocket *socket,
                                                      QJsonObject response,
                                                      qint64 startedNs,
                                                      const QString &requestId)
{
    QQuickItem *panel = m_window
        ? m_window->findChild<QQuickItem *>(QStringLiteral("statusPanel")) : nullptr;
    if (!m_window || !panel) {
        m_controller.sendError(socket, response.value(QStringLiteral("command")).toString(),
                  QStringLiteral("status panel unavailable"), startedNs, requestId);
        return;
    }

    m_window->setProperty("statusPanelOpen", true);
    const QRectF sceneRect(panel->mapToScene(QPointF(0, 0)),
                           panel->mapToScene(QPointF(panel->width(), panel->height())));
    QPointer<QLocalSocket> guardedSocket(socket);
    const int settleMs = m_window->property("transitionDuration").toInt() + 30;
    QTimer::singleShot(settleMs, this,
        [this, guardedSocket, response, sceneRect, startedNs, requestId]() mutable {
            if (!m_window) {
                m_controller.sendError(guardedSocket,
                          response.value(QStringLiteral("command")).toString(),
                          QStringLiteral("window unavailable"),
                          startedNs, requestId);
                return;
            }
            const QImage image = m_window->grabWindow();
            m_window->setProperty("statusPanelOpen", false);
            if (image.isNull() || m_window->width() <= 0 || m_window->height() <= 0) {
                m_controller.sendError(guardedSocket,
                          response.value(QStringLiteral("command")).toString(),
                          QStringLiteral("window capture failed"),
                          startedNs, requestId);
                return;
            }

            const qreal scaleX = static_cast<qreal>(image.width()) / m_window->width();
            const qreal scaleY = static_cast<qreal>(image.height()) / m_window->height();
            const int sampleY = qBound(
                0, qRound(sceneRect.center().y() * scaleY), image.height() - 1);
            const int leftCenter = qRound(sceneRect.left() * scaleX);
            const int rightCenter = qRound(sceneRect.right() * scaleX);
            const QColor panelColor(m_window->property("themePanelColor").toString());
            const int panelIntensity =
                (panelColor.red() + panelColor.green() + panelColor.blue()) / 3;
            const auto edgeStrength = [&](int center) {
                int strength = 0;
                QJsonArray pixels;
                for (int x = center - 2; x <= center + 2; ++x) {
                    const int boundedX = qBound(0, x, image.width() - 1);
                    const QColor pixel = image.pixelColor(boundedX, sampleY);
                    const int intensity =
                        (pixel.red() + pixel.green() + pixel.blue()) / 3;
                    strength += qMax(0, intensity - panelIntensity);
                    pixels.append(pixel.name(QColor::HexRgb));
                }
                return qMakePair(strength, pixels);
            };
            const auto left = edgeStrength(leftCenter);
            const auto right = edgeStrength(rightCenter);
            response.insert(QStringLiteral("leftBorderStrength"), left.first);
            response.insert(QStringLiteral("rightBorderStrength"), right.first);
            response.insert(QStringLiteral("leftBorderPixels"), left.second);
            response.insert(QStringLiteral("rightBorderPixels"), right.second);
            response.insert(QStringLiteral("sampleY"), sampleY);
            response.insert(QStringLiteral("devicePixelRatio"), scaleX);
            m_controller.sendResponse(guardedSocket, response, startedNs, requestId);
        });
}

void EditorController::TestSupport::runLargeDocumentBenchmark(QLocalSocket *socket, qint64 startedNs,
                                                 const QString &requestId)
{
    if (!m_controller.isVisible()) {
        m_controller.sendError(socket, QStringLiteral("benchmarkLargeDocument"),
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
    if (m_commands) {
        m_commands->resetHeadingFolds();
    }
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
    m_controller.waitForNextFrame(socket, response, startedNs, requestId);
}

void EditorController::TestSupport::restoreTestDocument()
{
    if (!m_hasSavedTestText || !m_editor) {
        return;
    }
    if (m_commands) {
        m_commands->resetHeadingFolds();
    }
    m_editor->setProperty("text", m_savedTestText);
    if (m_window) {
        QMetaObject::invokeMethod(m_window, "resetScroll");
    }
    m_savedTestText.clear();
    m_hasSavedTestText = false;
}

void EditorController::TestSupport::runImeBenchmark(QLocalSocket *socket, qint64 startedNs,
                                      const QString &requestId)
{
    if (!m_controller.isVisible()) {
        m_controller.sendError(socket, QStringLiteral("benchmarkIme"), QStringLiteral("editor must be visible"),
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
    m_controller.sendResponse(socket, response, startedNs, requestId);
}

void EditorController::TestSupport::runAnimationBenchmark(QLocalSocket *socket, qint64 startedNs,
                                             const QString &requestId)
{
    if (!m_controller.isVisible()) {
        m_controller.sendError(socket, QStringLiteral("benchmarkAnimation"),
                  QStringLiteral("editor must be visible"), startedNs, requestId);
        return;
    }
    if (m_pendingAnimation.socket) {
        m_controller.sendError(socket, QStringLiteral("benchmarkAnimation"),
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
        m_controller.sendError(pending.socket, QStringLiteral("benchmarkAnimation"),
                  QStringLiteral("could not start QML animation"), pending.startedNs,
                  pending.requestId);
    }
}

void EditorController::TestSupport::animationBenchmarkFinished()
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
    m_controller.sendResponse(pending.socket, response, pending.startedNs, pending.requestId);
}

void EditorController::TestSupport::appendDiagnostics(QJsonObject &status) const
{
    status.insert(QStringLiteral("testSelfWriteSequence"),
                  static_cast<qint64>(m_clipboardHistory
                      ? m_clipboardHistory->selfWriteSequence() : 0));
    if (!m_window) {
        return;
    }

    status.insert(QStringLiteral("findQueryText"),
                  m_window->property("findQueryText").toString());
    status.insert(QStringLiteral("findPreviousButtonColor"),
                  m_window->property("findPreviousButtonColor").toString());
    status.insert(QStringLiteral("findNextButtonColor"),
                  m_window->property("findNextButtonColor").toString());
    status.insert(QStringLiteral("findReplaceAllButtonColor"),
                  m_window->property("findReplaceAllButtonColor").toString());
    status.insert(QStringLiteral("findCaseSensitiveButtonColor"),
                  m_window->property("findCaseSensitiveButtonColor").toString());
    status.insert(QStringLiteral("findPanelX"),
                  m_window->property("findPanelX").toDouble());
    status.insert(QStringLiteral("findPanelY"),
                  m_window->property("findPanelY").toDouble());
    status.insert(QStringLiteral("findPanelWidth"),
                  m_window->property("findPanelWidth").toDouble());
    status.insert(QStringLiteral("settingsPageContentHeight"),
                  m_window->property("settingsPageContentHeight").toDouble());
    status.insert(QStringLiteral("settingsPageLastControlBottom"),
                  m_window->property("settingsPageLastControlBottom").toDouble());
    status.insert(QStringLiteral("settingsPageConfigCaptionY"),
                  m_window->property("settingsPageConfigCaptionY").toDouble());
    status.insert(QStringLiteral("historyLayoutProgress"),
                  m_window->property("historyLayoutProgress").toDouble());
    if (QQuickItem *historyPanel =
            m_window->findChild<QQuickItem *>(QStringLiteral("clipboardHistoryPanel"))) {
        status.insert(QStringLiteral("historyPanelTopLeftRadius"),
                      historyPanel->property("topLeftRadius").toDouble());
        status.insert(QStringLiteral("historyPanelTopRightRadius"),
                      historyPanel->property("topRightRadius").toDouble());
        status.insert(QStringLiteral("historyPanelBottomLeftRadius"),
                      historyPanel->property("bottomLeftRadius").toDouble());
        status.insert(QStringLiteral("historyPanelBottomRightRadius"),
                      historyPanel->property("bottomRightRadius").toDouble());
    }
    const auto insertItemGeometry = [this, &status](const QString &objectName,
                                                    const QString &statusPrefix) {
        if (QQuickItem *item = m_window->findChild<QQuickItem *>(objectName)) {
            const QPointF topLeft = item->mapToScene(QPointF(0, 0));
            status.insert(statusPrefix + QStringLiteral("X"), topLeft.x());
            status.insert(statusPrefix + QStringLiteral("Y"), topLeft.y());
            status.insert(statusPrefix + QStringLiteral("Width"), item->width());
            status.insert(statusPrefix + QStringLiteral("Height"), item->height());
        }
    };
    insertItemGeometry(QStringLiteral("headerTitle"),
                       QStringLiteral("headerTitle"));
    insertItemGeometry(QStringLiteral("historyTitle"),
                       QStringLiteral("historyTitle"));
    insertItemGeometry(QStringLiteral("historySearchFrame"),
                       QStringLiteral("historySearchFrame"));
    insertItemGeometry(QStringLiteral("historyList"),
                       QStringLiteral("historyList"));
    insertItemGeometry(QStringLiteral("historyDeleteButton"),
                       QStringLiteral("historyDeleteButton"));
    status.insert(QStringLiteral("historyDeleteButtonColor"),
                  m_window->property("historyDeleteButtonColor").toString());
    insertItemGeometry(QStringLiteral("editorViewport"),
                       QStringLiteral("editorViewport"));
    insertItemGeometry(QStringLiteral("fileDropArea"),
                       QStringLiteral("fileDropArea"));
    if (QQuickItem *statusPanel =
            m_window->findChild<QQuickItem *>(QStringLiteral("statusPanel"))) {
        status.insert(QStringLiteral("statusPanelRootLevel"),
                      statusPanel->parentItem() == m_window->contentItem());
        status.insert(QStringLiteral("statusPanelZ"), statusPanel->z());
    }
    // 预览摘要可能包含用户剪贴板内容，只允许隔离测试 IPC 读取。
    status.insert(QStringLiteral("historyDragPreviewVisible"),
                  m_window->property("historyDragPreviewVisible").toBool());
    status.insert(QStringLiteral("historyDragPreviewText"),
                  m_window->property("historyDragPreviewText").toString());
    status.insert(QStringLiteral("historyDragCursorOverridden"),
                  m_commands && m_commands->externalTextDragCursorOverridden());
    status.insert(QStringLiteral("historyDragCursorShape"),
                  m_commands ? m_commands->externalTextDragCursorShape()
                             : static_cast<int>(Qt::ArrowCursor));
    if (QObject *preview = m_window->findChild<QObject *>(
            QStringLiteral("historyDragPreview"))) {
        status.insert(QStringLiteral("historyDragPreviewX"),
                      preview->property("x").toDouble());
        status.insert(QStringLiteral("historyDragPreviewY"),
                      preview->property("y").toDouble());
        status.insert(QStringLiteral("historyDragPreviewWidth"),
                      preview->property("width").toDouble());
        status.insert(QStringLiteral("historyDragPreviewHeight"),
                      preview->property("height").toDouble());
        status.insert(QStringLiteral("historyDragPreviewOpacity"),
                      preview->property("opacity").toDouble());
    }
}

void EditorController::TestSupport::captureFirstFrame()
{
    if (m_firstFrameCaptured || !m_window) {
        return;
    }
    const QImage image = m_window->grabWindow();
    if (!image.isNull()) {
        const QColor pixel = image.pixelColor(qBound(0, 4, image.width() - 1),
                                              qBound(0, 4, image.height() - 1));
        m_firstFrameColor = pixel.name(QColor::HexRgb);
    }
    m_firstFrameCaptured = true;
}

void EditorController::TestSupport::observeKeyPress(QKeyEvent *event)
{
    if (!m_pendingInput.socket || event->isAutoRepeat()) {
        return;
    }
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
                    m_controller.sendResponse(guardedSocket, response, pending.startedNs,
                                 pending.requestId);
                }, Qt::QueuedConnection);
            }, Qt::ConnectionType(Qt::DirectConnection | Qt::SingleShotConnection));
}
