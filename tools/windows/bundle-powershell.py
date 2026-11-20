# SPDX-License-Identifier: MIT
"""Add the pinned, self-contained Microsoft PowerShell distribution to a Craft ZIP."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile
import urllib.request
import zipfile

VERSION = '7.6.10'
SHA256 = '7d871531806eb1646264e4bbabd9f0c30c23355591bc756bb9909448c29ae166'
URL = f'https://github.com/PowerShell/PowerShell/releases/download/v{VERSION}/PowerShell-{VERSION}-win-x64.zip'


def bundle(archive, download):
    if not download.exists():
        download.parent.mkdir(parents=True, exist_ok=True)
        partial = download.with_suffix('.download')
        urllib.request.urlretrieve(URL, partial)
        partial.replace(download)
    with download.open('rb') as stream:
        digest = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    if digest.hexdigest() != SHA256:
        raise RuntimeError('PowerShell archive checksum mismatch')
    with zipfile.ZipFile(download) as shell, zipfile.ZipFile(archive) as original:
        required = {'pwsh.exe', 'Modules/PSReadLine/PSReadLine.psd1', 'LICENSE.txt', 'ThirdPartyNotices.txt'}
        if not required.issubset(shell.namelist()):
            raise RuntimeError('PowerShell distribution is incomplete')
        manifest = shell.read('Modules/PSReadLine/PSReadLine.psd1').decode('utf-8-sig')
        readline_version = re.search(r"ModuleVersion\s*=\s*'([^']+)'", manifest).group(1)
        metadata = {'powerShell': VERSION, 'psReadLine': readline_version, 'source': URL, 'sha256': SHA256,
                    'executable': 'powershell/pwsh.exe', 'selfContained': True}
        temporary = archive.with_suffix('.bundling.zip')
        with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as output:
            for info in original.infolist():
                if info.filename.startswith('powershell/') or info.filename in ('bundled-runtime.json', 'Start-Konsole.exe'):
                    continue
                with original.open(info) as source, output.open(info, 'w') as target:
                    shutil.copyfileobj(source, target)
            for info in shell.infolist():
                # Archive contents stay intact, including licenses and the .NET runtime.
                name = 'powershell/' + info.filename
                with shell.open(info) as source, output.open(name, 'w') as target:
                    shutil.copyfileobj(source, target)
            if 'bin/Start-Konsole.exe' in original.namelist():
                output.writestr('Start-Konsole.exe', original.read('bin/Start-Konsole.exe'))
            output.writestr('bundled-runtime.json', json.dumps(metadata, indent=2) + '\n')
    temporary.replace(archive)
    archive.with_name('bundled-runtime.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print(f'Bundled PowerShell {VERSION}, PSReadLine {readline_version}: {archive}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--download', type=Path, default=Path(tempfile.gettempdir()) / f'PowerShell-{VERSION}-win-x64.zip')
    args = parser.parse_args()
    bundle(args.archive, args.download)
