'use strict';
const assert = require('node:assert/strict');
const net = require('node:net');
const { spawn } = require('node:child_process');
const path = require('node:path');
const wrapper = path.join(__dirname, '../systemd/picdplayer-kiosk.sh.in');
const run = (port) => new Promise((resolve, reject) => {
  const child = spawn('/bin/bash', [wrapper], {
    env: { ...process.env, XDG_RUNTIME_DIR: '/tmp', XDG_STATE_HOME: '/tmp',
      PICDPLAYER_CAGE: '/bin/echo', PICDPLAYER_CHROMIUM: '/bin/true',
      PICDPLAYER_API_WAIT_HOST: '127.0.0.1', PICDPLAYER_API_WAIT_PORT: String(port),
      PICDPLAYER_API_WAIT_TIMEOUT_SECONDS: '1' },
  });
  let output = '';
  child.stdout.on('data', (data) => { output += data; });
  child.stderr.on('data', (data) => { output += data; });
  child.on('error', reject);
  child.on('close', (code) => resolve({ code, output }));
});
(async () => {
  const server = net.createServer((socket) => socket.end());
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const port = server.address().port;
  let success;
  try { success = await run(port); }
  finally { await new Promise((resolve) => server.close(resolve)); }
  assert.equal(success.code, 0);
  const markers = [...success.output.matchAll(/event=(\w+) wrapper_elapsed_ms=(\d+)/g)];
  assert.deepEqual(markers.map((m) => m[1]), [
    'kiosk_wrapper_start', 'api_wait_start', 'api_ready', 'cage_exec',
  ]);
  assert(markers.every((m, i) => i === 0 || Number(m[2]) >= Number(markers[i - 1][2])));
  assert(success.output.includes('--remote-debugging-address=127.0.0.1 --remote-debugging-port=9222'));
  const failure = await run(port);
  assert.equal(failure.code, 1);
  assert(failure.output.includes('event=api_wait_start'));
  assert(failure.output.includes('API did not listen'));
  assert(!failure.output.includes('event=api_ready'));
  assert(!failure.output.includes('event=cage_exec'));
  console.log('PASS: wrapper TCP readiness, elapsed markers, preserved arguments and timeout');
})().catch((error) => { console.error(error); process.exitCode = 1; });
