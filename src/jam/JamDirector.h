// JamDirector — deterministic, bounded musical policy for the adaptive drummer.
//
// DEVPLAN DIRECTOR-001 / SPEC.md sections 13.3, 14, 15.
//
// WHAT THIS OWNS, AND WHAT IT DOES NOT
//   The director owns musical decisions: which style/tier groove should be
//   playing, whether a fill/break is wanted, and how hard the drums should be
//   hit. It emits a proposed `QueuedBarChange` plus a `JamIntent`.
//
//   It does NOT own the live transport lifecycle. Joining, stopping and the
//   audio-owner echo are the job of `JamJoinPolicy` (INT-LIVE-001) in the live
//   session; the director only *observes* the clock lock state and the playback
//   echo and exposes its own `DirectorState` for inspection. It never issues a
//   stop and never claims a musical change landed before the caller confirms it.
//
// INTENT / ACK
//   `update()` proposes at most one `QueuedBarChange` at a time. The caller
//   publishes it (e.g. `DrumClockBridge::requestBarChange`) and reports the
//   accept/reject through `acknowledgePublication()`. A rejected proposal stays
//   pending and is re-emitted unchanged; selection/repetition state advances
//   only on acceptance (SPEC.md 8.3, ADAPTIVE-WAVE-CONTRACT.md). The director
//   therefore cannot "forget" a rejected join into a latched wrong state.
//
// DETERMINISM / REAL-TIME
//   Pure data: no threads, no time source, no I/O, no allocation, no STL
//   containers. The only randomness is a seeded SplitMix64 owned by the object,
//   so the same settings + input sequence yields the same decisions on every
//   machine (DEVPLAN 21.2). It is designed to run on the Jam-control thread, at
//   control-tick rate, never on the audio callback.
//
// HEADER HYGIENE (SPEC.md 7.1): no JUCE, no STL containers.

#pragma once

#include <cstddef>
#include <cstdint>

#include "IDrumTransport.h"
#include "JamConfig.h"
#include "RhythmTypes.h"
#include "StyleCatalog.h"

namespace jam
{

// ---------------------------------------------------------------------------
// Structural policy constants. These are structural (they define the shape of
// the musical model), not tuned thresholds: `DirectorConfig` in JamConfig.h
// still owns every tunable. TUNE-002 may promote these into the config; they
// live here so `JamDirector.cpp` contains no unnamed magic numbers.
// ---------------------------------------------------------------------------

/** Bars per phrase; fills are preferentially placed on phrase boundaries. */
inline constexpr int kDirectorPhraseLengthBars = 4;

/** Bottom/top edges of the medium intensity tier (SPEC.md 14). */
inline constexpr float kDirectorTierLowEdge = 0.34f;
inline constexpr float kDirectorTierHighEdge = 0.67f;

/** Anti-repetition ring size. Fixed, so nothing allocates. */
inline constexpr int kDirectorRepetitionHistory = 16;

/** Minimum bars between automatic fills. */
inline constexpr int kDirectorMinFillGapBars = 4;

/** `fillAmount01` of 0.30 is the neutral amount: it leaves the configured base
 *  and phrase-boundary chances unchanged. */
inline constexpr float kDirectorNeutralFillAmount = 0.30f;

/** Extra fill chance added by a strong onset at a phrase boundary (SPEC.md 14). */
inline constexpr float kDirectorOnsetFillBonus = 0.10f;

/** Complexity envelope attack/release gains. Complexity is a user control, so
 *  it glides rather than snapping (SPEC.md 13.3: gradual tiers). */
inline constexpr float kDirectorComplexityAttack = 0.15f;
inline constexpr float kDirectorComplexityRelease = 0.08f;

/** Director state machine (SPEC.md 15). `Stopping` is an observed/proposed
 *  musical state; the actual transport stop is owned by JamJoinPolicy. */
enum class DirectorState : int
{
    Idle = 0,
    Listening,
    ReadyToJoin,
    Playing,
    Holdover,
    Reacquiring,
    Stopping
};

inline const char* toString (DirectorState state) noexcept
{
    switch (state)
    {
        case DirectorState::Idle:        return "Idle";
        case DirectorState::Listening:   return "Listening";
        case DirectorState::ReadyToJoin: return "ReadyToJoin";
        case DirectorState::Playing:     return "Playing";
        case DirectorState::Holdover:    return "Holdover";
        case DirectorState::Reacquiring: return "Reacquiring";
        case DirectorState::Stopping:    return "Stopping";
    }
    return "Unknown";
}

/** User-controlled settings. SPEC.md 16. Values are clamped on entry. */
struct DirectorSettings
{
    StyleId style = StyleId::Rock;
    float intensity01 = 0.5f;
    float complexity01 = 0.5f;
    float fillAmount01 = 0.3f;

