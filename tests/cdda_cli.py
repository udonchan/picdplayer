"""No device access: selection and arguments must fail before opening the device."""
import subprocess
import sys
for args, expected in [
    (['--pcm-output', 'sample.pcm'], 'require --probe-cdda'),
    (['--pcm-output', '-'], 'file path'),
    (['--pcm-output'], 'Usage:'),
    (['--probe-cdda', '/nonexistent', '--cdda-reader', 'typo'], 'unknown CDDA'),
    (['--probe-cdda', '/nonexistent', '--cdda-reader', 'paranoia'], 'not built'),
    (['--probe-cdda', '/nonexistent'], 'explicit'),
    (['--cdda-reader', 'direct'], 'require --probe-cdda'),
    (['--probe-cdda', '/nonexistent', '--probe-drives'], 'one diagnostic'),
    (['--frames', '751'], 'Invalid'),
    (['--frames', '0'], 'Invalid'),
    (['--frames', '12x'], 'Invalid'),
    (['--direct-retries', '11'], 'Invalid'),
]:
    result = subprocess.run([sys.argv[1], *args], capture_output=True, text=True, timeout=3)
    assert result.returncode == 2 and expected in result.stderr, result
print('PASS: CDDA CLI validation')
