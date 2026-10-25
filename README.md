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

- Press `1`–`6` to select a video (`6` selects Flower).
- Press Left or Right to cycle through motion threshold, inverted traces, displacement map, displaced video, and original video.
- Press Up or Down to adjust the frame offset.
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
The **Displaced video** layer applies the map to the original video with edge replication and decaying frame feedback for smearing.
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
