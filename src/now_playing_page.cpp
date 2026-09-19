#include "now_playing_page.hpp"

std::string_view now_playing_html() {
    return R"PICD(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>PiCDPlayer</title>
  <link rel="stylesheet" href="/player.css">
</head>
<body>
  <main>
    <header><span class="brand">PiCDPlayer</span><span id="connection" class="connection">Connecting</span></header>
    <section class="now-playing" aria-live="polite">
      <div id="art" class="art placeholder"><img id="cover" alt=""><span id="art-mark">CD</span></div>
      <div class="details">
        <p id="media-message" class="eyebrow">WAITING FOR DISC</p>
        <h1 id="album">No disc</h1>
        <p id="album-artist" class="album-artist">Insert an audio CD</p>
        <div class="track-row"><span id="track-number">—</span><div><h2 id="track-title">—</h2><p id="track-artist">—</p></div></div>
        <div class="timeline"><div class="progress"><span id="progress"></span></div><div><span id="position">0:00</span><span id="duration">0:00</span></div></div>
        <p id="player-state" class="state">NO_DISC</p>
      </div>
    </section>
  </main>
  <script src="/player.js" defer></script>
</body>
</html>)PICD";
}

std::string_view now_playing_css() {
    return R"PICD(:root{color-scheme:dark;--bg:#101314;--panel:#191e20;--line:#30393c;--text:#edf2f2;--muted:#9aa8aa;--accent:#7ed9a3}*{box-sizing:border-box}body{min-height:100vh;margin:0;background:radial-gradient(circle at 85% 8%,#19312a 0,transparent 28rem),var(--bg);color:var(--text);font:16px/1.45 system-ui,sans-serif;cursor:none}main{max-width:1200px;min-height:100vh;margin:auto;padding:32px 48px}header{display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid var(--line);padding-bottom:21px}.brand{font-size:25px;font-weight:750;letter-spacing:-.04em}.connection{color:var(--accent);border:1px solid currentColor;border-radius:999px;padding:5px 11px;font-size:14px}.now-playing{display:grid;grid-template-columns:minmax(250px,420px) minmax(0,1fr);gap:clamp(34px,7vw,96px);align-items:center;padding-top:min(13vh,130px)}.art{aspect-ratio:1;border:1px solid var(--line);background:#202729;position:relative;overflow:hidden;display:grid;place-items:center;box-shadow:0 24px 70px #0005}.art img{position:absolute;width:100%;height:100%;object-fit:cover;display:none}.art.has-cover img{display:block}.art-mark{width:38%;aspect-ratio:1;border:2px solid var(--muted);border-radius:50%;display:grid;place-items:center;color:var(--muted);font-size:clamp(18px,3vw,32px);letter-spacing:.08em}.art.has-cover .art-mark{display:none}.eyebrow,.state{font-weight:700;letter-spacing:.11em;color:var(--accent);font-size:13px;margin:0 0 10px}.details h1{font-size:clamp(32px,5vw,61px);line-height:1.05;letter-spacing:-.045em;margin:0;overflow-wrap:anywhere}.album-artist{color:var(--muted);font-size:clamp(18px,2vw,24px);margin:11px 0 45px;overflow-wrap:anywhere}.track-row{display:grid;grid-template-columns:auto minmax(0,1fr);gap:18px;align-items:start;padding:20px 0;border-top:1px solid var(--line);border-bottom:1px solid var(--line)}#track-number{font-size:28px;font-weight:750;color:var(--accent);min-width:2.2ch}.track-row h2{font-size:clamp(21px,3vw,32px);line-height:1.2;margin:0;overflow-wrap:anywhere}.track-row p{color:var(--muted);margin:4px 0 0;overflow-wrap:anywhere}.timeline{margin-top:30px}.progress{height:5px;background:#354043;border-radius:4px;overflow:hidden}.progress span{display:block;height:100%;width:0;background:var(--accent);transition:width .2s linear}.timeline div:last-child{display:flex;justify-content:space-between;color:var(--muted);font-variant-numeric:tabular-nums;margin-top:7px}.state{margin-top:29px}@media(max-width:720px){main{padding:23px}.now-playing{grid-template-columns:1fr;padding-top:45px;gap:30px}.art{max-width:430px;width:min(100%,430px)}.album-artist{margin-bottom:28px}})PICD";
}

