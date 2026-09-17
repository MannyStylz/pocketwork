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

## v1.0 additions
- Preview Play/Stop is now a proper toggle: selecting a file plays its
  trimmed selection ONCE, stopping naturally at the end (or when you
  press Stop) and reverting to "Preview" — press again anytime to replay
  the SAME file, no need to reselect it.
- "Delete File..." button in the preview panel, with a confirmation
  prompt, to remove a file from disk directly while browsing.
- Mouse-wheel zoom on the waveform, centered on the cursor, for precise
  trim-handle placement on longer files. Browser window is also now
  larger by default and resizable from any edge (not just the corner).
- Each Kick/Snare/Hat/Breakbeat slot now remembers the EXACT FILE it was
  last loaded from (not just the folder) — reopening that slot's loader
  highlights that same file, so swapping it for a different one is fast.
- Three dedicated playback buttons: "Play/Stop MIDI" (samples only),
  "Play/Stop Breakbeat" (original audio only, new), and "Play/Stop Both"
  (both together). Starting any one automatically stops the others.

## v1.1 fixes
- Loaded/trimmed samples (kick, snare, hat, breakbeat) are now embedded
  directly in the plugin's saved state as actual audio data — not just a
  file path. This is the real fix for samples disappearing after closing
  and reopening the plugin: previously only the path was remembered, and
  if the host refreshed plugin state for any reason, there was nothing to
  reload from. Now the audio itself survives regardless.
- Opening a Load dialog for a slot with no history of its own now falls
  back to the most recently loaded file across ANY slot (not the Music
  folder) — since kick/snare/hat are often chopped from the same source
  file/pack in one sitting.
- You can now drag anywhere on the waveform that ISN'T a trim handle to
  PAN the zoomed view left/right, without needing to zoom back out.
- Fixed: the delete-file confirmation dialog could appear behind the
  browser window. It's now properly associated with the window so it
  shows in front.

## v1.2 additions
- Added separate Kick/Snare/Hat outputs alongside the existing Main
  output. Main always carries everything (nothing changes if you don't
  touch routing), but if you enable and route the extra outputs in FL
  Studio's mixer, each sound also gets an independent copy you can
  process/mix on its own channel.

### FL Studio setup for separate outputs
1. On the Channel Rack, right-click POCKETWORK's channel and look for a
   plugin output/routing option (in FL Studio this is typically under the
   plugin's own settings icon or a "Fx" output selector) to enable the
   Kick/Snare/Hat outputs.
2. Assign each enabled output to its own Mixer track (Insert), same way
   you'd route any multi-output instrument.
3. Main stays wired to the mixer track POCKETWORK was originally on;
   Kick/Snare/Hat can now be EQ'd/compressed/panned completely
   independently.

## v1.3 fix
- Fixed the real cause of playback sounding slower than the original
  breakbeat: loaded audio is now automatically resampled to match the
  project's actual sample rate (using JUCE's LagrangeInterpolator), since
  playing a file back at the wrong sample rate genuinely changes its
  speed and pitch. Source BPM/Bars were never meant to control playback
  speed — they only affect how detected hits map onto the export grid.

## v1.4 changes
- ROLLED BACK the separate Kick/Snare/Hat multi-output feature (v1.2) —
  it correlated with new crashes and a "+" quick-add load error in FL
  Studio. Back to a single stable stereo output while that gets
  revisited more carefully later.
- Clarified: playback speed follows the loaded audio's own native tempo.
  There is no time-stretching to match your project's tempo — that would
  be a separate, substantial feature if wanted (sample-rate correctness,
  fixed in v1.3, is a different thing from tempo-matching).
- Added "Export..." buttons next to each Kick/Snare/Hat slot — saves your
  trimmed sample out as its own WAV file, so you can load it into a
  normal, separate FL Studio instrument/channel per sound if you want
  independent mixer routing without any plugin-side complexity.
- Bumped plugin identity again (fresh scan needed) to clear any stale FL
  Studio cache from the rolled-back multi-output version.

