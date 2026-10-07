// BTrackBackend — jam::IRhythmTracker adapter around BTrack 1.0.7 (GPLv3).
//
// This is the ONLY layer that touches BTrack. The header deliberately does NOT
// include "BTrack.h" (the implementation is behind a pimpl), so no translation
// unit that merely consumes IRhythmTracker can be pulled across the GPL
// boundary. See third_party/BTrack/CMakeLists.txt's licence note and SPEC.md
// section 25.6.
//
// Evidence, not command: this class emits jam::RhythmObservation and never
// writes a drum tempo. The Musical Clock owns that (SPEC.md section 9.1).
//
// Sample-rate handling (SPEC.md section 17 reference configuration is 48 kHz,
// but the contract requires an arbitrary supported rate):
//
//   BTrack::calculateTempo() hard-codes 44100.0 and its onset-detection
//   function is internally renormalised to a fixed 512-point grid whose point
//   spacing is 512 / sampleRate seconds, INDEPENDENT of the configured hop.
//   The tempo->lag mapping therefore only agrees with the comb filter bank when
//   the onset detection function is presented at 44.1 kHz. Hop scaling
//   (hop = 512 * rate / 44100) does NOT fix this: the internal resample
//   renormalises the hop away. This adapter therefore resamples the incoming
//   audio to a 44.1 kHz analysis rate before it reaches BTrack, and consumes it
//   with BTrack's native hop (512) and frame (1024).
//
//   Measured on the corpus and on synthetic click trains: feeding BTrack 48 kHz
//   audio at a 512 hop biases the reported BPM by about -8.8 % (slow); hop
//   scaling to 557 biases it by about -10 %; resampling to 44.1 kHz brings the
//   synthetic-click error back inside about +/-3 %. The tests in
//   tests/jam/BTrackBackendTests.cpp pin this. See task-notes/TRACK-001.md.

#pragma once

#include "jam/IRhythmTracker.h"

#include <cstdint>
#include <memory>

namespace jam
{

/** Tunables the adapter owns. Constants that affect evidence semantics live
    here rather than as scattered literals, per the TRACK-001 contract. */
struct BTrackBackendConfig
{
    /** Block RMS at or below this is reported as silence (SPEC.md section 19:
        "silence does not create false acceleration"). Digital silence is
        -120 dBFS, so any value between the noise floor and a real performance
        works; -60 dBFS is well below the corpus's quietest fixture. */
    float silenceRmsDbfs = -60.0f;

    /** BTrack's tempo estimator searches a 80..160 BPM grid and can return a
        value slightly outside its own range while settling; clamp the evidence
        to a musically supported window. */
    float minBpm = 40.0f;
    float maxBpm = 240.0f;

    /** Beat confidence is a monotone function of the recent relative tempo
        change; this is the relative change at which confidence reaches 0. */
    float confidenceFullScaleRelTempoChange = 0.02f;
};

class BTrackBackend : public IRhythmTracker
{
public:
    explicit BTrackBackend (const BTrackBackendConfig& config = {});
    ~BTrackBackend() override;

    BTrackBackend (const BTrackBackend&) = delete;
    BTrackBackend& operator= (const BTrackBackend&) = delete;

    /** @param sampleRate  rate the backend is fed at; arbitrary in
        [8000, 192000]. Deterministic: after reset(r) the same input sequence
        yields the same observation sequence. */
    void reset (double sampleRate) override;

    RhythmObservation process (const AnalysisFrame& frame) override;

    /** Stable identifier used by the evaluation harness. */
    const char* id() const noexcept override;

    // --- integration surface (TRACK-001 "expose the latency") ----------------

    /** BTrack's analysis hop, at the 44.1 kHz analysis rate. */
    static constexpr int analysisHopSamples() noexcept { return 512; }

    /** BTrack's analysis frame, at the 44.1 kHz analysis rate. */
    static constexpr int analysisFrameSamples() noexcept { return 1024; }

    /** Rate BTrack is fed at after the adapter's resample. */
    static constexpr double analysisRateHz() noexcept { return 44100.0; }

    /** Maximum framing latency the adapter introduces, in seconds: one BTrack
        analysis frame. (1024 / 44100 = 23.2 ms.) The best case is one hop
        (11.6 ms); the beat predictor keeps the measured onset alignment inside
        one hop, which the drift test asserts. */
    static constexpr double framingLatencySeconds() noexcept
    {
        return static_cast<double> (analysisFrameSamples()) / analysisRateHz();
    }

    /** The analysis hop expressed in device samples at the rate passed to
        reset(); e.g. ~557 at 48 kHz. Zero before reset(). */
    double deviceHopSamples() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    BTrackBackendConfig   config_;
    double                resetRate_ = 48000.0;
};

} // namespace jam
