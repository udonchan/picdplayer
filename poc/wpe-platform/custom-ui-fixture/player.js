const state = document.getElementById('state');
const socket = document.getElementById('socket');
fetch('/api/state').then(response => response.json()).then(snapshot => {
  state.textContent = `Player: ${snapshot.player.state}`;
}).catch(error => { state.textContent = `State error: ${error.message}`; });
const events = new WebSocket(`ws://${location.host}/api/events`);
events.addEventListener('open', () => { socket.textContent = 'Events: connected'; });
events.addEventListener('close', () => { socket.textContent = 'Events: disconnected'; });
