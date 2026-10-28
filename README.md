# CCTV Motion Difference

An openFrameworks and OpenCV app for exploring motion masks and travelling pixel traces in video.

## Run

From this directory, rebuild, quit the open interactive app, and relaunch it:

```sh
make dev
```

Open **http://127.0.0.1:8190** for the Vue controls.
The app opens one **CCTV Video** window for the selected layer; the browser has controls only.
`make dev` installs the pinned frontend packages, builds the Vue assets and native app, then replaces only the interactive instance; headless renders keep running.
Node.js 22.12+ is needed to build the controls, not to run them once built.
App output goes to `obj/app.log`.

## Windows and live settings

The browser uses Baza's monochrome panel arrangement: source and configuration on the left, selected-layer controls in the middle, and shared flocking tuning plus runtime counts on the right.
Groups collapse independently and panel bodies scroll on desktop; smaller windows stack the panels.
Switch between light and dark themes in the header.
The video window fits the selected layer with black letterboxing.
Focus that native window for the existing keyboard shortcuts.
Closing it quits the app; closing a browser tab leaves playback running.

Controls apply live over a local WebSocket and are confirmed by the app.
Numeric fields accept typed values on Enter or blur; Escape restores the current value.
Use the minus/plus buttons or drag a numeric label horizontally to adjust it; hold Shift for finer dragging.
Controls that do not apply to the selected layer are disabled.
Shared boid tuning stays editable across layers.
Disconnected controls are disabled, reconnect automatically and refresh from the app; unconfirmed commands are not replayed.
Use **Save config** for interactive persistence and **Add render spec** to append an offline job.

The app serves the compiled frontend and `/ws` from loopback port 8190 using the installed Poco binary through `local-addons/ofxWebControls`.
The local addon avoids the bundled `ofxPoco` source scanner compiling PDF implementation files from its XCFramework headers.
Only the built interface assets are served; settings, media and queue files are not HTTP routes.
A port conflict is reported in `obj/app.log` and exits startup.

For Vue development with hot reload, keep the native app running and use:

```sh
npm --prefix ui run dev
```

Open the Vite URL at **http://127.0.0.1:5173**; its WebSocket proxy connects to the native app.
Run `make controls` to rebuild the assets served on port 8190 after frontend changes.

With the native app running, run the repeatable integration check:

```sh
npm --prefix ui test
```

It exercises the real HTTP server and WebSocket command path, including invalid edits, layer switching, multi-client state updates, reconnection snapshots and origin checks.
It restores the settings and layer it changes, and checks that both TOML files remain byte-for-byte unchanged.
Run it while nobody is editing controls: switching effect layers resets their particle simulation and marks session settings as unsaved.


Edit `bin/data/settings.toml` while the app runs. It checks file contents every 250 ms and applies a change after two identical reads (normally within half a second). The entire file must validate before any value changes. Invalid or missing files leave the current settings running and show an actionable error in the browser controls and `obj/app.log`. Fixing the file resumes reload automatically, including editor saves that replace the file by rename.

Click **Save config** to explicitly write the current controls to `settings.toml` using a temporary file and rename. UI changes are otherwise session-only; the next valid external edit replaces them. Save refuses a pending/unseen external edit: wait for reload, review the controls, then save again. Saving replaces comments and formatting. Restore a missing file in your editor before saving from the app.

The dependency-free parser supports a **narrow TOML subset, not full TOML**: one scalar `key = value` per line, blank lines, `#` comments, decimal numbers (including exponents), lowercase booleans, and double-quoted enum names without escapes. Use `2.5`, not the C++ literal `2.5f`; suffixes are invalid TOML. Unknown/duplicate keys or tables, arrays of scalar values, dotted/quoted keys, single-quoted/multiline strings, numeric separators and non-decimal numbers are rejected.

`[global]` requires the ten original keys below; `hysteresisThresholding`, `polygonGate`, `objectCutout` and `bakeStoppedPixels` are optional and default to `false` when absent; `polygonDetail` and `speedContrast` are optional and default to `0`. They are supported in `[global]` and per-mode overrides. Optional `[motionMask]`, `[motionPolygons]`, `[invertedGrayscale]`, `[displacementMap]`, `[displacedVideo]` and `[originalVideo]` tables override any keys except `mode`, which belongs only in `[global]`. Every override is validated against the complete globals before anything is applied, including modes not currently selected. The optional `[boids]` table holds shared tuning for all modes; its seven keys are not allowed in `[global]` or per-mode overrides. The existing `boids` boolean in `[global]` and per-mode tables remains the enable switch. Legacy complete flat settings are still accepted; Save config writes tables.

