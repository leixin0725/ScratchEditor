#include "uiconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaType>
#include <QStandardPaths>

#include <algorithm>

namespace {

QString configFilePath(bool isolatedTestMode)
{
    const QByteArray overridePath = qgetenv("SCRATCHEDITOR_UI_CONFIG");
    if (!overridePath.isEmpty()) {
        return QDir::cleanPath(QString::fromLocal8Bit(overridePath));
    }

    const QString bundledPath = QCoreApplication::applicationDirPath()
        + QStringLiteral("/config/ui.json");
    if (isolatedTestMode) {
        return bundledPath;
    }

    const QString sharedDirectory = QStandardPaths::writableLocation(
        QStandardPaths::AppConfigLocation);
    if (sharedDirectory.isEmpty()) {
        return bundledPath;
    }
    const QString sharedPath = sharedDirectory + QStringLiteral("/ui.json");
    if (!QFileInfo::exists(sharedPath)) {
        QDir().mkpath(sharedDirectory);
        QFile::copy(bundledPath, sharedPath);
    }
    return QFileInfo::exists(sharedPath) ? sharedPath : bundledPath;
}

// JSONC：解析前剥离 // 与 /* */ 注释，字符串字面量内不剥离。
QByteArray stripJsonComments(const QByteArray &input)
{
    QByteArray output;
    output.reserve(input.size());
    bool inString = false;
    bool escaped = false;
    int index = 0;
    while (index < input.size()) {
        const char current = input.at(index);
        if (inString) {
            output.append(current);
            if (escaped) {
                escaped = false;
            } else if (current == '\\') {
                escaped = true;
            } else if (current == '"') {
                inString = false;
            }
            ++index;
            continue;
        }
        if (current == '"') {
            inString = true;
            output.append(current);
            ++index;
            continue;
        }
        if (current == '/' && index + 1 < input.size()
            && input.at(index + 1) == '/') {
            while (index < input.size() && input.at(index) != '\n') {
                ++index;
            }
            continue;
        }
        if (current == '/' && index + 1 < input.size()
            && input.at(index + 1) == '*') {
            index += 2;
            while (index + 1 < input.size()
                   && !(input.at(index) == '*' && input.at(index + 1) == '/')) {
                ++index;
            }
            index = index + 1 < input.size() ? index + 2 : input.size();
            continue;
        }
        output.append(current);
        ++index;
    }
    return output;
}

QVariantMap mergeMaps(const QVariantMap &base, const QVariantMap &overrides)
{
    QVariantMap result = base;
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
        const QVariant baseChild = result.value(it.key());
        if (it.value().typeId() == QMetaType::QVariantMap
            && baseChild.typeId() == QMetaType::QVariantMap) {
            result.insert(it.key(), mergeMaps(baseChild.toMap(), it.value().toMap()));
        } else {
            result.insert(it.key(), it.value());
        }
    }
    return result;
}

QVariant valueAt(const QVariantMap &map, const QStringList &keys)
{
    QVariantMap current = map;
    for (int index = 0; index < keys.size() - 1; ++index) {
        const QVariant child = current.value(keys.at(index));
        if (child.typeId() != QMetaType::QVariantMap) {
            return {};
        }
        current = child.toMap();
    }
    return current.value(keys.last());
}

void insertAt(QVariantMap &map, const QStringList &keys, const QVariant &value,
              int index = 0)
{
    if (index == keys.size() - 1) {
        map.insert(keys.at(index), value);
        return;
    }
    QVariantMap child = map.value(keys.at(index)).toMap();
    insertAt(child, keys, value, index + 1);
    map.insert(keys.at(index), child);
}

int intAt(const QVariantMap &map, const QStringList &keys, int fallback,
          int minValue, int maxValue)
{
    const QVariant value = valueAt(map, keys);
    if (!value.isValid()) {
        return fallback;
    }
    bool ok = false;
    const int parsed = value.toInt(&ok);
    if (!ok || parsed < minValue || parsed > maxValue) {
        return fallback;
    }
    return parsed;
}

double doubleAt(const QVariantMap &map, const QStringList &keys, double fallback,
                double minValue, double maxValue)
{
    const QVariant value = valueAt(map, keys);
    if (!value.isValid()) {
        return fallback;
    }
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    if (!ok || parsed < minValue || parsed > maxValue) {
        return fallback;
    }
    return parsed;
}

QString stringAt(const QVariantMap &map, const QStringList &keys,
                 const QString &fallback)
{
    const QVariant value = valueAt(map, keys);
    const QString parsed = value.toString().trimmed();
    return parsed.isEmpty() ? fallback : parsed;
}

bool boolAt(const QVariantMap &map, const QStringList &keys, bool fallback)
{
    const QVariant value = valueAt(map, keys);
    return value.typeId() == QMetaType::Bool ? value.toBool() : fallback;
}

} // namespace

