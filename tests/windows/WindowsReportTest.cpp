/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/windows/QtReport.h"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QtTest>

class WindowsReportTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void launcherPathsAndReports()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString root = temporary.filePath(QString::fromUtf8("portable żółć with spaces"));
        const QString logs = temporary.filePath(QStringLiteral("reports-profile"));
        QVERIFY(QDir().mkpath(logs));
        QVERIFY(QDir().mkpath(root));
        const QString launcher = QDir(root).filePath(QStringLiteral("Start-Konsole.exe"));
        QVERIFY(QFile::copy(qEnvironmentVariable("KONSOLE_TEST_LAUNCHER"), launcher));
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("LOCALAPPDATA"), logs);
        environment.insert(QStringLiteral("USERPROFILE"), temporary.filePath(QStringLiteral("unavailable-home")));
        environment.insert(QStringLiteral("KONSOLE_LAUNCH_TEST_ROOT"), QDir::toNativeSeparators(root));
        environment.insert(QStringLiteral("QT_PLUGIN_PATH"), QStringLiteral("Z:/foreign-qt"));
        environment.remove(QStringLiteral("KONSOLE_REPORT_PID"));
        environment.remove(QStringLiteral("KONSOLE_REPORT_FILE"));
        QString target = launcher;
        QString callerDirectory = QDir::tempPath();
        const auto run = [&](const QStringList &arguments) {
            QProcess process; process.setProcessEnvironment(environment);
            process.setWorkingDirectory(callerDirectory);
            process.start(target, arguments);
            if (!process.waitForStarted(10000) || !process.waitForFinished(15000)) { return -1; }
            return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -2;
        };
        const auto reportText = [&] {
            QByteArray result;
            const QDir directory(QDir(logs).filePath(QStringLiteral("Konsole/reports")));
            for (const QString &name : directory.entryList({QStringLiteral("*.log")}, QDir::Files)) {
                QFile file(directory.filePath(name)); if (file.open(QIODevice::ReadOnly)) { result += file.readAll(); }
            }
            return result;
        };
        QCOMPARE(run({QStringLiteral("--no-error-dialog"), QStringLiteral("--check-portable")}), 2);
        QVERIFY(reportText().contains("missing or unavailable portable file"));
        const QStringList files = {
            QStringLiteral("bin/konsole.exe"), QStringLiteral("bin/konsoleapp.dll"), QStringLiteral("bin/konsoleprivate.dll"),
            QStringLiteral("bin/Qt6Core.dll"), QStringLiteral("bin/Qt6Gui.dll"), QStringLiteral("bin/Qt6Widgets.dll"),
            QStringLiteral("bin/Qt6Concurrent.dll"), QStringLiteral("bin/platforms/qwindows.dll"),
            QStringLiteral("bin/konsole-file-host.exe"), QStringLiteral("bin/Start-RemoteHost.exe"), QStringLiteral("bin/Start-RemoteHost.ps1"),
            QStringLiteral("bin/KonsoleCompletion.ps1"), QStringLiteral("powershell/pwsh.exe"), QStringLiteral("powershell/coreclr.dll"),
            QStringLiteral("powershell/Modules/PSReadLine/PSReadLine.psd1"),
            QStringLiteral("powershell/Modules/PSReadLine/Microsoft.PowerShell.PSReadLine.dll"),
            QStringLiteral("file/file.exe"), QStringLiteral("file/magic.mgc")};
        for (const QString &relative : files) {
            const QString path = QDir(root).filePath(relative);
            QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
            QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        }
        const QString terminal = QDir(root).filePath(QStringLiteral("bin/konsole.exe"));
        QVERIFY(QFile::remove(terminal));
        QVERIFY(QFile::copy(qEnvironmentVariable("KONSOLE_TEST_LAUNCH_PROBE"), terminal));
        QCOMPARE(run({QStringLiteral("--no-error-dialog"), QStringLiteral("--check-portable")}), 0);
        QVERIFY(reportText().contains("portable validation] passed"));
        QCOMPARE(run({QStringLiteral("--no-error-dialog"), QStringLiteral("--launcher-check")}), 0);
        // The CLI probe exits zero only when --help is forwarded intact.
        QCOMPARE(run({QStringLiteral("--no-error-dialog"), QStringLiteral("--help")}), 0);
        // Its ordinary exit status 3 must produce a launcher error/report.
        QCOMPARE(run({QStringLiteral("--no-error-dialog")}), 7);
        QVERIFY(reportText().contains("child exit status] 3 (0x00000003)"));
        QCOMPARE(run({QStringLiteral("--no-error-dialog"), QStringLiteral("--launcher-crash")}), 7);
        QVERIFY(reportText().contains("unhandled exception"));
        QVERIFY(reportText().contains("exception=0xc0000005"));
        QVERIFY(reportText().contains(QDir::toNativeSeparators(root).toUtf8()));
        QVERIFY(!reportText().contains("Z:/foreign-qt"));
        const QString remoteLauncher = QDir(root).filePath(QStringLiteral("Start-RemoteHost.exe"));
        QVERIFY(QFile::copy(qEnvironmentVariable("KONSOLE_TEST_REMOTE_LAUNCHER"), remoteLauncher));
        const QString host = QDir(root).filePath(QStringLiteral("bin/konsole-file-host.exe"));
        QVERIFY(QFile::remove(host));
        QVERIFY(QFile::copy(qEnvironmentVariable("KONSOLE_TEST_LAUNCH_PROBE"), host));
        target = remoteLauncher; callerDirectory = root;
        QCOMPARE(run({QStringLiteral("--no-error-dialog"), QStringLiteral("--launcher-check")}), 0);
        QCOMPARE(run({QStringLiteral("--no-error-dialog"), QStringLiteral("--help")}), 0);
        QCOMPARE(run({QStringLiteral("--no-error-dialog"), QStringLiteral("--launcher-paths"),
                      QStringLiteral("--root"), QStringLiteral("."), QStringLiteral("--cwd"), QStringLiteral("."),
                      QStringLiteral("--key-file"), QStringLiteral("pairing key.txt")}), 0);
        QCOMPARE(run({QStringLiteral("--no-error-dialog"), QStringLiteral("--root"), QStringLiteral("missing-exchange")}), 8);
        QVERIFY(reportText().contains("unavailable remote directory"));
        QVERIFY(QFile::remove(QDir(root).filePath(QStringLiteral("powershell/pwsh.exe"))));
        QCOMPARE(run({QStringLiteral("--no-error-dialog"), QStringLiteral("--check-portable")}), 2);
    }
    void qtWarningsExcludeMessageData()
    {
        Konsole::WindowsReport::initializeQt(L"report-test");
        qWarning("private-terminal-sample-and-pairing-key-must-not-be-in-report");
        QMessageLogger(nullptr, 0, nullptr, "qt.qpa.input").warning("private-input-sample-must-not-be-in-report");
        Konsole::WindowsReport::event(L"test error", L"Known safe diagnostic reason");
        QFile file(QString::fromStdWString(Konsole::WindowsReport::state().path));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray contents = file.readAll();
        QVERIFY(contents.contains("Qt message"));
        QVERIFY(contents.contains("Known safe diagnostic reason"));
        QVERIFY(!contents.contains("private-terminal-sample-and-pairing-key"));
        QVERIFY(!contents.contains("private-input-sample"));
    }
};
QTEST_GUILESS_MAIN(WindowsReportTest)
#include "WindowsReportTest.moc"
