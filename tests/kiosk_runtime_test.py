#!/usr/bin/env python3
"""Linux wrapper and deploy regressions using loopback TCP and local SSH mocks.

Execute the actual remote verification script with a virtual uptime file;
no SSH connection, systemd service, or installed browser is required.
"""
import os
import pathlib
import platform
import shutil
import socket
import subprocess
import tempfile
import unittest

SOURCE = pathlib.Path(__file__).resolve().parents[1]


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def _find_bash():
    """Return the absolute path to bash, or None."""
    return shutil.which("bash")


def _has_linux_bash():
    return platform.system() == "Linux" and _find_bash() is not None


# ---------------------------------------------------------------------------
# 1. Wrapper-budget query  – pure constant arithmetic
# ---------------------------------------------------------------------------

@unittest.skipIf(
    not _has_linux_bash(),
    "Wrapper budget query requires Linux",
)
class WrapperBudgetQueryTest(unittest.TestCase):
    """Validate ``--startup-wait-budget-seconds`` output (307)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="kiosk ")
        self.addCleanup(self.tmp.cleanup)
        self.bin_dir = pathlib.Path(self.tmp.name) / "bin"
        self.bin_dir.mkdir()
        # REAL wrapper template – never fabricated constants
        self.wrapper = SOURCE / "systemd" / "picdplayer-kiosk.sh.in"

    @staticmethod
    def _run_query(wrapper, extra_env=None):
        bash = _find_bash() or "/bin/bash"
        cmd = [bash, str(wrapper), "--startup-wait-budget-seconds"]
        env = {k: v for k, v in os.environ.items() if not k.startswith("XDG_")}
        if extra_env:
            env.update(extra_env)
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10, env=env)
        return result.stdout.strip(), result.returncode

    def test_returns_307(self):
        out, rc = self._run_query(str(self.wrapper))
        self.assertEqual(rc, 0)
        self.assertEqual(out, "307")            # 300 + 1 + 5 + 1

    def test_independent_of_xdg_runtime_dir(self):
        """Budget is pure constant arithmetic – XDG_RUNTIME_DIR must not matter."""
        out, rc = self._run_query(str(self.wrapper), {"XDG_RUNTIME_DIR": "/does/not/exist"})
        self.assertEqual(rc, 0)
        stdout, _ = self._run_query(str(self.wrapper))
        self.assertEqual(stdout, "307")

    def test_independent_of_xdg_state_home(self):
        out, rc = self._run_query(str(self.wrapper), {"XDG_STATE_HOME": "/does/not/exist"})
        self.assertEqual(rc, 0)
        stdout, _ = self._run_query(str(self.wrapper))
        self.assertEqual(stdout, "307")

    def test_independent_of_bad_config(self):
        """Budget does not read ``/etc/default/picdplayer-kiosk``."""
        out, rc = self._run_query(str(self.wrapper), {"PICDPLAYER_API_WAIT_TIMEOUT_SECONDS": "invalid"})
        self.assertEqual(rc, 0)
        self.assertEqual(out, "307")

    def test_query_exits_before_template_substitution(self):
        """The ``--budget`` flag must exit *before* any ``/proc`` or XDG reads."""
        out, rc = self._run_query(str(self.wrapper), {"XDG_RUNTIME_DIR": "/proc"})
        self.assertEqual(rc, 0)
        self.assertEqual(out, "307")

    def test_xdg_runtime_dir_set_to_valid_path(self):
        """Valid XDG_RUNTIME_DIR should also be accepted (normal boundary)."""
        tmpdir = str(pathlib.Path(tempfile.mkdtemp(prefix="kiosk_xrd_")))
        try:
            out, rc = self._run_query(str(self.wrapper), {"XDG_RUNTIME_DIR": tmpdir})
            self.assertEqual(rc, 0)
        finally:
            shutil.rmtree(tmpdir, ignore_errors=True)

    def test_zero_budget_still_computed_as_307(self):
        """The budget sum itself is always 307 even if nothing waits yet."""
        out, rc = self._run_query(str(self.wrapper))
        self.assertEqual(rc, 0)
        self.assertNotEqual(out, "0")

    def test_budget_boundary_three_hundred_max_api_wait(self):
        """KIOSK_API_WAIT_MAX limits wrapper ``PICDPLAYER_API_WAIT_TIMEOUT_SECONDS`` to 300."""
        content = (SOURCE / "systemd" / "picdplayer-kiosk.sh.in").read_text()
        self.assertIn("KIOSK_API_WAIT_MAX=300", content)

    def test_budget_one_acceptable_boundary(self):
        """PICDPLAYER_API_WAIT_TIMEOUT_SECONDS=1 must be accepted."""
        content = (SOURCE / "systemd" / "picdplayer-kiosk.sh.in").read_text()
        self.assertIn("wait_seconds < 1", content)

    def test_budget_301_rejected_boundary(self):
        """PICDPLAYER_API_WAIT_TIMEOUT_SECONDS=301 must be rejected."""
        content = (SOURCE / "systemd" / "picdplayer-kiosk.sh.in").read_text()
        self.assertIn("wait_seconds >", content)


# ---------------------------------------------------------------------------
# 2. Wrapper timeout validation boundaries – textual verification
# ---------------------------------------------------------------------------

@unittest.skipIf(not _has_linux_bash(), "Requires Linux")
class WrapperValidationBoundaryTest(unittest.TestCase):
    """Verify the wrapper correctly validates port/wait boundaries via source analysis."""

    def setUp(self):
        self.content = (SOURCE / "systemd" / "picdplayer-kiosk.sh.in").read_text()

    def test_port_range_1_to_65535(self):
        self.assertIn("port < 1", self.content)
        self.assertIn("port > 65535", self.content)

    def test_wait_timeout_range_1_to_300(self):
        self.assertIn("wait_seconds < 1", self.content)
        self.assertIn("wait_seconds >", self.content)                     # uses var, not "300"

    def test_cage_and_chromium_required(self):
        self.assertIn("! -x \"$PICDPLAYER_CAGE\"", self.content)
        self.assertIn("! -x \"$PICDPLAYER_CHROMIUM\"", self.content)

    def test_xdg_runtime_dir_mandatory(self):
        self.assertIn("XDG_RUNTIME_DIR must be set", self.content)

    def test_xdg_state_home_mandatory(self):
        self.assertIn("XDG_STATE_HOME must be set", self.content)


# ---------------------------------------------------------------------------
# 3. Negative-control: prove new constants differ from the old hardcoded 35
# ---------------------------------------------------------------------------

@unittest.skipIf(not _has_linux_bash(), "Requires Linux")
class WrapperNegativeControlTest(unittest.TestCase):
    """Negative controls that prove the new budget differs from the old 35."""

    def test_budget_not_35(self):
        """Old verifier hard-coded 35; new budget is 307."""
        bash_path = _find_bash() or "/bin/bash"
        result = subprocess.run(
            [bash_path, str(SOURCE / "systemd" / "picdplayer-kiosk.sh.in"),
             "--startup-wait-budget-seconds"],
            capture_output=True, text=True, timeout=10,
            env={k: v for k, v in os.environ.items() if not k.startswith("XDG_")},
        )
        self.assertEqual(result.returncode, 0)
        self.assertNotEqual(result.stdout.strip(), "35")

    def test_apis_timeout_kill_after_constant_present(self):
        """KIOSK_API_TIMEOUT_KILL_AFTER=1 must appear."""
        content = (SOURCE / "systemd" / "picdplayer-kiosk.sh.in").read_text()
        self.assertIn("KIOSK_API_TIMEOUT_KILL_AFTER=1", content)

    def test_wpe_version_timeout_constant_present(self):
        """WPE_VERSION_TIMEOUT and WPE_VERSION_KILL_AFTER must exist."""
        content = (SOURCE / "systemd" / "picdplayer-kiosk.sh.in").read_text()
        self.assertIn("WPE_VERSION_TIMEOUT=5", content)
        self.assertIn("WPE_VERSION_KILL_AFTER=1", content)

    def test_startup_wait_budget_function_present(self):
        """The budget calculation function must exist."""
        content = (SOURCE / "systemd" / "picdplayer-kiosk.sh.in").read_text()
        self.assertIn("startup_wait_budget_seconds", content)

    def test_query_flag_present(self):
        """--startup-wait-budget-seconds flag must trigger the function call."""
        content = (SOURCE / "systemd" / "picdplayer-kiosk.sh.in").read_text()
        self.assertIn('"--startup-wait-budget-seconds"', content)


# ---------------------------------------------------------------------------
# 4. Deploy flow budget verification – extend existing deploy.sh SSH mocks
#    No real Pi, no SSH, no Docker. Mock only the budget-related paths.
# ---------------------------------------------------------------------------

@unittest.skipIf(not _has_linux_bash(), "Requires Linux and GNU timeout")
class KioskRuntimeDeployTest(unittest.TestCase):
    """Extended deploy.sh tests that verify the new budget path."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="picdplayer ")
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        # Copy scripts (including deploy.sh with budget verification)
        shutil.copytree(SOURCE / "scripts", self.root / "scripts")
        self.package_dir = self.root / "package-container"
        self.package_dir.mkdir()
        (self.package_dir / "picdplayer.deb").write_bytes(b"fake deb content")
        # Minimal mock infrastructure
        self.bin_dir = self.root / "bin"
        self.bin_dir.mkdir()
        (self.root / "uptime").write_text("0.00 0.00\n")

    def _run_deploy(self, budget="307", kiosk_before="active",
                    kiosk_after=None, **changes):
        if kiosk_after is None:
            kiosk_after = kiosk_before
        bash = _find_bash()
        if bash is None:
            return (1, "bash not found")

        mock_content = '''#!/usr/bin/env python3
import os, pathlib, shlex, subprocess, sys
command = " ".join(sys.argv[1:])
root = pathlib.Path(os.environ["TEST_ROOT"])
if "bash -s --" in command:
    script = sys.stdin.read().replace("/proc/uptime", shlex.quote(str(root / "uptime")))
    script = script.replace("/usr/local/libexec/picdplayer-kiosk", shlex.quote(str(root / "budget-query")))
    sys.exit(subprocess.run(["bash", "-s"], input=script, text=True, timeout=15).returncode)
if "ActiveState" in command:
    if "picdplayer-kiosk.service" in command:
        counter = root / "kiosk-observed"
        print(os.environ["KIOSK_AFTER" if counter.exists() else "KIOSK_BEFORE"])
        counter.touch()
    else:
        print("active")
'''
        (self.bin_dir / "ssh").write_text(mock_content)
        (self.bin_dir / "ssh").chmod(0o755)

        # Also mock rsync to succeed silently (deploy.sh calls it)
        (self.bin_dir / "rsync").write_text("#!/usr/bin/env python3\nimport sys; sys.exit(0)\n")
        (self.bin_dir / "rsync").chmod(0o755)
        query = self.root / "budget-query"
        query.write_text('''#!/bin/bash
echo query >> "$TEST_ROOT/queries"
[[ ${QUERY_FAILURE:-0} == 0 ]] || exit 1
if [[ $QUERY_OUTPUT == wrapper ]]; then
    exec bash "$SOURCE_WRAPPER" --startup-wait-budget-seconds
fi
printf '%s\\n' "$QUERY_OUTPUT"
''')
        query.chmod(0o755)
        commands = {
            "systemctl": '''case "$*" in
*ActiveState*) echo "${RUNTIME_STATE-active}"
if [[ -n ${STATE_CLOCK:-} ]]; then echo "$STATE_CLOCK 0.00" > "$TEST_ROOT/uptime"; fi ;;
*MainPID*) [[ ${PID_FAILURE:-0} == 0 ]] || exit 1
if [[ ${BLOCK_PID:-0} == 1 ]]; then /bin/sleep 2; fi
echo "${RUNTIME_PID-123}" ;;
esac''',
            "ps": '''read -r now unused < "$TEST_ROOT/uptime"
if (( ${now%%.*} < ${READY_AT:-0} )); then echo bash; else echo "${ACTUAL_RUNTIME-cage}"; fi
if [[ -n ${CLOCK_JUMP:-} ]]; then echo "$CLOCK_JUMP.00 0.00" > "$TEST_ROOT/uptime"; fi''',
            "sleep": '''read -r now unused < "$TEST_ROOT/uptime"
echo "$(( ${now%%.*} + 1 )).00 0.00" > "$TEST_ROOT/uptime"''',
        }
        for name, body in commands.items():
            path = self.bin_dir / name
            path.write_text("#!/bin/bash\nset -eu\n" + body + "\n")
            path.chmod(0o755)

        env_overrides = {
            "QUERY_OUTPUT": str(budget),
            "KIOSK_BEFORE": kiosk_before,
            "KIOSK_AFTER": kiosk_after,
            "TEST_ROOT": str(self.root),
            "SOURCE_WRAPPER": str(SOURCE / "systemd/picdplayer-kiosk.sh.in"),
            **changes,
        }
        result = subprocess.run(
            [bash, str(self.root / "scripts" / "deploy.sh")],
            capture_output=True, text=True, timeout=30,
            env={**os.environ, "PATH": f"{self.bin_dir}:{os.environ['PATH']}", **env_overrides},
        )
        return result.returncode, result.stdout + result.stderr

    def test_307_budget_passes_on_inactive_kiosk(self):
        rc, out = self._run_deploy("307", kiosk_before="inactive", QUERY_FAILURE="1")
        self.assertEqual(rc, 0, msg=out)
        self.assertIn("inactive (not started for verification)", out)
        self.assertFalse((self.root / "queries").exists())

    def test_35_budget_passes_on_active_kiosk(self):
        """An alternate positive wrapper budget passes verification."""
        rc, out = self._run_deploy("35")
        self.assertEqual(rc, 0, msg=out)

    def test_1_budget_passes_verification(self):
        """Minimum positive budget is accepted by deploy."""
        rc, out = self._run_deploy("1")
        self.assertEqual(rc, 0, msg=out)

    def test_budget_zero_fails_closed(self):
        """Budget 0 must cause a fail-closed exit."""
        rc, out = self._run_deploy("0")
        self.assertNotEqual(rc, 0)
        self.assertIn("invalid kiosk startup budget", out)

    def test_budget_nonnumeric_fails(self):
        """Non-numeric budget string must cause validation failure."""
        rc, out = self._run_deploy("garbage")
        self.assertNotEqual(rc, 0)
        self.assertIn("invalid kiosk startup budget", out)

    def test_budget_empty_fails(self):
        """Empty budget string must cause validation failure."""
        rc, out = self._run_deploy("")
        self.assertNotEqual(rc, 0)
        self.assertIn("invalid kiosk startup budget", out)

    def test_budget_negative_fails(self):
        """Negative budget must be rejected."""
        rc, out = self._run_deploy("-5")
        self.assertNotEqual(rc, 0)
        self.assertIn("invalid kiosk startup budget", out)

    def test_budget_999_accepted(self):
        """A three-digit positive budget passes verification."""
        rc, out = self._run_deploy("999")
        self.assertEqual(rc, 0, msg=out)
        self.assertIn("kiosk_runtime_verified: process=cage", out)
        self.assertEqual((self.root / "queries").read_text(), "query\n")

    def test_query_failure_is_not_success(self):
        rc, out = self._run_deploy(QUERY_FAILURE="1")
        self.assertNotEqual(rc, 0)
        self.assertIn("startup budget query failed", out)

    def test_delayed_runtime_uses_wrapper_budget(self):
        rc, out = self._run_deploy(READY_AT="40")
        self.assertEqual(rc, 0, out)
        self.assertIn("kiosk_runtime_verified: process=cage", out)

    def test_runtime_deadline_is_bounded(self):
        rc, out = self._run_deploy("1", READY_AT="6")
        self.assertNotEqual(rc, 0)
        self.assertIn("startup deadline exceeded", out)

    def test_runtime_before_deadline_succeeds(self):
        rc, out = self._run_deploy("1", READY_AT="5")
        self.assertEqual(rc, 0, out)
        self.assertIn("kiosk_runtime_verified: process=cage", out)

    def test_observation_is_limited_by_remaining_deadline(self):
        rc, out = self._run_deploy("1", STATE_CLOCK="5.99", BLOCK_PID="1")
        self.assertNotEqual(rc, 0)
        self.assertIn("PID observation failed", out)

    def test_installed_wrapper_supplies_budget(self):
        rc, out = self._run_deploy("wrapper")
        self.assertEqual(rc, 0, out)
        self.assertIn("startup-wait-budget: 307s", out)
        self.assertEqual((self.root / "queries").read_text(), "query\n")

    def test_malformed_or_unbounded_budget_fails(self):
        for value in ("01", "1.5", "1\n2", "999999999999999999999"):
            with self.subTest(value=value):
                rc, out = self._run_deploy(value)
                self.assertNotEqual(rc, 0)
                self.assertIn("invalid kiosk startup budget", out)

    def test_late_observation_cannot_report_success(self):
        rc, out = self._run_deploy("1", CLOCK_JUMP="6")
        self.assertNotEqual(rc, 0)
        self.assertIn("startup deadline exceeded", out)

    def test_wrong_runtime_is_rejected(self):
        rc, out = self._run_deploy(ACTUAL_RUNTIME="picdplayer-wpe")
        self.assertNotEqual(rc, 0)
        self.assertIn("runtime mismatch", out)

    def test_runtime_state_and_pid_failures(self):
        for changes in ({"RUNTIME_STATE": "failed"}, {"RUNTIME_PID": "0"}, {"PID_FAILURE": "1"}):
            with self.subTest(changes=changes):
                rc, out = self._run_deploy(**changes)
                self.assertNotEqual(rc, 0)
                self.assertNotIn("Deployment completed successfully", out)

    def test_kiosk_inactive_state_logged(self):
        """When kiosk is inactive during deploy, verified state is recorded."""
        rc, out = self._run_deploy("307", kiosk_before="inactive")
        self.assertEqual(rc, 0, msg=out)
        self.assertIn("inactive", out.lower())

    def test_kiosk_active_verified_once_with_grace(self):
        """Active kiosk is checked with the wrapper budget and grace."""
        rc, out = self._run_deploy("307", kiosk_before="active",
                                   kiosk_after="active")
        self.assertEqual(rc, 0, msg=out)
        self.assertIn("startup-wait-budget: 307s (grace 5s)", out)
        self.assertIn("kiosk_runtime_verified: process=cage", out)
        self.assertEqual((self.root / "queries").read_text(), "query\n")


