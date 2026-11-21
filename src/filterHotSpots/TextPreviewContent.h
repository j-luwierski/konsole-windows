/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <QByteArray>
#include <QString>
namespace Konsole {
struct TextPreviewContent {
    QString text;
    QString error;
    bool binary = false;
    QString metadata;
    static TextPreviewContent read(const QString &path, qint64 maximumBytes, bool showFileInfo = false, const QString &fileExecutable = {});
    static TextPreviewContent readText(const QString &path, qint64 maximumBytes);
    static TextPreviewContent readBytes(const QByteArray &bytes, qint64 maximumBytes);
    static QString fileCommand();
    static QString pathWithoutLocation(QString path);
    static QString windowsPathPattern();
};
}
