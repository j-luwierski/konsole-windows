# SPDX-License-Identifier: GPL-2.0-or-later
# PSReadLine owns editing/prediction; completers supply command-specific data.
$konsoleCompletionRoot = if ($konsoleFileRoot) { Split-Path -Parent $konsoleFileRoot } else { Split-Path -Parent $PSScriptRoot }
$konsolePoshGit = Join-Path $konsoleCompletionRoot 'powershell/Modules/posh-git/posh-git.psd1'
$konsoleGitLoaded = $false
if ((Get-ExecutionPolicy) -notin @('Restricted','AllSigned') -and (Test-Path -LiteralPath $konsolePoshGit) -and (Get-Command git -CommandType Application -ErrorAction SilentlyContinue)) {
    $konsoleSavedPrompt = $function:prompt
    try {
        function global:prompt { 'PS> ' }
        Import-Module $konsolePoshGit -ErrorAction Stop -DisableNameChecking
        $konsoleGitLoaded = $true
    } catch { Write-Verbose ('Konsole: using integrated Git completion: ' + $_.Exception.Message) }
    finally { if ($konsoleSavedPrompt) { Set-Item Function:global:prompt -Value $konsoleSavedPrompt } }
}
# The integrated provider reads data, not unsigned module scripts, when policy disallows them.
$konsoleGitData = Join-Path $konsoleCompletionRoot 'powershell/Modules/posh-git/konsole-options.json'
if (!$konsoleGitLoaded -and (Test-Path -LiteralPath $konsoleGitData)) {
    $global:KonsoleGitCompletionData = [IO.File]::ReadAllText($konsoleGitData) | ConvertFrom-Json
    Register-ArgumentCompleter -Native -CommandName git,git.exe -ScriptBlock {
        param($wordToComplete, $commandAst, $cursorPosition)
        $parts = @($commandAst.CommandElements | Select-Object -Skip 1)
        $sub = if ($parts.Count) { try { [string]$parts[0].SafeGetValue() } catch { '' } } else { '' }
        $candidates = @(); $kind = 'ParameterValue'
        if ($wordToComplete.StartsWith('-')) {
            $kind = 'ParameterName'
            $definition = $global:KonsoleGitCompletionData.options.PSObject.Properties[$sub]
            $candidates = if ($definition) { @($definition.Value) } else { @($global:KonsoleGitCompletionData.globalOptions) }
        } elseif ($parts.Count -le 1) { $candidates = @($global:KonsoleGitCompletionData.commands) }
        elseif ($sub -in @('checkout','switch','branch','merge','rebase','log','show','diff','reset','cherry-pick')) {
            $saved = Get-Variable LASTEXITCODE -Scope Global -ValueOnly -ErrorAction SilentlyContinue
            try { $candidates = @(git for-each-ref '--format=%(refname:short)' refs/heads refs/remotes refs/tags 2>$null) }
            finally { $global:LASTEXITCODE = $saved }
        } elseif ($sub -in @('push','pull','fetch','remote')) {
            $saved = Get-Variable LASTEXITCODE -Scope Global -ValueOnly -ErrorAction SilentlyContinue
            try { $candidates = @(git remote 2>$null) } finally { $global:LASTEXITCODE = $saved }
        }
        foreach ($value in $candidates) {
            if ($value -and $value.StartsWith($wordToComplete, [StringComparison]::Ordinal)) {
                $insert = if ($kind -eq 'ParameterName' -or $value -match '^[A-Za-z0-9_./:@+-]+$') { $value } else { "'" + $value.Replace("'", "''") + "'" }
                [System.Management.Automation.CompletionResult]::new($insert, $value, $kind, $value)
            }
        }
    }
}

if (!(Test-Path Variable:global:KonsoleAcceptPredictionWithTab)) { $global:KonsoleAcceptPredictionWithTab = $true }
if ([Microsoft.PowerShell.PSConsoleReadLine].GetMethods().Name -contains 'AcceptSuggestion') {
    Set-PSReadLineKeyHandler -Chord Tab -BriefDescription KonsoleCompleteOrAcceptSuggestion -ScriptBlock {
        param($key, $arg)
        $before = ''; $cursor = 0
        [Microsoft.PowerShell.PSConsoleReadLine]::GetBufferState([ref]$before, [ref]$cursor)
        # Accept a visible prediction only at the end of the current input.
        if ($global:KonsoleAcceptPredictionWithTab -and $cursor -eq $before.Length) {
            [Microsoft.PowerShell.PSConsoleReadLine]::AcceptSuggestion($key, $arg)
            $after = ''; $newCursor = 0
            [Microsoft.PowerShell.PSConsoleReadLine]::GetBufferState([ref]$after, [ref]$newCursor)
            if ($after -ne $before) { return }
        }
        [Microsoft.PowerShell.PSConsoleReadLine]::Complete($key, $arg)
    }
    if ((Get-PSReadLineOption).PredictionSource -eq 'None') {
        Set-PSReadLineOption -PredictionSource History -PredictionViewStyle InlineView
    }
}
Set-PSReadLineKeyHandler -Chord Ctrl+Spacebar -Function MenuComplete
Set-PSReadLineKeyHandler -Chord Shift+Tab -Function TabCompletePrevious

