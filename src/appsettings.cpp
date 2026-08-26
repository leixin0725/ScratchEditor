#include "appsettings.h"

#include "uiconfig.h"

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
constexpr auto fontFamilyKey = "appearance/fontFamily";
constexpr auto fallbackFontFamilyKey = "appearance/fallbackFontFamily";
constexpr auto fontPointSizeKey = "appearance/fontPointSize";
constexpr auto fontWeightKey = "appearance/fontWeight";
constexpr auto animationsEnabledKey = "appearance/animationsEnabled";
constexpr auto statusPanelFontSizeKey = "statusPanel/fontSize";
constexpr auto statusPanelShowDelayMsKey = "statusPanel/showDelayMs";
constexpr auto statusPanelHideDelayMsKey = "statusPanel/hideDelayMs";
constexpr auto statusPanelMaxWidthKey = "statusPanel/maxWidth";
constexpr auto clipboardHistoryCardHeightKey = "clipboardHistory/cardHeight";
constexpr auto defaultTheme = "dark";

QString shortcutKey(const QString &commandId)
{
    return QStringLiteral("shortcuts/%1").arg(commandId);
}

} // namespace

AppSettings::AppSettings(bool testMode, const UiConfig *uiConfig)
    : m_uiConfig(uiConfig)
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
    const QString fallbackTheme =
        m_uiConfig ? m_uiConfig->defaultTheme() : QString::fromLatin1(defaultTheme);
    const QString fallbackPrimaryFamily = m_uiConfig
        ? m_uiConfig->editorDefaultFontFamily() : QStringLiteral("Consolas");
    const QString fallbackSecondaryFamily = m_uiConfig
        ? m_uiConfig->editorDefaultFallbackFontFamily() : QStringLiteral("NSimSun");
    const int fallbackFontSize =
        m_uiConfig ? m_uiConfig->editorDefaultFontSize() : 13;
    const int fallbackFontWeight =
        m_uiConfig ? m_uiConfig->editorDefaultFontWeight() : 400;
    const bool fallbackAnimations =
        m_uiConfig ? m_uiConfig->defaultAnimationsEnabled() : true;
    const QString primaryFamily = validFontFamily(fallbackPrimaryFamily)
        ? fallbackPrimaryFamily : defaultFontFamily();
    const QString secondaryFamily = validFontFamily(fallbackSecondaryFamily)
        ? fallbackSecondaryFamily : defaultFontFamily();
    Appearance result{fallbackTheme, primaryFamily, secondaryFamily,
                      fallbackFontSize, fallbackFontWeight, fallbackAnimations};
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
    const QString storedFallbackFamily =
        m_settings->value(QLatin1StringView(fallbackFontFamilyKey),
                          result.fallbackFontFamily).toString().trimmed();
    if (validFontFamily(storedFallbackFamily)) {
        result.fallbackFontFamily = storedFallbackFamily;
    }
    const int sizeMin = m_uiConfig ? m_uiConfig->editorFontSizeMin() : 9;
    const int sizeMax = m_uiConfig ? m_uiConfig->editorFontSizeMax() : 24;
    const int storedSize = m_settings->value(QLatin1StringView(fontPointSizeKey),
                                             fallbackFontSize).toInt();
    if (storedSize >= sizeMin && storedSize <= sizeMax) {
        result.fontPointSize = storedSize;
    }
    const int storedWeight = m_settings->value(QLatin1StringView(fontWeightKey),
                                               fallbackFontWeight).toInt();
    if (validFontWeight(storedWeight)) {
        result.fontWeight = storedWeight;
    }
    result.animationsEnabled = m_settings->value(QLatin1StringView(animationsEnabledKey),
                                                 fallbackAnimations).toBool();
    return result;
}

