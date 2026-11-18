/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "RemoteTabDialog.h"
#include <QJsonObject>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
#include <KLocalizedString>
using namespace Konsole::FileRemote;
QByteArray Konsole::FileRemote::configureConnection(QWidget *parent, bool connectNow)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("FileRemote"));
    // Remember the pairing key in memory only, never in the shared folder/settings.
    static QString memoryKey;
    QDialog dialog(parent);
    dialog.setWindowTitle(i18n("Remote Tab via Shared Folder"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *intro = new QLabel(i18n("Start Start-RemoteHost.ps1 on the other computer first. Both computers need read, write and delete access to the same folder. Their local paths may differ."), &dialog);
    intro->setWordWrap(true); layout->addWidget(intro);
    auto *form = new QFormLayout;
    auto *folder = new QLineEdit(settings.value(QStringLiteral("root")).toString(), &dialog);
    auto *browse = new QPushButton(i18n("Browse…"), &dialog);
    auto *row = new QHBoxLayout; row->addWidget(folder); row->addWidget(browse);
    form->addRow(i18n("Exchange folder:"), row);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
        const QString selected = QFileDialog::getExistingDirectory(&dialog, i18n("Exchange folder"), folder->text());
        if (!selected.isEmpty()) { folder->setText(selected); }
    });
    auto *host = new QLineEdit(settings.value(QStringLiteral("host"), QStringLiteral("host1")).toString(), &dialog);
    form->addRow(i18n("Host ID:"), host);
    auto *key = new QLineEdit(memoryKey, &dialog); key->setEchoMode(QLineEdit::Password);
    key->setPlaceholderText(i18n("64 hexadecimal characters from the host"));
    form->addRow(i18n("Pairing key:"), key);
    auto *poll = new QSpinBox(&dialog); poll->setRange(20, 5000); poll->setSuffix(QStringLiteral(" ms"));
    poll->setValue(settings.value(QStringLiteral("poll"), 50).toInt());
    form->addRow(i18n("Folder polling interval:"), poll);
    auto *timeout = new QSpinBox(&dialog); timeout->setRange(5, 600); timeout->setSuffix(QStringLiteral(" s"));
    timeout->setValue(settings.value(QStringLiteral("timeout"), 30).toInt());
    form->addRow(i18n("Connection / heartbeat timeout:"), timeout);
    layout->addLayout(form);
    auto *note = new QLabel(i18n("The key stays in memory until Konsole closes. Messages are authenticated; terminal contents in the exchange folder are not encrypted. Use a folder accessible only to the intended users. This connection uses files only, with no TCP/SSH listener."), &dialog);
    note->setWordWrap(true); layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(connectNow ? i18n("Connect") : i18n("Save settings"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        static const QRegularExpression hex(QStringLiteral("^[a-fA-F0-9]{64}$"));
        if (!QDir(folder->text()).exists() || !QRegularExpression(QStringLiteral("^[a-zA-Z0-9_-]{1,64}$")).match(host->text()).hasMatch() || (connectNow && !hex.match(key->text().trimmed()).hasMatch())) {
            QMessageBox::warning(&dialog, i18n("Remote Tab"), i18n("Select an existing folder, a host ID using letters, digits, _ or -, and a valid pairing key.")); return;
        }
        dialog.accept();
    });
    dialog.resize(580, dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted) { return {}; }
    memoryKey = key->text().trimmed();
    const QString root = QDir(folder->text()).absolutePath();
    settings.setValue(QStringLiteral("root"), root); settings.setValue(QStringLiteral("host"), host->text());
    settings.setValue(QStringLiteral("poll"), poll->value()); settings.setValue(QStringLiteral("timeout"), timeout->value());
    return QJsonDocument(QJsonObject{{QStringLiteral("root"), root}, {QStringLiteral("host"), host->text()},
        {QStringLiteral("key"), memoryKey}, {QStringLiteral("poll"), poll->value()},
        {QStringLiteral("timeout"), timeout->value() * 1000}}).toJson(QJsonDocument::Compact);
}
