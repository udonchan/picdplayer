/*
 * PiCDPlayer default now-playing UI.
 * 日本語: daemon が状態の唯一の所有者です。この画面は API snapshot を表示するだけです。
 * English: The daemon is the sole state owner. This page only renders API snapshots.
 */

'use strict';

const scriptStartMs = performance.now();
// Optional, best-effort observations. Never await telemetry or retry failures.
// 任意の計測です。送信完了を待たず、失敗しても UI の処理を続けます。
const boot = (() => {
  const pageId = `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;
  const sent = new Set();
  let connected = false;
  let painted = false;
  let scheduled = false;
  function mark(event, clientMs = performance.now()) {
    if (sent.has(event)) return;
    sent.add(event);
    try {
      Promise.resolve(fetch('/api/ui-boot', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ page_id: pageId, event, client_ms: clientMs }),
      })).catch(() => {});
    } catch {
      // Telemetry is optional even if fetch throws synchronously.
      // fetch が同期的に失敗しても表示には影響させません。
    }
  }
  function ready() {
    if (connected && painted) mark('ui_ready');
  }
  mark('ui_script_start', scriptStartMs);
  if (document.readyState === 'complete') {
    // A late script cannot observe the original DOMContentLoaded event.
    // 遅れて実行された場合は元のイベント時刻を作りません。
  } else {
    document.addEventListener('DOMContentLoaded', () => mark('dom_content_loaded'), { once: true });
  }
  return {
    connection(value) {
      connected = value;
      if (value) mark('websocket_connected');
      ready();
    },
    snapshot() {
      if (scheduled) return;
      scheduled = true;
      // Two animation frames allow a paint opportunity, not HDMI scanout proof.
      // 2 回の rAF は描画機会の近似であり、HDMI 表示完了の保証ではありません。
      requestAnimationFrame(() => requestAnimationFrame(() => {
        painted = true;
        mark('first_render');
        ready();
      }));
    },
  };
})();

const byId = (id) => document.getElementById(id);
const set = (id, value) => {
  const element = byId(id);
  const text = value || '—';
  if (element.textContent !== text) element.textContent = text;
};

// CD frame is 1/75 second. Keep this conversion in the UI presentation layer.
// CD frame は 1/75 秒です。この変換は表示層だけで行います。
const formatTime = (frames) => {
  if (!Number.isInteger(frames) || frames < 0) return '0:00';
  const minutes = Math.floor(frames / 4500);
  const seconds = String(Math.floor(frames / 75) % 60).padStart(2, '0');
  return `${minutes}:${seconds}`;
};

function setConnection(value) {
  set('connection', value);
}

function currentTrack(tracks, number) {
  return tracks?.find((track) => track.number === number) || null;
}

// Cover art is optional enrichment. A failed image must not hide the album data.
// ジャケットは任意の付加情報です。画像読込失敗でアルバム情報を消しません。
function showArt(artwork, layout, session) {
  const container = byId('art');
  const image = byId('cover');
  const url = typeof artwork?.url === 'string'
    ? artwork.url
    : '';

  const identity = url ? JSON.stringify([url, layout?.session_id || session || null,
    layout?.disc_generation ?? null]) : '';

  // Compare image identity before touching attributes or event handlers.
  // 同じ画像なら属性・handler を書き換えず、失敗時も毎回再試行しません。
  if ((image.dataset.identity || '') === identity) return;
  // Same endpoint can now serve a different disc after reconnect.
  // 同URLでもdisc/session変更時は旧画像を破棄して再取得する。
  if (url && image.dataset.url === url) image.removeAttribute('src');
  image.dataset.identity = identity;
  image.dataset.url = url;
  if (container.classList.contains('has-cover')) container.classList.remove('has-cover');

  if (!url) {
    image.removeAttribute('src');
    image.onload = null;
    image.onerror = null;
    return;
  }

  image.onload = () => {
    // Ignore completion from a replaced image and avoid repeated class writes.
    // 差し替え前の画像完了を無視し、同じ class を繰り返し設定しません。
    if (image.dataset.identity === identity && image.complete && image.naturalWidth > 0 &&
        !container.classList.contains('has-cover')) container.classList.add('has-cover');
  };
  image.onerror = () => {
    if (image.dataset.identity === identity && container.classList.contains('has-cover')) {
      container.classList.remove('has-cover');
    }
  };
  image.src = url;
  if (image.complete && image.naturalWidth > 0) image.onload();
}

function mediaMessage(hasDisc, mediaState, enrichmentStatus) {
  if (!hasDisc) return mediaState === 'LOADING' ? 'READING DISC' : 'WAITING FOR DISC';
  if (enrichmentStatus === 'LOADING') return 'LOOKING UP ALBUM';
  if (enrichmentStatus === 'UNAVAILABLE') return 'ALBUM SELECTION REQUIRED';
  if (enrichmentStatus === 'ERROR') return 'METADATA UNAVAILABLE';
  return 'NOW PLAYING';
}

// Cache only the last CSS value, never an extrapolated playback position.
// CSS の直前の表示値だけを保持し、再生位置を browser 側で進めません。
let progressScale;

// Rendering only: never keep an independent player state in the browser.
// 描画専用です。browser 側に独立した再生状態を持ちません。
function render(snapshot) {
  const player = snapshot.player || {};
  const disc = snapshot.disc || {};
  const enrichment = snapshot.enrichment || {};
  const track = currentTrack(snapshot.tracks, player.track_number);
  const hasDisc = disc.state === 'AUDIO_READY';
  const fallbackTrack = Number.isInteger(player.track_number)
    ? `Track ${String(player.track_number).padStart(2, '0')}`
    : '—';

  set('media-message', mediaMessage(hasDisc, disc.state, enrichment.status));
  set('album', hasDisc ? (disc.title || 'Audio CD') : 'No disc');
  set('album-artist', hasDisc
    ? (disc.artist || (enrichment.status === 'LOADING'
      ? 'Looking up album information'
      : 'Unknown artist'))
    : 'Insert an audio CD');
  set('track-number', Number.isInteger(player.track_number)
    ? String(player.track_number).padStart(2, '0')
    : '—');
  set('track-title', hasDisc ? (track?.title || fallbackTrack) : '—');
  set('track-artist', hasDisc ? (track?.artist || disc.artist || '') : '');

  const position = player.position_frames;
  const length = player.track_duration_frames ?? track?.duration_frames;
  set('position', formatTime(position));
  set('duration', formatTime(length));
  const fraction = Number.isInteger(position) && Number.isInteger(length) && length > 0
    ? Math.min(100, 100 * position / length)
    : 0;
  const scale = `scaleX(${fraction / 100})`;
  if (scale !== progressScale) {
    byId('progress').style.transform = scale;
    progressScale = scale;
  }
  set('player-state', player.state || 'NO_DISC');
  showArt(hasDisc ? snapshot.artwork?.cover : null, disc.layout, snapshot.read?.session_id);
  boot.snapshot();
}

// Fetch an initial REST snapshot before opening the live event stream.
// WebSocket 接続前に REST から初期 snapshot を取得します。
async function load() {
  try {
    const response = await fetch('/api/state', { cache: 'no-store' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    render(await response.json());
  } catch {
    setConnection('Offline');
  }
}

let retry;

// CEC/API events update the daemon state; reconnect after daemon or network restarts.
// CEC/API の状態更新は daemon が行います。daemon や通信の再起動後は再接続します。
function connect() {
  clearTimeout(retry);
  setConnection('Connecting');
  const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
  const socket = new WebSocket(`${scheme}://${location.host}/api/events`);

  socket.onopen = () => {
    setConnection('Live');
    boot.connection(true);
  };
  socket.onmessage = (event) => {
    try {
      render(JSON.parse(event.data));
    } catch {
      setConnection('Invalid state');
    }
  };
  socket.onerror = () => socket.close();
  socket.onclose = () => {
    boot.connection(false);
    setConnection('Reconnecting');
    retry = setTimeout(async () => {
      await load();
      connect();
    }, 1500);
  };
}

load().finally(connect);
