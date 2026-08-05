#include "appsettings.h"

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace {

constexpr auto schemaVersionKey = "meta/schemaVersion";
constexpr auto geometryKey = "window/geometry";
constexpr auto externalGeometryKey = "window/externalGeometry";
constexpr auto themeKey = "appearance/theme";
constexpr auto fontFamilyKey = "editor/fontFamily";
constexpr auto fontPointSizeKey = "editor/fontPointSize";
constexpr auto animationsEnabledKey = "ui/animationsEnabled";
constexpr auto defaultTheme = "dark";
constexpr int defaultFontPointSize = 13;
constexpr bool defaultAnimationsEnabled = true;

QString shortcutKey(const QString &commandId)
{
    return QStringLiteral("shortcuts/%1").arg(commandId);
}

} // namespace

AppSettings::AppSettings(bool testMode)
{
    const QString path = settingsPath(testMode);
    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    const bool allowLegacyMigration = !testMode && !info.exists();
    m_settings = std::make_unique<QSettings>(path, QSettings::IniFormat);
    initialize(allowLegacyMigration);
}

AppSettings::~AppSettings() = default;

QString AppSettings::fileName() const
{
    return m_settings ? QDir::toNativeSeparators(m_settings->fileName()) : QString();
}

int AppSettings::status() const
{
    return m_settings ? static_cast<int>(m_settings->status()) : -1;
}

int AppSettings::schemaVersion() const
{
    return m_settings ? m_settings->value(QLatin1StringView(schemaVersionKey), 0).toInt() : 0;
}

QStringList AppSettings::allKeys() const
{
    return m_settings ? m_settings->allKeys() : QStringList{};
}

QRect AppSettings::windowGeometry() const
{
    if (!m_settings) {
        return {};
    }
    const QVariant value = m_settings->value(QLatin1StringView(geometryKey));
    return value.canConvert<QRect>() ? value.toRect() : QRect{};
}

void AppSettings::setWindowGeometry(const QRect &geometry)
{
    if (!m_settings || !geometry.isValid()) {
        return;
    }
    m_settings->setValue(QLatin1StringView(geometryKey), geometry);
    sync();
}

QRect AppSettings::externalWindowGeometry() const
{
    if (!m_settings) {
        return {};
    }
    const QVariant value = m_settings->value(QLatin1StringView(externalGeometryKey));
    return value.canConvert<QRect>() ? value.toRect() : QRect{};
}

void AppSettings::setExternalWindowGeometry(const QRect &geometry)
{
    if (!m_settings || !geometry.isValid()) {
        return;
    }
    m_settings->setValue(QLatin1StringView(externalGeometryKey), geometry);
    sync();
}

QString AppSettings::shortcut(const QString &commandId, const QString &defaultValue) const
{
    return m_settings ? m_settings->value(shortcutKey(commandId), defaultValue).toString()
                      : defaultValue;
}

void AppSettings::setShortcut(const QString &commandId, const QString &sequence)
{
    if (!m_settings) {
        return;
    }
    m_settings->setValue(shortcutKey(commandId), sequence);
    sync();
}

void AppSettings::resetShortcuts()
{
    if (!m_settings) {
        return;
    }
    m_settings->remove(QStringLiteral("shortcuts"));
    sync();
}

AppSettings::Appearance AppSettings::appearance() const
{
    Appearance result{QString::fromLatin1(defaultTheme), defaultFontFamily(),
                      defaultFontPointSize, defaultAnimationsEnabled};
    if (!m_settings) {
        return result;
    }

    const QString storedTheme = m_settings->value(QLatin1StringView(themeKey), result.theme)
                                    .toString().trimmed().toLower();
    if (validTheme(storedTheme)) {
        result.theme = storedTheme;
    }
    const QString storedFamily = m_settings->value(QLatin1StringView(fontFamilyKey),
                                                    result.fontFamily).toString().trimmed();
    if (validFontFamily(storedFamily)) {
        result.fontFamily = storedFamily;
    }
    const int storedSize = m_settings->value(QLatin1StringView(fontPointSizeKey),
                                             defaultFontPointSize).toInt();
    if (storedSize >= 9 && storedSize <= 24) {
        result.fontPointSize = storedSize;
    }
    result.animationsEnabled = m_settings->value(QLatin1StringView(animationsEnabledKey),
                                                 defaultAnimationsEnabled).toBool();
    return result;
}