UiConfig UiConfig::defaults()
{
    UiConfig config;
    QVariantMap map;
    const auto put = [&map](const QStringList &keys, const QVariant &value) {
        insertAt(map, keys, value);
    };
    put({"window", "defaultWidth"}, 920);
    put({"window", "defaultHeight"}, 640);
    put({"window", "minimumWidth"}, 500);
    put({"window", "minimumHeight"}, 320);
    put({"layout", "margin"}, 18);
    put({"layout", "resizeMargin"}, 8);
    put({"layout", "dragZoneHeight"}, 52);
    put({"layout", "borderWidth"}, 1);
    put({"layout", "spacingInput"}, 9);
    put({"layout", "spacingTight"}, 2);
    put({"layout", "spacingSmall"}, 4);
    put({"layout", "spacingNormal"}, 5);
    put({"layout", "spacingMedium"}, 8);
    put({"layout", "spacingLarge"}, 10);
    put({"layout", "spacingWide"}, 12);
    put({"layout", "spacingHuge"}, 20);
    put({"layout", "radiusSmall"}, 3);
    put({"layout", "radiusNormal"}, 4);
    put({"layout", "radiusMedium"}, 5);
    put({"layout", "radiusLarge"}, 6);
    put({"layout", "radiusXLarge"}, 7);
    put({"layout", "radiusPill"}, 17);
    put({"layout", "controlHeightCompact"}, 30);
    put({"layout", "controlHeightSmall"}, 32);
    put({"layout", "controlHeightNormal"}, 34);
    put({"layout", "controlHeightLarge"}, 36);
    put({"layout", "controlHeightTall"}, 38);
    put({"layout", "controlHeightExtraTall"}, 40);
    put({"layout", "editorPaddingX"}, 12);
    put({"layout", "editorPaddingY"}, 8);
    put({"layout", "editorContentBottomGap"}, 20);
    put({"layout", "selectionCursorWidth"}, 2);
    put({"layout", "scrollbarWidth"}, 5);
    put({"layout", "scrollbarMinHeight"}, 28);
    put({"layout", "scrollbarOffset"}, 2);
    put({"fonts", "family"}, QStringLiteral("Microsoft YaHei UI"));
    put({"fonts", "monospaceFamily"}, QStringLiteral("Cascadia Mono"));
    put({"fonts", "title"}, 13);
    put({"fonts", "heading"}, 11);
    put({"fonts", "normal"}, 10);
    put({"fonts", "small"}, 9);
    put({"fonts", "caption"}, 8);
    put({"fonts", "editorDefaultSize"}, 13);
    put({"fonts", "editorSizeMin"}, 9);
    put({"fonts", "editorSizeMax"}, 24);
    put({"animation", "transitionDuration"}, 120);
    put({"animation", "windowShapeScale"}, 0.98);
    put({"animation", "historyHoverOpenDelay"}, 100);
    put({"animation", "historyHoverCloseDelay"}, 250);
    put({"animation", "scrollRefreshInterval"}, 60);
    put({"animation", "benchmarkDuration"}, 1000);
    put({"animation", "probeWidth"}, 40);
    put({"animation", "probeHeight"}, 2);
    put({"animation", "probeRadius"}, 1);
    put({"animation", "probeYOffset"}, 4);
    put({"panels", "statusText", "maxWidth"}, 360);
    put({"panels", "statusText", "maxWidthRatio"}, 0.55);
    put({"panels", "statusPanel", "padding"}, 20);
    put({"panels", "statusPanel", "margins"}, 10);
    put({"panels", "statusPanel", "spacing"}, 5);
    put({"panels", "statusPanel", "topGap"}, 6);
    put({"panels", "statusPanel", "bottomGap"}, 12);
    put({"panels", "statusPanel", "defaultFontSize"}, 10);
    put({"panels", "statusPanel", "fontSizeMin"}, 9);
    put({"panels", "statusPanel", "fontSizeMax"}, 24);
    put({"panels", "statusPanel", "defaultShowDelayMs"}, 300);
    put({"panels", "statusPanel", "showDelayMinMs"}, 0);
    put({"panels", "statusPanel", "showDelayMaxMs"}, 2000);
    put({"panels", "statusPanel", "defaultHideDelayMs"}, 250);
    put({"panels", "statusPanel", "hideDelayMinMs"}, 0);
    put({"panels", "statusPanel", "hideDelayMaxMs"}, 3000);
    put({"panels", "statusPanel", "defaultMaxWidth"}, 360);
    put({"panels", "statusPanel", "maxWidthMin"}, 200);
    put({"panels", "statusPanel", "maxWidthMax"}, 800);
    put({"panels", "find", "maxWidth"}, 760);
    put({"panels", "find", "widthInset"}, 48);
    put({"panels", "find", "heightSingle"}, 66);
    put({"panels", "find", "heightReplace"}, 104);
    put({"panels", "find", "paddingX"}, 12);
    put({"panels", "find", "paddingY"}, 10);
    put({"panels", "find", "rowGap"}, 10);
    put({"panels", "find", "controlsWidth"}, 328);
    put({"panels", "find", "caseSensitiveWidth"}, 42);
    put({"panels", "find", "prevWidth"}, 54);
    put({"panels", "find", "nextWidth"}, 54);
    put({"panels", "find", "closeWidth"}, 30);
    put({"panels", "find", "actionWidth"}, 70);
    put({"panels", "find", "rightInset"}, 42);
    put({"panels", "find", "gap"}, 8);
    put({"panels", "find", "statusY"}, 47);
    put({"panels", "find", "statusYReplace"}, 86);
    put({"panels", "find", "statusWidth"}, 142);
    put({"panels", "history", "triggerWidth"}, 12);
    put({"panels", "history", "minWidth"}, 200);
    put({"panels", "history", "maxWidth"}, 360);
    put({"panels", "history", "overlayThreshold"}, 320);
    put({"panels", "history", "cardHeightDefault"}, 58);
    put({"panels", "history", "cardHeightMin"}, 44);
    put({"panels", "history", "cardHeightMax"}, 200);
    put({"panels", "history", "titleX"}, 14);
    put({"panels", "history", "titleY"}, 12);
    put({"panels", "history", "searchX"}, 12);
    put({"panels", "history", "searchY"}, 40);
    put({"panels", "history", "searchInsetX"}, 24);
    put({"panels", "history", "inputMargin"}, 8);
    put({"panels", "history", "listX"}, 8);
    put({"panels", "history", "listY"}, 82);
    put({"panels", "history", "listInsetX"}, 16);
    put({"panels", "history", "listBottomInset"}, 132);
    put({"panels", "history", "cardTextX"}, 8);
    put({"panels", "history", "cardTextY"}, 4);
    put({"panels", "history", "cardTextInsetX"}, 16);
    put({"panels", "history", "cardMetaHeight"}, 22);
    put({"panels", "history", "cardMetaBottomGap"}, 16);
    put({"panels", "history", "cardLineHeight"}, 18);
    put({"panels", "history", "cardMaxLines"}, 3);
    put({"panels", "history", "emptyTextInsetX"}, 24);
    put({"panels", "history", "footerMarginX"}, 12);
    put({"panels", "history", "footerBottomGap"}, 10);
    put({"panels", "history", "footerButtonWidth"}, 72);
    put({"panels", "dialog", "maxWidth"}, 420);
    put({"panels", "dialog", "widthInset"}, 48);
    put({"panels", "dialog", "height"}, 140);
    put({"panels", "dialog", "titleTop"}, 24);
    put({"panels", "dialog", "buttonY"}, 82);
    put({"panels", "dialog", "buttonWidth"}, 120);
    put({"panels", "dialog", "buttonSide"}, 54);
    put({"panels", "commandPalette", "maxWidth"}, 620);
    put({"panels", "commandPalette", "minTop"}, 72);
    put({"panels", "commandPalette", "topRatio"}, 0.14);
    put({"panels", "commandPalette", "widthInset"}, 64);
    put({"panels", "commandPalette", "maxHeight"}, 500);
    put({"panels", "commandPalette", "bottomGap"}, 60);
    put({"panels", "commandPalette", "innerPaddingX"}, 14);
    put({"panels", "commandPalette", "innerPaddingY"}, 14);
    put({"panels", "commandPalette", "insetX"}, 28);
    put({"panels", "commandPalette", "textMarginX"}, 12);
    put({"panels", "commandPalette", "listX"}, 10);
    put({"panels", "commandPalette", "listTopGap"}, 12);
    put({"panels", "commandPalette", "listInsetX"}, 20);
    put({"panels", "commandPalette", "listBottomInset"}, 118);
    put({"panels", "commandPalette", "rowTextMargin"}, 10);
    put({"panels", "commandPalette", "statusX"}, 16);
    put({"panels", "commandPalette", "statusYFromBottom"}, 38);
    put({"panels", "commandPalette", "statusInsetX"}, 32);
    put({"panels", "commandPalette", "shortcutYFromBottom"}, 86);
    put({"panels", "settingsPage", "maxWidth"}, 640);
    put({"panels", "settingsPage", "widthInset"}, 40);
    put({"panels", "settingsPage", "maxHeight"}, 430);
    put({"panels", "settingsPage", "heightInset"}, 32);
    put({"panels", "settingsPage", "paddingX"}, 20);
    put({"panels", "settingsPage", "titleY"}, 16);
    put({"panels", "settingsPage", "closeMarginX"}, 18);
    put({"panels", "settingsPage", "closeHitInset"}, -8);
    put({"panels", "settingsPage", "contentY"}, 52);
    put({"panels", "settingsPage", "contentHeight"}, 425);
    put({"panels", "settingsPage", "contentBottomInset"}, 112);
    put({"panels", "settingsPage", "rowHeight"}, 48);
    put({"panels", "settingsPage", "labelYOffset"}, 9);
    put({"panels", "settingsPage", "labelWidth"}, 118);
    put({"panels", "settingsPage", "columnX"}, 126);
    put({"panels", "settingsPage", "controlWidth"}, 86);
    put({"panels", "settingsPage", "controlWidthWide"}, 110);
    put({"panels", "settingsPage", "captionTopGap"}, 41);
    put({"panels", "settingsPage", "captionBottomGap"}, 20);
    put({"panels", "settingsPage", "saveStatusBottomGap"}, 19);
    put({"panels", "settingsPage", "saveStatusWidthInset"}, 300);
    put({"panels", "settingsPage", "buttonsGap"}, 10);
    put({"panels", "settingsPage", "buttonsBottomGap"}, 12);
    put({"panels", "settingsPage", "actionButtonWidth"}, 92);
    put({"panels", "settingsPage", "applyRightMargin"}, 20);
    put({"palette", "dark", "background"}, QStringLiteral("#252525"));
    put({"palette", "dark", "editorSurface"}, QStringLiteral("#292929"));
    put({"palette", "dark", "panel"}, QStringLiteral("#292929"));
    put({"palette", "dark", "field"}, QStringLiteral("#1d1d1d"));
    put({"palette", "dark", "text"}, QStringLiteral("#f2f2f2"));
    put({"palette", "dark", "strongText"}, QStringLiteral("#ffffff"));
    put({"palette", "dark", "mutedText"}, QStringLiteral("#9a9a9a"));
    put({"palette", "dark", "border"}, QStringLiteral("#505050"));
    put({"palette", "dark", "button"}, QStringLiteral("#393939"));
    put({"palette", "dark", "buttonAccentText"}, QStringLiteral("#ffffff"));
    put({"palette", "dark", "danger"}, QStringLiteral("#ff8a80"));
    put({"palette", "dark", "dangerText"}, QStringLiteral("#ffffff"));
    put({"palette", "dark", "overlay"}, QStringLiteral("#88000000"));
    put({"palette", "dark", "scrollbarThumbActive"}, QStringLiteral("#8b8b8b"));
    put({"palette", "dark", "scrollbarThumbIdle"}, QStringLiteral("#555555"));
    put({"palette", "light", "background"}, QStringLiteral("#f7f8fa"));
    put({"palette", "light", "editorSurface"}, QStringLiteral("#ffffff"));
    put({"palette", "light", "panel"}, QStringLiteral("#ffffff"));
    put({"palette", "light", "field"}, QStringLiteral("#f5f7fa"));
    put({"palette", "light", "text"}, QStringLiteral("#24292f"));
    put({"palette", "light", "strongText"}, QStringLiteral("#111111"));
    put({"palette", "light", "mutedText"}, QStringLiteral("#57606a"));
    put({"palette", "light", "border"}, QStringLiteral("#d0d7de"));
    put({"palette", "light", "button"}, QStringLiteral("#eaeef2"));
    put({"palette", "light", "buttonAccentText"}, QStringLiteral("#ffffff"));
    put({"palette", "light", "danger"}, QStringLiteral("#cf222e"));
    put({"palette", "light", "dangerText"}, QStringLiteral("#ffffff"));
    put({"palette", "light", "overlay"}, QStringLiteral("#88000000"));
    put({"palette", "light", "scrollbarThumbActive"}, QStringLiteral("#6e7781"));
    put({"palette", "light", "scrollbarThumbIdle"}, QStringLiteral("#afb8c1"));
    put({"placement", "anchorGap"}, 16);
    put({"preferences", "theme"}, QStringLiteral("dark"));
    put({"preferences", "animationsEnabled"}, true);
    config.m_map = map;
    return config;
}

