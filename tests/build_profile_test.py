#!/usr/bin/env python3
"""Check the authoritative CMake profile mapping without application dependencies."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BuildProfileTest(unittest.TestCase):
    def test_mapping_and_switch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.20)\nproject(Profile NONE)\n'
                f'include("{ROOT}/cmake/BuildProfile.cmake")\n'
                'file(WRITE "${CMAKE_BINARY_DIR}/identity" '
                '"${CMAKE_BUILD_TYPE}/${PICDPLAYER_PROFILE}/${PICDPLAYER_DEBUG_DIAGNOSTICS}")\n')
            for build_type, expected in (
                    (None, 'Release/Release/0'), ('', 'Release/Release/0'), ('Debug', 'Debug/Debug/1'),
                     ('Release', 'Release/Release/0'),
                     ('Debug', 'Debug/Debug/1'),
                    ('RelWithDebInfo', 'RelWithDebInfo/Release/0'),
                    ('MinSizeRel', 'MinSizeRel/Release/0')):
                args = [] if build_type is None else [f'-DCMAKE_BUILD_TYPE={build_type}']
                result = subprocess.run(['cmake', '-S', str(root), '-B', str(root / 'build'), *args],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual((root / 'build/identity').read_text(), expected)
            for index, args in enumerate((['-DCMAKE_BUILD_TYPE=invalid'], ['-DCMAKE_BUILD_TYPE=debug'],
                                         ['-DCMAKE_BUILD_TYPE=OFF'], ['-DCMAKE_BUILD_TYPE=0'])):
                result = subprocess.run(['cmake', '-S', str(root), '-B', str(root / f'invalid-{index}'), *args],
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Unsupported CMAKE_BUILD_TYPE', result.stdout + result.stderr)
            result = subprocess.run(
                ['cmake', '-S', str(root), '-B', str(root / 'multi'), '-G', 'Ninja Multi-Config',
                 '-DCMAKE_BUILD_TYPE=Release'], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('PiCDPlayer requires a single-config generator', result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
