#pragma once

#include <QRect>
#include <QString>
#include <QStringList>

#include <memory>

class QSettings;
class UiConfig;

class AppSettings final
{
public:
    struct Appearance {
        QString theme;
        QString fontFamily;
        QString fallbackFontFamily;
        int fontPointSize = 13;
        int fontWeight = 400;
        bool animationsEnabled = true;
    };

    struct StatusPanel {
        int fontSize = 10;
        int showDelayMs = 300;
        int hideDelayMs = 250;
        int maxWidth = 360;
    };

    // uiConfig 提供外观/状态面板/历史卡片等用户设置的默认值（单一来源）；
    // 传入 nullptr 时回退到本类内置常量。
    explicit AppSettings(bool testMode, const UiConfig *uiConfig = nullptr);
    ~AppSettings();

    QString fileName() const;
    int status() const;
    int schemaVersion() const;
    QStringList allKeys() const;

    QRect windowGeometry() const;
    void setWindowGeometry(const QRect &geometry);
    QRect externalWindowGeometry() const;
    void setExternalWindowGeometry(const QRect &geometry);

    QString shortcut(const QString &commandId, const QString &defaultValue) const;
    void setShortcut(const QString &commandId, const QString &sequence);
    void resetShortcuts();

    Appearance appearance() const;
    bool setAppearance(const QString &theme, const QString &fontFamily,
                       const QString &fallbackFontFamily, int fontPointSize,
                       int fontWeight, bool animationsEnabled,
                       QString *errorMessage = nullptr);
    void resetAppearance();
    StatusPanel statusPanel() const;
    bool setStatusPanel(int fontSize, int showDelayMs, int hideDelayMs, int maxWidth,
                        QString *errorMessage = nullptr);
    void resetStatusPanel();
    int historyCardHeight() const;
    void resetAll();

private:
    static constexpr int CurrentSchemaVersion = 4;
    static QString settingsPath(bool testMode);
    static QString defaultFontFamily();
    static bool validTheme(const QString &theme);
    static bool validFontFamily(const QString &fontFamily);
    static bool validFontWeight(int fontWeight);
    void initialize(bool allowLegacyMigration);
    void migrateLegacySettings();
    void migrateSchemaV1Keys();
    void migrateSchemaV3Keys();
    void writeSchemaVersion();
    void sync();

    std::unique_ptr<QSettings> m_settings;
    const UiConfig *m_uiConfig = nullptr;
};
