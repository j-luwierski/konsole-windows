/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "FileRemoteHost.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>
using namespace Konsole::FileRemote;
Host::Host(HostOptions options) : _options(std::move(options))
{
    connect(&_timer, &QTimer::timeout, this, [this] { poll(); });
}
Host::~Host()
{
    _timer.stop();
    for (auto &s : _sessions) { finish(*s, QStringLiteral("Host stopped")); }
}
bool Host::start(QString &error)
{
    if (!Channel::validId(_options.host) || _options.key.size() != 32 || !QDir(_options.root).exists()
        || _options.shell.isEmpty() || _options.ledgerDirectory.isEmpty()) {
        error = QStringLiteral("Invalid host options (existing root, host ID, local ledger, shell and 32-byte key required)"); return false;
    }
    const QString hostPath = QDir(_options.root).filePath(_options.host);
    _sessionsPath = QDir(hostPath).filePath(QStringLiteral("sessions"));
    if (QFileInfo(hostPath).isSymLink() || QFileInfo(_sessionsPath).isSymLink()
        || !QDir().mkpath(_sessionsPath) || !QDir().mkpath(_options.ledgerDirectory)) {
        error = QStringLiteral("Cannot create host folders"); return false;
    }
    _lock = std::make_unique<QLockFile>(QDir(hostPath).filePath(QStringLiteral("host.lock")));
    _lock->setStaleLockTime(0); // Never steal a lock held by another host.
    if (!_lock->tryLock(0)) { error = QStringLiteral("Host ID already in use, or stale host.lock requires manual removal after verifying the previous host stopped"); return false; }
    _timer.start(qBound(20, _options.pollMs, 5000)); return true;
}
void Host::acceptRequests()
{
    int active = 0;
    for (const auto &s : _sessions) { if (!s->finished) { ++active; } }
    const auto folders = QDir(_sessionsPath).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    // Bounded work per poll; old completed folders can be removed when offline.
    int examined = 0;
    for (const auto &folder : folders) {
        if (active >= _options.maximumSessions || ++examined > 4096) { break; }
        const QString id = folder.fileName();
        if (folder.isSymLink() || QUuid(id).isNull()) { continue; }
        const QString request = QDir(folder.absoluteFilePath()).filePath(QStringLiteral("request.json"));
        if (!QFileInfo::exists(request)) { continue; }
        const QString receipt = QDir(_options.ledgerDirectory).filePath(id);
        if (QFileInfo::exists(receipt)) { continue; }
        QJsonObject message; QString error;
        if (!Channel::readSigned(request, message, _options.key, error)) { continue; }
        const qint64 age = QDateTime::currentMSecsSinceEpoch() - message.value(QStringLiteral("created")).toString().toLongLong();
        if (message.value(QStringLiteral("version")).toInt() != 1 || message.value(QStringLiteral("host")).toString() != _options.host
            || message.value(QStringLiteral("session")).toString() != id || age < -30000 || age > 120000) { continue; }
        // Local durable receipt survives host restarts, even if share files are replayed.
        QSaveFile seen(receipt);
        if (!seen.open(QIODevice::WriteOnly) || seen.write("accepted\n") != 9 || !seen.commit()) { continue; }
        if (!QFile::remove(request)) { continue; }
        auto session = std::make_unique<Session>();
        session->input = std::make_unique<Channel>(folder.absoluteFilePath(), _options.key, QStringLiteral("in"));
        session->output = std::make_unique<Channel>(folder.absoluteFilePath(), _options.key, QStringLiteral("out"));
        session->process = std::make_unique<ConPtyProcess>();
        Session *s = session.get();
        // Respect close/timeout before starting a late request.
        bool cancelled = false;
        int cols = qBound(1, message.value(QStringLiteral("cols")).toInt(80), 1000);
        int rows = qBound(1, message.value(QStringLiteral("rows")).toInt(24), 1000);
        for (int n = 0; n < 64; ++n) {
            QJsonObject queued;
            if (!s->input->receive(queued)) { if (!s->input->error().isEmpty()) { cancelled = true; } break; }
            const QString type = queued.value(QStringLiteral("type")).toString();
            if (type == QStringLiteral("close")) { cancelled = true; }
            else if (type == QStringLiteral("resize")) {
                cols = qBound(1, queued.value(QStringLiteral("cols")).toInt(80), 1000);
                rows = qBound(1, queued.value(QStringLiteral("rows")).toInt(24), 1000);
            }
            else if (type == QStringLiteral("input")) { s->pending += QByteArray::fromBase64(queued.value(QStringLiteral("data")).toString().toLatin1()); }
        }
        if (cancelled) { continue; }
        const QByteArray initialInput = std::move(s->pending); s->pending.clear();
        connect(s->process->notifier(), &QIODevice::readyRead, this, [this, s] {
            const QByteArray output = s->process->readAll();
            if (s->pending.size() + output.size() > 16 * 1024 * 1024) { finish(*s, QStringLiteral("Output queue exceeds 16 MiB")); return; }
            s->pending += output; flush(*s);
        });
        connect(s->process.get(), &IPtyProcess::exited, this, [this, s] {
            if (!s->finished && !s->exiting) {
                s->pending += s->process->readAll(); s->exitCode = s->process->exitCode();
                s->exiting = true; flush(*s);
            }
        });
        s->lastPeer.start(); s->heartbeat.start();
        if (!s->output->send({{QStringLiteral("type"), QStringLiteral("accepted")}})) { continue; }
        _sessions.push_back(std::move(session)); ++active;
        if (!s->process->startProcess(_options.shell, _options.arguments, _options.workingDirectory, {}, qint16(cols), qint16(rows))) {
            finish(*s, s->process->lastError()); continue;
        }
        if (!initialInput.isEmpty()) { s->process->write(initialInput.constData(), int(initialInput.size())); }
    }
}
void Host::flush(Session &s)
{
    if (s.finished) { return; }
    for (int i = 0; i < 32 && !s.pending.isEmpty(); ++i) {
        const QByteArray chunk = s.pending.left(32768);
        if (!s.output->send({{QStringLiteral("type"), QStringLiteral("output")}, {QStringLiteral("data"), QString::fromLatin1(chunk.toBase64())}})) {
            finish(s, s.output->error()); return;
        }
        s.pending.remove(0, chunk.size());
    }
    if (s.exiting && s.pending.isEmpty()) {
        s.output->send({{QStringLiteral("type"), QStringLiteral("exit")}, {QStringLiteral("code"), s.exitCode}, {QStringLiteral("error"), s.error}});
        s.finished = true;
    }
}
void Host::finish(Session &s, const QString &error)
{
    if (s.finished) { return; }
    s.exiting = true; s.error = error;
    // Mark before kill, which may synchronously emit exited.
    s.finished = true;
    s.process->kill();
    s.output->send({{QStringLiteral("type"), QStringLiteral("exit")}, {QStringLiteral("code"), error.isEmpty() ? 0 : 1}, {QStringLiteral("error"), error}});
}
void Host::poll()
{
    for (auto &session : _sessions) {
        Session &s = *session;
        if (s.finished) { continue; }
        for (int i = 0; i < 64; ++i) {
            QJsonObject packet;
            if (!s.input->receive(packet)) { if (!s.input->error().isEmpty()) { finish(s, s.input->error()); } break; }
            s.lastPeer.restart();
            const QString type = packet.value(QStringLiteral("type")).toString();
            if (type == QStringLiteral("close")) { finish(s); break; }
            if (type == QStringLiteral("input")) {
                const QByteArray data = QByteArray::fromBase64(packet.value(QStringLiteral("data")).toString().toLatin1());
                if (s.process->write(data.constData(), int(data.size())) != data.size()) { finish(s, QStringLiteral("Cannot write remote console input")); break; }
            } else if (type == QStringLiteral("resize")) {
                if (!s.process->resize(qint16(qBound(1, packet.value(QStringLiteral("cols")).toInt(), 1000)),
                    qint16(qBound(1, packet.value(QStringLiteral("rows")).toInt(), 1000)))) { finish(s, QStringLiteral("Cannot resize remote console")); break; }
            }
        }
        if (s.finished) { continue; }
        if (s.lastPeer.elapsed() > qBound(5000, _options.timeoutMs, 600000)) { finish(s, QStringLiteral("Client heartbeat timed out")); continue; }
        flush(s);
        if (!s.finished && s.heartbeat.elapsed() >= 1000) {
            if (!s.output->send({{QStringLiteral("type"), QStringLiteral("heartbeat")}})) { finish(s, s.output->error()); }
            s.heartbeat.restart();
        }
    }
    _sessions.erase(std::remove_if(_sessions.begin(), _sessions.end(), [](const auto &s) { return s->finished; }), _sessions.end());
    acceptRequests();
}
