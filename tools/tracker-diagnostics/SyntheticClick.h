// SyntheticClick — deterministic click/onset train generator for the TRACK-004
// BPM quantisation experiment.
//
// The waveform is the SAME `clickSample()` construction the pinned backend
// tests use (tests/jam/BTrackBackendTests.cpp:146-165, mirrored in
// AubioBackendTests.cpp): a hashed noise floor plus a 6 ms exponentially
// decaying noise burst every beat. Reproducing it exactly means the harness
// diagnostic and the unit tests describe the same input.

#pragma once

#include "BackendRunner.h"   // rhythmeval::WavData

namespace tracker_diag
{

struct ClickSpec
{
    double bpm = 120.0;
    double sampleRate = 48000.0;
    double seconds = 12.0;
    double firstBeat = 0.1;      // seconds, matches the backend tests
    double noiseFloor = 0.003;
};

rhythmeval::WavData makeClickTrain (const ClickSpec& spec);

} // namespace tracker_diag
