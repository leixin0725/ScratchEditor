#include "suites.h"
#include "support.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <cmath>

namespace EditingTests {

QString checkFileDropAndDrag(QJsonObject &checks, QJsonObject &details, const QTemporaryDir &droppedItems)
{
    const QString plainFilePath = droppedItems.filePath(QStringLiteral("plain.txt"));
    const QString spacedFilePath =
        droppedItems.filePath(QStringLiteral("含 空格😀.txt"));
    const QString directoryPath = droppedItems.filePath(QStringLiteral("folder"));
    QFile plainFile(plainFilePath);
    const bool plainFileCreated = plainFile.open(QIODevice::WriteOnly);
    plainFile.close();
    QFile spacedFile(spacedFilePath);
    const bool spacedFileCreated = spacedFile.open(QIODevice::WriteOnly);
    spacedFile.close();
    const bool directoryCreated = QDir().mkpath(directoryPath);
    const QString plainUrl = QUrl::fromLocalFile(plainFilePath).toString(QUrl::FullyEncoded);
    const QString spacedUrl = QUrl::fromLocalFile(spacedFilePath).toString(QUrl::FullyEncoded);
    const QString directoryUrl = QUrl::fromLocalFile(directoryPath).toString(QUrl::FullyEncoded);
    const QString plainPathText = droppedPathText(plainFilePath);
    const QString spacedPathText = droppedPathText(spacedFilePath);
    const QString directoryPathText = droppedPathText(directoryPath);
    addCheck(checks, details, QStringLiteral("fileDropFixtureReady"),
             droppedItems.isValid() && plainFileCreated && spacedFileCreated
                 && directoryCreated,
             QJsonObject{{QStringLiteral("temporaryDirectory"), droppedItems.path()},
                         {QStringLiteral("plainFileCreated"), plainFileCreated},
                         {QStringLiteral("spacedFileCreated"), spacedFileCreated},
                         {QStringLiteral("directoryCreated"), directoryCreated}});

    setTextAndSelection(QStringLiteral("left-right"), 4, 4);
    const QJsonObject singleFileDrop = dropUrls({plainUrl});
    addCheck(checks, details, QStringLiteral("fileDropSingleFileAtCursor"),
             singleFileDrop.value(QStringLiteral("inserted")).toBool()
                 && singleFileDrop.value(QStringLiteral("text")).toString()
                    == QStringLiteral("left") + plainPathText + QStringLiteral("-right")
                 && singleFileDrop.value(QStringLiteral("cursorPosition")).toInt()
                    == 4 + plainPathText.size(),
             singleFileDrop);
    addCheck(checks, details, QStringLiteral("fileDropDoesNotAddContextSpaces"),
             singleFileDrop.value(QStringLiteral("text")).toString()
                    == QStringLiteral("left") + plainPathText + QStringLiteral("-right"),
             singleFileDrop);
    addCheck(checks, details, QStringLiteral("fileDropPlainPathIsUnquoted"),
             !plainPathText.startsWith(QLatin1Char('"')),
             QJsonObject{{QStringLiteral("path"), plainPathText}});

    setTextAndSelection(QStringLiteral("dir:"), 4, 4);
    const QJsonObject singleDirectoryDrop = dropUrls({directoryUrl});
    addCheck(checks, details, QStringLiteral("fileDropSingleDirectory"),
             singleDirectoryDrop.value(QStringLiteral("inserted")).toBool()
                 && singleDirectoryDrop.value(QStringLiteral("text")).toString()
                    == QStringLiteral("dir:") + directoryPathText,
             singleDirectoryDrop);

    setTextAndSelection(QString(), 0, 0);
    const QJsonObject orderedDuplicateDrop =
        dropUrls({plainUrl, spacedUrl, plainUrl});
    const QString orderedDuplicateText =
        plainPathText + QLatin1Char(' ') + spacedPathText
        + QLatin1Char(' ') + plainPathText;
    addCheck(checks, details, QStringLiteral("fileDropPreservesOrderAndDuplicates"),
             orderedDuplicateDrop.value(QStringLiteral("inserted")).toBool()
                 && orderedDuplicateDrop.value(QStringLiteral("text")).toString()
                    == orderedDuplicateText,
             orderedDuplicateDrop);
    addCheck(checks, details, QStringLiteral("fileDropQuotesWhitespaceAndJoinsWithOneSpace"),
             spacedPathText.startsWith(QLatin1Char('"'))
                 && spacedPathText.endsWith(QLatin1Char('"'))
                 && orderedDuplicateDrop.value(QStringLiteral("text")).toString()
                    == orderedDuplicateText,
             orderedDuplicateDrop);

    const QString utf16Text = QStringLiteral("A😀B");
    setTextAndSelection(utf16Text, 3, 3);
    const QJsonObject utf16Drop = dropUrls({spacedUrl});
    addCheck(checks, details, QStringLiteral("fileDropUsesUtf16CursorPositions"),
             utf16Drop.value(QStringLiteral("text")).toString()
                    == QStringLiteral("A😀") + spacedPathText + QStringLiteral("B")
                 && utf16Drop.value(QStringLiteral("cursorPosition")).toInt()
                    == 3 + spacedPathText.size(),
             utf16Drop);

    setTextAndSelection(QStringLiteral("before TARGET after"), 7, 13);
    const QJsonObject forwardSelectionDrop = dropUrls({plainUrl});
    setTextAndSelection(QStringLiteral("before TARGET after"), 7, 13, 7);
    const QJsonObject reverseSelectionDrop = dropUrls({directoryUrl});
    addCheck(checks, details, QStringLiteral("fileDropReplacesForwardSelection"),
             forwardSelectionDrop.value(QStringLiteral("text")).toString()
                    == QStringLiteral("before ") + plainPathText + QStringLiteral(" after")
                 && forwardSelectionDrop.value(QStringLiteral("selectionStart")).toInt()
                    == 7 + plainPathText.size()
                 && forwardSelectionDrop.value(QStringLiteral("selectionEnd")).toInt()
                    == 7 + plainPathText.size(),
             forwardSelectionDrop);
    addCheck(checks, details, QStringLiteral("fileDropReplacesReverseSelection"),
             reverseSelectionDrop.value(QStringLiteral("text")).toString()
                    == QStringLiteral("before ") + directoryPathText + QStringLiteral(" after")
                 && reverseSelectionDrop.value(QStringLiteral("cursorPosition")).toInt()
                    == 7 + directoryPathText.size()
                 && reverseSelectionDrop.value(QStringLiteral("selectionStart")).toInt()
                    == reverseSelectionDrop.value(QStringLiteral("selectionEnd")).toInt(),
             reverseSelectionDrop);

    setTextAndSelection(QStringLiteral("undo selection"), 5, 14, 5);
    const QJsonObject beforeDropUndo = editorStatus();
    const QJsonObject droppedBeforeUndo = dropUrls({plainUrl, directoryUrl});
    const QJsonObject undoneDrop = request(QStringLiteral("testUndo"));
    addCheck(checks, details, QStringLiteral("fileDropIsOneUndoWithSelectionRestore"),
             droppedBeforeUndo.value(QStringLiteral("inserted")).toBool()
                 && undoneDrop.value(QStringLiteral("invoked")).toBool()
                 && undoneDrop.value(QStringLiteral("text")).toString()
                    == QStringLiteral("undo selection")
                 && undoneDrop.value(QStringLiteral("selectionStart"))
                    == beforeDropUndo.value(QStringLiteral("selectionStart"))
                 && undoneDrop.value(QStringLiteral("selectionEnd"))
                    == beforeDropUndo.value(QStringLiteral("selectionEnd"))
                 && undoneDrop.value(QStringLiteral("cursorPosition"))
                    == beforeDropUndo.value(QStringLiteral("cursorPosition")),
             QJsonObject{{QStringLiteral("before"), beforeDropUndo},
                         {QStringLiteral("dropped"), droppedBeforeUndo},
                         {QStringLiteral("undone"), undoneDrop}});

    const auto invalidDropLeavesState = [&](const QString &name, const QStringList &urls) {
        setTextAndSelection(QStringLiteral("unchanged"), 2, 7, 2);
        const QJsonObject before = editorStatus();
        const QJsonObject result = dropUrls(urls);
        addCheck(checks, details, name,
                 !result.value(QStringLiteral("inserted")).toBool()
                     && result.value(QStringLiteral("text")).toString()
                        == QStringLiteral("unchanged")
                     && result.value(QStringLiteral("selectionStart"))
                        == before.value(QStringLiteral("selectionStart"))
                     && result.value(QStringLiteral("selectionEnd"))
                        == before.value(QStringLiteral("selectionEnd"))
                     && result.value(QStringLiteral("cursorPosition"))
                        == before.value(QStringLiteral("cursorPosition")),
                 result);
    };
    invalidDropLeavesState(QStringLiteral("fileDropRejectsEmptyList"), {});
    invalidDropLeavesState(QStringLiteral("fileDropRejectsNonLocalUrl"),
                           {QStringLiteral("https://example.com/file.txt")});
    const QString missingUrl = QUrl::fromLocalFile(
        droppedItems.filePath(QStringLiteral("missing.txt"))).toString(QUrl::FullyEncoded);
    invalidDropLeavesState(QStringLiteral("fileDropRejectsMissingPath"), {missingUrl});
    invalidDropLeavesState(QStringLiteral("fileDropRejectsMixedValidInvalidAtomically"),
                           {plainUrl, missingUrl});

    const QString orderedBefore = QStringLiteral("1. one\n2. two\n3. three");
    setTextAndSelection(orderedBefore, 6, 13);
    const QJsonObject orderedListDrop = dropUrls({plainUrl});
    addCheck(checks, details, QStringLiteral("fileDropRepairsOrderedList"),
             orderedListDrop.value(QStringLiteral("text")).toString()
                    == QStringLiteral("1. one") + plainPathText
                       + QStringLiteral("\n2. three"),
             orderedListDrop);

    QString longDropDocument;
    for (int line = 0; line < 240; ++line) {
        longDropDocument += QStringLiteral("line %1\n").arg(line);
    }
    setTextAndSelection(longDropDocument, longDropDocument.size(), longDropDocument.size());
    setScrollY(0);
    const QJsonObject longDocumentDrop = dropUrls({plainUrl});
    QThread::msleep(120);
    const QJsonObject longDocumentDropStatus = editorStatus();
    addCheck(checks, details, QStringLiteral("fileDropUsesInputAutoScroll"),
             longDocumentDrop.value(QStringLiteral("inserted")).toBool()
                 && longDocumentDropStatus.value(
                        QStringLiteral("inputScrollLastKind")).toString()
                    == QStringLiteral("drop")
                 && longDocumentDropStatus.value(
                        QStringLiteral("inputScrollDidScroll")).toBool()
                 && longDocumentDropStatus.value(
                        QStringLiteral("scrollContentY")).toDouble() > 0.0,
             longDocumentDropStatus);

    const QString dragText = QStringLiteral("AA<move>BB");
    setTextAndSelection(dragText, 2, 8);
    const QJsonObject movedForward = dragSelection(2, 8, dragText.size());
    const QJsonObject undoneMove = request(QStringLiteral("testUndo"));
    setTextAndSelection(dragText, 2, 8);
    const QJsonObject movedBackward = dragSelection(2, 8, 0);
    setTextAndSelection(dragText, 2, 8);
    const QJsonObject droppedInside = dragSelection(2, 8, 5);
    const QString multilineDragText = QStringLiteral("top\nmiddle\nbottom");
    setTextAndSelection(multilineDragText, 4, 11);
    const QJsonObject movedMultiline = dragSelection(4, 11, 0);
    addCheck(checks, details, QStringLiteral("selectionDragMove"),
             movedForward.value(QStringLiteral("eventsAccepted")).toBool()
                 && movedForward.value(QStringLiteral("moved")).toBool()
                 && movedForward.value(QStringLiteral("text")).toString()
                    == QStringLiteral("AABB<move>")
                 && movedForward.value(QStringLiteral("selectionStart")).toInt() == 4
                 && movedForward.value(QStringLiteral("selectionEnd")).toInt() == 10
                 && undoneMove.value(QStringLiteral("text")).toString() == dragText
                 && movedBackward.value(QStringLiteral("eventsAccepted")).toBool()
                 && movedBackward.value(QStringLiteral("moved")).toBool()
                 && movedBackward.value(QStringLiteral("text")).toString()
                    == QStringLiteral("<move>AABB")
                 && movedBackward.value(QStringLiteral("selectionStart")).toInt() == 0
                 && movedBackward.value(QStringLiteral("selectionEnd")).toInt() == 6
                 && droppedInside.value(QStringLiteral("eventsAccepted")).toBool()
                 && !droppedInside.value(QStringLiteral("moved")).toBool()
                 && droppedInside.value(QStringLiteral("text")).toString() == dragText
                 && movedMultiline.value(QStringLiteral("eventsAccepted")).toBool()
                 && movedMultiline.value(QStringLiteral("moved")).toBool()
                 && movedMultiline.value(QStringLiteral("text")).toString()
                    == QStringLiteral("middle\ntop\nbottom"),
             QJsonObject{{QStringLiteral("forward"), movedForward},
                         {QStringLiteral("undo"), undoneMove},
                         {QStringLiteral("backward"), movedBackward},
                         {QStringLiteral("inside"), droppedInside},
                         {QStringLiteral("multiline"), movedMultiline}});

    QString nativeSelectionScrollText;
    for (int index = 0; index < 80; ++index) {
        nativeSelectionScrollText += QStringLiteral("line-%1\t目录文字 %2\n")
                                         .arg(index, 2, 10, QLatin1Char('0'))
                                         .arg(index);
    }
    request(QStringLiteral("show"));
    QThread::msleep(100);
    request(QStringLiteral("testSetText"),
            {{QStringLiteral("text"), nativeSelectionScrollText}});
    const int nativeSelectionAnchor =
        nativeSelectionScrollText.indexOf(QStringLiteral("line-02"));
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), nativeSelectionAnchor},
             {QStringLiteral("end"), nativeSelectionAnchor}});
    setScrollY(0);
    const QJsonObject nativeSelectionDragStarted = nativeSelectionDrag(
        QStringLiteral("begin"), QStringLiteral("bottom"), nativeSelectionAnchor);
    QThread::msleep(210);
    const QJsonObject nativeSelectionDragging = editorStatus();
    nativeSelectionDrag(QStringLiteral("finish"), QStringLiteral("bottom"));
    const QJsonObject nativeSelectionReleased = editorStatus();
    QThread::msleep(70);
    const QJsonObject nativeSelectionSettled = editorStatus();
    QThread::msleep(120);
    const QJsonObject nativeSelectionStopped = editorStatus();
    addCheck(checks, details, QStringLiteral("nativeSelectionDragAutoScrollsAndStops"),
             nativeSelectionDragStarted.value(QStringLiteral("eventsAccepted")).toBool()
                 && nativeSelectionDragging.value(
                        QStringLiteral("scrollContentY")).toDouble() > 20.0
                 && nativeSelectionDragging.value(QStringLiteral("selectionEnd")).toInt()
                        > nativeSelectionAnchor
                 && std::abs(nativeSelectionStopped.value(
                                 QStringLiteral("scrollContentY")).toDouble()
                             - nativeSelectionSettled.value(
                                 QStringLiteral("scrollContentY")).toDouble()) < 1.5,
             QJsonObject{{QStringLiteral("started"), nativeSelectionDragStarted},
                         {QStringLiteral("dragging"), nativeSelectionDragging},
                         {QStringLiteral("released"), nativeSelectionReleased},
                         {QStringLiteral("settled"), nativeSelectionSettled},
                         {QStringLiteral("stopped"), nativeSelectionStopped}});

    const int upwardSelectionAnchor =
        nativeSelectionScrollText.indexOf(QStringLiteral("line-70"));
    request(QStringLiteral("testSetSelection"),
            {{QStringLiteral("start"), upwardSelectionAnchor},
             {QStringLiteral("end"), upwardSelectionAnchor}});
    setScrollY(100000);
    const QJsonObject upwardSelectionInitial = editorStatus();
    const QJsonObject upwardSelectionStarted = nativeSelectionDrag(
        QStringLiteral("begin"), QStringLiteral("top"), upwardSelectionAnchor);
    QThread::msleep(210);
    const QJsonObject upwardSelectionDragging = editorStatus();
    nativeSelectionDrag(QStringLiteral("finish"), QStringLiteral("top"));
    QThread::msleep(70);
    const QJsonObject upwardSelectionSettled = editorStatus();
    QThread::msleep(120);
    const QJsonObject upwardSelectionStopped = editorStatus();
    addCheck(checks, details, QStringLiteral("nativeSelectionDragAutoScrollsUpward"),
             upwardSelectionStarted.value(QStringLiteral("eventsAccepted")).toBool()
                 && upwardSelectionDragging.value(
                        QStringLiteral("scrollContentY")).toDouble()
                        < upwardSelectionInitial.value(
                              QStringLiteral("scrollContentY")).toDouble() - 20.0
                 && upwardSelectionDragging.value(QStringLiteral("selectionStart")).toInt()
                        < upwardSelectionAnchor
                 && std::abs(upwardSelectionStopped.value(
                                 QStringLiteral("scrollContentY")).toDouble()
                             - upwardSelectionSettled.value(
                                 QStringLiteral("scrollContentY")).toDouble()) < 1.5,
             QJsonObject{{QStringLiteral("initial"), upwardSelectionInitial},
                         {QStringLiteral("started"), upwardSelectionStarted},
                         {QStringLiteral("dragging"), upwardSelectionDragging},
                         {QStringLiteral("settled"), upwardSelectionSettled},
                         {QStringLiteral("stopped"), upwardSelectionStopped}});
    request(QStringLiteral("hide"));
    QThread::msleep(180);
    return plainFilePath;
}

} // namespace EditingTests
