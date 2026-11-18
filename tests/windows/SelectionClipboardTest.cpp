/* SPDX-License-Identifier: MIT */
#include "../../src/terminalDisplay/SelectionClipboard.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QtTest>

class SelectionClipboardTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void selectionIsIndependentFromSystemClipboard()
    {
        auto *clipboard = QGuiApplication::clipboard();
        const auto previous = clipboard->text();
        clipboard->setText(QStringLiteral("system clipboard"));
        const QString selected = QString::fromUtf8("Zażółć gęślą jaźń — 😀\ndrugi wiersz");
        Konsole::SelectionClipboard::setText(selected);
        QCOMPARE(clipboard->text(), QStringLiteral("system clipboard"));
        QCOMPARE(Konsole::SelectionClipboard::text(), selected);
        clipboard->setText(QStringLiteral("copied in another application"));
        QCOMPARE(Konsole::SelectionClipboard::text(), selected);
        // Clearing a view's selection must not discard the last selected text.
        Konsole::SelectionClipboard::setText({});
        QCOMPARE(Konsole::SelectionClipboard::text(), selected);
        Konsole::SelectionClipboard::setText(QStringLiteral("selection from another tab"));
        QCOMPARE(Konsole::SelectionClipboard::text(), QStringLiteral("selection from another tab"));
        QCOMPARE(clipboard->text(), QStringLiteral("copied in another application"));
        clipboard->setText(previous);
    }
};
QTEST_MAIN(SelectionClipboardTest)
#include "SelectionClipboardTest.moc"
