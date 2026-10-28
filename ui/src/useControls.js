import { computed, onBeforeUnmount, reactive, ref, shallowRef } from 'vue';

// The native app is authoritative; only unacknowledged edits are overlaid locally.
export function useControls() {
  const state = shallowRef(null);
  const connected = ref(false);
  const status = ref('Connecting to the video app…');
  const error = ref('');
  const busy = ref(false);
  const optimistic = reactive({});
  const queue = [];
  let socket, reconnectTimer, commandTimer, active, nextId = 1, disposed = false;
  const settings = computed(() => ({ ...state.value?.settings, ...optimistic }));

  function accept(snapshot) {
    if (snapshot && (!connected.value || !state.value || snapshot.sequence > state.value.sequence)) state.value = snapshot;
  }
  function clearEdits() {
    queue.length = 0;
    active = null;
    busy.value = false;
    for (const key of Object.keys(optimistic)) delete optimistic[key];
    clearTimeout(commandTimer);
  }
  function pump() {
    if (active || !connected.value || !queue.length) return;
    active = queue.shift();
    busy.value = true;
    socket.send(JSON.stringify(active));
    commandTimer = setTimeout(() => {
      error.value = 'The video app has not confirmed this action. Reconnecting; check the current values before retrying.';
      socket.close();
    }, 15000);
  }
  function command(action, payload = {}) {
    if (!connected.value) return;
    error.value = '';
    queue.push({ id: nextId++, action, ...payload });
    pump();
  }
  function edit(key, value) {
    if (!connected.value) return;
    optimistic[key] = value;
    const last = queue.at(-1);
    if (last?.action === 'settings' && last.targetMode === state.value.settings.mode) {
      last.values[key] = value;
    } else {
      command('settings', { targetMode: state.value.settings.mode, values: { [key]: value } });
    }
  }
  function connect() {
    socket = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`);
    socket.onmessage = ({ data }) => {
      try {
        const message = JSON.parse(data);
        if (message.type === 'state') {
          accept(message);
          connected.value = true;
          status.value = 'Connected';
          pump();
        } else if (message.type === 'result' && active?.id === message.id) {
          clearTimeout(commandTimer);
          accept(message.state);
          if (!message.ok) {
            error.value = message.error;
            clearEdits();
          } else {
            for (const key of Object.keys(active.values ?? {})) {
              if (!queue.some((job) => job.action === 'settings' && key in job.values)) delete optimistic[key];
            }
            status.value = active.action === 'saveConfig' ? 'Configuration saved' :
              active.action === 'saveSpec' ? 'Render spec added' : 'Connected';
            active = null;
            busy.value = false;
            pump();
          }
        }
      } catch {
        error.value = 'Unrecognized response from the video app. Reload the controls.';
        socket.close();
      }
    };
    socket.onclose = () => {
      connected.value = false;
      status.value = 'Disconnected · reconnecting…';
      if (active) error.value = 'Connection lost before confirmation. Check the current values before retrying.';
      clearEdits();
      if (!disposed) reconnectTimer = setTimeout(connect, 1000);
    };
    socket.onerror = () => socket.close();
  }
  connect();
  onBeforeUnmount(() => {
    disposed = true;
    clearTimeout(reconnectTimer);
    clearEdits();
    socket?.close();
  });
  return { state, settings, connected, busy, status, error, command, edit };
}
