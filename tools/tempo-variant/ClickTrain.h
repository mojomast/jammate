// ClickTrain — deterministic click/onset train generator for the TRACK-005
// tempo-report variant experiment.
//
// The waveform construction is the SAME clickSample() the pinned backend tests
// and the TRACK-004 synthetic sweep use (a hashed noise floor plus a 6 ms
// exponentially-decaying noise burst per beat). This generator adds one thing
// the TRACK-004 one does not: an optional phase-continuous tempo STEP at a fixed
// time, so the variant's causal response to a real tempo change can be measured
// without any offline future availability.

#pragma once

#include "BackendRunner.h"   // rhythmeval::WavData

#include <vector>

namespace tempo_variant
{

struct ClickSpec
{
    double sampleRate = 48000.0;
    double seconds = 24.0;
    double firstBeat = 0.1;
    double noiseFloor = 0.003;

    double bpm = 126.0;       // tempo of the first segment
    double stepToBpm = 0.0;   // 0 => constant; otherwise step at stepSeconds
    double stepSeconds = 12.0;
};

/** Beat times (seconds) the generator renders. Exposed for tests. */
std::vector<double> clickBeatTimes (const ClickSpec& spec);

rhythmeval::WavData makeClickTrain (const ClickSpec& spec);

} // namespace tempo_variant
