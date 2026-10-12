#!/usr/bin/env python3
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(Path('/proc/uptime').exists(), 'Linux kiosk wrapper')
class RuntimeTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='picdplayer-kiosk-runtime-')
        self.directory = Path(self.temp.name)
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen(32)
        self.env = {**os.environ, 'XDG_RUNTIME_DIR': self.temp.name, 'XDG_STATE_HOME': self.temp.name,
                    'PICDPLAYER_API_WAIT_PORT': str(self.listener.getsockname()[1]),
                    'PICDPLAYER_API_WAIT_TIMEOUT_SECONDS': '1', 'PICDPLAYER_CAGE': '/bin/echo',
                    'PICDPLAYER_CHROMIUM': '/bin/true', 'PICDPLAYER_KIOSK_RUNTIME': 'wpe',
                    'PICDPLAYER_KIOSK_FALLBACK': 'chromium', 'PICDPLAYER_WPE': str(self.directory / 'wpe')}

    def tearDown(self):
        self.listener.close()
        self.temp.cleanup()

    def wpe(self, version='2.54.0', exit_code=0):
        path = self.directory / 'wpe'
        path.write_text(f'#!/bin/sh\nif [ "$1" = --version ]; then echo {version}; exit 0; fi\necho WPE_EXEC; exit {exit_code}\n')
        path.chmod(0o700)

    def wrapper(self, **changes):
        return subprocess.run(['bash', str(ROOT / 'systemd/picdplayer-kiosk.sh.in')],
                              env={**self.env, **changes}, capture_output=True, text=True, timeout=4)

    def stopped(self, result):
        return subprocess.run(['bash', str(ROOT / 'systemd/picdplayer-kiosk-post-stop.sh.in')],
                              env={**self.env, 'SERVICE_RESULT': result}, capture_output=True, text=True, timeout=2)

    def test_primary(self):
        self.wpe()
        result = self.wrapper()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('effective=wpe', result.stdout)
        self.assertIn('WPE_EXEC', result.stdout)
        self.assertNotIn('event=cage_exec', result.stdout)

    def test_explicit_chromium(self):
        self.wpe()
        result = self.wrapper(PICDPLAYER_KIOSK_RUNTIME='chromium')
        self.assertEqual(result.returncode, 0)
        self.assertIn('effective=chromium reason=explicit', result.stdout)
        self.assertNotIn('WPE_EXEC', result.stdout)

    def test_missing_and_incompatible(self):
        result = self.wrapper()
        self.assertEqual(result.returncode, 0)
        self.assertIn('reason=wpe-missing', result.stdout)
        for version in ('2.48.3', 'invalid'):
            self.wpe(version)
            result = self.wrapper()
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('reason=wpe-incompatible', result.stdout)

    def test_fallback_disabled(self):
        result = self.wrapper(PICDPLAYER_KIOSK_FALLBACK='none')
        self.assertEqual(result.returncode, 1)
        self.assertNotIn('event=cage_exec', result.stdout)

    def test_failure_marks_next_restart_only(self):
        self.wpe(exit_code=1)
        result = self.wrapper()
        self.assertEqual(result.returncode, 1)
        self.assertNotIn('event=cage_exec', result.stdout)
        self.assertEqual(self.stopped('exit-code').returncode, 0)
        self.assertTrue((self.directory / 'chromium-fallback').exists())
        result = self.wrapper()
        self.assertEqual(result.returncode, 0)
        self.assertIn('reason=wpe-previous-failure', result.stdout)

    def test_normal_stop_does_not_arm_fallback(self):
        self.wpe()
        self.assertEqual(self.wrapper().returncode, 0)
        self.assertEqual(self.stopped('success').returncode, 0)
        self.assertFalse((self.directory / 'wpe-attempt').exists())
        self.assertFalse((self.directory / 'chromium-fallback').exists())

    def test_invalid_configuration(self):
        for field in ('PICDPLAYER_KIOSK_RUNTIME', 'PICDPLAYER_KIOSK_FALLBACK'):
            self.assertEqual(self.wrapper(**{field: 'invalid'}).returncode, 2)


if __name__ == '__main__':
    unittest.main()
