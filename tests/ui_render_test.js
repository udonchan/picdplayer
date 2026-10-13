'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(process.argv[2] || path.join(__dirname, '../ui/default/player.js'), 'utf8');

// Count DOM write attempts, including assignments of the existing value.
// 同値代入も含めて DOM 書き込みを数え、browser の paint 回数とは区別します。
async function main() {
  const writes = [];
  const nodes = new Map();
  const sockets = [];
  const frames = [];
  const snapshot = {
    revision: 1,
    player: { state: 'STOPPED', track_number: 1, position_frames: 0, track_duration_frames: 4500 },
    disc: { state: 'AUDIO_READY', title: 'Album', artist: 'Artist' },
    tracks: [{ number: 1, title: 'Track', duration_frames: 4500 }],
    enrichment: { status: 'AVAILABLE' },
    artwork: { cover: null },
  };
  function node(id) {
    if (nodes.has(id)) return nodes.get(id);
    const classes = new Set();
    const tracked = (kind) => new Proxy({}, {
      set(target, key, value) {
        writes.push(`${id}.${kind}.${key}`);
        target[key] = value;
        return true;
      },
    });
    const value = new Proxy({
      textContent: '', dataset: tracked('dataset'), style: tracked('style'),
      complete: false, naturalWidth: 0,
      classList: {
        contains: (name) => classes.has(name),
        add(name) { writes.push(`${id}.class.add`); classes.add(name); },
        remove(name) { writes.push(`${id}.class.remove`); classes.delete(name); },
      },
      removeAttribute(name) { writes.push(`${id}.remove.${name}`); delete this[name]; },
    }, {
      set(target, key, value) {
        writes.push(`${id}.${key}`);
        target[key] = value;
        return true;
      },
    });
    nodes.set(id, value);
    return value;
  }
  const metadataControl = node('metadata-control');
  metadataControl.dataset.command = 'metadata';
  metadataControl.addEventListener = () => {};
  let backgroundState = { requested: true, effective: true, available: true, unavailable_reason: null };
  let backgroundPostStatus = 204;
  let stateGetStatus = 200;
  let deferOff = false;
  let offAcknowledgement;
  let deferState = false;
  let stateReply;
  const context = {
    performance: { now: () => 1 },
    document: { readyState: 'complete', getElementById: node, addEventListener() {},
      querySelectorAll: (selector) => selector === '.player-controls button[data-command]' ? [metadataControl] : [] },
    location: { protocol: 'http:', host: 'localhost' },
    fetch: async (url, options) => {
      if (url === '/api/state' && stateGetStatus !== 200) return { ok: false, status: stateGetStatus };
      if (url === '/api/artist-background') {
        if (options?.method === 'POST') {
          if (backgroundPostStatus !== 204) return { status: backgroundPostStatus };
          const enabled = JSON.parse(options.body).enabled;
          backgroundState = { ...backgroundState, requested: enabled, effective: enabled };
          if (!enabled && deferOff) return new Promise((resolve) => { offAcknowledgement = resolve; });
          return { status: 204 };
        }
        return { ok: true, json: async () => ({ ...backgroundState }) };
      }
      if (url === '/api/state') {
        const captured = JSON.parse(JSON.stringify({ ...snapshot, revision: snapshot.revision + 1,
          artwork: { ...snapshot.artwork, background: backgroundState.effective ? snapshot.artwork.background : null } }));
        if (deferState) return new Promise((resolve) => { stateReply = () => resolve({ ok: true, json: async () => captured }); });
        return { ok: true, json: async () => captured };
      }
      return { ok: true, json: async () => snapshot };
    },
    WebSocket: class { constructor() { sockets.push(this); } },
    requestAnimationFrame: (fn) => frames.push(fn),
    setTimeout() {}, clearTimeout() {},
  };
  vm.runInNewContext(source, context);
  for (let i = 0; i < 12; i++) await Promise.resolve();
  sockets[0].onopen();
  while (frames.length) frames.shift()();
  function receive(value) {
    writes.length = 0;
    sockets[0].onmessage({ data: JSON.stringify(value) });
    return [...writes];
  }
  const repeatedWrites = Array.from({ length: 60 }, () => receive(snapshot).length)
    .reduce((sum, count) => sum + count, 0);
  console.log(`Identical STOPPED snapshots: 60; DOM write attempts: ${repeatedWrites}`);
  snapshot.player.state = 'PLAYING';
  receive(snapshot);
  let playingWrites = 0;
  for (let i = 1; i <= 60; i++) {
    snapshot.player.position_frames = i * 15;
    playingWrites += receive(snapshot).length;
  }
  console.log(`Position updates (15 frames each): 60; DOM write attempts: ${playingWrites}`);
  if (process.env.PICDPLAYER_RENDER_BENCHMARK_ONLY === '1') return;
  assert.equal(playingWrites, 72); // 60 progress updates + 12 displayed seconds, no lost positions.
  snapshot.player.state = 'STOPPED';
  snapshot.player.position_frames = 0;
  receive(snapshot);
  assert.equal(repeatedWrites, 0);
  assert.deepEqual(receive({ ...snapshot, revision: 2, diagnostic: { counter: 99 } }), []);
  snapshot.player.position_frames = 15;
  assert.deepEqual(receive(snapshot), ['progress.style.transform']);
  assert.equal(node('position').textContent, '0:00');
  snapshot.player.position_frames = 75;
  assert.deepEqual(receive(snapshot).sort(), ['position.textContent', 'progress.style.transform']);
  assert.equal(node('position').textContent, '0:01');
  snapshot.tracks[0].title = '<script>literal title</script>';
  assert.deepEqual(receive(snapshot), ['track-title.textContent']);
  assert.equal(node('track-title').textContent, snapshot.tracks[0].title);
  snapshot.artwork.cover = { url: '/cover-a' };
  receive(snapshot);
  const oldLoad = node('cover').onload;
  node('cover').complete = true;
  node('cover').naturalWidth = 100;
  oldLoad();
  assert(node('art').classList.contains('has-cover'));
  assert.deepEqual(receive(snapshot), []);
  snapshot.artwork.cover = { url: '/cover-b' };
  node('cover').complete = false;
  receive(snapshot);
  assert(!node('art').classList.contains('has-cover'));
  oldLoad();
  assert(!node('art').classList.contains('has-cover'));
  node('cover').onerror();
  assert.deepEqual(receive(snapshot), []); // No retry storm after a failed image.
  // Same URL after a missed disc removal must load the new disc's cover.
  // 切断中のdisc交換でも同URLの旧画像を保持しない。
  snapshot.read = { session_id: 'session-a' };
  snapshot.disc.layout = { session_id: 'session-a', disc_generation: 1 };
  snapshot.revision = 3;
  receive(snapshot);
  snapshot.disc.layout.disc_generation = 2;
  snapshot.revision = 4;
  assert(receive(snapshot).includes('cover.src'), 'same-URL cover must reload after disc generation change');
  assert.deepEqual(receive(snapshot), []);
  const retiredLoad = node('cover').onload;
  snapshot.read.session_id = 'session-b';
  snapshot.disc.layout.session_id = 'session-b';
  snapshot.revision = 5;
  assert(receive(snapshot).includes('cover.src'));
  node('cover').complete = true; node('cover').naturalWidth = 100;
  retiredLoad();
  assert(!node('art').classList.contains('has-cover'));
  node('cover').onload();
  snapshot.disc.state = 'LOADING';
  snapshot.revision = 6;
  receive(snapshot);
  assert(!node('art').classList.contains('has-cover'));
  snapshot.artwork.cover = null;
  snapshot.revision = 7;
  receive(snapshot);
  assert(!node('art').classList.contains('has-cover'));
  assert.deepEqual(receive(snapshot), []);
  snapshot.disc.state = 'AUDIO_READY';
  snapshot.artwork.cover = { url: '/api/presentation/artwork/cover' };
  snapshot.artwork.background = { url: '/api/presentation/artwork/background/opaque-1' };
  snapshot.revision++;
  receive(snapshot);
  const layer = node('player-background');
  const background = node('background-artist');
  const blurredCover = node('background-cover');
  const staleBackgroundLoad = background.onload;
  blurredCover.complete = true; blurredCover.naturalWidth = 100;
  blurredCover.onload();
  assert(layer.classList.contains('has-cover'));
  background.complete = true; background.naturalWidth = 100;
  background.onload();
  assert(layer.classList.contains('has-artist'));
  assert.deepEqual(receive(snapshot), []);
  background.onerror();
  assert(!layer.classList.contains('has-artist'));
  assert(layer.classList.contains('has-cover'));
  assert.deepEqual(receive(snapshot), []); // No decode-failure retry storm.
  snapshot.artwork.background.url = '/api/presentation/artwork/background/opaque-2';
  background.complete = false;
  snapshot.revision++;
  receive(snapshot);
  staleBackgroundLoad();
  assert(!layer.classList.contains('has-artist'));
  background.complete = true; background.naturalWidth = 100;
  background.onload();
  assert(layer.classList.contains('has-artist'));
  const retiredBackgroundLoad = background.onload;
  snapshot.artwork.background = null; // OFF removes the image reference.
  snapshot.revision++;
  receive(snapshot);
  assert(!layer.classList.contains('has-artist'));
  assert.equal(background.src, undefined);
  retiredBackgroundLoad();
  assert(!layer.classList.contains('has-artist'));
  snapshot.artwork.background = { url: 'https://provider.invalid/private.jpg' };
  snapshot.revision++;
  receive(snapshot);
  assert.equal(background.src, undefined);
  blurredCover.onerror();
  assert(!layer.classList.contains('has-cover'));
  snapshot.player = { state: 'NO_DISC' };
  snapshot.disc = { state: 'NO_DISC' };
  snapshot.revision++;
  receive(snapshot);
  assert.equal(node('album').textContent, 'No disc');
  assert.equal(node('position').textContent, '0:00');
  assert.equal(node('progress').style.transform, 'scaleX(0)');
  assert.equal(metadataControl.textContent, 'Choose album');
  snapshot.disc.state = 'AUDIO_READY';
  snapshot.enrichment.selection = { state: 'SELECTED', selected_index: 0, metadata_generation: 3,
    disc_generation: 3, session_id: 'session-a', candidates: [{ index: 0, title: 'First' }, { index: 1, title: 'Second' }] };
  snapshot.revision++;
  receive(snapshot);
  assert.equal(metadataControl.textContent, 'Change album');
  assert.deepEqual(receive(snapshot), []);
  snapshot.enrichment.selection = null;
  snapshot.revision++;
  receive(snapshot);
  assert.equal(metadataControl.textContent, 'Choose album');
  assert.deepEqual(receive(snapshot), []);
  // HTTP can still work while the state stream is disconnected or delayed.
  snapshot.player.state = 'STOPPED';
  snapshot.artwork.background = { url: '/api/presentation/artwork/background/off-without-state' };
  snapshot.revision++;
  receive(snapshot);
  background.complete = true; background.naturalWidth = 100;
  background.onload();
  assert(layer.classList.contains('has-artist'));
  const loadBeforeOff = background.onload;
  sockets[0].onclose();
  context.openSettings();
  await new Promise(setImmediate);
  assert.equal(node('background-off').disabled, false);
  for (const status of [403, 409]) {
    backgroundPostStatus = status;
    await context.selectBackground(0);
    assert(layer.classList.contains('has-artist'), 'rejected OFF must not hide an active photo');
  }
  backgroundPostStatus = 204;
  stateGetStatus = 503;
  await context.selectBackground(0);
  assert(!layer.classList.contains('has-artist'), 'accepted OFF must hide photos without a state event');
  assert.equal(background.src, undefined);
  loadBeforeOff();
  assert(!layer.classList.contains('has-artist'));
  snapshot.revision += 2;
  context.render(snapshot); // A delayed pre-OFF model must not resurrect the retired image.
  assert.equal(background.src, undefined);
  stateGetStatus = 200;
  await context.selectBackground(1);
  context.render(snapshot);
  assert.equal(background.src, undefined); // ON does not make an expired reference usable again.
  snapshot.artwork.background.url = '/api/presentation/artwork/background/after-on';
  snapshot.revision++;
  background.complete = false;
  context.render(snapshot);
  background.complete = true; background.naturalWidth = 100;
  background.onload();
  assert(layer.classList.contains('has-artist'));
  deferOff = true;
  const delayedOff = context.selectBackground(0);
  backgroundState = { ...backgroundState, requested: true, effective: true }; // Another API client's ON.
  snapshot.artwork.background.url = '/api/presentation/artwork/background/newer-than-off';
  snapshot.revision += 2;
  background.complete = false;
  context.render(snapshot);
  background.complete = true; background.naturalWidth = 100;
  background.onload();
  offAcknowledgement({ status: 204 });
  await delayedOff;
  assert.equal(background.src, snapshot.artwork.background.url);
  assert(layer.classList.contains('has-artist'), 'a newer owner image must survive a delayed OFF acknowledgement');
  // A No Disc response captured before a new session must not overwrite its live state.
  deferOff = false;
  snapshot.player = { state: 'NO_DISC' };
  snapshot.disc = { state: 'NO_DISC' };
  snapshot.read = { session_id: null };
  snapshot.artwork.background = null;
  snapshot.revision++;
  context.render(JSON.parse(JSON.stringify(snapshot)));
  deferState = true;
  const offAcrossSession = context.selectBackground(0);
  await new Promise(setImmediate);
  backgroundState = { ...backgroundState, requested: true, effective: true };
  snapshot.player = { state: 'STOPPED', track_number: 1, position_frames: 0 };
  snapshot.disc = { state: 'AUDIO_READY', title: 'Newest album' };
  snapshot.read = { session_id: 'new-session' };
  snapshot.artwork.background = { url: '/api/presentation/artwork/background/new-session' };
  snapshot.revision++;
  background.complete = false;
  context.render(JSON.parse(JSON.stringify(snapshot)));
  background.complete = true; background.naturalWidth = 100;
  background.onload();
  stateReply();
  await offAcrossSession;
  assert.equal(node('album').textContent, 'Newest album');
  assert.equal(background.src, snapshot.artwork.background.url);
  assert(layer.classList.contains('has-artist'));
  console.log('PASS: identical values, position, metadata, artwork lifecycle, disc removal and OFF without state events');
}
main().catch((error) => { console.error(error); process.exitCode = 1; });
