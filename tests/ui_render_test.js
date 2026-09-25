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
  vm.runInNewContext(source, {
    performance: { now: () => 1 },
    document: { readyState: 'complete', getElementById: node, addEventListener() {} },
    location: { protocol: 'http:', host: 'localhost' },
    fetch: async (url) => ({ ok: true, json: async () => snapshot }),
    WebSocket: class { constructor() { sockets.push(this); } },
    requestAnimationFrame: (fn) => frames.push(fn),
    setTimeout() {}, clearTimeout() {},
  });
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
  assert.equal(playingWrites, 72); // 60 widths + 12 displayed seconds, no lost positions.
  snapshot.player.state = 'STOPPED';
  snapshot.player.position_frames = 0;
  receive(snapshot);
  assert.equal(repeatedWrites, 0);
  assert.deepEqual(receive({ ...snapshot, revision: 2, diagnostic: { counter: 99 } }), []);
  snapshot.player.position_frames = 15;
  assert.deepEqual(receive(snapshot), ['progress.style.width']);
  assert.equal(node('position').textContent, '0:00');
  snapshot.player.position_frames = 75;
  assert.deepEqual(receive(snapshot).sort(), ['position.textContent', 'progress.style.width']);
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
  snapshot.artwork.cover = null;
  receive(snapshot);
  assert(!node('art').classList.contains('has-cover'));
  assert.deepEqual(receive(snapshot), []);
  snapshot.player = { state: 'NO_DISC' };
  snapshot.disc = { state: 'NO_DISC' };
  receive(snapshot);
  assert.equal(node('album').textContent, 'No disc');
  assert.equal(node('position').textContent, '0:00');
  assert.equal(node('progress').style.width, '0%');
  console.log('PASS: identical values, position, metadata, artwork lifecycle and disc removal');
}
main().catch((error) => { console.error(error); process.exitCode = 1; });
