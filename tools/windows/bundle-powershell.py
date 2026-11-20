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


def bundle(archive, download, require_launchers=False):
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
        launchers = {'Start-Konsole.exe': 'bin/konsole-launcher.exe',
                     'Start-RemoteHost.exe': 'bin/konsole-remote-launcher.exe'}
        if require_launchers:
            missing = set(launchers.values()) - set(original.namelist())
            if missing:
                raise RuntimeError('Native launchers missing from Craft package: ' + ', '.join(sorted(missing)))
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
                if info.filename.startswith('powershell/') or info.filename in ('bundled-runtime.json', 'Start-Konsole.exe', 'Start-RemoteHost.exe',
                                                                    'bin/Start-Konsole.exe', 'bin/Start-RemoteHost.exe'):
                    continue
                with original.open(info) as source:
                    output.writestr(info.filename, source.read())
            for info in shell.infolist():
                # Archive contents stay intact, including licenses and the .NET runtime.
                name = 'powershell/' + info.filename
                with shell.open(info) as source:
                    output.writestr(name, source.read())
            # Craft filters unrelated executable names; retain its konsole-*
            # entries, then add the user-facing entry points explicitly.
            for launcher, source in launchers.items():
                if source in original.namelist():
                    data = original.read(source)
                    output.writestr(launcher, data)
                    output.writestr('bin/' + launcher, data)
                    print(f'Bundled native launcher: {launcher} from {source}')
            output.writestr('bundled-runtime.json', json.dumps(metadata, indent=2) + '\n')
    temporary.replace(archive)
    archive.with_name('bundled-runtime.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print(f'Bundled PowerShell {VERSION}, PSReadLine {readline_version}: {archive}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--download', type=Path, default=Path(tempfile.gettempdir()) / f'PowerShell-{VERSION}-win-x64.zip')
    parser.add_argument('--require-launchers', action='store_true')
    args = parser.parse_args()
    bundle(args.archive, args.download, args.require_launchers)
