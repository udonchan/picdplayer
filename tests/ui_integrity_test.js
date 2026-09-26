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
  const snapshot = {
    schema_version: 1, revision: 1,
    player: { state: 'PLAYING', track_number: 1, position_frames: 75, track_duration_frames: 4500 },
    disc: { state: 'AUDIO_READY', title: 'Album', artist: 'Artist',
      layout: { session_id: 'session-a', disc_generation: 2, start_lba: 0, leadout_lba: 100, tracks: [] } },
    tracks: [{ number: 1, title: 'Track', duration_frames: 4500 }],
    enrichment: { status: 'AVAILABLE' }, artwork: { cover: null },
    drive: { vendor: 'Drive', model: 'Model', digital_audio_extraction: { value: 'UNKNOWN', source: 'NONE' },
      c2_supported: { value: 'NOT_CHECKED', source: 'NONE' }, read_cache: { value: 'UNKNOWN', source: 'NONE' },
      speed_control: { value: 'YES', source: 'KERNEL_REPORTED' }, read_offset_samples: null },
    read: { session_id: 'session-a', stream_generation: 4, activity: 'READING', effective_strategy: 'direct-single-read',
      queued_blocks: 1, buffer_capacity_frames: 90, read_block_frames: 75, dropped_events: 0,
      policy: { pending: true, requested: { mode: 'REPEAT' }, effective: { mode: 'SINGLE' } },
      history: { included: false, capacity: 128 }, event_window: { first_sequence: 1, last_sequence: 2, worker_dropped: 0 },
      current_playback: { status: 'CLEAN', local_verification: 'SINGLE_READ', start_lba: 20, frames_read: 15,
        disc_generation: 2, direct_retries: 0, verification: { attempts: 0 } },
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
    performance: { now: () => 1 }, document: { readyState: 'complete', getElementById: node, addEventListener() {} },
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
  assert.equal(node('read-policy').textContent, 'SINGLE → REPEAT (PENDING)');
  assert.equal(node('read-buffer').textContent, '1 / 1 blocks');
  assert.match(node('read-current').textContent, /LBA 20–35/);
  assert.match(node('read-map-state').textContent, /2 regions/);
  assert.match(node('disc-map').style.background, /conic-gradient/);
  assert.equal(node('map-current').hidden, false);
  assert.equal(node('map-latest').hidden, false);
  sockets[0].onmessage({ data: JSON.stringify({ ...snapshot, revision: 2,
    player: { ...snapshot.player, position_frames: 90 } }) });
  await settle();
  assert.equal(historyRequests, 1, 'snapshot updates must not poll read history');

  sockets[0].onmessage({ data: JSON.stringify({ ...snapshot, revision: 1,
    read: { ...snapshot.read, activity: 'FAILED' } }) });
  assert.equal(node('read-activity').textContent, 'READING');

  const nextSession = structuredClone(snapshot);
  nextSession.revision = 1;
  nextSession.read.session_id = 'session-b';
  nextSession.disc = { state: 'NO_DISC', layout: null };
  sockets[0].onmessage({ data: JSON.stringify(nextSession) });
  assert.match(node('read-map-state').textContent, /No accepted audio disc/);
  assert.equal(node('map-current').hidden, true);
  console.log('PASS: Integrity summary, bounded map, revision and session handling');
}

main().catch((error) => { console.error(error); process.exitCode = 1; });
