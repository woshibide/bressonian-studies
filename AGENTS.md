# Project guide

## Purpose

This openFrameworks app is a workspace for experimenting with video pixel manipulation using OpenCV.

Source footage lives at `/Users/pyotr/Downloads/opencv/videos/sources`.
Keep source footage unchanged and put generated media under `bin/data` or the shared videos workspace's `renders` directory.

## Development

- Use the installed openFrameworks 0.12.1 environment and the existing `ofxOpenCv` addon.
- Keep experiments small and visible in the running app.
- Preserve the app's existing build setup; avoid adding dependencies when openFrameworks or OpenCV already provides the needed function.
- Treat frame rate and timestamps as media properties, not the macOS display refresh rate.
- Before converting footage, inspect stream metadata and frame timestamps.
- For screen recordings containing repeated CCTV frames, make a derived copy that removes duplicate frames while preserving presentation timestamps. Do not overwrite originals.
- Record each conversion's filter, output path, and resulting frame count/rate so the result can be reproduced.

## Working agreement

- Make the smallest complete change and explain what changed and how to use it.
- After every code edit, rebuild and launch this app yourself, then inspect the running result before reporting completion. If the environment blocks rebuilding or launching, state the exact blocker and do not claim the app was verified.
- Do not add tests unless requested.
- Keep comments factual and emoji-free.
- Never edit generated files or changelogs by hand.
