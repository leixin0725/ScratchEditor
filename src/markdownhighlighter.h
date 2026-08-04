#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <array>

class MarkdownStyle;

class MarkdownHighlighter final : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    MarkdownHighlighter(QTextDocument *document, const MarkdownStyle &style);
    void setStyle(const MarkdownStyle &style);

protected:
    void highlightBlock(const QString &text) override;

private:
    std::array<QTextCharFormat, 6> m_headingFormats;
    QTextCharFormat m_quoteFormat;
    QTextCharFormat m_listMarkerFormat;
    QTextCharFormat m_boldFormat;
    QTextCharFormat m_italicFormat;
    QTextCharFormat m_boldItalicFormat;
    QTextCharFormat m_strikethroughFormat;
    QTextCharFormat m_inlineCodeFormat;
    QTextCharFormat m_codeBlockFormat;
    QTextCharFormat m_codeFenceFormat;
    QTextCharFormat m_linkFormat;
    QTextCharFormat m_linkBracketsFormat;
    QTextCharFormat m_completedTaskFormat;
    QTextCharFormat m_checkboxBracketsFormat;
};
