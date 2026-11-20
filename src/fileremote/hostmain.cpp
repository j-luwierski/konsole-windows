/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "FileRemoteHost.h"
#include "../windows/QtReport.h"
#include "../filterHotSpots/TextPreviewContent.h"
#include "../ptyqt/windowsshellintegration.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <atomic>
using namespace Konsole::FileRemote;
static std::atomic_bool stopping{false};
static BOOL WINAPI consoleControl(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) { stopping = true; return TRUE; }
    return FALSE;
}
int main(int argc, char **argv)
{
    Konsole::WindowsReport::initializeQt(L"remote-host");
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("konsole-file-host"));
    QCoreApplication::setOrganizationName(QStringLiteral("KDE"));
    QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("Konsole shared-folder remote terminal host (no sockets). Run in the foreground as the intended Windows user."));
    parser.addHelpOption();
    parser.addOptions({
        {QStringLiteral("root"), QStringLiteral("Local path to the shared exchange folder"), QStringLiteral("path")},
        {QStringLiteral("host"), QStringLiteral("Host ID"), QStringLiteral("id"), QStringLiteral("host1")},
        {QStringLiteral("key-file"), QStringLiteral("Local pairing key file (outside the share)"), QStringLiteral("path")},
        {QStringLiteral("generate-key"), QStringLiteral("Create a local pairing key file and exit"), QStringLiteral("path")},
        {QStringLiteral("shell"), QStringLiteral("PowerShell executable (defaults to bundled PowerShell)"), QStringLiteral("path")},
        {QStringLiteral("cwd"), QStringLiteral("Initial remote working directory"), QStringLiteral("path")},
        {QStringLiteral("poll"), QStringLiteral("Polling interval, 20..5000 ms"), QStringLiteral("ms"), QStringLiteral("50")},
        {QStringLiteral("timeout"), QStringLiteral("Client heartbeat timeout, 5..600 seconds"), QStringLiteral("seconds"), QStringLiteral("30")},
        {QStringLiteral("max-sessions"), QStringLiteral("Maximum simultaneous shells, 1..16"), QStringLiteral("count"), QStringLiteral("4")}});
    parser.process(app);
    QTextStream out(stdout), err(stderr);
    if (parser.isSet(QStringLiteral("generate-key"))) {
        const QString path = parser.value(QStringLiteral("generate-key"));
        if (QFileInfo::exists(path)) { err << "Key file already exists; refusing to overwrite it\n"; Konsole::WindowsReport::event(L"remote host configuration error", L"Key file already exists; refusing overwrite"); return 1; }
        QSaveFile file(path);
        const QByteArray key = Channel::generateKey().toHex() + '\n';
        if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
            || file.write(key) != key.size() || !file.commit()) { err << file.errorString() << '\n'; Konsole::WindowsReport::event(L"remote host configuration error", L"Cannot write local key file"); return 1; }
        out << "Pairing key (enter on the client, keep private): " << QString::fromLatin1(key); return 0;
    }
    QFile keyFile(parser.value(QStringLiteral("key-file")));
    if (!keyFile.open(QIODevice::ReadOnly) || keyFile.size() > 128) { err << "Provide --key-file with a local 64-hex-character key\n"; Konsole::WindowsReport::event(L"remote host configuration error", L"Cannot read local key file"); return 1; }
    const QByteArray keyHex = keyFile.readAll().trimmed();
    if (keyHex.size() != 64 || QByteArray::fromHex(keyHex).toHex() != keyHex.toLower()) { err << "Invalid key file\n"; Konsole::WindowsReport::event(L"remote host configuration error", L"Invalid key format"); return 1; }
    HostOptions options;
    options.root = QDir(parser.value(QStringLiteral("root"))).absolutePath(); options.host = parser.value(QStringLiteral("host"));
    if (!parser.isSet(QStringLiteral("root"))) { err << "--root is required\n"; Konsole::WindowsReport::event(L"remote host configuration error", L"Missing exchange folder option"); return 1; }
    const QString keyPath = QFileInfo(keyFile).absoluteFilePath();
    const QString rootCanonical = QDir(options.root).canonicalPath();
    if (!rootCanonical.isEmpty() && (keyPath.compare(rootCanonical, Qt::CaseInsensitive) == 0
        || keyPath.startsWith(rootCanonical + QLatin1Char('/'), Qt::CaseInsensitive))) {
        err << "Keep the pairing key outside the exchange folder\n"; Konsole::WindowsReport::event(L"remote host configuration error", L"Key file must be outside exchange folder"); return 1;
    }
    options.key = QByteArray::fromHex(keyHex);
    options.fileExecutable = Konsole::TextPreviewContent::fileCommand();
    options.shell = parser.value(QStringLiteral("shell"));
    if (options.shell.isEmpty()) { options.shell = WindowsShellIntegration::bundledPowerShell(QCoreApplication::applicationDirPath()); }
    if (options.shell.isEmpty()) { err << "Bundled PowerShell missing; provide --shell\n"; Konsole::WindowsReport::event(L"remote host configuration error", L"Bundled PowerShell missing"); return 1; }
    options.workingDirectory = parser.value(QStringLiteral("cwd"));
    if (options.workingDirectory.isEmpty()) { options.workingDirectory = QDir::homePath(); }
    options.arguments = WindowsShellIntegration::arguments();
    options.pollMs = parser.value(QStringLiteral("poll")).toInt(); options.timeoutMs = qBound(0, parser.value(QStringLiteral("timeout")).toInt(), 601) * 1000;
    options.maximumSessions = parser.value(QStringLiteral("max-sessions")).toInt();
    if (options.pollMs < 20 || options.pollMs > 5000 || options.timeoutMs < 5000 || options.timeoutMs > 600000
        || options.maximumSessions < 1 || options.maximumSessions > 16 || !QDir(options.workingDirectory).exists()) { err << "Invalid poll/timeout/session limit or working directory\n"; Konsole::WindowsReport::event(L"remote host configuration error", L"Invalid limits or unavailable working directory"); return 1; }
    const QByteArray identity = (QDir(options.root).absolutePath() + QLatin1Char('/') + options.host).toUtf8();
    const QString ledgerId = QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    options.ledgerDirectory = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("receipts/") + ledgerId);
    Host host(options); QString error;
    if (!host.start(error)) { Konsole::WindowsReport::event(L"remote host start error", error.toStdWString().c_str()); err << error << '\n'; Konsole::WindowsReport::event(L"remote host configuration error", L"Cannot start shared-folder host"); return 1; }
    SetConsoleCtrlHandler(consoleControl, TRUE);
    QTimer timer; QObject::connect(&timer, &QTimer::timeout, &app, [&] { if (stopping) { app.quit(); } }); timer.start(50);
    out << "Konsole file host running: " << options.host << "\nExchange: " << options.root
        << "\nCtrl+C stops the host and its terminal sessions. Shells run as the current Windows user.\n"; out.flush();
    Konsole::WindowsReport::event(L"remote exchange folder", options.root.toStdWString().c_str());
    Konsole::WindowsReport::event(L"remote working directory", options.workingDirectory.toStdWString().c_str());
    const int status = app.exec();
    Konsole::WindowsReport::event(L"remote host exit", std::to_wstring(status).c_str());
    return status;
}
