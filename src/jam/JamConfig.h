// Central clock/director configuration.
//
// SPEC.md section 10.2: "All thresholds must live in one configuration structure
// and be covered by tests." No magic numbers may appear inside MusicalClock.cpp
// or JamDirector.cpp. Tuning work (DEVPLAN TUNE-001/002) edits this file and
// reruns the corpus, and nothing else.
//
// Every field has a documented default taken from SPEC.md 10.2 or 10.3. The
// defaults are the *initial* values proposed by the spec, not validated values.
// Evidence-backed revisions must be recorded in an ADR.

#pragma once

#include "RhythmTypes.h"

namespace jam
{

struct ClockConfig
{
    // --- Tempo domain -------------------------------------------------------
    double minAutoFollowBpm = 50.0;    // SPEC 10.2: supported auto-follow range
    double maxAutoFollowBpm = 220.0;   // SPEC 10.2
    double fallbackBpm = 100.0;        // used when no belief exists yet

    // --- Acquisition --------------------------------------------------------
    // SPEC 10.2: confidence lock threshold ~0.65, acquire window 1-2 bars.
    float lockConfidenceThreshold = 0.65f;
    double acquireWindowSeconds = 8.0;      // 2 bars at 60 BPM; 4 bars at 120 BPM
    int    minimumLockedObservations = 4;
    float tempoAgreementTolerance = 0.03f;  // 3% spread counts as agreement

    // --- Rejection of outliers --------------------------------------------
    // SPEC 10.2: sudden single-observation changes above ~10-12% rejected
    // unless reinforced.
    float isolatedJumpRejectRatio = 0.11f;
    int    isolatedJumpReinforceCount = 3;

    // --- Tempo smoothing ---------------------------------------------------
    // SPEC 10.3: Fixed slew = 0; Follow moderate; Loose lower with stronger
    // phase damping. Expressed as the maximum relative tempo change allowed per
    // observation, so behaviour is frame-rate independent.
    float maxSlewRatioPerObservation[kTempoModeCount] = { 0.0f, 0.020f, 0.004f };

    // --- Phase correction --------------------------------------------------
    // Fraction of the observed phase error absorbed per observation.
    float phaseCorrectionGain[kTempoModeCount] = { 0.0f, 0.25f, 0.06f };
    float maxPhaseErrorAbsorbed = 0.35f;   // clamp per observation, fraction of a beat

    // --- Confidence --------------------------------------------------------
    float confidenceAttackRate = 0.35f;     // rise toward observed confidence
    float confidenceReleaseRate = 0.08f;    // fall away from it
    float holdoverConfidenceFloor = 0.25f;  // floor while in holdover

    // --- Holdover / loss ---------------------------------------------------
    // SPEC 10.2: short silence -> holdover, long silence -> Lost.
    double holdoverEnterSeconds = 1.5;
    double lostEnterSeconds = 6.0;
    float  silenceRmsDbfs = -55.0f;

    // --- Half / double -----------------------------------------------------
    // SPEC 10.2: half/double candidates explicitly modelled, not guessed at the
    // end. A competing candidate at a metric ratio is tracked and confirmed,
    // never applied on first sight.
    float metricAmbiguityRatio = 0.055f;    // within 5.5% counts as a metric twin
    int   metricConfirmationObservations = 5;

    // --- Meter -------------------------------------------------------------
    int defaultBeatsPerBar = 4;
    int defaultBeatUnit = 4;
};

/** Director policy. SPEC.md sections 13.3, 14, 15. */
struct DirectorConfig
{
    // Join policy
    double joinLookaheadBars = 1.0;         // earliest join is one full bar away
    float  joinConfidenceThreshold = 0.70f;

    // Dynamics (SPEC 14). All mappings require hysteresis and attack/release
    // smoothing, so each pair of gains is asymmetric.
    float energyAttack = 0.35f;
    float energyRelease = 0.06f;
    float intensityDeadband = 0.12f;        // hysteresis band around tier edges
    int   highEnergySustainTicks = 8;       // ticks of sustained energy before promotion

    // Fill policy (SPEC 13.3)
    float fillBaseChance = 0.12f;
    float fillPhraseBoundaryBonus = 0.30f;
    float fillConfidenceSuppression = 0.85f; // multiplier applied at low confidence
    float fillConfidenceThreshold = 0.55f;

    // Pattern selection
    int     minimumRepetitionDistance = 4;      // bars before a groove may repeat
    uint64_t randomSeed = 0x5EED5EEDull;        // determinism for tests
};

} // namespace jam