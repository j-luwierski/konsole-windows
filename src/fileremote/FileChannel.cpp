/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "FileChannel.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
using namespace Konsole::FileRemote;
Channel::Channel(QString directory, QByteArray key, QString direction)
    : _directory(std::move(directory)), _direction(std::move(direction)), _key(std::move(key)) {}
bool Channel::validId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[a-zA-Z0-9_-]{1,64}$"));
    return re.match(id).hasMatch();
}
QByteArray Channel::generateKey()
{
    QByteArray key(32, '\0');
    for (int i = 0; i < key.size(); ++i) { key[i] = char(QRandomGenerator::system()->generate() & 255); }
    return key;
}
bool Channel::writeSigned(const QString &path, const QJsonObject &message, const QByteArray &key, QString &error)
{
    error.clear();
    if (key.size() != 32) { error = QStringLiteral("Invalid pairing key (expected 64 hexadecimal characters)"); return false; }
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (payload.size() > 128 * 1024) { error = QStringLiteral("Message exceeds 128 KiB"); return false; }
    const QByteArray mac = QMessageAuthenticationCode::hash(payload, key, QCryptographicHash::Sha256).toHex();
    const QByteArray wire = QJsonDocument(QJsonObject{{QStringLiteral("payload"), QString::fromLatin1(payload.toBase64())},
        {QStringLiteral("hmac"), QString::fromLatin1(mac)}}).toJson(QJsonDocument::Compact);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(wire) != wire.size() || !file.commit()) {
        error = file.errorString(); return false;
    }
    return true;
}
bool Channel::readSigned(const QString &path, QJsonObject &message, const QByteArray &key, QString &error)
{
    error.clear();
    if (QFileInfo(path).isSymLink()) { error = QStringLiteral("Symbolic link rejected"); return false; }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.size() > 192 * 1024) { error = QStringLiteral("Oversized packet"); return false; }
    const auto envelope = QJsonDocument::fromJson(file.readAll()).object();
    const QByteArray payload = QByteArray::fromBase64(envelope.value(QStringLiteral("payload")).toString().toLatin1());
    const QByteArray supplied = envelope.value(QStringLiteral("hmac")).toString().toLatin1();
    const QByteArray expected = QMessageAuthenticationCode::hash(payload, key, QCryptographicHash::Sha256).toHex();
    unsigned int difference = supplied.size() == expected.size() && key.size() == 32 ? 0U : 1U;
    for (int i = 0; i < expected.size(); ++i) { difference |= unsigned(expected[i] ^ (i < supplied.size() ? supplied[i] : 0)); }
    if (difference) { error = QStringLiteral("Pairing key / message authentication failed"); return false; }
    const auto document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) { error = QStringLiteral("Invalid packet JSON"); return false; }
    message = document.object(); return true;
}
QString Channel::packetPath(quint64 sequence) const
{
    return QDir(_directory).filePath(_direction + QLatin1Char('-') + QString::number(sequence).rightJustified(16, QLatin1Char('0')) + QStringLiteral(".json"));
}
bool Channel::send(const QJsonObject &message)
{
    _error.clear();
    QDir dir(_directory);
    if (QFileInfo(_directory).isSymLink() || !dir.exists()) { _error = QStringLiteral("Session directory unavailable"); return false; }
    if (dir.entryList({QStringLiteral("*.json")}, QDir::Files).size() >= 1024) { _error = QStringLiteral("Shared-folder queue full; peer stopped reading"); return false; }
    QJsonObject packet = message;
    packet.insert(QStringLiteral("version"), 1);
    packet.insert(QStringLiteral("session"), QFileInfo(_directory).fileName());
    packet.insert(QStringLiteral("direction"), _direction);
    packet.insert(QStringLiteral("sequence"), QString::number(_sent + 1));
    const QString path = packetPath(_sent + 1);
    if (QFileInfo::exists(path)) { _error = QStringLiteral("Sequence collision"); return false; }
    if (!writeSigned(path, packet, _key, _error)) { return false; }
    ++_sent; return true;
}
bool Channel::receive(QJsonObject &message)
{
    _error.clear();
    const QString path = packetPath(_received + 1);
    if (!QFileInfo::exists(path)) { return false; }
    if (!readSigned(path, message, _key, _error)) { return false; }
    if (message.value(QStringLiteral("version")).toInt() != 1
        || message.value(QStringLiteral("session")).toString() != QFileInfo(_directory).fileName()
        || message.value(QStringLiteral("direction")).toString() != _direction
        || message.value(QStringLiteral("sequence")).toString() != QString::number(_received + 1)) {
        _error = QStringLiteral("Wrong version, session, direction or sequence"); return false;
    }
    // Deleting only after verification acknowledges delivery and bounds disk use.
    if (!QFile::remove(path)) { _error = QStringLiteral("Cannot acknowledge packet (delete permission required)"); return false; }
    ++_received; return true;
}

bool Channel::hasCapacity(int limit) const
{
    return QDir(_directory).entryList({QStringLiteral("*.json")}, QDir::Files).size() < limit;
}
