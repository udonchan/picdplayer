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
  const text = value == null || value === '' ? '—' : String(value);
  if (element.textContent !== text) element.textContent = text;
};
const setStyle = (element, property, value) => {
  if (element.style[property] !== value) element.style[property] = value;
};
const setHidden = (element, value) => {
  if (element.hidden !== value) element.hidden = value;
};

// The View owns focus only; the daemon snapshot remains the playback authority.
// View が保持するのは focus だけで、再生状態は daemon の snapshot を正とします。
const controls = Array.from(document.querySelectorAll?.('.player-controls button[data-command]') || []);
const policyButtons = [byId('policy-single'), byId('policy-repeat')];
const settingsCloseButton = byId('settings-close');
const settingsActions = [...policyButtons, settingsCloseButton];
let focusedControl = -1;
let pendingCommand = null;
let feedbackTimer;
let settingsOpen = false;
let focusedPolicy = 0;
let settingsFocusMoved = false;
let settingsRequestId = 0;
let policyState = null;
let pendingPolicy = false;
let pickerOpen = false;
let pickerIndex = 0;
let pickerKey = '';
let autoOpenedPickerKey = '';
let pendingSelection = null;
let candidateButtons = [];
let lastPickerFocused = null;
const multipleCandidateSelection = (snapshot) => {
  const selection = snapshot?.enrichment?.selection;
  return snapshot?.disc?.state === 'AUDIO_READY' && ['AMBIGUOUS', 'SELECTED', 'DECLINED'].includes(selection?.state)
    && Array.isArray(selection.candidates) && selection.candidates.length > 1 ? selection : null;
};
const selectionKey = (selection) => selection
  ? JSON.stringify([selection.session_id, selection.disc_generation, selection.metadata_generation]) : '';
const candidateDescription = (candidate) => [candidate.artist, candidate.date, candidate.country,
  Number.isInteger(candidate.medium_position) ? `Disc ${candidate.medium_position}` : null,
  candidate.medium_title, Number.isInteger(candidate.track_count) ? `${candidate.track_count} tracks` : null]
  .filter(Boolean).join(' · ');
