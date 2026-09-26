/*
 * PiCDPlayer default technical-status UI.
 * 日本語: daemon の snapshot を読み取り専用で可視化します。操作状態は保持しません。
 * English: Read-only rendering of daemon snapshots; this page owns no player state.
 */

'use strict';

const byId = (id) => document.getElementById(id);
const set = (id, value) => {
  byId(id).textContent = value ?? '—';
};

const formatFrames = (value) => {
  if (!Number.isInteger(value)) return '—';
  const minutes = Math.floor(value / 75 / 60);
  const seconds = String(Math.floor(value / 75) % 60).padStart(2, '0');
  return `${minutes}:${seconds}`;
};

function formatEvidence(value) {
  if (!value) return 'Not available';
  const verification = value.verification || {};
  const detail = verification.attempts
    ? ` · ${verification.matching_reads}/${verification.attempts} match/read`
      + (verification.mismatches ? ` · ${verification.mismatches} mismatch` : '')
      + (verification.time_budget_exhausted ? ' · budget exhausted' : '')
    : '';
  return `${value.status} · ${value.local_verification}${detail}`
    + ` · LBA ${value.start_lba}–${value.start_lba + value.frames_read}`;
}

function formatCapability(value) {
  if (!value) return 'UNKNOWN';
  return `${value.value}${value.source && value.source !== 'NONE' ? ` · ${value.source}` : ''}`;
}

function setConnection(label, kind) {
  const node = byId('connection');
  node.textContent = label;
  node.className = `pill ${kind}`;
}

// This view intentionally issues no control commands.
// この画面は意図的に操作コマンドを送信しません。
// Snapshot is authoritative; events never reconstruct active warnings.
// 警告はsnapshotから置換し、古いイベントから再生成しない。
let diagnosticSession = null;
let diagnosticRevision = -1;
let diagnosticStream = null;
let diagnosticSequence = null;
let diagnosticDropped = 0;
let diagnosticGap = false;