```toml
[global]
frameOffset = 3
pixelFadePercent = 2.5
pixelFlightStrength = 1.0
trailSpeedFactor = 0.15
mode = "motionMask"
treatment = "smear"
colouredParticles = false
particlesOnly = false
boids = false
useSixtyFps = false
hysteresisThresholding = false
polygonGate = false
polygonDetail = 0
objectCutout = false
speedContrast = 0
bakeStoppedPixels = false

[boids]
boidCellSize = 16
boidAlignment = 0.8
boidCohesion = 0.04
boidSeparation = 0.10
boidCrowdCount = 24
boidSwirl = 0.1
boidSwirlDirection = 1

[motionMask]
frameOffset = 5
pixelFadePercent = 2.5
treatment = "streak"
```

Switching layers resolves that mode's globals plus overrides. UI edits are remembered per mode for the session; valid external reloads replace session edits and select `[global].mode`. **Save config** saves only the selected mode's controls as overrides and updates `[global].mode`. It also captures the current shared `[boids]` tuning, preserving global defaults and other modes' effective values, not their formatting; unsaved per-mode edits in other modes remain session-only. Invalid disk contents cannot be saved over from the app.

| Key | Accepted values |
| --- | --- |
| `frameOffset` | Integer, 0–120 source frames |
| `pixelFadePercent` | Finite number, 0–100 |
| `pixelFlightStrength` | Finite number, −100–100 (negative reverses flight) |
| `trailSpeedFactor` | Finite number, 0–100 |
| `mode` | `"motionMask"`, `"motionPolygons"`, `"invertedGrayscale"`, `"displacementMap"`, `"displacedVideo"`, `"originalVideo"` |
| `treatment` | `"smear"`, `"streak"`, `"stretch"` |
| `colouredParticles`, `particlesOnly`, `boids`, `useSixtyFps` | `true` or `false` |
| `hysteresisThresholding`, `polygonGate`, `objectCutout`, `bakeStoppedPixels` | Optional `true` or `false`; default to `false` |
| `polygonDetail`, `speedContrast` | Optional finite number, 0–100; defaults to `0` |

Shared `[boids]` tuning accepts the following optional keys. Missing keys, or a missing table, use the original defaults shown here. Numeric values must be finite.

| Key | Default | Accepted values and meaning |
| --- | --- | --- |
| `boidCellSize` | `16` | Integer, 8–512 pixels; neighbour grid cell size (lower bound limits grid memory use) |
| `boidAlignment` | `0.8` | 0–100; turn toward neighbours' heading |
| `boidCohesion` | `0.04` | 0–100; turn toward neighbours' centre |
| `boidSeparation` | `0.10` | 0–100; turn away from crowded spots |
| `boidCrowdCount` | `24` | 1–1,000,000; neighbour count used to scale crowd avoidance |
| `boidSwirl` | `0.1` | 0–100; swirl strength around neighbours' centre |
| `boidSwirlDirection` | `1` | Exactly `-1` or `1`; reverses the swirl direction |

Tuning changes hot-apply to all modes without resetting existing particles. As with other settings, every table and mode must validate before a reload applies; an invalid tuning value leaves the entire current configuration running.

Interactive values persist when switching videos; startup no longer takes effect parameters from the last saved render spec. **Add render spec** remains separate: it appends a snapshot, including all seven shared boid tuning values as flat fields, to the TOML render queue. `--render` reads only that queue and ignores `settings.toml`, so later interactive tuning changes do not change saved renders. The remaining traveller constants (traveller limit, stop speed and minimum flow) stay compile-time in `src/ParticleTuning.h`.

Playback, simulation (once per update tick), texture uploads, particle FBO work and presentation use the native video window's GL 2.1 context on the main thread.
Network threads exchange bounded text commands and snapshots only.
The app drains commands in `update()`, validates complete settings before applying them and publishes state at most every 200 ms plus command confirmations.
The video context is made current for input-triggered video changes and resource cleanup.

## Controls

