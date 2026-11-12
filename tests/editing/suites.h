#pragma once

#include <QJsonObject>

class QTemporaryDir;

namespace EditingTests {

void checkStyleConfiguration(QJsonObject &checks, QJsonObject &details);
QJsonObject checkInitializeEditor(QJsonObject &checks, QJsonObject &details);
QString checkFileDropAndDrag(QJsonObject &checks, QJsonObject &details, const QTemporaryDir &droppedItems);
void checkHighlighting(QJsonObject &checks, QJsonObject &details, const QJsonObject &initial);
QString checkHeadingNavigation(QJsonObject &checks, QJsonObject &details);
void checkOrderedLists(QJsonObject &checks, QJsonObject &details);
void checkClipboardEditing(QJsonObject &checks, QJsonObject &details);
void checkEditingCommands(QJsonObject &checks, QJsonObject &details);
void checkFindAndShortcuts(QJsonObject &checks, QJsonObject &details, const QJsonObject &initial,
                         const QString &headingScrollText);
void checkPunctuationAndFormatting(QJsonObject &checks, QJsonObject &details);
void checkInputTransactions(QJsonObject &checks, QJsonObject &details, const QString &plainFilePath);
void checkSpacingAndIme(QJsonObject &checks, QJsonObject &details);
void checkParser(QJsonObject &checks, QJsonObject &details);
void checkClipboardHistory(QJsonObject &checks, QJsonObject &details);
void checkSpacingPlanner(QJsonObject &checks, QJsonObject &details);
void checkScrolling(QJsonObject &checks, QJsonObject &details);
void checkWordBoundaries(QJsonObject &checks, QJsonObject &details);
void checkScaling(QJsonObject &checks, QJsonObject &details);

} // namespace EditingTests
