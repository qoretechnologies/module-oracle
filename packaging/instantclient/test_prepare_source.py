#!/usr/bin/python3
# Copyright (C) 2026 David Nichols
# SPDX-License-Identifier: MIT
"""Check that shared-cache inputs produce self-contained reproducible sources."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest


class PrepareSourceTest(unittest.TestCase):
    def test_symlinked_cache_and_checksum_rejection(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            recipe = root / 'recipe'
            shutil.copytree(Path(__file__).resolve().parent, recipe)
            spec = json.loads((recipe / 'upstream.json').read_text())
            cache = root / 'cache'
            cache.mkdir()
            expected = {}
            for arch in spec['architectures'].values():
                for kind in ('basic', 'sdk'):
                    item = arch[kind]
                    content = item['file'].encode()
                    target = root / item['file']
                    target.write_bytes(content)
                    (cache / item['file']).symlink_to(target)
                    item['sha256'] = hashlib.sha256(content).hexdigest()
                    expected[item['file']] = content
            (recipe / 'upstream.json').write_text(json.dumps(spec))
            outputs = []
            for name in ('first', 'different-path'):
                output = root / name
                subprocess.run([sys.executable, str(recipe / 'prepare-source.py'),
                                '--archives', str(cache), '--output', str(output)],
                               check=True, stdout=subprocess.DEVNULL)
                archive, = output.glob('*.orig.tar.xz')
                outputs.append(archive.read_bytes())
                with tarfile.open(archive) as tar:
                    for member in tar:
                        self.assertTrue(member.isfile(), member.name)
                        filename = Path(member.name).name
                        content = tar.extractfile(member).read()
                        self.assertEqual(content, expected.get(filename, (recipe / 'upstream.json').read_bytes()))
            self.assertEqual(*outputs)
            next(cache.iterdir()).resolve().write_bytes(b'corrupt download')
            result = subprocess.run([sys.executable, str(recipe / 'prepare-source.py'),
                                     '--archives', str(cache), '--output', str(root / 'bad')],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Archive checksum mismatch', result.stderr)
            self.assertFalse((root / 'bad').exists())


if __name__ == '__main__':
    unittest.main()
