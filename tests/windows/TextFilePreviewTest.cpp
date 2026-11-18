/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/filterHotSpots/TextFilePreview.h"
#include <QClipboard>
#include <QCursor>
#include <QFile>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTemporaryDir>
#include <QtTest>

using namespace Konsole;
class TextFilePreviewTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void windowsPathRecognition()
    {
        const QRegularExpression pattern(TextPreviewContent::windowsPathPattern() + QStringLiteral("\"[^\"\\n]+\"|'[^'\\n]+'"));
        QVERIFY(pattern.isValid());
        for (const auto &path : {QStringLiteral("C:\\data\\file.ini"), QStringLiteral("C:/data/file.ini:23"),
                                 QStringLiteral(".\\config\\file.ini"), QStringLiteral("\"C:\\my files\\file.ini\"")}) {
            const auto match = pattern.match(QStringLiteral("prefix ") + path + QStringLiteral(" suffix"));
            QVERIFY2(match.hasMatch(), qPrintable(path));
            QCOMPARE(match.captured(), path);
        }
    }
    void contentAndEncoding()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("config without extension"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QString text = QString::fromUtf8("[sekcja]\nwartość=Zażółć gęślą jaźń 😀\n");
        file.write(text.toUtf8());
        file.close();
        auto content = TextPreviewContent::read(path, 1024);
        QVERIFY2(content.error.isEmpty(), qPrintable(content.error));
        QCOMPARE(content.text, text);
        QVERIFY(!TextPreviewContent::read(path, 8).error.isEmpty());
        QVERIFY(!TextPreviewContent::read(directory.path(), 1024).error.isEmpty());
        QVERIFY(!TextPreviewContent::read(path + QStringLiteral(".missing"), 1024).error.isEmpty());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QStringEncoder encoder(QStringConverter::Utf16LE);
        file.write(QByteArray("\xff\xfe", 2) + QByteArray(encoder(text)));
        file.close();
        content = TextPreviewContent::read(path, 1024);
        QVERIFY2(content.error.isEmpty(), qPrintable(content.error));
        QCOMPARE(content.text, text);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QByteArray("MZ\0\1\2\3", 6));
        file.close();
        QVERIFY(!TextPreviewContent::read(path, 1024).error.isEmpty());
        QCOMPARE(TextPreviewContent::pathWithoutLocation(QStringLiteral("C:\\files\\settings.ini:23:4")), QStringLiteral("C:\\files\\settings.ini"));
        QCOMPARE(TextPreviewContent::pathWithoutLocation(QStringLiteral("\"C:\\a b\\settings.ini\":23")), QStringLiteral("C:\\a b\\settings.ini"));
        QCOMPARE(TextPreviewContent::pathWithoutLocation(QStringLiteral("'/tmp/a b.ini'")), QStringLiteral("/tmp/a b.ini"));
    }
    void stationaryHoverInteractivePopupAndExit()
    {
        QTemporaryDir directory;
        QFile file(directory.filePath(QStringLiteral("example.ini")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QString fullText;
        for (int line = 0; line < 2000; ++line) {
            fullText += QStringLiteral("key%1=value%1\n").arg(line);
        }
        fullText += QStringLiteral("END OF ENTIRE FILE\n");
        file.write(fullText.toUtf8());
        file.close();
        QWidget owner;
        owner.resize(780, 580);
        owner.move(0, 0);
        owner.setMouseTracking(true);
        owner.show();
        QVERIFY(QTest::qWaitForWindowExposed(&owner));
        TextFilePreview preview(&owner);
        TextPreviewOptions options;
        options.delayMs = 180;
        options.leaveDelayMs = 150;
        auto anchor = owner.mapToGlobal(QPoint(20, 20));
        QCursor::setPos(anchor);
        preview.hoverFile(file.fileName(), anchor, options);
        QTest::qWait(100);
        QVERIFY(!preview.popup()->isVisible());
        anchor += QPoint(1, 0);
        QCursor::setPos(anchor);
        preview.hoverFile(file.fileName(), anchor, options);
        QTest::qWait(100);
        QVERIFY(!preview.popup()->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(preview.popup()->isVisible(), 3000);
        QVERIFY(preview.popup()->windowFlags().testFlag(Qt::FramelessWindowHint));
        QCOMPARE(preview.editor()->toPlainText(), fullText);
        QVERIFY(preview.editor()->isReadOnly());
        QCOMPARE(preview.editor()->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOn);
        QVERIFY(preview.editor()->verticalScrollBar()->maximum() > 0);
        preview.leavePath();
        QCursor::setPos(preview.editor()->viewport()->mapToGlobal(QPoint(40, 40)));
        QTest::mouseMove(preview.editor()->viewport(), QPoint(40, 40));
        QTest::qWait(250);
        QVERIFY(preview.popup()->isVisible());
        preview.editor()->verticalScrollBar()->setValue(preview.editor()->verticalScrollBar()->maximum());
        QVERIFY(preview.editor()->verticalScrollBar()->value() > 0);
        preview.editor()->selectAll();
        preview.editor()->copy();
        QCOMPARE(QApplication::clipboard()->text(), fullText);
        QCursor::setPos(anchor);
        QTest::mouseMove(&owner, owner.mapFromGlobal(anchor));
        preview.hoverFile(file.fileName(), anchor, options);
        QVERIFY(!preview.popup()->isVisible());
        preview.leavePath();
        QTRY_VERIFY_WITH_TIMEOUT(!preview.popup()->isVisible(), 1500);
        options.enabled = false;
        preview.hoverFile(file.fileName(), QCursor::pos(), options);
        QTest::qWait(250);
        QVERIFY(!preview.popup()->isVisible());
        options.enabled = true;
        preview.hoverFile(file.fileName(), QCursor::pos(), options);
        preview.leavePath();
        QTest::qWait(250);
        QVERIFY(!preview.popup()->isVisible());
    }
};
QTEST_MAIN(TextFilePreviewTest)
#include "TextFilePreviewTest.moc"
