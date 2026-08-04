#include "markdownstyle.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTextCharFormat>
#include <QStandardPaths>

namespace {

MarkdownStyle::TokenStyle token(const QString &foreground, const QString &fontStyle,
                                const QString &background = {},
                                const QStringList &fontFamilies = {}, bool underline = false)
{
    MarkdownStyle::TokenStyle result;
    result.foreground = QColor(foreground);
    result.background = QColor(background);
    result.fontFamilies = fontFamilies;
    const QString normalized = fontStyle.toLower();
    result.bold = normalized.contains(QStringLiteral("bold"));
    result.italic = normalized.contains(QStringLiteral("italic"));
    result.strikeThrough = normalized.contains(QStringLiteral("strikethrough"));
    result.underline = underline;
    return result;
}

void applyToken(const QJsonObject &root, const QString &key,
                MarkdownStyle::TokenStyle *target)
{
    if (!target || !root.value(key).isObject()) {
        return;
    }
    const QJsonObject object = root.value(key).toObject();
    const QColor foreground(object.value(QStringLiteral("color")).toString());
    if (foreground.isValid()) {
        target->foreground = foreground;
    }
    const QColor background(object.value(QStringLiteral("backgroundColor")).toString());
    if (background.isValid()) {
        target->background = background;
    }
    if (object.value(QStringLiteral("fontFamilies")).isArray()) {
        QStringList families;
        for (const QJsonValue &value : object.value(QStringLiteral("fontFamilies")).toArray()) {
            if (!value.toString().isEmpty()) {
                families.append(value.toString());
            }
        }
        if (!families.isEmpty()) {
            target->fontFamilies = families;
        }
    }
    if (object.contains(QStringLiteral("fontStyle"))) {
        const QString fontStyle = object.value(QStringLiteral("fontStyle")).toString().toLower();
        target->bold = fontStyle.contains(QStringLiteral("bold"));
        target->italic = fontStyle.contains(QStringLiteral("italic"));
        target->strikeThrough = fontStyle.contains(QStringLiteral("strikethrough"));
    }
    if (object.contains(QStringLiteral("underline"))) {
        target->underline = object.value(QStringLiteral("underline")).toBool();
    }
}

bool applyTheme(const QJsonObject &root, MarkdownStyle *style)
{
    if (!style || !root.value(QStringLiteral("theme")).isObject()) {
        return false;
    }
    const QJsonObject theme = root.value(QStringLiteral("theme")).toObject();
    const QColor accent(theme.value(QStringLiteral("accentColor")).toString());
    const QColor accentText(theme.value(QStringLiteral("accentTextColor")).toString());
    if (accentText.isValid()) {
        style->accentTextColor = accentText;
    }
    if (!accent.isValid()) {
        return false;
    }
    style->accentColor = accent;
    return true;
}

QString styleFilePath(bool isolatedTestMode)
{
    const QByteArray overridePath = qgetenv("SCRATCHEDITOR_MARKDOWN_STYLE");
    if (!overridePath.isEmpty()) {
        return QString::fromLocal8Bit(overridePath);
    }

    const QString bundledPath = QCoreApplication::applicationDirPath()
        + QStringLiteral("/config/markdown-style.json");
    if (isolatedTestMode) {
        return bundledPath;
    }

    const QString sharedDirectory = QStandardPaths::writableLocation(
        QStandardPaths::AppConfigLocation);
    if (sharedDirectory.isEmpty()) {
        return bundledPath;
    }
    const QString sharedPath = sharedDirectory + QStringLiteral("/markdown-style.json");
    if (!QFileInfo::exists(sharedPath)) {
        QDir().mkpath(sharedDirectory);
        QFile::copy(bundledPath, sharedPath);
    }
    return QFileInfo::exists(sharedPath) ? sharedPath : bundledPath;
}

} // namespace

