#!/usr/bin/env python3
"""Exercise the masking of connection values in Oracle messages (qore#6991) without an Oracle server.

Copyright (C) 2026 Qore Technologies, s.r.o.
ORACLE_TEST_SANITIZE=1 builds the test with ASan and UBSan; ORACLE_TEST_VALGRIND=1 enables Valgrind.
"""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class OracleRedactTest(unittest.TestCase):
    def test_redact(self):
        with tempfile.TemporaryDirectory(prefix='oracle-redact-test-') as directory:
            executable = Path(directory) / 'oracle-redact'
            command = shlex.split(os.environ.get('CXX', 'c++')) + [
                '-g', '-std=c++11', '-Wall', '-Wextra', '-Werror']
            # valgrind does not run sanitized programs
            if os.environ.get('ORACLE_TEST_SANITIZE') == '1' and os.environ.get('ORACLE_TEST_VALGRIND') != '1':
                command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
            command += [str(ROOT / 'test/oracle-redact.cpp'), '-o', str(executable)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            command = [str(executable)]
            if os.environ.get('ORACLE_TEST_VALGRIND') == '1':
                command = ['valgrind', '--error-exitcode=99', '--leak-check=full',
                           '--errors-for-leak-kinds=definite,indirect'] + command
            # the sanitizer runtime must be the first library loaded, so no other library is preloaded
            env = {k: v for k, v in os.environ.items() if k != 'LD_PRELOAD'}
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, env=env)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertIn('all redaction checks passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
