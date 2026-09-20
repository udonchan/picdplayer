/* PiCDPlayer default UI reference.
 * 日本語: 技術画面: 読み取り品質、ドライブ能力、metadata状態を表示します。
 * English: Technical view;
 renders read quality, drive capabilities, and metadata state.
 * API contract: the daemon is authoritative;
 this page only renders snapshots.
 */

'use strict';

const byId=id=>document.getElementById(id);

const set=(id,value)=>{byId(id).textContent=value??'—'};

const frames=value=>Number.isInteger(value)?`${Math.floor(value/75/60)}:${String(Math.floor(value/75)%60).padStart(2,'0')}`:'—';

const evidence=value=>{if(!value)return'Not available';
const v=value.verification||{};
const detail=v.attempts?` · ${v.matching_reads}/${v.attempts} match/read${v.mismatches?` · ${v.mismatches} mismatch`:''}${v.time_budget_exhausted?' · budget exhausted':''}`:'';
return`${value.status} · ${value.local_verification}${detail} · LBA ${value.start_lba}–${value.start_lba+value.frames_read}`};

const capability=value=>value?`${value.value}${value.source&&value.source!=='NONE'?` · ${value.source}`:''}`:'UNKNOWN';


function connection(label,kind){const node=byId('connection');
node.textContent=label;
node.className=`pill ${kind}`}

// Technical diagnostics are read-only; commands are intentionally not issued here.
// 技術診断画面は読み取り専用で、ここからコマンドを発行しません。
function render(state){
  const player=state.player||{},read=state.read||{},drive=state.drive||{},media=state.media||{};

  set('player-state',player.state);
set('track',player.track);
set('position',frames(player.position_in_track_frames));

  set('integrity',media.state==='NO_DISC'?'NO DISC':evidence(read.current_playback));

  set('read-activity',read.activity);
set('read-strategy',read.effective_strategy);

  const policy=read.policy||{},requestedPolicy=policy.requested?.mode,effectivePolicy=policy.effective?.mode;

  set('read-policy',policy.pending?`${effectivePolicy||'—'} → ${requestedPolicy||'—'} (pending)`:(effectivePolicy||requestedPolicy));

  const capacity=Number.isInteger(read.buffer_capacity_frames)&&read.read_block_frames?read.buffer_capacity_frames/read.read_block_frames:'—';

  set('queued-blocks',`${read.queued_blocks??'—'} / ${capacity} (start ${read.startup_buffer_frames??'—'} frames)`);
set('latest-read',evidence(read.latest));

  set('prebuffer-wait',read.last_prebuffer_wait_ms==null?'Pending':`${read.last_prebuffer_wait_ms} ms (${read.prebuffer_target_frames??'—'} frames)`);

  const stats=read.stats||{};
set('read-stats',`${stats.read_calls??0} / ${stats.frames_accepted??0} frames · verified ${stats.verified_calls??0}`);

  set('read-errors',`${stats.direct_retries??0} / ${stats.failed_calls??0}`);
set('dropped-events',read.dropped_events??0);

  set('drive-name',[drive.vendor,drive.model].filter(Boolean).join(' ')||drive.device||'Unknown drive');

  set('drive-firmware',drive.firmware);
set('cap-dae',capability(drive.digital_audio_extraction));

  set('cap-c2',capability(drive.c2_supported));
set('cap-c2-trust',capability(drive.c2_trustworthy));

  set('cap-cache',capability(drive.read_cache));
set('cap-stream',capability(drive.accurate_stream));

  set('cap-speed',capability(drive.speed_control));

  set('drive-offset',drive.read_offset_samples==null?'UNKNOWN':`${drive.read_offset_samples} samples`);

  const probe=byId('probe-error');
probe.hidden=!drive.probe_error;
probe.textContent=drive.probe_error||'';

  set('media-state',media.state);
set('disc-tracks',state.disc?.track_count);

  const metadata=state.metadata||{};
set('metadata-state',metadata.status);

  const selected=Number.isInteger(metadata.selected)?metadata.candidates?.[metadata.selected]:null;

  set('album',selected?.album_title);
set('artist',selected?.album_artist);
set('revision',state.revision);

  const list=byId('events');
list.replaceChildren();
const events=(state.recent_events||[]).slice(-8).reverse();

  if(!events.length){const item=document.createElement('li');
item.textContent='No read observations';
list.append(item)}
  for(const event of events){const item=document.createElement('li');
item.className=event.severity||'';
item.textContent=`#${event.sequence} ${event.read_status} · LBA ${event.region?.start_lba}–${event.region?.end_lba}`;
list.append(item)}
}
// Fetch a snapshot before opening the live event stream.
// WebSocket接続前にRESTから初期スナップショットを取得します。
async function load(){try{const response=await fetch('/api/state',{cache:'no-store'});
if(!response.ok)throw new Error(`HTTP ${response.status}`);
render(await response.json())}catch(error){connection(`State error: ${error.message}`,'disconnected')}}
let retry;


// Keep the diagnostics page usable during daemon restarts.
// daemon再起動中も画面を復帰できるよう自動再接続します。
function connect(){clearTimeout(retry);
connection('Connecting','pending');
const scheme=location.protocol==='https:'?'wss':'ws';
const socket=new WebSocket(`${scheme}://${location.host}/api/events`);
socket.onopen=()=>connection('Live','connected');
socket.onmessage=message=>{try{render(JSON.parse(message.data))}catch{connection('Invalid snapshot','disconnected')}};
socket.onerror=()=>socket.close();
socket.onclose=()=>{connection('Reconnecting','disconnected');
retry=setTimeout(async()=>{await load();
connect()},1500)}}
load().finally(connect);
