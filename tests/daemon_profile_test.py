#!/usr/bin/env python3
"""Exercise real daemon defaults with ALSA null and no physical devices."""
import os
import subprocess
import sys
import unittest

BINARY, BUILD_TYPE, LOGGER = sys.argv[1:4]
del sys.argv[1:4]


class DaemonProfileTest(unittest.TestCase):
    def run_player(self, level=None, cec=False):
        env = dict(os.environ)
        env.pop('PICDPLAYER_LOG_LEVEL', None)
        env.pop('FANART_TV_API_KEY', None)
        if level is not None:
            env['PICDPLAYER_LOG_LEVEL'] = level
        result = subprocess.run(
            [BINARY, '--player', '/nonexistent-picdplayer-profile-test', '--cdda-reader', 'direct',
             '--audio-device', 'null', '--interactive',
             *(['--cec-device', '/nonexistent-picdplayer-profile-cec'] if cec else ['--no-cec'])],
            input='state\nquit\n', env=env, capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f'build_type={BUILD_TYPE}', result.stdout)
        self.assertIn('NOTICE player:', result.stdout)
        self.assertIn('output stopped', result.stdout)
        self.assertIn('NOTICE player: state=NO_DISC', result.stdout)
        return result

    def test_binary_identity(self):
        result = subprocess.run([BINARY, '--build-info'], capture_output=True, text=True, check=True)
        profile = 'Debug' if BUILD_TYPE == 'Debug' else 'Release'
        self.assertEqual(result.stdout, f'build_type={BUILD_TYPE} profile={profile}\n')

    def test_global_logger_levels(self):
        for level in (None, 'debug', 'info', 'warning', '', 'private-invalid-value'):
            env = dict(os.environ)
            env.pop('PICDPLAYER_LOG_LEVEL', None)
            if level is not None:
                env['PICDPLAYER_LOG_LEVEL'] = level
            result = subprocess.run([LOGGER, '--profile'], env=env, capture_output=True, text=True, check=True)
            effective = level if level in ('debug', 'info', 'warning') else (
                'debug' if BUILD_TYPE == 'Debug' else 'warning')
            self.assertEqual(' DEBUG profile:' in result.stdout, effective == 'debug')
            self.assertEqual(' INFO profile:' in result.stdout, effective in ('debug', 'info'))
            self.assertIn(' NOTICE profile:', result.stdout)
            self.assertIn(' WARN profile:', result.stderr)
            self.assertIn(' ERROR profile:', result.stderr)
            self.assertNotIn('private-invalid-value', result.stdout + result.stderr)

    def test_default(self):
        result = self.run_player()
        self.assertEqual(' INFO ' in result.stdout, BUILD_TYPE == 'Debug')

    def test_cec_waiting_is_operational(self):
        result = self.run_player(cec=True)
        self.assertIn('NOTICE cec: waiting for /nonexistent-picdplayer-profile-cec', result.stdout)

    def test_override(self):
        for level in ('debug', 'info', 'warning'):
            with self.subTest(level=level):
                result = self.run_player(level)
                self.assertEqual(' INFO ' in result.stdout, level != 'warning')

    def test_invalid_override_is_not_echoed(self):
        for level in ('private-invalid-value', ''):
            result = self.run_player(level)
            self.assertIn(' WARN logger:', result.stderr)
            self.assertNotIn('private-invalid-value', result.stdout + result.stderr)
            self.assertEqual(' INFO ' in result.stdout, BUILD_TYPE == 'Debug')


if __name__ == '__main__':
    unittest.main()
