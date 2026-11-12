#include "editing/suites.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace EditingTests;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QJsonObject checks;
    QJsonObject details;
    checkStyleConfiguration(checks, details);
    const QJsonObject initial = checkInitializeEditor(checks, details);
    // --- File drop: URL validation, path formatting and cursor semantics ---
    QTemporaryDir droppedItems(
        QDir::temp().filePath(QStringLiteral("ScratchEditorDrop-XXXXXX")));
    const QString plainFilePath = checkFileDropAndDrag(checks, details, droppedItems);
    checkHighlighting(checks, details, initial);
    const QString headingScrollText = checkHeadingNavigation(checks, details);
    checkOrderedLists(checks, details);
    checkClipboardEditing(checks, details);
    checkEditingCommands(checks, details);
    checkFindAndShortcuts(checks, details, initial, headingScrollText);
    checkPunctuationAndFormatting(checks, details);
    checkInputTransactions(checks, details, plainFilePath);
    checkSpacingAndIme(checks, details);
    checkParser(checks, details);
    checkClipboardHistory(checks, details);
    checkSpacingPlanner(checks, details);
    checkScrolling(checks, details);
    checkWordBoundaries(checks, details);
    checkScaling(checks, details);

    bool allPassed = true;
    for (auto it = checks.constBegin(); it != checks.constEnd(); ++it) {
        allPassed = allPassed && it.value().toBool();
    }
    const QJsonObject result{
        {QStringLiteral("allPassed"), allPassed},
        {QStringLiteral("checks"), checks},
        {QStringLiteral("details"), details},
    };
    const QByteArray output = QJsonDocument(result).toJson(QJsonDocument::Indented);
    fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout);
    return allPassed ? 0 : 1;
}