- Press `[` / `]` to play the previous / next video, wrapping at either end.
- On startup, the app scans `bin/data/fps-normalized` in filename order and reads each video's FPS from its metadata.
- Add MP4, MOV, M4V, AVI, MKV, or WebM files to that folder, then run `make dev` to include them.
- Files with unreadable metadata are skipped and logged in `obj/app.log`.
- Press Left or Right to cycle through motion threshold, motion polygons, inverted traces, displacement map, displaced video, and original video.
- Press Up or Down to adjust the frame offset.

The frame offset drives every layer.
The motion threshold compares each source frame with the one `offset` source frames earlier.
All effects except original video use this shared motion gate. With `hysteresisThresholding = false`, it retains the old strict difference threshold of `> 24`. With `true`, hysteresis uses a low threshold of `> 12` and a high threshold of `> 24`: it keeps only 4-connected spatial components of pixels above the low threshold that contain at least one pixel above the high threshold, matching scikit-image's hysteresis behavior. Connectivity is spatial within each difference frame, not temporal.
The **Polygon gate** toggle (`polygonGate`) adds one step after that threshold, before every effect.
It traces the thresholded mask, not the source video, so static edges in the scene never gate anything.
The mask is closed with an elliptical kernel of 1/60 of the frame width so fragments of one moving body merge.
Outer contours under 0.05% of the frame are dropped, and the rest are simplified with `approxPolyDP` to within 1.5% of each outline's length.
The filled polygons, holes included, then replace the mask as the gate, so effects move whole regions instead of their speckled edges.
The motion threshold layer shows the gate itself, so with the toggle on it shows the filled polygons.
The **Motion polygons** layer shows the step over the dimmed source: thresholded pixels in grey and the traced polygons with their vertices.
It always traces the polygons; they are blue while the gate is off and filled amber while it gates the effects.
The **Polygon detail** field (`polygonDetail`, 0–100) scales the closing kernel, minimum area and `approxPolyDP` tolerance by `1 - detail / 100`.
At 0 the outlines are coarse and merged; at 100 nothing is merged, dropped or simplified, so every pixel step of the mask becomes an edge.
Vertex dots are drawn only on coarse polygons, where corners are at least 8 line widths apart on average.
The frame difference covers an object where it is now, where it was `offset` frames ago, and its shadow.
The **Object cutout** toggle (`objectCutout`) keeps only the object as it is in the current frame.
It feeds each source frame once to OpenCV's `BackgroundSubtractorMOG2` (500 frames of history, variance threshold 16, shadow detection on), at most 960 pixels wide.
Inside the gate from the steps above, only pixels the model marks as foreground pass; its shadow pixels and background pixels drop out.
The model restarts when the toggle changes, the video changes, or a render job starts, so the first second or so after that is noisy while it learns the background.
Its shadow test marks pixels that are darker than the background with similar colour, so dark objects on a lighter road are partly marked as shadow too.
With the cutout on, the **Motion polygons** layer shows object pixels in source colour and shadow pixels in violet inside the gated region, instead of the grey threshold and amber fill.
The other layers measure optical flow across `offset` source frames, and particles slow with a half-life of `offset` frames.
A larger offset therefore measures more motion and lets it travel further.
At offset 0 nothing moves: the displaced video equals the source, the displacement map is neutral, and the traces layer is black.
Sources are slower than the 60 FPS output, so the motion measured on one source frame is fed in evenly over the output frames until the next source frame.
Without that, fast fades pulse at the source frame rate.
- Press `F` to switch between source frame rate and 60 FPS.
- Type a numeric value and press Enter or leave the field to apply it; press Esc to cancel.
- A positive trail speed factor makes higher-speed pixels leave shorter trails; zero preserves the old speed-to-length calculation.
- Click **Add render spec** to append the current settings and selected layer mode for the selected video to `bin/data/render-specs.toml`.
- The app shows how many specs are saved for the selected video, and each save gets the next `Spec NN` name.
- Saved specs render at 60 FPS and retain their selected mode.

## Displacement maps

The displacement map stores signed horizontal offsets in red and signed vertical offsets in green.
Channel values of 128 are neutral, values above 128 move one direction, and values below 128 move the other.
Blue stores displacement magnitude for inspection or masking.
The **Displaced video** layer applies the map to the original video with edge replication.
The **Pixel treatment** buttons picks how it does that, and saved specs store it as `treatment`:

- `smear` warps the previous output again and blends it in with decaying feedback, so moved pixels drag paint behind them.
- `streak` averages 12 samples of the current frame along each pixel's displacement path, which draws lines along the motion.
- `stretch` warps only the current frame, so pixels behind motion repeat and the image stretches without trails.

