/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "FileChannel.h"
#include "../ptyqt/conptyprocess.h"
#include <QElapsedTimer>
#include <QLockFile>
#include <QObject>
#include <QTimer>
#include <memory>
#include <vector>
namespace Konsole::FileRemote {
struct HostOptions {
    QString root, host, shell, workingDirectory, ledgerDirectory, fileExecutable;
    QStringList arguments;
    QByteArray key;
    int pollMs = 50, timeoutMs = 30000, maximumSessions = 4;
};
class Host : public QObject {
public:
    explicit Host(HostOptions options);
    ~Host() override;
    bool start(QString &error);
private:
    struct Session {
        std::unique_ptr<Channel> input, output;
        std::unique_ptr<ConPtyProcess> process;
        QElapsedTimer lastPeer, heartbeat;
        QByteArray pending;
        QByteArray fileReply;
        QString fileRequestId;
        qsizetype fileOffset = 0;
        bool fileBusy = false;
        bool finished = false, exiting = false;
        int exitCode = 0;
        QString error;
    };
    void poll();
    void acceptRequests();
    void finish(Session &session, const QString &error = {});
    void flush(Session &session);
    void readFile(Session &session, const QJsonObject &packet);
    void flushFile(Session &session);
    HostOptions _options;
    QTimer _timer;
    std::unique_ptr<QLockFile> _lock;
    std::vector<std::unique_ptr<Session>> _sessions;
    QString _sessionsPath;
};
}