QVariantMap UiConfig::sanitize(const QVariantMap &input)
{
    const QVariantMap fallback = defaults().m_map;
    QVariantMap result = input;
    const auto fixInt = [&](const QStringList &keys, int minValue, int maxValue) {
        const int fallbackValue =
            intAt(fallback, keys, qBound(minValue, 0, maxValue), minValue, maxValue);
        insertAt(result, keys, intAt(input, keys, fallbackValue, minValue, maxValue));
    };
    const auto fixDouble = [&](const QStringList &keys, double minValue, double maxValue) {
        const double fallbackValue = doubleAt(fallback, keys, minValue, minValue, maxValue);
        insertAt(result, keys, doubleAt(input, keys, fallbackValue, minValue, maxValue));
    };
    const auto fixString = [&](const QStringList &keys) {
        const QString fallbackValue = stringAt(fallback, keys, QString());
        insertAt(result, keys, stringAt(input, keys, fallbackValue));
    };

    fixInt({"window", "defaultWidth"}, 200, 10000);
    fixInt({"window", "defaultHeight"}, 200, 10000);
    fixInt({"window", "minimumWidth"}, 100, 2000);
    fixInt({"window", "minimumHeight"}, 100, 2000);
    fixInt({"layout", "margin"}, 0, 200);
    fixInt({"layout", "resizeMargin"}, 0, 100);
    fixInt({"layout", "dragZoneHeight"}, 20, 300);
    fixInt({"layout", "borderWidth"}, 0, 20);
    fixInt({"layout", "spacingInput"}, 0, 100);
    fixInt({"layout", "spacingTight"}, 0, 100);
    fixInt({"layout", "spacingSmall"}, 0, 100);
    fixInt({"layout", "spacingNormal"}, 0, 100);
    fixInt({"layout", "spacingMedium"}, 0, 100);
    fixInt({"layout", "spacingLarge"}, 0, 100);
    fixInt({"layout", "spacingWide"}, 0, 100);
    fixInt({"layout", "spacingHuge"}, 0, 200);
    fixInt({"layout", "radiusSmall"}, 0, 100);
    fixInt({"layout", "radiusNormal"}, 0, 100);
    fixInt({"layout", "radiusMedium"}, 0, 100);
    fixInt({"layout", "radiusLarge"}, 0, 100);
    fixInt({"layout", "radiusXLarge"}, 0, 100);
    fixInt({"layout", "radiusPill"}, 0, 200);
    fixInt({"layout", "controlHeightCompact"}, 16, 200);
    fixInt({"layout", "controlHeightSmall"}, 16, 200);
    fixInt({"layout", "controlHeightNormal"}, 16, 200);
    fixInt({"layout", "controlHeightLarge"}, 16, 200);
    fixInt({"layout", "controlHeightTall"}, 16, 200);
    fixInt({"layout", "controlHeightExtraTall"}, 16, 200);
    fixInt({"layout", "editorPaddingX"}, 0, 200);
    fixInt({"layout", "editorPaddingY"}, 0, 200);
    fixInt({"layout", "editorContentBottomGap"}, 0, 400);
    fixInt({"layout", "selectionCursorWidth"}, 1, 20);
    fixInt({"layout", "scrollbarWidth"}, 1, 50);
    fixInt({"layout", "scrollbarMinHeight"}, 8, 400);
    fixInt({"layout", "scrollbarOffset"}, 0, 100);
    fixString({"fonts", "family"});
    fixString({"fonts", "monospaceFamily"});
    fixInt({"fonts", "title"}, 6, 72);
    fixInt({"fonts", "heading"}, 6, 72);
    fixInt({"fonts", "normal"}, 6, 72);
    fixInt({"fonts", "small"}, 6, 72);
    fixInt({"fonts", "caption"}, 6, 72);
    fixInt({"fonts", "editorSizeMin"}, 6, 40);
    fixInt({"fonts", "editorSizeMax"}, 8, 72);
    fixInt({"fonts", "statusPanelDefaultSize"}, 6, 72);
    fixInt({"animation", "transitionDuration"}, 0, 10000);
    fixDouble({"animation", "windowShapeScale"}, 0.5, 1.0);
    fixInt({"animation", "historyHoverOpenDelay"}, 0, 10000);
    fixInt({"animation", "historyHoverCloseDelay"}, 0, 10000);
    fixInt({"animation", "scrollRefreshInterval"}, 1, 10000);
    fixInt({"animation", "benchmarkDuration"}, 1, 60000);
    fixInt({"animation", "probeWidth"}, 1, 1000);
    fixInt({"animation", "probeHeight"}, 1, 100);
    fixInt({"animation", "probeRadius"}, 0, 100);
    fixInt({"animation", "probeYOffset"}, 0, 200);
    fixInt({"panels", "statusText", "maxWidth"}, 100, 2000);
    fixDouble({"panels", "statusText", "maxWidthRatio"}, 0.1, 1.0);
    fixInt({"panels", "statusPanel", "padding"}, 0, 400);
    fixInt({"panels", "statusPanel", "margins"}, 0, 100);
    fixInt({"panels", "statusPanel", "spacing"}, 0, 100);
    fixInt({"panels", "statusPanel", "topGap"}, 0, 100);
    fixInt({"panels", "statusPanel", "bottomGap"}, 0, 100);
    fixInt({"panels", "statusPanel", "fontSizeMin"}, 6, 40);
    fixInt({"panels", "statusPanel", "fontSizeMax"}, 8, 72);
    fixInt({"panels", "statusPanel", "showDelayMinMs"}, 0, 10000);
    fixInt({"panels", "statusPanel", "showDelayMaxMs"}, 0, 10000);
    fixInt({"panels", "statusPanel", "hideDelayMinMs"}, 0, 10000);
    fixInt({"panels", "statusPanel", "hideDelayMaxMs"}, 0, 10000);
    fixInt({"panels", "statusPanel", "maxWidthMin"}, 100, 2000);
    fixInt({"panels", "statusPanel", "maxWidthMax"}, 100, 4000);
    fixInt({"panels", "find", "maxWidth"}, 200, 4000);
    fixInt({"panels", "find", "widthInset"}, 0, 1000);
    fixInt({"panels", "find", "heightSingle"}, 24, 400);
    fixInt({"panels", "find", "heightReplace"}, 24, 600);
    fixInt({"panels", "find", "paddingX"}, 0, 200);
    fixInt({"panels", "find", "paddingY"}, 0, 200);
    fixInt({"panels", "find", "rowGap"}, 0, 200);
    fixInt({"panels", "find", "controlsWidth"}, 0, 4000);
    fixInt({"panels", "find", "caseSensitiveWidth"}, 10, 500);
    fixInt({"panels", "find", "prevWidth"}, 10, 500);
    fixInt({"panels", "find", "nextWidth"}, 10, 500);
    fixInt({"panels", "find", "closeWidth"}, 10, 500);
    fixInt({"panels", "find", "actionWidth"}, 10, 500);
    fixInt({"panels", "find", "rightInset"}, 0, 1000);
    fixInt({"panels", "find", "gap"}, 0, 200);
    fixInt({"panels", "find", "statusY"}, 0, 2000);
    fixInt({"panels", "find", "statusYReplace"}, 0, 2000);
    fixInt({"panels", "find", "statusWidth"}, 20, 2000);
    fixInt({"panels", "history", "triggerWidth"}, 1, 200);
    fixInt({"panels", "history", "minWidth"}, 50, 2000);
    fixInt({"panels", "history", "maxWidth"}, 50, 4000);
    fixInt({"panels", "history", "overlayThreshold"}, 50, 4000);
    fixInt({"panels", "history", "cardHeightMin"}, 20, 300);
    fixInt({"panels", "history", "cardHeightMax"}, 50, 1000);
    fixInt({"panels", "history", "titleX"}, 0, 1000);
    fixInt({"panels", "history", "titleY"}, 0, 1000);
    fixInt({"panels", "history", "searchX"}, 0, 1000);
    fixInt({"panels", "history", "searchY"}, 0, 1000);
    fixInt({"panels", "history", "searchInsetX"}, 0, 1000);
    fixInt({"panels", "history", "inputMargin"}, 0, 100);
    fixInt({"panels", "history", "listX"}, 0, 1000);
    fixInt({"panels", "history", "listY"}, 0, 1000);
    fixInt({"panels", "history", "listInsetX"}, 0, 1000);
    fixInt({"panels", "history", "listBottomInset"}, 0, 4000);
    fixInt({"panels", "history", "cardTextX"}, 0, 1000);
    fixInt({"panels", "history", "cardTextY"}, 0, 1000);
    fixInt({"panels", "history", "cardTextInsetX"}, 0, 1000);
    fixInt({"panels", "history", "cardMetaHeight"}, 8, 500);
    fixInt({"panels", "history", "cardMetaBottomGap"}, 0, 500);
    fixInt({"panels", "history", "cardLineHeight"}, 8, 500);
    fixInt({"panels", "history", "cardMaxLines"}, 1, 20);
    fixInt({"panels", "history", "emptyTextInsetX"}, 0, 1000);
    fixInt({"panels", "history", "footerMarginX"}, 0, 1000);
    fixInt({"panels", "history", "footerBottomGap"}, 0, 1000);
    fixInt({"panels", "history", "footerButtonWidth"}, 10, 1000);
    fixInt({"panels", "dialog", "maxWidth"}, 100, 4000);
    fixInt({"panels", "dialog", "widthInset"}, 0, 1000);
    fixInt({"panels", "dialog", "height"}, 20, 2000);
    fixInt({"panels", "dialog", "titleTop"}, 0, 1000);
    fixInt({"panels", "dialog", "buttonY"}, 0, 2000);
    fixInt({"panels", "dialog", "buttonWidth"}, 10, 2000);
    fixInt({"panels", "dialog", "buttonSide"}, 0, 2000);
    fixInt({"panels", "commandPalette", "maxWidth"}, 200, 4000);
    fixInt({"panels", "commandPalette", "minTop"}, 0, 4000);
    fixDouble({"panels", "commandPalette", "topRatio"}, 0.0, 1.0);
    fixInt({"panels", "commandPalette", "widthInset"}, 0, 2000);
    fixInt({"panels", "commandPalette", "maxHeight"}, 50, 4000);
    fixInt({"panels", "commandPalette", "bottomGap"}, 0, 2000);
    fixInt({"panels", "commandPalette", "innerPaddingX"}, 0, 500);
    fixInt({"panels", "commandPalette", "innerPaddingY"}, 0, 500);
    fixInt({"panels", "commandPalette", "insetX"}, 0, 1000);
    fixInt({"panels", "commandPalette", "textMarginX"}, 0, 500);
    fixInt({"panels", "commandPalette", "listX"}, 0, 1000);
    fixInt({"panels", "commandPalette", "listTopGap"}, 0, 500);
    fixInt({"panels", "commandPalette", "listInsetX"}, 0, 1000);
    fixInt({"panels", "commandPalette", "listBottomInset"}, 0, 4000);
    fixInt({"panels", "commandPalette", "rowTextMargin"}, 0, 500);
    fixInt({"panels", "commandPalette", "statusX"}, 0, 1000);
    fixInt({"panels", "commandPalette", "statusYFromBottom"}, 0, 2000);
    fixInt({"panels", "commandPalette", "statusInsetX"}, 0, 1000);
    fixInt({"panels", "commandPalette", "shortcutYFromBottom"}, 0, 2000);
    fixInt({"panels", "settingsPage", "maxWidth"}, 200, 4000);
    fixInt({"panels", "settingsPage", "widthInset"}, 0, 1000);
    fixInt({"panels", "settingsPage", "maxHeight"}, 100, 4000);
    fixInt({"panels", "settingsPage", "heightInset"}, 0, 1000);
    fixInt({"panels", "settingsPage", "paddingX"}, 0, 1000);
    fixInt({"panels", "settingsPage", "titleY"}, 0, 1000);
    fixInt({"panels", "settingsPage", "closeMarginX"}, 0, 1000);
    fixInt({"panels", "settingsPage", "closeHitInset"}, -200, 200);
    fixInt({"panels", "settingsPage", "contentY"}, 0, 2000);
    fixInt({"panels", "settingsPage", "contentHeight"}, 50, 8000);
    fixInt({"panels", "settingsPage", "contentBottomInset"}, 0, 4000);
    fixInt({"panels", "settingsPage", "rowHeight"}, 16, 500);
    fixInt({"panels", "settingsPage", "labelYOffset"}, 0, 200);
    fixInt({"panels", "settingsPage", "labelWidth"}, 20, 2000);
    fixInt({"panels", "settingsPage", "columnX"}, 0, 4000);
    fixInt({"panels", "settingsPage", "controlWidth"}, 10, 2000);
    fixInt({"panels", "settingsPage", "controlWidthWide"}, 10, 2000);
    fixInt({"panels", "settingsPage", "captionTopGap"}, 0, 4000);
    fixInt({"panels", "settingsPage", "captionBottomGap"}, 0, 4000);
    fixInt({"panels", "settingsPage", "saveStatusBottomGap"}, 0, 1000);
    fixInt({"panels", "settingsPage", "saveStatusWidthInset"}, 0, 4000);
    fixInt({"panels", "settingsPage", "buttonsGap"}, 0, 500);
    fixInt({"panels", "settingsPage", "buttonsBottomGap"}, 0, 500);
    fixInt({"panels", "settingsPage", "actionButtonWidth"}, 10, 2000);
    fixInt({"panels", "settingsPage", "applyRightMargin"}, 0, 1000);
    fixString({"palette", "dark", "background"});
    fixString({"palette", "dark", "editorSurface"});
    fixString({"palette", "dark", "panel"});
    fixString({"palette", "dark", "field"});
    fixString({"palette", "dark", "text"});
    fixString({"palette", "dark", "strongText"});
    fixString({"palette", "dark", "mutedText"});
    fixString({"palette", "dark", "border"});
    fixString({"palette", "dark", "button"});
    fixString({"palette", "dark", "buttonAccentText"});
    fixString({"palette", "dark", "danger"});
    fixString({"palette", "dark", "dangerText"});
    fixString({"palette", "dark", "overlay"});
    fixString({"palette", "dark", "scrollbarThumbActive"});
    fixString({"palette", "dark", "scrollbarThumbIdle"});
    fixString({"palette", "light", "background"});
    fixString({"palette", "light", "editorSurface"});
    fixString({"palette", "light", "panel"});
    fixString({"palette", "light", "field"});
    fixString({"palette", "light", "text"});
    fixString({"palette", "light", "strongText"});
    fixString({"palette", "light", "mutedText"});
    fixString({"palette", "light", "border"});
    fixString({"palette", "light", "button"});
    fixString({"palette", "light", "buttonAccentText"});
    fixString({"palette", "light", "danger"});
    fixString({"palette", "light", "dangerText"});
    fixString({"palette", "light", "overlay"});
    fixString({"palette", "light", "scrollbarThumbActive"});
    fixString({"palette", "light", "scrollbarThumbIdle"});
    fixInt({"placement", "anchorGap"}, 0, 500);
    fixString({"preferences", "theme"});

    // 跨字段一致性：最小值不得超过默认值，用户默认值须落在合法范围内。
    const int minWidth = intAt(result, {"window", "minimumWidth"}, 500, 100, 2000);
    const int defaultWidth = intAt(result, {"window", "defaultWidth"}, 920, 200, 10000);
    if (minWidth > defaultWidth) {
        insertAt(result, {"window", "minimumWidth"}, defaultWidth);
    }
    const int minHeight = intAt(result, {"window", "minimumHeight"}, 320, 100, 2000);
    const int defaultHeight = intAt(result, {"window", "defaultHeight"}, 640, 200, 10000);
    if (minHeight > defaultHeight) {
        insertAt(result, {"window", "minimumHeight"}, defaultHeight);
    }
    const int editorMin = intAt(result, {"fonts", "editorSizeMin"}, 9, 6, 40);
    const int editorMax = intAt(result, {"fonts", "editorSizeMax"}, 24, 8, 72);
    insertAt(result, {"fonts", "editorDefaultSize"},
             intAt(result, {"fonts", "editorDefaultSize"}, 13, editorMin, editorMax));
    const int panelFontMin = intAt(result, {"panels", "statusPanel", "fontSizeMin"}, 9, 6, 40);
    const int panelFontMax = intAt(result, {"panels", "statusPanel", "fontSizeMax"}, 24, 8, 72);
    insertAt(result, {"panels", "statusPanel", "defaultFontSize"},
             intAt(result, {"panels", "statusPanel", "defaultFontSize"}, 10,
                   panelFontMin, panelFontMax));
    const int showMin = intAt(result, {"panels", "statusPanel", "showDelayMinMs"}, 0, 0, 10000);
    const int showMax = intAt(result, {"panels", "statusPanel", "showDelayMaxMs"}, 2000, 0, 10000);
    insertAt(result, {"panels", "statusPanel", "defaultShowDelayMs"},
             intAt(result, {"panels", "statusPanel", "defaultShowDelayMs"}, 300,
                   showMin, showMax));
    const int hideMin = intAt(result, {"panels", "statusPanel", "hideDelayMinMs"}, 0, 0, 10000);
    const int hideMax = intAt(result, {"panels", "statusPanel", "hideDelayMaxMs"}, 3000, 0, 10000);
    insertAt(result, {"panels", "statusPanel", "defaultHideDelayMs"},
             intAt(result, {"panels", "statusPanel", "defaultHideDelayMs"}, 250,
                   hideMin, hideMax));
    const int widthMin = intAt(result, {"panels", "statusPanel", "maxWidthMin"}, 200, 100, 2000);
    const int widthMax = intAt(result, {"panels", "statusPanel", "maxWidthMax"}, 800, 100, 4000);
    insertAt(result, {"panels", "statusPanel", "defaultMaxWidth"},
             intAt(result, {"panels", "statusPanel", "defaultMaxWidth"}, 360,
                   widthMin, widthMax));
    const int cardMin = intAt(result, {"panels", "history", "cardHeightMin"}, 44, 20, 300);
    const int cardMax = intAt(result, {"panels", "history", "cardHeightMax"}, 200, 50, 1000);
    insertAt(result, {"panels", "history", "cardHeightDefault"},
             intAt(result, {"panels", "history", "cardHeightDefault"}, 58,
                   cardMin, cardMax));
    const int historyMin = intAt(result, {"panels", "history", "minWidth"}, 200, 50, 2000);
    const int historyMax = intAt(result, {"panels", "history", "maxWidth"}, 360, 50, 4000);
    if (historyMin > historyMax) {
        insertAt(result, {"panels", "history", "minWidth"}, historyMax);
    }
    return result;
}

