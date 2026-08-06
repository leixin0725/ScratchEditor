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
inline QList<Entry> entries(bool externalFileMode)
{
    if (externalFileMode) {
        return {
            {QStringLiteral("Ctrl+S / Esc"), QStringLiteral("保存并返回 CLI")},
            {QStringLiteral("Ctrl+W"), QStringLiteral("不保存退出")},
        };
    }
    return {
        {QStringLiteral("Esc"), QStringLiteral("关闭并复制")},
        {QStringLiteral("Ctrl+S"), QStringLiteral("关闭并输入")},
        {QStringLiteral("Ctrl+W"), QStringLiteral("关闭不保存")},
    };
}

inline QStringList forMode(bool externalFileMode)
{
    QStringList lines;
    const QList<Entry> modeEntries = entries(externalFileMode);
    lines.reserve(modeEntries.size());
    for (const Entry &entry : modeEntries) {
        lines.append(entry.shortcut + QStringLiteral(" · ") + entry.label);
    }
    return lines;
}

} // namespace StatusPanelHints