Specs without `treatment` use `smear`.

The **Coloured particles** toggle switches both displacement layers from pulling through the field to pushing pixels, like the inverted grayscale traces.
Every moving pixel becomes a particle that carries its colour and travels at its flow speed, slowing with a half-life of `frameOffset` frames.
Each particle and grayscale trace fades by the pixel fade every frame from the moment it leaves its origin, and ends once it is invisible or stops.
With particles, `smear` paints particles onto a frame that fades toward the current video, `streak` draws a fading trail sized by the trail speed factor, and `stretch` draws each pixel along its whole path from where it started, up to 256 pixels.
The displacement map shows each particle's travel since it started, using the same colour encoding.
Saved specs store the toggle as `colouredParticles`, and specs without it use the field.
The **Particles only** toggle works with coloured particles and puts the displaced video on black, so only the flying pixels and their own smear paint show.
Saved specs store it as `particlesOnly`.
The **Speed contrast** field (`speedContrast`, 0–100) contrast-stretches each coloured particle by its current speed.
Every channel moves away from mid grey by a gain of `1 + speedContrast * speed`, where speed is in pixels per output frame including flight strength, and clips at 0 and 255.
Fast particles therefore compress toward vivid, saturated extremes, and relax back to their source colour as they slow.
It applies to coloured particles on the displaced video, not to grayscale traces or the displacement map colours. 0 turns it off; around 0.3 is already strong.

### Pixel fade 0

A pixel fade of exactly 0 means nothing fades.
Above 0, smear always lets at least 2% of the live frame back in per frame, as before.
At 0:

- Field smear (coloured particles off) only re-warps its own previous output, so the image freezes and moved content keeps sliding toward the edges.
- Coloured smear paints a persistent layer composited over the live video (or black with **Particles only**), so the video keeps playing and paint never fades.
- Grayscale traces already accumulate without fading.

Travellers still slow with the `frameOffset` half-life and leave the simulation below the stop speed.
The **Bake stopped pixels** toggle (`bakeStoppedPixels`) keeps them on screen: a stopped traveller's last drawn shape (point, streak or stretch line, or map colour) stays where it stopped and is redrawn every frame.
It takes effect only on layers that redraw from scratch each frame: displaced video streak and stretch, and the displacement map.
Smear and grayscale traces already keep every pixel at fade 0.
Baked travellers have their own limit, `maxBakedTravellers` (10,000,000) in `src/ParticleTuning.h`; beyond it the oldest are dropped first.
Busy footage such as daegu bakes about 55,000 travellers per frame, so the limit holds roughly the last 3 seconds of stops.
Baking is only available at fade 0: the toggle is dimmed above it, typing a fade above 0 switches it off, and a settings file or spec with `bakeStoppedPixels = true` and a fade above 0 loads with baking off.
Baked travellers clear with the other travellers when the video, layer, frame offset, treatment or an effect toggle changes.
The controls show live and baked counts against their limits.

The **Boids** toggle makes travelling pixels flock: they turn toward their neighbours' heading and centre, away from crowded spots, and swirl around their neighbours' centre.
Boids only turn travellers, so their speed still decays with the frame offset half-life and offset 0 still shows no effect.
It applies to the inverted grayscale traces and to coloured particles, not to field mode.
Saved specs store it as `boids`.
Edit the shared `[boids]` table in `settings.toml` to tune neighbour cell size, alignment, cohesion, separation, crowd count, swirl strength and direction live, using the ranges above. These values are shared across modes, while each mode can independently enable or disable flocking with its `boids` boolean.
Flight strength controls how far the map pushes pixels, and pixel fade controls how quickly old motion vectors and image feedback fade.
To render the map itself, save a spec with the displacement map layer selected.

## Render queue

`bin/data/render-specs.toml` is the editable render queue.
Only videos and specs listed in that file are rendered; videos omitted from it are skipped.
Clicking **Add render spec** appends only the current settings for the selected video and preserves the rest of the queue.
Remove a spec or a video entry from the TOML to keep it out of the next render.
An empty or comments-only TOML file is a cleared queue: rendering has no jobs, and **Add render spec** starts a new queue with the current spec. It does not reload entries from legacy JSON. Other non-empty queues still require `version = 1` and valid entries.

### Queue schema and migration

