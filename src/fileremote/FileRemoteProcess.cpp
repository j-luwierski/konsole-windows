/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "FileRemoteProcess.h"
#include "FileRemoteConfig.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUuid>
#include <cstring>
using namespace Konsole::FileRemote;
qint64 InputBuffer::readData(char *target, qint64 count)
{
    const qint64 size = qMin(count, qint64(_data.size()));
    std::memcpy(target, _data.constData(), size_t(size)); _data.remove(0, size); return size;
}
Process::Process()
{
    connect(&_timer, &QTimer::timeout, this, [this] { poll(); });
}
Process::~Process()
{
    kill();
    if (QCoreApplication::instance() && !_connectionId.isEmpty()) {
        const QByteArray name = QByteArray("konsoleFileRemoteProcess.") + _connectionId.toLatin1();
        auto processes = QCoreApplication::instance()->property(name.constData()).toList();
        for (auto it = processes.begin(); it != processes.end();) {
            if (it->value<QObject *>() == this) { it = processes.erase(it); } else { ++it; }
        }
        QCoreApplication::instance()->setProperty(name.constData(), processes);
    }
}
bool Process::startProcess(const QString &, const QStringList &arguments, const QString &, QStringList, qint16 cols, qint16 rows)
{
    if (m_pid || _finished || arguments.size() != 2 || arguments[0] != QStringLiteral("--konsole-file-remote")) { return false; }
    const auto config = QJsonDocument::fromJson(connectionSnapshot(arguments[1])).object();
    const QString root = config.value(QStringLiteral("root")).toString();
    const QString host = config.value(QStringLiteral("host")).toString();
    const QByteArray key = QByteArray::fromHex(config.value(QStringLiteral("key")).toString().toLatin1());
    if (!QDir(root).exists() || !Channel::validId(host) || key.size() != 32) {
        m_lastError = QStringLiteral("Invalid shared folder, host ID or pairing key"); return false;
    }
    const QString hostPath = QDir(root).filePath(host);
    const QString sessionsPath = QDir(hostPath).filePath(QStringLiteral("sessions"));
    if (!QDir(sessionsPath).exists() || QFileInfo(hostPath).isSymLink() || QFileInfo(sessionsPath).isSymLink()) {
        m_lastError = QStringLiteral("Remote host folder unavailable; start the host program first"); return false;
    }
    _sessionDirectory = QDir(sessionsPath).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkdir(_sessionDirectory)) { m_lastError = QStringLiteral("Cannot create session directory"); return false; }
    _timeout = qBound(5000, config.value(QStringLiteral("timeout")).toInt(30000), 600000);
    _out = std::make_unique<Channel>(_sessionDirectory, key, QStringLiteral("in"));
    _in = std::make_unique<Channel>(_sessionDirectory, key, QStringLiteral("out"));
    QJsonObject request{{QStringLiteral("version"), 1}, {QStringLiteral("host"), host},
        {QStringLiteral("session"), QFileInfo(_sessionDirectory).fileName()},
        {QStringLiteral("created"), QString::number(QDateTime::currentMSecsSinceEpoch())},
        {QStringLiteral("cols"), qBound(1, int(cols), 1000)}, {QStringLiteral("rows"), qBound(1, int(rows), 1000)}};
    if (!Channel::writeSigned(QDir(_sessionDirectory).filePath(QStringLiteral("request.json")), request, key, m_lastError)) { return false; }
    _connectionId = arguments[1];
    const QByteArray registryName = QByteArray("konsoleFileRemoteProcess.") + _connectionId.toLatin1();
    auto processes = QCoreApplication::instance()->property(registryName.constData()).toList();
    processes.append(QVariant::fromValue(static_cast<QObject *>(this)));
    QCoreApplication::instance()->setProperty(registryName.constData(), processes);
    m_size = {cols, rows}; m_pid = 1; _lastPeer.start(); _lastHeartbeat.start();
    _timer.start(qBound(20, config.value(QStringLiteral("poll")).toInt(50), 5000));
    QTimer::singleShot(0, this, [this] { _buffer.append(QByteArray("\r\n[Konsole: waiting for shared-folder host...]\r\n")); });
    Q_EMIT started(); return true;
}
bool Process::send(const QJsonObject &message)
{
    if (!_out || _finished) { return false; }
    if (!_out->send(message)) { finish(1, _out->error()); return false; }
    return true;
}
qint64 Process::write(const char *data, int size)
{
    if (!m_pid || size < 0) { return -1; }
    for (int offset = 0; offset < size; offset += 32768) {
        const QByteArray part(data + offset, qMin(32768, size - offset));
        if (!send({{QStringLiteral("type"), QStringLiteral("input")}, {QStringLiteral("data"), QString::fromLatin1(part.toBase64())}})) { return -1; }
    }
    return size;
}
bool Process::resize(qint16 cols, qint16 rows)
{
    m_size = {cols, rows};
    return !m_pid || send({{QStringLiteral("type"), QStringLiteral("resize")}, {QStringLiteral("cols"), qBound(1, int(cols), 1000)},
        {QStringLiteral("rows"), qBound(1, int(rows), 1000)}});
}
bool Process::kill()
{
    if (!m_pid || _finished) { return true; }
    send({{QStringLiteral("type"), QStringLiteral("close")}});
    finish(0); return true;
}
void Process::finish(int code, const QString &reason)
{
    if (_finished) { return; }
    _finished = true; _timer.stop(); m_pid = 0; m_exitCode = code;
    if (!reason.isEmpty()) {
        m_lastError = reason;
        _buffer.append(QStringLiteral("\r\n[Konsole remote: %1]\r\n").arg(reason).toUtf8());
    }
    const auto requests = std::exchange(_fileRequests, {});
    for (const auto &request : requests) {
        if (request.receiver) { request.callback({{}, QStringLiteral("Remote connection closed")}); }
    }
    Q_EMIT exited();
}
void Process::poll()
{
    if (_finished) { return; }
    for (int count = 0; count < 64; ++count) {
        QJsonObject packet;
        if (!_in->receive(packet)) { if (!_in->error().isEmpty()) { finish(1, _in->error()); } break; }
        _lastPeer.restart();
        const QString type = packet.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("accepted")) { _accepted = true; _fileRpc = packet.value(QStringLiteral("fileRpc")).toInt() == 1; }
        else if (type == QStringLiteral("file-response")) {
            const QString id = packet.value(QStringLiteral("id")).toString();
            auto request = _fileRequests.find(id);
            if (request == _fileRequests.end()) { continue; }
            request->elapsed.restart();
            request->bytes += QByteArray::fromBase64(packet.value(QStringLiteral("data")).toString().toLatin1());
            if (request->bytes.size() > request->limit * 6 + 1024 * 1024) {
                const auto copy = request.value(); _fileRequests.erase(request);
                if (copy.receiver) { copy.callback({{}, QStringLiteral("Remote file response exceeds size limit")}); }
            } else if (packet.value(QStringLiteral("final")).toBool()) {
                const auto copy = request.value(); _fileRequests.erase(request);
                const auto doc = QJsonDocument::fromJson(copy.bytes);
                if (copy.receiver) {
                    if (!doc.isObject()) { copy.callback({{}, QStringLiteral("Invalid remote file response")}); }
                    else {
                        const auto result = doc.object();
                        copy.callback({result.value(QStringLiteral("text")).toString(), result.value(QStringLiteral("error")).toString(),
                            result.value(QStringLiteral("binary")).toBool(), result.value(QStringLiteral("metadata")).toString()});
                    }
                }
            }
        }
        else if (type == QStringLiteral("output")) { _buffer.append(QByteArray::fromBase64(packet.value(QStringLiteral("data")).toString().toLatin1())); }
        else if (type == QStringLiteral("exit")) { finish(packet.value(QStringLiteral("code")).toInt(1), packet.value(QStringLiteral("error")).toString()); return; }
    }
    if (_finished) { return; }
    const auto ids = _fileRequests.keys();
    for (const auto &id : ids) {
        const auto request = _fileRequests.value(id);
        if (!request.receiver || request.elapsed.elapsed() > qMax(30000, _timeout)) {
            _fileRequests.remove(id);
            if (request.receiver) { request.callback({{}, QStringLiteral("Remote file request timed out")}); }
        }
    }
    if (_lastPeer.elapsed() > _timeout) {
        // Mark a pending request cancelled, so a late host cannot start a shell.
        send({{QStringLiteral("type"), QStringLiteral("close")}});
        finish(1, _accepted ? QStringLiteral("Host heartbeat timed out") : QStringLiteral("Connection timed out (check host, folder and pairing key)")); return;
    }
    if (_lastHeartbeat.elapsed() >= 1000) { send({{QStringLiteral("type"), QStringLiteral("heartbeat")}}); _lastHeartbeat.restart(); }
}

