/* SPDX-License-Identifier: MIT */
#include "../../src/ptyqt/conptyprocess.h"
#include "../../src/ptyqt/windowsshellintegration.h"
#include <QSignalSpy>
#include <QStandardPaths>
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
    void powerShellCompletionAndHistory_data()
    {
        QTest::addColumn<QString>("shell");
        const auto windowsPowerShell = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
        QVERIFY2(!windowsPowerShell.isEmpty(), "Windows PowerShell must be available for the default profile");
        QTest::newRow("WindowsPowerShell") << windowsPowerShell;
        const auto bundled = qEnvironmentVariable("KONSOLE_TEST_BUNDLED_PWSH");
        if (!bundled.isEmpty()) {
            QVERIFY(QFileInfo::exists(bundled));
            QTest::newRow("BundledPowerShell7") << bundled;
        }
        const auto pwsh = QStandardPaths::findExecutable(QStringLiteral("pwsh.exe"));
        if (!pwsh.isEmpty()) {
            QTest::newRow("PowerShell7") << pwsh;
        }
    }
    void powerShellCompletionAndHistory()
    {
        QFETCH(QString, shell);
        QTemporaryDir workingDirectory(QDir::tempPath() + QStringLiteral("/Konsole preview cwd XXXXXX"));
        QVERIFY(workingDirectory.isValid());
        ConPtyProcess process;
        QByteArray output;
        connect(process.notifier(), &QIODevice::readyRead, this, [&] { output += process.readAll(); });
        const QString script = WindowsShellIntegration::initializationScript() + QStringLiteral(R"PS(
if ((Get-PSReadLineKeyHandler | Where-Object Key -eq 'Tab').Function -ne 'Complete') { throw 'Tab binding not initialized' }
if ((Get-PSReadLineKeyHandler | Where-Object Key -eq 'Ctrl+r').Function -ne 'ReverseSearchHistory') { throw 'Ctrl+R binding not initialized' }
$expectedReadLine = Join-Path $PSHOME 'Modules/PSReadLine'
if ((Test-Path (Join-Path $expectedReadLine 'PSReadLine.psd1')) -and (Get-Module PSReadLine).ModuleBase -ne $expectedReadLine) { throw 'PSReadLine not loaded from shell distribution' }
Write-Output 'konsole-readline-ready'
)PS");
        QVERIFY2(process.startProcess(shell, WindowsShellIntegration::arguments(script), {}, {QStringLiteral("KONSOLE_PREVIEW_TEST_PATH=") + workingDirectory.path()}, 120, 40), qPrintable(process.lastError()));
        QTRY_VERIFY2_WITH_TIMEOUT(output.contains("konsole-readline-ready"), output.constData(), 20000);
        // The expected result is not present verbatim in the input, so merely
        // echoing the keystrokes cannot make this test pass.
        const QByteArray complete("Write-Out\t ('konsole-'+'completed-result')\r");
        QCOMPARE(process.write(complete.constData(), complete.size()), qint64(complete.size()));
        QTRY_VERIFY_WITH_TIMEOUT(output.contains("konsole-completed-result"), 15000);
        const auto previousResults = output.count("konsole-completed-result");
        const QByteArray search("\x12" "completed-result\r");
        QCOMPARE(process.write(search.constData(), search.size()), qint64(search.size()));
        QTRY_VERIFY_WITH_TIMEOUT(output.count("konsole-completed-result") > previousResults, 15000);
        const QByteArray changeDirectory("Set-Location -LiteralPath $env:KONSOLE_PREVIEW_TEST_PATH\r");
        QCOMPARE(process.write(changeDirectory.constData(), changeDirectory.size()), qint64(changeDirectory.size()));
        const QByteArray directoryReport = QByteArray("\x1b]7;") + QUrl::fromLocalFile(workingDirectory.path()).toEncoded();
        QTRY_VERIFY2_WITH_TIMEOUT(output.contains(directoryReport), output.constData(), 15000);
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
        QSignalSpy exited(&process, &IPtyProcess::exited);
        process.kill();
        QCOMPARE(exited.count(), 1);
        process.kill();
        QCOMPARE(exited.count(), 1);
    }
};
QTEST_GUILESS_MAIN(ConPtyTest)
#include "ConPtyTest.moc"