UiConfig UiConfig::load(bool isolatedTestMode)
{
    UiConfig config;
    config.m_filePath = configFilePath(isolatedTestMode);
    QFile file(config.m_filePath);
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray stripped = stripJsonComments(file.readAll());
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(stripped, &error);
        if (error.error == QJsonParseError::NoError && document.isObject()) {
            config.m_map = sanitize(mergeMaps(defaults().m_map, document.object().toVariantMap()));
            config.m_loadedFromFile = true;
            return config;
        }
    }
    config.m_map = sanitize(defaults().m_map);
    return config;
}

QVariantMap UiConfig::map() const
{
    return m_map;
}

QString UiConfig::filePath() const
{
    return m_filePath;
}

bool UiConfig::loadedFromFile() const
{
    return m_loadedFromFile;
}

int UiConfig::windowDefaultWidth() const
{
    return intAt(m_map, {"window", "defaultWidth"}, 920, 200, 10000);
}

int UiConfig::windowDefaultHeight() const
{
    return intAt(m_map, {"window", "defaultHeight"}, 640, 200, 10000);
}

int UiConfig::windowMinimumWidth() const
{
    return intAt(m_map, {"window", "minimumWidth"}, 500, 100, 2000);
}

int UiConfig::windowMinimumHeight() const
{
    return intAt(m_map, {"window", "minimumHeight"}, 320, 100, 2000);
}

