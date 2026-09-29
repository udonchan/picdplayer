'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(process.argv[2] || path.join(__dirname, '../ui/default/player.js'), 'utf8');

async function settle() {
  for (let i = 0; i < 20; ++i) await Promise.resolve();
}

async function main() {
  const nodes = new Map();
  const sockets = [];
  const frames = [];
  let historyRequests = 0;
  let now = 0;
  const snapshot = {
    schema_version: 1, revision: 1,
    player: { state: 'PLAYING', track_number: 1, position_frames: 75, track_duration_frames: 4500 },
    disc: { state: 'AUDIO_READY', title: 'Album', artist: 'Artist',
      layout: { session_id: 'session-a', disc_generation: 2, start_lba: 0, leadout_lba: 100, tracks: [] } },
    tracks: [{ number: 1, title: 'Track', duration_frames: 4500 }],
    enrichment: { status: 'AVAILABLE' }, artwork: { cover: null },
    drive: { vendor: 'Drive', model: 'Model', digital_audio_extraction: { value: 'UNKNOWN', source: 'NONE' },
      c2_supported: { value: 'NOT_CHECKED', source: 'NONE' }, read_cache: { value: 'UNKNOWN', source: 'NONE' },
      speed_control: { value: 'YES', source: 'KERNEL_REPORTED' }, requested_speed_x: 4,
      speed_request_error: '', current_speed_x: null, read_offset_samples: null },
    read: { session_id: 'session-a', stream_generation: 4, activity: 'READING', effective_strategy: 'direct-single-read',
      queued_blocks: 1, buffer_capacity_frames: 90, read_block_frames: 75, dropped_events: 0,
      read_stall: { inflight_ms: null, timeout_ms: 10000, last_timeout_ms: null },
      policy: { pending: true, requested: { mode: 'REPEAT' }, effective: { mode: 'SINGLE' } },
      history: { included: false, capacity: 128 }, event_window: { first_sequence: 1, last_sequence: 2, worker_dropped: 0 },
      current_playback: { status: 'CLEAN', local_verification: 'SINGLE_READ', start_lba: 20, frames_read: 15,
        disc_generation: 2, direct_retries: 0, read_independence: 'CACHE_POSSIBLE',
        verification: { attempts: 0, overlap: 'MATCHED' } },
      latest: { status: 'UNCERTAIN', local_verification: 'SINGLE_READ', start_lba: 50, frames_read: 15,
        disc_generation: 2, direct_retries: 1, verification: { attempts: 0 } }, active_warning: null },
  };
  const detail = { schema_version: 1, session_id: 'session-a', stream_generation: 4,
    disc_map: { scope: 'DISC', disc_generation: 2, revision: 3, observations_complete: true, capacity: 256,
      regions: [{ start_lba: 0, end_lba: 15, flags: 3 }, { start_lba: 45, end_lba: 60, flags: 36 }] } };
  function node(id) {
    if (nodes.has(id)) return nodes.get(id);
    const value = { textContent: '', hidden: false, dataset: {}, style: {}, complete: false, naturalWidth: 0,
      classList: { contains() { return false; }, add() {}, remove() {} }, removeAttribute() {}, addEventListener() {} };
    nodes.set(id, value); return value;
  }
  vm.runInNewContext(source, {
    performance: { now: () => now }, document: { readyState: 'complete', getElementById: node, addEventListener() {} },
    location: { protocol: 'http:', host: 'localhost' },
    fetch: async (url) => {
      if (url === '/api/read-history') ++historyRequests;
      return { ok: true, json: async () => url === '/api/read-history' ? detail : snapshot };
    },
    WebSocket: class { constructor() { sockets.push(this); } }, requestAnimationFrame: (fn) => frames.push(fn),
    setTimeout() {}, clearTimeout() {}, Promise,
  });
  await settle();
  while (frames.length) frames.shift()();
  sockets[0].onopen();
  assert.equal(node('connection').textContent, 'DAEMON · CONNECTED');
  assert.equal(node('connection').dataset.state, 'connected');
  assert.equal(node('read-policy').textContent, 'SINGLE → REPEAT (PENDING)');
  assert.equal(node('drive-speed-request').textContent, '4x REQUEST ACCEPTED');
  assert.equal(node('drive-speed-current').textContent, 'NOT AVAILABLE');
  assert.equal(node('read-buffer').textContent, '1 / 1 blocks');
  assert.equal(node('read-buffer-meter').style.transform, 'scaleX(1)');
  assert.equal(node('read-stall').textContent, 'idle · limit 10000 ms');
  assert.equal(node('integrity-summary').textContent, 'CURRENT READ · CLEAN');
  assert.match(node('read-current').textContent, /LBA 20–35/);
  assert.match(node('read-current').textContent, /CACHE_POSSIBLE/);
  assert.match(node('read-current').textContent, /overlap MATCHED/);
  assert.match(node('read-map-state').textContent, /2 regions/);
  assert.match(node('disc-map').style.background, /conic-gradient\(from 0deg/);
  assert.equal(node('map-latest').hidden, false);
  now = 1999;
  sockets[0].onmessage({ data: JSON.stringify({ ...snapshot, revision: 2,
    player: { ...snapshot.player, position_frames: 90 } }) });
  await settle();
  assert.equal(historyRequests, 1, 'read-history must not be fetched for every snapshot');

  now = 2000;
  sockets[0].onmessage({ data: JSON.stringify({ ...snapshot, revision: 3,
    player: { ...snapshot.player, position_frames: 105 } }) });
  await settle();
  assert.equal(historyRequests, 2, 'playing snapshots refresh the map at the bounded cadence');

  sockets[0].onmessage({ data: JSON.stringify({ ...snapshot, revision: 1,
    read: { ...snapshot.read, activity: 'FAILED' } }) });
  assert.equal(node('read-activity').textContent, 'READING');

  const nextSession = structuredClone(snapshot);
  nextSession.revision = 1;
  nextSession.read.session_id = 'session-b';
  nextSession.read.current_playback = null;
  nextSession.read.latest = null;
  nextSession.disc = { state: 'NO_DISC', layout: null };
  sockets[0].onmessage({ data: JSON.stringify(nextSession) });
  assert.match(node('read-map-state').textContent, /No accepted audio disc/);
  assert.equal(node('integrity-summary').textContent, 'CURRENT READ · NOT AVAILABLE');
  assert.equal(node('read-current').textContent, 'NOT AVAILABLE');
  assert.equal(node('map-latest').hidden, true);
  sockets[0].onclose();
  assert.equal(node('connection').textContent, 'DAEMON · RECONNECTING');
  assert.equal(node('connection').dataset.state, 'reconnecting');
  console.log('PASS: Integrity summary, bounded map, revision and session handling');
}

main().catch((error) => { console.error(error); process.exitCode = 1; });
