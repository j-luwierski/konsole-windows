/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/filterHotSpots/TextFilePreview.h"
#include "../../src/filterHotSpots/FileMetadata.h"
#include <QClipboard>
#include <QCursor>
#include <QFile>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStandardPaths>
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
    void binaryFormatsNeverAppearAsRawText()
    {
        QTemporaryDir dir;
        QFile pdf(dir.filePath(QStringLiteral("document.txt")));
        QVERIFY(pdf.open(QIODevice::WriteOnly)); pdf.write("%PDF-1.7\n1 0 obj\n<< /Type /Catalog >>\nendobj\n%%EOF\n"); pdf.close();
        const auto text = TextPreviewContent::readText(pdf.fileName(), 4096);
        QVERIFY(text.binary); QVERIFY(!text.error.isEmpty());
        const auto info = TextPreviewContent::read(pdf.fileName(), 4096, true);
        QVERIFY(info.error.isEmpty()); QVERIFY(info.text.contains(QStringLiteral("file:")));
        QVERIFY(info.metadata.contains(QStringLiteral("Modified:")));
        QVERIFY(!info.text.startsWith(QStringLiteral("%PDF")));
    }
    void remoteLoaderAndWholeFileCopy()
    {
        QWidget owner; owner.resize(300, 180); owner.show();
        QTest::qWait(30);
        auto *preview = TextFilePreview::forWidget(&owner);
        const QString text = QString::fromUtf8("[zdalny]\nwartość=żółć 😀\n") + QString(100000, QLatin1Char('x'));
        int requests = 0;
        TextFilePreview::Loader loader = [&](const QString &path, qint64, bool, bool textOnly, QObject *receiver, TextFilePreview::Completion done) {
            ++requests;
            QCOMPARE(path, QStringLiteral("Z:/remote-only/nonexistent-here.ini"));
            QTimer::singleShot(10, receiver, [done, textOnly, text] {
                done({text, {}, false, textOnly ? QString() : QStringLiteral("Owner: REMOTE\\user\nCreated: remote-date")});
            });
        };
        TextPreviewOptions options; options.delayMs = 100;
        const QPoint point = owner.mapToGlobal(QPoint(40, 40)); QCursor::setPos(point);
        preview->hoverFile(QStringLiteral("Z:/remote-only/nonexistent-here.ini"), point, options, loader, QStringLiteral("remote1"));
        QTRY_VERIFY_WITH_TIMEOUT(preview->popup()->isVisible(), 3000);
        QVERIFY(preview->editor()->toPlainText().startsWith(QStringLiteral("Owner: REMOTE")));
        QVERIFY(preview->editor()->toPlainText().endsWith(text));
        QApplication::clipboard()->setText(QStringLiteral("old"));
        TextFilePreview::copyFileContents(QStringLiteral("Z:/remote-only/nonexistent-here.ini"), 1024 * 1024, &owner, loader);
        QTRY_COMPARE_WITH_TIMEOUT(QApplication::clipboard()->text(), text, 3000);
        QCOMPARE(requests, 2); preview->dismiss();
    }
    void stationaryEventsDoNotCancelIdentification()
    {
        QWidget owner; owner.resize(300, 180); owner.show();
        QTest::qWait(30);
        auto *preview = TextFilePreview::forWidget(&owner);
        TextPreviewOptions options; options.delayMs = 100;
        const QPoint point = owner.mapToGlobal(QPoint(40, 40)); QCursor::setPos(point);
        int requests = 0;
        TextFilePreview::Loader loader = [&](const QString &, qint64, bool, bool, QObject *receiver, TextFilePreview::Completion done) {
            ++requests;
            QTimer::singleShot(300, receiver, [done] { done({QStringLiteral("file: PE32+ executable"), {}, false, QStringLiteral("Size: 123")}); });
        };
        QTimer events;
        connect(&events, &QTimer::timeout, &owner, [&] {
            preview->hoverFile(QStringLiteral("program.exe"), point, options, loader);
        });
        preview->hoverFile(QStringLiteral("program.exe"), point, options, loader);
        events.start(40);
        QTRY_VERIFY_WITH_TIMEOUT(preview->popup()->isVisible(), 2000);
        QCOMPARE(requests, 1);
        QVERIFY(preview->editor()->toPlainText().contains(QStringLiteral("PE32+")));
        events.stop(); preview->dismiss();
    }
    void fileIdentificationFallback()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QString::fromUtf8("typ pliku żółć 😀.data"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg=="));
        file.close();
        const auto disabled = TextPreviewContent::read(path, 4096, false);
        QVERIFY(disabled.binary);
        QVERIFY(!disabled.error.isEmpty());
        QString executable = qEnvironmentVariable("KONSOLE_TEST_FILE_EXE");
        if (executable.isEmpty()) { executable = TextPreviewContent::fileCommand(); }
        if (executable.isEmpty()) { QSKIP("Native file runtime is tested after assembling the portable ZIP"); }
        const auto identified = TextPreviewContent::read(path, 4096, true, executable);
        QVERIFY(identified.error.isEmpty());
        QVERIFY(!identified.binary);
        QVERIFY2(identified.text.contains(QStringLiteral("PNG image data")), qPrintable(identified.text));
        QVERIFY(identified.text.contains(QStringLiteral("file:")));
        QVERIFY(identified.text.contains(QStringLiteral("bytes")));
    }
    void copyEntireContentsWithoutPreviewMetadata()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QString::fromUtf8("cały plik żółć.ini"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QString contents = QString::fromUtf8("[ustawienia]\nwartość=żółć 😀\n") + QString(20000, QLatin1Char('x')) + QStringLiteral("\nEND\n");
        QStringEncoder encoder(QStringConverter::Utf16LE);
        file.write(QByteArray("\xff\xfe", 2) + QByteArray(encoder(contents)));
        file.close();
        QApplication::clipboard()->setText(QStringLiteral("previous clipboard"));
        TextFilePreview::copyFileContents(path, 1024 * 1024);
        QTRY_COMPARE_WITH_TIMEOUT(QApplication::clipboard()->text(), contents, 3000);
    }
    void metadataAndCleanClipboardPath()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QString::fromUtf8("config żółć.ini"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("name=value\n");
        file.close();
        const auto description = FileMetadata::describe(path);
        for (const auto &label : {QStringLiteral("Path:"), QStringLiteral("Size:"), QStringLiteral("Created:"),
                                  QStringLiteral("Modified:"), QStringLiteral("Owner:"), QStringLiteral("Permissions")}) {
            QVERIFY2(description.contains(label), qPrintable(description));
        }
#ifdef Q_OS_WIN
        QVERIFY2(description.contains(QStringLiteral("Primary group:")), qPrintable(description));
        QVERIFY2(description.contains(QStringLiteral("Full DACL (SDDL):")), qPrintable(description));
#else
        QVERIFY(description.contains(QStringLiteral("Group:")));
#endif
        QCOMPARE(FileMetadata::clipboardPath(path + QStringLiteral(":12:3")), QDir::toNativeSeparators(path));
        auto localUrl = QUrl::fromLocalFile(path);
        QCOMPARE(FileMetadata::localFilePath(localUrl), QDir::fromNativeSeparators(path));
        localUrl.setHost(QStringLiteral("localhost"));
        QCOMPARE(FileMetadata::localFilePath(localUrl), QDir::fromNativeSeparators(path));
        localUrl.setHost(QStringLiteral("remote.invalid"));
        QVERIFY(FileMetadata::localFilePath(localUrl).isEmpty());
        const auto content = TextPreviewContent::read(path, 1024);
        QCOMPARE(content.text, QStringLiteral("name=value\n"));
        QVERIFY(content.metadata.contains(QStringLiteral("Modified:")));
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
        QVERIFY(preview.editor()->toPlainText().endsWith(fullText));
        QVERIFY(preview.editor()->toPlainText().contains(QStringLiteral("Created:")));
        QVERIFY(preview.editor()->toPlainText().contains(QStringLiteral("Permissions")));
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
        QCOMPARE(QApplication::clipboard()->text(), preview.editor()->toPlainText());
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