bool AppSettings::setAppearance(const QString &theme, const QString &fontFamily,
                                const QString &fallbackFontFamily, int fontPointSize,
                                int fontWeight, bool animationsEnabled,
                                QString *errorMessage)
{
    const QString normalizedTheme = theme.trimmed().toLower();
    const QString normalizedFamily = fontFamily.trimmed();
    const QString normalizedFallbackFamily = fallbackFontFamily.trimmed();
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
    if (!validFontFamily(normalizedFallbackFamily)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("系统中不存在 fallback 字体：%1")
                                .arg(normalizedFallbackFamily);
        }
        return false;
    }
    const int sizeMin = m_uiConfig ? m_uiConfig->editorFontSizeMin() : 9;
    const int sizeMax = m_uiConfig ? m_uiConfig->editorFontSizeMax() : 24;
    if (fontPointSize < sizeMin || fontPointSize > sizeMax) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("字号必须在 %1 到 %2 之间")
                                .arg(sizeMin).arg(sizeMax);
        }
        return false;
    }
    if (!validFontWeight(fontWeight)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("字体粗细必须是 100 到 900 之间的整百数值");
        }
        return false;
    }
    if (!m_settings) {
        return false;
    }

    m_settings->setValue(QLatin1StringView(themeKey), normalizedTheme);
    m_settings->setValue(QLatin1StringView(fontFamilyKey), normalizedFamily);
    m_settings->setValue(QLatin1StringView(fallbackFontFamilyKey),
                         normalizedFallbackFamily);
    m_settings->setValue(QLatin1StringView(fontPointSizeKey), fontPointSize);
    m_settings->setValue(QLatin1StringView(fontWeightKey), fontWeight);
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
    // 清理历史版本遗留的 editor/ui 段（schema 1 迁移后不应再出现）。
    m_settings->remove(QStringLiteral("editor"));
    m_settings->remove(QStringLiteral("ui"));
    writeSchemaVersion();
    sync();
}

AppSettings::StatusPanel AppSettings::statusPanel() const
{
    StatusPanel result{
        m_uiConfig ? m_uiConfig->statusPanelDefaultFontSize() : 10,
        m_uiConfig ? m_uiConfig->statusPanelDefaultShowDelayMs() : 300,
        m_uiConfig ? m_uiConfig->statusPanelDefaultHideDelayMs() : 250,
        m_uiConfig ? m_uiConfig->statusPanelDefaultMaxWidth() : 360};
    if (!m_settings) {
        return result;
    }

    const int fontSizeMin = m_uiConfig ? m_uiConfig->statusPanelFontSizeMin() : 9;
    const int fontSizeMax = m_uiConfig ? m_uiConfig->statusPanelFontSizeMax() : 24;
    const int storedFontSize =
        m_settings->value(QLatin1StringView(statusPanelFontSizeKey),
                          result.fontSize).toInt();
    if (storedFontSize >= fontSizeMin && storedFontSize <= fontSizeMax) {
        result.fontSize = storedFontSize;
    }
    const int showMin = m_uiConfig ? m_uiConfig->statusPanelShowDelayMinMs() : 0;
    const int showMax = m_uiConfig ? m_uiConfig->statusPanelShowDelayMaxMs() : 2000;
    const int storedShowDelayMs =
        m_settings->value(QLatin1StringView(statusPanelShowDelayMsKey),
                          result.showDelayMs).toInt();
    if (storedShowDelayMs >= showMin && storedShowDelayMs <= showMax) {
        result.showDelayMs = storedShowDelayMs;
    }
    const int hideMin = m_uiConfig ? m_uiConfig->statusPanelHideDelayMinMs() : 0;
    const int hideMax = m_uiConfig ? m_uiConfig->statusPanelHideDelayMaxMs() : 3000;
    const int storedHideDelayMs =
        m_settings->value(QLatin1StringView(statusPanelHideDelayMsKey),
                          result.hideDelayMs).toInt();
    if (storedHideDelayMs >= hideMin && storedHideDelayMs <= hideMax) {
        result.hideDelayMs = storedHideDelayMs;
    }
    const int widthMin = m_uiConfig ? m_uiConfig->statusPanelMaxWidthMin() : 200;
    const int widthMax = m_uiConfig ? m_uiConfig->statusPanelMaxWidthMax() : 800;
    const int storedMaxWidth =
        m_settings->value(QLatin1StringView(statusPanelMaxWidthKey),
                          result.maxWidth).toInt();
    if (storedMaxWidth >= widthMin && storedMaxWidth <= widthMax) {
        result.maxWidth = storedMaxWidth;
    }
    return result;
}