The offline schema is independent of interactive settings: `fadePercent`, `flightStrength` and `fps` are render fields, not interactive key aliases. The root requires `version = 1`; `[[videos]]` creates video entries and `[[videos.specs]]` appends specs to the most recent video. `source` is required; optional `input` matches an exact data-relative path, otherwise `source` matches the discovered label. Entries for unavailable videos remain in the queue but are not rendered.

```toml
version = 1

[[videos]]
source = "Fireworks"
input = "fps-normalized/fireworks_30fps.mp4"

[[videos.specs]]
name = "Spec 01"
fps = 60.0
mode = "invertedGrayscale"
frameOffset = 3
fadePercent = 2.5
flightStrength = 1.0
trailSpeedFactor = 0.15
treatment = "smear"
colouredParticles = false
particlesOnly = false
boids = false
boidCellSize = 16
boidAlignment = 0.8
boidCohesion = 0.04
boidSeparation = 0.10
boidCrowdCount = 24
boidSwirl = 0.1
boidSwirlDirection = 1
hysteresisThresholding = false
polygonGate = false
polygonDetail = 0
objectCutout = false
speedContrast = 0
bakeStoppedPixels = false
```

Spec fields are optional for historical compatibility: defaults are the example above except `fadePercent = 15`. The optional booleans `hysteresisThresholding`, `polygonGate`, `objectCutout` and `bakeStoppedPixels` default to `false` and `polygonDetail` and `speedContrast` (0–100) default to `0` for existing specs; `true` enables the same shared motion gate steps described above. `name` is preserved metadata; CLI selection and filenames use queue position. Render `trailSpeedFactor` accepts finite nonnegative values, including existing values above the interactive limit of 100. `flightStrength` accepts any finite float, `fps` must be positive, `fadePercent` is 0–100 and `frameOffset` is an integer 0–120. Modes, treatments and booleans use the same choices as above. The seven optional boid tuning fields are flat keys in each `[[videos.specs]]`, not a `[boids]` table; they use the same types and ranges as the interactive tuning table. Each omitted tuning field uses its original default above, independently of interactive settings and other specs. **Add render spec** captures all seven values even when `boids = false`; the boolean remains the enable switch. Queue strings additionally accept JSON-compatible double-quoted escapes (for paths/names); other TOML string forms remain unsupported. Unknown fields are rejected rather than silently discarded. Validation covers the entire queue, including unavailable videos; a malformed queue aborts rendering or Save specs with an error.

When a requested `.toml` queue is absent, the app looks for the same basename with `.json`, validates and converts it to TOML, and retains the original unchanged. This also supports the historical single-spec video shape. Existing TOML always wins, even when invalid: there is no fallback to stale JSON. Save specs re-reads the complete queue and atomically replaces it after appending, preserving every existing entry, field, custom name and ordering, including unavailable videos. Comments and formatting are normalized. Render runs archive the TOML queue next to their output.

## Render

Render every queued spec to MP4 without opening a window:

```sh
make render
```

The command rebuilds first, reads the TOML at start, and renders several specs at once.
Each spec's frames are piped straight into ffmpeg (libx264, CRF 18, yuv420p), the same encode as the earlier videos; no PNG frames are written.
The video shows the layer named by the spec's `mode`.
Each run writes to a new `bin/data/renders/inverted-motion-trails/video/<timestamp>/` folder, as `<video>-spec-NN.mp4`, next to a copy of the `render-specs.toml` it used.
A video is written as `.partial.mp4` and only renamed when ffmpeg finishes, so a finished name always means a complete file.
`ffmpeg` must be on `PATH`.
In a terminal, a live table shows each spec's progress, frame rate and time left, plus a total; when output is redirected to a file, plain progress lines are written instead.
When a terminal run finishes with at least one video, the output folder opens in Finder.

Options:

- `ONLY=korea` renders one video; `ONLY=korea/2` renders one spec.
- `FRAMES=120` stops each spec after 120 output frames, for quick previews.
- `PARALLEL=6` sets how many specs render at once; the default is a third of the CPU cores.
- `SPECS=/path/to/queue.toml` chooses a queue (`--render --specs /path/to/queue.toml` directly); JSON paths are migration inputs only, not accepted CLI queues.
- `OUT=/some/dir` writes the videos into that directory instead of a timestamped folder.

The trace simulation only runs when the inverted grayscale layer is shown or rendered, because no other layer reads it.
