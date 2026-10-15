# QSplit — AI Stem Separator by Headroom Audio

Drop in a full song → separate it into **Vocals / Drums / Bass / Other** →
audition the stems inside the plugin with the **Cleanup** spectral gate →
bounce clean stems to your DAW.

## Phase 1 (this scaffold)

Full plugin shell, UI, transport, mixer, export, and preset system with a
**stub separator** (copies the song into the "Other" stem) so everything works
end-to-end. Phase 2 swaps in the real AI model behind the `StemSeparator`
interface in `Source/StemSeparator.h` — no UI or engine changes needed.

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

First build downloads JUCE 7.0.12 (~150 MB), then it's cached. Produces VST3,
AU (macOS), and Standalone.

## Layout

- `Source/StemSeparator.h` — `StemSeparator` abstract interface + `StubSeparator`
- `Source/PluginProcessor.{h,cpp}` — song loading, background separation
  (ThreadPool), internal transport, per-stem mixer, spectral-gate cleanup,
  WAV stem export
- `Source/PluginEditor.{h,cpp}` — drop zone, progress, waveform overview,
  transport, 4 stem strips, Cleanup/Master knobs, preset menu with favorites

## Presets

Factory + user presets live in `~/Documents/Headroom Audio/QSplit/Presets/`
(`*.qsplitpreset`), favorites in `favorites.txt`. Presets store cleanup amount,
master gain, and per-stem level/mute/solo.
