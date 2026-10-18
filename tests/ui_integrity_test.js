'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(process.argv[2] || path.join(__dirname, '../ui/default/player.js'), 'utf8');

async function settle() {
  for (let i = 0; i < 20; ++i) await Promise.resolve();
}

async function main(scenario = null) {
  const nodes = new Map();
  const listeners = new Map();
  const sockets = [];
  const frames = [];
  let historyRequests = 0;
  let pendingMap = false;
  const timers = [];
  let now = 0;
  let configurationWrites = 0;
  const snapshot = {
    schema_version: 1, revision: 1,
    player: { state: 'PLAYING', track_number: 1, position_frames: 75, track_duration_frames: 4500 },
    disc: { state: 'AUDIO_READY', title: 'Album', artist: 'Artist',
      layout: { session_id: 'session-a', disc_generation: 2, start_lba: 0, leadout_lba: 100, tracks: [] } },
    tracks: [{ number: 1, title: 'Track', duration_frames: 4500 }],
    enrichment: { status: 'AVAILABLE' }, artwork: { cover: null },
    drive: { vendor: 'Drive', model: 'Model', digital_audio_extraction: { value: 'UNKNOWN', source: 'NONE' },
      c2_supported: { value: 'UNKNOWN', source: 'NONE' }, read_cache: { value: 'UNKNOWN', source: 'NONE' },
      speed_control: { value: 'YES', source: 'KERNEL_REPORTED' }, requested_speed_x: 4,
      speed_request_error: '', current_speed_x: null, read_offset_samples: null },
    read: { session_id: 'session-a', stream_generation: 4, activity: 'READING', effective_strategy: 'direct-single-read',
      queued_blocks: 1, buffer_capacity_frames: 90, read_block_frames: 75, dropped_events: 0,
      read_stall: { inflight_ms: null, timeout_ms: 10000, last_timeout_ms: null },
      policy: { pending: true, requested: { mode: 'REPEAT' }, effective: { mode: 'SINGLE' } },
      strategy: { requested: 'direct+repeat-2of3', effective: 'direct-single-read',
        pending: true, downgrade_reason: 'POLICY_RESTART_REQUIRED' },
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
  const mapRequests = [];
  let mapResponse = null;
  let resolveDelayed;
  if (scenario) {
    snapshot.player.state = scenario === 'cadence' ? 'PLAYING' : 'STOPPED';
    if (snapshot.player.state === 'STOPPED') snapshot.read.current_playback = null;
    mapResponse = () => ({ ok: false, status: 503 });
    if (scenario === 'cadence' || scenario === 'delayed' || scenario === 'stream-delayed') {
      const delayed = new Promise(resolve => { resolveDelayed = resolve; });
      mapResponse = () => delayed;
    }
  }
  function node(id) {
    if (nodes.has(id)) return nodes.get(id);
    const value = { textContent: '', hidden: false, dataset: {}, style: {}, complete: false, naturalWidth: 0,
      classList: { contains() { return false; }, add() {}, remove() {} }, removeAttribute() {},
      addEventListener(event, callback) { listeners.set(`${id}:${event}`, callback); } };
    let text = '';
    Object.defineProperty(value, 'textContent', { get() { return text; }, set(next) {
      text = next;
      if (/^read-(policy|strategy)-/.test(id)) ++configurationWrites;
    } });
    nodes.set(id, value); return value;
  }
  vm.runInNewContext(source, {
    performance: { now: () => now }, document: { readyState: 'complete', getElementById: node, addEventListener() {} },
    location: { protocol: 'http:', host: 'localhost' },
    fetch: async (url) => {
      assert.notEqual(url, '/api/read-history', 'map refresh must not fetch full history');
      if (url === '/api/disc-map') {
        ++historyRequests;
        mapRequests.push(now);
        if (mapResponse) return mapResponse();
      }
      if (url === '/api/disc-map' && pendingMap) return { ok: false, status: 503 };
      return { ok: true, json: async () => url === '/api/disc-map' ? detail : scenario ? structuredClone(snapshot) : snapshot };
    },
    WebSocket: class { constructor() { sockets.push(this); } }, requestAnimationFrame: (fn) => frames.push(fn),
    setTimeout(fn, ms) { timers.push({ fn, ms, due: now + ms, active: true }); return timers.length; },
    clearTimeout(id) { if (timers[id - 1]) timers[id - 1].active = false; }, Promise,
  });
  await settle();
  if (scenario) {
    const advance = async (time) => {
      now = time;
      for (const timer of [...timers]) {
        if (timer.active && timer.due <= now) {
          timer.active = false;
          timer.fn();
          await settle();
        }
      }
    };
    const pendingTimers = () => timers.filter(timer => timer.active && timer.ms === 2000);
    const success = () => ({ ok: true, status: 200, json: async () => structuredClone(detail) });
    assert.deepEqual(mapRequests, [0]);
    if (scenario === 'cadence') {
      now = 100;
      resolveDelayed({ ok: false, status: 503 });
      await settle();
      assert.equal(pendingTimers().length, 1);
      mapResponse = success;
      now = 2000;
      sockets[0].onmessage({ data: JSON.stringify({ ...snapshot, revision: 2 }) });
      await settle();
      assert.deepEqual(mapRequests, [0, 2000]);
      assert.match(node('read-map-state').textContent, /revision 3/);
      await advance(2100);
      assert.deepEqual(mapRequests, [0, 2000], 'successful periodic fetch retires the obsolete forced retry');
    } else {
      now = 100;
      snapshot.revision = 2;
      if (scenario === 'stream' || scenario === 'stream-auto' || scenario === 'stream-delayed') {
        snapshot.read.stream_generation = detail.stream_generation = 5;
      } else {
        snapshot.read.session_id = snapshot.disc.layout.session_id = detail.session_id = 'session-b';
        snapshot.disc.layout.disc_generation = detail.disc_map.disc_generation = 3;
      }
      let resolveCurrent;
      if (scenario === 'delayed') {
        const current = new Promise(resolve => { resolveCurrent = resolve; });
        mapResponse = () => current;
      } else {
        mapResponse = () => ({ ok: false, status: 503 });
      }
      sockets[0].onmessage({ data: JSON.stringify(snapshot) });
      await settle();
      if (scenario === 'stream-auto' || scenario === 'stream-delayed') {
        assert.deepEqual(mapRequests, [0], 'stopped stream change must not fetch before the cadence is due');
        if (scenario === 'stream-delayed') {
          now = 200;
          resolveDelayed({ ok: false, status: 503 });
          await settle();
          assert.equal(timers.filter(timer => timer.active && timer.due === 2000).length, 1,
            'obsolete response must leave exactly one current-stream acquisition retry');
        }
        mapResponse = () => {
          assert.equal(snapshot.read.stream_generation, 5);
          return success();
        };
        await advance(1999);
        assert.deepEqual(mapRequests, [0]);
        await advance(2000);
        assert.deepEqual(mapRequests, [0, 2000], 'stopped new stream retries without manual refresh or another snapshot');
        assert.match(node('read-map-state').textContent, /revision 3/);
        assert.equal(pendingTimers().length, 0);
        console.log(`PASS: map retry ${scenario} at 2000 ms without manual refresh`);
        return;
      }
      if (scenario === 'stream') {
        assert.deepEqual(mapRequests, [0], 'same-disc stopped stream change preserves the fetch cadence');
        listeners.get('refresh-read-map:click')();
        await settle();
      }
      assert.deepEqual(mapRequests, [0, 100]);
      if (scenario === 'delayed') {
        resolveDelayed({ ok: false, status: 503 });
        await settle();
        assert.equal(pendingTimers().length, 0, 'delayed obsolete503 must not arm a retry');
        resolveCurrent({ ok: false, status: 503 });
        await settle();
      }
      assert.equal(pendingTimers().length, 1, 'only the current context owns a retry');
      assert.equal(pendingTimers()[0].due, 2100, 'old context cannot retain or replace the current retry');
      mapResponse = success;
      await advance(2000);
      assert.deepEqual(mapRequests, [0, 100], 'retry waits two seconds after the current503');
      await advance(2100);
      assert.deepEqual(mapRequests, [0, 100, 2100], 'stopped current context retries without another snapshot');
      assert.match(node('read-map-state').textContent, /revision 3/);
      assert.equal(pendingTimers().length, 0);
    }
    console.log(`PASS: map retry ${scenario}`);
    return;
  }
  while (frames.length) frames.shift()();
  sockets[0].onopen();
  assert.equal(node('connection').textContent, 'DAEMON · CONNECTED');
  assert.equal(node('connection').dataset.state, 'connected');
  assert.equal(node('read-policy-single').textContent, 'SINGLE · EFFECTIVE');
  assert.equal(node('read-policy-repeat').textContent, 'REPEAT · REQUESTED');
  assert.equal(node('read-policy-single').dataset.effective, 'true');
  assert.equal(node('read-policy-repeat').dataset.requested, 'true');
  assert.equal(node('read-policy-state').textContent, 'Pending: PENDING (until STOPPED)');
  assert.equal(node('read-strategy-effective').textContent, 'Effective: direct-single-read');
  assert.equal(node('read-strategy-requested').textContent, 'Requested: direct+repeat-2of3');
  assert.match(node('read-strategy-state').textContent, /PENDING.*POLICY_RESTART_REQUIRED/);
  assert.equal(node('read-strategy-compatibility').hidden, true);
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
  assert.equal(node('map-marker-label').textContent, 'LATEST OBSERVED READ · LBA 50 · MAY BE AHEAD');
  assert.equal(node('map-marker-label').dataset.marker, 'available');
  now = 1999;
  sockets[0].onmessage({ data: JSON.stringify({ ...snapshot, revision: 2,
    player: { ...snapshot.player, position_frames: 90 } }) });
  await settle();
  assert.equal(historyRequests, 1, 'disc-map must not be fetched for every snapshot');

  now = 2000;
  sockets[0].onmessage({ data: JSON.stringify({ ...snapshot, revision: 3,
    player: { ...snapshot.player, position_frames: 105 } }) });
  await settle();
  assert.equal(historyRequests, 2, 'playing snapshots refresh the map at the bounded cadence');

  const stopped = structuredClone(snapshot);
  stopped.revision = 4;
  stopped.player.state = 'STOPPED';
  stopped.read.current_playback = null;
  sockets[0].onmessage({ data: JSON.stringify(stopped) });
  await settle();
  assert.equal(historyRequests, 2, 'STOPPED snapshots do not poll the map');
  listeners.get('refresh-read-map:click')();
  await settle();
  assert.equal(historyRequests, 3, 'Refresh map explicitly refetches while STOPPED');

  sockets[0].onmessage({ data: JSON.stringify({ ...snapshot, revision: 1,
    read: { ...snapshot.read, activity: 'FAILED' } }) });
  assert.equal(node('read-activity').textContent, 'READING');

  const offsetSnapshot = structuredClone(stopped);
  offsetSnapshot.revision = 5;
  offsetSnapshot.drive.read_offset_samples = 1;
  offsetSnapshot.read.offset_window = { disc_generation: 2, offset_samples: 1, reason: 'BOUNDARY_CLIPPED',
    unavailable: [{ start_sample: 58799, end_sample: 58800 }],
    alignment_excluded: [{ start_sample: 58212, end_sample: 58799 }] };
  sockets[0].onmessage({ data: JSON.stringify(offsetSnapshot) });
  assert.equal(node('drive-offset-window').hidden, false);
  assert.match(node('drive-offset-window').textContent, /unavailable 1 · alignment excluded 587/);
  assert.match(node('drive-offset-window').textContent, /calibration unverified/);
  offsetSnapshot.revision = 6;
  offsetSnapshot.disc.layout.disc_generation = 3;
  sockets[0].onmessage({ data: JSON.stringify(offsetSnapshot) });
  assert.equal(node('drive-offset-window').hidden, true);
  assert.equal(node('drive-offset-window').textContent, '—');

  const observing = structuredClone(stopped);
  observing.revision = 7;
  observing.player = { state: 'NO_DISC', track_number: null, position_frames: null, track_duration_frames: null };
  observing.disc = { state: 'NO_DISC', layout: null };
  observing.tracks = [];
  observing.enrichment = { status: 'NOT_REQUESTED' };
  observing.artwork = { cover: null };
  for (const key of ['digital_audio_extraction', 'c2_supported', 'c2_trustworthy', 'read_cache', 'accurate_stream', 'speed_control']) {
    observing.drive[key] = { value: 'UNKNOWN', source: 'NONE', detail: '' };
  }
  Object.assign(observing.drive, { vendor: '', model: '', firmware: '', requested_speed_x: null, current_speed_x: null });
  // Sparse v1 telemetry must not resurrect observations from the old context.
  observing.read = { session_id: 'session-a', stream_generation: 5, activity: 'IDLE', queued_blocks: 0,
    latest: null, current_playback: null, active_warning: null };
  observing.drive.observation = { generation: 2, presence: 'UNKNOWN', reason: 'STATE_CHANGED',
    sampled: true, method: 'NODE_SYSFS_POLL', error: 'METADATA_UNAVAILABLE', revalidating: true, retiring_io: true };
  sockets[0].onmessage({ data: JSON.stringify(observing) });
  assert.match(node('drive-observation').textContent, /NODE UNKNOWN · observed context 2/);
  assert.match(node('drive-observation').textContent, /revalidating · retiring old I\/O · physical identity unverified/);

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
  assert.equal(node('map-marker-label').textContent, 'LATEST OBSERVED READ · NOT AVAILABLE');
  assert.equal(node('map-marker-label').dataset.marker, 'unavailable');
  assert.equal(node('drive-observation').textContent, 'Device observation UNKNOWN');
  await settle();
  const requestsBeforePending = historyRequests;
  const restored = structuredClone(snapshot);
  restored.revision = 2;
  restored.read.session_id = restored.disc.layout.session_id = 'session-b';
  restored.disc.layout.disc_generation = 3;
  restored.read.current_playback.disc_generation = restored.read.latest.disc_generation = 3;
  pendingMap = true;
  sockets[0].onmessage({ data: JSON.stringify(restored) });
  await settle();
  assert.equal(historyRequests, requestsBeforePending + 1);
  sockets[0].onmessage({ data: JSON.stringify({ ...restored, revision: 3 }) });
  await settle();
  assert.equal(historyRequests, requestsBeforePending + 1, 'pending map must not cause per-snapshot retries');
  const mapTimers = timers.filter(timer => timer.ms === 2000);
  assert.equal(mapTimers.length, 1, 'pending map has one bounded retry');
  pendingMap = false;
  detail.session_id = 'session-b';
  detail.disc_map.disc_generation = 3;
  detail.disc_map.revision = 4;
  detail.snapshot = { sequence: 8, captured_at_unix_ms: Date.now() - 5000 };
  now += 2000;
  mapTimers[0].fn();
  await settle();
  assert.equal(historyRequests, requestsBeforePending + 2);
  assert.match(node('read-map-state').textContent, /revision 4.*captured /);
  assert.ok(node('read-map-state').textContent.endsWith(new Date(detail.snapshot.captured_at_unix_ms).toISOString()));
  detail.stream_generation = 3;
  detail.disc_map.revision = 999;
  listeners.get('refresh-read-map:click')();
  await settle();
  assert.match(node('read-map-state').textContent, /revision 4/);
  assert.doesNotMatch(node('read-map-state').textContent, /revision 999/);

  const configuration = structuredClone(restored);
  configuration.player.state = 'STOPPED';
  configuration.read.current_playback = null;
  configuration.revision = 4;
  const requestsBeforeConfiguration = historyRequests;
  const publishConfiguration = () => {
    ++configuration.revision;
    sockets[0].onmessage({ data: JSON.stringify(configuration) });
  };
  for (const mode of ['SINGLE', 'REPEAT']) {
    configuration.read.policy = { requested: { mode }, effective: { mode }, pending: false };
    configuration.read.strategy = { requested: 'direct-single-read', effective: 'direct-single-read',
      pending: false, downgrade_reason: 'NONE' };
    publishConfiguration();
    assert.equal(node(`read-policy-${mode.toLowerCase()}`).textContent, `${mode} · EFFECTIVE / REQUESTED`);
    assert.equal(node(`read-policy-${mode === 'SINGLE' ? 'repeat' : 'single'}`).dataset.effective, 'false');
    assert.match(node('read-policy-state').textContent, /Pending: NOT PENDING$/);
    configuration.read.policy.pending = true;
    configuration.read.strategy.pending = true;
    publishConfiguration();
    assert.match(node('read-policy-state').textContent, /Pending: PENDING \(until STOPPED\)$/,
      'same mode does not imply the complete policy has already applied');
    assert.match(node('read-strategy-state').textContent, /Pending: PENDING/);
  }
  for (const reason of ['NONE', 'POLICY_RESTART_REQUIRED', 'C2_PROBE_PENDING', 'C2_CAPABILITY_UNKNOWN',
    'C2_UNSUPPORTED', 'C2_BACKEND_UNSUPPORTED', 'C2_STREAM_RESTART_REQUIRED']) {
    const pending = ['POLICY_RESTART_REQUIRED', 'C2_PROBE_PENDING', 'C2_STREAM_RESTART_REQUIRED'].includes(reason);
    configuration.read.strategy = { requested: reason === 'C2_BACKEND_UNSUPPORTED' ? 'paranoia-library-single-read'
      : 'direct-c2-pointers-single-read', effective: reason === 'C2_BACKEND_UNSUPPORTED'
      ? 'paranoia-library-single-read' : 'direct-single-read', pending, downgrade_reason: reason };
    configuration.read.effective_strategy = configuration.read.strategy.effective;
    publishConfiguration();
    assert.equal(node('read-strategy-state').textContent,
      `Pending: ${pending ? 'PENDING (until STOPPED)' : 'NOT PENDING'} · Downgrade: ${reason}`);
    assert.doesNotMatch(node('read-strategy-state').textContent, /APPLIED|CLEAN/);
  }
  configuration.read.effective_strategy = 'compatibility-disagrees';
  publishConfiguration();
  assert.equal(node('read-strategy-compatibility').hidden, false);
  assert.match(node('read-strategy-compatibility').textContent, /compatibility-disagrees.*MISMATCH/);
  assert.equal(node('read-strategy-effective').textContent, 'Effective: direct-single-read');
  for (const pending of [null, undefined, 'false', 0]) {
    configuration.read.policy = { requested: { mode: 'SINGLE' }, effective: null, pending };
    configuration.read.strategy = { requested: null, effective: null, pending, downgrade_reason: null };
    publishConfiguration();
    assert.match(node('read-policy-state').textContent, /Effective: NOT AVAILABLE.*Pending: UNKNOWN$/);
    assert.equal(node('read-strategy-state').textContent, 'Pending: UNKNOWN · Downgrade: NOT AVAILABLE');
    assert.equal(node('read-strategy-effective').textContent, 'Effective: NOT AVAILABLE');
    assert.match(node('read-strategy-compatibility').textContent, /strategy effective unavailable/);
  }
  const hostile = '<img src=x onerror=alert(1)>' + 'x'.repeat(220);
  configuration.read.policy = { requested: { mode: hostile }, effective: { mode: false }, pending: null };
  configuration.read.strategy = { requested: hostile, effective: hostile, pending: false, downgrade_reason: hostile };
  configuration.read.effective_strategy = hostile;
  publishConfiguration();
  assert.match(node('read-policy-state').textContent, /Effective: UNKNOWN · Requested: UNKNOWN \(<img/);
  assert.equal(node('read-strategy-requested').textContent, `Requested: ${hostile}`);
  assert.equal(node('read-strategy-effective').textContent, `Effective: ${hostile}`);
  assert.equal(node('read-strategy-state').textContent, `Pending: NOT PENDING · Downgrade: ${hostile}`);
  assert.equal(node('read-strategy-compatibility').hidden, true);
  const writesBeforeIdentical = configurationWrites;
  publishConfiguration();
  assert.equal(configurationWrites, writesBeforeIdentical, 'identical configuration must not rewrite DOM text');
  delete configuration.read.policy;
  delete configuration.read.strategy;
  delete configuration.read.effective_strategy;
  publishConfiguration();
  assert.equal(node('read-policy-single').textContent, 'SINGLE');
  assert.equal(node('read-policy-repeat').textContent, 'REPEAT');
  assert.equal(node('read-policy-single').dataset.effective, 'false');
  assert.equal(node('read-policy-repeat').dataset.requested, 'false');
  assert.equal(node('read-policy-state').textContent,
    'Effective: NOT AVAILABLE · Requested: NOT AVAILABLE · Pending: UNKNOWN');
  assert.equal(node('read-strategy-effective').textContent, 'Effective: NOT AVAILABLE');
  assert.equal(node('read-strategy-requested').textContent, 'Requested: NOT AVAILABLE');
  assert.equal(node('read-strategy-state').textContent, 'Pending: UNKNOWN · Downgrade: NOT AVAILABLE');
  assert.equal(node('read-strategy-compatibility').hidden, true);
  await settle();
  assert.equal(historyRequests, requestsBeforeConfiguration, 'configuration display does not fetch extra diagnostics');
  console.log('PASS: policy selections, strategy reasons, pending/null/mismatch, literal strings and DOM dedup');
  sockets[0].onclose();
  assert.equal(node('connection').textContent, 'DAEMON · RECONNECTING');
  assert.equal(node('connection').dataset.state, 'reconnecting');
  console.log('PASS: Integrity summary, bounded map, revision and session handling');
}

(async () => {
  for (const scenario of [null, 'context', 'stream', 'stream-auto', 'stream-delayed', 'cadence', 'delayed']) {
    try { await main(scenario); }
    catch (error) { console.error(`FAIL: ${scenario || 'integrity'}`, error); process.exitCode = 1; }
  }
})();
