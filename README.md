# POCKETWORK v0.2

Free VST3 MIDI-groove/extraction plugin. Original implementation — not a clone
of any proprietary product; screenshots referenced are for workflow/visual
inspiration only.

## What's real in this build
- Sensitivity, Pocket, and Dynamics knobs are now genuine, automatable plugin
  parameters (via JUCE's AudioProcessorValueTreeState) — moving them changes
  real values, and a DAW can automate/save/recall them correctly.
- MIDI export produces a real, valid .mid file with Pocket timing offsets and
  Dynamics-scaled velocity applied, on GM channel 10.
- Live MIDI passthrough works (notes flow through the plugin unmodified).

## What's NOT real yet (on purpose, not hidden)
- Pocket/Dynamics are NOT yet applied to live MIDI as it plays — only to
  exported files. Doing this properly requires reading the host's tempo and
  playback position (PPQ) to know where each note sits on the grid. That's a
  real feature for a future milestone, not something to fake here.
- Grid selector, REC, CLICK, and count-in controls are not in this build.
  They were left out entirely rather than added as non-functional buttons.
- The custom Pocket/Velocity/Groove DNA visual displays are not built yet —
  current UI shows labeled panel outlines only.
- No MIDI event selection/editing (click, drag, lasso) yet.
- No audio-to-MIDI extraction yet — this remains MIDI-only.

## Build
This project no longer requires installing JUCE by hand — CMake downloads it
automatically the first time you configure the project (see CMakeLists.txt).

### Locally (Windows + Visual Studio + CMake)
1. Configure: `cmake -B build`
2. Build: `cmake --build build --config Release`
3. Find `POCKETWORK.vst3` inside the `build` folder and copy it to your
   system VST3 folder (typically `C:\Program Files\Common Files\VST3`).

### Automatically (GitHub Actions)
Every push to the `main` branch triggers a cloud build on a Windows runner.
The finished `POCKETWORK.vst3` is attached as a downloadable build
artifact on the Actions tab of the repository — no local install required.
