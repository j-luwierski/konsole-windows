/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "TextPreviewContent.h"
#include "FileMetadata.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringConverter>
using namespace Konsole;
QString TextPreviewContent::windowsPathPattern()
{
    return QStringLiteral(R"RX((?:[A-Za-z]:[\\/]|\\\\)[^\s"'<>|]+|[.\p{L}\w~-]+(?:\\[^\s"'<>|]+)+|)RX");
}

QString TextPreviewContent::pathWithoutLocation(QString path)
{
    if (path.size() >= 2 && ((path.front() == QLatin1Char('"') && path.back() == QLatin1Char('"'))
                            || (path.front() == QLatin1Char('\'') && path.back() == QLatin1Char('\'')))) {
        path = path.mid(1, path.size() - 2);
    }
    static const QRegularExpression suffix(QStringLiteral(R"((:\d+(?::\d+)?:?|\(\d+(?:,\d+)?\)\]?)$)"));
    path.remove(suffix);
    if (path.size() >= 2 && ((path.front() == QLatin1Char('"') && path.back() == QLatin1Char('"'))
                            || (path.front() == QLatin1Char('\'') && path.back() == QLatin1Char('\'')))) {
        path = path.mid(1, path.size() - 2);
    }
    return path;
}

QString TextPreviewContent::fileCommand()
{
#ifdef Q_OS_WIN
    const QString bundled = QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../file/file.exe"));
    if (QFileInfo::exists(bundled)) { return bundled; }
#endif
    return QStandardPaths::findExecutable(QStringLiteral("file"));
}

TextPreviewContent TextPreviewContent::read(const QString &path, qint64 maximumBytes, bool showFileInfo, const QString &fileExecutable)
{
    auto content = readText(path, maximumBytes);
    content.metadata = FileMetadata::describe(path);
    if (!showFileInfo || content.error.isEmpty()) {
        return content;
    }
    const QFileInfo info(path);
    QString description = QObject::tr("Text preview unavailable: %1\n\nFile: %2\nSize: %3 bytes\nMIME: %4\n\nfile:\n")
                              .arg(content.error, info.fileName()).arg(info.size()).arg(QMimeDatabase().mimeTypeForFile(info.absoluteFilePath(), QMimeDatabase::MatchContent).name());
    const QString program = fileExecutable.isEmpty() ? fileCommand() : fileExecutable;
    if (program.isEmpty()) {
        description += QObject::tr("The file identification tool is unavailable.");
    } else {
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
        process.setProcessEnvironment(environment);
        QStringList arguments{QStringLiteral("--brief")};
        const QString magic = QFileInfo(program).dir().filePath(QStringLiteral("magic.mgc"));
        if (QFileInfo::exists(magic)) { arguments << QStringLiteral("--magic-file") << magic; }
        arguments << QStringLiteral("--") << info.absoluteFilePath();
        process.start(program, arguments, QIODevice::ReadOnly);
        if (!process.waitForStarted(1000) || !process.waitForFinished(5000)) {
            process.kill();
            process.waitForFinished(1000);
            description += QObject::tr("file could not complete: %1").arg(process.errorString());
        } else if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            QByteArray errorOutput = process.readAllStandardError();
            errorOutput += process.readAllStandardOutput();
            description += QObject::tr("file failed: %1").arg(QString::fromLocal8Bit(errorOutput.left(32768)));
        } else {
            description += QString::fromUtf8(process.readAllStandardOutput().left(32768)).trimmed();
        }
    }
    return {description, {}, false, content.metadata};
}

TextPreviewContent TextPreviewContent::readText(const QString &path, qint64 maximumBytes)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        return {{}, QObject::tr("The path is not a regular local file.")};
    }
    if (info.size() > maximumBytes) {
        return {{}, QObject::tr("This file exceeds the preview size limit. Increase the limit in Settings → Configure Konsole → Thumbnails / File previews.")};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {{}, QObject::tr("Cannot read the file: %1").arg(file.errorString())};
    }
    const QByteArray bytes = file.read(maximumBytes + 1);
    if (file.error() != QFileDevice::NoError) {
        return {{}, QObject::tr("Cannot read the file: %1").arg(file.errorString())};
    }
    if (bytes.size() > maximumBytes) {
        return {{}, QObject::tr("The file grew beyond the preview size limit.")};
    }
    return readBytes(bytes, maximumBytes);
}

TextPreviewContent TextPreviewContent::readBytes(const QByteArray &bytes, qint64 maximumBytes)
{
    if (bytes.size() > maximumBytes) {
        return {{}, QObject::tr("This file exceeds the preview size limit. Increase the limit in Settings → Configure Konsole → Thumbnails / File previews.")};
    }
    const auto mime = QMimeDatabase().mimeTypeForData(bytes);
    if (!bytes.isEmpty() && mime.isValid() && mime.name() != QStringLiteral("application/octet-stream")
        && !mime.inherits(QStringLiteral("text/plain")) && !mime.inherits(QStringLiteral("application/xml"))
        && mime.name() != QStringLiteral("application/json")) {
        return {{}, QObject::tr("This file is %1 and is not a text document.").arg(mime.name()), true};
    }
    const auto detected = QStringConverter::encodingForData(bytes);
    const auto encoding = detected.value_or(QStringConverter::Utf8);
    if (!detected && bytes.contains('\0')) {
        return {{}, QObject::tr("This file contains binary data and cannot be previewed as text."), true};
    }
    QStringDecoder decoder(encoding);
    QString text = decoder(bytes);
    if (decoder.hasError() && !detected) {
        QStringDecoder local(QStringConverter::System);
        text = local(bytes);
        if (local.hasError()) {
            return {{}, QObject::tr("The text encoding could not be decoded. Use UTF-8 or Unicode with a byte-order mark.")};
        }
    } else if (decoder.hasError()) {
        return {{}, QObject::tr("The file contains invalid text encoding.")};
    }
    int controls = 0;
    for (const auto ch : text) {
        if ((ch.unicode() < 32 && ch != QLatin1Char('\n') && ch != QLatin1Char('\r') && ch != QLatin1Char('\t') && ch != QLatin1Char('\f'))
            || ch == QChar(0x7f)) {
            ++controls;
        }
    }
    if (text.contains(QChar(0)) || (controls > 0 && controls * qint64(100) > text.size())) {
        return {{}, QObject::tr("This file contains binary data and cannot be previewed as text."), true};
    }
    return {text, {}};
}

