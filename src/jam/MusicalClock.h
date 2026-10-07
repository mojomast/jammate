// Musical Clock / Entrainment Engine.
//
// The clock is the single authority on what the band believes about time. A
// rhythm tracker produces RhythmObservation *evidence*; the clock fuses that
// evidence into one stable ClockSnapshot that the Jam Director and every
// accompaniment voice consume. No tracker value ever reaches a drum engine as
// tempo. SPEC.md sections 6, 9.1, 9.3, 10.
//
// Threading: the clock runs on the rhythm-analysis / control thread, never on
// the audio callback (SPEC.md 7.3). It may therefore allocate, but it must not
// block, sleep, or do I/O. This implementation allocates nothing after
// construction and contains no containers that can grow without bound.
//
// Determinism: no wall-clock, no sleeping, no unseeded randomness, no global
// state. The same observe()/advance()/command() call sequence always produces
// the same snapshot sequence (DEVPLAN 21.2).
//
// Header hygiene: only <cstdint>/<cstddef> and the frozen jam headers. No STL
// containers and no JUCE, so the clock can be compiled and tested with no
// audio device.

#pragma once

#include <cstddef>
#include <cstdint>

#include "JamConfig.h"
#include "RhythmTypes.h"

namespace jam
{

class MusicalClock
{
public:
    explicit MusicalClock (const ClockConfig& config = {});

    /** Consume one tracker observation. Off the audio thread. */
    void observe (const RhythmObservation& observation);

    /** Advance the internal clock forward by `samples` of audio time at
        `sampleRate`. This is how time passes when no observation arrives, e.g.
        during silence or holdover. Deterministic given the same call sequence. */
    void advance (uint64_t samples, double sampleRate);

    /** Apply one user command (SPEC.md 5.6, 8.3). */
    void command (const ClockCommand& command);

    /** The band's current stable belief. */
    ClockSnapshot snapshot() const;

    /** Back to Acquiring; forget all history. */
    void reset();

    /** Fixed / Follow / Loose (SPEC.md 10.3). */
    void setMode (TempoMode mode);
    TempoMode mode() const noexcept;

private:
    ClockSnapshot buildSnapshot() const;
    void publish();

    bool isUsable (const RhythmObservation& observation) const;
    void handleUsableObservation (const RhythmObservation& observation);
    void handleWeakObservation (const RhythmObservation& observation);
    void maybeLock();
    void resetAcquisition();

    void fuseTempoCandidate (double candidate);
    void slewToward (double target);
    int metricTwinDirection (double candidate, double belief) const;
    void applyMetricCorrection (double factor);

    void applyPhaseCorrection (const RhythmObservation& observation);
    void alignPhaseTo (uint64_t atSampleTime, double targetPhase01);
    double beatsBetween (uint64_t fromSampleTime, uint64_t toSampleTime) const;

    void updateConfidence (double target);
    void updateTimedState();
    void applyTimeBasedConfidenceDecay (double deltaSeconds);

    void applyTapTempo (uint64_t tapSampleTime);
    void applyResync (uint64_t atSampleTime, bool bar);

    ClockConfig config_;

    ClockSnapshot published_;
    uint64_t generation_ = 0;

    TempoMode mode_ = TempoMode::Follow;
    ClockLockState state_ = ClockLockState::Acquiring;
    bool tempoFrozen_ = false;

    // Time base.
    uint64_t sampleTime_ = 0;
    double sampleRate_ = 48000.0;
    double beatsElapsed_ = 0.0;   // continuous beat position since reset
    double bpm_ = 0.0;            // the published, smoothed belief

    // Meter.
    int beatsPerBar_ = 4;
    int beatUnit_ = 4;

    // Belief quality.
    double confidence_ = 0.0;
    double secondsSinceUsableEvidence_ = 0.0;

    // Acquisition bookkeeping.
    double acquisitionSeconds_ = 0.0;
    int consistentObservations_ = 0;
    int phaseEvidenceCount_ = 0;
    bool haveLastPhase_ = false;
    double lastPhase01_ = 0.0;
    uint64_t lastPhaseSampleTime_ = 0;

    // Outlier / metric-ambiguity bookkeeping.
    int jumpStreak_ = 0;
    int jumpDirection_ = 0;
    int metricTwinStreak_ = 0;
    int metricTwinDirection_ = 0;

    // Tap tempo sequence.
    bool haveLastTap_ = false;
    uint64_t lastTapSampleTime_ = 0;
    double tapIntervalSum_ = 0.0;
    int tapIntervalCount_ = 0;
};

} // namespace jam
