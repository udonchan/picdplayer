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
