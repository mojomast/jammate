// JamDirector implementation. See JamDirector.h for the contract.
//
// Threading: single control-thread owner. No locks, no allocation, no I/O.
// Determinism: the only entropy is the seeded SplitMix64 in this file, advanced
// only at the decision points documented below, so replaying the same settings
// and input sequence reproduces the same decisions and the same bar counts.

#include "JamDirector.h"

#include <cstdint>

namespace jam
{

namespace
{

float clamp01 (float value, float fallback) noexcept
{
    if (! (value == value))          // NaN
        return fallback;
    if (value < 0.0f)
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

DirectorSettings sanitize (const DirectorSettings& raw) noexcept
{
    DirectorSettings s = raw;
    const int style = static_cast<int> (s.style);
    if (style < 0 || style >= kStyleCount)
        s.style = StyleId::Rock;
    s.intensity01 = clamp01 (s.intensity01, 0.5f);
    s.complexity01 = clamp01 (s.complexity01, 0.5f);
    s.fillAmount01 = clamp01 (s.fillAmount01, kDirectorNeutralFillAmount);
    return s;
}

/** Tolerance for detecting a bar-phase wrap. barPhase01 is in [0, 1); a wrap is
 *  a drop towards zero. Small enough never to fire on normal progression. */
constexpr double kBarPhaseEpsilon = 1e-6;

/** How strongly input energy can shift the user's intensity target. Structural:
 *  the user setting remains the baseline; energy only nudges it. */
constexpr float kEnergyInfluence = 0.4f;

} // namespace

JamDirector::JamDirector (const DirectorConfig& config) noexcept
    : config_ (config)
{
    reset();
}

void JamDirector::reset (const DirectorSettings& settings) noexcept
{
    settings_ = sanitize (settings);
    lastRequestFill_ = settings_.requestFill;
    lastRequestBreak_ = settings_.requestBreak;

    state_ = DirectorState::Idle;
    sessionActive_ = false;
    stopRequested_ = false;

    intensityEnvelope_ = settings_.intensity01;
    complexityEnvelope_ = settings_.complexity01;
    highEnergyTicks_ = 0;
    desiredTier_ = rawTierForEnvelope (intensityEnvelope_);
    committedTier_ = desiredTier_;

    committedGroove_ = kNoLibraryEntry;
    committedFill_ = kNoLibraryEntry;

    pendingAck_ = false;
    pendingChange_ = QueuedBarChange {};
    pendingTier_ = desiredTier_;
    pendingFill_ = kNoLibraryEntry;
    pendingBreak_ = false;
    pendingCrash_ = false;

    userFillLatched_ = false;
    userBreakLatched_ = false;

    haveBarPhase_ = false;
    lastBarPhase_ = 0.0;
    haveTransportBar_ = false;
    lastTransportBar_ = 0;
    newBarThisTick_ = false;
    barsObserved_ = 0;
    barsSinceLastFill_ = 0;
    lastSessionGeneration_ = 0;
    haveSessionGeneration_ = false;
    lastClockGeneration_ = 0;
    haveClockGeneration_ = false;
    lastCursor_ = 0;
    haveCursor_ = false;

    recentCount_ = 0;
    recentHead_ = 0;
    for (int i = 0; i < kDirectorRepetitionHistory; ++i)
        recent_[i] = kNoLibraryEntry;

    rngState_ = config_.randomSeed != 0 ? config_.randomSeed : 0x9E3779B97F4A7C15ull;

    acceptedPublications_ = 0;
    rejectedPublications_ = 0;
    cancelledPublications_ = 0;
    fillsEmitted_ = 0;
    fillsSuppressed_ = 0;
    staleResets_ = 0;
}

void JamDirector::notifySessionStarted() noexcept
{
    if (state_ == DirectorState::Idle)
    {
        state_ = DirectorState::Listening;
        sessionActive_ = true;
        stopRequested_ = false;
    }
}

void JamDirector::notifyStopRequested() noexcept
{
    // Forgetting the pending decision is required: a proposal from before the
    // stop must not be published after it. The live command queue is untouched.
    cancelPending();
    userFillLatched_ = false;
    userBreakLatched_ = false;
    stopRequested_ = true;
    state_ = DirectorState::Stopping;
}

void JamDirector::cancelPending() noexcept
{
    if (! pendingAck_)
        return;

    pendingAck_ = false;
    pendingChange_ = QueuedBarChange {};
    pendingFill_ = kNoLibraryEntry;
    pendingBreak_ = false;
    pendingCrash_ = false;
    ++cancelledPublications_;
}

void JamDirector::notifyStopCompleted() noexcept
{
    if (state_ != DirectorState::Stopping)
        return;

    cancelPending();
    userFillLatched_ = false;
    userBreakLatched_ = false;
    stopRequested_ = false;
    sessionActive_ = false;
    state_ = DirectorState::Idle;

    // The transport is no longer playing our selection, so a later restart must
    // propose a groove rather than assume the old one is sounding.
    committedGroove_ = kNoLibraryEntry;
    committedFill_ = kNoLibraryEntry;
    committedTier_ = desiredTier_;
    recentCount_ = 0;
    recentHead_ = 0;
    haveBarPhase_ = false;
    haveTransportBar_ = false;
    haveCursor_ = false;
    barsObserved_ = 0;
    barsSinceLastFill_ = 0;
}

void JamDirector::setSettings (const DirectorSettings& settings) noexcept
{
    const DirectorSettings next = sanitize (settings);

    if (next.style != settings_.style)
    {
        // The committed groove/fill belong to the old style. Abandon the
        // selection (but not the user envelopes, which are style-independent)
        // so a valid change for the new style is proposed.
        committedGroove_ = kNoLibraryEntry;
        committedFill_ = kNoLibraryEntry;
        committedTier_ = desiredTier_;
        recentCount_ = 0;
        recentHead_ = 0;

        pendingAck_ = false;
        pendingChange_ = QueuedBarChange {};
        pendingFill_ = kNoLibraryEntry;
        pendingBreak_ = false;
        pendingCrash_ = false;
    }

    settings_ = next;

    // Edge-trigger the one-shot requests so a held UI value requests once.
    if (settings_.requestFill && ! lastRequestFill_)
        userFillLatched_ = true;
    if (settings_.requestBreak && ! lastRequestBreak_)
        userBreakLatched_ = true;
    lastRequestFill_ = settings_.requestFill;
    lastRequestBreak_ = settings_.requestBreak;
}

const StyleDescriptor& JamDirector::activeStyle() const noexcept
{
    return StyleCatalog::style (settings_.style);
}

GrooveTier JamDirector::rawTierForEnvelope (float value) const noexcept
{
    if (value < kDirectorTierLowEdge)
        return GrooveTier::Low;
    if (value < kDirectorTierHighEdge)
        return GrooveTier::Medium;
    return GrooveTier::High;
}

std::uint64_t JamDirector::nextRandom() noexcept
{
    // SplitMix64: fast, well-distributed, fully deterministic from the seed.
    std::uint64_t z = (rngState_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

double JamDirector::random01() noexcept
{
    // Top 53 bits -> exactly [0, 1), like a uniform double.
    return static_cast<double> (nextRandom() >> 11) * (1.0 / 9007199254740992.0);
}

bool JamDirector::recentContains (LibraryIndex index) const noexcept
{
    int window = effectiveRepetitionWindowBars();
    if (window > kDirectorRepetitionHistory)
        window = kDirectorRepetitionHistory;
    if (window > recentCount_)
        window = recentCount_;

    for (int i = 0; i < window; ++i)
    {
        const int slot = (recentHead_ - 1 - i + 2 * kDirectorRepetitionHistory)
                         % kDirectorRepetitionHistory;
        if (recent_[slot] == index)
            return true;
    }
    return false;
}

int JamDirector::effectiveRepetitionWindowBars() const noexcept
{
    // The style overlay owns the window; DirectorConfig is the fallback/default
    // for a style that does not specify one (SPEC.md 13.1).
    const int styleWindow = activeStyle().minRepetitionDistanceBars;
    const int window = styleWindow > 0 ? styleWindow : config_.minimumRepetitionDistance;
    return window < 0 ? 0 : window;
}

void JamDirector::pushRecent (LibraryIndex index) noexcept
{
    recent_[recentHead_] = index;
    recentHead_ = (recentHead_ + 1) % kDirectorRepetitionHistory;
    if (recentCount_ < kDirectorRepetitionHistory)
        ++recentCount_;
}

LibraryIndex JamDirector::chooseGroove (GrooveTier tier, LibraryIndex avoid) noexcept
{
    const StyleDescriptor& s = activeStyle();

    // Fall back to a non-empty tier rather than proposing nothing.
    if (s.grooveCount[(int) tier] <= 0)
    {
        if (s.grooveCount[(int) GrooveTier::Medium] > 0)
            tier = GrooveTier::Medium;
        else if (s.grooveCount[(int) GrooveTier::Low] > 0)
            tier = GrooveTier::Low;
        else if (s.grooveCount[(int) GrooveTier::High] > 0)
            tier = GrooveTier::High;
        else
            return kNoLibraryEntry;
    }

    const int n = s.grooveCount[(int) tier];
    if (n <= 0)
        return kNoLibraryEntry;

    LibraryIndex candidates[kMaxTierPatterns] {};
    int count = 0;

    for (int i = 0; i < n; ++i)
    {
        const LibraryIndex index = s.grooves[(int) tier][i].index;
        if (index == avoid || recentContains (index))
            continue;
        candidates[count++] = index;
    }

    if (count == 0)
    {
        // Every alternative is too recent; accept any non-immediate-repeat.
        for (int i = 0; i < n; ++i)
        {
            const LibraryIndex index = s.grooves[(int) tier][i].index;
            if (index != avoid)
                candidates[count++] = index;
        }
    }

    if (count == 0)
        return s.grooves[(int) tier][0].index;   // only one pattern and it is `avoid`

    return candidates[static_cast<int> (nextRandom() % static_cast<std::uint64_t> (count))];
}

bool JamDirector::isPhraseBoundary() const noexcept
{
    return barsObserved_ > 0 && (barsObserved_ % static_cast<std::uint64_t> (kDirectorPhraseLengthBars)) == 0;
}

void JamDirector::applySessionResetIfNeeded (const DirectorInputs& inputs) noexcept
{
    bool reset = false;

    if (inputs.discontinuity)
        reset = true;

    if (inputs.sessionGeneration != 0)
    {
        if (! haveSessionGeneration_ || inputs.sessionGeneration != lastSessionGeneration_)
        {
            if (haveSessionGeneration_)
                reset = true;
            haveSessionGeneration_ = true;
            lastSessionGeneration_ = inputs.sessionGeneration;
        }
    }

    // The MusicalClock's generation is a publish counter, not a session id, but
    // it only ever increases within a session. A drop means the clock was reset
    // underneath us, so our bar/phase tracking is stale.
    if (haveClockGeneration_ && inputs.clock.generation < lastClockGeneration_)
        reset = true;
    haveClockGeneration_ = true;
    lastClockGeneration_ = inputs.clock.generation;

    if (! reset)
        return;

    ++staleResets_;
    cancelPending();
    userFillLatched_ = false;
    userBreakLatched_ = false;

    committedGroove_ = kNoLibraryEntry;
    committedFill_ = kNoLibraryEntry;
    committedTier_ = rawTierForEnvelope (intensityEnvelope_);
    recentCount_ = 0;
    recentHead_ = 0;

    haveBarPhase_ = false;
    haveTransportBar_ = false;
    haveCursor_ = false;
    barsObserved_ = 0;
    barsSinceLastFill_ = 0;
    highEnergyTicks_ = 0;
    intensityEnvelope_ = settings_.intensity01;
    complexityEnvelope_ = settings_.complexity01;

    if (sessionActive_)
        state_ = DirectorState::Listening;
}

void JamDirector::applyStateMachine (const DirectorInputs& inputs) noexcept
{
    if (stopRequested_)
    {
        state_ = DirectorState::Stopping;
        return;
    }

    const bool locked = inputs.clock.lockState == ClockLockState::Locked;
    const bool confident = inputs.clock.confidence01 >= config_.joinConfidenceThreshold;
    const bool usable = locked && confident;
    const bool holdover = inputs.clock.lockState == ClockLockState::Holdover;
    const bool lost = inputs.clock.lockState == ClockLockState::Lost;
    const bool echo = inputs.playbackEchoPlaying;

    switch (state_)
    {
        case DirectorState::Idle:
            break;

        case DirectorState::Listening:
            if (usable)
                state_ = DirectorState::ReadyToJoin;
            break;

        case DirectorState::ReadyToJoin:
            if (echo)
                state_ = DirectorState::Playing;
            else if (! usable)
                state_ = lost ? DirectorState::Reacquiring : DirectorState::Listening;
            break;

        case DirectorState::Playing:
            if (! echo)
                state_ = DirectorState::Stopping;
            else if (holdover)
                state_ = DirectorState::Holdover;
            else if (lost)
                state_ = DirectorState::Reacquiring;
            break;

        case DirectorState::Holdover:
            if (! echo)
                state_ = DirectorState::Stopping;
            else if (usable)
                state_ = DirectorState::Playing;
            else if (lost)
                state_ = DirectorState::Reacquiring;
            break;

        case DirectorState::Reacquiring:
            if (! echo)
                state_ = DirectorState::Stopping;
            else if (usable)
                state_ = DirectorState::Playing;
            break;

        case DirectorState::Stopping:
            break;
    }

    // A stop or a lost lock invalidates any unacknowledged musical proposal.
    if (state_ == DirectorState::Stopping || inputs.clock.lockState == ClockLockState::Lost)
        cancelPending();
}

void JamDirector::enforcePendingSafety (const DirectorInputs& inputs) noexcept
{
    if (! pendingAck_)
        return;

    // A lifecycle that is no longer running/stopping-safe, a clock that is not
    // Locked (Holdover reduces confidence), or a fill proposed under a belief
    // that has since dropped below the fill threshold must not stay alive. The
    // proposal is forgotten; live queues are untouched.
    if (! inputs.lifecycleAllowsPerformance)
    {
        cancelPending();
        return;
    }
    if (inputs.clock.lockState != ClockLockState::Locked)
    {
        cancelPending();
        return;
    }
    if (pendingFill_ != kNoLibraryEntry
        && inputs.clock.confidence01 < config_.fillConfidenceThreshold)
    {
        cancelPending();
    }
}

void JamDirector::updateEnvelopes (const DirectorInputs& inputs) noexcept
{
    // Intensity: the user setting is the baseline, guitar energy nudges it
    // (SPEC.md 14), and the result glides with asymmetric attack/release so the
    // drummer never jumps tiers.
    const float energyTarget = clamp01 (settings_.intensity01
                                            + (inputs.energy01 - 0.5f) * kEnergyInfluence,
                                        settings_.intensity01);
    const float intensityGain =
        energyTarget > intensityEnvelope_ ? config_.energyAttack : config_.energyRelease;
    intensityEnvelope_ = clamp01 (intensityEnvelope_
                                      + (energyTarget - intensityEnvelope_) * intensityGain,
                                  intensityEnvelope_);

    // Complexity: a user control, so it simply glides to the requested value.
    const float complexityTarget = settings_.complexity01;
    const float complexityGain = complexityTarget > complexityEnvelope_
                                     ? kDirectorComplexityAttack
                                     : kDirectorComplexityRelease;
    complexityEnvelope_ = clamp01 (complexityEnvelope_
                                       + (complexityTarget - complexityEnvelope_) * complexityGain,
                                   complexityEnvelope_);

    // Tier with hysteresis: promote one step at a time, gate Low -> High behind
    // sustained energy, demote only once the envelope is clearly past the edge.
    const GrooveTier raw = rawTierForEnvelope (intensityEnvelope_);
    if (raw > desiredTier_)
    {
        if (desiredTier_ == GrooveTier::Medium)
        {
            ++highEnergyTicks_;
            if (highEnergyTicks_ >= config_.highEnergySustainTicks)
            {
                desiredTier_ = GrooveTier::High;
                highEnergyTicks_ = 0;
            }
        }
        else
        {
            desiredTier_ = static_cast<GrooveTier> (static_cast<int> (desiredTier_) + 1);
            highEnergyTicks_ = 0;
        }
    }
    else if (raw < desiredTier_)
    {
        const float demoteEdge = desiredTier_ == GrooveTier::High ? kDirectorTierHighEdge
                                                                  : kDirectorTierLowEdge;
        if (intensityEnvelope_ < demoteEdge - config_.intensityDeadband)
            desiredTier_ = static_cast<GrooveTier> (static_cast<int> (desiredTier_) - 1);
        highEnergyTicks_ = 0;
    }
    else
    {
        highEnergyTicks_ = 0;
    }
}

void JamDirector::detectBarBoundary (const DirectorInputs& inputs) noexcept
{
    newBarThisTick_ = false;

    // The Musical Clock's bar phase is the SINGLE authoritative bar-advance
    // source. The explicit transport is a confirmation/re-anchor source only:
    // it never adds a bar of its own, which is what previously caused one
    // physical bar to be counted twice when a lagged transport.bar increment
    // landed on the tick after the phase wrap.
    if (! haveBarPhase_)
    {
        haveBarPhase_ = true;
        lastBarPhase_ = inputs.clock.barPhase01;
    }
    else
    {
        if (inputs.clock.barPhase01 + kBarPhaseEpsilon < lastBarPhase_)
            newBarThisTick_ = true;
        lastBarPhase_ = inputs.clock.barPhase01;
    }

    if (inputs.transport.playing && inputs.transport.bar > 0)
    {
        if (haveTransportBar_)
        {
            const int delta = inputs.transport.bar - lastTransportBar_;

            // A skip or a backward move is a resync/discontinuity in the rendered
            // grid. Re-baseline the phase tracker so the next genuine wrap is
            // measured from the transport's position, and never fabricate a bar
            // count from the transport jump itself.
            if (delta < 0 || delta > 1)
                haveBarPhase_ = false;
            // delta == 0: stall; delta == 1: confirmation of the same physical
            // bar the clock already advances. Neither increments the counter.
        }
        lastTransportBar_ = inputs.transport.bar;
        haveTransportBar_ = true;
    }
    else
    {
        haveTransportBar_ = false;
    }

    if (newBarThisTick_)
    {
        ++barsObserved_;
        ++barsSinceLastFill_;
    }
}

bool JamDirector::buildProposal (const DirectorInputs& inputs, DirectorDecision& out) noexcept
{
    // The live policy owns join/stop; the director only tunes the performance
    // once playback is actually engaged (real echo). Before that it holds.
    if (state_ != DirectorState::Playing)
        return false;

    // A live policy that is stopping forbids and forgets any proposal. This is
    // checked before the pending re-emit so a stop never leaves a proposal alive.
    if (! inputs.lifecycleAllowsPerformance)
    {
        cancelPending();
        return false;
    }

    if (inputs.clock.lockState != ClockLockState::Locked)
        return false;   // Holdover/Lost move the state machine out of Playing

    // A pending proposal is re-emitted unchanged until the caller acknowledges
    // it: rejected publications retry, they do not get replaced.
    if (pendingAck_)
    {
        out.hasBarChange = true;
        out.barChange = pendingChange_;
        out.fillRequested = pendingFill_ != kNoLibraryEntry;
        out.breakRequested = pendingBreak_;
        return true;
    }

    const bool directable = inputs.clock.confidence01 >= config_.joinConfidenceThreshold;
    const bool phrase = isPhraseBoundary();
    const bool initial = committedGroove_ == kNoLibraryEntry;

    bool wantChange = false;
    bool needGroove = false;
    bool wantFill = false;
    bool wantBreak = false;

    // Explicit user actions are honoured whenever playback is engaged.
    if (userBreakLatched_)
    {
        wantBreak = true;
        wantFill = false;
        wantChange = true;
        needGroove = true;
    }
    if (userFillLatched_)
    {
        wantFill = true;
        wantBreak = false;
        wantChange = true;
    }

    bool suppressedAutomaticFill = false;

    // Groove/tier/phrase changes need a strong belief; a confidence dip only
    // suppresses, it does not propose a new pattern.
    if (directable)
    {
        if (initial)
        {
            wantChange = true;
            needGroove = true;
        }
        if (desiredTier_ != committedTier_)
        {
            wantChange = true;
            needGroove = true;
        }
        if (newBarThisTick_ && phrase)
        {
            // A phrase boundary is the musical reason to rotate the groove.
            wantChange = true;
            needGroove = true;
        }
    }

    // Fills are allowed from a moderate belief and HARD-suppressed below the
    // configured fill threshold, so a confidence dip records suppression
    // instead of silently proposing a fill.
    if (newBarThisTick_ && ! wantFill && settings_.fillAmount01 > 0.0f
        && barsSinceLastFill_ >= static_cast<std::uint64_t> (kDirectorMinFillGapBars))
    {
        double probability = config_.fillBaseChance
                             + (phrase ? config_.fillPhraseBoundaryBonus : 0.0);
        probability *= static_cast<double> (settings_.fillAmount01)
                       / static_cast<double> (kDirectorNeutralFillAmount);
        if (inputs.onsetEvent && phrase)
            probability += static_cast<double> (kDirectorOnsetFillBonus)
                           * static_cast<double> (inputs.onsetStrength01);

        if (inputs.clock.confidence01 < config_.fillConfidenceThreshold)
        {
            probability = 0.0;
            suppressedAutomaticFill = true;
        }
        else if (inputs.clock.confidence01 < config_.joinConfidenceThreshold)
        {
            probability *= static_cast<double> (config_.fillConfidenceSuppression);
        }

        if (probability > 1.0)
            probability = 1.0;

        if (probability > 0.0 && random01() < probability)
        {
            wantFill = true;
            wantChange = true;
        }
    }

    if (! wantChange)
    {
        if (suppressedAutomaticFill)
            ++fillsSuppressed_;
        return false;
    }

    LibraryIndex groove = committedGroove_;
    if (needGroove || wantBreak)
        groove = chooseGroove (wantBreak ? GrooveTier::Low : desiredTier_, committedGroove_);
    if (groove == kNoLibraryEntry)
        groove = committedGroove_;
    if (groove == kNoLibraryEntry)
        return false;   // nothing valid to propose yet

    const StyleDescriptor& style = activeStyle();

    FillKind fillKind = FillKind::Short;
    LibraryIndex fill = kNoLibraryEntry;

    if (wantFill)
    {
        if (phrase)
            fillKind = random01() < 0.5 ? FillKind::Transition : FillKind::Long;
        else
            fillKind = FillKind::Short;

        const int available = style.fillCount[(int) fillKind];
        if (available > 0)
        {
            fill = style.fills[(int) fillKind]
                              [static_cast<int> (nextRandom()
                                                 % static_cast<std::uint64_t> (available))]
                       .index;
        }
        else if (style.fillCount[(int) FillKind::Short] > 0)
        {
            fillKind = FillKind::Short;
            fill = style.fills[(int) FillKind::Short][0].index;
        }
        else
        {
            wantFill = false;
        }
    }

    pendingChange_ = QueuedBarChange {};
    pendingChange_.generation = inputs.clock.generation;
    pendingChange_.groove = groove;
    pendingChange_.fill = fill;
    pendingChange_.intensity01 = intensityEnvelope_;
    pendingChange_.swing01 = style.defaultSwing01;
    pendingChange_.humanizeVelocity = style.humanizeVelocity;
    pendingChange_.humanizeTiming = style.humanizeTiming;
    pendingChange_.humanizeRoundRobin = style.humanizeRoundRobin;

    pendingAck_ = true;
    pendingTier_ = desiredTier_;
    pendingFill_ = fill;
    pendingBreak_ = wantBreak;
    pendingCrash_ = wantFill && fillKind == FillKind::Transition;

    userFillLatched_ = false;
    userBreakLatched_ = false;

    out.hasBarChange = true;
    out.barChange = pendingChange_;
    out.fillRequested = fill != kNoLibraryEntry;
    out.breakRequested = wantBreak;
    return true;
}

void JamDirector::commitPending() noexcept
{
    if (pendingChange_.groove != kNoLibraryEntry)
    {
        committedGroove_ = pendingChange_.groove;
        pushRecent (committedGroove_);
    }
    if (pendingChange_.fill != kNoLibraryEntry)
    {
        committedFill_ = pendingChange_.fill;
        ++fillsEmitted_;
        barsSinceLastFill_ = 0;
    }
    committedTier_ = pendingTier_;
    pendingAck_ = false;
    pendingFill_ = kNoLibraryEntry;
    pendingBreak_ = false;
    pendingCrash_ = false;
}

void JamDirector::acknowledgePublication (bool accepted) noexcept
{
    if (! pendingAck_)
        return;

    if (accepted)
    {
        ++acceptedPublications_;
        commitPending();
    }
    else
    {
        // Keep the proposal pending; the next update re-emits it unchanged.
        ++rejectedPublications_;
    }
}

DirectorDecision JamDirector::update (const DirectorInputs& inputs) noexcept
{
    applySessionResetIfNeeded (inputs);

    newBarThisTick_ = false;

    // Safety transitions run on EVERY tick, including a repeated audio cursor,
    // so an idempotent tick can still observe Lost/Holdover/lifecycle and cancel
    // a pending proposal. Only the energy envelope, phrase counter and RNG are
    // held back for a repeated cursor.
    applyStateMachine (inputs);
    enforcePendingSafety (inputs);

    const bool duplicateCursor = inputs.audioCursor != 0 && haveCursor_
                                 && inputs.audioCursor == lastCursor_;
    if (duplicateCursor)
    {
        DirectorDecision held;
        finalizeDecision (held);
        return held;
    }

    if (inputs.audioCursor != 0)
    {
        haveCursor_ = true;
        lastCursor_ = inputs.audioCursor;
    }

    updateEnvelopes (inputs);
    detectBarBoundary (inputs);

    DirectorDecision out;
    if (sessionActive_ && state_ != DirectorState::Stopping)
        buildProposal (inputs, out);
    finalizeDecision (out);
    return out;
}

void JamDirector::finalizeDecision (DirectorDecision& out) noexcept
{
    // Re-emit a pending proposal so retries survive idempotent ticks.
    if (! out.hasBarChange && pendingAck_)
    {
        out.hasBarChange = true;
        out.barChange = pendingChange_;
    }

    out.state = state_;
    out.tier = desiredTier_;
    out.intensityEnvelope01 = intensityEnvelope_;
    out.complexityEnvelope01 = complexityEnvelope_;
    out.barsObserved = barsObserved_;
    out.phraseIndex = static_cast<int> (barsObserved_
                                        / static_cast<std::uint64_t> (kDirectorPhraseLengthBars));

    out.fillRequested = (pendingAck_ && pendingFill_ != kNoLibraryEntry) || userFillLatched_;
    out.breakRequested = (pendingAck_ && pendingBreak_) || userBreakLatched_;

    out.intent.intensity01 = intensityEnvelope_;
    out.intent.complexity01 = complexityEnvelope_;
    out.intent.fillAmount01 = settings_.fillAmount01;
    out.intent.swing01 = activeStyle().defaultSwing01;
    out.intent.requestFill = out.fillRequested;
    out.intent.requestBreak = out.breakRequested;
    out.intent.requestCrash = pendingAck_ && pendingCrash_;
    // The transport stop is owned by JamJoinPolicy; the director never claims it.
    out.intent.requestStop = false;
    out.intent.sectionIndex = out.phraseIndex;
}

DirectorReport JamDirector::report() const noexcept
{
    DirectorReport r;
    r.state = state_;
    r.tier = desiredTier_;
    r.settings = settings_;
    r.committedGroove = committedGroove_;
    r.committedFill = committedFill_;
    r.pendingGroove = pendingAck_ ? pendingChange_.groove : kNoLibraryEntry;
    r.pendingFill = pendingAck_ ? pendingFill_ : kNoLibraryEntry;
    r.pendingAck = pendingAck_;
    r.pendingBreak = pendingAck_ && pendingBreak_;
    r.pendingCrash = pendingAck_ && pendingCrash_;
    r.pendingChange = pendingAck_ ? pendingChange_ : QueuedBarChange {};
    r.intensityEnvelope01 = intensityEnvelope_;
    r.complexityEnvelope01 = complexityEnvelope_;
    r.repetitionWindowBars = effectiveRepetitionWindowBars();
    r.barsObserved = barsObserved_;
    r.acceptedPublications = acceptedPublications_;
    r.rejectedPublications = rejectedPublications_;
    r.cancelledPublications = cancelledPublications_;
    r.fillsEmitted = fillsEmitted_;
    r.fillsSuppressed = fillsSuppressed_;
    r.staleResets = staleResets_;
    r.intent.intensity01 = intensityEnvelope_;
    r.intent.complexity01 = complexityEnvelope_;
    r.intent.fillAmount01 = settings_.fillAmount01;
    r.intent.swing01 = activeStyle().defaultSwing01;
    r.intent.requestFill = (pendingAck_ && pendingFill_ != kNoLibraryEntry) || userFillLatched_;
    r.intent.requestBreak = (pendingAck_ && pendingBreak_) || userBreakLatched_;
    r.intent.requestCrash = pendingAck_ && pendingCrash_;
    r.intent.requestStop = false;   // owned by JamJoinPolicy
    r.intent.sectionIndex = static_cast<int> (barsObserved_
                                              / static_cast<std::uint64_t> (kDirectorPhraseLengthBars));
    return r;
}

} // namespace jam
