#!/usr/bin/env python3
"""Exercise the built module's UTF-8 length helper without an Oracle server.

Copyright (C) 2026 Qore Technologies, s.r.o.
Set ORACLE_BUILD_DIR to select a build; ORACLE_TEST_VALGRIND=1 enables Valgrind.
"""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Utf8LengthTest(unittest.TestCase):
    def test_utf8_length(self):
        build = Path(os.environ.get('ORACLE_BUILD_DIR', ROOT / 'build')).resolve()
        modules = list(build.glob('oracle-api-*.qmod'))
        self.assertEqual(1, len(modules), f'build the oracle module in {build} first')
        with tempfile.TemporaryDirectory(prefix='oracle-utf8-test-') as directory:
            executable = Path(directory) / 'utf8-length'
            command = shlex.split(os.environ.get('CXX', 'c++')) + [
                '-g', '-Wall', '-Wextra', '-Werror',
                str(ROOT / 'test/ocilib-utf8-length.cpp'), str(modules[0]),
                '-lqore', '-o', str(executable)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            command = [str(executable)]
            if os.environ.get('ORACLE_TEST_VALGRIND') == '1':
                command = ['valgrind', '--error-exitcode=99', '--leak-check=full',
                           '--errors-for-leak-kinds=definite,indirect'] + command
            # The regression was an infinite loop on every nonempty input.
            result = subprocess.run(command, capture_output=True, text=True,
                                    timeout=180 if os.environ.get('ORACLE_TEST_VALGRIND') == '1' else 5)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertIn('large inputs passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
