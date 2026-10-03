#!/usr/bin/python3
# Copyright (C) 2026 Qore Technologies, s.r.o.
# SPDX-License-Identifier: MIT
"""Qualify the Oracle driver and both extension forms without a database."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def artifact(directories, pattern):
    matches = sorted({file.resolve() for directory in directories for file in directory.glob(pattern)
                      if file.is_file()})
    if len(matches) != 1:
        raise RuntimeError(f'Expected exactly one {pattern} artifact: {matches!r}')
    return matches[0]


def isolated_suite(source):
    return re.sub(r'^%prepend-module-path .*\n', '', source, flags=re.M)


def run(build=None, compiler=False):
    source = Path(__file__).resolve().parents[1]
    env = os.environ.copy()
    for key in ('QORE_MODULE_DIR', 'QORE_MODULE_DIR_ONLY', 'QORE_INCLUDE_DIR', 'LD_LIBRARY_PATH',
                'LD_PRELOAD', 'ORACLE_HOME', 'ORACLE_INSTANT_CLIENT', 'ORACLE_INCLUDES', 'TNS_ADMIN'):
        env.pop(key, None)
    env.update(LC_ALL='C.UTF-8', TZ='UTC')
    paths = subprocess.check_output(['/usr/bin/qore', '--module-path'], env=env, text=True).strip().split(':')
    installed = [Path(path) for path in paths]
    native = artifact([build.resolve()] if build else installed, 'oracle-api-*.qmod')
    compiled = artifact([build.resolve() / 'qlib-qmod'] if build else installed, 'OracleExtensions.qmod')
    extension = artifact([source / 'qlib'] if build else installed, 'OracleExtensions.qm')
    env.update(QORE_MODULE_DIR=':'.join(dict.fromkeys([str(native.parent), str(compiled.parent), *paths])),
               QORE_MODULE_DIR_ONLY='1')
    with tempfile.TemporaryDirectory(prefix='qore-oracle-rpm-') as directory:
        root = Path(directory)
        suite = root / 'oracle-offline.qtest'
        suite.write_text(isolated_suite((source / 'test/oracle-offline.qtest').read_text()))
        for user_module in (extension, compiled):
            print(f'Qualifying {user_module}', flush=True)
            command = ['/usr/bin/qore', '-b', '--enable-debug', '-l', str(native), '-l', str(user_module)]
            subprocess.run([*command, str(suite), '-v'], env=env, cwd=root, check=True, timeout=120)
        if compiler:
            code = (source / 'debian/tests/compiler').read_text().split("<<'EOF'\n", 1)[1].split('\nEOF', 1)[0]
            (root / 'oracle-smoke.q').write_text(code + '\n')
            subprocess.run(['/usr/bin/qcc', '-o', str(root / 'oracle-smoke'), str(root / 'oracle-smoke.q')],
                           env=env, cwd=root, check=True, timeout=120)
            subprocess.run([str(root / 'oracle-smoke')], env=env, cwd=root, check=True, timeout=30)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--build-dir', type=Path)
    mode.add_argument('--installed', action='store_true')
    parser.add_argument('--compiler', action='store_true')
    args = parser.parse_args()
    run(args.build_dir, args.compiler)
