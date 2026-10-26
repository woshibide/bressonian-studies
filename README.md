# CCTV Motion Difference

An openFrameworks and OpenCV app for exploring motion masks and travelling pixel traces in video.

## Run

From this directory, rebuild, quit the open interactive app, and relaunch it:

```sh
make dev
```

`make dev` only quits the interactive instance; headless renders keep running.
App output goes to `obj/app.log`.

## Controls

- Press `[` / `]` to play the previous / next video, wrapping at either end.
- On startup, the app scans `bin/data/fps-normalized` in filename order and reads each video's FPS from its metadata.
- Add MP4, MOV, M4V, AVI, MKV, or WebM files to that folder, then run `make dev` to include them.
- Files with unreadable metadata are skipped and logged in `obj/app.log`.
- Press Left or Right to cycle through motion threshold, inverted traces, displacement map, displaced video, and original video.
- Press Up or Down to adjust the frame offset.

The frame offset drives every layer.
The motion threshold compares each source frame with the one `offset` source frames earlier.
The other layers measure optical flow across `offset` source frames, and particles slow with a half-life of `offset` frames.
A larger offset therefore measures more motion and lets it travel further.
At offset 0 nothing moves: the displaced video equals the source, the displacement map is neutral, and the traces layer is black.
Sources are slower than the 60 FPS output, so the motion measured on one source frame is fed in evenly over the output frames until the next source frame.
Without that, fast fades pulse at the source frame rate.
- Press `F` to switch between source frame rate and 60 FPS.
- Click a float field, type a value, then press Enter to apply it; press Esc to cancel.
- A positive trail speed factor makes higher-speed pixels leave shorter trails; zero preserves the old speed-to-length calculation.
- Click **Save specs** to append the current settings and selected layer mode for the selected video to `bin/data/render-specs.json`.
- The app shows how many specs are saved for the selected video, and each save gets the next `Spec NN` name.
- Saved specs render at 60 FPS and retain their selected mode.

## Displacement maps

The displacement map stores signed horizontal offsets in red and signed vertical offsets in green.
Channel values of 128 are neutral, values above 128 move one direction, and values below 128 move the other.
Blue stores displacement magnitude for inspection or masking.
The **Displaced video** layer applies the map to the original video with edge replication.
The **Pixel treatment** dropdown picks how it does that, and saved specs store it as `treatment`:

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

The **Boids** toggle makes travelling pixels flock: they turn toward their neighbours' heading and centre, away from crowded spots, and swirl around their neighbours' centre.
Boids only turn travellers, so their speed still decays with the frame offset half-life and offset 0 still shows no effect.
It applies to the inverted grayscale traces and to coloured particles, not to field mode.
Saved specs store it as `boids`.
The tuning constants (neighbour cell size, alignment, cohesion, separation, crowd count, swirl strength and direction) live in `src/ParticleTuning.h`, together with the traveller limit, stop speed and minimum flow; edit them, then run `make dev`.
Flight strength controls how far the map pushes pixels, and pixel fade controls how quickly old motion vectors and image feedback fade.
To render the map itself, save a spec with the displacement map layer selected.

## Render queue

`bin/data/render-specs.json` is the editable render queue.
Only videos and specs listed in that file are rendered; videos omitted from it are skipped.
Clicking **Save specs** appends only the current settings for the selected video and preserves the rest of the queue.
Remove a spec or a video entry from the JSON to keep it out of the next render.

## Render

Render every queued spec to MP4 without opening a window:

```sh
make render
```

The command rebuilds first, reads the JSON at start, and renders several specs at once.
Each spec's frames are piped straight into ffmpeg (libx264, CRF 18, yuv420p), the same encode as the earlier videos; no PNG frames are written.
The video shows the layer named by the spec's `mode`.
Each run writes to a new `bin/data/renders/inverted-motion-trails/video/<timestamp>/` folder, as `<video>-spec-NN.mp4`, next to a copy of the `render-specs.json` it used.
A video is written as `.partial.mp4` and only renamed when ffmpeg finishes, so a finished name always means a complete file.
`ffmpeg` must be on `PATH`.
In a terminal, a live table shows each spec's progress, frame rate and time left, plus a total; when output is redirected to a file, plain progress lines are written instead.

Options:

- `ONLY=korea` renders one video; `ONLY=korea/2` renders one spec.
- `FRAMES=120` stops each spec after 120 output frames, for quick previews.
- `PARALLEL=6` sets how many specs render at once; the default is a third of the CPU cores.
- `OUT=/some/dir` writes the videos into that directory instead of a timestamped folder.

The trace simulation only runs when the inverted grayscale layer is shown or rendered, because no other layer reads it.
