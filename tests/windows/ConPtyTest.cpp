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
if ((Get-PSReadLineKeyHandler | Where-Object Key -eq 'Tab').Function -notin @('Complete','KonsoleCompleteOrAcceptSuggestion')) { throw 'Tab binding not initialized' }
if ((Get-PSReadLineKeyHandler | Where-Object Key -eq 'Ctrl+r').Function -ne 'ReverseSearchHistory') { throw 'Ctrl+R binding not initialized' }
$expectedReadLine = Join-Path $PSHOME 'Modules/PSReadLine'
if ((Test-Path (Join-Path $expectedReadLine 'PSReadLine.psd1')) -and (Get-Module PSReadLine).ModuleBase -ne $expectedReadLine) { throw 'PSReadLine not loaded from shell distribution' }
if ((Get-Command Set-PSReadLineOption).Parameters.ContainsKey('PredictionSource')) { Set-PSReadLineOption -PredictionSource None }
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
        if (shell == qEnvironmentVariable("KONSOLE_TEST_BUNDLED_PWSH")) {
            const QByteArray fileCommand("file --brief -- (Join-Path $PSHOME 'pwsh.exe'); Write-Output ('LOCAL-FILE-' + $LASTEXITCODE)\r");
            QCOMPARE(process.write(fileCommand.constData(), fileCommand.size()), qint64(fileCommand.size()));
            QTRY_VERIFY2_WITH_TIMEOUT(output.contains("PE32+ executable"), output.constData(), 15000);
            QTRY_VERIFY_WITH_TIMEOUT(output.contains("LOCAL-FILE-0"), 15000);
        }
        const QByteArray changeDirectory("Set-Location -LiteralPath $env:KONSOLE_PREVIEW_TEST_PATH\r");
        QCOMPARE(process.write(changeDirectory.constData(), changeDirectory.size()), qint64(changeDirectory.size()));
        const QByteArray directoryReport = QByteArray("\x1b]7;") + QUrl::fromLocalFile(workingDirectory.path()).toEncoded();
        QTRY_VERIFY2_WITH_TIMEOUT(output.contains(directoryReport), output.constData(), 15000);
        if (QFileInfo(shell).baseName().compare(QStringLiteral("pwsh"), Qt::CaseInsensitive) == 0) {
            QFile listedFile(workingDirectory.filePath(QString::fromUtf8("listing name # żółć.txt")));
            QVERIFY(listedFile.open(QIODevice::WriteOnly));
            listedFile.write("value=listing\n");
            listedFile.close();
            const QByteArray link = QByteArray("\x1b]8;;") + QUrl::fromLocalFile(listedFile.fileName()).toEncoded();
            for (const QByteArray command : {QByteArray("ls\r"), QByteArray("dir\r"), QByteArray("Get-ChildItem\r")}) {
                const auto count = output.count(link);
                QCOMPARE(process.write(command.constData(), command.size()), qint64(command.size()));
                QTRY_VERIFY2_WITH_TIMEOUT(output.count(link) > count, output.constData(), 15000);
            }
            const QByteArray objectCheck("Get-ChildItem -File | ForEach-Object { ('konsole-object-'+'type:') + $_.GetType().Name }\r");
            QCOMPARE(process.write(objectCheck.constData(), objectCheck.size()), qint64(objectCheck.size()));
            QTRY_VERIFY2_WITH_TIMEOUT(output.contains("konsole-object-type:FileInfo"), output.constData(), 15000);
        }
        if (shell == qEnvironmentVariable("KONSOLE_TEST_BUNDLED_PWSH")) {
            const QByteArray seed("Set-PSReadLineOption -PredictionSource History -PredictionViewStyle InlineView; Write-Output ('PREDICTION-'+'ACCEPTED-06')\r");
            QCOMPARE(process.write(seed.constData(), seed.size()), qint64(seed.size()));
            QTRY_VERIFY_WITH_TIMEOUT(output.contains("PREDICTION-ACCEPTED-06"), 10000);
            const auto predictionCount = output.count("PREDICTION-ACCEPTED-06");
            // Seed a standalone history line, then accept its visible gray tail by Tab.
            const QByteArray history("Write-Output ('PREDICTION-'+'ACCEPTED-06')\r");
            process.write(history.constData(), history.size());
            QTRY_VERIFY_WITH_TIMEOUT(output.count("PREDICTION-ACCEPTED-06") > predictionCount, 10000);
            const auto beforeTab = output.count("PREDICTION-ACCEPTED-06");
            const QByteArray prefix("Write-Output ('PREDICTION-"); process.write(prefix.constData(), prefix.size());
            QTest::qWait(500); process.write("\t", 1); QTest::qWait(100);
            QCOMPARE(output.count("PREDICTION-ACCEPTED-06"), beforeTab); // Tab never executes.
            process.write("\r", 1);
            QTRY_VERIFY2_WITH_TIMEOUT(output.count("PREDICTION-ACCEPTED-06") > beforeTab, output.constData(), 10000);
        }
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
