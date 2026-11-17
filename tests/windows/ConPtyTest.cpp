/* SPDX-License-Identifier: MIT */
#include "../../src/ptyqt/conptyprocess.h"
#include <QSignalSpy>
#include <QtTest>

class ConPtyTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void failedStartAndCleanup()
    {
        ConPtyProcess process;
        QVERIFY(!process.startProcess(QStringLiteral("nonexistent-konsole-shell.exe"), {}, {}, {}, 80, 24));
        QVERIFY(!process.lastError().isEmpty());
        process.kill();
        process.kill();
    }
    void outputEnvironmentAndExit()
    {
        ConPtyProcess process;
        QVERIFY(process.isAvailable());
        QByteArray output;
        connect(process.notifier(), &QIODevice::readyRead, this, [&] { output += process.readAll(); });
        QSignalSpy exited(&process, &IPtyProcess::exited);
        QVERIFY2(process.startProcess(QStringLiteral("cmd.exe"),
                                      {QStringLiteral("/d"), QStringLiteral("/c"),
                                       QStringLiteral("echo %KONSOLE_TEST_VALUE% & exit /b 7")},
                                      {}, {QStringLiteral("KONSOLE_TEST_VALUE=konsole-conpty-ok")}, 80, 24),
                 qPrintable(process.lastError()));
        QTRY_COMPARE_WITH_TIMEOUT(exited.count(), 1, 15000);
        QVERIFY(output.contains("konsole-conpty-ok"));
        QCOMPARE(process.exitCode(), 7);
        QCOMPARE(process.pid(), 0);
        process.kill();
    }
    void inputAndResize()
    {
        ConPtyProcess process;
        QByteArray output;
        connect(process.notifier(), &QIODevice::readyRead, this, [&] { output += process.readAll(); });
        QVERIFY2(process.startProcess(QStringLiteral("cmd.exe"), {QStringLiteral("/d"), QStringLiteral("/q")}, {}, {}, 80, 24),
                 qPrintable(process.lastError()));
        QVERIFY(process.resize(120, 40));
        QCOMPARE(process.size().first, qint16(120));
        QVERIFY(!process.resize(0, 0));
        const QByteArray input("echo konsole-input-ok\r\n");
        QCOMPARE(process.write(input.constData(), input.size()), qint64(input.size()));
        QTRY_VERIFY_WITH_TIMEOUT(output.contains("konsole-input-ok"), 10000);
        process.kill();
        process.kill();
    }
};
QTEST_GUILESS_MAIN(ConPtyTest)
#include "ConPtyTest.moc"
