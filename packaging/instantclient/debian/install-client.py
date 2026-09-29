#!/usr/bin/python3
# Copyright (C) 2026 David Nichols
# SPDX-License-Identifier: MIT
"""Verify and stage the complete, unmodified Basic and SDK distributions."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import stat
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--verify-only', action='store_true')
args = parser.parse_args()
spec = json.loads(Path('upstream.json').read_text())
prefix = PurePosixPath(spec['root'])
for kind, package in [('basic', 'qore-oracle-instantclient23'),
                      ('sdk', 'qore-oracle-instantclient23-dev')]:
    archive = Path(spec[kind]['file'])
    if hashlib.sha256(archive.read_bytes()).hexdigest() != spec[kind]['sha256']:
        raise SystemExit(f'Archive checksum mismatch: {archive}')
    seen = set()
    with zipfile.ZipFile(archive) as z:
        for member in z.infolist():
            name = PurePosixPath(member.filename)
            if name.is_absolute() or '..' in name.parts or name in seen:
                raise SystemExit(f'Unsafe or duplicate archive path: {name}')
            seen.add(name)
            if name.parts[0] == 'META-INF':
                target = Path('debian', package, 'usr/share/doc', package,
                              'upstream-signatures', *name.parts)
            elif name == prefix or prefix in name.parents:
                target = Path('debian', package, 'usr/lib/x86_64-linux-gnu/oracle/23',
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
    scan.joinpath('libaio.so.1').symlink_to('/usr/lib/x86_64-linux-gnu/libaio.so.1t64')
