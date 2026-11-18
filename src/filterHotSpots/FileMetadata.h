/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef KONSOLE_FILEMETADATA_H
#define KONSOLE_FILEMETADATA_H
#include <QString>
#include <QUrl>
namespace Konsole
{
struct FileMetadata {
    static QString localFilePath(QUrl url);
    static QString describe(const QString &path);
    static QString clipboardPath(const QString &path);
};
}
#endif
