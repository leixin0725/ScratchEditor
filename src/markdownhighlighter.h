#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class MarkdownHighlighter final : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit MarkdownHighlighter(QTextDocument *document);
    void setDarkTheme(bool darkTheme);

protected:
    void highlightBlock(const QString &text) override;

private:
    QTextCharFormat m_headingFormat;
    QTextCharFormat m_quoteFormat;
    QTextCharFormat m_listFormat;
    QTextCharFormat m_taskFormat;
    QTextCharFormat m_boldFormat;
    QTextCharFormat m_italicFormat;
    QTextCharFormat m_codeFormat;
    QTextCharFormat m_linkFormat;
    QTextCharFormat m_fenceFormat;
    bool m_darkTheme = true;
    bool m_themeConfigured = false;
};
