#!/usr/bin/python3
# Copyright (C) 2026 Qore Technologies, s.r.o.
# SPDX-License-Identifier: MIT
"""Protect mandatory artifact selection and installed test isolation."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

loader = importlib.util.spec_from_file_location('fixture', Path(__file__).with_name('run-tests.py'))
fixture = importlib.util.module_from_spec(loader)
loader.loader.exec_module(fixture)


class ArtifactTests(unittest.TestCase):
    def test_missing_artifact_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            for pattern in ('oracle-api-*.qmod', 'OracleExtensions.qmod', 'OracleExtensions.qm'):
                with self.subTest(pattern=pattern), self.assertRaisesRegex(RuntimeError, 'exactly one'):
                    fixture.artifact([Path(directory)], pattern)

    def test_only_matching_files_are_selected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected = root / 'oracle-api-2.0.qmod'
            expected.touch()
            (root / 'xml-api-2.0.qmod').touch()
            (root / 'oracle-api-directory.qmod').mkdir()
            self.assertEqual(expected.resolve(), fixture.artifact([root], 'oracle-api-*.qmod'))

    def test_duplicate_abi_artifacts_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('oracle-api-2.0.qmod', 'oracle-api-1.5.qmod'):
                (root / name).touch()
            with self.assertRaisesRegex(RuntimeError, 'exactly one'):
                fixture.artifact([root], 'oracle-api-*.qmod')

    def test_duplicate_search_paths_do_not_duplicate_the_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected = root / 'OracleExtensions.qmod'
            expected.touch()
            self.assertEqual(expected.resolve(), fixture.artifact([root, root], expected.name))

    def test_local_search_directives_removed_without_removing_assertions(self):
        source = '%modern\n%prepend-module-path "../build"\n%requires oracle\nassertEq(42, bindInOut(42).value);\n'
        expected = '%modern\n%requires oracle\nassertEq(42, bindInOut(42).value);\n'
        self.assertEqual(expected, fixture.isolated_suite(source))


if __name__ == '__main__':
    unittest.main()