# PowerShell 7.6's native fallback applies automatically to new applications.
# Query only top-level --help, never run the user's partial command/subcommand.
# A console EXE may still implement --help incorrectly; the fallback can be disabled.
if (!(Test-Path Variable:global:KonsoleNativeHelpCompletionEnabled)) { $global:KonsoleNativeHelpCompletionEnabled = $true }
$global:KonsoleNativeHelpCompletionCache = @{}
function global:Get-KonsoleNativeFlagCompletion {
    param([string]$Word, $CommandAst)
    if (!$global:KonsoleNativeHelpCompletionEnabled -or ($Word -and !$Word.StartsWith('-'))) { return }
    $name = $CommandAst.GetCommandName()
    if (!$name) { return }
    $command = Get-Command -Name ([WildcardPattern]::Escape($name)) -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (!$command) { return }
    $path = $command.Source
    $item = Get-Item -LiteralPath $path -ErrorAction SilentlyContinue
    if (!$item) { return }
    if ([Environment]::OSVersion.Platform -eq 'Win32NT') {
        if ($item.Extension -ne '.exe') { return }
        # Avoid launching GUI applications merely to request completion.
        $stream = $null; $reader = $null
        try {
            $stream = [IO.File]::Open($path, 'Open', 'Read', 'ReadWrite')
            $reader = [IO.BinaryReader]::new($stream)
            $stream.Position = 0x3c; $offset = $reader.ReadInt32()
            if ($offset -lt 64 -or $offset -gt $stream.Length - 96) { return }
            $stream.Position = $offset
            if ($reader.ReadUInt32() -ne 0x4550) { return }
            $stream.Position = $offset + 4 + 20 + 68
            if ($reader.ReadUInt16() -ne 3) { return }
        } catch { return } finally { if ($reader) { $reader.Dispose() } elseif ($stream) { $stream.Dispose() } }
    }
    $cacheKey = $path + ':' + $item.LastWriteTimeUtc.Ticks
    if (!$global:KonsoleNativeHelpCompletionCache.ContainsKey($cacheKey)) {
        $values = @()
        $process = [Diagnostics.Process]::new()
        try {
            $process.StartInfo.FileName = $path
            $process.StartInfo.ArgumentList.Add('--help')
            $process.StartInfo.UseShellExecute = $false
            $process.StartInfo.CreateNoWindow = $true
            $process.StartInfo.WorkingDirectory = [IO.Path]::GetTempPath()
            $process.StartInfo.RedirectStandardInput = $true
            $process.StartInfo.RedirectStandardOutput = $true
            $process.StartInfo.RedirectStandardError = $true
            if ($process.Start()) {
                $process.StandardInput.Close()
                $stdout = $process.StandardOutput.ReadToEndAsync()
                $stderr = $process.StandardError.ReadToEndAsync()
                if (!$process.WaitForExit(1000)) { $process.Kill($true); $null = $process.WaitForExit(500) }
                if ($stdout.Wait(200) -and $stderr.Wait(200)) {
                    $help = $stdout.Result + "`n" + $stderr.Result
                    if ($help.Length -gt 131072) { $help = $help.Substring(0, 131072) }
                    $values = @([regex]::Matches($help, '(?<![\w-])--?[A-Za-z][A-Za-z0-9_-]*(?:=)?') |
                        ForEach-Object Value | Sort-Object -Unique -CaseSensitive)
                }
            }
        } catch {} finally {
            try { if ($process.Id -and !$process.HasExited) { $process.Kill($true) } } catch {}
            $process.Dispose()
        }
        # Bound the cache; failed/unsupported help is cached too.
        if ($global:KonsoleNativeHelpCompletionCache.Count -ge 256) { $global:KonsoleNativeHelpCompletionCache.Clear() }
        $global:KonsoleNativeHelpCompletionCache[$cacheKey] = $values
    }
    foreach ($value in $global:KonsoleNativeHelpCompletionCache[$cacheKey]) {
        if ($value.StartsWith($Word, [StringComparison]::Ordinal)) {
            [System.Management.Automation.CompletionResult]::new($value, $value, 'ParameterName', $value)
        }
    }
}
if ($global:KonsoleNativeHelpCompletionEnabled -and (Get-Command Register-ArgumentCompleter).Parameters.ContainsKey('NativeFallback')) {
    Register-ArgumentCompleter -NativeFallback -ScriptBlock {
        param($wordToComplete, $commandAst, $cursorPosition)
        Get-KonsoleNativeFlagCompletion $wordToComplete $commandAst
    }
}