int UiConfig::transitionDuration() const
{
    return intAt(m_map, {"animation", "transitionDuration"}, 120, 0, 10000);
}

double UiConfig::windowShapeScale() const
{
    return doubleAt(m_map, {"animation", "windowShapeScale"}, 0.98, 0.5, 1.0);
}

int UiConfig::placementAnchorGap() const
{
    return intAt(m_map, {"placement", "anchorGap"}, 16, 0, 500);
}

QString UiConfig::defaultTheme() const
{
    return stringAt(m_map, {"preferences", "theme"}, QStringLiteral("dark"));
}

bool UiConfig::defaultAnimationsEnabled() const
{
    return boolAt(m_map, {"preferences", "animationsEnabled"}, true);
}

int UiConfig::editorDefaultFontSize() const
{
    return intAt(m_map, {"fonts", "editorDefaultSize"}, 13, 6, 72);
}

int UiConfig::editorFontSizeMin() const
{
    return intAt(m_map, {"fonts", "editorSizeMin"}, 9, 6, 40);
}

int UiConfig::editorFontSizeMax() const
{
    return intAt(m_map, {"fonts", "editorSizeMax"}, 24, 8, 72);
}

int UiConfig::statusPanelDefaultFontSize() const
{
    return intAt(m_map, {"panels", "statusPanel", "defaultFontSize"}, 10, 6, 72);
}

