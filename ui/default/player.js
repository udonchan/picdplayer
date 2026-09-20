/* PiCDPlayer default UI reference.
 * 日本語: 再生画面: /api/state と /api/events を読み、現在曲とジャケットを表示します。
 * English: Now-playing view;
 reads /api/state and /api/events and renders track metadata.
 * API contract: the daemon is authoritative;
 this page only renders snapshots.
 */

'use strict';

const byId=id=>document.getElementById(id);

const set=(id,value)=>byId(id).textContent=value||'—';

const time=frames=>Number.isInteger(frames)&&frames>=0?`${Math.floor(frames/4500)}:${String(Math.floor(frames/75)%60).padStart(2,'0')}`:'0:00';


function connection(value){byId('connection').textContent=value}

function selectedMetadata(metadata){if(!Number.isInteger(metadata?.selected))return null;
return metadata.candidates?.[metadata.selected]||null}

function currentTrack(selected,number){return selected?.tracks?.find(track=>track.track_number===number)||null}

function showArt(artwork){const container=byId('art'),image=byId('cover');
const url=artwork?.status==='AVAILABLE'&&typeof artwork.image_url==='string'?artwork.image_url:'';
if(!url){image.removeAttribute('src');
image.dataset.url='';
container.classList.remove('has-cover');
return}image.onload=()=>container.classList.add('has-cover');
image.onerror=()=>container.classList.remove('has-cover');
if(image.dataset.url!==url){image.dataset.url=url;
image.src=url}if(image.complete&&image.naturalWidth>0)container.classList.add('has-cover')}

// 描画だけを担当し、操作状態を保持しません。
// Rendering only; the daemon remains the sole owner of player state.
function render(snapshot){
  const player=snapshot.player||{},media=snapshot.media||{},metadata=snapshot.metadata||{};

  const selected=selectedMetadata(metadata),track=currentTrack(selected,player.track);

  const hasDisc=media.state==='AUDIO_READY';

  const fallbackTrack=Number.isInteger(player.track)?`Track ${String(player.track).padStart(2,'0')}`:'—';

  const message=hasDisc?(metadata.status==='LOADING'?'LOOKING UP ALBUM':metadata.status==='AMBIGUOUS'?'ALBUM SELECTION REQUIRED':metadata.status==='ERROR'?'METADATA UNAVAILABLE':'NOW PLAYING'):(media.state==='LOADING'?'READING DISC':'WAITING FOR DISC');

  set('media-message',message);
set('album',hasDisc?(selected?.album_title||'Audio CD'):'No disc');

  set('album-artist',hasDisc?(selected?.album_artist||(metadata.status==='LOADING'?'Looking up album information':'Unknown artist')):'Insert an audio CD');

  set('track-number',Number.isInteger(player.track)?String(player.track).padStart(2,'0'):'—');

  set('track-title',hasDisc?(track?.title||fallbackTrack):'—');
set('track-artist',hasDisc?(track?.artist||selected?.album_artist||''):'');

  const position=player.position_in_track_frames,length=player.current_track_length_frames;

  set('position',time(position));
set('duration',time(length));

  const fraction=Number.isInteger(position)&&Number.isInteger(length)&&length>0?Math.min(100,100*position/length):0;

  byId('progress').style.width=`${fraction}%`;
set('player-state',player.state||'NO_DISC');
showArt(metadata.cover_art);

}
// 初期表示はREST snapshot、以後の更新はWebSocketを使います。
// Load an initial REST snapshot, then receive incremental snapshots over WebSocket.
async function load(){try{const response=await fetch('/api/state',{cache:'no-store'});
if(!response.ok)throw new Error(`HTTP ${response.status}`);
render(await response.json())}catch{connection('Offline')}}
let retry;


// CEC/API状態はdaemon側で更新されるため、切断時は再接続します。
// Reconnect automatically when the daemon's WebSocket connection is lost.
function connect(){clearTimeout(retry);
connection('Connecting');
const scheme=location.protocol==='https:'?'wss':'ws';
const socket=new WebSocket(`${scheme}://${location.host}/api/events`);
socket.onopen=()=>connection('Live');
socket.onmessage=event=>{try{render(JSON.parse(event.data))}catch{connection('Invalid state')}};
socket.onerror=()=>socket.close();
socket.onclose=()=>{connection('Reconnecting');
retry=setTimeout(async()=>{await load();
connect()},1500)}}
load().finally(connect);
