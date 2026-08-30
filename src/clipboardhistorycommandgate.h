#pragma once

#include <QString>
#include <QStringList>

inline const QStringList &clipboardHistoryTestCommands()
{
    static const QStringList commands{
        QStringLiteral("testEmitClipboardChange"),
        QStringLiteral("testClipboardHistoryState"),
        QStringLiteral("testResetClipboardHistory"),
        QStringLiteral("testSetClipboardHistoryFault"),
        QStringLiteral("testRestartClipboardMonitoring"),
        QStringLiteral("testWaitForClipboardHistoryIdle"),
        QStringLiteral("testClipboardHistoryUiAction"),
        QStringLiteral("testClipboardHistoryWindowLeave"),
        QStringLiteral("testDragClipboardHistory"),
        QStringLiteral("testClipboardHistoryDragUi"),
    };
    return commands;
}

inline bool isClipboardHistoryTestCommand(const QString &command)
{
    return clipboardHistoryTestCommands().contains(command);
}

inline bool clipboardHistoryTestCommandAllowed(const QString &command, bool testMode)
{
    return testMode && isClipboardHistoryTestCommand(command);
}
