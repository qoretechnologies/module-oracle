#!/usr/bin/python3
# Copyright (C) 2026 David Nichols
# SPDX-License-Identifier: MIT
"""Verify and stage the complete, unmodified Basic and SDK distributions."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import stat
import subprocess
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--verify-only', action='store_true')
args = parser.parse_args()
spec = json.loads(Path('upstream.json').read_text())
host_arch = subprocess.check_output(['dpkg-architecture', '-qDEB_HOST_ARCH'], text=True).strip()
multiarch = subprocess.check_output(['dpkg-architecture', '-qDEB_HOST_MULTIARCH'], text=True).strip()
if host_arch not in spec['architectures']:
    raise SystemExit(f'Unsupported architecture: {host_arch}')
arch = spec['architectures'][host_arch]
if multiarch != arch['multiarch']:
    raise SystemExit(f'Architecture/triplet mismatch: {host_arch}/{multiarch}')
libdir = Path('usr/lib', multiarch, 'oracle/23')
prefix = PurePosixPath(spec['root'])
for kind, package in [('basic', 'qore-oracle-instantclient23'),
                      ('sdk', 'qore-oracle-instantclient23-dev')]:
    archive = Path(arch[kind]['file'])
    if hashlib.sha256(archive.read_bytes()).hexdigest() != arch[kind]['sha256']:
        raise SystemExit(f'Archive checksum mismatch: {archive}')
    seen = set()
    with zipfile.ZipFile(archive) as z:
        for member in z.infolist():
            name = PurePosixPath(member.filename)
            if not name.parts or name.is_absolute() or '..' in name.parts or name in seen:
                raise SystemExit(f'Unsafe or duplicate archive path: {name}')
            seen.add(name)
            if name.parts[0] == 'META-INF':
                target = Path('debian', package, 'usr/share/doc', package,
                              'upstream-signatures', *name.parts)
            elif name == prefix or prefix in name.parents:
                target = Path('debian', package, libdir,
                              *name.relative_to(prefix).parts)
            else:
                raise SystemExit(f'Unexpected archive root: {name}')
            mode = member.external_attr >> 16
            if member.is_dir():
                if not args.verify_only:
                    target.mkdir(parents=True, exist_ok=True)
                continue
            data = z.read(member)  # Checks the ZIP member CRC too.
            if stat.S_ISLNK(mode):
                link = PurePosixPath(data.decode('utf-8'))
                if link.is_absolute() or '..' in link.parts:
                    raise SystemExit(f'Unsafe archive symlink: {name}')
            elif mode and not stat.S_ISREG(mode):
                raise SystemExit(f'Unsupported archive member type: {name}')
            if not stat.S_ISLNK(mode) and data.startswith(b'\x7fELF'):
                # Both supported platforms require ELF64, little-endian objects.
                if (len(data) < 64 or data[4:7] != b'\x02\x01\x01'
                        or int.from_bytes(data[18:20], 'little') != arch['elf_machine']):
                    raise SystemExit(f'ELF architecture mismatch: {name} ({host_arch})')
            if args.verify_only:
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists() or target.is_symlink():
                raise SystemExit(f'Refusing to overwrite staged path: {target}')
            if stat.S_ISLNK(mode):
                target.symlink_to(str(link))
            else:
                target.write_bytes(data)
                # Signed ZIP metadata uses mode 000; installed files must be
                # readable. Preserve executable intent with Debian permissions.
                target.chmod(0o755 if mode & 0o111 else 0o644)
    print(f'{kind}: verified {len(seen)} archive members')

if not args.verify_only:
    # Used only by dpkg-shlibdeps. The installed alias belongs to the separate
    # compatibility package, and shlibs.local identifies its system provider.
    scan = Path('debian/compat-scan')
    scan.mkdir()
    scan.joinpath('libaio.so.1').symlink_to(f'/usr/lib/{multiarch}/libaio.so.1t64')
    compat = Path('debian/qore-oracle-libaio-compat/usr/lib', multiarch)
    compat.mkdir(parents=True)
    compat.joinpath('libaio.so.1').symlink_to('libaio.so.1t64')
    config = Path('debian/qore-oracle-instantclient23/etc/ld.so.conf.d/instantclient.conf')
    config.parent.mkdir(parents=True)
    config.write_text(f'/{libdir}\n')
