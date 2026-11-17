# SPDX-License-Identifier: MIT
# Run in an initialized KDE Craft PowerShell environment (MSVC x64).
param([string]$OutputDirectory = '', [switch]$SkipPackage)
$ErrorActionPreference = 'Stop'
if (-not (Get-Command craft -ErrorAction SilentlyContinue)) {
    throw 'Initialize KDE Craft first: https://develop.kde.org/docs/getting-started/building/craft/craft-windows/'
}
$sourceDirectory = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $sourceDirectory 'dist/custom-windows' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$sourceOptions = @('--options', "konsole.srcDir=$sourceDirectory", '--options', 'konsole.version=master',
                   '--options', 'konsole.buildTests=False', '--options', 'konsole.packageAppx=False')
function Invoke-CraftChecked {
    param([string[]]$CraftArguments)
    & craft @CraftArguments
    if ($LASTEXITCODE -ne 0) { throw "Craft failed ($LASTEXITCODE): $CraftArguments" }
}
# Use KDE's binary cache for dependencies, but compile Konsole from this checkout.
Invoke-CraftChecked (@('--install-deps') + $sourceOptions + @('konsole'))
Invoke-CraftChecked (@('--no-cache', '--compile', '--install', '--qmerge') + $sourceOptions + @('konsole'))

$testBuild = Join-Path $sourceDirectory 'build-windows-backend'
& cmake -S (Join-Path $sourceDirectory 'tests/windows') -B $testBuild -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'Backend test configuration failed.' }
& cmake --build $testBuild
if ($LASTEXITCODE -ne 0) { throw 'Backend test compilation failed.' }
& ctest --test-dir $testBuild --output-on-failure --output-junit (Join-Path $OutputDirectory 'backend-tests.xml')
if ($LASTEXITCODE -ne 0) { throw 'Backend tests failed; package was not created.' }

if (-not $SkipPackage) {
    Invoke-CraftChecked @('nsis')
    $packageOptions = $sourceOptions + @('--options', "[Packager]Destination=$OutputDirectory",
        '--options', '[Packager]PackageDebugSymbols=False')
    Invoke-CraftChecked (@('--package') + $packageOptions + @('--options', '[Packager]PackageType=NullsoftInstallerPackager', 'konsole'))
    Invoke-CraftChecked (@('--package') + $packageOptions + @('--options', '[Packager]PackageType=CreateArchivePackager', 'konsole'))
    $installers = @(Get-ChildItem $OutputDirectory -Filter '*.exe')
    if ($installers.Count -eq 0) { throw 'Packaging did not produce an EXE installer.' }
}
$revision = (& git -C $sourceDirectory rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source revision.' }
$dirty = [bool](& git -C $sourceDirectory status --porcelain --untracked-files=no)
@{
    source = $sourceDirectory
    revision = $revision
    modifiedTrackedFiles = $dirty
    architecture = 'x86_64'
    builtAtUtc = [DateTime]::UtcNow.ToString('o')
    githubRun = $env:GITHUB_RUN_ID
    origin = 'Custom build of this checkout; not a downloaded upstream Konsole binary'
} | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $OutputDirectory 'build-info.json')
Get-ChildItem $OutputDirectory -File | Where-Object { $_.Extension -in '.exe', '.7z', '.zip' } | ForEach-Object {
    $digest = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$digest  $($_.Name)" | Set-Content -Encoding ascii ($_.FullName + '.sha256')
}
Write-Host "Custom Konsole build: $OutputDirectory"
