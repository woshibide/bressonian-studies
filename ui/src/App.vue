<script setup>
import { computed, ref } from 'vue';
import NumberControl from './NumberControl.vue';
import { useControls } from './useControls.js';

const { state, settings, connected, busy, status, error, command, edit } = useControls();
const layers = [
  ['motionMask', 'Motion mask'], ['motionPolygons', 'Motion polygons'],
  ['invertedGrayscale', 'Inverted traces'], ['displacementMap', 'Displacement map'],
  ['displacedVideo', 'Displaced video'], ['originalVideo', 'Original video'],
];
const layerLabel = computed(() => layers.find(([key]) => key === settings.value.mode)?.[1] ?? 'Layer');
const source = computed(() => state.value?.sources[state.value.selectedVideo]);
const particles = computed(() => settings.value.mode === 'invertedGrayscale' ||
  (settings.value.colouredParticles && ['displacementMap', 'displacedVideo'].includes(settings.value.mode)));
const displacement = computed(() => ['displacementMap', 'displacedVideo'].includes(settings.value.mode));
const original = computed(() => settings.value.mode === 'originalVideo');
let storedTheme;
try { storedTheme = localStorage.getItem('cctv-theme'); } catch {}
const theme = ref(storedTheme === 'dark' ? 'dark' : 'light');
function toggleTheme() {
  theme.value = theme.value === 'light' ? 'dark' : 'light';
  try { localStorage.setItem('cctv-theme', theme.value); } catch {}
}
function adjacent(direction) {
  const count = state.value.sources.length;
  command('video', { index: (state.value.selectedVideo + direction + count) % count });
}
const tuning = [
  { name: 'boidCellSize', label: 'Cell size', min: 8, max: 512, step: 1, unit: 'px' },
  { name: 'boidAlignment', label: 'Alignment', min: 0, max: 100, step: 0.1 },
  { name: 'boidCohesion', label: 'Cohesion', min: 0, max: 100, step: 0.01 },
  { name: 'boidSeparation', label: 'Separation', min: 0, max: 100, step: 0.01 },
  { name: 'boidCrowdCount', label: 'Crowd count', min: 1, max: 1000000, step: 1 },
  { name: 'boidSwirl', label: 'Swirl', min: 0, max: 100, step: 0.01 },
];
const format = (value) => Number(value ?? 0).toLocaleString('en-US');
</script>

