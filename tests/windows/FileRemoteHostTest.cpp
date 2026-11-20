#include "TerminalTestOutput.h"
#include <QScopeGuard>
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/fileremote/FileRemoteHost.h"
#include "../../src/fileremote/FileRemoteProcess.h"
#include "../../src/fileremote/FileRemoteConfig.h"
#include "../../src/ptyqt/windowsshellintegration.h"
#include <QDir>
#include <QDateTime>
#include <QUuid>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>
using namespace Konsole::FileRemote;
using Konsole::TextPreviewContent;
class FileRemoteHostTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void executableAndLocalKey()
    {
        QString executable = qEnvironmentVariable("KONSOLE_TEST_HOST_EXE");
        if (executable.isEmpty()) { executable = qEnvironmentVariable("KONSOLE_TEST_HOST_BUILD"); }
        QVERIFY(QFileInfo::exists(executable));
        QProcess process;
        if (!qEnvironmentVariable("KONSOLE_TEST_HOST_EXE").isEmpty()) {
            auto env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("PATH"), QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("System32")));
            process.setProcessEnvironment(env);
        }
        process.start(executable, {QStringLiteral("--help")});
        QVERIFY(process.waitForFinished(10000)); QCOMPARE(process.exitCode(), 0);
        QVERIFY(process.readAllStandardOutput().contains("--root"));
        QTemporaryDir local;
        const QString keyPath = local.filePath(QStringLiteral("pairing.key"));
        process.start(executable, {QStringLiteral("--generate-key"), keyPath});
        QVERIFY(process.waitForFinished(10000)); QCOMPARE(process.exitCode(), 0);
        QFile key(keyPath); QVERIFY(key.open(QIODevice::ReadOnly)); QCOMPARE(key.readAll().trimmed().size(), 64); key.close();
        process.start(executable, {QStringLiteral("--generate-key"), keyPath});
        QVERIFY(process.waitForFinished(10000)); QVERIFY(process.exitCode() != 0);
    }
    void interactivePowerShellOverFiles_data()
    {
        QTest::addColumn<QString>("shell");
        QString shell = qEnvironmentVariable("KONSOLE_TEST_BUNDLED_PWSH");
        if (shell.isEmpty()) { shell = QStandardPaths::findExecutable(QStringLiteral("pwsh.exe")); }
        QVERIFY(!shell.isEmpty()); QTest::newRow("PowerShell7") << shell;
    }
    void interactivePowerShellOverFiles()
    {
        QFETCH(QString, shell);
        QTemporaryDir root(QDir::tempPath() + QStringLiteral("/Konsole remote exchange XXXXXX"));
        QTemporaryDir ledger; QTemporaryDir cwd;
        HostOptions options; options.root = root.path(); options.host = QStringLiteral("host1");
        options.key = Channel::generateKey(); options.shell = shell; options.workingDirectory = cwd.path();
        options.ledgerDirectory = ledger.path(); options.pollMs = 20;
        options.fileExecutable = qEnvironmentVariable("KONSOLE_TEST_FILE_EXE");
        options.arguments = WindowsShellIntegration::arguments(WindowsShellIntegration::initializationScript()
            + QStringLiteral("\nif ((Get-Command Set-PSReadLineOption).Parameters.ContainsKey('PredictionSource')) { Set-PSReadLineOption -PredictionSource None }; Write-Output ('FILE-REMOTE-'+'READY')\n"));
        Host host(options); QString error; QVERIFY2(host.start(error), qPrintable(error));
        Host duplicate(options); QVERIFY(!duplicate.start(error));
        const QByteArray config = QJsonDocument(QJsonObject{{QStringLiteral("root"), root.path()}, {QStringLiteral("host"), options.host},
            {QStringLiteral("key"), QString::fromLatin1(options.key.toHex())}, {QStringLiteral("poll"), 20}}).toJson();
        const QStringList connectionArgumentsForTest = connectionArguments(config);
        Process client; QByteArray output;
        const auto captureOutput = qScopeGuard([&] { QFile log(QStringLiteral("remote-stream.log")); if (log.open(QIODevice::WriteOnly | QIODevice::Append)) { log.write(output); } });
        connect(client.notifier(), &QIODevice::readyRead, this, [&] { output += client.readAll(); });
        QVERIFY2(client.startProcess({}, connectionArgumentsForTest, {}, {}, 120, 40), qPrintable(client.lastError()));
        QTRY_VERIFY2_WITH_TIMEOUT(terminalTestText(output).contains("FILE-REMOTE-READY"), output.constData(), 25000);
        const auto send = [&](const QByteArray &input) { return client.write(input.constData(), int(input.size())) == input.size(); };
        QVERIFY(send(QByteArray("Write-Out\t ('FILE-REMOTE-'+'COMPLETED')\r")));
        QTRY_VERIFY2_WITH_TIMEOUT(terminalTestText(output).contains("FILE-REMOTE-COMPLETED"), output.constData(), 15000);
        const auto count = terminalTestText(output).count("FILE-REMOTE-COMPLETED");
        QVERIFY(send(QByteArray("\x12" "COMPLETED\r")));
        QTRY_VERIFY_WITH_TIMEOUT(terminalTestText(output).count("FILE-REMOTE-COMPLETED") > count, 15000);
        // Native child process: preserve process output and status through the transport.
        QVERIFY(send(QByteArray("cmd /d /c \"exit 23\"; Write-Output ('FILE-REMOTE-EXIT-' + $LASTEXITCODE)\r")));
        QTRY_VERIFY2_WITH_TIMEOUT(terminalTestText(output).contains("FILE-REMOTE-EXIT-23"), output.constData(), 15000);
        QVERIFY(client.resize(137, 43));
        QVERIFY(send(QByteArray("Write-Output ('FILE-REMOTE-SIZE-' + [Console]::WindowWidth)\r")));
        QTRY_VERIFY2_WITH_TIMEOUT(terminalTestText(output).contains("FILE-REMOTE-SIZE-137"), output.constData(), 15000);
        QVERIFY(send(QByteArray("Write-Output ('FILE-REMOTE-'+'SLEEPING'); Start-Sleep -Seconds 60\r")));
        QTRY_VERIFY_WITH_TIMEOUT(terminalTestText(output).contains("FILE-REMOTE-SLEEPING"), 10000);
        QTest::qWait(200);
        const QByteArray promptReport = QByteArray("\x1b]7;") + QUrl::fromLocalFile(cwd.path()).toEncoded();
        const auto promptsBeforeInterrupt = output.count(promptReport);
        QVERIFY(send(QByteArray(1, '\3')));
        // Ctrl+C completes asynchronously. Wait for the integration's next
        // prompt report instead of typing while PowerShell is still unwinding.
        QTRY_VERIFY2_WITH_TIMEOUT(output.count(promptReport) > promptsBeforeInterrupt, output.constData(), 10000);
        QVERIFY(send(QByteArray("Write-Output ('FILE-REMOTE-'+'INTERRUPTED')\r")));
        QTRY_VERIFY2_WITH_TIMEOUT(terminalTestText(output).contains("FILE-REMOTE-INTERRUPTED"), output.constData(), 10000);
        // Large UTF-16 file: response crosses many authenticated packets.
        const QString remotePath = cwd.filePath(QString::fromUtf8("zdalny plik żółć.ini"));
        const QString contents = QString::fromUtf8("[zdalny]\nwartość=żółć 😀\n") + QString(150000, QLatin1Char('x'));
        QFile remoteFile(remotePath); QVERIFY(remoteFile.open(QIODevice::WriteOnly));
        QStringEncoder encoder(QStringConverter::Utf16LE);
        remoteFile.write(QByteArray("\xff\xfe", 2) + QByteArray(encoder(contents))); remoteFile.close();
        int completed = 0; TextPreviewContent result;
        const auto request = [&](const QString &path, bool textOnly, qint64 limit = 1024 * 1024) {
            Process::requestFile(connectionArgumentsForTest, path, limit, true, textOnly, this,
                [&](const TextPreviewContent &value) { result = value; ++completed; });
        };
        request(remotePath, false);
        QTRY_COMPARE_WITH_TIMEOUT(completed, 1, 15000); QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.text, contents); QVERIFY(result.metadata.contains(QStringLiteral("Owner:")));
        QVERIFY(result.metadata.contains(QStringLiteral("Created:"))); QVERIFY(result.metadata.contains(QStringLiteral("Full DACL")));
        request(remotePath, true); QTRY_COMPARE_WITH_TIMEOUT(completed, 2, 15000);
        QCOMPARE(result.text, contents); QVERIFY(result.metadata.isEmpty());
        request(remotePath, true, 32); QTRY_COMPARE_WITH_TIMEOUT(completed, 3, 15000); QVERIFY(!result.error.isEmpty());
        const QString imagePath = cwd.filePath(QString::fromUtf8("binary image żółć 😀.png"));
        QFile image(imagePath); QVERIFY(image.open(QIODevice::WriteOnly));
        image.write(QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==")); image.close();
        request(imagePath, false); QTRY_COMPARE_WITH_TIMEOUT(completed, 4, 15000);
        QVERIFY(result.error.isEmpty()); QVERIFY(result.text.contains(QStringLiteral("file:")));
        if (!options.fileExecutable.isEmpty()) { QVERIFY2(result.text.contains(QStringLiteral("PNG image data")), qPrintable(result.text)); }
        request(imagePath, true); QTRY_COMPARE_WITH_TIMEOUT(completed, 5, 15000); QVERIFY(!result.error.isEmpty());
        request(cwd.filePath(QStringLiteral("missing.txt")), false); QTRY_COMPARE_WITH_TIMEOUT(completed, 6, 15000);
        QVERIFY(result.text.contains(QStringLiteral("unavailable")) || !result.error.isEmpty());
        if (!qEnvironmentVariable("KONSOLE_TEST_BUNDLED_PWSH").isEmpty()) {
            QVERIFY(send(QByteArray("file --version; Write-Output ('REMOTE-FILE-' + $LASTEXITCODE)\r")));
            QTRY_VERIFY2_WITH_TIMEOUT(terminalTestText(output).contains("5.48"), output.constData(), 15000);
            QTRY_VERIFY_WITH_TIMEOUT(terminalTestText(output).contains("REMOTE-FILE-0"), 15000);
            const QString quotedImage = QString(imagePath).replace(QLatin1Char('\''), QStringLiteral("''"));
            QVERIFY(send(QStringLiteral("file --brief -- '%1'; Write-Output ('REMOTE-MAGIC-' + $LASTEXITCODE)\r").arg(quotedImage).toUtf8()));
            QTRY_VERIFY2_WITH_TIMEOUT(terminalTestText(output).contains("PNG image data"), output.constData(), 15000);
            QTRY_VERIFY_WITH_TIMEOUT(terminalTestText(output).contains("REMOTE-MAGIC-0"), 15000);
        }
        if (!qEnvironmentVariable("KONSOLE_TEST_BUNDLED_PWSH").isEmpty()) {
            const QString probe = qEnvironmentVariable("KONSOLE_TEST_COMPLETION_EXE");
            QVERIFY(QFileInfo::exists(probe));
            const QString helpCheck = QStringLiteral("$line=\"& '%1' --be\"; if ([System.Management.Automation.CommandCompletion]::CompleteInput($line,$line.Length,$null).CompletionMatches.CompletionText -contains '--beta') { Write-Output ('NEW-COMMAND-'+'COMPLETED') }\r").arg(QString(probe).replace(QLatin1Char('\''), QStringLiteral("''")));
            QVERIFY(send(helpCheck.toUtf8()));
            QTRY_VERIFY2_WITH_TIMEOUT(terminalTestText(output).contains("NEW-COMMAND-COMPLETED"), output.constData(), 15000);
            const QByteArray gitCheck("if (!(Get-Module posh-git) -and $global:KonsoleGitCompletionData.poshGit -ne '1.1.0') { throw 'Bundled Git completer not loaded' }; git init --quiet; 'completion' | Set-Content -LiteralPath git-tab-test.txt; $line='git commit --am'; if ([System.Management.Automation.CommandCompletion]::CompleteInput($line,$line.Length,$null).CompletionMatches.CompletionText -contains '--amend') { Write-Output ('GIT-COMPLETION-'+'READY') }\r");
            QVERIFY(send(gitCheck));
            QTRY_VERIFY2_WITH_TIMEOUT(terminalTestText(output).contains("GIT-COMPLETION-READY"), output.constData(), 15000);
            QVERIFY(send(QByteArray("git stat\t --porcelain; Write-Output ('GIT-TAB-EXIT-' + $LASTEXITCODE)\r")));
            QTRY_VERIFY2_WITH_TIMEOUT(terminalTestText(output).contains("GIT-TAB-EXIT-0"), output.constData(), 15000);
        }
        QSignalSpy exited(&client, &IPtyProcess::exited);
        QVERIFY(send(QByteArray("exit 7\r")));
        QTRY_COMPARE_WITH_TIMEOUT(exited.count(), 1, 15000); QCOMPARE(client.exitCode(), 7);
        // Files are the only data path; consumed input/output packets are deleted.
        const auto folders = QDir(root.filePath(QStringLiteral("host1/sessions"))).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(folders.size(), 1);
        QVERIFY(!QFileInfo::exists(QDir(folders.first().absoluteFilePath()).filePath(QStringLiteral("request.json"))));
        QCOMPARE(QDir(ledger.path()).entryList(QDir::Files).size(), 1);
    }
    void replayAfterRestartDoesNotStartAnotherShell()
    {
        QTemporaryDir root, ledger, cwd;
        HostOptions options; options.root = root.path(); options.host = QStringLiteral("host1");
        options.key = Channel::generateKey(); options.shell = QStringLiteral("cmd.exe");
        options.arguments = {QStringLiteral("/d"), QStringLiteral("/c"), QStringLiteral("echo started > marker.txt")};
        options.workingDirectory = cwd.path(); options.ledgerDirectory = ledger.path(); options.pollMs = 20;
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString directory = QDir(root.path()).filePath(QStringLiteral("host1/sessions/") + id);
        const QString request = QDir(directory).filePath(QStringLiteral("request.json"));
        const QJsonObject message{{QStringLiteral("version"), 1}, {QStringLiteral("host"), options.host},
            {QStringLiteral("session"), id}, {QStringLiteral("created"), QString::number(QDateTime::currentMSecsSinceEpoch())},
            {QStringLiteral("cols"), 80}, {QStringLiteral("rows"), 24}};
        QString error;
        {
            Host host(options); QVERIFY(host.start(error)); QVERIFY(QDir().mkpath(directory));
            QVERIFY(Channel::writeSigned(request, message, options.key, error));
            QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(cwd.filePath(QStringLiteral("marker.txt"))), 10000);
        }
        QVERIFY(QFile::remove(cwd.filePath(QStringLiteral("marker.txt"))));
        QVERIFY(Channel::writeSigned(request, message, options.key, error));
        Host restarted(options); QVERIFY(restarted.start(error)); QTest::qWait(500);
        QVERIFY(!QFileInfo::exists(cwd.filePath(QStringLiteral("marker.txt"))));
        QCOMPARE(QDir(ledger.path()).entryList(QDir::Files).size(), 1);
    }
    void wrongKeyNeverStartsShell()
    {
        QTemporaryDir root, ledger, cwd;
        HostOptions options; options.root = root.path(); options.host = QStringLiteral("host1");
        options.key = Channel::generateKey(); options.shell = QStringLiteral("cmd.exe");
        options.arguments = {QStringLiteral("/c"), QStringLiteral("echo SHOULD_NOT_START > marker.txt")};
        options.workingDirectory = cwd.path(); options.ledgerDirectory = ledger.path(); options.pollMs = 20;
        Host host(options); QString error; QVERIFY(host.start(error));
        const auto config = QJsonDocument(QJsonObject{{QStringLiteral("root"), root.path()}, {QStringLiteral("host"), options.host},
            {QStringLiteral("key"), QString::fromLatin1(Channel::generateKey().toHex())}, {QStringLiteral("timeout"), 5000},
            {QStringLiteral("poll"), 20}}).toJson();
        Process client; QSignalSpy exited(&client, &IPtyProcess::exited);
        QVERIFY(client.startProcess({}, connectionArguments(config), {}, {}, 80, 24));
        QTRY_COMPARE_WITH_TIMEOUT(exited.count(), 1, 8000);
        QVERIFY(!QFileInfo::exists(cwd.filePath(QStringLiteral("marker.txt"))));
        QCOMPARE(QDir(ledger.path()).entryList(QDir::Files).size(), 0);
    }
};
QTEST_GUILESS_MAIN(FileRemoteHostTest)
#include "FileRemoteHostTest.moc"
