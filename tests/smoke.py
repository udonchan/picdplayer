"""Hardware-independent process checks; run: python3 tests/smoke.py build/cdplayerd."""
import signal
import subprocess
import sys
import tempfile

binary = sys.argv[1]
with tempfile.TemporaryDirectory() as directory:
    for args in (['--no-cec'], ['--cec-device', directory + '/absent']):
        for sig in (signal.SIGINT, signal.SIGTERM):
            p = subprocess.Popen([binary, *args], stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, text=True)
            try:
                assert 'started' in p.stdout.readline()
                if '--cec-device' in args:
                    assert 'waiting for' in p.stdout.readline()
                p.send_signal(sig)
                out, err = p.communicate(timeout=3)
                assert p.returncode == 0, (out, err)
                assert f'shutdown signal={int(sig)}' in out, out
            finally:
                if p.poll() is None:
                    p.kill()
                    p.wait()
    p = subprocess.run([binary, '--cec-device', '/dev/null'], capture_output=True,
                       text=True, timeout=3)
    assert p.returncode == 1 and 'CEC ioctl' in p.stderr, p
print('PASS: SIGINT/SIGTERM, missing device wait, invalid device error')
