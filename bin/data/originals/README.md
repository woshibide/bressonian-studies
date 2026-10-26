# Fireworks silent playback copy

Original with audio: `fireworks_30fps-with-audio.mp4`.
App playback copy: `../fps-normalized/fireworks_30fps.mp4`.

Conversion command, run from the project directory:

```sh
ffmpeg -nostdin -n -i bin/data/originals/fireworks_30fps-with-audio.mp4 -map 0:v:0 -c:v copy -an -movflags +faststart bin/data/fps-normalized/fireworks_30fps.mp4
```

No video filter or re-encoding was applied.
The output contains 3,300 H.264 frames at 30000/1001 FPS (29.97 FPS), with no audio stream.
All video packet hashes, presentation timestamps, decode timestamps, and durations match the original.

## 1080p playback copy

The app playback copy is now 1920×1080 with no audio.
The previous silent 3840×2160 copy is preserved as `fireworks_30fps-4k-silent.mp4`.

```sh
ffmpeg -nostdin -n -i bin/data/originals/fireworks_30fps-4k-silent.mp4 -map 0:v:0 -vf scale=1920:1080:flags=lanczos -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -fps_mode passthrough -an -movflags +faststart bin/data/fps-normalized/fireworks_30fps.mp4
```

Verified 3,300 frames at 30000/1001 FPS with unchanged presentation timestamps and frame durations.
The full output decodes without errors.
