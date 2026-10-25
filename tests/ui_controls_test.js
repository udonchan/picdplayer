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
  const pcmPosts = [];
  const pendingPcmGets = [];
  const pendingPcmPosts = [];
  let pcm = { schema_version: 1, session_id: 'pcm-session', disc_generation: 2, state: 'IDLE',
    failure: 'NONE', busy: false, reader_released: true, job_id: 0, accepted_frames: 0, accepted_bytes: 0,
    can_start: true, start_rejection: null, profile: { canonical: 's16le-44100-stereo-v1',
      algorithm: 'sha256', version: 'local-pcm-sha256-v1' }, external_verification: 'NOT_CHECKED' };
  let pcmGetStatus = 404;
  let pcmPostStatus = 202;
  let pcmGets = 0;
  let deferPcmGet = false;
  let deferPcmPost = false;
  const pcmSignals = [];
  const pendingBackgroundGets = [];
  const pendingBackgroundPosts = [];
  let background = { requested: false, effective: false, available: false, unavailable_reason: 'runtime_unavailable' };
  let backgroundGetStatus = 200;
  let backgroundPostStatus = 204;
  let deferBackgroundGet = false;
  let deferBackgroundPost = false;
  let backgroundOffline = false;
  let backgroundSaveFails = false;
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
  const cancelledTimers = new Set();
  const timerDelays = new Map();
  const snapshot = {
    schema_version: 1, revision: 1,
    player: { state: 'STOPPED', track_number: 1, position_frames: 0, track_duration_frames: 4500 },
    disc: { state: 'AUDIO_READY' }, tracks: [{ number: 1, duration_frames: 4500 }, { number: 2, duration_frames: 4500 }],
    enrichment: { status: 'NOT_REQUESTED' }, artwork: { cover: null },
  };
  function element() {
    return {
      textContent: '', style: {}, dataset: {}, hidden: false, disabled: true, ariaDisabled: 'false',
      classList: { contains: () => false, add() {}, remove() {} },
      removeAttribute() {}, addEventListener(type, fn) { this[`on${type}`] = fn; },
      focus() {
        this.focused = true;
        if (activeElement !== this) {
          activeElement = this;
          this.onfocus?.();
        }
      }, blur() { this.focused = false; },
      setAttribute(name, value) { this[name] = value; },
      append(...children) { this.children = [...(this.children || []), ...children]; },
      replaceChildren(...children) { this.children = children; },
    };
  }
  function node(id) {
    if (nodes.has(id)) return nodes.get(id);
    const item = element();
    if (id === 'settings-pcm-status') delete item.disabled; // A paragraph is not a disabled button.
    nodes.set(id, item);
    return item;
  }
  const controls = ['previous', 'play', 'pause', 'stop', 'next', 'metadata', 'settings'].map((command) => {
    const button = node(`button-${command}`);
    button.dataset.command = command;
    return button;
  });
  const settingsIds = ['policy-single', 'policy-repeat', 'background-off',
    'background-on', 'pcm-start', 'pcm-cancel', 'settings-close'];
  settingsIds.forEach((id) => { node(id).disabled = false; });
  node('settings-panel').querySelector = (selector) => {
    assert.equal(selector, '.settings-surface');
    return node('settings-surface');
  };
  vm.runInNewContext(source, {
    AbortController,
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
      if (url === '/api/complete-pcm') {
        pcmSignals.push(options?.signal);
        if (options?.method === 'POST') {
          pcmPosts.push(JSON.parse(options.body));
          if (deferPcmPost) return new Promise((resolve) => pendingPcmPosts.push(resolve));
          return { status: pcmPostStatus, json: async () => ({ error: 'capture_releasing', executed: false, retry: 'manual' }) };
        }
        pcmGets += 1;
        if (deferPcmGet) return new Promise((resolve) => pendingPcmGets.push(resolve));
        return { ok: pcmGetStatus === 200, status: pcmGetStatus, json: async () => ({ ...pcm }) };
      }
      if (url === '/api/read-policy') {
        if (options?.method === 'POST') {
          const body = JSON.parse(options.body);
          policyPosts.push(body);
          if (policyPostStatus === 204) {
            policy.requested = { ...body, mode: body.mode.toUpperCase() };
            policy.pending = true;
            if (background.persistence_configured) background = { ...background,
              persistence_state: 'saved', requested_source: 'saved' };
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
          if (backgroundPostStatus === 204 && background.persistence_configured) background = { ...background,
            persistence_state: backgroundSaveFails ? 'unsaved' : 'saved',
            requested_source: backgroundSaveFails ? 'session' : 'saved' };
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
    requestAnimationFrame() {}, setTimeout(fn, delay) {
      timers.push(fn); timerDelays.set(timers.length, delay); return timers.length;
    }, clearTimeout(id) { cancelledTimers.add(id); },
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
  assert.equal(pcmGets, 0);
  node('settings-surface').scrollTop = 125;
  controls[6].onclick({ detail: 1 });
  assert.equal(node('settings-surface').scrollTop, 0);
  assert.equal(controls[6].disabled, false);
  await new Promise(setImmediate);
  assert.equal(node('settings-surface').scrollTop, 0); // Late initial GET keeps the heading visible.
  assert.equal(node('policy-single').dataset.selected, 'true');
  assert.match(node('settings-persistence').textContent, /session only/);
  assert.equal(node('settings-policy-source').textContent, 'Source (requested/effective): Startup / Startup.');
  assert.equal(node('background-on').ariaDisabled, 'true');
  assert.equal(node('background-off').ariaDisabled, 'false');
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
  node('settings-surface').scrollTop = 200;
  controls[6].onclick({ detail: 1 });
  await new Promise(setImmediate);
  assert.equal(node('settings-surface').scrollTop, 0); // Reopening does not retain lower-section scroll.
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
  assert.equal(node('policy-single').ariaDisabled, 'true'); // No stale value before GET completes.
  assert(settingsIds.every((id) => node(id).disabled === false));
  assert.equal(node('settings-policy-status').textContent, 'Loading read policy…');
  assert.equal(node('settings-policy-source').textContent, 'Source (requested/effective): Unknown / Unknown.');
  now += 300;
  keys.keydown({ key: 'ArrowRight', preventDefault() {} });
  pendingPolicyGets.shift()({ ok: true, json: async () => ({ ...policy,
    requested: { ...policy.requested, mode: 'SINGLE' } }) });
  await new Promise(setImmediate);
  assert.equal(node('policy-repeat').dataset.focused, 'true'); // Unavailable choices remain reachable; late GET preserves focus.
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
  navigation('up');
  assert.equal(node('settings-close').dataset.focused, 'true');
  sockets[2].onmessage({ data: JSON.stringify({ action: 'select' }) });
  assert.equal(node('settings-panel').hidden, true); // CEC can reach and activate Back.
  deferPolicyGet = false;
  controls[6].onclick({ detail: 1 });
  await new Promise(setImmediate);
  const beforeSettingsNavigation = posts.length + policyPosts.length + backgroundPosts.length + pcmPosts.length;
  assert.equal(node('pcm-start').ariaDisabled, 'true');
  assert.equal(node('pcm-cancel').ariaDisabled, 'true');
  for (const input of ['semantic', 'keyboard']) {
    node('policy-single').focus();
    for (const [action, id] of [
      ['left', 'policy-single'], ['down', 'background-off'], ['right', 'background-on'],
      ['right', 'background-on'], ['down', 'pcm-cancel'],
      ['left', 'pcm-start'], ['left', 'pcm-start'], ['up', 'background-off'],
      ['up', 'policy-single'], ['right', 'policy-repeat'], ['down', 'background-on'],
      ['up', 'policy-repeat'], ['up', 'settings-close'], ['up', 'pcm-cancel'], ['down', 'settings-close'],
      ['left', 'settings-close'], ['right', 'settings-close'], ['down', 'policy-repeat'],
      ['left', 'policy-single'],
    ]) {
      now += 300;
      if (input === 'semantic') navigation(action);
      else keys.keydown({ key: { up: 'ArrowUp', down: 'ArrowDown', left: 'ArrowLeft', right: 'ArrowRight' }[action], preventDefault() {} });
      assert.equal(activeElement, node(id));
      assert.equal(node(id).dataset.focused, 'true');
      assert.equal(settingsIds.filter((item) => node(item).dataset.focused === 'true').length, 1);
    }
    for (const id of ['pcm-start', 'pcm-cancel']) {
      node(id).focus();
      now += 300;
      if (input === 'semantic') navigation('down');
      else keys.keydown({ key: 'ArrowDown', preventDefault() {} });
      assert.equal(activeElement, node('settings-close'));
      assert.equal(node('settings-close').dataset.focused, 'true');
    }
  }
  node('policy-single').focus();
  for (const reverse of [false, true]) {
    const order = reverse ? settingsIds.slice(1).reverse() : settingsIds.slice(1);
    for (const id of [...order, settingsIds[0]]) {
      keys.keydown({ key: 'Tab', shiftKey: reverse, preventDefault() {} });
      assert.equal(activeElement, node(id));
      assert.equal(node(id).dataset.focused, 'true');
    }
  }
  assert.equal(posts.length + policyPosts.length + backgroundPosts.length + pcmPosts.length, beforeSettingsNavigation);
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
  assert.equal(node('background-on').ariaDisabled, 'false');
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
  assert.equal(node('background-on').ariaDisabled, 'true');
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
    assert.equal(node('background-on').ariaDisabled, 'true');
    assert.equal(node('background-off').ariaDisabled, 'true');
    assert.match(node('settings-background-state').textContent, /Unknown \/ Unknown/);
    assert.equal(node('policy-repeat').ariaDisabled, 'false');
    background = valid;
  }
  background = { requested: false, effective: false, available: false, unavailable_reason: 'private-provider-url-or-key' };
  await reopenSettings();
  assert.equal(node('background-on').ariaDisabled, 'true');
  assert.equal(node('background-off').ariaDisabled, 'false');
  assert(!node('settings-background-status').textContent.includes(background.unavailable_reason));
  for (const reason of ['key_missing', 'metadata_disabled', '__proto__']) {
    background.unavailable_reason = reason;
    await reopenSettings();
    assert.equal(node('background-on').ariaDisabled, 'true');
    assert(!node('settings-background-status').textContent.includes('__proto__'));
  }
  backgroundGetStatus = 503;
  await reopenSettings();
  assert.equal(node('background-on').ariaDisabled, 'true');
  assert.equal(node('policy-repeat').ariaDisabled, 'false');
  assert.match(node('settings-background-state').textContent, /Unknown/);
  backgroundGetStatus = 200;
  policyGetStatus = 503;
  background = { requested: false, effective: false, available: true, unavailable_reason: null };
  await reopenSettings();
  assert.equal(node('background-on').ariaDisabled, 'false');
  assert.equal(node('policy-single').ariaDisabled, 'true');
  policyGetStatus = 200;
  // A late GET from an earlier opening must not replace the current state.
  deferBackgroundGet = true;
  await reopenSettings();
  assert.equal(node('background-on').ariaDisabled, 'true');
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
  assert.equal(node('background-on').ariaDisabled, 'false');
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
  // One semantic-only Settings journey. This does not simulate a physical CEC bus.
  policyPostStatus = 204;
  background = { requested: false, effective: false, available: true, unavailable_reason: null,
    persistence_configured: true, persistence_state: 'saved', requested_source: 'restored' };
  const semantic = async (action) => {
    now += 300;
    navigation(action);
    await new Promise(setImmediate);
    assert(settingsIds.every((id) => node(id).disabled === false));
  };
  const selectSetting = async (id) => {
    const enabledCount = settingsIds.filter((id) => !node(id).disabled).length;
    assert.equal(enabledCount, 7);
    const target = settingsIds.indexOf(id);
    const targetRow = target === 6 ? -1 : Math.floor(target / 2);
    for (let step = 0; step < 4; step++) {
      const current = settingsIds.findIndex((item) => node(item).dataset.focused === 'true');
      const row = current === 6 ? -1 : Math.floor(current / 2);
      if (row === targetRow) break;
      await semantic(row < targetRow ? 'down' : 'up');
    }
    if (target < 6) await semantic(target % 2 ? 'right' : 'left');
    assert.equal(node(id).dataset.focused, 'true');
    await semantic('select');
  };
  assert.equal(controls[6].dataset.focused, 'true');
  await semantic('select');
  assert.equal(node('settings-panel').hidden, false);
  assert.match(node('settings-background-save-state').textContent, /saved.*Restored/);
  await selectSetting('background-on');
  assert.equal(backgroundPosts.at(-1).enabled, true);
  assert.equal(node('settings-background-state').textContent, 'Requested / effective: On / On.');
  backgroundSaveFails = true;
  await selectSetting('background-off');
  assert.equal(backgroundPosts.at(-1).enabled, false);
  assert.equal(node('settings-background-state').textContent, 'Requested / effective: Off / Off.');
  assert.match(node('settings-background-save-state').textContent, /Off is applied.*NOT SAVED.*Restart/);
  backgroundSaveFails = false;
  await selectSetting('policy-single');
  assert.match(node('settings-background-save-state').textContent, /saved for daemon restarts/);
  await selectSetting('settings-close');
  assert.equal(node('settings-panel').hidden, true);
  assert.equal(controls[6].dataset.focused, 'true');
  // Explicit local acquisition: acceptance is never treated as completion or reader release.
  // Unavailable acquisition buttons remain reachable and explain rejection without POSTing.
  pcmGetStatus = 200;
  pcm = { ...pcm, can_start: false, start_rejection: 'offset_unknown' };
  let startScrolls = 0;
  node('pcm-start').scrollIntoView = (options) => {
    assert.equal(options.block, 'nearest');
    startScrolls += 1;
  };
  let statusScrolls = 0;
  node('settings-pcm-status').scrollIntoView = (options) => {
    assert.equal(options.block, 'nearest');
    statusScrolls += 1;
  };
  deferPolicyGet = true;
  await reopenSettings();
  node('pcm-start').focus();
  deferPolicyGet = false;
  assert.equal(pendingPolicyGets.length, 1);
  pendingPolicyGets.shift()({ ok: true, json: async () => ({ ...policy }) });
  await new Promise(setImmediate);
  assert.equal(activeElement, node('pcm-start'));
  assert.equal(node('pcm-start').dataset.focused, 'true'); // Late policy GET preserves native button focus.
  const beforeStatusCommands = posts.length + policyPosts.length + backgroundPosts.length + pcmPosts.length;
  assert.equal(node('pcm-start').ariaDisabled, 'true');
  assert.equal(node('pcm-cancel').ariaDisabled, 'true');
  await selectSetting('pcm-start');
  assert(startScrolls > 0);
  assert(statusScrolls > 0);
  assert.match(node('settings-pcm-status').textContent, /offset_unknown/);
  assert.match(node('settings-pcm-feedback').textContent, /offset_unknown/);
  assert.equal(node('settings-pcm-feedback').hidden, false);
  assert.equal(posts.length + policyPosts.length + backgroundPosts.length + pcmPosts.length, beforeStatusCommands);
  assert.equal(node('settings-panel').hidden, false);
  await semantic('right');
  assert.equal(node('pcm-cancel').dataset.focused, 'true');
  await semantic('select');
  assert.match(node('settings-pcm-feedback').textContent, /No active acquisition to cancel/);
  assert.equal(posts.length + policyPosts.length + backgroundPosts.length + pcmPosts.length, beforeStatusCommands);
  assert.doesNotMatch(fs.readFileSync(path.join(__dirname, '../ui/default/player.html'), 'utf8'),
    /id="settings-pcm-status"[^>]*tabindex=/);
  assert.notEqual(node('settings-pcm-status').dataset.focused, 'true');
  assert.notEqual(node('settings-pcm-region').dataset.focused, 'true');
  await selectSetting('settings-close');
  pcm = { ...pcm, can_start: true, start_rejection: null };
  const pollPcm = async () => {
    const id = [...timerDelays.keys()].reverse().find((id) => timerDelays.get(id) === 1000 && !cancelledTimers.has(id));
    assert(id, 'one active read-only PCM poll');
    cancelledTimers.add(id);
    timers[id - 1]();
    await new Promise(setImmediate);
  };
  pcmGetStatus = 200;
  await reopenSettings();
  assert.equal(node('pcm-start').ariaDisabled, 'false');
  assert.equal(pcmPosts.length, 0); // Open and status reads do not acquire anything.
  deferPcmGet = true;
  await selectSetting('pcm-start');
  node('pcm-start').onclick({ detail: 1 });
  assert.deepEqual(pcmPosts, [{ action: 'start', session_id: 'pcm-session', disc_generation: 2,
    duration_ms: 1800000, profile: 'local-pcm-sha256-v1' }]);
  assert.equal(node('pcm-start').ariaDisabled, 'true');
  assert.equal(node('settings-pcm-result').hidden, true);
  assert.match(node('settings-pcm-feedback').textContent, /accepted.*waiting/);
  pcm = { ...pcm, state: 'COLLECTING', busy: true, reader_released: false, job_id: 7,
    can_start: false, start_rejection: 'capture_releasing' };
  pendingPcmGets.shift()({ ok: true, json: async () => ({ ...pcm }) });
  await new Promise(setImmediate);
  deferPcmGet = false;
  assert.equal(node('pcm-cancel').ariaDisabled, 'false');
  pcm = { ...pcm, state: 'INCOMPLETE', failure: 'CANCELLED' };
  await selectSetting('pcm-cancel');
  assert.deepEqual(pcmPosts.at(-1), { action: 'cancel', session_id: 'pcm-session', job_id: 7 });
  assert.match(node('settings-pcm-status').textContent, /INCOMPLETE.*Waiting for reader release/);
  assert.match(node('settings-pcm-feedback').textContent, /Cancellation accepted.*waiting/);
  assert.equal(node('settings-pcm-result').hidden, true);
  pcm = { ...pcm, busy: false, reader_released: true, can_start: true, start_rejection: null };
  await pollPcm();
  assert.equal(node('pcm-cancel').ariaDisabled, 'true');
  assert.match(node('settings-pcm-status').textContent, /Incomplete acquisition/);
  assert(!node('settings-pcm-status').textContent.includes('Waiting for reader release'));
  assert.match(node('settings-pcm-feedback').textContent, /confirmed.*reader released/);
  pcmPostStatus = 409;
  await selectSetting('pcm-start');
  const rejectedCount = pcmPosts.length;
  await pollPcm();
  assert.equal(pcmPosts.length, rejectedCount); // Polling never retries the rejected POST.
  assert.match(node('settings-pcm-feedback').textContent, /rejected \(409\).*manually/);
  pcm = { ...pcm, state: 'COMPLETE', failure: 'NONE', can_start: false };
  await pollPcm();
  assert.equal(node('settings-pcm-result').hidden, false);
  assert.match(node('settings-pcm-result').textContent, /\/api\/complete-pcm\/result.*NOT_CHECKED/);
  // Old/no-provider daemons and malformed state affect only acquisition.
  const validPcm = pcm;
  for (const status of [404, 503, 200]) {
    pcmGetStatus = status;
    pcm = {};
    await reopenSettings();
    assert.equal(node('pcm-start').ariaDisabled, 'true');
    assert.equal(node('pcm-cancel').ariaDisabled, 'true');
    assert.equal(node('policy-single').ariaDisabled, 'false');
    assert.equal(node('background-off').ariaDisabled, 'false');
    assert.match(node('settings-pcm-status').textContent, /unavailable/);
  }
  // Close aborts the in-flight GET and removes its poll. Ignored aborts cannot mutate a new dialog.
  deferPcmGet = true;
  await reopenSettings();
  const oldSignal = pcmSignals.at(-1);
  navigation('back');
  assert.equal(oldSignal.aborted, true);
  assert.equal([...timerDelays.keys()].filter((id) => timerDelays.get(id) === 1000 && !cancelledTimers.has(id)).length, 0);
  deferPcmGet = false;
  await reopenSettings();
  pendingPcmGets.shift()({ ok: true, json: async () => validPcm });
  await new Promise(setImmediate);
  assert.equal(node('settings-pcm-result').hidden, true);
  assert.equal(node('pcm-start').ariaDisabled, 'true');
  // A stale POST reply must not change a reopened dialog or duplicate its request.
  pcm = { ...validPcm, state: 'IDLE', can_start: true, job_id: 0 };
  await reopenSettings();
  deferPcmPost = true;
  node('pcm-start').onclick({ detail: 1 });
  const oldPostSignal = pcmSignals.at(-1);
  await reopenSettings();
  assert.equal(oldPostSignal.aborted, true);
  const pendingCount = pcmPosts.length;
  node('pcm-start').onclick({ detail: 1 });
  assert.equal(pcmPosts.length, pendingCount);
  pendingPcmPosts.shift()({ status: 202 });
  await new Promise(setImmediate);
  assert.equal(node('settings-pcm-feedback').textContent, '—');
  deferPcmPost = false;
  await pollPcm();
  assert.equal(node('pcm-start').ariaDisabled, 'false');
  navigation('back');
  assert.equal(pcmPosts.length, pendingCount); // Closing never cancels or starts a job.
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
