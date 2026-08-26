#pragma once

#include <QString>
#include <QVariantMap>

// UI 设计令牌的唯一默认源：窗口尺寸、布局、字体、动画、面板参数、
// 深浅调色板、窗口摆放与用户偏好默认值均来自 config/ui.json。
// 文件缺失、解析失败或字段非法时回退到内置默认值；改动后重启生效。
class UiConfig final
{
public:
    static UiConfig load(bool isolatedTestMode = false);

    // 合并并校验后的完整令牌表，供 QML 以 controller.uiConfig 读取。
    QVariantMap map() const;
    QString filePath() const;
    bool loadedFromFile() const;

    int windowDefaultWidth() const;
    int windowDefaultHeight() const;
    int windowMinimumWidth() const;
    int windowMinimumHeight() const;

    int transitionDuration() const;
    double windowShapeScale() const;

    int placementAnchorGap() const;

    QString defaultTheme() const;
    bool defaultAnimationsEnabled() const;

    QString editorDefaultFontFamily() const;
    QString editorDefaultFallbackFontFamily() const;
    int editorDefaultFontSize() const;
    int editorDefaultFontWeight() const;
    int editorFontSizeMin() const;
    int editorFontSizeMax() const;

    int statusPanelDefaultFontSize() const;
    int statusPanelFontSizeMin() const;
    int statusPanelFontSizeMax() const;
    int statusPanelDefaultShowDelayMs() const;
    int statusPanelShowDelayMinMs() const;
    int statusPanelShowDelayMaxMs() const;
    int statusPanelDefaultHideDelayMs() const;
    int statusPanelHideDelayMinMs() const;
    int statusPanelHideDelayMaxMs() const;
    int statusPanelDefaultMaxWidth() const;
    int statusPanelMaxWidthMin() const;
    int statusPanelMaxWidthMax() const;

    int historyCardHeightDefault() const;
    int historyCardHeightMin() const;
    int historyCardHeightMax() const;

private:
    static UiConfig defaults();
    static QVariantMap sanitize(const QVariantMap &input);

    QVariantMap m_map;
    QString m_filePath;
    bool m_loadedFromFile = false;
};
