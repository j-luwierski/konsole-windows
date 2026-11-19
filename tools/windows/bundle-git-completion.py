# SPDX-License-Identifier: MIT
"""Bundle pinned upstream posh-git sources and license, without modifying PowerShell."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import tempfile
import urllib.request
import zipfile
VERSION = '1.1.0'
COMMIT = 'd8b126decc29193a78e71cca6f59598017703a85'
SHA256 = 'c27773795158b7de133394dd726320de0db67c8c6ebeea8628cc2aa04696ee6a'
URL = f'https://codeload.github.com/dahlbyk/posh-git/zip/{COMMIT}'
PREFIX = 'powershell/Modules/posh-git/'
def bundle(archive, download):
    if not download.exists():
        download.parent.mkdir(parents=True, exist_ok=True)
        partial = download.with_suffix('.download')
        urllib.request.urlretrieve(URL, partial)
        partial.replace(download)
    if hashlib.sha256(download.read_bytes()).hexdigest() != SHA256:
        raise RuntimeError('posh-git source checksum mismatch')
    metadata = {'poshGit': VERSION, 'commit': COMMIT, 'source': URL, 'sha256': SHA256,
                'module': PREFIX + 'posh-git.psd1', 'license': 'MIT', 'gitExecutableBundled': False}
    temporary = archive.with_suffix('.git-completion.zip')
    with zipfile.ZipFile(archive) as original, zipfile.ZipFile(download) as source:
        root = source.namelist()[0]
        with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as output:
            for info in original.infolist():
                if info.filename.startswith(PREFIX) or info.filename == 'git-completion-runtime.json': continue
                with original.open(info) as src, output.open(info, 'w') as dst: shutil.copyfileobj(src, dst)
            for name in source.namelist():
                if name.startswith(root + 'src/') and not name.endswith('/'):
                    output.writestr(PREFIX + name[len(root + 'src/'):], source.read(name))
            definitions = source.read(root + 'src/GitParamTabExpansion.ps1').decode('utf-8-sig')
            options = {}
            for table, prefix in [('shortGitParams', '-'), ('longGitParams', '--')]:
                body = re.search(r'\$' + table + r'\s*=\s*@\{(.*?)^\}', definitions, re.M | re.S).group(1)
                for quoted, plain, values in re.findall(r"^\s*(?:'([^']+)'|([\w-]+))\s*=\s*'([^']*)'\s*$", body, re.M):
                    options.setdefault(quoted or plain, []).extend(prefix + value for value in values.split())
            data = {'poshGit': VERSION, 'options': options,
                    'commands': sorted(set(options) | {'worktree', 'sparse-checkout', 'maintenance', 'range-diff'}),
                    'globalOptions': ['--version', '--help', '-C', '-c', '--git-dir=', '--work-tree=', '--bare', '--no-pager', '--paginate', '--no-optional-locks']}
            output.writestr(PREFIX + 'konsole-options.json', json.dumps(data, indent=2) + '\n')
            for name in ['LICENSE.txt', 'README.md']:
                output.writestr(PREFIX + name, source.read(root + name))
            output.write(download, PREFIX + f'sources/posh-git-{VERSION}.zip')
            output.writestr('git-completion-runtime.json', json.dumps(metadata, indent=2) + '\n')
    temporary.replace(archive)
    archive.with_name('git-completion-runtime.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print(f'Bundled posh-git {VERSION}: {archive}')
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--download', type=Path, default=Path(tempfile.gettempdir()) / f'posh-git-{VERSION}.zip')
    args = parser.parse_args()
    bundle(args.archive, args.download)