void Process::requestFile(const QStringList &arguments, const QString &path, qint64 limit,
                          bool showInfo, bool textOnly, QObject *receiver, FileCallback callback)
{
    const int index = arguments.indexOf(QStringLiteral("--konsole-file-remote"));
    Process *process = nullptr;
    if (index >= 0 && index + 1 < arguments.size()) {
        const QByteArray name = QByteArray("konsoleFileRemoteProcess.") + arguments[index + 1].toLatin1();
        const auto processes = QCoreApplication::instance()->property(name.constData()).toList();
        for (const auto &entry : processes) {
            auto *candidate = dynamic_cast<Process *>(entry.value<QObject *>());
            if (candidate && !candidate->_finished && candidate->_fileRpc) { process = candidate; break; }
        }
    }
    if (!process || process->_finished || !process->_fileRpc) {
        callback({{}, QStringLiteral("Remote file access unavailable. Update and restart both host and client with portable 0.6.0 or newer.")}); return;
    }
    if (process->_fileRequests.size() >= 4) { callback({{}, QStringLiteral("Too many pending remote file requests")}); return; }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    FileRequest request{receiver, std::move(callback), {}, qBound(qint64(1), limit, qint64(256) * 1024 * 1024), {}};
    request.elapsed.start(); process->_fileRequests.insert(id, request);
    process->send({{QStringLiteral("type"), QStringLiteral("file-request")}, {QStringLiteral("id"), id},
        {QStringLiteral("path"), path}, {QStringLiteral("limit"), QString::number(request.limit)},
        {QStringLiteral("showInfo"), showInfo}, {QStringLiteral("textOnly"), textOnly}});
}
