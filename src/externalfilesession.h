#pragma once

#include <QString>

class ExternalFileSession final
{
public:
    explicit ExternalFileSession(const QString &filePath);

    QString filePath() const;
    QString displayPath() const;

    bool load(QString *text, QString *errorMessage) const;
    bool save(const QString &text, QString *errorMessage) const;

private:
    QString m_filePath;
};
