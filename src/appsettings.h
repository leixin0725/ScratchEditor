#pragma once

#include <QRect>
#include <QString>
#include <QStringList>

#include <memory>

class QSettings;

class AppSettings final
{
public:
    struct Appearance {
        QString theme;
        QString fontFamily;
        int fontPointSize = 13;
        bool animationsEnabled = true;
    };

    explicit AppSettings(bool testMode);
    ~AppSettings();

    QString fileName() const;
    int status() const;
    int schemaVersion() const;
    QStringList allKeys() const;

    QRect windowGeometry() const;
    void setWindowGeometry(const QRect &geometry);

    QString shortcut(const QString &commandId, const QString &defaultValue) const;
    void setShortcut(const QString &commandId, const QString &sequence);
    void resetShortcuts();

    Appearance appearance() const;
    bool setAppearance(const QString &theme, const QString &fontFamily, int fontPointSize,
                       bool animationsEnabled, QString *errorMessage = nullptr);
    void resetAppearance();
    void resetAll();

private:
    static constexpr int CurrentSchemaVersion = 1;
    static QString settingsPath(bool testMode);
    static QString defaultFontFamily();
    static bool validTheme(const QString &theme);
    static bool validFontFamily(const QString &fontFamily);
    void initialize(bool allowLegacyMigration);
    void migrateLegacySettings();
    void writeSchemaVersion();
    void sync();

    std::unique_ptr<QSettings> m_settings;
};