function closePicker() {
  pickerOpen = false;
  lastPickerFocused = null;
  pendingSelection = null;
  setHidden(byId('metadata-picker'), true);
  set('metadata-feedback', '');
  focusControl(-1);
}
function renderPickerFocus() {
  const selection = multipleCandidateSelection(currentSnapshot);
  const selectedIndex = selection?.selected_index;
  candidateButtons.forEach((button, index) => {
    const focused = String(pickerOpen && index === pickerIndex);
    if (button.dataset.focused !== focused) {
      button.dataset.focused = focused;
    }
    button.setAttribute('aria-selected', String(button.dataset.candidateIndex === String(selectedIndex) ||
      (button.dataset.candidateIndex === 'none' && selection?.state === 'DECLINED')));
    const candidate = selection?.candidates[index];
    if (candidate && button.children?.[1]) {
      const details = `${candidate.index === selectedIndex ? 'CURRENT SELECTION · ' : ''}${candidateDescription(candidate) || 'Additional details not available'}`;
      if (button.children[1].textContent !== details) button.children[1].textContent = details;
    }
    const disabled = Boolean(pendingSelection);
    if (button.disabled !== disabled) button.disabled = disabled;
  });
  const active = pickerOpen ? candidateButtons[pickerIndex] : null;
  if (active && active !== lastPickerFocused) {
    active.focus?.({ preventScroll: true });
    active.scrollIntoView?.({ block: 'nearest' });
  }
  lastPickerFocused = active;
}
function renderPicker(snapshot) {
  const selection = multipleCandidateSelection(snapshot);
  const nextKey = selectionKey(selection);
  if (!selection || (pickerOpen && pickerKey && pickerKey !== nextKey)) {
    if (pickerOpen) closePicker();
    pickerKey = '';
    candidateButtons = [];
    if (!selection) return;
  }
  if (settingsOpen) return;
  // Show each new ambiguity once. Back dismisses it without suppressing the
  // transport controls, while Choose album can reopen the same candidate set.
  // 新しい曖昧候補は一度だけ自動表示します。Back後は手動で開き直せます。
  if (selection.state === 'AMBIGUOUS' && autoOpenedPickerKey !== nextKey) {
    autoOpenedPickerKey = nextKey;
    openPicker();
    return;
  }
  if (pendingSelection && selection.state === 'SELECTED' &&
      selection.selected_index === selection.candidates[pendingSelection.index]?.index) {
    closePicker();
    return;
  }
  if (pendingSelection && selection.state === 'DECLINED' &&
      pendingSelection.index === selection.candidates.length) {
    closePicker();
    return;
  }
  if (!pickerOpen) return;
  if (pickerKey !== nextKey) {
    pickerKey = nextKey;
    const selectedPosition = selection.candidates.findIndex((candidate) => candidate.index === selection.selected_index);
    pickerIndex = selection.state === 'DECLINED' ? selection.candidates.length
      : selectedPosition >= 0 ? selectedPosition : 0;
    lastPickerFocused = null;
    set('metadata-picker-note', selection.state === 'SELECTED'
      ? 'Choose different album information. The current choice does not prove which physical release is inserted.'
      : 'Playback continues as Audio CD until a candidate is selected. A match here does not prove which physical release is inserted.');
    const list = byId('metadata-candidates');
    candidateButtons = selection.candidates.map((candidate, index) => {
      const button = document.createElement('button');
      button.type = 'button';
      button.setAttribute('role', 'option');
      button.dataset.candidateIndex = String(candidate.index);
      const title = document.createElement('span');
      title.className = 'candidate-title';
      title.textContent = candidate.title || `Candidate ${index + 1}`;
      const details = document.createElement('span');
      details.className = 'candidate-details';
      details.textContent = `${candidate.index === selection.selected_index ? 'CURRENT SELECTION · ' : ''}${candidateDescription(candidate) || 'Additional details not available'}`;
      button.append(title, details);
      button.addEventListener('click', (event) => {
        pickerIndex = index;
        if (event.detail === 0) handleNavigationInput('select', 'keyboard');
        else requestCandidate(index);
      });
      return button;
    });
    const none = document.createElement('button');
    none.type = 'button';
    none.dataset.candidateIndex = 'none';
    none.setAttribute('role', 'option');
    const noneTitle = document.createElement('span');
    noneTitle.className = 'candidate-title';
    noneTitle.textContent = 'None of these';
    const noneDetails = document.createElement('span');
    noneDetails.className = 'candidate-details';
    noneDetails.textContent = 'Use Audio CD information for this disc';
    none.append(noneTitle, noneDetails);
    none.addEventListener('click', (event) => {
      pickerIndex = selection.candidates.length;
      if (event.detail === 0) handleNavigationInput('select', 'keyboard');
      else requestCandidate(pickerIndex);
    });
    candidateButtons.push(none);
    list.replaceChildren(...candidateButtons);
  }
  renderPickerFocus();
}
function openPicker() {
  const selection = multipleCandidateSelection(currentSnapshot);
  if (!selection) return;
  pickerOpen = true;
  pickerKey = '';
  focusControl(-1);
  setHidden(byId('metadata-picker'), false);
  renderPicker(currentSnapshot);
}
async function requestCandidate(index) {
  const selection = multipleCandidateSelection(currentSnapshot);
  if (!pickerOpen || !selection || pendingSelection || index < 0 || index > selection.candidates.length) return;
  const decline = index === selection.candidates.length;
  if (decline && selection.state === 'DECLINED') { closePicker(); return; }
  if (!decline && selection.state === 'SELECTED' && selection.selected_index === selection.candidates[index].index) {
    closePicker();
    return;
  }
  const key = selectionKey(selection);
  pendingSelection = { key, index };
  renderPickerFocus();
  set('metadata-feedback', 'SENDING SELECTION');
  let accepted = false;
  try {
    const response = await fetch('/api/metadata-selection', { method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ session_id: selection.session_id,
        disc_generation: selection.disc_generation, metadata_generation: selection.metadata_generation,
        candidate_index: decline ? null : selection.candidates[index].index }),
    });
    if (!pickerOpen || selectionKey(multipleCandidateSelection(currentSnapshot)) !== key) return;
    accepted = response.status === 204;
    set('metadata-feedback', response.status === 204 ? 'WAITING FOR ALBUM UPDATE'
      : `SELECTION REJECTED · ${response.status}`);
    if (response.status === 409) load();
  } catch {
    if (pickerOpen && selectionKey(multipleCandidateSelection(currentSnapshot)) === key)
      set('metadata-feedback', 'CONNECTION ERROR');
  } finally {
    if (!accepted) pendingSelection = null;
    renderPickerFocus();
  }
}
const controlEnabled = (command, snapshot) => {
  if (command === 'settings') return true;
  if (snapshot?.disc?.state !== 'AUDIO_READY') return false;
  if (command === 'metadata') return Boolean(multipleCandidateSelection(snapshot));
  const state = snapshot?.player?.state;
  if (!['STOPPED', 'PLAYING', 'PAUSED'].includes(state)) return false;
  if (command === 'play') return state === 'STOPPED' || state === 'PAUSED';
  if (command === 'pause') return state === 'PLAYING';
  if (command === 'stop') return state === 'PLAYING' || state === 'PAUSED';
  return Array.isArray(snapshot?.tracks) && snapshot.tracks.length > 1;
};
function renderControls(snapshot) {
  const selection = multipleCandidateSelection(snapshot);
  const matchStatus = byId('metadata-match-status');
  setHidden(matchStatus, !selection);
  if (selection) {
    const selectedPosition = selection.candidates.findIndex((candidate) => candidate.index === selection.selected_index);
    set('metadata-match-status', selection.state === 'AMBIGUOUS'
      ? `${selection.candidates.length} MATCHES · CHOOSE`
      : selection.state === 'DECLINED' ? `${selection.candidates.length} MATCHES · NONE`
      : selectedPosition >= 0
        ? `${selection.candidates.length} MATCHES · #${selectedPosition + 1}`
        : `${selection.candidates.length} MATCHES · UNKNOWN`);
  }
  if (focusedControl >= 0 && !controlEnabled(controls[focusedControl]?.dataset.command, snapshot))
    focusedControl = -1;
  controls.forEach((button, index) => {
    if (button.dataset.command === 'metadata') {
      setHidden(button, !selection);
      button.textContent = selection && selection.state !== 'AMBIGUOUS' ? 'Change album' : 'Choose album';
    }
    const stateAvailable = controlEnabled(button.dataset.command, snapshot);
    const available = stateAvailable && pendingCommand !== button.dataset.command;
    if (button.disabled === available) button.disabled = !available;
    const focused = focusedControl === index && stateAvailable;
    if (button.dataset.focused !== String(focused)) button.dataset.focused = String(focused);
  });
}
function focusControl(index) {
  if (index < 0 && focusedControl >= 0) controls[focusedControl]?.blur?.();
  focusedControl = index;
  renderControls(currentSnapshot);
  if (index >= 0) controls[index]?.focus?.({ preventScroll: true });
}
function moveControl(direction) {
  if (!controls.length) return;
  const enabled = controls.map((button, index) => !button.disabled ? index : -1).filter((index) => index >= 0);
  if (!enabled.length) return;
  const position = enabled.indexOf(focusedControl);
  const next = position < 0 ? (direction > 0 ? enabled[0] : enabled[enabled.length - 1])
    : enabled[(position + direction + enabled.length) % enabled.length];
  focusControl(next);
}
function controlFeedback(message) {
  const label = byId('control-feedback');
  if (label.textContent !== message) label.textContent = message;
  clearTimeout(feedbackTimer);
  if (message) feedbackTimer = setTimeout(() => {
    if (label.textContent === message) label.textContent = '';
  }, 1800);
}
function renderPolicySettings() {
  const requested = policyState?.requested;
  const effective = policyState?.effective;
  const valid = requested && ['SINGLE', 'REPEAT'].includes(requested.mode) &&
    ['region_frames', 'required_matches', 'maximum_attempts', 'time_budget_ms']
      .every((key) => Number.isInteger(requested[key]) && requested[key] >= 0);
  policyButtons.forEach((button, index) => {
    button.disabled = !valid || pendingPolicy;
    button.dataset.selected = String(valid && requested.mode === (index ? 'REPEAT' : 'SINGLE'));
    button.dataset.focused = String(settingsOpen && focusedPolicy === index);
  });
  settingsCloseButton.dataset.focused = String(settingsOpen && focusedPolicy === 2);
  set('settings-region', valid ? `${requested.region_frames} CD frames` : '—');
  set('settings-matches', valid ? requested.required_matches : '—');
  set('settings-attempts', valid ? requested.maximum_attempts : '—');
  set('settings-budget', valid ? `${requested.time_budget_ms} ms` : '—');
  set('settings-persistence', policyState?.persistence_configured === true
    ? 'A successful change is saved for daemon restarts.'
    : 'Changes apply to this daemon session only.');
  if (!valid) set('settings-policy-status', 'Read policy unavailable. Playback is unaffected.');
  if (valid) set('settings-policy-status', policyState.pending
    ? `Requested ${requested.mode}; effective ${effective?.mode || 'UNKNOWN'} until playback stops.`
    : `Effective ${effective?.mode || requested.mode}.`);
}
async function loadPolicySettings() {
  const requestId = settingsRequestId;
  try {
    const response = await fetch('/api/read-policy');
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const loaded = await response.json();
    if (!settingsOpen || requestId !== settingsRequestId) return;
    policyState = loaded;
    if (settingsOpen && !settingsFocusMoved &&
        ['SINGLE', 'REPEAT'].includes(policyState?.requested?.mode)) {
      focusPolicy(policyState.requested.mode === 'REPEAT' ? 1 : 0);
    }
    renderPolicySettings();
  } catch {
    if (!settingsOpen || requestId !== settingsRequestId) return;
    policyState = null;
    renderPolicySettings();
    set('settings-policy-status', 'Read policy unavailable. Playback is unaffected.');
  }
}
function focusPolicy(index) {
  focusedPolicy = index;
  renderPolicySettings();
  settingsActions[index]?.focus?.({ preventScroll: true });
}
function openSettings() {
  settingsOpen = true;
  settingsFocusMoved = false;
  settingsRequestId += 1;
  policyState = null;
  setHidden(byId('settings-panel'), false);
  focusPolicy(0);
  set('settings-policy-status', 'Loading read policy…');
  set('settings-persistence', 'Checking whether changes are saved…');
  void loadPolicySettings();
}
function closeSettings() {
  settingsOpen = false;
  settingsRequestId += 1;
  setHidden(byId('settings-panel'), true);
  const index = controls.findIndex((button) => button.dataset.command === 'settings');
  if (index >= 0) focusControl(index);
  renderPicker(currentSnapshot);
}
async function selectPolicy(index) {
  const requested = policyState?.requested;
  if (!settingsOpen || pendingPolicy || policyButtons[index]?.disabled || !requested) return;
  const requestId = settingsRequestId;
  const mode = index ? 'repeat' : 'single';
  pendingPolicy = true;
  renderPolicySettings();
  set('settings-policy-status', 'Saving policy…');
  let failed = false;
  try {
    const response = await fetch('/api/read-policy', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ mode, region_frames: requested.region_frames,
        required_matches: requested.required_matches, maximum_attempts: requested.maximum_attempts,
        time_budget_ms: requested.time_budget_ms }),
    });
    if (response.status !== 204) throw new Error(`HTTP ${response.status}`);
    if (settingsOpen && requestId === settingsRequestId) await loadPolicySettings();
  } catch {
    failed = true;
  } finally {
    pendingPolicy = false;
    renderPolicySettings();
    if (failed && settingsOpen && requestId === settingsRequestId)
      set('settings-policy-status', 'Policy change failed. Current playback continues.');
  }
}
async function activateControl(button) {
  if (!button || button.disabled || pendingCommand) return;
  const command = button.dataset.command;
  if (command === 'settings') { openSettings(); return; }
  if (command === 'metadata') { openPicker(); return; }
  if (!['play', 'pause', 'stop', 'previous', 'next'].includes(command)) return;
  pendingCommand = command;
  renderControls(currentSnapshot);
  controlFeedback('SENDING');
  try {
    const response = await fetch(`/api/${command}`, { method: 'POST' });
    controlFeedback(response.status === 204 ? 'REQUEST ACCEPTED' : `REJECTED · ${response.status}`);
  } catch {
    controlFeedback('CONNECTION ERROR');
  } finally {
    pendingCommand = null;
    renderControls(currentSnapshot);
  }
}
function handleNavigation(action) {
  if (settingsOpen) {
    if (action === 'back') closeSettings();
    else if (action === 'left' || action === 'up' || action === 'right' || action === 'down') {
      settingsFocusMoved = true;
      const direction = action === 'left' || action === 'up' ? -1 : 1;
      focusPolicy((focusedPolicy + direction + settingsActions.length) % settingsActions.length);
    }
    else if (action === 'select') {
      if (focusedPolicy === 2) closeSettings();
      else void selectPolicy(focusedPolicy);
    }
    return;
  }
  if (pickerOpen) {
    if (action === 'back') closePicker();
    else if ((action === 'up' || action === 'left') && !pendingSelection) {
      pickerIndex = (pickerIndex + candidateButtons.length - 1) % candidateButtons.length;
      renderPickerFocus();
    } else if ((action === 'down' || action === 'right') && !pendingSelection) {
      pickerIndex = (pickerIndex + 1) % candidateButtons.length;
      renderPickerFocus();
    } else if (action === 'select') requestCandidate(pickerIndex);
    return;
  }
  if (action === 'left' || action === 'up') moveControl(-1);
  else if (action === 'right' || action === 'down') moveControl(1);
  else if (action === 'select') activateControl(controls[focusedControl]);
  else if (action === 'back') focusControl(-1);
}
// Some TVs deliver one CEC direction both through the daemon and as a Chromium key.
// 同じリモコン操作がCECとChromiumのキー入力の両方へ届く場合、後着の一方だけを抑えます。
let lastNavigationInput = null;
function handleNavigationInput(action, source) {
  const now = Date.now();
  const duplicate = lastNavigationInput?.action === action
    && lastNavigationInput.source !== source && now - lastNavigationInput.time < 250;
  if (duplicate) {
    lastNavigationInput = null;
    return;
  }
  lastNavigationInput = { action, source, time: now };
  handleNavigation(action);
}
controls.forEach((button, index) => button.addEventListener('click', (event) => {
  if (pickerOpen || settingsOpen) return;
  focusControl(index);
  // Keyboard activation produces a click with detail=0; a CEC select can
  // arrive for the same remote press. Pointer clicks remain independent.
  // キーボード由来のclickは同じリモコン操作のCEC selectと重複し得ます。
  if (event?.detail === 0) handleNavigationInput('select', 'keyboard');
  else activateControl(button);
}));
policyButtons.forEach((button, index) => button.addEventListener?.('click', (event) => {
  focusPolicy(index);
  if (event?.detail === 0) handleNavigationInput('select', 'keyboard');
  else void selectPolicy(index);
}));
settingsCloseButton.addEventListener?.('click', closeSettings);
document.addEventListener('keydown', (event) => {
  if (settingsOpen && event.key === 'Tab') {
    event.preventDefault();
    settingsFocusMoved = true;
    const targets = [...policyButtons.filter((button) => !button.disabled), settingsCloseButton];
    const current = targets.indexOf(document.activeElement);
    const next = current < 0 ? (event.shiftKey ? targets.length - 1 : 0)
      : (current + (event.shiftKey ? targets.length - 1 : 1)) % targets.length;
    targets[next].focus?.({ preventScroll: true });
    return;
  }
  const actions = { ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right', Escape: 'back' };
  if (!actions[event.key] || (!controls.length && !settingsOpen && !pickerOpen)) return;
  event.preventDefault();
  handleNavigationInput(actions[event.key], 'keyboard');
});

// CD frame is 1/75 second. Keep this conversion in the UI presentation layer.
// CD frame は 1/75 秒です。この変換は表示層だけで行います。
const formatTime = (frames) => {
  if (!Number.isInteger(frames) || frames < 0) return '0:00';
  const minutes = Math.floor(frames / 4500);
  const seconds = String(Math.floor(frames / 75) % 60).padStart(2, '0');
  return `${minutes}:${seconds}`;
};

function setConnection(value) {
  const states = {
    Live: ['connected', 'DAEMON · CONNECTED'],
    Connecting: ['connecting', 'DAEMON · CONNECTING'],
    Reconnecting: ['reconnecting', 'DAEMON · RECONNECTING'],
    Offline: ['offline', 'DAEMON · OFFLINE'],
    'Invalid state': ['invalid', 'DAEMON · INVALID STATE'],
  };
  const [state, label] = states[value] || ['unknown', 'DAEMON · UNKNOWN'];
  const element = byId('connection');
  if (element.dataset.state !== state) element.dataset.state = state;
  set('connection', label);
}

function currentTrack(tracks, number) {
  return tracks?.find((track) => track.number === number) || null;
}

const safeInteger = (value) => Number.isSafeInteger(value);
const safeNonNegative = (value) => safeInteger(value) && value >= 0;

function formatEvidence(value) {
  if (!value || !safeInteger(value.start_lba) || !safeNonNegative(value.frames_read)) return 'NOT AVAILABLE';
  const verification = value.verification || {};
  const suffix = safeNonNegative(value.direct_retries) && value.direct_retries > 0
    ? ` · retry ${value.direct_retries}` : '';
  const repeat = safeNonNegative(verification.attempts) && verification.attempts > 0
    ? ` · ${verification.matching_reads ?? '—'}/${verification.attempts} match` : '';
  const independence = typeof value.read_independence === 'string'
    ? ` · ${value.read_independence}` : '';
  const overlap = typeof verification.overlap === 'string' && verification.overlap !== 'NOT_REQUESTED'
    ? ` · overlap ${verification.overlap}` : '';
  return `${value.status || 'UNKNOWN'} · ${value.local_verification || 'UNKNOWN'} · LBA ${value.start_lba}–${value.start_lba + value.frames_read}${suffix}${repeat}${independence}${overlap}`;
}

function formatCapability(value) {
  if (!value || typeof value.value !== 'string') return 'UNKNOWN';
  return value.source && value.source !== 'NONE' ? `${value.value} · ${value.source}` : value.value;
}

function integritySummary(read) {
  if (read.active_warning) return `STREAM WARNING · ${read.active_warning.status || 'UNKNOWN'}`;
  if (!read.current_playback) return 'CURRENT READ · NOT AVAILABLE';
  return `CURRENT READ · ${read.current_playback.status || 'UNKNOWN'}`;
}

function formatReadStall(value) {
  if (!value || !safeNonNegative(value.timeout_ms)) return 'NOT AVAILABLE';
  const inflight = safeNonNegative(value.inflight_ms) ? `in-flight ${value.inflight_ms} ms` : 'idle';
  const last = safeNonNegative(value.last_timeout_ms) ? ` · last timeout ${value.last_timeout_ms} ms` : '';
  return `${inflight} · limit ${value.timeout_ms} ms${last}`;
}

function discLayoutKey(snapshot) {
  const layout = snapshot?.disc?.layout;
  const session = snapshot?.read?.session_id;
  if (snapshot?.schema_version !== 1 || snapshot?.disc?.state !== 'AUDIO_READY'
      || !layout || typeof layout.session_id !== 'string' || !layout.session_id
      || layout.session_id !== session || !safeNonNegative(layout.disc_generation)
      || !safeInteger(layout.start_lba) || !safeInteger(layout.leadout_lba)
      || layout.leadout_lba <= layout.start_lba) return null;
  return `${layout.session_id}:${layout.disc_generation}`;
}

let presentationSession = null;
let presentationRevision = -1;
let presentationStream = null;
let eventSequence = null;
let eventDropped = 0;
let observationGap = false;
let currentSnapshot = null;
let mapKey = null;
let mapLoadingKey = null;
let mapLastRequestedAt = Number.NEGATIVE_INFINITY;
let discMap = null;
let renderedMapIdentity = null;
let mapObservedStream = null;

function acceptSnapshot(snapshot) {
  const session = snapshot?.read?.session_id;
  const revision = snapshot?.revision;
  const stream = snapshot?.read?.stream_generation;
  if (typeof session === 'string' && session) {
    if (session === presentationSession && safeNonNegative(revision) && revision <= presentationRevision) return false;
    if (session !== presentationSession || stream !== presentationStream) {
      eventSequence = null;
      eventDropped = 0;
      observationGap = false;
      presentationRevision = -1;
    }
    presentationSession = session;
    presentationStream = stream;
    if (safeNonNegative(revision)) presentationRevision = revision;
  }
  const window = snapshot?.read?.event_window;
  if (window) {
    if ((eventSequence === null && safeNonNegative(window.first_sequence) && window.first_sequence > 1)
        || (eventSequence !== null && safeNonNegative(window.first_sequence)
          && window.first_sequence > eventSequence + 1)
        || (safeNonNegative(window.worker_dropped) && window.worker_dropped > eventDropped)) observationGap = true;
    if (safeNonNegative(window.last_sequence)) eventSequence = window.last_sequence;
    if (safeNonNegative(window.worker_dropped)) eventDropped = window.worker_dropped;
  }
  const key = discLayoutKey(snapshot);
  if (key !== mapKey) {
    mapKey = key;
    discMap = null;
    mapLoadingKey = null;
    mapLastRequestedAt = Number.NEGATIVE_INFINITY;
    renderedMapIdentity = null;
    mapObservedStream = null;
  }
  currentSnapshot = snapshot;
  return true;
}

function mapMatchesSnapshot(snapshot, detail) {
  const key = discLayoutKey(snapshot);
  const map = detail?.disc_map;
  if (!key || detail?.schema_version !== 1 || detail?.session_id !== snapshot.read?.session_id
      || !map || map.scope !== 'DISC' || map.disc_generation !== snapshot.disc.layout?.disc_generation
      || !safeNonNegative(map.revision) || !safeNonNegative(map.capacity) || map.capacity > 256
      || typeof map.observations_complete !== 'boolean' || !Array.isArray(map.regions)
      || map.regions.length > map.capacity) return null;
  if (discMap && discMap.key === key && safeNonNegative(discMap.revision) && map.revision < discMap.revision) return null;
  return { key, ...map };
}

const discMapRefreshIntervalMs = 2000;

function requestDiscMap(snapshot, force = false) {
  const key = discLayoutKey(snapshot);
  const playing = snapshot?.player?.state === 'PLAYING' && snapshot?.read?.current_playback;
  const now = performance.now();
  const due = now - mapLastRequestedAt >= discMapRefreshIntervalMs;
  if (!key || mapLoadingKey === key
      || (!force && discMap?.key === key && (!playing || !due))) return;
  mapLastRequestedAt = now;
  mapLoadingKey = key;
  renderDiscMap(snapshot);
  try {
    Promise.resolve(fetch('/api/read-history', { cache: 'no-store' })).then(async (response) => {
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const detail = await response.json();
      const accepted = mapMatchesSnapshot(currentSnapshot, detail);
      if (accepted) discMap = accepted;
    }).catch(() => {}).finally(() => {
      if (mapLoadingKey === key) mapLoadingKey = null;
      renderDiscMap(currentSnapshot);
    });
  } catch {
    mapLoadingKey = null;
    renderDiscMap(snapshot);
  }
}

function mapColor(flags) {
  if (!safeNonNegative(flags)) return '#647174';
  if (flags & (32 | 64)) return '#ff8c82';
  if (flags & 16) return '#b99cff';
  if (flags & (4 | 8)) return '#f2c66d';
  if (flags & 2) return '#7199a5';
  return '#647174';
}

function setMarker(id, layout, evidence) {
  const marker = byId(id);
  const lba = evidence?.start_lba;
  if (!layout || !safeInteger(lba) || !safeNonNegative(evidence?.disc_generation)
      || evidence.disc_generation !== layout.disc_generation || lba < layout.start_lba || lba >= layout.leadout_lba) {
    setHidden(marker, true);
    return false;
  }
  const angle = 2 * Math.PI * (lba - layout.start_lba) / (layout.leadout_lba - layout.start_lba) - Math.PI / 2;
  setStyle(marker, 'left', `${50 + 36 * Math.cos(angle)}%`);
  setStyle(marker, 'top', `${50 + 36 * Math.sin(angle)}%`);
  setHidden(marker, false);
  return true;
}

function renderDiscMap(snapshot) {
  const map = byId('disc-map');
  const label = byId('read-map-state');
  const layout = snapshot?.disc?.layout;
  const key = discLayoutKey(snapshot);
  if (!key) {
    set('read-map-state', snapshot?.disc?.state === 'NO_DISC' ? 'No accepted audio disc' : 'Waiting for an accepted disc layout');
    setStyle(map, 'background', '#202729');
    renderedMapIdentity = null;
    setMarker('map-latest', null, null);
    if (byId('map-marker-label').dataset.marker !== 'unavailable')
      byId('map-marker-label').dataset.marker = 'unavailable';
    set('map-marker-label', 'LATEST OBSERVED READ · NOT AVAILABLE');
    return;
  }
  if (!discMap || discMap.key !== key) {
    set('read-map-state', mapLoadingKey === key ? 'Loading bounded disc observations' : 'Map not obtained');
    setStyle(map, 'background', '#202729');
    renderedMapIdentity = null;
  } else {
    const identity = `${key}:${discMap.revision}`;
    if (identity !== renderedMapIdentity) {
      const span = layout.leadout_lba - layout.start_lba;
      const layers = [];
      for (const region of discMap.regions) {
        if (!safeInteger(region?.start_lba) || !safeInteger(region?.end_lba) || !safeNonNegative(region?.flags)
            || region.end_lba <= region.start_lba || region.start_lba < layout.start_lba
            || region.end_lba > layout.leadout_lba) continue;
        const begin = 100 * (region.start_lba - layout.start_lba) / span;
        const end = 100 * (region.end_lba - layout.start_lba) / span;
        // CSS conic-gradient starts at 12 o'clock, matching setMarker's -PI/2 angle.
        layers.push(`conic-gradient(from 0deg, transparent 0% ${begin}%, ${mapColor(region.flags)} ${begin}% ${end}%, transparent ${end}% 100%)`);
      }
      setStyle(map, 'background', layers.length ? layers.join(',') : '#202729');
      renderedMapIdentity = identity;
    }
    const completeness = discMap.observations_complete === true ? 'bounded observations' : 'incomplete lower bound';
    const validRegions = discMap.regions.filter((region) => safeInteger(region?.start_lba)
      && safeInteger(region?.end_lba) && safeNonNegative(region?.flags)
      && region.end_lba > region.start_lba && region.start_lba >= layout.start_lba
      && region.end_lba <= layout.leadout_lba).length;
    const regionLabel = validRegions === discMap.regions.length
      ? `${validRegions} regions`
      : `${validRegions} valid regions / ${discMap.regions.length} reported`;
    set('read-map-state', `${regionLabel} · ${completeness} · revision ${discMap.revision}`);
  }
  const latest = snapshot?.read?.latest;
  const hasLatest = setMarker('map-latest', layout, latest);
  const markerState = hasLatest ? 'available' : 'unavailable';
  if (byId('map-marker-label').dataset.marker !== markerState)
    byId('map-marker-label').dataset.marker = markerState;
  set('map-marker-label', hasLatest ? `LATEST OBSERVED READ · LBA ${latest.start_lba} · MAY BE AHEAD`
    : 'LATEST OBSERVED READ · NOT AVAILABLE');
}

// Cover art is optional enrichment. A failed image must not hide the album data.
// ジャケットは任意の付加情報です。画像読込失敗でアルバム情報を消しません。
function showArt(artwork, layout, session, selection) {
  const container = byId('art');
  const image = byId('cover');
  const url = typeof artwork?.url === 'string'
    ? artwork.url
    : '';

  const identity = url ? JSON.stringify([url, layout?.session_id || session || null,
    layout?.disc_generation ?? null, selection?.metadata_generation ?? null,
    selection?.selected_index ?? null]) : '';

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

function mediaMessage(hasDisc, mediaState, enrichmentStatus, selection) {
  if (!hasDisc) return mediaState === 'LOADING' ? 'READING DISC' : 'WAITING FOR DISC';
  if (enrichmentStatus === 'LOADING') return 'LOOKING UP ALBUM';
  if (selection?.state === 'AMBIGUOUS') return 'ALBUM SELECTION AVAILABLE';
  if (selection?.state === 'DECLINED') return 'NO ALBUM MATCH SELECTED';
  if (enrichmentStatus === 'UNAVAILABLE') return 'METADATA UNAVAILABLE';
  if (enrichmentStatus === 'ERROR') return 'METADATA UNAVAILABLE';
  return 'NOW PLAYING';
}

// Cache only the last CSS value, never an extrapolated playback position.
// CSS の直前の表示値だけを保持し、再生位置を browser 側で進めません。
let progressScale;

// Rendering only: never keep an independent player state in the browser.
// 描画専用です。browser 側に独立した再生状態を持ちません。
function render(snapshot) {
  if (!acceptSnapshot(snapshot)) return;
  const player = snapshot.player || {};
  const disc = snapshot.disc || {};
  const read = snapshot.read || {};
  const drive = snapshot.drive || {};
  const enrichment = snapshot.enrichment || {};
  const track = currentTrack(snapshot.tracks, player.track_number);
  const hasDisc = disc.state === 'AUDIO_READY';
  const fallbackTrack = Number.isInteger(player.track_number)
    ? `Track ${String(player.track_number).padStart(2, '0')}`
    : '—';

  set('media-message', mediaMessage(hasDisc, disc.state, enrichment.status, enrichment.selection));
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
  renderControls(snapshot);
  renderPicker(snapshot);
  showArt(hasDisc ? snapshot.artwork?.cover : null, disc.layout, snapshot.read?.session_id,
    enrichment.selection);
  set('read-activity', read.activity || 'UNKNOWN');
  set('read-strategy', read.effective_strategy || 'UNKNOWN');
  const requestedPolicy = read.policy?.requested?.mode;
  const effectivePolicy = read.policy?.effective?.mode;
  set('read-policy', read.policy?.pending
    ? `${effectivePolicy || 'UNKNOWN'} → ${requestedPolicy || 'UNKNOWN'} (PENDING)`
    : (effectivePolicy || requestedPolicy || 'UNKNOWN'));
  const blockCapacity = safeNonNegative(read.buffer_capacity_frames) && safeInteger(read.read_block_frames)
    && read.read_block_frames > 0 ? Math.floor(read.buffer_capacity_frames / read.read_block_frames) : null;
  const bufferKnown = safeNonNegative(read.queued_blocks) && blockCapacity !== null && blockCapacity > 0;
  set('read-buffer', bufferKnown ? `${read.queued_blocks} / ${blockCapacity} blocks` : 'N/A');
  const bufferFraction = bufferKnown ? Math.min(1, read.queued_blocks / blockCapacity) : 0;
  setStyle(byId('read-buffer-meter'), 'transform', `scaleX(${bufferFraction})`);
  set('read-current', formatEvidence(read.current_playback));
  set('read-latest', formatEvidence(read.latest));
  set('read-warning', read.active_warning ? formatEvidence(read.active_warning) : 'NONE');
  const history = read.history || {};
  set('read-window', observationGap ? 'UNKNOWN (event gap)' : `${history.included ? 'DETAIL ' : 'STREAM '}window ${history.capacity ?? '—'} · dropped ${read.dropped_events ?? '—'}`);
  const stats = read.stats || {};
  set('read-stats', safeNonNegative(stats.read_calls) && safeNonNegative(stats.frames_accepted)
    ? `${stats.read_calls} / ${stats.frames_accepted} frames` : 'NOT AVAILABLE');
  const coverage = read.coverage || {};
  set('read-coverage', `${coverage.scope || 'UNKNOWN'} · ${safeNonNegative(coverage.accepted_unique_frames) ? coverage.accepted_unique_frames : '—'} accepted unique frames`);
  set('read-stall', formatReadStall(read.read_stall));
  set('integrity-summary', integritySummary(read));
  set('drive-name', [drive.vendor, drive.model].filter(Boolean).join(' ') || drive.device || 'UNKNOWN DRIVE');
  set('cap-dae', formatCapability(drive.digital_audio_extraction));
  set('cap-c2', formatCapability(drive.c2_supported));
  set('cap-c2-trust', formatCapability(drive.c2_trustworthy));
  set('cap-cache', formatCapability(drive.read_cache));
  set('cap-stream', formatCapability(drive.accurate_stream));
  set('cap-speed', formatCapability(drive.speed_control));
  set('drive-speed-request', drive.speed_request_error
    ? `NOT APPLIED · ${drive.speed_request_error}`
    : (Number.isInteger(drive.requested_speed_x) ? `${drive.requested_speed_x}x REQUEST ACCEPTED` : 'NOT REQUESTED'));
  set('drive-speed-current', Number.isInteger(drive.current_speed_x)
    ? `${drive.current_speed_x}x OBSERVED` : 'NOT AVAILABLE');
  set('drive-offset', drive.read_offset_samples == null ? 'UNKNOWN' : `${drive.read_offset_samples} samples`);
  set('drive-note', drive.probe_error || 'Capability reports and speed requests are not measurement results.');
  renderDiscMap(snapshot);
  const observationStream = discLayoutKey(snapshot) && typeof read.session_id === 'string'
    && safeNonNegative(read.stream_generation) && read.current_playback
    ? `${discLayoutKey(snapshot)}:${read.stream_generation}` : null;
  if (observationStream && observationStream !== mapObservedStream) {
    mapObservedStream = observationStream;
    requestDiscMap(snapshot, true);
  } else {
    requestDiscMap(snapshot);
  }
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
let activeSocket;

// CEC/API events update the daemon state; reconnect after daemon or network restarts.
// CEC/API の状態更新は daemon が行います。daemon や通信の再起動後は再接続します。
function connect() {
  clearTimeout(retry);
  setConnection('Connecting');
  const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
  const socket = new WebSocket(`${scheme}://${location.host}/api/events`);
  activeSocket = socket;

  socket.onopen = () => {
    if (activeSocket !== socket) return;
    setConnection('Live');
    boot.connection(true);
  };
  socket.onmessage = (event) => {
    if (activeSocket !== socket) return;
    try {
      render(JSON.parse(event.data));
    } catch {
      setConnection('Invalid state');
    }
  };
  socket.onerror = () => { if (activeSocket === socket) socket.close(); };
  socket.onclose = () => {
    if (activeSocket !== socket) return;
    activeSocket = null;
    boot.connection(false);
    setConnection('Reconnecting');
    retry = setTimeout(async () => {
      await load();
      connect();
    }, 1500);
  };
}

byId('refresh-read-map').addEventListener?.('click', () => requestDiscMap(currentSnapshot, true));
byId('metadata-close').addEventListener?.('click', closePicker);

let navigationSocket;
let navigationRetry;
function connectNavigation() {
  clearTimeout(navigationRetry);
  const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
  const socket = new WebSocket(`${scheme}://${location.host}/api/navigation`);
  navigationSocket = socket;
  socket.onmessage = (event) => {
    if (navigationSocket !== socket) return;
    try {
      const message = JSON.parse(event.data);
      if (typeof message?.action === 'string') handleNavigationInput(message.action, 'cec');
    } catch { /* A malformed input is ignored; playback continues. */ }
  };
  socket.onerror = () => { if (navigationSocket === socket) socket.close(); };
  socket.onclose = () => {
    if (navigationSocket !== socket) return;
    navigationSocket = null;
    navigationRetry = setTimeout(connectNavigation, 1500);
  };
}

load().finally(() => { connect(); if (controls.length) connectNavigation(); });