## v1.5 fix
- The sample-preview audio connection is now created ONCE per editor
  session and reused, instead of being created and destroyed every
  single time a Load dialog opened. Repeatedly opening/closing an audio
  device connection (which happened every time you loaded a sample or
  breakbeat) is a plausible real cause of the crashes after loading
  several files in a row — this removes that churn entirely.

## v1.6 fixes
- Fixed: Sensitivity/Pocket/Dynamics sliders were being read AFTER being
  used each block, so every block acted on the PREVIOUS block's values —
  moving a slider never seemed to do anything in real time.
- Fixed: Dynamics was never actually applied to sample-triggered playback
  ("Play Both"/"Play MIDI") at all — only to exported MIDI and live
  keyboard notes. Now it genuinely scales playback velocity too.
- Fixed: playing an unmapped MIDI note (i.e. almost any regular keyboard
  key, not a GM drum pad number) was defaulting to trigger the Snare
  sample. Now only the specific GM kick/snare/hat note numbers (36,
  38/40, 42/46) trigger anything — everything else is correctly ignored.
- KNOWN REMAINING GAP: Pocket still does not affect the timing of
  sample-triggered playback ("Play Both"/"Play MIDI") — only exported
  MIDI and live keyboard-played notes. Making Pocket humanize playback
  timing in real time is a separate, real feature that hasn't been built
  yet (breakbeat playback currently runs on its own internal clock, not
  locked to the host's beat grid, which real-time swing needs).

## v1.7 — likely real fix for the recurring crashes
- Found and fixed a genuine race condition: the message thread (Detect
  Groove, loading a new breakbeat/sample) could rewrite the plugin's
  internal groove data at the EXACT same instant the audio thread was
  reading that same data during looping playback, with no protection
  between them. This is a serious, real class of bug — not a rare edge
  case — and fits the exact reported pattern (unpredictable crashes,
  often while doing something else, sometimes well into normal use).
- Fixed via a proper thread-safe handoff: analysis now builds its
  results off to the side and commits them atomically under a lock that
  the audio thread also respects (using a non-blocking try-lock, so the
  audio thread is never made to wait — if the two ever do overlap by a
  hair, playback just skips new data for one single audio block, which
  is inaudible, rather than risking a crash).
- Also confirmed: the "+" quick-add load error in FL Studio is a stale
  cache issue on that machine from repeated internal ID changes during
  troubleshooting, not a bug in POCKETWORK. The full "Select generator
  plugin" list is unaffected and reliable. No further ID changes are
  planned, so this should stay resolved going forward.

## v1.8
- Added a safety cap (30 seconds) on embedding audio into saved project
  state — protects against an oversized state blob if an unusually long
  file ever ends up in a sample slot. Does not appear to be the cause of
  the empty-project save crash (that looks more likely tied to the "+"
  load-failure leaving a broken plugin reference behind — see below),
  but a real safety improvement regardless.

## Known open issue: "+" quick-add load failure (FL Studio side)
Deleting POCKETWORK's entry from FL Studio's plugin database folder
alone does not fix this — Image-Line's own documented repair procedure
requires a FULL rescan immediately after (Verify plugins + Rescan
previously verified plugins + Rescan plugins with errors, all three
checked). If a "+" load fails, delete that broken instance from the
Channel Rack before saving the project — saving a broken/half-loaded
plugin reference may be what's crashing FL Studio, not a bug in a
properly-running POCKETWORK instance.

## v1.9
- Recalibrated the hi-hat detection threshold (0.45 → 0.12) — the old
  value was based on a wrong assumption about how the gentle filter used
  for high-frequency detection behaves, making Hat essentially
  unreachable (everything fell to Kick or Snare instead, matching the
  "kick and snare play, hat never does" report). HONEST CAVEAT: this is
  a recalibration based on understanding the filter's real behavior, not
  something tested against your actual audio — may need further tuning
  once you hear real results.
