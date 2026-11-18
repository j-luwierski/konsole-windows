# SPDX-License-Identifier: GPL-2.0-or-later
# Run explicitly on the remote Windows host; no administrator privileges needed.
param(
    [Parameter(Mandatory=$true)][string]$ExchangePath,
    [string]$HostId = 'host1',
    [string]$KeyFile = (Join-Path $env:LOCALAPPDATA 'KonsoleFileRemote/pairing.key'),
    [string]$WorkingDirectory = $HOME,
    [ValidateRange(20,5000)][int]$PollMilliseconds = 50,
    [ValidateRange(5,600)][int]$TimeoutSeconds = 30,
    [ValidateRange(1,16)][int]$MaximumSessions = 4,
    [string]$PowerShellPath = ''
)
$ErrorActionPreference = 'Stop'
$hostExecutable = Join-Path $PSScriptRoot 'konsole-file-host.exe'
if (-not (Test-Path -LiteralPath $hostExecutable -PathType Leaf)) { throw 'Keep this script in the portable bin folder with konsole-file-host.exe.' }
if (-not (Test-Path -LiteralPath $ExchangePath -PathType Container)) { throw 'ExchangePath must be an existing shared folder.' }
$KeyFile = [IO.Path]::GetFullPath($KeyFile)
$exchangeFullPath = [IO.Path]::GetFullPath($ExchangePath).TrimEnd([IO.Path]::DirectorySeparatorChar)
if ($KeyFile.StartsWith($exchangeFullPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Keep KeyFile outside the exchange folder.' }
if (-not (Test-Path -LiteralPath $KeyFile)) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $KeyFile) | Out-Null
    & $hostExecutable --generate-key $KeyFile
    if ($LASTEXITCODE -ne 0) { throw 'Cannot create the local pairing key.' }
} else {
    Write-Host ('Pairing key (enter on the client, keep private): ' + [IO.File]::ReadAllText($KeyFile).Trim())
}
$hostArguments = @('--root', $ExchangePath, '--host', $HostId, '--key-file', $KeyFile,
    '--cwd', $WorkingDirectory, '--poll', "$PollMilliseconds", '--timeout', "$TimeoutSeconds", '--max-sessions', "$MaximumSessions")
if ($PowerShellPath) { $hostArguments += @('--shell', $PowerShellPath) }
& $hostExecutable @hostArguments
if ($LASTEXITCODE -ne 0) { throw "Remote host stopped with exit code $LASTEXITCODE" }
