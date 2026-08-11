#include "clipboardgateway.h"
#include "clipboardhistorycommandgate.h"
#include "clipboardhistorycoordinator.h"
#include "clipboardhistorymodel.h"
#include "clipboardhistorystore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

namespace {
int failures = 0;

template<typename Actual, typename Expected>
void checkEqual(const char *name, const Actual &actual, const Expected &expected)
{
    if (actual == expected) {
        std::fprintf(stdout, "PASS %s\n", name);
        return;
    }
    QString detail;
    QDebug(&detail).noquote() << "actual=" << actual << "expected=" << expected;
    std::fprintf(stderr, "FAIL %s %s\n", name, detail.toUtf8().constData());
    ++failures;
}

void checkTrue(const char *name, bool actual)
{
    checkEqual(name, actual, true);
}

template<typename Predicate>
bool waitUntil(Predicate predicate, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return predicate();
}

void commandGateTests()
{
    for (const QString &command : clipboardHistoryTestCommands()) {
        const QByteArray productionName = QStringLiteral("production rejects %1")
                                              .arg(command).toUtf8();
        checkEqual(productionName.constData(),
                   clipboardHistoryTestCommandAllowed(command, false), false);
        const QByteArray testName = QStringLiteral("test mode accepts %1")
                                        .arg(command).toUtf8();
        checkEqual(testName.constData(),
                   clipboardHistoryTestCommandAllowed(command, true), true);
    }
    checkEqual("unrelated command is not history test command",
               isClipboardHistoryTestCommand(QStringLiteral("show")), false);
}

void gatewayTests()
{
    constexpr auto backendEnvironment = "SCRATCHEDITOR_TEST_CLIPBOARD_BACKEND";
    const bool backendWasSet = qEnvironmentVariableIsSet(backendEnvironment);
    const QByteArray originalBackend = qgetenv(backendEnvironment);
    qunsetenv(backendEnvironment);

    auto gateway = ClipboardGateway::create(true);
    QString error;
    checkTrue("memory monitoring starts", gateway->startMonitoring(0, &error));
    checkEqual("memory backend", gateway->backendName(), QStringLiteral("memory"));
    checkEqual("memory native attempts", gateway->nativeAccessAttempts(), quint64(0));
    gateway->setTestClipboardText(QStringLiteral("virtual"));
    checkEqual("set does not create candidate",
               int(gateway->readHistoryCandidate(nullptr).kind),
               int(ClipboardCaptureCandidate::Kind::ReadFailure));
    ClipboardCaptureCandidate candidate;
    candidate.kind = ClipboardCaptureCandidate::Kind::Text;
    candidate.text = QStringLiteral("injected");
    candidate.sequenceNumber = 42;
    candidate.capturedAtUtcMs = 1234;
    candidate.excludeFromMonitor = true;
    candidate.includeInHistory = ClipboardCaptureCandidate::IncludeInHistory::Malformed;
    const auto injected = gateway->injectTestChange(candidate);
    checkEqual("injected sequence", injected.sequenceNumber, quint32(42));
    checkEqual("injected text", gateway->testClipboardText(), QStringLiteral("injected"));
    checkEqual("injected monitor exclusion", injected.excludeFromMonitor, true);
    checkEqual("injected malformed history marker", int(injected.includeInHistory),
               int(ClipboardCaptureCandidate::IncludeInHistory::Malformed));
    checkTrue("memory write", gateway->writeText(QStringLiteral("written"), &error));
    checkTrue("memory delivery", gateway->deliverText(0, &error));
    checkEqual("delivery sink", gateway->testDeliveredText(), QStringLiteral("written"));
    gateway->stopMonitoring();
    checkEqual("monitoring stopped", gateway->monitoring(), false);
    gateway->setTestFault(QStringLiteral("listenerRegistration"), true);
    checkEqual("listener registration fault", gateway->startMonitoring(0, &error), false);
    gateway->setTestFault(QStringLiteral("listenerRegistration"), false);
    checkTrue("listener registration recovers", gateway->startMonitoring(0, &error));
    gateway->setTestFault(QStringLiteral("sequenceRace"), true);
    const quint32 firstSequence = gateway->sequenceNumber();
    checkTrue("sequence race changes sequence", gateway->sequenceNumber() != firstSequence);

    qputenv(backendEnvironment, QByteArrayLiteral("native"));
    const auto nativeTestGateway = ClipboardGateway::create(true);
#ifdef Q_OS_WIN
    checkEqual("explicit native test backend", nativeTestGateway->backendName(),
               QStringLiteral("win32"));
#else
    checkEqual("explicit native test backend fallback", nativeTestGateway->backendName(),
               QStringLiteral("memory"));
#endif
    if (backendWasSet) {
        qputenv(backendEnvironment, originalBackend);
    } else {
        qunsetenv(backendEnvironment);
    }
}

void decoderTests()
{
    QString text;
    QString error;
    const QByteArray valid("A\0B\0\0\0", 6);
    checkTrue("utf16 decoder valid",
              ClipboardGateway::decodeUnicodeTextBuffer(valid, &text, &error));
    checkEqual("utf16 decoder text", text, QStringLiteral("AB"));
    checkEqual("utf16 decoder odd",
               ClipboardGateway::decodeUnicodeTextBuffer(QByteArray("A", 1), &text, &error),
               false);
    checkEqual("utf16 decoder no terminator",
               ClipboardGateway::decodeUnicodeTextBuffer(QByteArray("A\0", 2), &text, &error),
               false);
    QByteArray oversizeUtf16((ClipboardHistoryModel::MaximumItemUtf8Bytes + 1) * 2 + 2, 0);
    for (qsizetype offset = 0; offset < oversizeUtf16.size() - 2; offset += 2) {
        oversizeUtf16[offset] = 'a';
    }
    checkEqual("utf16 decoder rejects over 1 MiB",
               ClipboardGateway::decodeUnicodeTextBuffer(oversizeUtf16, &text, &error), false);
}

void modelTests()
{
    ClipboardHistoryModel model;
    checkEqual("empty ignored", int(model.capture({}, 1)),
               int(ClipboardHistoryModel::CaptureOutcome::Empty));
    checkEqual("unicode inserted", int(model.capture(QStringLiteral("你好😀"), 10)),
               int(ClipboardHistoryModel::CaptureOutcome::Inserted));
    const QString stableId = model.items().first().id;
    checkEqual("utf16 character count", model.items().first().characterCount(), qsizetype(4));
    model.capture(QStringLiteral("second"), 20);
    model.capture(QStringLiteral("你好😀"), 30);
    checkEqual("duplicate count", model.items().size(), qsizetype(2));
    checkEqual("duplicate stable id", model.items().first().id, stableId);
    model.capture(QStringLiteral(" whitespace "), 40);
    model.capture(QStringLiteral("whitespace"), 50);
    checkEqual("whitespace remains distinct", model.items().size(), qsizetype(4));
    const qsizetype beforeExactVariants = model.items().size();
    model.capture(QStringLiteral("line"), 51);
    model.capture(QStringLiteral("line\n"), 52);
    model.capture(QString::fromUtf8("\xC3\xA9"), 53);
    model.capture(QStringLiteral("e\u0301"), 54);
    checkEqual("newline and Unicode normalization remain exact",
               model.items().size(), beforeExactVariants + 4);
    model.setFilter(QStringLiteral("SECOND"));
    checkEqual("case insensitive filter", model.rowCount(), 1);
    model.setFilter({});
    for (int i = 0; i < 105; ++i) model.capture(QString::number(i), 100 + i);
    checkEqual("capacity", model.items().size(), ClipboardHistoryModel::MaximumItems);
    checkEqual("full text not exposed as role", model.roleNames().values().contains("text"), false);

    ClipboardHistoryModel boundaryModel;
    const QString exactMiB(ClipboardHistoryModel::MaximumItemUtf8Bytes, QLatin1Char('a'));
    checkEqual("exact 1 MiB accepted", int(boundaryModel.capture(exactMiB, 1)),
               int(ClipboardHistoryModel::CaptureOutcome::Inserted));
    checkEqual("over 1 MiB rejected",
               int(boundaryModel.capture(exactMiB + QLatin1Char('b'), 2)),
               int(ClipboardHistoryModel::CaptureOutcome::Oversize));
    boundaryModel.capture(QStringLiteral("selection-a"), 3);
    boundaryModel.capture(QStringLiteral("selection-b"), 4);
    boundaryModel.setSelectedId(boundaryModel.items().first().id);
    const quint64 beforeMissingDelete = boundaryModel.revision();
    checkEqual("missing delete has no effect", boundaryModel.deleteById(QStringLiteral("missing")), false);
    checkEqual("missing delete revision stable", boundaryModel.revision(), beforeMissingDelete);
    checkTrue("delete by stable id", boundaryModel.deleteById(boundaryModel.selectedId()));
    checkTrue("selection falls back", !boundaryModel.selectedId().isEmpty());
    checkTrue("clear history", boundaryModel.clearHistory());
    checkEqual("clear selection", boundaryModel.selectedId(), QString());
    const quint64 emptyRevision = boundaryModel.revision();
    checkEqual("empty clear has no effect", boundaryModel.clearHistory(), false);
    checkTrue("authorized empty clear advances revision", boundaryModel.clearHistory(true));
    checkEqual("authorized empty clear revision", boundaryModel.revision(), emptyRevision + 1);

    ClipboardHistoryModel mergingModel;
    const auto sessionItem = mergingModel.capture(QStringLiteral("session-newer"), 200);
    ClipboardHistoryItem persistedItem;
    persistedItem.id = QStringLiteral("persisted-id");
    persistedItem.text = QStringLiteral("persisted-older");
    persistedItem.capturedAtUtcMs = 100;
    mergingModel.mergePersisted({persistedItem}, 10);
    checkEqual("session capture accepted before async load",
               int(sessionItem), int(ClipboardHistoryModel::CaptureOutcome::Inserted));
    checkEqual("async merge keeps session first", mergingModel.items().at(0).text,
               QStringLiteral("session-newer"));
    checkEqual("async merge appends persisted item", mergingModel.items().at(1).id,
               QStringLiteral("persisted-id"));
    checkEqual("async merge advances persisted revision", mergingModel.revision(), quint64(11));

    ClipboardHistoryModel filterModel;
    filterModel.capture(QStringLiteral("alpha old"), 1, QStringLiteral("alpha-id"));
    filterModel.capture(QStringLiteral("beta ALPHA new"), 2, QStringLiteral("beta-id"));
    filterModel.capture(QStringLiteral("gamma"), 3, QStringLiteral("gamma-id"));
    const QVector<ClipboardHistoryItem> persistentOrder = filterModel.items();
    filterModel.setSelectedId(QStringLiteral("alpha-id"));
    filterModel.setFilter(QStringLiteral("ALPHA"));
    checkEqual("filter newest match first", filterModel.visibleIds(),
               QVector<QString>({QStringLiteral("beta-id"), QStringLiteral("alpha-id")}));
    checkEqual("query preserves persistent order", filterModel.items().first().id,
               persistentOrder.first().id);
    filterModel.capture(QStringLiteral("alpha old"), 4);
    checkEqual("duplicate reorder keeps stable selection", filterModel.selectedId(),
               QStringLiteral("alpha-id"));
    checkEqual("duplicate reorder moves stable id first", filterModel.visibleIds().first(),
               QStringLiteral("alpha-id"));
    filterModel.setFilter(QStringLiteral("gamma"));
    checkEqual("filtered-out selection falls back", filterModel.selectedId(),
               QStringLiteral("gamma-id"));
    checkTrue("selected visible item deletes", filterModel.deleteById(filterModel.selectedId()));
    checkEqual("empty result clears selection", filterModel.selectedId(), QString());
}

void storeTests()
{
    ClipboardHistorySnapshot input;
    input.revision = 7;
    input.items.append({QStringLiteral("id-1"), QStringLiteral("secret-你好"), 42});
    QString error;
    const QByteArray envelope = ClipboardHistoryStore::encodeEnvelope(input, &error);
    checkTrue("encode envelope", !envelope.isEmpty());
    ClipboardHistorySnapshot decoded;
    checkTrue("decode envelope", ClipboardHistoryStore::decodeEnvelope(envelope, &decoded, &error));
    checkEqual("codec revision", decoded.revision, quint64(7));
    checkEqual("codec text", decoded.items.first().text, input.items.first().text);
    QByteArray corrupt = envelope;
    corrupt[corrupt.size() / 2] ^= 1;
    checkEqual("corrupt envelope rejected",
               ClipboardHistoryStore::decodeEnvelope(corrupt, &decoded, &error), false);
    checkEqual("trailing envelope bytes rejected",
               ClipboardHistoryStore::decodeEnvelope(envelope + QByteArray(1, '\0'),
                                                     &decoded, &error), false);
    QByteArray declaredOverflow = envelope;
    for (int index = 6; index < 14; ++index) {
        declaredOverflow[index] = char(0xff);
    }
    checkEqual("declared payload overflow rejected",
               ClipboardHistoryStore::decodeEnvelope(declaredOverflow, &decoded, &error), false);
    ClipboardHistorySnapshot duplicateId = input;
    duplicateId.items.append({QStringLiteral("id-1"), QStringLiteral("other"), 43});
    checkEqual("duplicate stable id rejected",
               ClipboardHistoryStore::encodeEnvelope(duplicateId, &error).isEmpty(), true);
    ClipboardHistorySnapshot duplicateText = input;
    duplicateText.items.append({QStringLiteral("id-2"), QStringLiteral("secret-你好"), 43});
    checkEqual("duplicate persisted text rejected",
               ClipboardHistoryStore::encodeEnvelope(duplicateText, &error).isEmpty(), true);

    QTemporaryDir directory;
    checkTrue("temporary directory", directory.isValid());
    const QString path = directory.filePath(QStringLiteral("clipboard-history.dat"));
    ClipboardHistoryStore store(path);
    store.save(input);
    checkTrue("store flush", store.waitForIdle(10000));
    QFile file(path);
    checkTrue("cipher file exists", file.open(QIODevice::ReadOnly));
    const QByteArray cipher = file.readAll();
    file.close();
    checkEqual("cipher hides plaintext", cipher.contains("secret-"), false);
    ClipboardHistorySnapshot loaded;
    checkTrue("store load", store.load(&loaded, &error));
    checkEqual("store roundtrip", loaded.items.first().text, input.items.first().text);

    ClipboardHistorySnapshot faultSnapshot;
    ClipboardHistoryStore readFaultStore(path);
    readFaultStore.setFault(QStringLiteral("read"), true);
    checkEqual("read fault locks store", readFaultStore.load(&faultSnapshot, &error), false);
    checkEqual("read fault state", int(readFaultStore.state()),
               int(ClipboardHistoryStore::State::ReadLocked));
    ClipboardHistoryStore decryptFaultStore(path);
    decryptFaultStore.setFault(QStringLiteral("decrypt"), true);
    checkEqual("decrypt fault locks store", decryptFaultStore.load(&faultSnapshot, &error), false);
    checkEqual("decrypt fault state", int(decryptFaultStore.state()),
               int(ClipboardHistoryStore::State::ReadLocked));

    const QString encryptFaultPath = directory.filePath(QStringLiteral("encrypt-fault.dat"));
    checkTrue("encrypt fault fixture copied", QFile::copy(path, encryptFaultPath));
    ClipboardHistoryStore encryptFaultStore(encryptFaultPath);
    checkTrue("encrypt fault fixture loads", encryptFaultStore.load(&faultSnapshot, &error));
    encryptFaultStore.setFault(QStringLiteral("encrypt"), true);
    ClipboardHistorySnapshot encryptReplacement = input;
    encryptReplacement.revision = 8;
    encryptReplacement.items.prepend(
        {QStringLiteral("id-encrypt"), QStringLiteral("encrypt failure"), 44});
    encryptFaultStore.save(encryptReplacement);
    checkTrue("encrypt fault worker reaches idle", encryptFaultStore.waitForIdle(5000));
    checkEqual("encrypt fault state", int(encryptFaultStore.state()),
               int(ClipboardHistoryStore::State::WriteFailed));
    QFile encryptUnchanged(encryptFaultPath);
    checkTrue("encrypt fault last-known-good reopened", encryptUnchanged.open(QIODevice::ReadOnly));
    checkEqual("encrypt fault preserves last-known-good",
               QCryptographicHash::hash(encryptUnchanged.readAll(), QCryptographicHash::Sha256),
               QCryptographicHash::hash(cipher, QCryptographicHash::Sha256));
    encryptUnchanged.close();

    const QString shutdownPath = directory.filePath(QStringLiteral("shutdown-timeout.dat"));
    checkTrue("shutdown timeout fixture copied", QFile::copy(path, shutdownPath));
    ClipboardHistoryStore shutdownStore(shutdownPath);
    checkTrue("shutdown timeout fixture loads", shutdownStore.load(&faultSnapshot, &error));
    shutdownStore.setTestWriteDelayMs(100);
    ClipboardHistorySnapshot shutdownReplacement = input;
    shutdownReplacement.revision = 8;
    shutdownReplacement.items.prepend(
        {QStringLiteral("id-shutdown"), QStringLiteral("must not commit"), 45});
    shutdownStore.save(shutdownReplacement);
    QElapsedTimer shutdownTimer;
    shutdownTimer.start();
    checkEqual("shutdown timeout abandons pending revision",
               shutdownStore.flushForShutdown(5), false);
    checkTrue("shutdown timeout obeys controlled deadline", shutdownTimer.elapsed() < 80);
    checkEqual("production shutdown deadline constant",
               ClipboardHistoryStore::ShutdownWaitTimeoutMs, 10000);
    checkTrue("abandoned worker reaches idle", shutdownStore.waitForIdle(500));
    QFile shutdownUnchanged(shutdownPath);
    checkTrue("shutdown last-known-good reopened", shutdownUnchanged.open(QIODevice::ReadOnly));
    checkEqual("shutdown timeout preserves last-known-good",
               QCryptographicHash::hash(shutdownUnchanged.readAll(), QCryptographicHash::Sha256),
               QCryptographicHash::hash(cipher, QCryptographicHash::Sha256));
    shutdownUnchanged.close();

    const QByteArray lastKnownGood = QCryptographicHash::hash(cipher, QCryptographicHash::Sha256);
    ClipboardHistorySnapshot replacement = input;
    replacement.revision = 8;
    replacement.items.prepend({QStringLiteral("id-2"), QStringLiteral("new secret"), 43});
    store.setFault(QStringLiteral("write"), true);
    store.save(replacement);
    checkTrue("write fault reaches idle", store.waitForIdle(10000));
    QFile unchanged(path);
    checkTrue("last-known-good reopened", unchanged.open(QIODevice::ReadOnly));
    checkEqual("write fault preserves last-known-good",
               QCryptographicHash::hash(unchanged.readAll(), QCryptographicHash::Sha256),
               lastKnownGood);
    unchanged.close();
    checkEqual("write fault state", int(store.state()),
               int(ClipboardHistoryStore::State::WriteFailed));

    QByteArray corruptedCipher = cipher;
    corruptedCipher[corruptedCipher.size() / 2] ^= 1;
    QFile corruptFile(path);
    checkTrue("corrupt fixture open", corruptFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    checkEqual("corrupt fixture write", corruptFile.write(corruptedCipher), qint64(corruptedCipher.size()));
    corruptFile.close();
    ClipboardHistoryStore lockedStore(path);
    ClipboardHistorySnapshot rejected;
    checkEqual("corrupt ciphertext rejected", lockedStore.load(&rejected, &error), false);
    checkEqual("corrupt ciphertext read locked", int(lockedStore.state()),
               int(ClipboardHistoryStore::State::ReadLocked));
    checkTrue("explicit reset unlocks corrupt store", lockedStore.resetUnreadableStore(&error));
    ClipboardHistorySnapshot emptyReset;
    emptyReset.revision = 1;
    lockedStore.save(emptyReset);
    checkTrue("explicit empty reset persisted", lockedStore.waitForIdle(5000));
    checkEqual("explicit empty reset state", int(lockedStore.state()),
               int(ClipboardHistoryStore::State::Ready));
    checkEqual("explicit empty reset error", lockedStore.error(), QString());
    ClipboardHistoryStore resetStore(path);
    ClipboardHistorySnapshot resetSnapshot;
    checkTrue("reset store reloads", resetStore.load(&resetSnapshot, &error));
    checkEqual("reset store is empty", resetSnapshot.items.size(), 0);
    checkEqual("reset store revision", resetSnapshot.revision, quint64(1));
}

void coordinatorTests()
{
    QTemporaryDir directory;
    checkTrue("coordinator temporary directory", directory.isValid());

    const QString asyncPath = directory.filePath(QStringLiteral("async-load.dat"));
    ClipboardHistorySnapshot persisted;
    persisted.revision = 7;
    persisted.items.append(
        {QStringLiteral("async-id"), QStringLiteral("async-loaded"), 100});
    {
        ClipboardHistoryStore fixture(asyncPath);
        fixture.save(persisted);
        checkTrue("coordinator async fixture persisted", fixture.waitForIdle(5000));
    }
    ClipboardHistoryCoordinator asyncCoordinator(true, true, asyncPath);
    checkTrue("coordinator async load completes", waitUntil([&] {
        return asyncCoordinator.storeStateName() == QStringLiteral("Ready")
            && asyncCoordinator.revision() >= persisted.revision;
    }));
    checkEqual("coordinator async load item count", asyncCoordinator.items().size(),
               qsizetype(1));
    checkEqual("coordinator async load text", asyncCoordinator.items().first().text,
               QStringLiteral("async-loaded"));

    const QString echoPath = directory.filePath(QStringLiteral("echo.dat"));
    ClipboardHistoryCoordinator echoCoordinator(true, true, echoPath);
    checkTrue("coordinator echo store ready", waitUntil([&] {
        return echoCoordinator.storeStateName() == QStringLiteral("Ready");
    }));
    QString error;
    checkTrue("coordinator write captures", echoCoordinator.writeText(
                  QStringLiteral("self-write"), &error));
    const quint64 beforeEchoRevision = echoCoordinator.revision();
    ClipboardCaptureCandidate echo;
    echo.kind = ClipboardCaptureCandidate::Kind::Text;
    echo.text = QStringLiteral("self-write");
    echo.sequenceNumber = echoCoordinator.selfWriteSequence();
    echo.capturedAtUtcMs = 200;
    echo.includeInHistory = ClipboardCaptureCandidate::IncludeInHistory::Allow;
    echo = echoCoordinator.injectTestChange(echo);
    checkEqual("coordinator self write echo suppressed",
               int(echoCoordinator.captureTestCandidate(echo)),
               int(ClipboardHistoryCoordinator::CaptureOutcome::SelfWriteNotification));
    checkEqual("coordinator self write revision stable", echoCoordinator.revision(),
               beforeEchoRevision);
    checkEqual("coordinator echo suppression consumed once",
               int(echoCoordinator.captureTestCandidate(echo)),
               int(ClipboardHistoryCoordinator::CaptureOutcome::DuplicateRefreshed));

    const QString priorityPath = directory.filePath(QStringLiteral("priority.dat"));
    ClipboardHistoryCoordinator priorityCoordinator(true, true, priorityPath);
    checkTrue("coordinator priority store ready", waitUntil([&] {
        return priorityCoordinator.storeStateName() == QStringLiteral("Ready");
    }));
    priorityCoordinator.setTestFault(QStringLiteral("listenerRegistration"), true);
    checkEqual("coordinator monitor fault restart", priorityCoordinator.restartMonitoring(0),
               false);
    const QString monitorError = priorityCoordinator.error();
    checkTrue("coordinator monitor fault visible", !monitorError.isEmpty());
    priorityCoordinator.setTestFault(QStringLiteral("write"), true);
    ClipboardCaptureCandidate storeFailure;
    storeFailure.kind = ClipboardCaptureCandidate::Kind::Text;
    storeFailure.text = QStringLiteral("write-fault");
    storeFailure.capturedAtUtcMs = 300;
    storeFailure.includeInHistory = ClipboardCaptureCandidate::IncludeInHistory::Allow;
    checkEqual("coordinator priority capture accepted",
               int(priorityCoordinator.captureTestCandidate(storeFailure)),
               int(ClipboardHistoryCoordinator::CaptureOutcome::Inserted));
    checkTrue("coordinator priority write settles", priorityCoordinator.waitForIdle(5000));
    checkTrue("coordinator priority state delivered", waitUntil([&] {
        return priorityCoordinator.storeStateName() == QStringLiteral("WriteFailed");
    }));
    checkEqual("coordinator monitor error has priority", priorityCoordinator.error(),
               monitorError);
    priorityCoordinator.setTestFault(QStringLiteral("listenerRegistration"), false);
    checkTrue("coordinator monitor fault recovers", priorityCoordinator.restartMonitoring(0));
    checkTrue("coordinator store error revealed", !priorityCoordinator.error().isEmpty()
              && priorityCoordinator.error() != monitorError);

    const QString lockedPath = directory.filePath(QStringLiteral("locked.dat"));
    {
        QFile corrupt(lockedPath);
        checkTrue("coordinator locked fixture opens",
                  corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
        checkEqual("coordinator locked fixture writes", corrupt.write("corrupt", 7), qint64(7));
    }
    {
        ClipboardHistoryCoordinator lockedCoordinator(true, true, lockedPath);
        checkTrue("coordinator read locked arrives", waitUntil([&] {
            return lockedCoordinator.storeStateName() == QStringLiteral("ReadLocked");
        }));
        checkTrue("coordinator read locked can clear", lockedCoordinator.canClear());
        checkTrue("coordinator read locked clear", lockedCoordinator.clearHistory());
        checkTrue("coordinator read locked clear persists",
                  lockedCoordinator.waitForIdle(5000));
        checkTrue("coordinator read locked shutdown", lockedCoordinator.shutdown());
    }
    ClipboardHistoryStore clearedStore(lockedPath);
    ClipboardHistorySnapshot cleared;
    checkTrue("coordinator cleared store reloads", clearedStore.load(&cleared, &error));
    checkEqual("coordinator cleared store item count", cleared.items.size(), qsizetype(0));
    checkEqual("coordinator cleared store revision", cleared.revision, quint64(1));

    const QString shutdownPath = directory.filePath(QStringLiteral("coordinator-shutdown.dat"));
    {
        ClipboardHistoryCoordinator shutdownCoordinator(true, true, shutdownPath);
        checkTrue("coordinator shutdown store ready", waitUntil([&] {
            return shutdownCoordinator.storeStateName() == QStringLiteral("Ready");
        }));
        checkTrue("coordinator shutdown write accepted", shutdownCoordinator.writeText(
                      QStringLiteral("shutdown-flush"), &error));
        checkTrue("coordinator shutdown flushes", shutdownCoordinator.shutdown());
    }
    ClipboardHistoryStore shutdownStore(shutdownPath);
    ClipboardHistorySnapshot shutdownSnapshot;
    checkTrue("coordinator shutdown store reloads",
              shutdownStore.load(&shutdownSnapshot, &error));
    checkEqual("coordinator shutdown persisted text", shutdownSnapshot.items.first().text,
               QStringLiteral("shutdown-flush"));
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    commandGateTests();
    gatewayTests();
    decoderTests();
    modelTests();
    storeTests();
    coordinatorTests();
    return failures == 0 ? 0 : 1;
}
