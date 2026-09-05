# POCKETWORK v0.4

Free VST3 groove-extraction/pocket-editing plugin. Original implementation —
not a clone of any proprietary product; reference screenshots were used for
workflow/visual inspiration only.

## What's real in this build
- A real instrument plugin (shows up in FL Studio's Channel Rack directly,
  no Patcher workaround needed) with genuine stereo audio output.
- Load any WAV/AIFF/MP3/FLAC breakbeat and play it back ("Load Breakbeat...",
  "Play Both").
- Real onset/transient detection ("Detect Groove") — genuinely analyzes the
  loaded audio's energy envelope to find where hits occur and how loud they
  are. Not faked.
- Source BPM / Bars controls let you tell the analyzer the file's real
  musical length, so detected hits map onto the correct grid step instead of
  assuming exactly one bar.
- While "Play Both" plays the breakbeat's real audio, it ALSO sends MIDI
  trigger notes at each hit's exact real position in the audio — ready to
  trigger your own drum instrument in sync with the original break.
- Sensitivity, Pocket, and Dynamics are genuine, automatable plugin
  parameters (via JUCE's AudioProcessorValueTreeState).
- MIDI export produces a real, valid .mid file with Pocket timing offsets
  and Dynamics-scaled velocity applied, on GM channel 10.
- Live-played MIDI passthrough with real-time swing/Pocket applied to
  off-beat 16th notes (confirmed working via host tempo/position).

## What's NOT real yet (on purpose, not hidden)
- No per-drum-type classification — every detected hit (from breakbeat
  analysis) uses the same placeholder note (38). Telling a kick from a
  snare from a hi-hat within a mixed breakbeat is real, harder audio
  classification work for a future milestone.
- No automatic tempo detection — Source BPM/Bars are entered manually.
- No sample-rate conversion on breakbeat playback — if the loaded file's
  sample rate doesn't match the project's, playback speed/pitch will be
  slightly off.
- Breakbeat playback is one-shot, not looped, for now.
- Grid selector, REC, CLICK, and count-in controls are not in this build.
- The custom Pocket/Velocity/Groove DNA visual displays are not built yet —
  current UI shows labeled panel outlines only.
- No MIDI event selection/editing (click, drag, lasso) yet.

## Known reminder
Before finalizing the visual design, rename UI labels that came directly
from the reference tool's screenshots ("THE POCKET," "GROOVE DNA,"
"Sensitivity," "Export Map") to original wording.

## Build
This project downloads JUCE automatically via CMake FetchContent — no
manual JUCE install needed, locally or in the cloud.

### Automatically (GitHub Actions)
Every push to the `main` branch triggers a cloud build on a Windows runner.
The finished `POCKETWORK.vst3` is attached as a downloadable build artifact
on the Actions tab of the repository.

### Locally (Windows + Visual Studio + CMake)
1. Configure: `cmake -B build`
2. Build: `cmake --build build --config Release`
3. Find `POCKETWORK.vst3` inside the `build` folder and copy it to your
   system VST3 folder (typically `C:\Program Files\Common Files\VST3`).

## FL Studio setup notes
- Load POCKETWORK from the Channel Rack's instrument browser (not the
  Mixer). If it doesn't appear there after installing, run Options >
  Manage Plugins with "Verify plugins" checked and "Start scan."
- To hear the extracted groove trigger your own drum sounds, that
  instrument needs a sound mapped to GM note 38 (the current placeholder
  note for all detected hits).
