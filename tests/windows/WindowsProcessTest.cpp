/* SPDX-License-Identifier: MIT */
#include "../../src/ptyqt/windowsprocessutils.h"
#include "../../src/ptyqt/windowsshellintegration.h"
#include <QtTest>

class WindowsProcessTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
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
        const QByteArray bytes = QByteArray::fromBase64(arguments.constLast().toLatin1());
        QStringDecoder decoder(QStringDecoder::Utf16LE);
        const QString decoded = decoder(bytes);
        QCOMPARE(decoded, WindowsShellIntegration::initializationScript());
        QVERIFY(decoded.contains(QStringLiteral("-Chord Tab -Function Complete")));
        QVERIFY(decoded.contains(QStringLiteral("-Chord Ctrl+r -Function ReverseSearchHistory")));
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
