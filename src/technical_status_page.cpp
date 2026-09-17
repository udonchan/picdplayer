#include "technical_status_page.hpp"

std::string_view technical_status_html() {
    return R"PICD(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>PiCDPlayer Technical Status</title>
  <link rel="stylesheet" href="/debug/status.css">
</head>
<body>
  <header>
    <div><h1>PiCDPlayer</h1><p>Technical status</p></div>
    <span id="connection" class="pill pending">Connecting</span>
  </header>
  <main>
    <section class="hero">
      <div><span class="label">PLAYER</span><strong id="player-state">—</strong></div>
      <div><span class="label">TRACK</span><strong id="track">—</strong></div>
      <div><span class="label">POSITION</span><strong id="position">—</strong></div>
      <div><span class="label">CURRENT PCM</span><strong id="integrity">—</strong></div>
    </section>
    <section class="grid">
      <article>
        <h2>Read path</h2>
        <dl>
          <dt>Activity</dt><dd id="read-activity">—</dd>
          <dt>Strategy</dt><dd id="read-strategy">—</dd>
          <dt>Buffered blocks</dt><dd id="queued-blocks">—</dd>
          <dt>Last prebuffer</dt><dd id="prebuffer-wait">—</dd>
          <dt>Latest read</dt><dd id="latest-read">—</dd>
          <dt>Calls / accepted</dt><dd id="read-stats">—</dd>
          <dt>Retries / failures</dt><dd id="read-errors">—</dd>
          <dt>Dropped events</dt><dd id="dropped-events">—</dd>
        </dl>
        <p class="note">CLEAN means no anomaly was observed by the selected reader. It does not prove identity with the original PCM.</p>
      </article>
      <article>
        <h2>Drive</h2>
        <p id="drive-name" class="drive-name">—</p>
        <dl>
          <dt>Firmware</dt><dd id="drive-firmware">—</dd>
          <dt>DAE</dt><dd id="cap-dae">—</dd>
          <dt>C2 support</dt><dd id="cap-c2">—</dd>
          <dt>C2 trust</dt><dd id="cap-c2-trust">—</dd>
          <dt>Read cache</dt><dd id="cap-cache">—</dd>
          <dt>Accurate stream</dt><dd id="cap-stream">—</dd>
          <dt>Speed control</dt><dd id="cap-speed">—</dd>
          <dt>Read offset</dt><dd id="drive-offset">—</dd>
        </dl>
        <p id="probe-error" class="warning" hidden></p>
      </article>
      <article>
        <h2>Disc</h2>
        <dl>
          <dt>Media</dt><dd id="media-state">—</dd>
          <dt>Tracks</dt><dd id="disc-tracks">—</dd>
          <dt>Metadata</dt><dd id="metadata-state">—</dd>
          <dt>Album</dt><dd id="album">—</dd>
          <dt>Artist</dt><dd id="artist">—</dd>
        </dl>
      </article>
      <article>
        <h2>Recent observations</h2>
        <ol id="events" class="events"><li>No read observations</li></ol>
      </article>
    </section>
  </main>
  <footer>Read-only diagnostics · snapshot revision <span id="revision">—</span></footer>
  <script src="/debug/status.js" defer></script>
</body>
</html>)PICD";
}

std::string_view technical_status_css() {
    return R"PICD(:root{color-scheme:dark;--bg:#101314;--panel:#191e20;--line:#2c3437;--text:#edf2f2;--muted:#9aa8aa;--good:#7ed9a3;--warn:#f2c66d;--bad:#ff8c82}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 system-ui,sans-serif}header,main,footer{max-width:1100px;margin:auto;padding:20px}header{display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid var(--line)}h1,h2,p{margin:0}header p,.label,.note,footer{color:var(--muted)}h1{font-size:24px}h2{font-size:16px;margin-bottom:14px}.pill{padding:5px 10px;border:1px solid;border-radius:999px}.connected{color:var(--good)}.pending{color:var(--warn)}.disconnected,.warning{color:var(--bad)}.hero{display:grid;grid-template-columns:repeat(4,1fr);gap:1px;background:var(--line);border:1px solid var(--line);border-radius:10px;overflow:hidden;margin-bottom:18px}.hero div{background:var(--panel);padding:18px}.hero strong,.hero span{display:block}.hero strong{font-size:20px;margin-top:4px}.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:18px}article{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:18px;min-width:0}dl{display:grid;grid-template-columns:minmax(120px,1fr) minmax(0,1.6fr);gap:8px 14px;margin:0}dt{color:var(--muted)}dd{margin:0;overflow-wrap:anywhere}.drive-name{font-size:18px;margin-bottom:14px}.note{font-size:13px;margin-top:16px}.events{margin:0;padding-left:22px;max-height:270px;overflow:auto}.events li{padding:4px 0}.events .WARNING,.events .ERROR{color:var(--warn)}footer{text-align:center;font-size:13px}@media(max-width:720px){.hero,.grid{grid-template-columns:1fr 1fr}.hero strong{font-size:17px}}@media(max-width:480px){.hero,.grid{grid-template-columns:1fr}header{align-items:flex-start}})PICD";
}

