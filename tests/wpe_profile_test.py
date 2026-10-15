#!/usr/bin/env python3
"""Check the real launcher's policy without connecting to DRM or opening ports."""
import os
import subprocess
import sys
import unittest

BINARY, BUILD_TYPE = sys.argv[1:3]
del sys.argv[1:3]


class WpeProfileTest(unittest.TestCase):
    def diagnostics(self, **changes):
        env = dict(os.environ)
        for key in ('PICDPLAYER_INSPECTOR', 'WEBKIT_INSPECTOR_HTTP_SERVER', 'WEBKIT_INSPECTOR_SERVER'):
            env.pop(key, None)
        return subprocess.run([BINARY, '--diagnostics'], env={**env, **changes},
                              capture_output=True, text=True, timeout=5)

    def expect(self, address, **changes):
        result = self.diagnostics(**changes)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f'build_type={BUILD_TYPE}', result.stdout)
        self.assertIn(f'inspector={address}\n', result.stdout)

    def test_default(self):
        self.expect('127.0.0.1:9223' if BUILD_TYPE == 'Debug' else 'off')

    def test_explicit_on(self):
        self.expect('127.0.0.1:9223', PICDPLAYER_INSPECTOR='on')
        self.expect('127.0.0.1:1', WEBKIT_INSPECTOR_HTTP_SERVER='127.0.0.1:1')
        self.expect('127.0.0.1:65535', PICDPLAYER_INSPECTOR='on',
                    WEBKIT_INSPECTOR_HTTP_SERVER='127.0.0.1:65535')

    def test_explicit_off_wins(self):
        self.expect('off', PICDPLAYER_INSPECTOR='off',
                    WEBKIT_INSPECTOR_HTTP_SERVER='0.0.0.0:9223', WEBKIT_INSPECTOR_SERVER='invalid-secret')

    def test_empty_http(self):
        self.expect('off', WEBKIT_INSPECTOR_HTTP_SERVER='')
        self.expect('127.0.0.1:9223', PICDPLAYER_INSPECTOR='on', WEBKIT_INSPECTOR_HTTP_SERVER='')

    def test_invalid_values_are_not_echoed(self):
        for key, values in (
                ('PICDPLAYER_INSPECTOR', ('', 'invalid-secret', 'ON')),
                ('WEBKIT_INSPECTOR_SERVER', ('invalid-secret', '127.0.0.1:9223')),
                ('WEBKIT_INSPECTOR_HTTP_SERVER', (
                    '0.0.0.0:9223', 'localhost:9223', '[::1]:9223', '127.0.0.2:9223',
                    '127.0.0.1:0', '127.0.0.1:65536', '127.0.0.1:999999999999999999999',
                    '127.0.0.1:', '127.0.0.1:+1', '127.0.0.1:9223junk', 'invalid-secret'))):
            for value in values:
                with self.subTest(key=key, value=value):
                    result = self.diagnostics(**{key: value})
                    self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                    self.assertNotIn('invalid-secret', result.stdout + result.stderr)
                    self.assertNotIn('wpe_profile:', result.stdout)

    def test_version_contract(self):
        result = subprocess.run([BINARY, '--version'], capture_output=True, text=True, check=True)
        self.assertEqual(result.stdout, '2.54.0\n')


if __name__ == '__main__':
    unittest.main()
