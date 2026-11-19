/* SPDX-License-Identifier: MIT */
#include "../../src/ptyqt/windowsprocessutils.h"
#include "../../src/ptyqt/windowsshellintegration.h"
#include <QtTest>
#include "TerminalTestOutput.h"

class WindowsProcessTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void interleavedTerminalReports()
    {
        const QByteArray wire("P\x1b]7;file:///C:/work\x07" "E32+ \x1b[93mexecutable\x1b[m");
        QCOMPARE(terminalTestText(wire), QByteArray("PE32+ executable"));
        QCOMPARE(terminalTestText(QByteArray("P\x1b]7;file:///C:/work\x1b\\NG image data")), QByteArray("PNG image data"));
    }
    void quoting()
    {
        using WindowsProcessUtils::quoteArgument;
        QCOMPARE(quoteArgument(QStringLiteral("/d")), QStringLiteral("/d"));
        QCOMPARE(quoteArgument(QString()), QStringLiteral("\"\""));
        QCOMPARE(quoteArgument(QStringLiteral("a b")), QStringLiteral("\"a b\""));
        QCOMPARE(quoteArgument(QStringLiteral("a\"b")), QStringLiteral("\"a\\\"b\""));
        QCOMPARE(quoteArgument(QStringLiteral("C:\\a b\\")), QStringLiteral("\"C:\\a b\\\\\""));
        QCOMPARE(quoteArgument(QStringLiteral("a\\\"b")), QStringLiteral("\"a\\\\\\\"b\""));
    }
    void powerShellInitialization()
    {
        const auto arguments = WindowsShellIntegration::arguments();
        QVERIFY(arguments.contains(QStringLiteral("-NoExit")));
        // Leave space for executable path and test/setup statements below the Win32 limit.
        QVERIFY(arguments.constLast().size() < 31000);
        const QByteArray bytes = QByteArray::fromBase64(arguments.constLast().toLatin1());
        QStringDecoder decoder(QStringDecoder::Utf16LE);
        const QString decoded = decoder(bytes);
        QCOMPARE(decoded, WindowsShellIntegration::initializationScript());
        QVERIFY(decoded.contains(QStringLiteral("-Chord Tab -Function Complete")));
        QVERIFY(decoded.contains(QStringLiteral("-Chord Ctrl+r -Function ReverseSearchHistory")));
    }
    void relocatedPortableShell()
    {
        QTemporaryDir portable;
        QVERIFY(portable.isValid());
        QDir root(portable.path());
        QVERIFY(root.mkpath(QStringLiteral("bin")));
        QVERIFY(root.mkpath(QStringLiteral("powershell/Modules/PSReadLine")));
        const auto bin = root.filePath(QStringLiteral("bin"));
        QVERIFY(WindowsShellIntegration::bundledPowerShell(bin).isEmpty());
        QFile executable(root.filePath(QStringLiteral("powershell/pwsh.exe")));
        QVERIFY(executable.open(QIODevice::WriteOnly));
        executable.close();
        QVERIFY(WindowsShellIntegration::bundledPowerShell(bin).isEmpty());
        QFile module(root.filePath(QStringLiteral("powershell/Modules/PSReadLine/PSReadLine.psd1")));
        QVERIFY(module.open(QIODevice::WriteOnly));
        module.close();
        QCOMPARE(QDir::fromNativeSeparators(WindowsShellIntegration::bundledPowerShell(bin)), executable.fileName());
    }
    void environment()
    {
        qputenv("KONSOLE_TEST_INHERITED", "parent");
        const auto block = WindowsProcessUtils::environmentBlock({QStringLiteral("KONSOLE_TEST_OVERRIDE=a=b"),
                                                                 QStringLiteral("KONSOLE_TEST_INHERITED=child")});
        QVERIFY(block.size() >= 2);
        QCOMPARE(block.back(), L'\0');
        QCOMPARE(block[block.size() - 2], L'\0');
        QStringList entries;
        const wchar_t *entry = block.data();
        while (*entry) {
            const auto value = QString::fromWCharArray(entry);
            entries.append(value);
            entry += value.toStdWString().size() + 1;
        }
        QVERIFY(entries.contains(QStringLiteral("KONSOLE_TEST_INHERITED=child")));
        QVERIFY(entries.contains(QStringLiteral("KONSOLE_TEST_OVERRIDE=a=b")));
        QVERIFY(!entries.contains(QStringLiteral("KONSOLE_TEST_INHERITED=parent")));
        qunsetenv("KONSOLE_TEST_INHERITED");
    }
};
QTEST_GUILESS_MAIN(WindowsProcessTest)
#include "WindowsProcessTest.moc"