    /** One-shot requests. The director latches them, so a UI pulse survives a
     *  tick that cannot propose a change yet. */
    bool requestFill = false;
    bool requestBreak = false;
};

/** Everything the director consumes on one control tick. */
struct DirectorInputs
{
    /** Session generation (from the live session). 0 means "unspecified". A
     *  change to a non-zero value is a session boundary and resets musical
     *  state; pass 0 in deterministic unit tests that only drive the clock. */
    std::uint64_t sessionGeneration = 0;

    /** Absolute audio cursor (device sample timeline) this tick corresponds to.
     *  When non-zero, a repeated value means the control worker ran again for
     *  the same audio position; the director then does not advance its energy
     *  envelope, phrase counter or RNG, and does not re-decide a committed bar.
     *  0 disables the guard (deterministic tests advance by phase). */
    std::uint64_t audioCursor = 0;

    /** True when the session saw a transport discontinuity this tick. Pending
     *  proposals are forgotten and stale musical state is reset; live queues are
     *  not touched here. */
    bool discontinuity = false;

    /** The band's stable belief. The director never reads tempo from anywhere
     *  else, and never writes tempo anywhere. */
    ClockSnapshot clock {};

    /** Explicit transport/audio position, as echoed by the audio owner. Used for
     *  bar-boundary confirmation and diagnostics; the clock owns time. */
    TransportPosition transport {};

    /** Audio-owner echo: the injected drums are really rendering. The director
     *  only proposes musical changes while this is true and the live policy has
     *  the requested-running lifecycle. */
    bool playbackEchoPlaying = false;

    /** Set by the session from JamJoinPolicy: true only while the user's running
     *  intent is live (requestedRunning && !stopPending). The director reads it;
     *  it never owns or changes the lifecycle. Defaults true so deterministic
     *  unit tests can ignore lifecycle plumbing. */
    bool lifecycleAllowsPerformance = true;

    /** Input features (SPEC.md 14). No tracker tempo is present on purpose. */
    float energy01 = 0.0f;
    float onsetStrength01 = 0.0f;
    float transientDensity01 = 0.0f;
    bool onsetEvent = false;
    bool silence = false;
};

/** The director's proposal for this tick. */
struct DirectorDecision
{
    DirectorState state = DirectorState::Idle;

    JamIntent intent {};

    /** True when `barChange` must be published. The same change is re-emitted on
     *  every tick until `acknowledgePublication()` reports the outcome. */
    bool hasBarChange = false;
    QueuedBarChange barChange {};

    // Diagnostics for UI/tests.
    GrooveTier tier = GrooveTier::Medium;
    float intensityEnvelope01 = 0.0f;
    float complexityEnvelope01 = 0.0f;
    std::uint64_t barsObserved = 0;
    int phraseIndex = 0;
    bool fillRequested = false;
    bool breakRequested = false;
};

/** Read-only snapshot for UI and diagnostics. */
struct DirectorReport
{
    DirectorState state = DirectorState::Idle;
    JamIntent intent {};
    DirectorSettings settings {};
    GrooveTier tier = GrooveTier::Medium;
    LibraryIndex committedGroove = kNoLibraryEntry;
    LibraryIndex committedFill = kNoLibraryEntry;
    LibraryIndex pendingGroove = kNoLibraryEntry;
    LibraryIndex pendingFill = kNoLibraryEntry;
    bool pendingAck = false;
    QueuedBarChange pendingChange {};
    float intensityEnvelope01 = 0.0f;
    float complexityEnvelope01 = 0.0f;
    std::uint64_t barsObserved = 0;
    std::uint64_t acceptedPublications = 0;
    std::uint64_t rejectedPublications = 0;
    std::uint64_t cancelledPublications = 0;
    std::uint64_t fillsEmitted = 0;
    std::uint64_t fillsSuppressed = 0;
    std::uint64_t staleResets = 0;
};

class JamDirector
{
public:
    explicit JamDirector (const DirectorConfig& config = DirectorConfig {}) noexcept;

    JamDirector (const JamDirector&) = delete;
    JamDirector& operator= (const JamDirector&) = delete;

    // --- lifecycle -----------------------------------------------------------

    /** Forget all musical state and return to Idle. Sane defaults fill any
     *  invalid settings field. Call on every session boundary. */
    void reset (const DirectorSettings& settings = DirectorSettings {}) noexcept;

    /** Start listening. Idle -> Listening; a no-op in any other state. */
    void notifySessionStarted() noexcept;

    /** The user asked to stop. Pending proposals are forgotten and the director
     *  moves to Stopping; the transport stop itself is JamJoinPolicy's. */
    void notifyStopRequested() noexcept;

    /** Forget any unacknowledged proposal without touching the live command
     *  queues. Called automatically on Stop and Lost; call it explicitly on a
     *  user Reset. */
    void cancelPending() noexcept;

    // --- settings ------------------------------------------------------------

