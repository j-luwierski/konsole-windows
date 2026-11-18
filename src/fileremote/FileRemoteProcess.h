/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../ptyqt/iptyprocess.h"
#include "FileChannel.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QTimer>
#include <memory>
namespace Konsole::FileRemote {
class InputBuffer : public QIODevice {
public:
    InputBuffer() { open(QIODevice::ReadOnly); }
    void append(const QByteArray &data) { _data += data; Q_EMIT readyRead(); }
    qint64 bytesAvailable() const override { return _data.size() + QIODevice::bytesAvailable(); }
    bool isSequential() const override { return true; }
protected:
    qint64 readData(char *target, qint64 count) override;
    qint64 writeData(const char *, qint64) override { return -1; }
private:
    QByteArray _data;
};
class Process : public IPtyProcess {
public:
    Process();
    ~Process() override;
    bool startProcess(const QString &, const QStringList &, const QString &, QStringList, qint16, qint16) override;
    bool resize(qint16 cols, qint16 rows) override;
    bool kill() override;
    PtyType type() override { return AutoPty; }
    QString dumpDebugInfo() override { return QStringLiteral("Authenticated shared-folder terminal"); }
    QIODevice *notifier() override { return &_buffer; }
    QByteArray readAll() override { return _buffer.readAll(); }
    qint64 write(const char *data, int size) override;
    bool isAvailable() override { return true; }
    void moveToThread(QThread *thread) override { QObject::moveToThread(thread); }
    int processList() const override { return m_pid ? 2 : 0; }
private:
    void poll();
    void finish(int exitCode, const QString &reason = {});
    bool send(const QJsonObject &message);
    QTimer _timer;
    InputBuffer _buffer;
    std::unique_ptr<Channel> _out, _in;
    QString _sessionDirectory;
    QElapsedTimer _lastPeer, _lastHeartbeat;
    int _timeout = 30000;
    bool _accepted = false, _finished = false;
};
}
