# POCKETWORK v0.5

Free VST3 groove-extraction plugin. Original implementation — not a clone of
any proprietary product.

## What's real in this build
- Real instrument plugin (Channel Rack in FL Studio, no Patcher needed).
- Load a breakbeat, play it back ("Load Breakbeat...", "Play Both").
- Real onset/transient detection ("Detect Groove") with heuristic
  kick/snare/hat classification based on frequency content — a genuine,
  working technique, not machine learning, and not perfect on unusual or
  heavily layered material.
- Source BPM / Bars controls map detected hits onto the correct grid.
- Load your OWN kick/snare/hat one-shot samples directly into the plugin
  ("Load Kick/Snare/Hat..."). While "Play Both" plays the breakbeat's real
  audio, it ALSO triggers your loaded samples in sync with each detected
  hit — entirely self-contained, no MIDI-out or external instrument/Patcher
  routing required. This matches how the reference tool most likely works.
- Live MIDI (e.g. from a keyboard) also triggers your loaded samples,
  humanized by the same swing/Dynamics engine.
- Sensitivity, Pocket, Dynamics — genuine automatable parameters.
- MIDI export produces a real, valid .mid file (GM channel 10, correct
  per-drum GM notes now: kick 36 / snare 38 / hat 42).

## What's NOT real yet (on purpose, not hidden)
- Drum classification is a simple heuristic (low vs high frequency energy),
  not machine learning — it WILL misclassify some hits.
- No automatic tempo detection — BPM/Bars are manual.
- No sample-rate conversion on playback.
- Breakbeat playback is one-shot, not looped.
- Grid selector, REC, CLICK, count-in controls not built yet.
- Custom Pocket/Velocity/Groove DNA visual displays not built yet.
- No MIDI event selection/editing (click, drag, lasso) yet.

## Known reminder
Before finalizing the visual design, rename UI labels that came directly
from the reference tool's screenshots ("THE POCKET," "GROOVE DNA,"
"Sensitivity," "Export Map") to original wording.

## Build
Downloads JUCE automatically via CMake FetchContent.

### Automatically (GitHub Actions)
Every push to `main` triggers a cloud build. Download the finished
`POCKETWORK.vst3` from the Actions tab's Artifacts section.

### Locally (Windows + Visual Studio + CMake)
1. `cmake -B build`
2. `cmake --build build --config Release`
3. Copy `POCKETWORK.vst3` from the `build` folder to your VST3 folder.

## FL Studio setup
Load from the Channel Rack (not the Mixer). If it doesn't appear there,
run Options > Manage Plugins with "Verify plugins" checked, then rescan.

## v0.6 additions
- Loading a breakbeat or sample now opens a browser with a live preview
  panel — click any file to hear it before committing to load it. This
  uses its own independent, separate audio connection purely for preview
  (not routed through the host), so it needs to be able to open a
- Preview now explicitly prefers WASAPI shared mode over ASIO for its
  connection, specifically to avoid conflicting with a DAW's exclusive-mode
  ASIO driver. If it still cannot get a device (rare), the preview panel
  says so plainly instead of staying silently quiet — loading/classifying
  the file is unaffected either way.
- The browser now remembers the last folder used (for both breakbeats and
  samples) and starts there next time, instead of resetting to the Music
  folder each time. This persists only for the current plugin session —
  it resets if you reload the project or reopen the plugin.

## v0.8 fixes
- Fixed a real crash: the sample preview player was destroying the
  previous preview's audio data before properly telling the playback
  engine to release it first — an intermittent use-after-free that could
  crash FL Studio after clicking through several preview files. Fixed by
  correcting the detach/destroy order.
- The last-browsed folder (for breakbeats, samples, AND MIDI export) is
  now saved with the plugin's own state, so it persists across project
  reloads — not just within the current editing session. First-ever load
  (no saved state yet) still defaults sensibly to the Music folder.

## v0.9 additions
- Full visual trim editor: loading a breakbeat or sample now shows a real
  waveform with two draggable green handles (start/end). The selection
  loops automatically so you can audition exactly the section you want
  before clicking "Load Trimmed Selection." Double-clicking a file still
  loads the whole thing, no trim, like a normal file dialog.
- "Clear" button next to each Kick/Snare/Hat slot to unload a sample
  without needing to overwrite it with a different file.
- Breakbeat/groove playback now LOOPS continuously (both "Play Both" and
  "Play MIDI") until you click the button again to stop, instead of
  playing once and stopping.
- "Play MIDI" is now wired up: it triggers ONLY your loaded kick/snare/hat
  samples using the extracted groove's exact timing, WITHOUT the original
  breakbeat audio underneath — useful for judging how your own sounds
  carry the groove on their own. Starting either "Play Both" or "Play
  MIDI" automatically stops the other, since they share the same
  playback position.
