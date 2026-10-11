"""Frozen acceptance contract for the experimental control inventory candidate."""
import hashlib
import importlib.util
import io
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
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

    def test_integer_mode_without_directories(self):
        entry = self.invoke(archive([('control', tarfile.REGTYPE, b'x')]))['control']
        self.assertIs(type(entry['mode']), int)
        self.assertEqual(entry['mode'], 0o644)

    def test_duplicate_nonregular_entries_are_not_skipped(self):
        for kind in (tarfile.SYMTYPE, tarfile.LNKTYPE, tarfile.FIFOTYPE):
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                self.invoke(archive([('control', tarfile.REGTYPE, b'x'), ('./control', kind, b'')]))

    def test_safe_directories_are_ignored(self):
        entries = [('.', tarfile.DIRTYPE, b''), ('nested/', tarfile.DIRTYPE, b''),
                   ('./control', tarfile.REGTYPE, b'x')]
        self.assertEqual(set(self.invoke(archive(entries))), {'control'})

    def test_unsafe_directories_are_rejected(self):
        for path in ('../', '/outside/'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.invoke(archive([(path, tarfile.DIRTYPE, b''), ('control', tarfile.REGTYPE, b'x')]))

    def test_regular_root_path_is_rejected(self):
        with self.assertRaises(ValueError):
            self.invoke(archive([('.', tarfile.REGTYPE, b'x'), ('control', tarfile.REGTYPE, b'y')]))

    def test_truncated_member_is_invalid_archive(self):
        data = archive([('control', tarfile.REGTYPE, b'x' * 2048)])
        with self.assertRaises(ValueError):
            self.invoke(data[:600])

    @unittest.skipUnless(shutil.which('dpkg-deb'), 'real Debian archive requires dpkg-deb')
    def test_real_debian_archive_does_not_execute_maintainer(self):
        with tempfile.TemporaryDirectory(prefix='control-inventory-') as temporary:
            root = Path(temporary)
            control_dir = root / 'package/DEBIAN'
            control_dir.mkdir(parents=True)
            control = (b'Package: inventory-fixture\nVersion: 1.0\nArchitecture: all\n'
                       b'Maintainer: Test <test@example.invalid>\nDescription: fixture\n')
            (control_dir / 'control').write_bytes(control)
            marker = root / 'must-not-exist'
            script = f'#!/bin/sh\nprintf executed > "{marker}"\n'.encode()
            (control_dir / 'postinst').write_bytes(script)
            (control_dir / 'postinst').chmod(0o755)
            package = root / 'fixture with spaces.deb'
            subprocess.run(['dpkg-deb', '--build', str(root / 'package'), str(package)],
                           check=True, stdout=subprocess.DEVNULL)
            inventory = candidate.control_inventory(package)
            self.assertEqual(inventory['control']['sha256'], hashlib.sha256(control).hexdigest())
            self.assertEqual(inventory['postinst'], {
                'sha256': hashlib.sha256(script).hexdigest(), 'mode': 0o755, 'size': len(script),
            })
            self.assertFalse(marker.exists())

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
