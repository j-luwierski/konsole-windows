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
# Expose the portable native file command, including its magic database.
$konsoleFileRoot = $env:KONSOLE_PORTABLE_FILE_DIRECTORY
if (!$konsoleFileRoot) { $konsoleFileRoot = Join-Path (Split-Path -Parent $PSHOME) 'file' }
if (Test-Path -LiteralPath (Join-Path $konsoleFileRoot 'file.exe')) {
    $env:PATH = $konsoleFileRoot + [IO.Path]::PathSeparator + $env:PATH
    $env:MAGIC = Join-Path $konsoleFileRoot 'magic.mgc'
}
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
# Keep Get-ChildItem/ls/dir as object-producing commands. Only their default
# filesystem table view adds OSC 8 links with the full, original path.
if ($PSVersionTable.PSVersion -ge [Version]'7.2') {
    function global:Format-KonsoleFileName($item) {
        $escape = [string][char]27
        $uri = [Uri]::new($item.FullName).AbsoluteUri
        return $escape + ']8;;' + $uri + $escape + '\' + $item.Name + $escape + ']8;;' + $escape + '\'
    }
    $konsoleFormat = @'
<Configuration><ViewDefinitions><View><Name>Konsole.FileSystem</Name>
<ViewSelectedBy><TypeName>System.IO.FileInfo</TypeName><TypeName>System.IO.DirectoryInfo</TypeName></ViewSelectedBy>
<GroupBy><ScriptBlock>if ($_.PSIsContainer) { $_.Parent.FullName } else { $_.DirectoryName }</ScriptBlock><Label>Directory</Label></GroupBy>
<TableControl><TableHeaders>
<TableColumnHeader><Label>Mode</Label><Width>7</Width></TableColumnHeader>
<TableColumnHeader><Label>LastWriteTime</Label><Width>20</Width></TableColumnHeader>
<TableColumnHeader><Label>Length</Label><Width>12</Width><Alignment>Right</Alignment></TableColumnHeader>
<TableColumnHeader><Label>Name</Label></TableColumnHeader>
</TableHeaders><TableRowEntries><TableRowEntry><TableColumnItems>
<TableColumnItem><ScriptBlock>$_.Mode</ScriptBlock></TableColumnItem>
<TableColumnItem><ScriptBlock>$_.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')</ScriptBlock></TableColumnItem>
<TableColumnItem><ScriptBlock>if (!$_.PSIsContainer) { $_.Length }</ScriptBlock><Alignment>Right</Alignment></TableColumnItem>
<TableColumnItem><ScriptBlock>Format-KonsoleFileName $_</ScriptBlock></TableColumnItem>
</TableColumnItems></TableRowEntry></TableRowEntries></TableControl></View></ViewDefinitions></Configuration>
'@
    $konsoleFormatPath = Join-Path ([IO.Path]::GetTempPath()) ('konsole-' + [Guid]::NewGuid().ToString('N') + '.format.ps1xml')
    try {
        [IO.File]::WriteAllText($konsoleFormatPath, $konsoleFormat, [Text.UTF8Encoding]::new($false))
        Update-FormatData -PrependPath $konsoleFormatPath -ErrorAction Stop
        Register-EngineEvent -SourceIdentifier PowerShell.Exiting -MessageData $konsoleFormatPath -Action {
            Remove-Item -LiteralPath $event.MessageData -Force -ErrorAction SilentlyContinue
        } | Out-Null
    } catch {
        Remove-Item -LiteralPath $konsoleFormatPath -Force -ErrorAction SilentlyContinue
        Write-Warning ('Konsole: file listing links could not be enabled: ' + $_.Exception.Message)
    }
}
# Report the real directory after cd so relative file paths resolve correctly.
$global:KonsoleOriginalPrompt = $function:prompt
function global:prompt {
    try {
        if ($PWD.Provider.Name -eq 'FileSystem') {
            $konsoleDirectory = [Uri]::new($PWD.ProviderPath)
            [Console]::Write(([char]27 + ']7;' + $konsoleDirectory.AbsoluteUri + [char]7))
        }
    } catch {}
    if ($global:KonsoleOriginalPrompt) { & $global:KonsoleOriginalPrompt } else { 'PS> ' }
}
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
