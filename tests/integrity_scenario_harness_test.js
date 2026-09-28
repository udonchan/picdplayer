'use strict';

const assert = require('node:assert/strict');
const http = require('node:http');
const { spawn } = require('node:child_process');

const harness = process.argv[2];
assert.ok(harness, 'harness executable argument is required');

function get(port, pathname) {
  return new Promise((resolve, reject) => {
    const request = http.get({ host: '127.0.0.1', port, path: pathname }, (response) => {
      let body = '';
      response.setEncoding('utf8');
      response.on('data', (chunk) => { body += chunk; });
      response.on('end', () => resolve({ status: response.statusCode, body }));
    });
    request.on('error', reject);
  });
}

function sleep(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

async function eventually(action, description) {
  let lastError;
  for (let attempt = 0; attempt < 40; ++attempt) {
    try { return await action(); } catch (error) { lastError = error; }
    await sleep(25);
  }
  throw new Error(`${description}: ${lastError?.message || 'timed out'}`);
}

async function runScenario(name, expectedStatus, port) {
  const child = spawn(harness, ['--scenario', name, '--port', String(port), '--read-delay-ms', '20'],
    { stdio: ['ignore', 'pipe', 'pipe'] });
  let stdout = '';
  child.stdout.setEncoding('utf8');
  child.stdout.on('data', (chunk) => { stdout += chunk; });
  let stderr = '';
  child.stderr.setEncoding('utf8');
  child.stderr.on('data', (chunk) => { stderr += chunk; });
  try {
    const page = await eventually(async () => {
      const result = await get(port, '/player');
      assert.equal(result.status, 200);
      assert.match(result.body, /player\.js/);
      return result;
    }, `${name}: Player page did not become available`);
    assert.match(page.body, /PiCDPlayer/);
    assert.match(stdout, new RegExp(`INTEGRITY_SCENARIO_HARNESS development_only=1 scenario=${name}`));

    const state = await eventually(async () => {
      const result = await get(port, '/api/state');
      assert.equal(result.status, 200);
      const snapshot = JSON.parse(result.body);
      assert.equal(snapshot.read.latest?.status, expectedStatus);
      if (name !== 'uncertain' && name !== 'read-ahead')
        assert.notEqual(snapshot.read.current_playback, null);
      return snapshot;
    }, `${name}: expected latest read status ${expectedStatus}`);
    assert.equal(state.drive.device, 'fixture');
    assert.equal(state.read.session_id, `fixture-${name}`);
    if (name === 'read-ahead' || name === 'uncertain')
      assert.equal(state.read.current_playback, null);

    const history = await eventually(async () => {
      const result = await get(port, '/api/read-history');
      assert.equal(result.status, 200);
      const detail = JSON.parse(result.body);
      assert.equal(detail.session_id, `fixture-${name}`);
      assert.ok(Array.isArray(detail.history?.regions));
      assert.ok(Array.isArray(detail.disc_map?.regions));
      return detail;
    }, `${name}: read history did not become available`);
    assert.ok(history.history.regions.length > 0);
    assert.ok(history.disc_map.regions.length > 0);
  } finally {
    child.kill('SIGTERM');
    await new Promise((resolve) => child.once('exit', resolve));
    assert.equal(child.exitCode, 0, `${name}: harness failed: ${stderr}\n${stdout}`);
  }
}

async function main() {
  const scenarios = [
    ['clean', 'CLEAN'], ['retry', 'UNCERTAIN'], ['recovered', 'RECOVERED'],
    ['uncertain', 'UNCERTAIN'], ['mixed', 'RECOVERED'], ['read-ahead', 'CLEAN'],
    ['transition', 'RECOVERED'],
  ];
  for (const [index, [name, status]] of scenarios.entries()) {
    await runScenario(name, status, 18100 + index);
  }
  console.log('PASS: Integrity scenario harness exposes Player, state, and history for all scenarios');
}

main().catch((error) => { console.error(error); process.exitCode = 1; });
