# SPDX-License-Identifier: MIT
# Fast backend validation with the same pinned portable shell and completion data.
param([string]$TestBuild = 'build-windows-tests', [string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$runtime = Join-Path ([IO.Path]::GetTempPath()) 'Konsole backend portable test'
New-Item -ItemType Directory -Force $runtime | Out-Null
$archive = Join-Path $runtime 'runtime.zip'
& python -c 'import sys,zipfile; zipfile.ZipFile(sys.argv[1], "w").close()' $archive
& python -m pip install zstandard==0.25.0 pefile==2024.8.26
if ($LASTEXITCODE -ne 0) { throw 'Cannot install runtime bundling dependencies.' }
foreach ($bundler in @('bundle-powershell.py','bundle-file.py','bundle-git-completion.py')) {
    & python (Join-Path $PSScriptRoot $bundler) $archive
    if ($LASTEXITCODE -ne 0) { throw "Runtime bundler failed: $bundler" }
}
Expand-Archive -LiteralPath $archive -DestinationPath $runtime -Force
$env:KONSOLE_TEST_BUNDLED_PWSH = Join-Path $runtime 'powershell/pwsh.exe'
$env:KONSOLE_TEST_FILE_EXE = Join-Path $runtime 'file/file.exe'
try {
    & ctest --test-dir $TestBuild -C $Configuration --output-on-failure
    $result = $LASTEXITCODE
    if ($result -ne 0) {
        foreach ($report in @('conpty-results.xml','file-remote-results.xml','Testing/Temporary/LastTest.log')) {
            $path = Join-Path $TestBuild $report
            if (Test-Path $path) { Get-Content -LiteralPath $path }
        }
        throw 'Bundled PowerShell backend validation failed.'
    }
} finally {
    Remove-Item Env:KONSOLE_TEST_BUNDLED_PWSH -ErrorAction SilentlyContinue
    Remove-Item Env:KONSOLE_TEST_FILE_EXE -ErrorAction SilentlyContinue
}
