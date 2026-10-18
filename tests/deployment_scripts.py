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
        self.real_timeout = shutil.which('timeout')
        self.env = dict(os.environ, PATH=f"{self.bin}:{os.environ['PATH']}",
                        TEST_ROOT=str(self.root), TEST_LOG=str(self.log))
        (self.root / 'uptime').write_text('0.00 0.00\n')
        shutil.copyfile(SOURCE / 'systemd/picdplayer-kiosk.sh.in', self.root / 'wrapper')
        query = self.root / 'budget-query'
        query.write_text('''#!/bin/bash
if [[ ${QUERY_FAILURE:-0} == 1 ]]; then exit 1; fi
if [[ -n ${QUERY_OUTPUT+x} ]]; then printf '%s\\n' "$QUERY_OUTPUT"; exit 0; fi
exec bash "$TEST_ROOT/wrapper" "$@"
''')
        query.chmod(0o755)
        mock = '''#!/usr/bin/env python3
import json, os, pathlib, shlex, subprocess, sys
name = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ['TEST_LOG'], 'a') as f:
    f.write(json.dumps([name, args]) + '\\n')
command = ' '.join(args)
if name == 'ssh':
    if 'bash -s --' in command:
        if os.environ.get('RUNTIME_FAILURE'):
            sys.exit(1)
        script = sys.stdin.read().replace('/proc/uptime', shlex.quote(os.environ['TEST_ROOT'] + '/uptime'))
        script = script.replace('/usr/local/libexec/picdplayer-kiosk', shlex.quote(os.environ['TEST_ROOT'] + '/budget-query'))
        sys.exit(subprocess.run(['bash', '-s', '--', shlex.split(args[-1])[-1]], input=script, text=True, timeout=20).returncode)
    if 'picdplayer-wpe --version' in command:
        sys.exit(int(os.environ.get('WPE_VERSION_FAILURE', '0')))
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
    if 'cat /usr/local/share/picdplayer/build-profile' in command:
        sys.exit(int(os.environ.get('INSTALLED_PROFILE_FAILURE', '0')))
    if '--build-info' in command or '--diagnostics' in command:
        sys.exit(int(os.environ.get('INSTALLED_BINARY_FAILURE', '0')))
if name == 'docker':
    if '/src/stage/usr/local/bin/cdplayerd' in args and '--build-info' in args:
        print('build_type=' + os.environ.get('PICDPLAYER_BUILD_TYPE', 'Release') + ' profile=Release')
    if '/src/scripts/package_profile.py' in args:
        if os.environ.get('OLD_PACKAGE') or os.environ.get('PROFILE_FAILURE'):
            sys.exit(1)
        build_type = os.environ.get('PACKAGE_BUILD_TYPE', 'Release')
        print('build_type=' + build_type)
        print('profile=' + ('Debug' if build_type == 'Debug' else 'Release') + '\\nversion=0.1.0')
    if args and args[0] == 'cp':
        sdk = pathlib.Path(os.environ['TEST_ROOT']) / 'build-wpe-runtime/2.54.0/lib/pkgconfig'
        sdk.mkdir(parents=True, exist_ok=True)
        (sdk / 'wpe-webkit-2.0.pc').touch()
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
if name == 'ps':
    print(os.environ.get('ACTUAL_RUNTIME', 'picdplayer-wpe'))
if name == 'systemctl':
    print('101044')
'''
        for command in ('ssh', 'rsync', 'docker', 'ps', 'systemctl', 'sleep'):
            path = self.bin / command
            path.write_text(mock)
            path.chmod(0o755)
        # Fast polling fixtures advance Linux uptime, not wall time or Bash SECONDS.
        polling = {
            'systemctl': '''case "$*" in
*ActiveState*) [[ ${SYSTEMCTL_FAILURE:-0} == 1 ]] && exit 1; echo "${RUNTIME_STATE-active}"
if [[ -n ${SYSTEMCTL_CLOCK:-} ]]; then echo "$SYSTEMCTL_CLOCK 0.00" > "$TEST_ROOT/uptime"; fi ;;
*MainPID*) [[ ${PID_FAILURE:-0} == 1 ]] && exit 1
if [[ ${BLOCK_PID:-0} == 1 ]]; then /bin/sleep 2; fi
echo "${RUNTIME_PID-101044}" ;;
esac''',
            'ps': '''[[ ${PS_FAILURE:-0} == 1 ]] && exit 1
read -r now unused < "$TEST_ROOT/uptime"
if (( ${now%%.*} < ${RUNTIME_READY_AT:-0} )); then echo bash; else echo "${ACTUAL_RUNTIME:-picdplayer-wpe}"; fi
if [[ -n ${OBSERVATION_CLOCK_JUMP:-} ]]; then echo "$OBSERVATION_CLOCK_JUMP.00 0.00" > "$TEST_ROOT/uptime"; fi''',
            'sleep': '''read -r now unused < "$TEST_ROOT/uptime"
echo "$(( ${now%%.*} + ${VIRTUAL_STEP:-1} )).00 0.00" > "$TEST_ROOT/uptime"''',
            'timeout': '''flag=$1
while [[ $1 == --* ]]; do shift; done
printf '["timeout", ["%s", "%s"]]\\n' "$flag" "$1" >> "$TEST_LOG"
if [[ -n ${REAL_TIMEOUT_BIN:-} ]]; then exec "$REAL_TIMEOUT_BIN" "$flag" "$@"; fi
shift
exec "$@"''',
        }
        for name, body in polling.items():
            path = self.bin / name
            path.write_text('#!/bin/bash\n' + body + '\n')
            path.chmod(0o755)

    def run_script(self, name, **env):
        result = subprocess.run(['bash', str(self.root / 'scripts' / name)],
                                env=dict(self.env, **env), text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
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

    def test_runtime_mismatch_is_not_success(self):
        result, _ = self.run_script('deploy.sh', BEFORE='active', AFTER='active', RUNTIME_FAILURE='1')
        self.assertNotEqual(result.returncode, 0)

    def test_missing_wpe_is_not_success(self):
        result, _ = self.run_script('deploy.sh', WPE_VERSION_FAILURE='1')
        self.assertNotEqual(result.returncode, 0)

    def test_real_runtime_check_rejects_fallback(self):
        result, _ = self.run_script('deploy.sh', BEFORE='active', AFTER='active', ACTUAL_RUNTIME='cage')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('runtime mismatch', result.stdout)

    def test_explicit_chromium_expectation(self):
        result, _ = self.run_script('deploy.sh', BEFORE='active', AFTER='active', ACTUAL_RUNTIME='cage',
                                    PICDPLAYER_EXPECT_KIOSK_RUNTIME='chromium')
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_wrapper_not_execing_runtime_is_not_success(self):
        result, _ = self.run_script('deploy.sh', BEFORE='active', AFTER='active', ACTUAL_RUNTIME='bash')
        self.assertNotEqual(result.returncode, 0)

    def test_inactive_runtime_is_not_started_for_verification(self):
        result, calls = self.run_script('deploy.sh', BEFORE='inactive', AFTER='inactive', QUERY_FAILURE='1')
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertFalse(any('bash -s --' in ' '.join(args) for name, args in calls))

    def test_delayed_runtime_beyond_old_limit_and_maximum_api_wait(self):
        for delay in ('40', '300'):
            with self.subTest(delay=delay):
                (self.root / 'uptime').write_text('0.00 0.00\n')
                result, _ = self.run_script('deploy.sh', BEFORE='active', AFTER='active', RUNTIME_READY_AT=delay)
                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertIn('kiosk_runtime_verified:', result.stdout)

    def test_startup_deadline_boundaries(self):
        budget = int(subprocess.check_output(['bash', str(self.root / 'wrapper'), '--startup-wait-budget-seconds'], text=True)) + 5
        for ready, success in ((budget - 1, True), (budget, False), (budget + 1, False)):
            with self.subTest(ready=ready):
                (self.root / 'uptime').write_text('0.00 0.00\n')
                result, _ = self.run_script('deploy.sh', BEFORE='active', AFTER='active', RUNTIME_READY_AT=str(ready), VIRTUAL_STEP='1')
                self.assertEqual(result.returncode == 0, success, result.stdout)
                if not success:
                    self.assertIn('deadline exceeded', result.stdout)

    def test_late_observation_cannot_report_success(self):
        result, _ = self.run_script('deploy.sh', BEFORE='active', AFTER='active', OBSERVATION_CLOCK_JUMP='1000')
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn('deadline exceeded', result.stdout)

    def test_observation_timeout_uses_fractional_remaining_budget(self):
        result, calls = self.run_script('deploy.sh', BEFORE='active', AFTER='active', SYSTEMCTL_CLOCK='311.99')
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn(('timeout', ['--signal=KILL', '0.01s']), [(name, args) for name, args in calls])

    @unittest.skipUnless(sys.platform == 'linux', 'GNU timeout observation guard')
    def test_real_timeout_kills_blocking_observation(self):
        self.assertIsNotNone(self.real_timeout)
        result, _ = self.run_script('deploy.sh', BEFORE='active', AFTER='active', QUERY_OUTPUT='1',
                                    SYSTEMCTL_CLOCK='5.99', BLOCK_PID='1', REAL_TIMEOUT_BIN=self.real_timeout)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn('PID observation failed', result.stdout)

    def test_budget_is_taken_from_installed_wrapper(self):
        wrapper = self.root / 'wrapper'
        wrapper.write_text(wrapper.read_text().replace('maximum_api_wait_seconds=300', 'maximum_api_wait_seconds=400'))
        result, _ = self.run_script('deploy.sh', BEFORE='active', AFTER='active', RUNTIME_READY_AT='400')
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_invalid_or_failed_budget_is_not_success(self):
        for env in ({'QUERY_FAILURE': '1'}, *({'QUERY_OUTPUT': value} for value in ('', '0', '-1', '01', '1.5', 'abc', '1\n2', '999999999999999999999'))):
            with self.subTest(env=env):
                result, _ = self.run_script('deploy.sh', BEFORE='active', AFTER='active', **env)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertNotIn('Deployment completed successfully', result.stdout)

    def test_runtime_observation_failures_are_not_success(self):
        for env in ({'SYSTEMCTL_FAILURE': '1'}, {'PID_FAILURE': '1'}, {'PS_FAILURE': '1'},
                    {'RUNTIME_PID': '0'}, {'RUNTIME_PID': ''}, {'RUNTIME_STATE': ''}, {'RUNTIME_STATE': 'inactive'}, {'RUNTIME_STATE': 'failed'}):
            with self.subTest(env=env):
                result, _ = self.run_script('deploy.sh', BEFORE='active', AFTER='active', **env)
                self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_explicit_chromium_rejects_wpe(self):
        result, _ = self.run_script('deploy.sh', BEFORE='active', AFTER='active', PICDPLAYER_EXPECT_KIOSK_RUNTIME='chromium')
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn('runtime mismatch', result.stdout)

    def test_invalid_runtime_blocks_transfer(self):
        result, calls = self.run_script('deploy.sh', PICDPLAYER_EXPECT_KIOSK_RUNTIME='invalid')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [])

    def test_build_failures_leave_no_deployable_artifact(self):
        for env in ({'NO_IMAGE': '1'}, {'CONFIGURE_FAILURE': '1'}, {}):
            with self.subTest(env=env):
                (self.package / 'old.deb').touch()
                result, calls = self.run_script('build-container.sh', **env)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(list(self.package.glob('*.deb')), [])
                if not env:
                    self.assertTrue(any('cpack' in args for name, args in calls))
                    for name, args in calls:
                        if name == 'docker' and ('-S' in args or 'cpack' in args or '--install' in ' '.join(args)):
                            self.assertIn('--user', args)
                            self.assertEqual(args[args.index('--user') + 1], f'{os.getuid()}:{os.getgid()}')

    def test_invalid_optional_paranoia_setting_blocks_build(self):
        result, calls = self.run_script('build-container.sh', PICDPLAYER_ENABLE_PARANOIA='invalid')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [])

    def test_profile_mismatch_and_old_package_block_ssh(self):
        for env in ({'PACKAGE_BUILD_TYPE': 'Debug'}, {'OLD_PACKAGE': '1'}):
            result, calls = self.run_script('deploy.sh', **env)
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertFalse(any(name in ('ssh', 'rsync') for name, _ in calls))

    def test_explicit_debug_package(self):
        result, _ = self.run_script('deploy.sh', PACKAGE_BUILD_TYPE='Debug', PICDPLAYER_BUILD_TYPE='Debug')
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_invalid_package_identity_blocks_ssh(self):
        result, calls = self.run_script('deploy.sh', PROFILE_FAILURE='1')
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertFalse(any(name in ('ssh', 'rsync') for name, _ in calls))

    def test_installed_profile_mismatch_is_not_success(self):
        for setting in ('INSTALLED_PROFILE_FAILURE', 'INSTALLED_BINARY_FAILURE'):
            result, _ = self.run_script('deploy.sh', **{setting: '1'})
            self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_invalid_build_type(self):
        result, calls = self.run_script('build-container.sh', PICDPLAYER_BUILD_TYPE='invalid')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [])
        self.assertEqual(list(self.package.glob('*.deb')), [])
        result, calls = self.run_script('deploy.sh', PICDPLAYER_BUILD_TYPE='invalid')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [])


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
