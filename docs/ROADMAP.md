# tını roadmap

Where the project is going and in which order. The order is deliberate: **engine first, content
second, layout last**. A modular home page only makes sense once we know which cards exist, and
those depend on NAM and the model library.

Status: ✅ done · 🚧 in progress · ⬜ planned

| Phase | Topic | Status |
|------:|-------|:------:|
| 0 | Dynamic signal chain (multiple copies of a pedal) | ✅ |
| 1 | NAM amp slot | ⬜ |
| 2 | Better IR handling and a local library | ⬜ |
| 3 | TONE3000 integration | ⬜ |
| 4 | Pages: Rig, Pedals, Library | ⬜ |
| 5 | Modular home page (movable and hideable cards) | ⬜ |

## Phase 0 — Dynamic signal chain ✅

The chain used to be a fixed list of 8 items (7 pedals + the amp), at most one of each pedal.

- Chain items are numbered `copy * count + type`; the amp is the last item (`PedalCatalog.h`).
- Every pedal type exists `maxCopies` (3) times. **All parameters of all copies are declared up
  front**: copy 0 keeps the plain ids (`DELAY_TIME`), copy *n* gets a suffix (`DELAY_TIME_2`). This keeps
  host automation working, which a parameter set that changes at runtime would not.
- Membership is the `*_ADDED` switch of each copy; copies that are not added are parked at the end of
  the chain order and skipped by the processor and the UI.
- The audio thread reads cached parameter pointers (no lookups by name) and picks up a new chain order
  with a try-lock once per block.
- Sessions and `.nfpreset` files saved by version 1 (order packed into one integer) still load.

## Phase 1 — NAM amp slot ⬜

Neural Amp Modeler support is what makes the "neural" in the product real.

- Add [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) (MIT) and Eigen under
  `third_party/`, wired into CMake. Both are compatible with the AGPLv3 licence of this project.
- New amp type: choose between the built-in `Crunch` amp and a NAM model. `Crunch` stays as the
  fallback while no model is loaded.
- Load models on a background thread and swap them in atomically; free the old model on the
  message thread, never on the audio thread.
- Models are usually trained at 48 kHz: resample when the host runs at another rate and report the
  added latency through `getLatencySamples()` / `LatencyProbe`.
- Respect the loudness / input-level metadata of a model so models are comparable in level.
- Open questions: CPU budget of Standard vs. Lite / Feather / Nano models; whether NAM pedals
  (drive / fuzz captures) get their own pedal type.

## Phase 2 — IR handling and a local library ⬜

- Keep the current single-IR loader. Add an A/B mix of two IRs, automatic handling of the IR's own
  sample rate, trimming and normalisation options.
- A shared local library under `Documents/tini/{Models,IRs}` with a browser, favourites and tags. It is
  the common target for local files and for downloads from TONE3000.

## Phase 3 — TONE3000 integration ⬜

[TONE3000](https://www.tone3000.com) offers an API (v1, beta): OAuth 2.0 with PKCE, search, a user's
favourites and own tones, and a download URL per model. Rate limit is 100 requests per minute.

Flow: "Sign in with TONE3000" opens the system browser, a loopback callback (or custom URL scheme)
receives the code, the token is kept in the OS credential store. Then: search / favourites → download
into the local library → load into the NAM or IR slot.

- All network work runs on its own thread and never touches the audio thread (`juce::URL` /
  `WebInputStream`).
- **Open points before starting:** whether public search and `.nam` / IR downloads work with an app
  key alone or require the end user to sign in; TONE3000's design and commercial terms; the licence of
  each individual tone (show it in the UI, do not redistribute). Request an API key from
  support@tone3000.com first.
- Bulk download (`/tones/download`) is restricted to approved partners and is not part of the plan.

## Phase 4 — Pages ⬜

Tabs in the header: **Rig** (home) · **Pedals** · **Library**.

- **Pedals:** the chain on a full page: large pedal cards, add by category, several copies of the same
  pedal, NAM pedals. Reuses `ChainStrip`, `PedalView` and `AddPedalMenu` from `Source/UI/Panels.h`.
- **Library:** local models / IRs and the TONE3000 browser side by side.

## Phase 5 — Modular home page ⬜

`MainView::resized()` currently uses fixed pixel sizes. Replace it with a grid-based card host.

- Each card has `{id, gridX, gridY, w, h, visible}`.
- Cards: Amp, Signal chain (summary), Cab / IR, Oscilloscope, Meters, Tuner, Presets.
- An **Edit** mode: drag cards onto the grid, resize from a corner, hide with an eye icon, "reset to
  default".
- The layout is an application setting (`PropertiesFile`), **not** part of a preset, so switching
  presets never rearranges the screen.
- A hidden card must stop its background work too (e.g. no scope copy while the oscilloscope is hidden).

## Smaller items

- Tuner card.
- Input gain calibration against the audio interface level (important for NAM).
- A/B snapshots and undo through the parameter tree's `UndoManager`.
- MIDI learn / footswitch control for presets and bypass.
- CPU meter in the header (needed once NAM is in).

## Engineering rules

- One phase = a series of small commits; the test suite (`NeuralFlowAmpTests`) must pass after each.
- Anything that changes saved data needs a migration and a test that loads an old file.
- No allocation, locks that can block, or file / network access on the audio thread.
- Naming: the UI says **tını**, file and executable names use ASCII **Tini**; the CMake target and
  class names (`NeuralFlowAmp…`) stay as they are.
