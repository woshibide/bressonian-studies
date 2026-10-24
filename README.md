# CCTV Motion Difference

An openFrameworks and OpenCV app for exploring motion masks and travelling pixel traces in video.

## Run

From this directory, build and launch the app:

```sh
make Release -j4
open bin/helloOpenCv.app
```

Quit any already-running copy before reopening after a rebuild, so macOS does not reuse the old process.

To launch the existing build without rebuilding, run `open bin/helloOpenCv.app` or open `bin/helloOpenCv.app` in Finder.

## Controls

- Press `1`–`5` to select a video.
- Press Left or Right to cycle through motion threshold, inverted traces, and original video.
- Press Up or Down to adjust the frame offset.
- Press `F` to switch between source frame rate and 60 FPS.
- Click a float field, type a value, then press Enter to apply it; press Esc to cancel.
- A positive trail speed factor makes higher-speed pixels leave shorter trails; zero preserves the old speed-to-length calculation.
- Click **Save specs** to append the selected video's current offset, fade, flight strength, and trail speed factor to `bin/data/render-specs.json`.
- The app shows how many specs are saved for the selected video, and each save gets the next `Spec NN` name.
- The render specs file sets every output to 60 FPS in inverted grayscale mode.
- Press `R` to render every saved spec for every video into its own numbered folder. Rendering is not started when the app opens.

## Prepared render settings

| Video | Offset | Fade (% / frame) | Flight strength | Trail speed factor |
| --- | ---: | ---: | ---: | ---: |
| Cercany Spec 01 | 3 | 2.50 | 1000 | 0.15 |
| Cercany Spec 02 | 60 | 2.50 | 1000 | 0.15 |
| Cercany Spec 03 | 60 | 30 | -20 | 0.15 |
| Daegu Spec 01 | 3 | 100 | -1 | 0.15 |
| Korea Spec 01 | 3 | 75 | -50 | 0.15 |
| Meagan Spec 01 | 15 | 100 | -50 | 0.15 |
| Meagan Spec 02 | 15 | 98 | -0.2 | 0.15 |
| Meagan Spec 03 | 15 | 60 | -50 | 0.15 |
| Meagan Spec 04 | 15 | 99 | -0.01 | 0.15 |
| Norway Spec 01 | 1 | 99 | -1 | 0.15 |
| Norway Spec 02 | 3 | 100 | -0.02 | 0.15 |
