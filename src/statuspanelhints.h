#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace StatusPanelHints {

struct Entry {
    QString shortcut;
    QString label;
};

// 状态面板快捷键提示的唯一数据源；后续新增提示条目只需扩展此列表。
// commandPaletteShortcut / settingsShortcut 传当前生效快捷键（可为空，为空时跳过对应条目）。
inline QList<Entry> entries(bool externalFileMode,
                            const QString &commandPaletteShortcut =
                                QStringLiteral("Ctrl+Shift+P"),
                            const QString &settingsShortcut = QStringLiteral("Ctrl+,"))
{
    QList<Entry> result;
    if (externalFileMode) {
        result = {
            {QStringLiteral("Ctrl+S / Esc"), QStringLiteral("保存并返回 CLI")},
            {QStringLiteral("Ctrl+W"), QStringLiteral("不保存退出")},
        };
    } else {
        result = {
            {QStringLiteral("Esc"), QStringLiteral("关闭并复制")},
            {QStringLiteral("Ctrl+S"), QStringLiteral("关闭并输入")},
            {QStringLiteral("Ctrl+W"), QStringLiteral("关闭不保存")},
        };
    }
    if (!commandPaletteShortcut.isEmpty()) {
        result.append({commandPaletteShortcut, QStringLiteral("打开命令面板")});
    }
    if (!settingsShortcut.isEmpty()) {
        result.append({settingsShortcut, QStringLiteral("打开设置")});
    }
    return result;
}

inline QStringList forMode(bool externalFileMode,
                           const QString &commandPaletteShortcut =
                               QStringLiteral("Ctrl+Shift+P"),
                           const QString &settingsShortcut = QStringLiteral("Ctrl+,"))
{
    QStringList lines;
    const QList<Entry> modeEntries =
        entries(externalFileMode, commandPaletteShortcut, settingsShortcut);
    lines.reserve(modeEntries.size());
    for (const Entry &entry : modeEntries) {
        lines.append(entry.shortcut + QStringLiteral(" · ") + entry.label);
    }
    return lines;
}

} // namespace StatusPanelHints
