'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../ui/default/player.js'), 'utf8');

async function main() {
  const nodes = new Map();
  const sockets = [];
  const posts = [];
  const policyPosts = [];
  const backgroundPosts = [];
  const pendingBackgroundGets = [];
  const pendingBackgroundPosts = [];
  let background = { requested: false, effective: false, available: false, unavailable_reason: 'runtime_unavailable' };
  let backgroundGetStatus = 200;
  let backgroundPostStatus = 204;
  let deferBackgroundGet = false;
  let deferBackgroundPost = false;
  let backgroundOffline = false;
  let backgroundGets = 0;
  const pendingPolicyGets = [];
  let deferPolicyGet = false;
  let policyPostStatus = 204;
  let policyGetStatus = 200;
  const policy = { requested: { mode: 'SINGLE', region_frames: 75,
    required_matches: 2, maximum_attempts: 3, time_budget_ms: 10000 },
  effective: { mode: 'SINGLE' }, pending: false, persistence_configured: false,
  requested_source: 'startup', effective_source: 'startup' };
  const postBodies = [];
  let postStatus = 204;
  let failPost = false;
  let now = 1000;
  let activeElement = null;
  const keys = {};
  const timers = [];
  const snapshot = {
    schema_version: 1, revision: 1,
    player: { state: 'STOPPED', track_number: 1, position_frames: 0, track_duration_frames: 4500 },
    disc: { state: 'AUDIO_READY' }, tracks: [{ number: 1, duration_frames: 4500 }, { number: 2, duration_frames: 4500 }],
    enrichment: { status: 'NOT_REQUESTED' }, artwork: { cover: null },
  };
  function element() {
    return {
      textContent: '', style: {}, dataset: {}, hidden: false, disabled: true,
      classList: { contains: () => false, add() {}, remove() {} },
      removeAttribute() {}, addEventListener(type, fn) { this[`on${type}`] = fn; },
      focus() { this.focused = true; activeElement = this; }, blur() { this.focused = false; },
      setAttribute(name, value) { this[name] = value; },
      append(...children) { this.children = [...(this.children || []), ...children]; },
      replaceChildren(...children) { this.children = children; },
    };
  }
  function node(id) {
    if (nodes.has(id)) return nodes.get(id);
    const item = element();
    nodes.set(id, item);
    return item;
  }
  const controls = ['previous', 'play', 'pause', 'stop', 'next', 'metadata', 'settings'].map((command) => {
    const button = node(`button-${command}`);
    button.dataset.command = command;
    return button;
  });
  node('settings-close').disabled = false; // Matches the always-enabled Back button in player.html.
  vm.runInNewContext(source, {
    addEventListener() {},
    ResizeObserver: class { observe() {} }, getComputedStyle: () => ({ fontSize: '20px' }),
    Date: class extends Date { static now() { return now; } },
    performance: { now: () => 1 },
    document: {
      fonts: { ready: Promise.resolve() },
      readyState: 'complete', getElementById: node,
      get activeElement() { return activeElement; },
      createElement: element,
      querySelectorAll: () => controls,
      addEventListener(type, fn) { keys[type] = fn; },
    },
    location: { protocol: 'http:', host: 'localhost' },
    fetch: async (url, options) => {
      if (url === '/api/state') return { ok: true, json: async () => snapshot };
      if (url === '/api/read-policy') {
        if (options?.method === 'POST') {
          const body = JSON.parse(options.body);
          policyPosts.push(body);
          if (policyPostStatus === 204) {
            policy.requested = { ...body, mode: body.mode.toUpperCase() };
            policy.pending = true;
          }
          return { ok: policyPostStatus === 204, status: policyPostStatus };
        }
        if (deferPolicyGet) return new Promise((resolve) => pendingPolicyGets.push(resolve));
        return { ok: policyGetStatus === 200, status: policyGetStatus, json: async () => ({ ...policy }) };
      }
      if (url === '/api/artist-background') {
        if (backgroundOffline) throw new Error('offline');
        if (options?.method === 'POST') {
          const body = JSON.parse(options.body);
          backgroundPosts.push(body);
          if (deferBackgroundPost) return new Promise((resolve) => pendingBackgroundPosts.push(resolve));
          if (backgroundPostStatus === 204) background = { ...background, requested: body.enabled,
            effective: body.enabled && background.available };
          return { status: backgroundPostStatus };
        }
        backgroundGets += 1;
        if (deferBackgroundGet) return new Promise((resolve) => pendingBackgroundGets.push(resolve));
        return { ok: backgroundGetStatus === 200, status: backgroundGetStatus, json: async () => ({ ...background }) };
      }
      if (options?.method === 'POST' && url !== '/api/ui-boot') {
        posts.push(url);
        postBodies.push(options.body);
        if (failPost) throw new Error('offline');
        return { status: postStatus, ok: postStatus === 204 };
      }
      return { ok: true, status: 204, json: async () => ({}) };
    },
    WebSocket: class { constructor(url) { this.url = url; sockets.push(this); } close() { this.onclose?.(); } },
    requestAnimationFrame() {}, setTimeout(fn) { timers.push(fn); return timers.length; }, clearTimeout() {},
  });
  for (let i = 0; i < 12; i++) await Promise.resolve();
  assert.equal(sockets.length, 2);
  assert(sockets[0].url.endsWith('/api/events'));
  assert(sockets[1].url.endsWith('/api/navigation'));
  assert.equal(controls[1].disabled, false); // play
  assert.equal(controls[2].disabled, true); // pause
  const navigation = (action) => sockets.at(-1).onmessage({ data: JSON.stringify({ action }) });
  assert.equal(controls[6].disabled, false);
  assert.equal(backgroundGets, 0); // Merely loading the Player never asks for background settings.
  controls[6].onclick({ detail: 1 });
  assert.equal(controls[6].disabled, false);
  await new Promise(setImmediate);
  assert.equal(node('policy-single').dataset.selected, 'true');
  assert.match(node('settings-persistence').textContent, /session only/);
  assert.equal(node('settings-policy-source').textContent, 'Source (requested/effective): Startup / Startup.');
  assert.equal(node('background-on').disabled, true);
  assert.equal(node('background-off').disabled, false);
  assert.equal(node('background-off').dataset.selected, 'true');
  assert.match(node('settings-background-status').textContent, /runtime image delivery/);
  assert.deepEqual(backgroundPosts, []);
  for (const [requested, effective, expected] of [
    ['startup', 'restored', 'Startup / Restored'],
    ['saved', 'session', 'Saved / Session only'],
    ['__proto__', 'future-value', 'Unknown / Unknown'],
    [123, null, 'Unknown / Unknown'],
    [undefined, undefined, 'Unknown / Unknown'],
  ]) {
    policy.requested_source = requested;
    policy.effective_source = effective;
    policy.persistence_configured = true;
    navigation('back');
    controls[6].onclick({ detail: 1 });
    assert.equal(node('settings-policy-source').textContent, 'Source (requested/effective): Unknown / Unknown.');
    await new Promise(setImmediate);
    assert.equal(node('settings-policy-source').textContent, `Source (requested/effective): ${expected}.`);
  }
  policy.requested_source = 'session';
  policy.effective_source = 'restored';
  policy.persistence_configured = false;
  navigation('back');
  controls[6].onclick({ detail: 1 });
  await new Promise(setImmediate);
  navigation('right');
  assert.equal(node('policy-repeat').dataset.focused, 'true');
  navigation('select');
  await new Promise(setImmediate);
  assert.equal(policyPosts.length, 1);
  assert.equal(policyPosts[0].mode, 'repeat');
  assert.match(node('settings-policy-status').textContent, /until playback stops/);
  assert.equal(node('settings-policy-source').textContent, 'Source (requested/effective): Session only / Restored.');
  navigation('back');
  assert.equal(node('settings-panel').hidden, true);
  controls[6].onclick({ detail: 1 });
  await new Promise(setImmediate);
  assert.equal(node('policy-repeat').dataset.focused, 'true'); // Reopening follows requested mode.
  policyPostStatus = 409;
  node('policy-single').onclick({ detail: 1 });
  await new Promise(setImmediate);
  assert.equal(node('settings-policy-status').textContent,
    'Policy change failed. Current playback continues.');
  assert.equal(policy.requested.mode, 'REPEAT');
  assert.equal(node('settings-policy-source').textContent, 'Source (requested/effective): Session only / Restored.');
  navigation('back');
  navigation('back');
  navigation('right');
  keys.keydown({ key: 'ArrowRight', preventDefault() {} });
  assert.equal(controls[0].dataset.focused, 'true'); // one TV press, two input paths
  navigation('back');
  now += 300;
  keys.keydown({ key: 'ArrowRight', preventDefault() {} });
  navigation('right');
  assert.equal(controls[0].dataset.focused, 'true'); // reverse arrival order
  navigation('back');
  now += 300;
  navigation('right');
  assert.equal(controls[0].dataset.focused, 'true');
  navigation('right');
  assert.equal(controls[1].dataset.focused, 'true');
  navigation('select');
  for (let i = 0; i < 4; i++) await Promise.resolve();
  assert.deepEqual(posts, ['/api/play']);
  controls[1].onclick({ detail: 0 }); // Browser activation of the same CEC press
  for (let i = 0; i < 4; i++) await Promise.resolve();
  assert.deepEqual(posts, ['/api/play']);
  assert.equal(node('control-feedback').textContent, 'REQUEST ACCEPTED');
  navigation('back');
  now += 300;
  controls[1].onclick({ detail: 0 }); // Browser activation arrives first
  for (let i = 0; i < 4; i++) await Promise.resolve();
  navigation('select');
  assert.deepEqual(posts, ['/api/play', '/api/play']);
  posts.pop();
  navigation('back');
  assert.equal(controls[1].dataset.focused, 'false');
  snapshot.revision = 2;
  snapshot.player.state = 'PLAYING';
  sockets[0].onmessage({ data: JSON.stringify(snapshot) });
  assert.equal(controls[1].disabled, true);
  assert.equal(controls[2].disabled, false);
  controls[2].onclick();
  for (let i = 0; i < 4; i++) await Promise.resolve();
  assert.deepEqual(posts, ['/api/play', '/api/pause']);
  postStatus = 409;
  controls[3].onclick();
  for (let i = 0; i < 4; i++) await Promise.resolve();
  assert.equal(node('control-feedback').textContent, 'REJECTED · 409');
  failPost = true;
  controls[4].onclick();
  for (let i = 0; i < 4; i++) await Promise.resolve();
  assert.equal(node('control-feedback').textContent, 'CONNECTION ERROR');
  snapshot.revision = 3;
  snapshot.disc.state = 'NO_DISC';
  snapshot.player.state = 'NO_DISC';
  sockets[0].onmessage({ data: JSON.stringify(snapshot) });
  navigation('select');
  assert.deepEqual(posts, ['/api/play', '/api/pause', '/api/stop', '/api/next']);
  keys.keydown({ key: 'ArrowRight', preventDefault() {} });
  assert(controls.slice(0, 5).every((button) => button.disabled));
  assert.equal(controls[6].disabled, false);
  snapshot.revision = 4;
  snapshot.disc.state = 'AUDIO_READY';
  snapshot.player.state = 'UNKNOWN';
  sockets[0].onmessage({ data: JSON.stringify(snapshot) });
  assert(controls.slice(0, 5).every((button) => button.disabled));
  assert.equal(controls[6].disabled, false);
  snapshot.revision = 5;
  snapshot.player.state = 'STOPPED';
  sockets[0].onmessage({ data: JSON.stringify(snapshot) });
  now += 300;
  sockets[1].onclose();
  timers.at(-1)(); // navigation reconnect delay
  assert.equal(sockets.length, 3);
  sockets[2].onmessage({ data: JSON.stringify({ action: 'right' }) });
  assert.equal(controls[0].dataset.focused, 'true');
  sockets[1].onmessage({ data: JSON.stringify({ action: 'right' }) });
  assert.equal(controls[0].dataset.focused, 'true'); // stale socket is ignored
  deferPolicyGet = true;
  controls[6].onclick({ detail: 1 });
  assert.equal(node('policy-single').disabled, true); // No stale value before GET completes.
  assert.equal(node('settings-policy-status').textContent, 'Loading read policy…');
  assert.equal(node('settings-policy-source').textContent, 'Source (requested/effective): Unknown / Unknown.');
  now += 300;
  keys.keydown({ key: 'ArrowRight', preventDefault() {} });
  pendingPolicyGets.shift()({ ok: true, json: async () => ({ ...policy,
    requested: { ...policy.requested, mode: 'SINGLE' } }) });
  await new Promise(setImmediate);
  assert.equal(node('settings-close').dataset.focused, 'true'); // Skip disabled choices and preserve deliberate focus.
  keys.keydown({ key: 'Escape', preventDefault() {} });
  controls[6].onclick({ detail: 1 });
  keys.keydown({ key: 'Escape', preventDefault() {} });
  controls[6].onclick({ detail: 1 });
  pendingPolicyGets.pop()({ ok: true, json: async () => ({ ...policy,
    requested: { ...policy.requested, mode: 'REPEAT' }, requested_source: 'saved' }) });
  await new Promise(setImmediate);
  pendingPolicyGets.shift()({ ok: true, json: async () => ({ ...policy,
    requested: { ...policy.requested, mode: 'SINGLE' }, requested_source: 'startup' }) });
  await new Promise(setImmediate);
  assert.equal(node('policy-repeat').dataset.selected, 'true'); // Old GET cannot replace it.
  assert.equal(node('settings-policy-source').textContent, 'Source (requested/effective): Saved / Restored.');
  now += 300;
  sockets[2].onmessage({ data: JSON.stringify({ action: 'right' }) });
  assert.equal(node('settings-close').dataset.focused, 'true');
  sockets[2].onmessage({ data: JSON.stringify({ action: 'select' }) });
  assert.equal(node('settings-panel').hidden, true); // CEC can reach and activate Back.
  deferPolicyGet = false;
  controls[6].onclick({ detail: 1 });
  await new Promise(setImmediate);
  keys.keydown({ key: 'Tab', preventDefault() {} });
  assert.equal(activeElement, node('settings-close'));
  keys.keydown({ key: 'Tab', preventDefault() {} });
  assert.equal(activeElement, node('background-off')); // Focus stays inside the dialog, skips unavailable On.
  keys.keydown({ key: 'Tab', preventDefault() {} });
  assert.equal(activeElement, node('policy-single'));
  keys.keydown({ key: 'Tab', shiftKey: true, preventDefault() {} });
  assert.equal(activeElement, node('background-off'));
  keys.keydown({ key: 'Escape', preventDefault() {} });
  policyGetStatus = 503;
  controls[6].onclick({ detail: 1 });
  await new Promise(setImmediate);
  assert.equal(node('settings-policy-source').textContent, 'Source (requested/effective): Unknown / Unknown.');
  assert.equal(node('settings-policy-status').textContent, 'Read policy unavailable. Playback is unaffected.');
  keys.keydown({ key: 'Escape', preventDefault() {} });
  policyGetStatus = 200;
  // Background is independent of Read Policy and never optimistically enables photos.
  const reopenSettings = async () => {
    navigation('back');
    controls[6].onclick({ detail: 1 });
    await new Promise(setImmediate);
  };
  await reopenSettings();
  node('background-on').onclick({ detail: 1 });
  assert.equal(backgroundPosts.length, 0);
  node('background-off').onclick({ detail: 1 });
  await new Promise(setImmediate);
  assert.deepEqual(backgroundPosts, [{ enabled: false }]);
  background = { requested: false, effective: false, available: true, unavailable_reason: null };
  await reopenSettings();
  assert.equal(node('background-on').disabled, false);
  assert.equal(backgroundPosts.length, 1);
  node('background-on').onclick({ detail: 1 });
  node('background-on').onclick({ detail: 1 });
  assert.equal(node('background-off').dataset.selected, 'true'); // No optimistic update.
  await new Promise(setImmediate);
  assert.deepEqual(backgroundPosts.at(-1), { enabled: true });
  assert.equal(backgroundPosts.length, 2); // No duplicate in-flight mutation.
  assert.equal(node('background-on').dataset.selected, 'true');
  assert.equal(node('settings-background-state').textContent, 'Requested / effective: On / On.');
  node('background-off').onclick({ detail: 1 });
  await new Promise(setImmediate);
  assert.equal(node('background-off').dataset.selected, 'true');
  const postCount = backgroundPosts.length;
  for (const status of [409, 403, 500, 200]) {
    backgroundPostStatus = status;
    node('background-on').onclick({ detail: 1 });
    await new Promise(setImmediate);
    assert.match(node('settings-background-status').textContent, /change failed/);
    assert.equal(node('background-off').dataset.selected, 'true');
  }
  assert.equal(backgroundPosts.length, postCount + 4);
  backgroundPostStatus = 204;
  backgroundGetStatus = 503;
  node('background-on').onclick({ detail: 1 });
  await new Promise(setImmediate);
  assert.match(node('settings-background-state').textContent, /Unknown/); // Accepted is not an observed result.
  assert.equal(node('background-on').disabled, true);
  backgroundGetStatus = 200;
  background = { requested: false, effective: false, available: true, unavailable_reason: null };
  await reopenSettings();
  backgroundOffline = true;
  node('background-on').onclick({ detail: 1 });
  await new Promise(setImmediate);
  assert.match(node('settings-background-status').textContent, /change failed/);
  backgroundOffline = false;
  for (const malformed of [{}, { ...background, requested: 'false' }, { ...background, effective: 0 },
    { ...background, available: 'true' }, { ...background, unavailable_reason: undefined },
    { ...background, requested: false, effective: true }]) {
    const valid = background;
    background = malformed;
    await reopenSettings();
    assert.equal(node('background-on').disabled, true);
    assert.equal(node('background-off').disabled, true);
    assert.match(node('settings-background-state').textContent, /Unknown \/ Unknown/);
    assert.equal(node('policy-repeat').disabled, false);
    background = valid;
  }
  background = { requested: false, effective: false, available: false, unavailable_reason: 'private-provider-url-or-key' };
  await reopenSettings();
  assert.equal(node('background-on').disabled, true);
  assert.equal(node('background-off').disabled, false);
  assert(!node('settings-background-status').textContent.includes(background.unavailable_reason));
  for (const reason of ['key_missing', 'metadata_disabled', '__proto__']) {
    background.unavailable_reason = reason;
    await reopenSettings();
    assert.equal(node('background-on').disabled, true);
    assert(!node('settings-background-status').textContent.includes('__proto__'));
  }
  backgroundGetStatus = 503;
  await reopenSettings();
  assert.equal(node('background-on').disabled, true);
  assert.equal(node('policy-repeat').disabled, false);
  assert.match(node('settings-background-state').textContent, /Unknown/);
  backgroundGetStatus = 200;
  policyGetStatus = 503;
  background = { requested: false, effective: false, available: true, unavailable_reason: null };
  await reopenSettings();
  assert.equal(node('background-on').disabled, false);
  assert.equal(node('policy-single').disabled, true);
  policyGetStatus = 200;
  // A late GET from an earlier opening must not replace the current state.
  deferBackgroundGet = true;
  await reopenSettings();
  assert.equal(node('background-on').disabled, true);
  await reopenSettings();
  pendingBackgroundGets.pop()({ ok: true, json: async () => ({ ...background }) });
  await new Promise(setImmediate);
  pendingBackgroundGets.shift()({ ok: true, json: async () => ({ requested: true, effective: true,
    available: true, unavailable_reason: null }) });
  await new Promise(setImmediate);
  assert.equal(node('background-off').dataset.selected, 'true');
  deferBackgroundGet = false;
  // Reopening during POST retains its guard, then re-reads the authoritative result.
  deferBackgroundPost = true;
  node('background-on').onclick({ detail: 1 });
  const writingCount = backgroundPosts.length;
  await reopenSettings();
  node('background-on').onclick({ detail: 1 });
  assert.equal(backgroundPosts.length, writingCount);
  background = { ...background, requested: true, effective: true };
  pendingBackgroundPosts.shift()({ status: 204 });
  await new Promise(setImmediate);
  assert.equal(node('background-on').dataset.selected, 'true');
  assert.equal(node('background-on').disabled, false);
  deferBackgroundPost = false;
  // Tab and semantic navigation share focus, including background selection and Back.
  node('background-off').focus();
  keys.keydown({ key: 'Tab', preventDefault() {} });
  assert.equal(activeElement, node('background-on'));
  now += 300;
  navigation('left');
  assert.equal(node('background-off').dataset.focused, 'true');
  navigation('select');
  await new Promise(setImmediate);
  assert.equal(node('background-off').dataset.selected, 'true');
  now += 300;
  const keyboardCount = backgroundPosts.length;
  node('background-on').onclick({ detail: 0 });
  navigation('select');
  await new Promise(setImmediate);
  assert.equal(backgroundPosts.length, keyboardCount + 1);
  assert.deepEqual(backgroundPosts.at(-1), { enabled: true });
  const deduplicatedCount = backgroundPosts.length;
  navigation('select');
  await new Promise(setImmediate);
  assert.equal(backgroundPosts.length, deduplicatedCount + 1); // A distinct same-source press remains valid.
  navigation('back');
  assert.equal(node('settings-panel').hidden, true);
  failPost = false;
  postStatus = 204;
  snapshot.revision = 6;
  snapshot.enrichment = { status: 'UNAVAILABLE', selection: {
    state: 'AMBIGUOUS', session_id: 'session-1', disc_generation: 2, metadata_generation: 3,
    selected_index: null,
    candidates: [
      { index: 0, title: 'The Slip', artist: 'Nine Inch Nails', date: '2008', country: 'US', track_count: 10 },
      { index: 1, title: 'The Slip', artist: 'Nine Inch Nails', date: '2008', country: 'GB', track_count: 10 },
    ],
  } };
  sockets[0].onmessage({ data: JSON.stringify(snapshot) });
  assert.equal(controls[5].hidden, false);
  assert.equal(node('media-message').textContent, 'ALBUM SELECTION AVAILABLE');
  assert.equal(node('metadata-match-status').textContent, '2 MATCHES · CHOOSE');
  assert.equal(node('metadata-picker').hidden, false); // New ambiguity opens automatically.
  const pickerNavigation = (action) => sockets[2].onmessage({ data: JSON.stringify({ action }) });
  now += 300;
  pickerNavigation('back');
  assert.equal(node('metadata-picker').hidden, true);
  snapshot.revision += 1;
  sockets[0].onmessage({ data: JSON.stringify(snapshot) });
  assert.equal(node('metadata-picker').hidden, true); // Back sticks for the same candidate set.
  pickerNavigation('left'); // Settings follows the metadata control.
  pickerNavigation('left');
  assert.equal(controls[5].dataset.focused, 'true');
  pickerNavigation('select');
  assert.equal(node('metadata-picker').hidden, false);
  assert.equal(node('metadata-candidates').children.length, 3); // Includes None of these.
  const postsBeforeDuplicateOpen = posts.length;
  controls[5].onclick({ detail: 0 }); // Delayed browser click from the same CEC press.
  assert.equal(posts.length, postsBeforeDuplicateOpen);
  assert.equal(node('metadata-candidates').children[0].dataset.focused, 'true');
  pickerNavigation('down');
  assert.equal(node('metadata-candidates').children[1].dataset.focused, 'true');
  pickerNavigation('select');
  for (let i = 0; i < 4; i++) await Promise.resolve();
  assert.equal(posts.at(-1), '/api/metadata-selection');
  assert.equal(JSON.parse(postBodies.at(-1)).candidate_index, 1);
  assert.equal(node('metadata-picker').hidden, false); // 204 is not application.
  assert.equal(node('metadata-feedback').textContent, 'WAITING FOR ALBUM UPDATE');
  pickerNavigation('select');
  assert.equal(posts.filter((url) => url === '/api/metadata-selection').length, 1);
  snapshot.revision = 8;
  snapshot.enrichment.status = 'AVAILABLE';
  snapshot.enrichment.selection.state = 'SELECTED';
  snapshot.enrichment.selection.selected_index = 1;
  snapshot.disc.title = 'The Slip';
  sockets[0].onmessage({ data: JSON.stringify(snapshot) });
  assert.equal(node('metadata-picker').hidden, true);
  assert.equal(node('album').textContent, 'The Slip');
  assert.equal(controls[5].hidden, false);
  assert.equal(controls[5].textContent, 'Change album');
  assert.equal(node('metadata-match-status').textContent, '2 MATCHES · #2');
  assert.equal(node('metadata-picker').hidden, true); // Selected state does not auto-open.
  pickerNavigation('left');
  pickerNavigation('left');
  assert.equal(controls[5].dataset.focused, 'true');
  pickerNavigation('select');
  assert.equal(node('metadata-picker').hidden, false);
  assert.equal(node('metadata-candidates').children[1].dataset.focused, 'true');
  assert.equal(node('metadata-candidates').children[1]['aria-selected'], 'true');
  assert.match(node('metadata-candidates').children[1].children[1].textContent, /CURRENT SELECTION/);
  pickerNavigation('select'); // Selecting the current choice is a local close, not another POST.
  assert.equal(node('metadata-picker').hidden, true);
  controls[5].onclick({ detail: 1 });
  pickerNavigation('up');
  pickerNavigation('select');
  for (let i = 0; i < 4; i++) await Promise.resolve();
  assert.equal(JSON.parse(postBodies.at(-1)).candidate_index, 0);
  assert.equal(node('metadata-picker').hidden, false); // Wait for authoritative snapshot.
  snapshot.revision = 9;
  snapshot.artwork.cover = { url: '/api/presentation/artwork/cover' };
  sockets[0].onmessage({ data: JSON.stringify(snapshot) });
  const firstCoverIdentity = node('cover').dataset.identity;
  snapshot.revision = 10;
  snapshot.enrichment.selection.selected_index = 0;
  sockets[0].onmessage({ data: JSON.stringify(snapshot) });
  assert.equal(node('metadata-picker').hidden, true);
  assert.equal(node('metadata-match-status').textContent, '2 MATCHES · #1');
  assert.notEqual(node('cover').dataset.identity, firstCoverIdentity);
  snapshot.revision = 11;
  pickerNavigation('left');
  pickerNavigation('left');
  pickerNavigation('select');
  pickerNavigation('up'); // Wrap from candidate 0 to None of these.
  assert.equal(node('metadata-candidates').children[2].dataset.focused, 'true');
  pickerNavigation('select');
  for (let i = 0; i < 4; i++) await Promise.resolve();
  assert.equal(JSON.parse(postBodies.at(-1)).candidate_index, null);
  assert.equal(node('metadata-picker').hidden, false);
  snapshot.enrichment.selection.state = 'DECLINED';
  snapshot.enrichment.selection.selected_index = null;
  snapshot.enrichment.status = 'UNAVAILABLE';
  snapshot.disc.title = null;
  snapshot.artwork.cover = null;
  sockets[0].onmessage({ data: JSON.stringify(snapshot) });
  assert.equal(node('metadata-picker').hidden, true);
  assert.equal(node('album').textContent, 'Audio CD');
  assert.equal(node('media-message').textContent, 'NO ALBUM MATCH SELECTED');
  assert.equal(node('metadata-match-status').textContent, '2 MATCHES · NONE');
  snapshot.revision = 12;
  sockets[0].onmessage({ data: JSON.stringify(snapshot) });
  assert.equal(node('metadata-picker').hidden, true); // Explicit decline stays dismissed.
  controls[5].onclick({ detail: 1 });
  assert.equal(node('metadata-candidates').children[2].dataset.focused, 'true');
  assert.equal(node('metadata-candidates').children[2]['aria-selected'], 'true');
  pickerNavigation('back');
  snapshot.revision = 13;
  snapshot.enrichment.status = 'UNAVAILABLE';
  snapshot.enrichment.selection.state = 'AMBIGUOUS';
  snapshot.enrichment.selection.selected_index = null;
  snapshot.enrichment.selection.metadata_generation = 4;
  sockets[0].onmessage({ data: JSON.stringify(snapshot) });
  assert.equal(node('metadata-picker').hidden, false); // New generation opens once.
  postStatus = 409;
  pickerNavigation('select');
  for (let i = 0; i < 4; i++) await Promise.resolve();
  assert.equal(node('metadata-feedback').textContent, 'SELECTION REJECTED · 409');
  pickerNavigation('back');
  assert.equal(node('metadata-picker').hidden, true);
  snapshot.revision = 14;
  snapshot.disc.state = 'NO_DISC';
  snapshot.enrichment.selection = null;
  sockets[0].onmessage({ data: JSON.stringify(snapshot) });
  assert.equal(controls[5].hidden, true);
  assert.equal(node('metadata-match-status').hidden, true);
  console.log('PASS: CEC/keyboard focus, command POST, authoritative state and No Disc');
}
main().catch((error) => { console.error(error); process.exitCode = 1; });