std::string_view now_playing_javascript() {
    return R"PICD('use strict';
const byId=id=>document.getElementById(id);
const set=(id,value)=>byId(id).textContent=value||'—';
const time=frames=>Number.isInteger(frames)&&frames>=0?`${Math.floor(frames/4500)}:${String(Math.floor(frames/75)%60).padStart(2,'0')}`:'0:00';
function connection(value){byId('connection').textContent=value}
function selectedMetadata(metadata){if(!Number.isInteger(metadata?.selected))return null;return metadata.candidates?.[metadata.selected]||null}
function currentTrack(selected,number){return selected?.tracks?.find(track=>track.track_number===number)||null}
function showArt(artwork){const container=byId('art'),image=byId('cover');const url=artwork?.status==='AVAILABLE'&&typeof artwork.image_url==='string'?artwork.image_url:'';if(!url){image.removeAttribute('src');image.dataset.url='';container.classList.remove('has-cover');return}image.onload=()=>container.classList.add('has-cover');image.onerror=()=>container.classList.remove('has-cover');if(image.dataset.url!==url){image.dataset.url=url;image.src=url}if(image.complete&&image.naturalWidth>0)container.classList.add('has-cover')}
function render(snapshot){
  const player=snapshot.player||{},media=snapshot.media||{},metadata=snapshot.metadata||{};
  const selected=selectedMetadata(metadata),track=currentTrack(selected,player.track);
  const hasDisc=media.state==='AUDIO_READY';
  const fallbackTrack=Number.isInteger(player.track)?`Track ${String(player.track).padStart(2,'0')}`:'—';
  const message=hasDisc?(metadata.status==='LOADING'?'LOOKING UP ALBUM':metadata.status==='AMBIGUOUS'?'ALBUM SELECTION REQUIRED':metadata.status==='ERROR'?'METADATA UNAVAILABLE':'NOW PLAYING'):(media.state==='LOADING'?'READING DISC':'WAITING FOR DISC');
  set('media-message',message);set('album',hasDisc?(selected?.album_title||'Audio CD'):'No disc');
  set('album-artist',hasDisc?(selected?.album_artist||(metadata.status==='LOADING'?'Looking up album information':'Unknown artist')):'Insert an audio CD');
  set('track-number',Number.isInteger(player.track)?String(player.track).padStart(2,'0'):'—');
  set('track-title',hasDisc?(track?.title||fallbackTrack):'—');set('track-artist',hasDisc?(track?.artist||selected?.album_artist||''):'');
  const position=player.position_in_track_frames,length=player.current_track_length_frames;
  set('position',time(position));set('duration',time(length));
  const fraction=Number.isInteger(position)&&Number.isInteger(length)&&length>0?Math.min(100,100*position/length):0;
  byId('progress').style.width=`${fraction}%`;set('player-state',player.state||'NO_DISC');showArt(metadata.cover_art);
}
async function load(){try{const response=await fetch('/api/state',{cache:'no-store'});if(!response.ok)throw new Error(`HTTP ${response.status}`);render(await response.json())}catch{connection('Offline')}}
let retry;
function connect(){clearTimeout(retry);connection('Connecting');const scheme=location.protocol==='https:'?'wss':'ws';const socket=new WebSocket(`${scheme}://${location.host}/api/events`);socket.onopen=()=>connection('Live');socket.onmessage=event=>{try{render(JSON.parse(event.data))}catch{connection('Invalid state')}};socket.onerror=()=>socket.close();socket.onclose=()=>{connection('Reconnecting');retry=setTimeout(async()=>{await load();connect()},1500)}}
load().finally(connect);
)PICD";
}
