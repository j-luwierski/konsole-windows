/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <QCoreApplication>
#include <QStringList>
#include <QUuid>
#include <QVariant>
namespace Konsole::FileRemote {
// Never put pairing keys in profile arguments, command logs or session layouts.
// Both libraries access the same application object; snapshots live until exit.
inline QStringList connectionArguments(const QByteArray &json)
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QByteArray property = QByteArray("konsoleFileRemote.") + id.toLatin1();
    QCoreApplication::instance()->setProperty(property.constData(), json);
    return {QStringLiteral("--konsole-file-remote"), id};
}
inline QByteArray connectionSnapshot(const QString &id)
{
    if (QUuid(id).isNull()) { return {}; }
    const QByteArray property = QByteArray("konsoleFileRemote.") + id.toLatin1();
    return QCoreApplication::instance()->property(property.constData()).toByteArray();
}
}
