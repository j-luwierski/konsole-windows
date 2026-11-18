/*
    SPDX-FileCopyrightText: 2026 Konsole contributors
    SPDX-License-Identifier: MIT
*/
#ifndef WINDOWSSHELLINTEGRATION_H
#define WINDOWSSHELLINTEGRATION_H

#include <QDir>
#include <QFileInfo>
#include <QStringConverter>
#include <QStringList>

namespace WindowsShellIntegration
{
// Resolve relative to the executable so moving the entire portable folder works.
inline QString bundledPowerShell(const QString &applicationDirectory)
{
    const QDir root(QDir(applicationDirectory).absoluteFilePath(QStringLiteral("../powershell")));
    const QString executable = QDir::cleanPath(root.filePath(QStringLiteral("pwsh.exe")));
    if (QFileInfo::exists(executable) && QFileInfo::exists(root.filePath(QStringLiteral("Modules/PSReadLine/PSReadLine.psd1")))) {
        return QDir::toNativeSeparators(executable);
    }
    return {};
}

// Applied only to Konsole's built-in PowerShell profile, after the user's
// PowerShell profile has loaded. No system profile or execution policy changes.
inline QString initializationScript()
{
    return QStringLiteral(R"PS(
try {
    $konsoleReadLine = Join-Path $PSHOME 'Modules/PSReadLine/PSReadLine.psd1'
    if (Test-Path -LiteralPath $konsoleReadLine) {
        Import-Module $konsoleReadLine -Force -ErrorAction Stop
    } else {
        Import-Module PSReadLine -ErrorAction Stop
    }
    Set-PSReadLineKeyHandler -Chord Tab -Function Complete -ErrorAction Stop
    Set-PSReadLineKeyHandler -Chord Ctrl+r -Function ReverseSearchHistory -ErrorAction Stop
    Set-PSReadLineOption -HistorySaveStyle SaveIncrementally -ErrorAction Stop
} catch {
    Write-Warning ('Konsole: could not enable Tab completion and Ctrl+R history search: ' + $_.Exception.Message)
}
[Console]::InputEncoding = New-Object System.Text.UTF8Encoding $false
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false
$OutputEncoding = [Console]::OutputEncoding
)PS");
}

inline QStringList arguments(const QString &script = initializationScript())
{
    QStringEncoder encoder(QStringEncoder::Utf16LE);
    const QByteArray encoded = encoder(script);
    return {QStringLiteral("-NoLogo"), QStringLiteral("-NoExit"), QStringLiteral("-EncodedCommand"), QString::fromLatin1(encoded.toBase64())};
}
}
#endif
