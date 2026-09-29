#!/usr/bin/python3
# Copyright (C) 2026 David Nichols
# SPDX-License-Identifier: MIT
"""Prepare a source package from checksum-verified Oracle download archives."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--archives', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
recipe = Path(__file__).resolve().parent
spec = json.loads((recipe / 'upstream.json').read_text())
inputs = [recipe / 'upstream.json']
for kind in ('basic', 'sdk'):
    path = args.archives / spec[kind]['file']
    if hashlib.sha256(path.read_bytes()).hexdigest() != spec[kind]['sha256']:
        raise SystemExit(f'Archive checksum mismatch: {path}')
    inputs.append(path)
args.output.mkdir(parents=True, exist_ok=True)
if any(args.output.iterdir()):
    raise SystemExit('Output directory must be empty')
name = 'qore-oracle-instantclient'
tree = args.output / f'{name}-{spec["version"]}'
tree.mkdir()
epoch = int(subprocess.check_output([
    'dpkg-parsechangelog', '-l' + str(recipe / 'debian/changelog'), '-STimestamp']))
archive = args.output / f'{name}_{spec["version"]}.orig.tar.xz'
with tarfile.open(archive, 'w:xz', format=tarfile.GNU_FORMAT, preset=1) as tar:
    for path in sorted(inputs):
        info = tar.gettarinfo(str(path), arcname=f'{tree.name}/{path.name}')
        info.uid = info.gid = 0
        info.uname = info.gname = 'root'
        info.mode = 0o644
        info.mtime = epoch
        with path.open('rb') as f:
            tar.addfile(info, f)
        shutil.copyfile(path, tree / path.name)
shutil.copytree(recipe / 'debian', tree / 'debian')
print(tree)
