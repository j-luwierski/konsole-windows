# SPDX-License-Identifier: MIT
"""Regression: Craft retains konsole-* EXEs but omits Start-* entry points."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('bundle_powershell', Path(__file__).with_name('bundle-powershell.py'))
bundler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundler)


class LauncherPackaging(unittest.TestCase):
    def test_filtered_executables_become_root_and_bin_entry_points(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shell = root / 'shell.zip'
            with zipfile.ZipFile(shell, 'w') as archive:
                for name, data in {'pwsh.exe': b'fixture', 'LICENSE.txt': b'license',
                                   'ThirdPartyNotices.txt': b'notices',
                                   'Modules/PSReadLine/PSReadLine.psd1': b"ModuleVersion = '2.4.5'"}.items():
                    archive.writestr(name, data)
            original_sha = bundler.SHA256
            bundler.SHA256 = hashlib.sha256(shell.read_bytes()).hexdigest()
            try:
                package = root / 'portable.zip'
                with zipfile.ZipFile(package, 'w') as archive:
                    archive.writestr('bin/konsole-launcher.exe', b'local-build')
                    archive.writestr('bin/konsole-remote-launcher.exe', b'remote-build')
                    archive.writestr('bin/konsole.exe', b'terminal')
                bundler.bundle(package, shell, require_launchers=True)
                # Re-bundling must not leave duplicate or stale entry points.
                bundler.bundle(package, shell, require_launchers=True)
                with zipfile.ZipFile(package) as archive:
                    self.assertEqual(len(archive.namelist()), len(set(archive.namelist())))
                    for name, expected in [('Start-Konsole.exe', b'local-build'), ('Start-RemoteHost.exe', b'remote-build')]:
                        self.assertEqual(archive.read(name), expected)
                        self.assertEqual(archive.read('bin/' + name), expected)
                    self.assertEqual(archive.read('powershell/pwsh.exe'), b'fixture')
                incomplete = root / 'incomplete.zip'
                with zipfile.ZipFile(incomplete, 'w') as archive:
                    archive.writestr('bin/konsole.exe', b'terminal')
                with self.assertRaisesRegex(RuntimeError, 'Native launchers missing'):
                    bundler.bundle(incomplete, shell, require_launchers=True)
            finally:
                bundler.SHA256 = original_sha


if __name__ == '__main__':
    unittest.main()
