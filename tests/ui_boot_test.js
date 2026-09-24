'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../ui/default/player.js'), 'utf8');

async function scenario({ failRest = false, failTelemetry = false, paintFirst = false } = {}) {
  const events = [];
  const frames = [];
  const timers = [];
  const sockets = [];
  const listeners = {};
  const nodes = new Map();
  let clock = 10;
  const context = {
    performance: { now: () => clock++ },
    document: {
      readyState: 'interactive',
      addEventListener: (name, fn) => { listeners[name] = fn; },
      getElementById: (id) => {
        if (!nodes.has(id)) nodes.set(id, {
          textContent: '', style: {}, dataset: {},
          classList: { add() {}, remove() {} }, removeAttribute() {},
        });
        return nodes.get(id);
      },
    },
    location: { protocol: 'http:', host: 'localhost:8080' },
    fetch: (url, options) => {
      if (url === '/api/ui-boot') {
        events.push(JSON.parse(options.body));
        if (failTelemetry === 'throw') throw new Error('optional');
        if (failTelemetry) return Promise.reject(new Error('optional'));
        return Promise.resolve({ ok: false, status: 429 });
      }
      if (failRest) return Promise.reject(new Error('offline'));
      return Promise.resolve({ ok: true, json: async () => ({ player: { state: 'NO_DISC' } }) });
    },
    WebSocket: class { constructor() { sockets.push(this); } close() { this.onclose(); } },
    requestAnimationFrame: (fn) => frames.push(fn),
    setTimeout: (fn) => { timers.push(fn); return timers.length; },
    clearTimeout() {},
  };
  vm.runInNewContext(source, context);
  const flush = async () => { for (let i = 0; i < 12; i++) await Promise.resolve(); };
  const paint = () => { while (frames.length) frames.shift()(); };
  const names = () => events.map((e) => e.event);
  await flush();
  assert.equal(sockets.length, 1);
  if (paintFirst) paint();
  assert(!names().includes('ui_ready'));
  sockets[0].onopen();
  if (failRest) {
    paint();
    assert(!names().includes('first_render'));
    sockets[0].onmessage({ data: '{}' });
  }
  paint();
  listeners.DOMContentLoaded(); // Deliberately after rendering / WS connection.
  assert.deepEqual([...names()].sort(), [
    'ui_script_start', 'dom_content_loaded', 'websocket_connected', 'first_render', 'ui_ready',
  ].sort());
  assert.equal(nodes.get('player-state').textContent, 'NO_DISC');
  assert(events.every((e) => Number.isFinite(e.client_ms)));
  assert.equal(new Set(events.map((e) => e.page_id)).size, 1);
  sockets[0].onclose();
  await timers.shift()();
  await flush();
  sockets[1].onopen();
  sockets[1].onmessage({ data: '{}' });
  paint();
  assert.equal(events.length, 5); // Reconnect does not repeat boot observations.
  return events[0].page_id;
}
(async () => {
  const first = await scenario({ paintFirst: true });
  const second = await scenario({ failRest: true, failTelemetry: true });
  assert.notEqual(first, second);
  await scenario({ failTelemetry: 'throw' });
  console.log('PASS: UI boot ordering, reload, REST recovery, reconnect and telemetry failures');
})().catch((error) => { console.error(error); process.exitCode = 1; });
