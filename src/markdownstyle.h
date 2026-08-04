#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

#include <array>

class QTextCharFormat;

class MarkdownStyle final
{
public:
    struct TokenStyle {
        QColor foreground;
        QColor background;
        QStringList fontFamilies;
        bool bold = false;
        bool italic = false;
        bool strikeThrough = false;
        bool underline = false;
    };

    static MarkdownStyle load(bool isolatedTestMode = false);

    QTextCharFormat textFormat(const TokenStyle &token) const;
    QString filePath() const;
    bool loadedFromFile() const;

    TokenStyle baseText;
    TokenStyle inlineCode;
    TokenStyle codeBlock;
    TokenStyle codeFence;
    TokenStyle listMarker;
    TokenStyle quote;
    std::array<TokenStyle, 6> headings;
    TokenStyle bold;
    TokenStyle italic;
    TokenStyle boldItalic;
    TokenStyle strikethrough;
    TokenStyle link;
    TokenStyle linkBrackets;
    TokenStyle completedTask;
    TokenStyle checkboxBrackets;
    QColor accentColor;
    QColor accentTextColor;

private:
    static MarkdownStyle defaults();
    QString m_filePath;
    bool m_loadedFromFile = false;
};