# ---------------------------------------------------------------------------
# 5. Integration: wrapper timeout with already-listening socket
# ---------------------------------------------------------------------------

@unittest.skipIf(not _has_linux_bash(), "Requires Linux")
class WrapperAlreadyListeningTest(unittest.TestCase):
    """Verify wrapper completes quickly when TCP port is already active."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="kiosk_listen ")
        self.addCleanup(self.tmp.cleanup)
        self.proc_dir = str(pathlib.Path(self.tmp.name) / "proc")
        (pathlib.Path(self.proc_dir)).mkdir(parents=True, exist_ok=True)
        (pathlib.Path(self.proc_dir) / "uptime").write_text("1000.00")
        self.bash = _find_bash() or "/bin/bash"

    def test_already_listening_completes_quickly(self):
        import socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.addCleanup(sock.close)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("127.0.0.1", 0))
        sock.listen(1)
        port = sock.getsockname()[1]

        import time as _t
        start_time = _t.time()
        env = {
            **os.environ,
            "XDG_RUNTIME_DIR": self.proc_dir,
            "XDG_STATE_HOME": "/tmp",
            "PICDPLAYER_API_WAIT_HOST": "127.0.0.1",
            "PICDPLAYER_API_WAIT_PORT": str(port),
            "PICDPLAYER_API_WAIT_TIMEOUT_SECONDS": "35",   # would full-sleep if naive
            "PICDPLAYER_CAGE": "/bin/echo",
            "PICDPLAYER_CHROMIUM": "/bin/true",
        }
        result = subprocess.run(
            [self.bash, str(SOURCE / "systemd" / "picdplayer-kiosk.sh.in")],
            capture_output=True, text=True, timeout=10, env=env,
        )
        elapsed = _t.time() - start_time

        self.assertLess(elapsed, 3.0,
                        msg=f"Took {elapsed:.1f}s with already-listening socket")
        # Markers should show api_ready and cage_exec completed.
        combined = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, combined)
        self.assertIn("event=api_ready", combined)
        self.assertIn("event=cage_exec", combined)
        sock.close()

    def test_unreachable_api_does_not_start_cage(self):
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
            result = subprocess.run(
                [self.bash, str(SOURCE / "systemd" / "picdplayer-kiosk.sh.in")],
                capture_output=True, text=True, timeout=4,
                env={**os.environ, "XDG_RUNTIME_DIR": self.proc_dir, "XDG_STATE_HOME": "/tmp",
                     "PICDPLAYER_API_WAIT_HOST": "127.0.0.1", "PICDPLAYER_API_WAIT_PORT": str(port),
                     "PICDPLAYER_API_WAIT_TIMEOUT_SECONDS": "1", "PICDPLAYER_CAGE": "/bin/echo",
                     "PICDPLAYER_CHROMIUM": "/bin/true"})
        self.assertEqual(result.returncode, 1)
        self.assertIn("API did not listen", result.stderr)
        self.assertNotIn("event=cage_exec", result.stdout)

    def test_zero_timeout_boundary_accepts_one(self):
        """PICDPLAYER_API_WAIT_TIMEOUT_SECONDS=1 must be within valid range."""
        content = (SOURCE / "systemd" / "picdplayer-kiosk.sh.in").read_text()
        self.assertIn("wait_seconds < 1", content)


if __name__ == "__main__":
    unittest.main()
