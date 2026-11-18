/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
namespace Konsole::FileRemote {
// Versioned, authenticated, ordered messages committed by atomic rename.
class Channel {
public:
    Channel(QString directory, QByteArray key, QString direction);
    bool send(const QJsonObject &message);
    // false + empty error means no next message yet.
    bool receive(QJsonObject &message);
    QString error() const { return _error; }
    static bool validId(const QString &id);
    static QByteArray generateKey();
    static bool writeSigned(const QString &path, const QJsonObject &message, const QByteArray &key, QString &error);
    static bool readSigned(const QString &path, QJsonObject &message, const QByteArray &key, QString &error);
private:
    QString packetPath(quint64 sequence) const;
    QString _directory, _direction, _error;
    QByteArray _key;
    quint64 _sent = 0, _received = 0;
};
}
