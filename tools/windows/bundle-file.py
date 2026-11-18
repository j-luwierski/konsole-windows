# SPDX-License-Identifier: MIT
"""Bundle native MSYS2 UCRT64 file(1), its database, DLLs, licenses and sources."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tarfile
import urllib.request
import zipfile
import zstandard

BINARIES = {'file.exe', 'libmagic-1.dll', 'libsystre-0.dll', 'libtre-5.dll', 'libintl-8.dll', 'libiconv-2.dll'}


def download(url, digest, cache):
    path = cache / url.rsplit('/', 1)[1]
    if not path.exists():
        temporary = path.with_suffix('.download')
        urllib.request.urlretrieve(url, temporary)
        temporary.replace(path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise RuntimeError(f'Checksum mismatch: {path.name}')
    return path


def bundle(archive, cache):
    cache.mkdir(parents=True, exist_ok=True)
    lock = json.loads(Path(__file__).with_name('file-runtime-lock.json').read_text())
    found = set()
    with zipfile.ZipFile(archive, 'a', zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        if 'file/file.exe' in output.namelist():
            raise RuntimeError('file runtime is already bundled')
        for package in lock['packages']:
            binary = download(package['url'], package['sha256'], cache)
            source = download(package['sourceUrl'], package['sourceSha256'], cache)
            output.write(source, 'file/sources/' + source.name)
            with binary.open('rb') as compressed, zstandard.ZstdDecompressor().stream_reader(compressed) as stream:
                with tarfile.open(fileobj=stream, mode='r|') as tar:
                    for member in tar:
                        if not member.isfile():
                            continue
                        path = PurePosixPath(member.name)
                        if '..' in path.parts or path.is_absolute():
                            raise RuntimeError('Unexpected package path')
                        name = path.name
                        target = None
                        if str(path.parent) == 'ucrt64/bin' and name in BINARIES:
                            target = 'file/' + name
                            found.add(name)
                        elif str(path) == 'ucrt64/share/misc/magic.mgc':
                            target = 'file/magic.mgc'
                            found.add('magic.mgc')
                        elif str(path).startswith('ucrt64/share/licenses/'):
                            target = 'file/licenses/' + str(path.relative_to('ucrt64/share/licenses'))
                        if target:
                            output.writestr(target, tar.extractfile(member).read())
        if found != BINARIES | {'magic.mgc'}:
            raise RuntimeError('Missing file runtime components: ' + str((BINARIES | {'magic.mgc'}) - found))
        metadata = {'fileVersion': lock['fileVersion'], 'origin': 'Unmodified native UCRT64 packages from MSYS2',
                    'executable': 'file/file.exe', 'database': 'file/magic.mgc', 'packages': lock['packages']}
        output.writestr('file-runtime.json', json.dumps(metadata, indent=2) + '\n')
        output.writestr('file/README.txt', 'file(1) native Windows runtime. No MSYS2 installation required.\n'
                        'Run file.exe --magic-file magic.mgc --brief -- PATH.\n'
                        'Complete source-only package archives (including build recipes) are in sources/.\n'
                        'Original component licenses are in licenses/. See file-runtime.json for provenance.\n')
    archive.with_name('file-runtime.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print('Bundled file ' + lock['fileVersion'] + ' with database, native DLLs, licenses and corresponding sources')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--cache', type=Path, default=Path.home() / '.cache/konsole-file-runtime')
    args = parser.parse_args()
    bundle(args.archive, args.cache)
