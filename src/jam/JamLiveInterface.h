// First audible live-slice seam. JUCE-free, POD commands/telemetry only.
// Exact worker ownership and lifecycle contracts: docs/research/LIVE-JAM-CONTRACT.md.
#pragma once

#include "RhythmTypes.h"
#include <cstdint>
#include <type_traits>

namespace jam
{
enum class JamLiveCommandType : int
{
    Start = 0,
    Stop,
    StopAtNextBar,
    TapTempo,
    ResyncNextBeat,
    ResyncNextBar,
    HalfTime,
    DoubleTime,
    FreezeTempo,
    ResumeFollow,
    SetMode,
    Reset,
    // Adaptive wave additions are appended to preserve the original command IDs.
    SetStyle,       // value: catalogue index (0..5)
    SetIntensity,   // value: 0..1
    SetComplexity,  // value: 0..1
    SetFillAmount,  // value: 0..1
    RequestFill
};

struct JamLiveCommand
{
    JamLiveCommandType type = JamLiveCommandType::Start;
    // SetMode: numeric TempoMode; other commands use zero. No raw detector BPM
    // setter: the MusicalClock owns tempo, including user correction commands.
    double value = 0.0;
};

enum class JamLiveBackend : int { unavailable = 0, experimentalBTrack, injectedTest };
enum class JamLiveFailure : int { none = 0, unavailableBackend, invalidDevice, workerFailure };

struct JamLiveState
{
    std::uint64_t sessionGeneration = 0;
    bool prepared = false;
    bool requestedRunning = false; // accepted start intent, not proof of sound
    bool joinPending = false;
    bool drumsPlaying = false;     // latest audio-owner echo, not scheduled intent
    JamLiveBackend backend = JamLiveBackend::unavailable;
    JamLiveFailure failure = JamLiveFailure::none;
    ClockSnapshot clock {};
    // Worker-owned settings are distinct from the audio owner's pattern echo.
    int styleIndex = 0;
    float intensity01 = 0.5f;
    float complexity01 = 0.5f;
    float fillAmount01 = 0.3f;
    float performanceIntensity01 = 0.5f;
    // These two fields come only from the audio-owner echo, never the director.
    int activeGroove = -1;
    bool fillPlaying = false;
    bool adaptiveChangePending = false;
    TempoMode mode = TempoMode::Follow;
    float candidateBpm = 0.0f;
    float inputPeak = 0.0f;
    double sampleRate = 0.0;
    std::uint64_t audioSampleTime = 0;
    std::uint64_t lastEventSampleTime = 0;
    std::uint64_t lastInputHorizonSampleTime = 0;
    // Externally observed audio cursor at control-worker receipt. Resolution is
    // an audio block; no sub-block receipt or physical device latency claim.
    std::uint64_t lastReceiptSampleTime = 0;
    bool receiptMeasured = false;
    std::uint64_t analysisDrops = 0;
    std::uint64_t observationDrops = 0;
    std::uint64_t userCommandDrops = 0;
    std::uint64_t drumCommandDrops = 0;
    std::uint64_t discontinuities = 0;
};

static_assert (std::is_trivially_copyable_v<JamLiveCommand>);
static_assert (std::is_trivially_copyable_v<JamLiveState>);

class IJamLiveControl
{
public:
    virtual ~IJamLiveControl() = default;
    // One message-thread producer. Bounded enqueue only; no lifecycle work.
    // False means rejected/full; UI must not present the intent as applied.
    virtual bool submitJamCommand (const JamLiveCommand&) noexcept = 0;
    // One message-thread reader; coherent latest-value attempt, no retry.
    // False leaves out unchanged: cache the previous whole state until next tick.
    virtual bool readJamLiveState (JamLiveState& out) const noexcept = 0;
};
} // namespace jam
