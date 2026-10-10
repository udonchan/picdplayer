"""Frozen acceptance contract for the experimental control inventory candidate."""
import hashlib
import importlib.util
import io
from pathlib import Path
import tarfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    'candidate', Path(__file__).resolve().parents[1] / 'scripts/debian_control_inventory.py')
candidate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(candidate)


def archive(entries):
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode='w') as output:
        for name, kind, content in entries:
            entry = tarfile.TarInfo(name)
            entry.type = kind
            entry.mode = 0o755 if name == 'postinst' else 0o644
            entry.size = len(content) if kind == tarfile.REGTYPE else 0
            if kind in (tarfile.SYMTYPE, tarfile.LNKTYPE):
                entry.linkname = 'control'
            output.addfile(entry, io.BytesIO(content) if entry.isfile() else None)
    return buffer.getvalue()


class Contract(unittest.TestCase):
    def invoke(self, data):
        with patch('subprocess.check_output', return_value=data) as call:
            result = candidate.control_inventory(Path('/tmp/example.deb'))
            call.assert_called_once_with(['dpkg-deb', '--ctrl-tarfile', '/tmp/example.deb'])
            return result

    def test_regular_files(self):
        control, script = b'Package: example\n', b'#!/bin/sh\nexit 99\n'
        result = self.invoke(archive([('./', tarfile.DIRTYPE, b''), ('./control', tarfile.REGTYPE, control), ('postinst', tarfile.REGTYPE, script)]))
        self.assertEqual(result, {
            'control': {'sha256': hashlib.sha256(control).hexdigest(), 'mode': 0o644, 'size': len(control)},
            'postinst': {'sha256': hashlib.sha256(script).hexdigest(), 'mode': 0o755, 'size': len(script)},
        })

    def test_control_only(self):
        self.assertEqual(set(self.invoke(archive([('control', tarfile.REGTYPE, b'x')]))), {'control'})

    def test_rejections(self):
        valid = ('control', tarfile.REGTYPE, b'x')
        cases = [[('postinst', tarfile.REGTYPE, b'x')], [],
                 [valid, ('../evil', tarfile.REGTYPE, b'x')], [valid, ('/evil', tarfile.REGTYPE, b'x')],
                 [valid, ('./control', tarfile.REGTYPE, b'y')],
                 [valid, ('link', tarfile.SYMTYPE, b'')], [valid, ('link', tarfile.LNKTYPE, b'')],
                 [valid, ('pipe', tarfile.FIFOTYPE, b'')], [valid, ('device', tarfile.CHRTYPE, b'')]]
        for entries in cases:
            with self.subTest(entries=entries), self.assertRaises(ValueError):
                self.invoke(archive(entries))

    def test_malformed(self):
        with self.assertRaises(ValueError):
            self.invoke(b'not a tar archive')


if __name__ == '__main__':
    unittest.main()
