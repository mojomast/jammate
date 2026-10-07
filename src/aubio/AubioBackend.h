// AubioBackend — jam::IRhythmTracker adapter around aubio 0.4.9 (GPLv3-or-later).
//
// This is the ONLY layer that touches aubio. The header deliberately does NOT
// include "aubio.h" (the implementation is behind a pimpl), so no translation
// unit that merely consumes IRhythmTracker is pulled across the GPL boundary.
// See third_party/aubio/CMakeLists.txt's licence note and SPEC.md section 25.6.
//
// Evidence, not command: this class emits jam::RhythmObservation and never
// writes a drum tempo. The Musical Clock owns that (SPEC.md section 9.1).
//
// SAMPLE-RATE HANDLING (SPEC.md section 17 reference configuration is 48 kHz,
// but the contract requires an arbitrary supported rate):
//
//   Decision — feed audio at the true device rate with a FIXED 512-sample hop.
//   No resampling, no hop scaling.
//
//   Unlike BTrack, aubio has no hard-coded analysis rate: its beat tracker
//   works in detection-function (DF) frames of `hop_size` samples and converts
//   with `bpm = 60 * samplerate / (hop_size * bp)` (beattracking.c:424), while
//   its 120 BPM prior is `rayparam = 60 * samplerate / 120 / hop_size`
//   (beattracking.c:65). Both scale with the rate, so a fixed hop is unbiased
//   at any rate. This is also how upstream's own example uses it: aubiotrack.c
//   hard-codes `buffer_size = 1024; hop_size = 512` for every input rate.
//
//   Measured on synthetic click trains: 80..160 BPM at both 44.1 kHz and
//   48 kHz lands within +1.7 % of truth (aubio rounds the beat period to the DF
//   frame grid and carries a small positive bias), and 48 kHz vs 44.1 kHz agree
//   to <0.2 %. Declaring the wrong rate (44100 for 48 kHz audio) biases the
//   estimate by exactly 44100/48000 - 1 = -8.1 %; the tests pin this.
//
//   A real (non-bias) property worth knowing when comparing rates: aubio sizes
//   its autocorrelation window as next_power_of_two(5.8 * rate / hop)
//   (tempo.c:188). At 44.1 kHz / 512 that is 512 DF frames (~6.0 s); at
//   48 kHz / 512 it is 1024 DF frames (~10.9 s), because 5.8*48000/512 = 543.75
//   just crosses the power-of-two boundary. It changes acquisition/hold
//   behaviour, not the tempo scale. See task-notes/TRACK-002.md.
//
// PHASE: aubio exposes the beat *position* (aubio_tempo_get_last(), including
//   the sub-hop offset) and the beat period (aubio_tempo_get_period()), but no
//   phase01 getter. The adapter normalises those two exposed quantities; it does
//   not free-run a phase. `phaseValid` is false until a beat has established an
//   anchor. See VENDORED-PATCHES.md and the .cpp.
//
// CAUSALITY: aubio 0.4.9 has no blocking/non-causal prediction mode. The adapter
//   calls only aubio_tempo_do() and pure getters; there is no set_btstate /
//   get_btstate in this revision. `process` never waits for future audio.

#pragma once

#include "jam/IRhythmTracker.h"

#include <cstdint>
#include <memory>

namespace jam
{

/** Tunables the adapter owns. Constants that affect evidence semantics live
    here rather than as scattered literals. */
struct AubioBackendConfig
{
    /** Block RMS at or below this is reported as silence (SPEC.md section 19:
        "silence does not create false acceleration"). Digital silence is
        -120 dBFS, so any value between the noise floor and a real performance
        works; -60 dBFS is well below the corpus's quietest fixture. This is the
        adapter's held-silence gate; aubio's own internal silence detector is
        left at its default -90 dBFS so the internal beat grid is not suppressed
        through quiet-but-real passages (see the .cpp). */
    float silenceRmsDbfs = -60.0f;

    /** aubio's tempo estimate is not bounded a priori; clamp the evidence to a
        musically supported window. */
    float minBpm = 40.0f;
    float maxBpm = 240.0f;

    /** Onset strength is derived from the block energy envelope (the same
        definition BTrackBackend uses), so the two candidates report a
        comparable `onsetStrength01` and the G3 comparison is about the tracker,
        not about whose onset feature is scaled differently. */
    float onsetFluxFloor = 1.0e-9f;
};

class AubioBackend : public IRhythmTracker
{
public:
    explicit AubioBackend (const AubioBackendConfig& config = {});
    ~AubioBackend() override;

    AubioBackend (const AubioBackend&) = delete;
    AubioBackend& operator= (const AubioBackend&) = delete;

    /** @param sampleRate  rate the backend is fed at; arbitrary in
        [8000, 192000]. Deterministic: after reset(r) the same input sequence
        yields the same observation sequence. */
    void reset (double sampleRate) override;

    RhythmObservation process (const AnalysisFrame& frame) override;

    /** Stable identifier used by the evaluation harness. */
    const char* id() const noexcept override;

    // --- integration surface (report the latency, as TRACK-001 did) ----------

    /** aubio's analysis hop at the device rate. Fixed at 512 samples at every
        rate; aubio's estimator normalises by samplerate/hop. */
    static constexpr int analysisHopSamples() noexcept { return 512; }

    /** aubio's FFT window at the device rate (upstream's example default). */
    static constexpr int analysisWindowSamples() noexcept { return 1024; }

    /** Maximum framing latency the adapter introduces, in seconds: one full hop
        of accumulated audio, because a beat occupies one hop and is only
        released when that hop completes. This is the same "one hop" bound the
        accumulator can produce; measured onset alignment is inside it. */
    static double framingLatencySeconds (double sampleRate) noexcept
    {
        return static_cast<double> (analysisHopSamples()) / sampleRate;
    }

    /** The hop in device samples (constant 512; kept for symmetry with
        BTrackBackend::deviceHopSamples()). */
    int deviceHopSamples() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    AubioBackendConfig    config_;
    double                resetRate_ = 48000.0;
};

} // namespace jam