std::string_view technical_status_javascript() {
    return R"PICD('use strict';
const byId=id=>document.getElementById(id);
const set=(id,value)=>{byId(id).textContent=value??'—'};
const frames=value=>Number.isInteger(value)?`${Math.floor(value/75/60)}:${String(Math.floor(value/75)%60).padStart(2,'0')}`:'—';
const evidence=value=>{if(!value)return'Not available';const v=value.verification||{};const detail=v.attempts?` · ${v.matching_reads}/${v.attempts} match/read${v.mismatches?` · ${v.mismatches} mismatch`:''}${v.time_budget_exhausted?' · budget exhausted':''}`:'';return`${value.status} · ${value.local_verification}${detail} · LBA ${value.start_lba}–${value.start_lba+value.frames_read}`};
const capability=value=>value?`${value.value}${value.source&&value.source!=='NONE'?` · ${value.source}`:''}`:'UNKNOWN';
function connection(label,kind){const node=byId('connection');node.textContent=label;node.className=`pill ${kind}`}
function render(state){
  const player=state.player||{},read=state.read||{},drive=state.drive||{},media=state.media||{};
  set('player-state',player.state);set('track',player.track);set('position',frames(player.position_in_track_frames));
  set('integrity',media.state==='NO_DISC'?'NO DISC':evidence(read.current_playback));
  set('read-activity',read.activity);set('read-strategy',`${read.requested_mode||'—'} · ${read.effective_strategy||'—'}`);
  const capacity=Number.isInteger(read.buffer_capacity_frames)&&read.read_block_frames?read.buffer_capacity_frames/read.read_block_frames:'—';
  set('queued-blocks',`${read.queued_blocks??'—'} / ${capacity} (start ${read.startup_buffer_frames??'—'} frames)`);set('latest-read',evidence(read.latest));
  set('prebuffer-wait',read.last_prebuffer_wait_ms==null?'Pending':`${read.last_prebuffer_wait_ms} ms (${read.prebuffer_target_frames??'—'} frames)`);
  const stats=read.stats||{};set('read-stats',`${stats.read_calls??0} / ${stats.frames_accepted??0} frames · verified ${stats.verified_calls??0}`);
  set('read-errors',`${stats.direct_retries??0} / ${stats.failed_calls??0}`);set('dropped-events',read.dropped_events??0);
  set('drive-name',[drive.vendor,drive.model].filter(Boolean).join(' ')||drive.device||'Unknown drive');
  set('drive-firmware',drive.firmware);set('cap-dae',capability(drive.digital_audio_extraction));
  set('cap-c2',capability(drive.c2_supported));set('cap-c2-trust',capability(drive.c2_trustworthy));
  set('cap-cache',capability(drive.read_cache));set('cap-stream',capability(drive.accurate_stream));
  set('cap-speed',capability(drive.speed_control));
  set('drive-offset',drive.read_offset_samples==null?'UNKNOWN':`${drive.read_offset_samples} samples`);
  const probe=byId('probe-error');probe.hidden=!drive.probe_error;probe.textContent=drive.probe_error||'';
  set('media-state',media.state);set('disc-tracks',state.disc?.track_count);
  const metadata=state.metadata||{};set('metadata-state',metadata.status);
  const selected=Number.isInteger(metadata.selected)?metadata.candidates?.[metadata.selected]:null;
  set('album',selected?.album_title);set('artist',selected?.album_artist);set('revision',state.revision);
  const list=byId('events');list.replaceChildren();const events=(state.recent_events||[]).slice(-8).reverse();
  if(!events.length){const item=document.createElement('li');item.textContent='No read observations';list.append(item)}
  for(const event of events){const item=document.createElement('li');item.className=event.severity||'';item.textContent=`#${event.sequence} ${event.read_status} · LBA ${event.region?.start_lba}–${event.region?.end_lba}`;list.append(item)}
}
async function load(){try{const response=await fetch('/api/state',{cache:'no-store'});if(!response.ok)throw new Error(`HTTP ${response.status}`);render(await response.json())}catch(error){connection(`State error: ${error.message}`,'disconnected')}}
let retry;
function connect(){clearTimeout(retry);connection('Connecting','pending');const scheme=location.protocol==='https:'?'wss':'ws';const socket=new WebSocket(`${scheme}://${location.host}/api/events`);socket.onopen=()=>connection('Live','connected');socket.onmessage=message=>{try{render(JSON.parse(message.data))}catch{connection('Invalid snapshot','disconnected')}};socket.onerror=()=>socket.close();socket.onclose=()=>{connection('Reconnecting','disconnected');retry=setTimeout(async()=>{await load();connect()},1500)}}
load().finally(connect);
)PICD";
}
