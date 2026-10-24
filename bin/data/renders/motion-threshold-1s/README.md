# One-second motion threshold renders

Each output compares the current frame with the frame one target-second earlier.
Frames are resampled to the app target FPS, then processed with OpenCV `absdiff`, grayscale conversion, and a binary threshold of 24.
The first second is black while the frame history fills; output duration matches the resampled source duration.

| Clip | Measured average FPS | Target FPS | Frames | Output |
|---|---:|---:|---:|---|
| Cercany | 15.73 | 16 | 1751 | `cercany-motion-1s.mp4` |
| Daegu | 15.04 | 15 | 503 | `daegu-motion-1s.mp4` |
| Korea | 15.00 | 15 | 800 | `korea-motion-1s.mp4` |
| Meagan | 27.59 | 28 | 1641 | `meagan-motion-1s.mp4` |
| Norway | 45.44 | 45 | 1440 | `norway-motion-1s.mp4` |