    void setSettings (const DirectorSettings& settings) noexcept;
    const DirectorSettings& settings() const noexcept { return settings_; }

    // --- control tick --------------------------------------------------------

    /** Consume one tick and return the current proposal. Deterministic. */
    DirectorDecision update (const DirectorInputs& inputs) noexcept;

    /** Report the outcome of publishing the last proposal. `true` commits the
     *  selection/repetition state; `false` keeps it pending for retry. Ignored
     *  when there is nothing pending. */
    void acknowledgePublication (bool accepted) noexcept;

    // --- observers -----------------------------------------------------------

    DirectorState state() const noexcept { return state_; }
    LibraryIndex committedGroove() const noexcept { return committedGroove_; }
    LibraryIndex committedFill() const noexcept { return committedFill_; }
    bool publicationPending() const noexcept { return pendingAck_; }

    DirectorReport report() const noexcept;

    /** Ergonomic publish helper: calls `publishFn(barChange)` only when a change
     *  is pending, feeds the bool result back through acknowledgePublication and
     *  returns whether it was accepted. The orchestrator wires this to
     *  `DrumClockBridge::requestBarChange`; tests wire it to a fake. */
    template <typename PublishFn>
    bool publishPending (PublishFn&& publishFn) noexcept
    {
        if (! pendingAck_)
            return false;
        const bool accepted = static_cast<bool> (publishFn (pendingChange_));
        acknowledgePublication (accepted);
        return accepted;
    }

private:
    void applySessionResetIfNeeded (const DirectorInputs& inputs) noexcept;
    void applyStateMachine (const DirectorInputs& inputs) noexcept;
    void updateEnvelopes (const DirectorInputs& inputs) noexcept;
    void detectBarBoundary (const DirectorInputs& inputs) noexcept;
    bool buildProposal (const DirectorInputs& inputs, DirectorDecision& out) noexcept;
    void finalizeDecision (DirectorDecision& out) noexcept;

    void commitPending() noexcept;
    LibraryIndex chooseGroove (GrooveTier tier, LibraryIndex avoid) noexcept;
    bool recentContains (LibraryIndex index) const noexcept;
    void pushRecent (LibraryIndex index) noexcept;
    bool isPhraseBoundary() const noexcept;

    std::uint64_t nextRandom() noexcept;
    double random01() noexcept;

    GrooveTier rawTierForEnvelope (float value) const noexcept;
    const StyleDescriptor& activeStyle() const noexcept;

    DirectorConfig config_;
    DirectorSettings settings_ {};

    DirectorState state_ = DirectorState::Idle;
    bool sessionActive_ = false;
    bool stopRequested_ = false;

    // Envelopes and tiers.
    float intensityEnvelope_ = 0.0f;
    float complexityEnvelope_ = 0.0f;
    int highEnergyTicks_ = 0;
    GrooveTier desiredTier_ = GrooveTier::Medium;
    GrooveTier committedTier_ = GrooveTier::Medium;

    // Selection.
    LibraryIndex committedGroove_ = kNoLibraryEntry;
    LibraryIndex committedFill_ = kNoLibraryEntry;

    // Pending proposal (retried until acknowledged).
    bool pendingAck_ = false;
    QueuedBarChange pendingChange_ {};
    GrooveTier pendingTier_ = GrooveTier::Medium;
    LibraryIndex pendingFill_ = kNoLibraryEntry;
    bool pendingBreak_ = false;
    bool pendingCrash_ = false;

    // One-shot user requests.
    bool userFillLatched_ = false;
    bool userBreakLatched_ = false;
    bool lastRequestFill_ = false;
    bool lastRequestBreak_ = false;

    // Bar / phrase tracking.
    bool haveBarPhase_ = false;
    double lastBarPhase_ = 0.0;
    bool haveTransportBar_ = false;
    int lastTransportBar_ = 0;
    bool newBarThisTick_ = false;
    std::uint64_t barsObserved_ = 0;
    std::uint64_t barsSinceLastFill_ = 0;
    std::uint64_t lastSessionGeneration_ = 0;
    bool haveSessionGeneration_ = false;
    std::uint64_t lastClockGeneration_ = 0;
    bool haveClockGeneration_ = false;
    std::uint64_t lastCursor_ = 0;
    bool haveCursor_ = false;

    // Anti-repetition ring.
    LibraryIndex recent_[kDirectorRepetitionHistory] {};
    int recentCount_ = 0;
    int recentHead_ = 0;

    // Deterministic RNG.
    std::uint64_t rngState_ = 0;

    // Diagnostics.
    std::uint64_t acceptedPublications_ = 0;
    std::uint64_t rejectedPublications_ = 0;
    std::uint64_t cancelledPublications_ = 0;
    std::uint64_t fillsEmitted_ = 0;
    std::uint64_t fillsSuppressed_ = 0;
    std::uint64_t staleResets_ = 0;
};

} // namespace jam
