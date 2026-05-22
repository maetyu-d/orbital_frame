# of::

A JUCE/C++ desktop app for performing orbital audio structures: states contain tracks, and tracks contain SuperCollider lanes.

## Requires SuperCollider

`of::` uses SuperCollider for all live audio. Install SuperCollider before running the app:

- macOS app path expected by default: `/Applications/SuperCollider.app`
- `sclang` is auto-detected at `/Applications/SuperCollider.app/Contents/MacOS/sclang`
- if SuperCollider is installed elsewhere, set the `sclang` path in `File > Settings`

Download SuperCollider from [supercollider.github.io](https://supercollider.github.io/).

## Build

```sh
cmake -S . -B build -DJUCE_PATH=../Granny/JUCE
cmake --build build --target of -j 6
```

If your JUCE checkout is somewhere else, pass that folder as `JUCE_PATH`.

## Run

The built macOS app is:

```text
build/of_artefacts/Debug/of.app
```

## SuperCollider

Each state can contain one or more circular tracks. Each track contains one or more lanes, and each lane can hold a live or rendered `.scd` script. Use `Audition` to run the selected lane, or `Run` / `Step` to play the active state's tracks according to the Fabric state program.

For copyable lane ideas, see the [SuperCollider lane catalogue](docs/supercollider-lane-catalogue.md).

For tighter standalone audio-app behavior, the app keeps one persistent `sclang` bridge alive, boots the SuperCollider server once, and preloads lane programs. This avoids spawning a new language process for every lane, while the JUCE transport keeps state, track, and child-track changes deterministic.

Use `Render all` before performing when you want JUCE to play rendered lane audio. When `Run` starts in rendered mode, stale or missing lanes are blocked until they have usable audio.

JUCE sends setup, play/stop, and emergency commands over OSC to the persistent language process on `127.0.0.1:57143`, with an ordered file-backed command path as startup insurance. Hidden code-level latency profiles control SC scheduling and buffer size without adding UI.

Lane objects are kept warm where possible: synth-like lane objects are gated/paused instead of recreated on every state change. A hidden crossfade value smooths state and track changes without adding a visible control.

Tracks can contain child tracks. Select a track and use `+ Child` to create a child-track set; the child timing controls decide whether it follows, free-runs, latches, or plays once. Leaving the parent track stops the child tracks and gates their lanes.

The header shows a compact audio status. `Log` opens a hidden drawer with captured `sclang` output for debugging scripts. `Panic` stops all active lane objects and sends `s.freeAll` to SuperCollider without cluttering the normal performance surface.

The app auto-detects `/Applications/SuperCollider.app/Contents/MacOS/sclang` on macOS, then falls back to `sclang` on the shell path. If SuperCollider is installed somewhere else, paste the full `sclang` executable path into the `sclang path` field.

For best stop behavior, write each lane so the final expression returns something stoppable, such as `Routine(...).play`, `Task(...).play`, or a `Synth`. The default lane scripts follow this pattern.
