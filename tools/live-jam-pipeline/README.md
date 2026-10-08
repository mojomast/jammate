# live-jam-pipeline driver

Portable, JUCE-free exercise of the INT-LIVE-001 control core. It drives a real
`jam::LiveJamSession` through a deterministic 120 BPM script and emulates the
audio-owner drum transport by consuming the **same** bounded `DrumClockBridge`
command queue the actual `DrumEngine` consumes.

This is not a replacement for the orchestrator's actual-processor replay. It
links only `jam-core`, so it proves the pipeline logic and the join/echo
handshake without an audio device, but it cannot measure callback allocation or
device deadlines. That evidence gate is `EVAL-LIVE-001`'s actual-processor
replay against a freshly built product.

## What it checks

- the clock locks from scripted evidence and Start joins once at a usable lock;
- `joinPending` is visible before `drumsPlaying`, and `drumsPlaying` is only set
  from the emulated audio-owner echo (never from the scheduled command);
- Stop clears the running intent, commits one stop, and does **not** auto-resume;
- a 4096-frame callback split into <=2048-sample chunks is not truncated (this
  phase runs the real `RhythmAnalyzer` worker with a counting tracker and a
  bounded wait).

Exit code 0 only when every invariant holds.

## Build and run

After building `jam-core`:

```sh
g++ -std=c++17 -O2 -I <repo>/src \
    tools/live-jam-pipeline/live_jam_pipeline_driver.cpp \
    <build>/libjam-core.a -lpthread -o live_jam_pipeline_driver
./live_jam_pipeline_driver
```

In-tree, the orchestrator wires `tools/live-jam-pipeline/CMakeLists.txt` with
`add_subdirectory` after `jam-core`; it registers the `jam.LiveJamPipeline`
ctest entry. No shared CMake file is edited by INT-LIVE-001.
