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
  const postBodies = [];
  let postStatus = 204;
  let failPost = false;
  let now = 1000;
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
      focus() { this.focused = true; }, blur() { this.focused = false; },
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
  const controls = ['previous', 'play', 'pause', 'stop', 'next', 'metadata'].map((command) => {
    const button = node(`button-${command}`);
    button.dataset.command = command;
    return button;
  });
  vm.runInNewContext(source, {
    Date: class extends Date { static now() { return now; } },
    performance: { now: () => 1 },
    document: {
      readyState: 'complete', getElementById: node,
      createElement: element,
      querySelectorAll: () => controls,
      addEventListener(type, fn) { keys[type] = fn; },
    },
    location: { protocol: 'http:', host: 'localhost' },
    fetch: async (url, options) => {
      if (url === '/api/state') return { ok: true, json: async () => snapshot };
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
  const navigation = (action) => sockets[1].onmessage({ data: JSON.stringify({ action }) });
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
  assert(controls.every((button) => button.disabled));
  snapshot.revision = 4;
  snapshot.disc.state = 'AUDIO_READY';
  snapshot.player.state = 'UNKNOWN';
  sockets[0].onmessage({ data: JSON.stringify(snapshot) });
  assert(controls.every((button) => button.disabled));
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
  pickerNavigation('back');
  assert.equal(node('metadata-picker').hidden, true);
  snapshot.revision += 1;
  sockets[0].onmessage({ data: JSON.stringify(snapshot) });
  assert.equal(node('metadata-picker').hidden, true); // Back sticks for the same candidate set.
  pickerNavigation('left'); // From no focus, CEC reaches the last enabled control.
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