int UiConfig::statusPanelFontSizeMin() const
{
    return intAt(m_map, {"panels", "statusPanel", "fontSizeMin"}, 9, 6, 40);
}

int UiConfig::statusPanelFontSizeMax() const
{
    return intAt(m_map, {"panels", "statusPanel", "fontSizeMax"}, 24, 8, 72);
}

int UiConfig::statusPanelDefaultShowDelayMs() const
{
    return intAt(m_map, {"panels", "statusPanel", "defaultShowDelayMs"}, 300, 0, 10000);
}

int UiConfig::statusPanelShowDelayMinMs() const
{
    return intAt(m_map, {"panels", "statusPanel", "showDelayMinMs"}, 0, 0, 10000);
}

int UiConfig::statusPanelShowDelayMaxMs() const
{
    return intAt(m_map, {"panels", "statusPanel", "showDelayMaxMs"}, 2000, 0, 10000);
}

int UiConfig::statusPanelDefaultHideDelayMs() const
{
    return intAt(m_map, {"panels", "statusPanel", "defaultHideDelayMs"}, 250, 0, 10000);
}

int UiConfig::statusPanelHideDelayMinMs() const
{
    return intAt(m_map, {"panels", "statusPanel", "hideDelayMinMs"}, 0, 0, 10000);
}

int UiConfig::statusPanelHideDelayMaxMs() const
{
    return intAt(m_map, {"panels", "statusPanel", "hideDelayMaxMs"}, 3000, 0, 10000);
}

int UiConfig::statusPanelDefaultMaxWidth() const
{
    return intAt(m_map, {"panels", "statusPanel", "defaultMaxWidth"}, 360, 100, 4000);
}

int UiConfig::statusPanelMaxWidthMin() const
{
    return intAt(m_map, {"panels", "statusPanel", "maxWidthMin"}, 200, 100, 2000);
}

int UiConfig::statusPanelMaxWidthMax() const
{
    return intAt(m_map, {"panels", "statusPanel", "maxWidthMax"}, 800, 100, 4000);
}

int UiConfig::historyCardHeightDefault() const
{
    return intAt(m_map, {"panels", "history", "cardHeightDefault"}, 58, 20, 1000);
}

int UiConfig::historyCardHeightMin() const
{
    return intAt(m_map, {"panels", "history", "cardHeightMin"}, 44, 20, 300);
}

int UiConfig::historyCardHeightMax() const
{
    return intAt(m_map, {"panels", "history", "cardHeightMax"}, 200, 50, 1000);
}