bool AppSettings::setStatusPanel(int fontSize, int showDelayMs, int hideDelayMs,
                                 int maxWidth, QString *errorMessage)
{
    const int fontSizeMin = m_uiConfig ? m_uiConfig->statusPanelFontSizeMin() : 9;
    const int fontSizeMax = m_uiConfig ? m_uiConfig->statusPanelFontSizeMax() : 24;
    const int showMin = m_uiConfig ? m_uiConfig->statusPanelShowDelayMinMs() : 0;
    const int showMax = m_uiConfig ? m_uiConfig->statusPanelShowDelayMaxMs() : 2000;
    const int hideMin = m_uiConfig ? m_uiConfig->statusPanelHideDelayMinMs() : 0;
    const int hideMax = m_uiConfig ? m_uiConfig->statusPanelHideDelayMaxMs() : 3000;
    const int widthMin = m_uiConfig ? m_uiConfig->statusPanelMaxWidthMin() : 200;
    const int widthMax = m_uiConfig ? m_uiConfig->statusPanelMaxWidthMax() : 800;
    if (fontSize < fontSizeMin || fontSize > fontSizeMax) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("面板字号必须在 %1 到 %2 之间")
                                .arg(fontSizeMin).arg(fontSizeMax);
        }
        return false;
    }
    if (showDelayMs < showMin || showDelayMs > showMax) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("面板显示延迟必须在 %1 到 %2 毫秒之间")
                                .arg(showMin).arg(showMax);
        }
        return false;
    }
    if (hideDelayMs < hideMin || hideDelayMs > hideMax) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("面板收起延迟必须在 %1 到 %2 毫秒之间")
                                .arg(hideMin).arg(hideMax);
        }
        return false;
    }
    if (maxWidth < widthMin || maxWidth > widthMax) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("面板最大宽度必须在 %1 到 %2 像素之间")
                                .arg(widthMin).arg(widthMax);
        }
        return false;
    }
    if (!m_settings) {
        return false;
    }

    m_settings->setValue(QLatin1StringView(statusPanelFontSizeKey), fontSize);
    m_settings->setValue(QLatin1StringView(statusPanelShowDelayMsKey), showDelayMs);
    m_settings->setValue(QLatin1StringView(statusPanelHideDelayMsKey), hideDelayMs);
    m_settings->setValue(QLatin1StringView(statusPanelMaxWidthKey), maxWidth);
    writeSchemaVersion();
    sync();
    return m_settings->status() == QSettings::NoError;
}

void AppSettings::resetStatusPanel()
{
    if (!m_settings) {
        return;
    }
    m_settings->remove(QStringLiteral("statusPanel"));
    writeSchemaVersion();
    sync();
}

int AppSettings::historyCardHeight() const
{
    const int defaultCardHeight =
        m_uiConfig ? m_uiConfig->historyCardHeightDefault() : 58;
    const int minCardHeight =
        m_uiConfig ? m_uiConfig->historyCardHeightMin() : 44;
    const int maxCardHeight =
        m_uiConfig ? m_uiConfig->historyCardHeightMax() : 200;
    if (!m_settings) {
        return defaultCardHeight;
    }
    const int stored = m_settings->value(QLatin1StringView(clipboardHistoryCardHeightKey),
                                         defaultCardHeight).toInt();
    return std::clamp(stored, minCardHeight, maxCardHeight);
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

bool AppSettings::validFontWeight(int fontWeight)
{
    return fontWeight >= 100 && fontWeight <= 900 && fontWeight % 100 == 0;
}

void AppSettings::initialize(bool allowLegacyMigration)
{
    if (allowLegacyMigration) {
        migrateLegacySettings();
    }
    migrateSchemaV1Keys();
    migrateSchemaV3Keys();
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

void AppSettings::migrateSchemaV1Keys()
{
    if (!m_settings || m_settings->value(QLatin1StringView(schemaVersionKey), 0).toInt() >= 2) {
        return;
    }
    const auto moveKey = [this](const char *from, const char *to) {
        if (m_settings->contains(QLatin1StringView(from))
            && !m_settings->contains(QLatin1StringView(to))) {
            m_settings->setValue(QLatin1StringView(to),
                                 m_settings->value(QLatin1StringView(from)));
        }
        m_settings->remove(QLatin1StringView(from));
    };
    moveKey("editor/fontFamily", "appearance/fontFamily");
    moveKey("editor/fontPointSize", "appearance/fontPointSize");
    moveKey("ui/animationsEnabled", "appearance/animationsEnabled");
}

void AppSettings::migrateSchemaV3Keys()
{
    if (!m_settings || m_settings->value(QLatin1StringView(schemaVersionKey), 0).toInt() >= 4) {
        return;
    }
    const int fallbackFontWeight =
        m_uiConfig ? m_uiConfig->editorDefaultFontWeight() : 400;
    const int storedFontWeight = m_settings->value(QLatin1StringView(fontWeightKey),
                                                   fallbackFontWeight).toInt();
    m_settings->setValue(QLatin1StringView(fontWeightKey),
                         validFontWeight(storedFontWeight) ? storedFontWeight
                                                          : fallbackFontWeight);
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
