// Adaptive Guitar Jam Companion — frozen core data types.
//
// Semantic contracts are frozen by SPEC.md sections 8 and 9. The exact C++
// spelling may evolve, but the meaning of these types must not drift without an
// ADR.
//
// Real-time rules: this header must remain dependency-free (no JUCE, no STL
// containers, no allocation) because types declared here cross the audio
// callback boundary.

#pragma once

#include <cstddef>
#include <cstdint>

namespace jam
{

// Maximum analysis block size accepted by the bounded analysis ring.
// Chosen to comfortably exceed a 4096-frame device block at every supported
// sample rate without unbounded growth. See SPEC.md section 8.1.
inline constexpr std::size_t kMaxAnalysisBlock = 2048;

/** Audio block handed from the analysis tap to the rhythm-analysis worker.
    Mono, because beat tracking does not benefit from stereo decorrelation and
    a single downmix keeps the queue payload bounded and predictable. */
struct AnalysisFrame
{
    uint64_t sampleTime = 0;                       // device sample-clock of first sample
    double   sourceSampleRate = 48000.0;           // rate the block was captured at
    uint32_t numSamples = 0;                       // valid entries in `samples`
    float    samples[kMaxAnalysisBlock] = {};      // mono, nominally [-1, 1]
};

/** Evidence emitted by a tracker backend. This is *evidence*, never a command.
    A backend must not write drum tempo directly; see SPEC.md section 9.1. */
struct RhythmObservation
{
    uint64_t inputSampleTime = 0;
    double   sourceSampleRate = 48000.0;

    float bpmCandidate = 0.0f;
    float beatPhase01 = 0.0f;
    float beatConfidence01 = 0.0f;

    float onsetStrength01 = 0.0f;
    float energyRmsDbfs = -120.0f;
    float transientDensity01 = 0.0f;

    bool beatEvent = false;
    bool silence = false;

    // True when the backend only offers a tempo estimate with no usable phase.
    // The Musical Clock must not lock on phase-free observations alone.
    bool phaseValid = false;
};

enum class ClockLockState : int
{
    Acquiring = 0,
    Locked    = 1,
    Holdover  = 2,
    Lost      = 3
};

inline const char* toString (ClockLockState s) noexcept
{
    switch (s)
    {
        case ClockLockState::Acquiring: return "Acquiring";
        case ClockLockState::Locked:    return "Locked";
        case ClockLockState::Holdover:  return "Holdover";
        case ClockLockState::Lost:      return "Lost";
    }
    return "Unknown";
}

/** The band's stable belief about time. Every accompaniment voice consumes this
    instead of estimating tempo independently. SPEC.md section 9.3. */
struct ClockSnapshot
{
    uint64_t generation = 0;

    double bpm = 0.0;
    double beatPhase01 = 0.0;     // 0..1 within the current beat
    double barPhase01 = 0.0;      // 0..1 within the current bar

    int beatInBar = 0;            // 1-based beat currently sounding
    int beatsPerBar = 4;
    int beatUnit = 4;             // denominator of the meter, e.g. 4 or 8

    float confidence01 = 0.0f;
    ClockLockState lockState = ClockLockState::Acquiring;
    bool tempoFrozen = false;
};

/** Musical decisions made by the Jam Director. Policy, not mechanism. */
struct JamIntent
{
    float intensity01 = 0.5f;
    float complexity01 = 0.5f;
    float fillAmount01 = 0.3f;
    float swing01 = 0.0f;

    bool requestFill = false;
    bool requestBreak = false;
    bool requestCrash = false;
    bool requestStop = false;

    int sectionIndex = 0;
};

/** How much the clock is permitted to follow the guitarist. SPEC.md 10.3. */
enum class TempoMode : int
{
    Fixed = 0,   // slew = 0, tempo is user-owned
    Follow = 1,  // moderate allowable tempo slew
    Loose  = 2   // lower slew, stronger phase damping
};

// Number of tempo modes; used to size per-mode configuration arrays. Keep in
// sync with TempoMode.
inline constexpr int kTempoModeCount = 3;

inline const char* toString (TempoMode m) noexcept
{
    switch (m)
    {
        case TempoMode::Fixed: return "Fixed";
        case TempoMode::Follow: return "Follow";
        case TempoMode::Loose:  return "Loose";
    }
    return "Unknown";
}

/** Non-coalescible user commands. These travel through a bounded SPSC command
    queue (SPEC.md 8.3), never through the snapshot path. */
enum class ClockCommandType : int
{
    None = 0,
    SetMode,        // arg0 = int(TempoMode)
    TapTempo,       // tapSampleTime = when the user tapped
    ResyncNextBeat,
    ResyncNextBar,
    HalfTime,
    DoubleTime,
    FreezeTempo,
    ResumeFollow,
    Reset
};

struct ClockCommand
{
    ClockCommandType type = ClockCommandType::None;
    uint64_t tapSampleTime = 0;
};

/** Source of observations for offline / deterministic testing. Real-time code
    consumes RhythmObservation; how it was produced is irrelevant to it. */
class IRhythmObservationSource
{
public:
    virtual ~IRhythmObservationSource() = default;
    virtual RhythmObservation obtainLatest() = 0;
};

} // namespace jam