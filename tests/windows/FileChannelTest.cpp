/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/fileremote/FileChannel.h"
#include "../../src/fileremote/FileRemoteProcess.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace Konsole::FileRemote;
class FileChannelTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void orderedBinaryAndAuthentication()
    {
        QTemporaryDir dir;
        const QByteArray key = Channel::generateKey();
        QCOMPARE(key.size(), 32);
        Channel sender(dir.path(), key, QStringLiteral("in")), receiver(dir.path(), key, QStringLiteral("in"));
        const QByteArray content = QByteArray::fromHex("00031b5b33316d0d0aff");
        for (int i = 0; i < 20; ++i) {
            QVERIFY(sender.send({{QStringLiteral("type"), QStringLiteral("input")}, {QStringLiteral("n"), i},
                {QStringLiteral("data"), QString::fromLatin1(content.toBase64())}}));
        }
        for (int i = 0; i < 20; ++i) {
            QJsonObject packet; QVERIFY(receiver.receive(packet));
            QCOMPARE(packet.value(QStringLiteral("n")).toInt(), i);
            QCOMPARE(QByteArray::fromBase64(packet.value(QStringLiteral("data")).toString().toLatin1()), content);
        }
        QJsonObject packet; QVERIFY(!receiver.receive(packet)); QVERIFY(receiver.error().isEmpty());
        QVERIFY(sender.send({{QStringLiteral("type"), QStringLiteral("heartbeat")}}));
        Channel wrongKey(dir.path(), Channel::generateKey(), QStringLiteral("in"));
        // Start a new sequence-1 channel in a separate folder for authentication tests.
        QTemporaryDir other; QString error;
        const QString path = other.filePath(QStringLiteral("packet.json"));
        QVERIFY(Channel::writeSigned(path, {{QStringLiteral("value"), 42}}, key, error));
        QVERIFY(!Channel::readSigned(path, packet, Channel::generateKey(), error));
        QVERIFY(error.contains(QStringLiteral("authentication")));
        QVERIFY(Channel::readSigned(path, packet, key, error)); QCOMPARE(packet.value(QStringLiteral("value")).toInt(), 42);
        QFile file(path); QVERIFY(file.open(QIODevice::ReadWrite)); QByteArray bytes = file.readAll();
        bytes.replace("payload", "tampered"); file.resize(0); file.write(bytes); file.close();
        QVERIFY(!Channel::readSigned(path, packet, key, error));
        QVERIFY(!Channel::validId(QStringLiteral("../host"))); QVERIFY(Channel::validId(QStringLiteral("host-1_2")));
    }
    void replayAndDirectionRejected()
    {
        QTemporaryDir dir; const auto key = Channel::generateKey();
        Channel channel(dir.path(), key, QStringLiteral("out"));
        const QJsonObject message{{QStringLiteral("version"), 1}, {QStringLiteral("session"), QFileInfo(dir.path()).fileName()},
            {QStringLiteral("direction"), QStringLiteral("in")}, {QStringLiteral("sequence"), QStringLiteral("1")}};
        QString error; QVERIFY(Channel::writeSigned(dir.filePath(QStringLiteral("out-0000000000000001.json")), message, key, error));
        QJsonObject received; QVERIFY(!channel.receive(received)); QVERIFY(!channel.error().isEmpty());
    }
    void clientRoundTripAndExit()
    {
        QTemporaryDir root;
        QVERIFY(QDir(root.path()).mkpath(QStringLiteral("host1/sessions")));
        const auto key = Channel::generateKey();
        const auto config = QJsonDocument(QJsonObject{{QStringLiteral("root"), root.path()}, {QStringLiteral("host"), QStringLiteral("host1")},
            {QStringLiteral("key"), QString::fromLatin1(key.toHex())}, {QStringLiteral("poll"), 20}}).toJson(QJsonDocument::Compact).toBase64();
        Process client; QByteArray output;
        connect(client.notifier(), &QIODevice::readyRead, this, [&] { output += client.readAll(); });
        QSignalSpy exited(&client, &IPtyProcess::exited);
        QVERIFY(client.startProcess({}, {QStringLiteral("--konsole-file-remote"), QString::fromLatin1(config)}, {}, {}, 80, 24));
        const QDir sessions(root.filePath(QStringLiteral("host1/sessions")));
        const auto names = sessions.entryList(QDir::Dirs | QDir::NoDotAndDotDot); QCOMPARE(names.size(), 1);
        const QString directory = sessions.filePath(names.first());
        QJsonObject request; QString error;
        QVERIFY(Channel::readSigned(QDir(directory).filePath(QStringLiteral("request.json")), request, key, error));
        QCOMPARE(request.value(QStringLiteral("cols")).toInt(), 80);
        Channel input(directory, key, QStringLiteral("in")), remoteOutput(directory, key, QStringLiteral("out"));
        QVERIFY(remoteOutput.send({{QStringLiteral("type"), QStringLiteral("accepted")}}));
        QCOMPARE(client.write("\3", 1), qint64(1));
        QJsonObject packet; QVERIFY(input.receive(packet));
        QCOMPARE(QByteArray::fromBase64(packet.value(QStringLiteral("data")).toString().toLatin1()), QByteArray(1, '\3'));
        QVERIFY(client.resize(132, 45)); QVERIFY(input.receive(packet)); QCOMPARE(packet.value(QStringLiteral("cols")).toInt(), 132);
        const QByteArray text = QString::fromUtf8("żółć 😀\r\n").toUtf8();
        QVERIFY(remoteOutput.send({{QStringLiteral("type"), QStringLiteral("output")}, {QStringLiteral("data"), QString::fromLatin1(text.toBase64())}}));
        QVERIFY(remoteOutput.send({{QStringLiteral("type"), QStringLiteral("exit")}, {QStringLiteral("code"), 17}}));
        QTRY_COMPARE_WITH_TIMEOUT(exited.count(), 1, 3000); QVERIFY(output.contains(text)); QCOMPARE(client.exitCode(), 17);
        client.kill(); QCOMPARE(exited.count(), 1);
    }
    void missingHostDoesNotStartLocalShell()
    {
        QTemporaryDir root;
        const auto config = QJsonDocument(QJsonObject{{QStringLiteral("root"), root.path()}, {QStringLiteral("host"), QStringLiteral("host1")},
            {QStringLiteral("key"), QString::fromLatin1(Channel::generateKey().toHex())}}).toJson().toBase64();
        Process client;
        QVERIFY(!client.startProcess({}, {QStringLiteral("--konsole-file-remote"), QString::fromLatin1(config)}, {}, {}, 80, 24));
        QCOMPARE(client.pid(), 0); QVERIFY(!client.lastError().isEmpty());
    }
};
QTEST_GUILESS_MAIN(FileChannelTest)
#include "FileChannelTest.moc"
