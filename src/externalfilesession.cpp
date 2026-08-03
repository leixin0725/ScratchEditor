#include "externalfilesession.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringDecoder>

ExternalFileSession::ExternalFileSession(const QString &filePath)
    : m_filePath(QDir::cleanPath(QFileInfo(filePath).absoluteFilePath()))
{
}

QString ExternalFileSession::filePath() const
{
    return m_filePath;
}

QString ExternalFileSession::displayPath() const
{
    return QDir::toNativeSeparators(m_filePath);
}

bool ExternalFileSession::load(QString *text, QString *errorMessage) const
{
    if (!text) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("没有可接收文件内容的目标");
        }
        return false;
    }

    const QFileInfo info(m_filePath);
    if (!info.exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("外部编辑文件不存在：%1").arg(displayPath());
        }
        return false;
    }
    if (!info.isFile()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("外部编辑路径不是普通文件：%1").arg(displayPath());
        }
        return false;
    }

    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法读取外部编辑文件：%1（%2）")
                                .arg(displayPath(), file.errorString());
        }
        return false;
    }

    QByteArray bytes = file.readAll();
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        bytes.remove(0, 3);
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString decoded = decoder.decode(bytes);
    if (decoder.hasError()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("外部编辑文件不是有效的 UTF-8 文本：%1")
                                .arg(displayPath());
        }
        return false;
    }

    *text = decoded;
    return true;
}

bool ExternalFileSession::save(const QString &text, QString *errorMessage) const
{
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法保存外部编辑文件：%1（%2）")
                                .arg(displayPath(), file.errorString());
        }
        return false;
    }

    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("外部编辑文件写入不完整：%1（%2）")
                                .arg(displayPath(), file.errorString());
        }
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法提交外部编辑文件：%1（%2）")
                                .arg(displayPath(), file.errorString());
        }
        return false;
    }
    return true;
}
