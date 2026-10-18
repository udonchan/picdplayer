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
    addEventListener() {},
    ResizeObserver: class { observe() {} },
    getComputedStyle: () => ({ fontSize: '20px' }),
    performance: { now: () => 1 },
    document: { fonts: { ready: Promise.resolve() }, readyState: 'complete', getElementById: node, addEventListener() {},
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
async function slideshowTests() {
  const nodes = new Map();
  const timers = new Map();
  let now = 0;
  let timerId = 0;
  let reducedMotion = false;
  let srcWrites = 0;
  function node(id) {
    if (!nodes.has(id)) {
      const classes = new Set();
      const value = { dataset: {}, style: {}, textContent: '', complete: false, naturalWidth: 0,
        classList: { contains: (name) => classes.has(name), add: (name) => classes.add(name), remove: (name) => classes.delete(name) },
        removeAttribute(name) { delete this[name]; if (name === 'src') { this.complete = false; this.naturalWidth = 0; } },
        setAttribute() {}, addEventListener() {},
      };
      nodes.set(id, new Proxy(value, { set(target, key, value) {
        if (key === 'src') srcWrites++;
        target[key] = value;
        return true;
      } }));
    }
    return nodes.get(id);
  }
  let stateReply;
  const context = {
    addEventListener() {},
    ResizeObserver: class { observe() {} },
    getComputedStyle: () => ({ fontSize: '20px' }),
    performance: { now: () => now },
    document: { fonts: { ready: Promise.resolve() }, readyState: 'complete', getElementById: node, querySelectorAll: () => [], addEventListener() {} },
    location: { protocol: 'http:', host: 'localhost' },
    fetch: (url, options) => {
      if (url === '/api/artist-background') return Promise.resolve(options?.method === 'POST'
        ? { status: 204 } : { ok: true, json: async () => ({ requested: false, effective: false, available: true, unavailable_reason: null }) });
      return new Promise((resolve) => { if (url === '/api/state') stateReply = resolve; });
    },
    WebSocket: class {}, requestAnimationFrame() {},
    matchMedia: () => ({ matches: reducedMotion }),
    setTimeout(fn, delay) { const id = ++timerId; timers.set(id, { fn, at: now + delay }); return id; },
    clearTimeout(id) { timers.delete(id); },
  };
  vm.runInNewContext(source, context);
  const url = (name) => `/api/presentation/artwork/background/${name}`;
  const model = { revision: 1, player: { state: 'STOPPED' }, disc: { state: 'AUDIO_READY', artist: 'Artist' },
    artwork: { cover: { url: '/api/presentation/artwork/cover' }, background: null } };
  function render(urls, timing = {}) {
    model.revision++;
    model.artwork.background = urls === null ? null : { url: urls[0], urls, dwell_ms: 45000, crossfade_ms: 4000, ...timing };
    context.render(JSON.parse(JSON.stringify(model)));
  }
  const images = () => [node('background-artist'), node('background-artist-next')];
  const visible = () => images().find((image) => image.style.opacity === '.24');
  const pending = () => images().find((image) => image.src && image.style.opacity === '0' && image.onload);
  function loaded(image) { image.complete = true; image.naturalWidth = 100; image.onload(); }
  function tick(ms) {
    const end = now + ms;
    while (true) {
      const next = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
      if (!next || next[1].at > end) break;
      now = next[1].at; timers.delete(next[0]); next[1].fn();
    }
    now = end;
  }
  const flush = async () => { for (let i = 0; i < 12; i++) await Promise.resolve(); };
  render(null);
  assert.equal(timers.size, 0, 'zero images: no slideshow timer');
  loaded(node('background-cover'));
  render([url('a')]); loaded(pending());
  assert.equal(visible().src, url('a'));
  assert.equal(timers.size, 0, 'one image: no slideshow timer');
  const first = visible();
  tick(20000);
  render([url('a'), url('b')]);
  const deadline = [...timers.values()][0].at;
  tick(10000);
  render([url('a'), url('b'), url('c')]);
  assert.equal(visible(), first, 'progressive additions keep current image');
  assert.equal([...timers.values()][0].at, deadline, 'multiple additions do not restart dwell');
  const writesBefore = srcWrites;
  for (let i = 0; i < 60; i++) context.render(JSON.parse(JSON.stringify(model)));
  assert.equal(srcWrites, writesBefore, 'identical references do not reload');
  assert.equal([...timers.values()][0].at, deadline);
  tick(34999); assert.equal(pending(), undefined);
  tick(1);
  const second = pending();
  assert(second); assert.equal(visible(), first, 'preloading retains current');
  let decodeResolve;
  second.decode = () => new Promise((resolve) => { decodeResolve = resolve; });
  loaded(second);
  assert.equal(visible(), first, 'load waits for decode');
  decodeResolve(); await flush();
  assert.equal(visible(), second); assert.equal(second.style.transitionDuration, '4000ms');
  assert.equal(first.style.opacity, '0');
  assert.equal([...timers.values()][0].at, now + 4000);
  tick(3999); assert.equal(timers.size, 1);
  tick(1); assert.equal([...timers.values()][0].at, now + 45000);
  const seen = [url('a'), second.src];
  tick(45000); const third = pending(); delete third.decode; loaded(third); seen.push(third.src);
  assert.equal(new Set(seen).size, 3, 'bag visits each progressive image before repeating');
  for (let i = 0; i < 6; i++) {
    tick(49000);
    const before = visible(); const next = pending(); delete next.decode; loaded(next);
    assert.notEqual(next.src, before.src, 'no immediate repeat across bag refills');
    assert.notEqual(next, before, 'two alternating layers');
  }
  render([url('fail-a'), url('fail-b'), url('fail-c')]);
  loaded(pending()); const good = visible();
  tick(45000); const failedUrl = pending().src; pending().onerror();
  assert.equal(visible(), good, 'failed next retains valid current');
  const other = pending(); assert(other); assert.notEqual(other.src, failedUrl);
  other.decode = () => Promise.reject(new Error('decode failed'));
  loaded(other); await flush();
  assert.equal(visible(), good, 'decode rejection retains valid current');
  assert.equal(timers.size, 0, 'only one viable image remains');
  good.onerror();
  assert.equal(visible(), undefined);
  assert(!node('player-background').classList.contains('has-artist'));
  assert(node('player-background').classList.contains('has-cover'), 'all failures fall back to blurred cover');
  node('background-cover').onerror();
  assert(!node('player-background').classList.contains('has-cover'), 'failed cover falls back to default');
  delete other.decode;
  render([url('stale-a'), url('stale-b')]); loaded(pending()); tick(45000);
  const stale = pending(); let staleDecode;
  stale.decode = () => new Promise((resolve) => { staleDecode = resolve; });
  const staleLoad = stale.onload; const staleError = stale.onerror;
  loaded(stale);
  render([url('replacement-a'), url('replacement-b')]);
  staleLoad(); staleError(); staleDecode(); await flush();
  assert.equal(visible(), undefined, 'replacement invalidates load/decode/error callbacks');
  delete stale.decode; loaded(pending());
  const staleTimer = [...timers.values()][0].fn;
  model.disc.artist = 'Other artist'; render([url('replacement-a'), url('replacement-b')]);
  staleTimer(); assert.equal(timers.size, 0, 'artist replacement cancels old timer');
  loaded(pending());
  model.disc.state = 'NO_DISC'; render([url('replacement-a'), url('replacement-b')]);
  assert.equal(timers.size, 0); assert(images().every((image) => !image.src));
  model.disc.state = 'AUDIO_READY';
  render([url('off-a'), url('off-b')]); loaded(pending()); tick(45000);
  const offLoad = pending().onload; const offTimerCount = timers.size;
  assert.equal(offTimerCount, 0);
  context.openSettings(); await flush();
  const off = context.selectBackground(0); await flush();
  assert.equal(timers.size, 0); assert(images().every((image) => !image.src), 'OFF ACK cancels both layers');
  offLoad(); assert.equal(visible(), undefined);
  stateReply({ ok: false, status: 503 }); await off;
  context.render(JSON.parse(JSON.stringify(model)));
  assert(images().every((image) => !image.src), 'delayed entire list stays retired after OFF');
  context.closeSettings();
  reducedMotion = true;
  render([url('motion-a'), url('motion-b')]); loaded(pending()); tick(45000); loaded(pending());
  assert.equal(visible().style.transitionDuration, '0ms');
  assert.equal([...timers.values()][0].at, now + 45000, 'reduced motion keeps slow dwell without fade timer');
  render([url('bounded-a'), url('bounded-b')], { dwell_ms: -1, crossfade_ms: Infinity });
  loaded(pending()); assert.equal([...timers.values()][0].at, now + 45000);
  reducedMotion = false; tick(45000); loaded(pending());
  assert.equal(visible().style.transitionDuration, '4000ms', 'invalid timing uses bounded defaults');
  render([url('timing-a'), url('timing-b')], { dwell_ms: 60000, crossfade_ms: 2000 });
  loaded(pending()); assert.equal([...timers.values()][0].at, now + 60000);
  tick(60000); loaded(pending()); assert.equal(visible().style.transitionDuration, '2000ms');
  render(null); assert.equal(timers.size, 0);
  model.artwork.background = { url: url('legacy') }; context.render(model); loaded(pending());
  assert.equal(visible().src, url('legacy')); assert.equal(timers.size, 0, 'legacy url is static');
  render([url('throw-a'), url('throw-b')]);
  pending().decode = () => { throw new Error('decode throws'); }; loaded(pending());
  const survivor = pending(); delete survivor.decode; loaded(survivor);
  assert.equal(visible().src, url('throw-b'), 'synchronous decode failure tries another image');
  render(['https://provider.invalid/private.jpg']);
  assert(images().every((image) => !image.src), 'provider URLs never become image sources');
  render([url('progress-a')]);
  const progressiveImage = pending(); let progressiveDecode;
  progressiveImage.decode = () => new Promise((resolve) => { progressiveDecode = resolve; });
  const progressiveLoad = progressiveImage.onload;
  loaded(progressiveImage);
  render([url('progress-a'), url('progress-b')]);
  render([url('progress-a'), url('progress-b'), url('progress-c')]);
  assert.equal(pending(), progressiveImage);
  assert.equal(progressiveImage.onload, progressiveLoad, 'progression preserves pending decode');
  progressiveDecode(); await flush(); delete progressiveImage.decode;
  assert.equal(visible(), progressiveImage); assert.equal(timers.size, 1);
  tick(45000); loaded(pending());
  const fadeDeadline = [...timers.values()][0].at;
  context.render(JSON.parse(JSON.stringify(model)));
  assert.equal([...timers.values()][0].at, fadeDeadline, 'identical snapshot preserves fade timer');
  render([url('progress-a'), url('progress-c')]);
  assert.equal(visible(), undefined, 'same first URL with list removal is replacement');
  assert.equal(timers.size, 0);
  loaded(pending());
  render([url('progress-a'), url('progress-b')]);
  assert.equal(visible(), undefined, 'same first URL with changed later reference is replacement');
  loaded(pending());
  model.read = { session_id: 'disc-session' };
  model.disc.layout = { session_id: 'disc-session', disc_generation: 1 };
  render([url('disc-a'), url('disc-b')]); loaded(pending());
  const discTimer = [...timers.values()][0].fn;
  model.disc.layout.disc_generation++;
  render([url('disc-a'), url('disc-b')]);
  discTimer(); assert.equal(visible(), undefined); assert.equal(timers.size, 0, 'disc generation cancels dwell');
  loaded(pending());
  context.openSettings(); await flush();
  const offDwellTimer = [...timers.values()][0].fn;
  const offDwell = context.selectBackground(0); await flush();
  offDwellTimer(); assert.equal(timers.size, 0); assert(images().every((image) => !image.src));
  stateReply({ ok: false, status: 503 }); await offDwell;
  context.closeSettings();
  render([url('off-decode-a'), url('off-decode-b')]); loaded(pending()); tick(45000);
  const offDecodeImage = pending(); let offDecodeResolve;
  offDecodeImage.decode = () => new Promise((resolve) => { offDecodeResolve = resolve; });
  loaded(offDecodeImage);
  context.openSettings(); await flush();
  const offDecode = context.selectBackground(0); await flush();
  offDecodeResolve(); await flush();
  assert.equal(visible(), undefined); assert.equal(timers.size, 0, 'OFF expires pending decode');
  stateReply({ ok: false, status: 503 }); await offDecode;
  console.log('PASS: progressive pending decode, fade stability, same-first replacement, disc generation, OFF dwell and OFF decode cancellation');
  console.log('PASS: slideshow progression, shuffle, dwell/fade, preload/decode, failures, stale work, OFF, reduced motion and legacy snapshots');
}
async function metadataFitTests() {
  const ids = ['album', 'album-artist', 'track-title', 'track-artist', 'drive-name'];
  const nodes = new Map();
  const frames = [];
  const writes = [];
  const reads = [];
  const requests = [];
  const observed = [];
  const segmenter = new Intl.Segmenter(undefined, { granularity: 'grapheme' });
  let resize;
  let viewportResize;
  let fontsReady;
  let fontWidth = 1;
  function node(id) {
    if (nodes.has(id)) return nodes.get(id);
    const attributes = new Map();
    const classes = new Set();
    const tracked = (kind) => new Proxy({}, { set(target, key, value) {
      writes.push(`${id}.${kind}.${key}`); target[key] = value; return true;
    } });
    const value = {
      id, width: 100, baseSize: id === 'drive-name' ? 14 : 20,
      textContent: '', title: '', dataset: tracked('dataset'), style: tracked('style'),
      complete: false, naturalWidth: 0,
      classList: { contains: (name) => classes.has(name), add: (name) => classes.add(name), remove: (name) => classes.delete(name) },
      addEventListener() {},
      getAttribute: (name) => attributes.get(name) ?? null,
      hasAttribute: (name) => attributes.has(name),
      setAttribute(name, value) { writes.push(`${id}.attribute.${name}`); attributes.set(name, value); },
      removeAttribute(name) { writes.push(`${id}.remove.${name}`); attributes.delete(name); },
      get clientWidth() { reads.push(`${id}.clientWidth`); return this.width; },
      // Deterministic grapheme-width approximation, not browser/font-layout evidence.
      get scrollWidth() {
        reads.push(`${id}.scrollWidth`);
        const size = Number.parseFloat(this.style.fontSize) || this.baseSize;
        let units = 0;
        for (const { segment } of segmenter.segment(this.textContent)) {
          units += /[\p{Script=Han}\p{Extended_Pictographic}]/u.test(segment) ? 1 : .5;
        }
        return Math.max(this.width, Math.ceil(units * size * fontWidth));
      },
    };
    const proxy = new Proxy(value, { set(target, key, value) {
      writes.push(`${id}.${key}`); target[key] = value; return true;
    } });
    nodes.set(id, proxy);
    return proxy;
  }
  const snapshot = {
    revision: 1,
    player: { state: 'STOPPED', track_number: 1, position_frames: 0, track_duration_frames: 4500 },
    disc: { state: 'AUDIO_READY', title: 'Album', artist: 'Artist' },
    tracks: [{ number: 1, title: 'Track', artist: 'Singer', duration_frames: 4500 }],
    drive: { vendor: 'V', model: 'Drive', read_offset_samples: 123 },
    read: { policy: { requested: { mode: 'SINGLE' }, effective: { mode: 'SINGLE' }, pending: false },
      strategy: { requested: 'PARANOIA', effective: 'PARANOIA', pending: false, downgrade_reason: 'NONE' },
      stats: { read_calls: 7, frames_accepted: 900 } },
    enrichment: { status: 'AVAILABLE' }, artwork: { cover: null },
  };
  const context = {
    addEventListener(event, callback) { assert.equal(event, 'resize'); viewportResize = callback; },
    performance: { now: () => 1 },
    document: { readyState: 'complete', getElementById: node, querySelectorAll: () => [], addEventListener() {},
      fonts: { ready: new Promise((resolve) => { fontsReady = resolve; }) } },
    ResizeObserver: class { constructor(callback) { resize = callback; } observe(target) { observed.push(target.id); } },
    getComputedStyle(element) { reads.push(`${element.id}.computedStyle`); return { fontSize: `${Number.parseFloat(element.style.fontSize) || element.baseSize}px` }; },
    location: { protocol: 'http:', host: 'localhost' },
    fetch: async (url) => { requests.push(url); return { ok: true, json: async () => snapshot }; },
    WebSocket: class {}, requestAnimationFrame: (fn) => frames.push(fn), setTimeout() {}, clearTimeout() {},
  };
  vm.runInNewContext(source, context);
  await new Promise(setImmediate);
  function flush() {
    let count = 0;
    while (frames.length) { assert(++count < 10, 'animation frames must settle'); frames.shift()(); }
  }
  function render() {
    snapshot.revision++;
    const input = structuredClone(snapshot);
    context.render(input); flush();
    assert.deepEqual(input, snapshot, 'fitting must preserve every raw snapshot field');
  }
  function width(id, value, height = 20) {
    node(id).width = value;
    resize([{ target: node(id), contentRect: { width: value, height } }]);
    flush();
  }
  function title(text) { snapshot.tracks[0].title = text; render(); }
  function unclipped(id, text) {
    assert.equal(node(id).textContent, text);
    assert.equal(node(id).title, '');
    assert.equal(node(id).getAttribute('aria-label'), null);
  }
  flush();
  const initialRequestCount = requests.length;
  assert.deepEqual(observed, ids, 'only the five metadata fields are observed');
  for (const id of ids) width(id, 100);
  unclipped('track-title', 'Track');
  assert.equal(node('track-title').style.fontSize, '');
  title('abcdefghijk');
  unclipped('track-title', 'abcdefghijk');
  assert.equal(node('track-title').style.fontSize, '18px', 'moderate text fits at 90%');
  title('abcdefghijkl');
  unclipped('track-title', 'abcdefghijkl');
  assert.equal(node('track-title').style.fontSize, '16px', 'moderate text fits at 80%');
  const long = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'.repeat(4);
  title(long);
  const clipped = node('track-title').textContent;
  assert(clipped.endsWith('...'));
  assert.equal(node('track-title').style.fontSize, '15px');
  assert.equal(node('track-title').title, long);
  assert.equal(node('track-title').getAttribute('aria-label'), long);
  assert(node('track-title').scrollWidth <= node('track-title').clientWidth);
  assert.equal(snapshot.tracks[0].title, long, 'render must not mutate its source');
  const next = `${long.slice(0, -1)}!`;
  title(next);
  assert.equal(node('track-title').textContent, clipped, 'tail-only change retains the same prefix');
  assert.equal(node('track-title').title, next);
  assert.equal(node('track-title').getAttribute('aria-label'), next);
  for (const text of ['\u6f22\u5b57'.repeat(30), 'e\u0301'.repeat(60), '\ud83d\udc69\u200d\ud83d\udcbb'.repeat(30)]) {
    title(text);
    const visible = node('track-title').textContent;
    assert(visible.endsWith('...'));
    const prefix = visible.slice(0, -3);
    const parts = Array.from(segmenter.segment(text), ({ segment }) => segment);
    assert(parts.some((_, index) => parts.slice(0, index + 1).join('') === prefix), 'prefix ends at a grapheme boundary');
    assert.equal(node('track-title').title, text);
  }
  width('track-title', 1000);
  const markup = '<script>literal title</script>';
  title(markup);
  unclipped('track-title', markup);
  assert.equal(node('track-title').innerHTML, undefined, 'markup must never be parsed');
  title(long);
  width('track-title', 100);
  assert.equal(node('track-title').style.fontSize, '15px');
  width('track-title', 2000);
  unclipped('track-title', long);
  assert.equal(node('track-title').style.fontSize, '', 'wide resize restores CSS base size');
  width('track-title', 100);
  assert.equal(node('track-title').style.fontSize, '15px', 'narrow resize does not compound shrinking');
  node('track-title').baseSize = 24;
  viewportResize(); flush();
  assert.equal(node('track-title').style.fontSize, '18px', 'viewport resize recomputes font even at unchanged column width');
  node('track-title').baseSize = 20;
  viewportResize(); flush();
  assert.equal(node('track-title').style.fontSize, '15px');
  title('Short');
  unclipped('track-title', 'Short');
  assert.equal(node('track-title').style.fontSize, '');
  width('track-title', 0);
  reads.length = 0;
  title(long);
  assert(!reads.some((read) => read === 'track-title.scrollWidth'), 'zero width defers fitting');
  assert.equal(node('track-title').textContent, long);
  width('track-title', 100);
  assert(node('track-title').textContent.endsWith('...'), 'visible resize activates deferred fitting');
  reads.length = 0; writes.length = 0;
  resize([{ target: node('track-title'), contentRect: { width: 100, height: 99 } }]);
  assert.equal(frames.length, 0, 'height-only feedback schedules no fit');
  assert.deepEqual(reads, []); assert.deepEqual(writes, []);
  title('abcdefghij');
  assert.equal(node('track-title').style.fontSize, '');
  fontWidth = 1.2;
  fontsReady(); await new Promise(setImmediate); flush();
  assert.equal(node('track-title').style.fontSize, '16px', 'fonts.ready recomputes unchanged metadata');
  fontWidth = 1;
  const requestCount = requests.length;
  reads.length = 0; writes.length = 0;
  for (let i = 1; i <= 60; i++) {
    snapshot.player.position_frames = i * 15;
    snapshot.read.stats.read_calls = i;
    render();
  }
  const fieldWrites = writes.filter((write) => ids.some((id) => write.startsWith(`${id}.`)));
  assert.deepEqual(fieldWrites, [], '60 increasing revisions must not rewrite metadata');
  assert.deepEqual(reads, [], '60 increasing revisions must not perform fitting reads');
  assert.equal(requests.length, requestCount, 'metadata fitting performs no extra fetch');
  assert.equal(node('read-stats').textContent, '60 / 900 frames');
  assert.equal(node('drive-offset').textContent, '123 samples');
  assert.equal(node('read-warning').textContent, 'NONE');
  assert.equal(node('read-strategy-state').textContent, 'Pending: NOT PENDING \u00b7 Downgrade: NONE');
  assert.equal(node('read-policy-single').textContent, 'SINGLE \u00b7 EFFECTIVE / REQUESTED');
  for (const id of ['read-stats', 'drive-offset', 'read-warning', 'read-strategy-state', 'read-policy-single']) {
    assert.equal(node(id).style.fontSize, undefined);
    assert.equal(node(id).getAttribute('aria-label'), null);
  }
  for (const id of ids) width(id, 400);
  snapshot.disc.title = long; snapshot.disc.artist = long;
  snapshot.tracks[0].title = long; snapshot.tracks[0].artist = long;
  snapshot.drive.model = long; render();
  for (const id of ids) assert(node(id).textContent.endsWith('...'));
  assert.equal(node('drive-name').style.fontSize, '12px', 'small base font respects 12px minimum');
  snapshot.disc.title = 'Album'; snapshot.disc.artist = 'Artist';
  snapshot.tracks[0].title = 'Track'; snapshot.tracks[0].artist = 'Singer'; render();
  for (const [id, text] of [['album', 'Album'], ['album-artist', 'Artist'], ['track-title', 'Track'], ['track-artist', 'Singer']]) {
    unclipped(id, text); assert.equal(node(id).style.fontSize, '');
  }
  snapshot.disc.title = long; snapshot.disc.artist = long;
  snapshot.tracks[0].title = long; snapshot.tracks[0].artist = long; render();
  const driveBefore = node('drive-name').textContent;
  const driveTitle = node('drive-name').title;
  snapshot.player = { state: 'NO_DISC' }; snapshot.disc = { state: 'NO_DISC' }; render();
  for (const [id, text] of [['album', 'No disc'], ['album-artist', 'Insert an audio CD'], ['track-title', '\u2014'], ['track-artist', '\u2014']]) {
    unclipped(id, text); assert.equal(node(id).style.fontSize, '');
  }
  assert.equal(node('drive-name').textContent, driveBefore, 'disc removal retains drive fitting');
  assert.equal(node('drive-name').title, driveTitle);
  snapshot.drive = { vendor: 'V', model: 'New' }; render();
  unclipped('drive-name', 'V New');
  assert.equal(node('drive-name').style.fontSize, '');
  assert.equal(requests.length, initialRequestCount, 'all fitting, resize and font work adds no fetch');
  console.log('PASS: metadata fit: scales, clipping, graphemes, literal text, raw cache, resets, resize, fonts and 60 updates (0 fitting reads / 0 metadata writes)');
}
main().then(slideshowTests).then(metadataFitTests).catch((error) => { console.error(error); process.exitCode = 1; });