<template>
  <div class="workspace" :data-ui-theme="theme">
    <header class="app-header">
      <h1>CCTV <span>/</span> Controls</h1>
      <div class="header-actions">
        <span class="connection" :class="{ connected }"><i aria-hidden="true"></i>{{ connected ? 'Connected' : 'Reconnecting' }}</span>
        <button class="theme-button" @click="toggleTheme">{{ theme === 'light' ? 'Dark' : 'Light' }} theme</button>
      </div>
    </header>

    <div v-if="!connected" class="notice" role="status">
      {{ state ? 'Video app disconnected. Controls will reconnect automatically.' : 'Waiting for the video app. Run make dev in the project folder.' }}
    </div>
    <div v-if="error" class="notice error" role="alert">{{ error }}<button @click="error = ''" aria-label="Dismiss error">×</button></div>

    <main v-if="state" class="panels">
      <aside class="panel source-panel" aria-label="Source and configuration">
        <header class="panel-header"><h2>Source</h2><span class="count">{{ state.sources.length }} videos</span></header>
        <div class="panel-body">
          <fieldset :disabled="!connected">
            <div class="group source-picker">
              <label for="video">Video</label>
              <select id="video" :value="state.selectedVideo" :disabled="busy || !state.sources.length"
                @change="command('video', { index: Number($event.target.value) })">
                <option v-for="video in state.sources" :key="video.index" :value="video.index">{{ video.label }}</option>
              </select>
              <div class="button-pair">
                <button :disabled="busy || !state.sources.length" @click="adjacent(-1)">← Previous</button>
                <button :disabled="busy || !state.sources.length" @click="adjacent(1)">Next →</button>
              </div>
              <p v-if="!state.sources.length" class="note">Add footage to bin/data/fps-normalized, then restart the app.</p>
              <p v-else-if="!state.videoLoaded" class="note">Video could not be loaded. Choose another source; see obj/app.log.</p>
              <dl v-if="source" class="source-meta">
                <div><dt>Dimensions</dt><dd>{{ state.metrics.width }} × {{ state.metrics.height }}</dd></div>
                <div><dt>Source rate</dt><dd>{{ source.fps.toFixed(2) }} fps</dd></div>
                <div><dt>Output</dt><dd>Native video window</dd></div>
              </dl>
            </div>
            <details class="group" open>
              <summary>Playback</summary>
              <fieldset class="choice-field"><legend>Frame rate</legend>
                <div class="segmented">
                  <button :aria-pressed="!settings.useSixtyFps" @click="edit('useSixtyFps', false)">Source</button>
                  <button :aria-pressed="settings.useSixtyFps" @click="edit('useSixtyFps', true)">60 fps</button>
                </div>
              </fieldset>
              <p class="note">The video stays in its own native window. This page controls the running app.</p>
            </details>
            <details class="group" open>
              <summary>Configuration</summary>
              <div class="config-state"><span>{{ state.dirty ? 'Unsaved session changes' : 'Configuration loaded' }}</span><span class="file-tag">TOML</span></div>
              <button class="full primary" :disabled="busy" @click="command('saveConfig')">Save config</button>
              <p class="note">Saves this layer and shared flocking values. Other layers keep their saved settings.</p>
              <p class="file-status" :class="{ 'status-error': /ERROR|failed|refused/i.test(state.settingsStatus) }">{{ state.settingsStatus }}</p>
            </details>
            <details class="group" open>
              <summary>Render queue <span class="count">{{ state.savedSpecs }} saved</span></summary>
              <button class="full" :disabled="busy || !state.videoLoaded" @click="command('saveSpec')">Add render spec</button>
              <p class="note">Adds this source, layer, and settings to the offline queue at 60 fps.</p>
              <p v-if="state.renderStatus" class="file-status">{{ state.renderStatus }}</p>
            </details>
          </fieldset>
        </div>
      </aside>

      <section class="panel effect-panel" aria-label="Layer controls">
        <header class="panel-header"><h2>Layer <span>/</span> {{ layerLabel }}</h2></header>
        <div class="panel-body">
          <fieldset :disabled="!connected">
            <div class="group layer-picker">
              <div class="layer-grid" role="group" aria-label="Active layer">
                <button v-for="[mode, label] in layers" :key="mode" :aria-pressed="settings.mode === mode"
                  :disabled="busy" @click="command('mode', { mode })">{{ label }}</button>
              </div>
            </div>
            <details class="group" open>
              <summary>Motion gate</summary>
              <div class="control-stack">
                <NumberControl name="frameOffset" label="Frame offset" unit="frames" :model-value="settings.frameOffset"
                  :min="0" :max="120" :disabled="original" @update:model-value="edit('frameOffset', $event)" />
                <label class="toggle-row"><span>Hysteresis thresholding</span><input type="checkbox" :checked="settings.hysteresisThresholding"
                  :disabled="original" @change="edit('hysteresisThresholding', $event.target.checked)" /></label>
                <label class="toggle-row"><span>Polygon gate</span><input type="checkbox" :checked="settings.polygonGate"
                  :disabled="original" @change="edit('polygonGate', $event.target.checked)" /></label>
                <NumberControl name="polygonDetail" label="Polygon detail" :model-value="settings.polygonDetail"
                  :min="0" :max="100" :step="1" :disabled="original || !(settings.polygonGate || settings.mode === 'motionPolygons')"
                  @update:model-value="edit('polygonDetail', $event)" />
                <label class="toggle-row"><span>Object cutout</span><input type="checkbox" :checked="settings.objectCutout"
                  :disabled="original" @change="edit('objectCutout', $event.target.checked)" /></label>
              </div>
              <p class="note">Compare source frames, then isolate the moving regions. Offset 0 produces no motion.</p>
            </details>
            <details class="group" open>
              <summary>Pixel treatment</summary>
              <fieldset :disabled="original || ['motionMask', 'motionPolygons'].includes(settings.mode)">
                <div class="segmented treatments" role="group" aria-label="Pixel treatment">
                  <button v-for="treatment in ['smear', 'streak', 'stretch']" :key="treatment"
                    :aria-pressed="settings.treatment === treatment" @click="edit('treatment', treatment)">{{ treatment }}</button>
                </div>
                <div class="control-stack">
                  <NumberControl name="pixelFadePercent" label="Pixel fade" unit="% / frame" :model-value="settings.pixelFadePercent"
                    :min="0" :max="100" :step="0.1" @update:model-value="edit('pixelFadePercent', $event)" />
                  <NumberControl name="pixelFlightStrength" label="Flight strength" :model-value="settings.pixelFlightStrength"
                    :min="-100" :max="100" :step="0.1" @update:model-value="edit('pixelFlightStrength', $event)" />
                  <NumberControl name="trailSpeedFactor" label="Trail speed factor" :model-value="settings.trailSpeedFactor"
                    :min="0" :max="100" :step="0.01" @update:model-value="edit('trailSpeedFactor', $event)" />
                </div>
              </fieldset>
            </details>
            <details class="group" open>
              <summary>Particles</summary>
              <div class="control-stack">
                <label class="toggle-row"><span>Coloured particles</span><input type="checkbox" :checked="settings.colouredParticles"
                  :disabled="!displacement" @change="edit('colouredParticles', $event.target.checked)" /></label>
                <label class="toggle-row"><span>Particles only</span><input type="checkbox" :checked="settings.particlesOnly"
                  :disabled="settings.mode !== 'displacedVideo' || !settings.colouredParticles" @change="edit('particlesOnly', $event.target.checked)" /></label>
                <NumberControl name="speedContrast" label="Speed contrast" :model-value="settings.speedContrast"
                  :min="0" :max="100" :step="0.1" :disabled="settings.mode !== 'displacedVideo' || !settings.colouredParticles"
                  @update:model-value="edit('speedContrast', $event)" />
                <label class="toggle-row"><span>Bake stopped pixels</span><input type="checkbox" :checked="settings.bakeStoppedPixels"
                  :disabled="!particles || settings.pixelFadePercent !== 0" @change="edit('bakeStoppedPixels', $event.target.checked)" /></label>
              </div>
              <p class="note">Set pixel fade to 0 to retain stopped particles. Speed contrast around 0.3 is already strong.</p>
            </details>
          </fieldset>
        </div>
      </section>

      <aside class="panel flock-panel" aria-label="Flocking and runtime">
        <header class="panel-header"><h2>Flocking</h2><span class="count">Shared tuning</span></header>
        <div class="panel-body">
          <fieldset :disabled="!connected">
            <div class="group">
              <label class="toggle-row"><span>Boids</span><input type="checkbox" :checked="settings.boids"
                :disabled="!particles" @change="edit('boids', $event.target.checked)" /></label>
              <p class="note">Enable per layer. The tuning below is shared by all layers and applies live.</p>
            </div>
            <details class="group" open>
              <summary>Neighbour behaviour</summary>
              <div class="control-stack">
                <NumberControl v-for="control in tuning" :key="control.name" v-bind="control" :model-value="settings[control.name]"
                  @update:model-value="edit(control.name, $event)" />
                <fieldset class="choice-field"><legend>Swirl direction</legend><div class="segmented">
                  <button :aria-pressed="settings.boidSwirlDirection === 1" @click="edit('boidSwirlDirection', 1)">Forward</button>
                  <button :aria-pressed="settings.boidSwirlDirection === -1" @click="edit('boidSwirlDirection', -1)">Reverse</button>
                </div></fieldset>
              </div>
            </details>
          </fieldset>
          <details class="group runtime" open>
            <summary>Runtime</summary>
            <dl class="source-meta">
              <div><dt>App rate</dt><dd>{{ connected ? state.metrics.fps.toFixed(1) : '—' }} fps</dd></div>
              <div><dt>Live particles</dt><dd>{{ format(state.metrics.travellers) }}</dd></div>
            </dl>
            <meter :value="state.metrics.travellers" :max="state.metrics.maxTravellers" aria-label="Live particle capacity"></meter>
            <p class="meter-caption">of {{ format(state.metrics.maxTravellers) }}</p>
            <dl class="source-meta"><div><dt>Baked particles</dt><dd>{{ format(state.metrics.baked) }}</dd></div></dl>
            <meter :value="state.metrics.baked" :max="state.metrics.maxBaked" aria-label="Baked particle capacity"></meter>
            <p class="meter-caption">of {{ format(state.metrics.maxBaked) }}</p>
          </details>
        </div>
      </aside>
    </main>
    <main v-else class="waiting"><h2>CCTV / Controls</h2><p>The controls appear when the video app connects.</p></main>
    <footer class="app-footer"><span role="status">{{ busy ? 'Applying…' : status }}</span><span>Video: native window <span class="separator">/</span> Edits: live · Save: explicit</span></footer>
  </div>
</template>
