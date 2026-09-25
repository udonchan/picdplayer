#!/usr/bin/env python3
"""Exercise deploy/build failure paths without SSH, root, or Docker access."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1]
BUILD = Path(sys.argv.pop(1)) if len(sys.argv) > 1 else None
UNITS = sys.argv.pop(1).split() if len(sys.argv) > 1 else []


class DeploymentScriptsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="picdplayer deploy ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        shutil.copytree(SOURCE / "scripts", self.root / "scripts")
        self.package = self.root / "package-container"
        self.package.mkdir()
        (self.package / "picdplayer_old_arm64.deb").write_text("old artifact")
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.log = self.root / "calls.jsonl"
        self.env = dict(os.environ, PATH=f"{self.bin}:{os.environ['PATH']}",
                        TEST_ROOT=str(self.root), TEST_LOG=str(self.log))
        mock = '''#!/usr/bin/env python3
import json, os, pathlib, sys
name = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ['TEST_LOG'], 'a') as f:
    f.write(json.dumps([name, args]) + '\\n')
command = ' '.join(args)
if name == 'ssh':
    if 'sudo -n -l' in command:
        sys.exit(int(os.environ.get('DENY_SUDO', '0')))
    if 'ActiveState' in command:
        unit = args[-1].split()[-1]
        counter = pathlib.Path(os.environ['TEST_ROOT']) / unit
        after = counter.exists()
        counter.touch()
        print(os.environ.get('AFTER' if after else 'BEFORE', 'inactive'))
    if 'sudo -n /usr/bin/dpkg' in command:
        sys.exit(int(os.environ.get('INSTALL_FAILURE', '0')))
    if 'dpkg-query' in command:
        sys.exit(int(os.environ.get('VERIFY_FAILURE', '0')))
if name == 'docker':
    if args[:2] == ['image', 'inspect']:
        sys.exit(int(os.environ.get('NO_IMAGE', '0')))
    if 'cpack' in args:
        (pathlib.Path(os.environ['TEST_ROOT']) / 'package-container/partial.deb').touch()
        sys.exit(1)
    if '--install' in command:
        binary = pathlib.Path(os.environ['TEST_ROOT']) / 'stage/usr/local/bin/cdplayerd'
        binary.parent.mkdir(parents=True)
        binary.touch()
        binary.chmod(0o755)
    if '-S' in args and os.environ.get('CONFIGURE_FAILURE'):
        sys.exit(1)
'''
        for command in ('ssh', 'rsync', 'docker'):
            path = self.bin / command
            path.write_text(mock)
            path.chmod(0o755)

    def run_script(self, name, **env):
        result = subprocess.run(['bash', str(self.root / 'scripts' / name)],
                                env=dict(self.env, **env), text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        calls = [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []
        return result, calls

    def test_active_and_inactive_preserved(self):
        for state in ('active', 'inactive'):
            with self.subTest(state=state):
                result, _ = self.run_script('deploy.sh', BEFORE=state, AFTER=state)
                self.assertEqual(result.returncode, 0, result.stdout)

    def test_failed_and_transitional_states_block_transfer(self):
        for state in ('failed', 'activating', 'deactivating', ''):
            with self.subTest(state=state):
                result, calls = self.run_script('deploy.sh', BEFORE=state, AFTER=state)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertFalse(any(name == 'rsync' for name, _ in calls))

    def test_sudo_denial_blocks_transfer(self):
        result, calls = self.run_script('deploy.sh', DENY_SUDO='1')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(any(name == 'rsync' for name, _ in calls))

    def test_changed_state_fails(self):
        result, _ = self.run_script('deploy.sh', BEFORE='active', AFTER='inactive')
        self.assertNotEqual(result.returncode, 0)

    def test_install_failure_stops_verification(self):
        result, calls = self.run_script('deploy.sh', INSTALL_FAILURE='1')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(any('dpkg-query' in ' '.join(args) for _, args in calls))

    def test_package_verification_failure(self):
        result, _ = self.run_script('deploy.sh', VERIFY_FAILURE='1')
        self.assertNotEqual(result.returncode, 0)

    def test_ambiguous_packages_block_ssh(self):
        (self.package / 'another.deb').touch()
        result, calls = self.run_script('deploy.sh')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [])

    def test_build_failures_leave_no_deployable_artifact(self):
        for env in ({'NO_IMAGE': '1'}, {'CONFIGURE_FAILURE': '1'}, {}):
            with self.subTest(env=env):
                (self.package / 'old.deb').touch()
                result, _ = self.run_script('build-container.sh', **env)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(list(self.package.glob('*.deb')), [])


@unittest.skipIf(BUILD is None, 'Pass the CMake build directory to test generated scripts')
class MaintainerScriptsTest(unittest.TestCase):
    def run_maintainer(self, script, action, **env):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log = root / 'calls'
            command = root / 'systemctl'
            command.write_text('#!/bin/sh\necho "$*" >> "$TEST_LOG"\n'
                               'case "$*" in "$FAIL_COMMAND") exit 1;; esac\n')
            command.chmod(0o755)
            # Simulate systemd presence without requiring root or a booted host.
            driver = 'test() { case "$*" in "-d /run/systemd/system") return "${SYSTEMD_ABSENT:-0}";; *) command test "$@";; esac; }; script=$1; shift; . "$script"'
            script_path = BUILD / 'packaging' / script if script != 'postrm' else SOURCE / 'packaging/debian/postrm'
            result = subprocess.run(['sh', '-c', driver, 'test', str(script_path), action],
                                    env={**os.environ, 'PATH': f"{root}:{os.environ['PATH']}",
                                         'TEST_LOG': str(log), 'FAIL_COMMAND': '', **env},
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            return result, log.read_text().splitlines() if log.exists() else []

    def test_configure_reload_before_restart(self):
        result, calls = self.run_maintainer('postinst', 'configure')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(calls, ['daemon-reload'] + [f'try-restart {u}' for u in UNITS])

    def test_remove_stops_in_reverse_order(self):
        result, calls = self.run_maintainer('prerm', 'remove')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(calls, [f'stop {u}' for u in reversed(UNITS)])

    def test_stop_failure_aborts_removal(self):
        if not UNITS:
            self.skipTest('No units in this configuration')
        result, calls = self.run_maintainer('prerm', 'remove', FAIL_COMMAND=f'stop {UNITS[-1]}')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [f'stop {UNITS[-1]}'])

    def test_upgrade_does_not_stop_services(self):
        result, calls = self.run_maintainer('prerm', 'upgrade')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(calls, [])

    def test_non_systemd_install_and_remove(self):
        for script, action in [('postinst', 'configure'), ('prerm', 'remove'), ('postrm', 'remove')]:
            result, calls = self.run_maintainer(script, action, SYSTEMD_ABSENT='1')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(calls, [])


if __name__ == '__main__':
    unittest.main()