MarkdownStyle MarkdownStyle::defaults()
{
    MarkdownStyle style;
    const QStringList codeFonts{QStringLiteral("Cascadia Mono"),
                                QStringLiteral("Microsoft YaHei UI")};
    style.accentColor = QColor(QStringLiteral("#85c7c0"));
    style.accentTextColor = QColor(QStringLiteral("#183331"));
    style.baseText = token(QStringLiteral("#C2C0B6"), QStringLiteral("normal"));
    style.inlineCode = token(QStringLiteral("#ffffff"), QStringLiteral("normal"),
                             QStringLiteral("#303030"), codeFonts);
    style.codeBlock = token(QStringLiteral("#C2C0B6"), QStringLiteral("normal"),
                            QStringLiteral("#303030"), codeFonts);
    style.codeFence = style.codeBlock;
    style.listMarker = token(QStringLiteral("#ffffff"), QStringLiteral("normal"));
    style.quote = token(QStringLiteral("#999999"), QStringLiteral("italic"));
    const std::array<QString, 6> headingColors{
        QStringLiteral("#d04255"), QStringLiteral("#d5763f"),
        QStringLiteral("#e5b567"), QStringLiteral("#a8c373"),
        QStringLiteral("#6c99bb"), QStringLiteral("#9e86c8")};
    for (size_t index = 0; index < style.headings.size(); ++index) {
        style.headings[index] = token(headingColors[index], QStringLiteral("bold"));
    }
    style.bold = token(QStringLiteral("#FFE6B7"), QStringLiteral("bold"));
    style.italic = token(QStringLiteral("#999999"), QStringLiteral("italic"));
    style.boldItalic = token(QStringLiteral("#FFE6B7"), QStringLiteral("bold italic"));
    style.strikethrough = token(QStringLiteral("#999999"),
                                QStringLiteral("strikethrough"));
    style.link = token(style.accentColor.name(QColor::HexRgb), QStringLiteral("normal"),
                       {}, {}, true);
    style.linkBrackets = token(QStringLiteral("#999999"), QStringLiteral("normal"));
    style.completedTask = token(QStringLiteral("#999999"),
                                QStringLiteral("strikethrough"));
    style.checkboxBrackets = token(QStringLiteral("#4a4a4a"), QStringLiteral("normal"));
    return style;
}

MarkdownStyle MarkdownStyle::load(bool isolatedTestMode)
{
    MarkdownStyle style = defaults();
    style.m_filePath = styleFilePath(isolatedTestMode);

    QFile file(style.m_filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return style;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return style;
    }

    const QJsonObject root = document.object();
    const bool configuredAccent = applyTheme(root, &style);
    applyToken(root, QStringLiteral("baseText"), &style.baseText);
    applyToken(root, QStringLiteral("inlineCode"), &style.inlineCode);
    applyToken(root, QStringLiteral("codeBlock"), &style.codeBlock);
    applyToken(root, QStringLiteral("codeFence"), &style.codeFence);
    applyToken(root, QStringLiteral("listMarker"), &style.listMarker);
    applyToken(root, QStringLiteral("quote"), &style.quote);
    applyToken(root, QStringLiteral("bold"), &style.bold);
    applyToken(root, QStringLiteral("italic"), &style.italic);
    applyToken(root, QStringLiteral("boldItalic"), &style.boldItalic);
    applyToken(root, QStringLiteral("strikethrough"), &style.strikethrough);
    applyToken(root, QStringLiteral("link"), &style.link);
    applyToken(root, QStringLiteral("linkBrackets"), &style.linkBrackets);
    applyToken(root, QStringLiteral("completedTask"), &style.completedTask);
    applyToken(root, QStringLiteral("checkboxBrackets"), &style.checkboxBrackets);
    if (configuredAccent) {
        style.link.foreground = style.accentColor;
    } else if (style.link.foreground.isValid()) {
        // Treat the former link color as the accent when loading a pre-theme config.
        style.accentColor = style.link.foreground;
    }

    const QJsonArray headings = root.value(QStringLiteral("headings")).toArray();
    for (const QJsonValue &value : headings) {
        const QJsonObject object = value.toObject();
        const int level = object.value(QStringLiteral("level")).toInt();
        if (level < 1 || level > 6) {
            continue;
        }
        QJsonObject wrapper;
        wrapper.insert(QStringLiteral("heading"), object);
        applyToken(wrapper, QStringLiteral("heading"), &style.headings[level - 1]);
    }
    style.m_loadedFromFile = true;
    return style;
}

QTextCharFormat MarkdownStyle::textFormat(const TokenStyle &tokenStyle) const
{
    QTextCharFormat format;
    if (tokenStyle.foreground.isValid()) {
        format.setForeground(tokenStyle.foreground);
    }
    if (tokenStyle.background.isValid()) {
        format.setBackground(tokenStyle.background);
    }
    if (!tokenStyle.fontFamilies.isEmpty()) {
        format.setFontFamilies(tokenStyle.fontFamilies);
    }
    format.setFontWeight(tokenStyle.bold ? QFont::Bold : QFont::Normal);
    format.setFontItalic(tokenStyle.italic);
    format.setFontStrikeOut(tokenStyle.strikeThrough);
    format.setFontUnderline(tokenStyle.underline);
    return format;
}

QString MarkdownStyle::filePath() const
{
    return m_filePath;
}

bool MarkdownStyle::loadedFromFile() const
{
    return m_loadedFromFile;
}
