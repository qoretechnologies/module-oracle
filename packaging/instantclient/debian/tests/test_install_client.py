#!/usr/bin/python3
# Copyright (C) 2026 David Nichols
# SPDX-License-Identifier: MIT
"""Exercise architecture selection and fail-closed vendor input validation."""
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile

INSTALLER = Path(__file__).resolve().parents[1] / 'install-client.py'


class ClientInstallerTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        tools = self.root / 'bin'
        tools.mkdir()
        dpkg = tools / 'dpkg-architecture'
        dpkg.write_text('#!/bin/sh\ncase "$1" in\n'
                        '-qDEB_HOST_ARCH) echo "$TEST_ARCH";;\n'
                        '-qDEB_HOST_MULTIARCH) echo "$TEST_TRIPLET";;\n'
                        '*) exit 1;;\nesac\n')
        dpkg.chmod(0o755)
        self.env = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ['PATH'])
        self.spec = {'root': 'instantclient_23_26', 'architectures': {}}
        for arch, triplet, machine in [('amd64', 'x86_64-linux-gnu', 62),
                                        ('arm64', 'aarch64-linux-gnu', 183)]:
            self.spec['architectures'][arch] = {'multiarch': triplet, 'elf_machine': machine}
            elf = bytearray(64)
            elf[:7] = b'\x7fELF\x02\x01\x01'
            elf[18:20] = machine.to_bytes(2, 'little')
            self.archive(arch, 'basic', [('instantclient_23_26/libclntsh.so.23.1', bytes(elf))])
            self.archive(arch, 'sdk', [('instantclient_23_26/sdk/include/oci.h', b'header')])

    def archive(self, arch, kind, members):
        path = self.root / f'{arch}-{kind}.zip'
        with zipfile.ZipFile(path, 'w') as archive:
            for name, data in members:
                if isinstance(name, str):
                    name = zipfile.ZipInfo(name)
                    name.create_system = 3
                    name.external_attr = (stat.S_IFREG | 0o644) << 16
                archive.writestr(name, data)
        self.spec['architectures'][arch][kind] = {
            'file': path.name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}

    def run_installer(self, arch='arm64', triplet=None, verify=False):
        (self.root / 'upstream.json').write_text(json.dumps(self.spec))
        self.env['TEST_ARCH'] = arch
        self.env['TEST_TRIPLET'] = triplet or self.spec['architectures'].get(arch, {}).get('multiarch', '')
        return subprocess.run([sys.executable, str(INSTALLER)] + (['--verify-only'] if verify else []),
                              cwd=self.root, env=self.env, capture_output=True, text=True)

    def test_native_payload_and_multiarch_paths(self):
        for arch, spec in self.spec['architectures'].items():
            result = self.run_installer(arch, verify=True)
            self.assertEqual(result.returncode, 0, result.stderr)
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stderr)
        libdir = 'usr/lib/aarch64-linux-gnu/oracle/23'
        basic = self.root / 'debian/qore-oracle-instantclient23'
        with zipfile.ZipFile(self.root / 'arm64-basic.zip') as archive:
            self.assertEqual((basic / libdir / 'libclntsh.so.23.1').read_bytes(),
                             archive.read('instantclient_23_26/libclntsh.so.23.1'))
        self.assertEqual((basic / 'etc/ld.so.conf.d/instantclient.conf').read_text(), f'/{libdir}\n')
        link = self.root / 'debian/qore-oracle-libaio-compat/usr/lib/aarch64-linux-gnu/libaio.so.1'
        self.assertEqual(os.readlink(link), 'libaio.so.1t64')
        self.assertFalse((basic / 'usr/lib/x86_64-linux-gnu').exists())

    def test_unknown_architecture_and_mismatched_triplet(self):
        self.assertIn('Unsupported architecture', self.run_installer('riscv64').stderr)
        self.assertIn('Architecture/triplet mismatch', self.run_installer(triplet='x86_64-linux-gnu').stderr)

    def test_wrong_elf_architecture(self):
        self.spec['architectures']['arm64']['basic'] = self.spec['architectures']['amd64']['basic']
        result = self.run_installer(verify=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('ELF architecture mismatch', result.stderr)

    def test_checksum_failure(self):
        (self.root / 'arm64-basic.zip').write_bytes(b'corrupt')
        self.assertIn('Archive checksum mismatch', self.run_installer(verify=True).stderr)

    def test_unsafe_archive_paths(self):
        for name in ['../escape', '/absolute', 'unexpected/file']:
            self.archive('arm64', 'basic', [(name, b'data')])
            result = self.run_installer(verify=True)
            self.assertNotEqual(result.returncode, 0, name)
            self.assertRegex(result.stderr, 'Unsafe|Unexpected')

    def test_unsafe_symlink(self):
        info = zipfile.ZipInfo('instantclient_23_26/escape')
        info.create_system = 3
        info.external_attr = (stat.S_IFLNK | 0o777) << 16
        self.archive('arm64', 'basic', [(info, b'../../escape')])
        self.assertIn('Unsafe archive symlink', self.run_installer(verify=True).stderr)


if __name__ == '__main__':
    unittest.main()