bool AppSettings::setAppearance(const QString &theme, const QString &fontFamily,
                                int fontPointSize, bool animationsEnabled,
                                QString *errorMessage)
{
    const QString normalizedTheme = theme.trimmed().toLower();
    const QString normalizedFamily = fontFamily.trimmed();
    if (!validTheme(normalizedTheme)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("主题必须是 dark 或 light");
        }
        return false;
    }
    if (!validFontFamily(normalizedFamily)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("系统中不存在字体：%1").arg(normalizedFamily);
        }
        return false;
    }
    if (fontPointSize < 9 || fontPointSize > 24) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("字号必须在 9 到 24 之间");
        }
        return false;
    }
    if (!m_settings) {
        return false;
    }

    m_settings->setValue(QLatin1StringView(themeKey), normalizedTheme);
    m_settings->setValue(QLatin1StringView(fontFamilyKey), normalizedFamily);
    m_settings->setValue(QLatin1StringView(fontPointSizeKey), fontPointSize);
    m_settings->setValue(QLatin1StringView(animationsEnabledKey), animationsEnabled);
    writeSchemaVersion();
    sync();
    return m_settings->status() == QSettings::NoError;
}

void AppSettings::resetAppearance()
{
    if (!m_settings) {
        return;
    }
    m_settings->remove(QStringLiteral("appearance"));
    m_settings->remove(QStringLiteral("editor"));
    m_settings->remove(QStringLiteral("ui"));
    writeSchemaVersion();
    sync();
}

void AppSettings::resetAll()
{
    if (!m_settings) {
        return;
    }
    m_settings->clear();
    writeSchemaVersion();
    sync();
}

QString AppSettings::settingsPath(bool testMode)
{
    const QString overridePath = QString::fromUtf8(qgetenv("SCRATCHEDITOR_SETTINGS_FILE")).trimmed();
    if (!overridePath.isEmpty()) {
        return QDir::cleanPath(overridePath);
    }

    if (testMode) {
        QString server = QString::fromUtf8(qgetenv("SCRATCHEDITOR_SERVER_NAME"));
        if (server.isEmpty()) {
            server = QStringLiteral("ScratchEditor.Test");
        }
        server.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")),
                       QStringLiteral("_"));
        const QString directory = QDir::temp().filePath(QStringLiteral("ScratchEditor/tests"));
        return QDir(directory).filePath(server + QStringLiteral(".ini"));
    }

    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(directory).filePath(QStringLiteral("settings.ini"));
}

QString AppSettings::defaultFontFamily()
{
    const QString preferred = QStringLiteral("Microsoft YaHei UI");
    return validFontFamily(preferred) ? preferred : QGuiApplication::font().family();
}

bool AppSettings::validTheme(const QString &theme)
{
    return theme == QStringLiteral("dark") || theme == QStringLiteral("light");
}

bool AppSettings::validFontFamily(const QString &fontFamily)
{
    if (fontFamily.trimmed().isEmpty()) {
        return false;
    }
    const QStringList families = QFontDatabase::families();
    return std::any_of(families.cbegin(), families.cend(), [&fontFamily](const QString &candidate) {
        return candidate.compare(fontFamily, Qt::CaseInsensitive) == 0;
    });
}

void AppSettings::initialize(bool allowLegacyMigration)
{
    if (allowLegacyMigration) {
        migrateLegacySettings();
    }
    writeSchemaVersion();
    sync();
}

void AppSettings::migrateLegacySettings()
{
    QSettings legacy(QSettings::NativeFormat, QSettings::UserScope,
                     QStringLiteral("ScratchEditor"), QStringLiteral("ScratchEditor"));
    for (const QString &key : legacy.allKeys()) {
        if (key == QLatin1StringView(geometryKey) || key.startsWith(QStringLiteral("shortcuts/"))) {
            m_settings->setValue(key, legacy.value(key));
        }
    }
    m_settings->setValue(QStringLiteral("meta/legacyNativeSettingsMigrated"), true);
}

void AppSettings::writeSchemaVersion()
{
    if (m_settings) {
        m_settings->setValue(QLatin1StringView(schemaVersionKey), CurrentSchemaVersion);
    }
}

void AppSettings::sync()
{
    if (m_settings) {
        m_settings->sync();
    }
}
