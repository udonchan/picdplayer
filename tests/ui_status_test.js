'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const nodes = new Map();
function node(id) {
  if (!nodes.has(id)) nodes.set(id, {
    textContent: '', children: [],
    replaceChildren() { this.children = []; },
    append(child) { this.children.push(child); },
  });
  return nodes.get(id);
}
const context = vm.createContext({
  document: { getElementById: node, createElement: () => ({}) },
  fetch: () => new Promise(() => {}),
});
vm.runInContext(fs.readFileSync(path.join(__dirname, '../ui/default/status.js'), 'utf8'), context);
const snapshot = {
  player: { state: 'PLAYING', track_number: 2, position_frames: 150 },
  disc: { state: 'AUDIO_READY', title: 'Album', artist: 'Artist' },
  tracks: [{ number: 1 }, { number: 2 }], enrichment: { status: 'AVAILABLE' },
  drive: { vendor: 'Drive', c2_supported: { value: 'UNKNOWN' } },
  read: { activity: 'reading', stats: { direct_retries: 3 } },
  recent_events: [{ sequence: 9, read_status: 'uncertain', region: { start_lba: 0, end_lba: 75 } }],
};
context.render(snapshot);
assert.equal(node('track').textContent, 2);
assert.equal(node('position').textContent, '0:02');
assert.equal(node('album').textContent, 'Album');
assert.equal(node('disc-tracks').textContent, 2);
assert.equal(node('metadata-state').textContent, 'AVAILABLE');
assert.equal(node('read-activity').textContent, 'reading');
assert.match(node('events').children[0].textContent, /#9 uncertain/);
context.render({ ...snapshot, disc: { state: 'NO_DISC' }, tracks: [] });
assert.equal(node('integrity').textContent, 'NO DISC');
assert.equal(node('cap-c2').textContent, 'UNKNOWN');
assert.equal(node('drive-name').textContent, 'Drive');
console.log('PASS: technical status uses presentation fields and diagnostics');

const warning = { status: 'UNCERTAIN', start_lba: 10, frames_read: 15 };
function diagnostic(revision, stream, active, first, last) {
  return { ...snapshot, revision, read: { ...snapshot.read, session_id: 'a', stream_generation: stream,
    active_warning: active, event_window: { first_sequence: first, last_sequence: last, worker_dropped: 0 } } };
}
context.render(diagnostic(10, 1, warning, 1, 1));
assert(node('events').children.some(x => x.textContent.startsWith('Stream warning:')));
context.render(diagnostic(9, 1, null, 1, 1)); // stale snapshot must not clear the warning
assert(node('events').children.some(x => x.textContent.startsWith('Stream warning:')));
context.render(diagnostic(11, 1, warning, 5, 8));
assert(node('events').children.some(x => x.textContent.startsWith('UNKNOWN:')));
context.render(diagnostic(12, 2, null, 1, 1));
assert(!node('events').children.some(x => x.textContent.startsWith('Stream warning:')));
assert(!node('events').children.some(x => x.textContent.startsWith('UNKNOWN:')));
context.render({ ...diagnostic(1, 1, null, 7, 9), read: { ...diagnostic(1, 1, null, 7, 9).read, session_id: 'b' } });
assert(node('events').children.some(x => x.textContent.startsWith('UNKNOWN:')));
console.log('PASS: snapshot warning replacement, stale revision, gap, stream and session changes');

// Exercise the actual reconnect callbacks, not only the render entry point.
// renderの単体呼出しだけでなく、REST→WS→切断→復元の順序を確認する。
(async () => {
  const sockets = [];
  const timers = [];
  let nextSnapshot = diagnostic(20, 1, warning, 1, 4);
  class Socket {
    constructor() { sockets.push(this); }
    close() { this.onclose(); }
  }
  const live = vm.createContext({
    document: { getElementById: node, createElement: () => ({}) },
    location: { protocol: 'http:', host: 'localhost' },
    WebSocket: Socket,
    fetch: async () => ({ ok: true, json: async () => nextSnapshot }),
    setTimeout: fn => { timers.push(fn); return timers.length; },
    clearTimeout: () => {},
  });
  vm.runInContext(fs.readFileSync(path.join(__dirname, '../ui/default/status.js'), 'utf8'), live);
  const settle = () => new Promise(resolve => setImmediate(resolve));
  await settle();
  assert.equal(sockets.length, 1);
  sockets[0].onopen();
  assert(node('events').children.some(x => x.textContent.startsWith('Stream warning:')));
  sockets[0].close();
  nextSnapshot = diagnostic(21, 1, warning, 1, 5);
  nextSnapshot.read.event_window.worker_dropped = 2;
  await timers.shift()();
  assert.equal(sockets.length, 2);
  assert(node('events').children.some(x => x.textContent.startsWith('UNKNOWN:')));
  assert(node('events').children.some(x => x.textContent.startsWith('Stream warning:')));
  sockets[1].onmessage({ data: JSON.stringify(diagnostic(19, 1, null, 1, 2)) });
  assert(node('events').children.some(x => x.textContent.startsWith('Stream warning:')));
  sockets[1].close();
  nextSnapshot = diagnostic(1, 1, null, null, null);
  nextSnapshot.read.session_id = 'restarted';
  await timers.shift()();
  assert.equal(sockets.length, 3);
  assert(!node('events').children.some(x => x.textContent.startsWith('Stream warning:')));
  sockets[1].onmessage({ data: JSON.stringify(diagnostic(999, 1, warning, 1, 5)) });
  assert(!node('events').children.some(x => x.textContent.startsWith('Stream warning:')));
  sockets[1].onclose();
  assert.equal(timers.length, 0);
  sockets[2].onmessage({ data: '{invalid' });
  assert.equal(node('connection').textContent, 'Invalid snapshot');
  console.log('PASS: reconnect restores warning, drop and daemon session');
})().catch(error => { console.error(error); process.exitCode = 1; });

if (process.argv[2]) {
  const output = require('node:child_process').execFileSync(process.argv[2], { encoding: 'utf8', timeout: 10000 });
  const line = output.split('\n').find(value => value.startsWith('DIAGNOSTIC_JSON='));
  assert(line, 'integration worker must publish its actual diagnostic snapshot');
  const actual = JSON.parse(line.slice('DIAGNOSTIC_JSON='.length));
  assert(actual.read.event_window.worker_dropped > 0);
  assert.equal(actual.read.active_warning.status, 'UNCERTAIN');
  context.render(actual);
  assert(node('events').children.some(x => x.textContent.startsWith('UNKNOWN:')));
  assert(node('events').children.some(x => x.textContent.startsWith('Stream warning:')));
  const historyLine = output.split('\n').find(value => value.startsWith('HISTORY_JSON='));
  assert(historyLine);
  const detail = JSON.parse(historyLine.slice('HISTORY_JSON='.length));
  const manual = fs.readFileSync(path.join(__dirname, '../docs/manual/custom-ui.md'), 'utf8');
  const example = manual.match(/```javascript\n(function matchingReadHistory[\s\S]*?)\n```/);
  assert(example, 'documented history matching example must exist');
  const recipe = vm.createContext({});
  vm.runInContext(example[1], recipe);
  assert.equal(recipe.matchingReadHistory(actual, detail), detail.history);
  assert.equal(recipe.matchingReadHistory(actual, { ...detail, session_id: 'retired' }), null);
  assert.equal(recipe.matchingReadHistory(actual, { ...detail, stream_generation: detail.stream_generation + 1 }), null);
  assert.equal(recipe.matchingReadHistory({ schema_version: 1, read: {} }, detail), null);
  assert.equal(recipe.matchingReadHistory(actual, { ...detail, history: { ...detail.history, included: false } }), null);
  assert.equal(recipe.matchingReadHistory(actual, { ...detail, schema_version: 2 }), null);
  console.log('PASS: worker overflow to UI and documented history generation matching');
}
