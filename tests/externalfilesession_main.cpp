#include "externalfilesession.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

namespace {

bool check(bool condition, const QString &message)
{
    if (!condition) {
        QTextStream(stderr) << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporaryDirectory(
        QDir::tempPath() + QStringLiteral("/ScratchEditor 外部文件测试-XXXXXX"));
    if (!check(temporaryDirectory.isValid(), QStringLiteral("无法创建临时测试目录"))) {
        return 1;
    }

    int failures = 0;
    const QString promptPath = temporaryDirectory.filePath(
        QStringLiteral("提示 prompt with spaces.md"));
    const QString originalText = QStringLiteral("\ufeff# 初始提示\r\n\r\n包含 emoji：🧪\r\n");
    failures += !check(writeBytes(promptPath, originalText.toUtf8()),
                       QStringLiteral("无法写入 UTF-8 BOM 测试夹具"));

    ExternalFileSession promptSession(promptPath);
    QString loadedText;
    QString errorMessage;
    failures += !check(promptSession.load(&loadedText, &errorMessage),
                       QStringLiteral("含空格和中文的路径应可读取：%1").arg(errorMessage));
    failures += !check(loadedText == originalText.sliced(1),
                       QStringLiteral("读取时应移除 UTF-8 BOM 并保留换行"));
    failures += !check(QFileInfo(promptSession.filePath()).isAbsolute(),
                       QStringLiteral("文件会话应持有规范化绝对路径"));

    const QString updatedText = QStringLiteral("# 已编辑\n\n保留 Unicode：你好 🌏\n");
    errorMessage.clear();
    failures += !check(promptSession.save(updatedText, &errorMessage),
                       QStringLiteral("Unicode 文本应可原子保存：%1").arg(errorMessage));
    const QByteArray updatedBytes = readBytes(promptPath);
    failures += !check(updatedBytes == updatedText.toUtf8(),
                       QStringLiteral("保存结果应为无 BOM 的 UTF-8 且保留末尾换行"));

    const QString emptyPath = temporaryDirectory.filePath(QStringLiteral("empty.txt"));
    failures += !check(writeBytes(emptyPath, {}), QStringLiteral("无法创建空文件夹具"));
    ExternalFileSession emptySession(emptyPath);
    loadedText = QStringLiteral("sentinel");
    errorMessage.clear();
    failures += !check(emptySession.load(&loadedText, &errorMessage) && loadedText.isEmpty(),
                       QStringLiteral("空文件应读取为空字符串：%1").arg(errorMessage));
    failures += !check(emptySession.save(QString(), &errorMessage)
                           && readBytes(emptyPath).isEmpty(),
                       QStringLiteral("空文件应可保存"));

    const QString invalidPath = temporaryDirectory.filePath(QStringLiteral("invalid.txt"));
    failures += !check(writeBytes(invalidPath, QByteArray::fromHex("c328")),
                       QStringLiteral("无法创建无效 UTF-8 夹具"));
    ExternalFileSession invalidSession(invalidPath);
    errorMessage.clear();
    failures += !check(!invalidSession.load(&loadedText, &errorMessage)
                           && errorMessage.contains(QStringLiteral("UTF-8")),
                       QStringLiteral("无效 UTF-8 应明确拒绝"));

    const QString missingPath = temporaryDirectory.filePath(QStringLiteral("missing.txt"));
    ExternalFileSession missingSession(missingPath);
    errorMessage.clear();
    failures += !check(!missingSession.load(&loadedText, &errorMessage)
                           && errorMessage.contains(QStringLiteral("不存在")),
                       QStringLiteral("缺失文件应明确拒绝"));

    ExternalFileSession directorySession(temporaryDirectory.path());
    errorMessage.clear();
    failures += !check(!directorySession.load(&loadedText, &errorMessage)
                           && errorMessage.contains(QStringLiteral("不是普通文件")),
                       QStringLiteral("目录路径应明确拒绝"));
    errorMessage.clear();
    failures += !check(!directorySession.save(QStringLiteral("must fail"), &errorMessage)
                           && !errorMessage.isEmpty(),
                       QStringLiteral("无法创建原子目标时保存应明确失败"));

    errorMessage.clear();
    failures += !check(!promptSession.load(nullptr, &errorMessage) && !errorMessage.isEmpty(),
                       QStringLiteral("空输出目标应安全失败"));

    if (failures != 0) {
        QTextStream(stderr) << failures << " external-file test(s) failed" << Qt::endl;
        return 1;
    }

    QTextStream(stdout) << "All external-file session tests passed." << Qt::endl;
    return 0;
}