function render(state) {
  const session = state.read?.session_id;
  const revision = state.revision;
  if (session && session === diagnosticSession && Number.isInteger(revision)
      && revision <= diagnosticRevision) return;
  if (session !== diagnosticSession || state.read?.stream_generation !== diagnosticStream) {
    diagnosticSequence = null;
    diagnosticDropped = 0;
    diagnosticGap = false;
    diagnosticRevision = -1;
  }
  diagnosticSession = session;
  diagnosticStream = state.read?.stream_generation;
  if (Number.isInteger(revision)) diagnosticRevision = revision;
  const window = state.read?.event_window;
  if (window) {
    if ((diagnosticSequence === null && window.first_sequence > 1)
        || (diagnosticSequence !== null && window.first_sequence > diagnosticSequence + 1)
        || window.worker_dropped > diagnosticDropped) diagnosticGap = true;
    if (window.last_sequence !== null) diagnosticSequence = window.last_sequence;
    diagnosticDropped = window.worker_dropped;
  }
  const player = state.player || {};
  const read = state.read || {};
  const drive = state.drive || {};
  const media = state.disc || {};
  const policy = read.policy || {};
  const stats = read.stats || {};
  const enrichment = state.enrichment || {};

  set('player-state', player.state);
  set('track', player.track_number);
  set('position', formatFrames(player.position_frames));
  set('integrity', media.state === 'NO_DISC' ? 'NO DISC' : formatEvidence(read.current_playback));
  set('read-activity', read.activity);
  set('read-strategy', read.effective_strategy);

  const requestedPolicy = policy.requested?.mode;
  const effectivePolicy = policy.effective?.mode;
  set('read-policy', policy.pending
    ? `${effectivePolicy || '—'} → ${requestedPolicy || '—'} (pending)`
    : (effectivePolicy || requestedPolicy));

  const capacity = Number.isSafeInteger(read.buffer_capacity_frames) && read.buffer_capacity_frames >= 0
      && Number.isSafeInteger(read.read_block_frames) && read.read_block_frames > 0
    ? Math.floor(read.buffer_capacity_frames / read.read_block_frames)
    : '—';
  set('queued-blocks', `${read.queued_blocks ?? '—'} / ${capacity}`
    + ` (start ${read.startup_buffer_frames ?? '—'} frames)`);
  set('latest-read', formatEvidence(read.latest));
  set('prebuffer-wait', read.last_prebuffer_wait_ms == null
    ? 'Pending'
    : `${read.last_prebuffer_wait_ms} ms (${read.prebuffer_target_frames ?? '—'} frames)`);
  set('read-stats', `${stats.read_calls ?? '—'} / ${stats.frames_accepted ?? '—'} frames`
    + ` · verified ${stats.verified_calls ?? '—'}`);
  set('read-errors', `${stats.direct_retries ?? '—'} / ${stats.failed_calls ?? '—'}`);
  set('dropped-events', read.dropped_events);

  set('drive-name', [drive.vendor, drive.model].filter(Boolean).join(' ')
    || drive.device || 'Unknown drive');
  set('drive-firmware', drive.firmware);
  set('cap-dae', formatCapability(drive.digital_audio_extraction));
  set('cap-c2', formatCapability(drive.c2_supported));
  set('cap-c2-trust', formatCapability(drive.c2_trustworthy));
  set('cap-cache', formatCapability(drive.read_cache));
  set('cap-stream', formatCapability(drive.accurate_stream));
  set('cap-speed', formatCapability(drive.speed_control));
  set('drive-offset', drive.read_offset_samples == null
    ? 'UNKNOWN'
    : `${drive.read_offset_samples} samples`);

  const probe = byId('probe-error');
  probe.hidden = !drive.probe_error;
  probe.textContent = drive.probe_error || '';

  set('media-state', media.state);
  set('disc-tracks', state.tracks?.length);
  set('metadata-state', enrichment.status);
  set('album', state.disc?.title);
  set('artist', state.disc?.artist);
  set('revision', state.revision);

  const list = byId('events');
  list.replaceChildren();
  if (diagnosticGap || (window && diagnosticSequence === null)) {
    const item = document.createElement('li');
    item.textContent = 'UNKNOWN: observation history is incomplete';
    list.append(item);
  }
  if (read.active_warning) {
    const item = document.createElement('li');
    item.className = 'WARNING';
    item.textContent = `Stream warning: ${formatEvidence(read.active_warning)}`;
    list.append(item);
  }
  const events = (state.recent_events || []).slice(-8).reverse();
  if (!events.length) {
    const item = document.createElement('li');
    item.textContent = 'No read observations';
    list.append(item);
  }
  for (const event of events) {
    const item = document.createElement('li');
    item.className = event.severity || '';
    item.textContent = `#${event.sequence} ${event.read_status}`
      + ` · LBA ${event.region?.start_lba}–${event.region?.end_lba}`;
    list.append(item);
  }
}

// Fetch a snapshot before subscribing to the live event stream.
// WebSocket 購読前に REST から初期 snapshot を取得します。
async function load() {
  try {
    const response = await fetch('/api/state', { cache: 'no-store' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    render(await response.json());
  } catch (error) {
    setConnection(`State error: ${error.message}`, 'disconnected');
  }
}

let retry;
let activeSocket;

// Retain a useful diagnostics page while the daemon is restarting.
// daemon 再起動中も診断画面を復帰できるよう自動再接続します。
function connect() {
  clearTimeout(retry);
  setConnection('Connecting', 'pending');
  const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
  const socket = new WebSocket(`${scheme}://${location.host}/api/events`);

  activeSocket = socket;
  socket.onopen = () => { if (activeSocket === socket) setConnection('Live', 'connected'); };
  socket.onmessage = (message) => {
    // Ignore callbacks from a retired connection after reconnect.
    // 再接続後に旧接続のcallbackが新sessionを上書きしない。
    if (activeSocket !== socket) return;
    try {
      render(JSON.parse(message.data));
    } catch {
      setConnection('Invalid snapshot', 'disconnected');
    }
  };
  socket.onerror = () => { if (activeSocket === socket) socket.close(); };
  socket.onclose = () => {
    if (activeSocket !== socket) return;
    activeSocket = null;
    setConnection('Reconnecting', 'disconnected');
    retry = setTimeout(async () => {
      await load();
      connect();
    }, 1500);
  };
}

load().finally(connect);
