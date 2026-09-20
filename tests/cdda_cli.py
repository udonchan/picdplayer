"""No device access: selection and arguments must fail before opening the device."""
import subprocess
import sys
for args, expected in [
    (['--player', '/nonexistent'], 'explicit'),
    (['--player', '/nonexistent', '--cdda-reader', 'typo'], 'unknown CDDA'),
    (['--player', '/nonexistent', '--cdda-reader', 'direct', '--frames', '75'], 'Player accepts'),
    (['--audio-device', 'null'], 'Player accepts'),
    (['--audio-latency-ms', '99'], 'Invalid --audio-latency-ms'),
    (['--audio-latency-ms', '2001'], 'Invalid --audio-latency-ms'),
    (['--audio-latency-ms', '500'], 'Player accepts'),
    (['--interactive'], 'requires --player'),
    (['--pcm-output', 'sample.pcm'], 'require --probe-cdda'),
    (['--pcm-output', '-'], 'file path'),
    (['--pcm-output'], 'Usage:'),
    (['--probe-cdda', '/nonexistent', '--cdda-reader', 'typo'], 'unknown CDDA'),

    (['--probe-cdda', '/nonexistent'], 'explicit'),
    (['--cdda-reader', 'direct'], 'require --probe-cdda'),
    (['--probe-cdda', '/nonexistent', '--probe-drives'], 'one diagnostic'),
    (['--probe-disc-id', '/nonexistent', '--probe-toc', '/nonexistent'], 'one diagnostic'),
    (['--probe-drive-start'], 'Usage:'),
    (['--probe-drive-start', '/nonexistent', '--probe-toc', '/nonexistent'], 'one diagnostic'),
    (['--probe-disc-id'], 'Usage:'),
    (['--probe-metadata'], 'Usage:'),
    (['--lookup-disc'], 'Usage:'),
    (['--metadata', 'typo'], 'Unknown metadata backend'),
    (['--metadata', 'musicbrainz'], 'requires --player'),
    (['--metadata-cache', '/tmp/cache'], 'requires a metadata diagnostic'),
    (['--api-port', '0'], 'Invalid --api-port'),
    (['--api-port', '65536'], 'Invalid --api-port'),
    (['--api-port', '8080'], 'requires --player'),
    (['--api-listen'], 'Usage:'),
    (['--api-listen', 'localhost'], 'numeric IP'),
    (['--api-listen', '0.0.0.0'], 'requires --api-port'),
    (['--read-buffer-frames', '300'], 'require --player'),
    (['--read-buffer-frames', '14'], 'Invalid'),
    (['--read-buffer-frames', '301'], 'Invalid'),
    (['--read-buffer-frames', '2265'], 'Invalid'),
    (['--startup-buffer-frames', '0'], 'Invalid'),
    (['--read-verification', 'repeat'], 'requires --player'),
    (['--read-verification', 'unknown'], 'Invalid --read-verification'),
    (['--player', '/nonexistent', '--cdda-reader', 'direct', '--read-buffer-frames', '150',
      '--startup-buffer-frames', '300'], 'Invalid playback buffer'),
    (['--player', '/nonexistent', '--cdda-reader', 'direct', '--read-verification', 'repeat',
      '--read-buffer-frames', '15', '--startup-buffer-frames', '15'], 'fit the read buffer'),
    (['--probe-metadata', '/nonexistent', '--probe-toc', '/nonexistent'], 'one diagnostic'),
    (['--custom-ui', '/nonexistent'], '--custom-ui requires'),
    (['--custom-ui', '', '--api-port', '8080'], '--custom-ui requires'),
    (['--frames', '751'], 'Invalid'),
    (['--frames', '0'], 'Invalid'),
    (['--frames', '12x'], 'Invalid'),
    (['--direct-retries', '11'], 'Invalid'),
]:
    result = subprocess.run([sys.argv[1], *args], capture_output=True, text=True, timeout=3)
    assert result.returncode == 2 and expected in result.stderr, result
print('PASS: CDDA CLI validation')

result = subprocess.run([sys.argv[1], '--probe-drive-start', '/nonexistent'],
                        capture_output=True, text=True, timeout=3)
assert result.returncode == 1 and 'open /nonexistent' in result.stderr, result

result = subprocess.run([sys.argv[1], '--probe-cdda', '/nonexistent', '--cdda-reader', 'paranoia'], capture_output=True, text=True, timeout=3)
if sys.argv[2] == 'ON':
    assert result.returncode == 1 and 'open /nonexistent' in result.stderr, result
else:
    assert result.returncode == 2 and 'not built' in result.stderr, result

result = subprocess.run([sys.argv[1], '--probe-disc-id', '/nonexistent'], capture_output=True, text=True, timeout=3)
if sys.argv[3] == 'ON':
    assert result.returncode == 1 and 'open /nonexistent' in result.stderr, result
else:
    assert result.returncode == 2 and 'metadata support is not built' in result.stderr, result

if sys.argv[4] == 'OFF':
    result = subprocess.run([sys.argv[1], '--player', '/nonexistent', '--cdda-reader', 'direct',
                             '--api-port', '8080'], capture_output=True, text=True, timeout=3)
    assert result.returncode == 2 and 'API support is not built' in result.stderr, result

result = subprocess.run([sys.argv[1], '--lookup-disc', 'bad/id'], capture_output=True, text=True, timeout=3)
if sys.argv[3] == 'ON':
    assert result.returncode == 1 and 'invalid MusicBrainz Disc ID' in result.stderr, result
else:
    assert result.returncode == 2 and 'metadata support is not built' in result.stderr, result
