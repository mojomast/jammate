#include "MusicalClock.h"

#include <algorithm>
#include <cmath>

namespace jam
{

namespace
{
// Unit conversion. Not a tunable: 60 seconds in a minute.
constexpr double kSecondsPerMinute = 60.0;

// Metric-ambiguity octave ratios. Not tunables: a half/double is by definition
// 0.5x / 2.0x. How close a candidate must be is ClockConfig::metricAmbiguityRatio.
constexpr double kHalfTimeRatio = 0.5;
constexpr double kDoubleTimeRatio = 2.0;

// Half of one phase revolution. A structural property of wrapping a phase into
// [0,1), not a tunable.
constexpr double kPhaseHalfTurn = 0.5;

double clampDouble (double value, double low, double high)
{
    return std::max (low, std::min (high, value));
}

double wrap01 (double value)
{
    value = std::fmod (value, 1.0);
    if (value < 0.0)
        value += 1.0;
    return value;
}

/** Signed phase error in (-0.5, 0.5], the shortest way round the circle. */
double wrapSigned (double value)
{
    value = wrap01 (value);
    if (value > kPhaseHalfTurn)
        value -= 1.0;
    return value;
}

int modeIndex (TempoMode mode) noexcept
{
    const int index = static_cast<int> (mode);
    return (index >= 0 && index < kTempoModeCount) ? index : 0;
}

bool snapshotsEqual (const ClockSnapshot& a, const ClockSnapshot& b)
{
    return a.bpm == b.bpm
        && a.beatPhase01 == b.beatPhase01
        && a.barPhase01 == b.barPhase01
        && a.beatInBar == b.beatInBar
        && a.beatsPerBar == b.beatsPerBar
        && a.beatUnit == b.beatUnit
        && a.confidence01 == b.confidence01
        && a.lockState == b.lockState
        && a.tempoFrozen == b.tempoFrozen;
}
} // namespace

MusicalClock::MusicalClock (const ClockConfig& config)
    : config_ (config)
{
    mode_ = config_.defaultMode;
    beatsPerBar_ = config_.defaultBeatsPerBar;
    beatUnit_ = config_.defaultBeatUnit;
    sampleRate_ = config_.defaultSampleRate;
    bpm_ = config_.fallbackBpm;
    publish();
}

void MusicalClock::reset()
{
    state_ = ClockLockState::Acquiring;
    mode_ = config_.defaultMode;
    tempoFrozen_ = false;

    sampleTime_ = 0;
    sampleRate_ = config_.defaultSampleRate;
    beatsElapsed_ = 0.0;
    bpm_ = config_.fallbackBpm;

    beatsPerBar_ = config_.defaultBeatsPerBar;
    beatUnit_ = config_.defaultBeatUnit;

    confidence_ = 0.0;
    secondsSinceUsableEvidence_ = 0.0;
    acquisitionSeconds_ = 0.0;
    consistentObservations_ = 0;
    phaseEvidenceCount_ = 0;
    haveLastPhase_ = false;
    lastPhase01_ = 0.0;
    lastPhaseSampleTime_ = 0;

    jumpStreak_ = 0;
    jumpDirection_ = 0;
    metricTwinStreak_ = 0;
    metricTwinDirection_ = 0;

    haveLastTap_ = false;
    lastTapSampleTime_ = 0;
    tapIntervalSum_ = 0.0;
    tapIntervalCount_ = 0;

    publish();
}

void MusicalClock::setMode (TempoMode mode)
{
    if (mode_ == mode)
        return;
    mode_ = mode;
    publish();
}

TempoMode MusicalClock::mode() const noexcept
{
    return mode_;
}

bool MusicalClock::isUsable (const RhythmObservation& observation) const
{
    if (observation.silence)
        return false;
    return observation.energyRmsDbfs >= config_.silenceRmsDbfs;
}

void MusicalClock::observe (const RhythmObservation& observation)
{
    if (isUsable (observation))
    {
        secondsSinceUsableEvidence_ = 0.0;
        handleUsableObservation (observation);
    }
    else
    {
        handleWeakObservation (observation);
    }
    publish();
}

void MusicalClock::handleUsableObservation (const RhythmObservation& observation)
{
    updateConfidence (static_cast<double> (observation.beatConfidence01));

    // Tempo: the clock owns the belief. A single observation can never become
    // the published tempo; see fuseTempoCandidate(). In Fixed mode the tempo is
    // user-owned and tracker tempo candidates are ignored entirely.
    if (mode_ != TempoMode::Fixed && ! tempoFrozen_ && observation.bpmCandidate > 0.0f)
        fuseTempoCandidate (static_cast<double> (observation.bpmCandidate));

    // Acquisition agreement: how many recent tempo candidates have sat within
    // tempoAgreementTolerance of the belief. Used only to decide when to lock.
    if (observation.bpmCandidate > 0.0f && bpm_ > 0.0)
    {
        const double relative =
            std::fabs (static_cast<double> (observation.bpmCandidate) - bpm_) / bpm_;
        if (relative <= static_cast<double> (config_.tempoAgreementTolerance))
            ++consistentObservations_;
        else if (consistentObservations_ > 0)
            --consistentObservations_;
    }

    if (observation.phaseValid)
    {
        ++phaseEvidenceCount_;
        haveLastPhase_ = true;
        lastPhase01_ = static_cast<double> (observation.beatPhase01);
        lastPhaseSampleTime_ = observation.inputSampleTime;

        // Phase is corrected only once the clock is confident enough to trust
        // its own grid (Locked). While acquiring, phase is snapped at lock.
        if (state_ == ClockLockState::Locked)
            applyPhaseCorrection (observation);
    }

    // Recovery from loss. Fresh usable evidence either re-locks (if confidence
    // survived the gap) or restarts acquisition.
    if (state_ == ClockLockState::Holdover)
    {
        if (confidence_ >= static_cast<double> (config_.lockConfidenceThreshold))
            state_ = ClockLockState::Locked;
        else
        {
            state_ = ClockLockState::Acquiring;
            resetAcquisition();
        }
    }
    else if (state_ == ClockLockState::Lost)
    {
        // SPEC 10.1 Lost: "Return to Acquiring" so a fresh lock can form.
        state_ = ClockLockState::Acquiring;
        resetAcquisition();
    }

    maybeLock();
}

void MusicalClock::handleWeakObservation (const RhythmObservation& observation)
{
    (void) observation;

    // Silence/weakness is evidence of absence. Confidence decays; the actual
    // Locked -> Holdover -> Lost transitions are time-driven in advance().
    const double target = (state_ == ClockLockState::Holdover)
                              ? static_cast<double> (config_.holdoverConfidenceFloor)
                              : 0.0;
    updateConfidence (target);
}

void MusicalClock::maybeLock()
{
    if (state_ != ClockLockState::Acquiring)
        return;
    if (confidence_ < static_cast<double> (config_.lockConfidenceThreshold))
        return;
    if (phaseEvidenceCount_ < config_.minimumLockedObservations)
        return;
    if (acquisitionSeconds_ < config_.acquireWindowSeconds)
        return;

    // In Fixed mode tempo candidates are irrelevant, so tempo agreement is not
    // part of the lock decision; the clock still locks onto the user's phase.
    if (mode_ != TempoMode::Fixed && consistentObservations_ < config_.minimumLockedObservations)
        return;

    state_ = ClockLockState::Locked;

    // Snap the grid onto the most recent phase evidence. The integer part of
    // the beat count is preserved so bar position stays continuous across lock.
    if (haveLastPhase_)
        alignPhaseTo (lastPhaseSampleTime_, lastPhase01_);
}

void MusicalClock::resetAcquisition()
{
    acquisitionSeconds_ = 0.0;
    consistentObservations_ = 0;
    phaseEvidenceCount_ = 0;
    haveLastPhase_ = false;
}

void MusicalClock::fuseTempoCandidate (double candidate)
{
    if (candidate <= 0.0)
        return;

    const double target =
        clampDouble (candidate, config_.minAutoFollowBpm, config_.maxAutoFollowBpm);

    if (bpm_ <= 0.0)
    {
        bpm_ = target;
        return;
    }

    // 1. Metric ambiguity first. A candidate that is the half or double of the
    //    belief is a tempo-octave question, not a tempo change. It must repeat
    //    metricConfirmationObservations times before it is believed (SPEC 10.2).
    const int twin = metricTwinDirection (candidate, bpm_);
    if (twin != 0 && state_ == ClockLockState::Locked)
    {
        if (twin == metricTwinDirection_)
            ++metricTwinStreak_;
        else
        {
            metricTwinDirection_ = twin;
            metricTwinStreak_ = 1;
        }

        jumpStreak_ = 0;
        jumpDirection_ = 0;

        if (metricTwinStreak_ >= config_.metricConfirmationObservations)
        {
            applyMetricCorrection (twin > 0 ? kDoubleTimeRatio : kHalfTimeRatio);
            metricTwinStreak_ = 0;
        }
        return;
    }

    // Not a metric twin: any sustained twin evidence is broken.
    metricTwinStreak_ = 0;
    metricTwinDirection_ = 0;

    // 2. Isolated-jump gate. A change larger than isolatedJumpRejectRatio is
    //    rejected until it is seen isolatedJumpReinforceCount times in the same
    //    direction (SPEC 10.2).
    const double relative = (target - bpm_) / bpm_;
    if (std::fabs (relative) > static_cast<double> (config_.isolatedJumpRejectRatio))
    {
        const int direction = relative > 0.0 ? 1 : -1;
        if (direction == jumpDirection_)
            ++jumpStreak_;
        else
        {
            jumpDirection_ = direction;
            jumpStreak_ = 1;
        }

        if (jumpStreak_ >= config_.isolatedJumpReinforceCount)
        {
            // Reinforced. While Locked we still slew rather than snap, so a
            // genuine tempo change is heard as a drift, not a discontinuity.
            if (state_ == ClockLockState::Locked)
                slewToward (target);
            else
                bpm_ = target;
            jumpStreak_ = 0;
        }
        return;
    }

    // 3. Ordinary candidate: slew-limited move toward it.
    jumpStreak_ = 0;
    jumpDirection_ = 0;
    slewToward (target);
}

void MusicalClock::slewToward (double target)
{
    const double maxStep =
        bpm_ * static_cast<double> (config_.maxSlewRatioPerObservation[modeIndex (mode_)]);
    const double difference = target - bpm_;

    if (std::fabs (difference) <= maxStep)
        bpm_ = target;
    else
        bpm_ += difference > 0.0 ? maxStep : -maxStep;
}

int MusicalClock::metricTwinDirection (double candidate, double belief) const
{
    if (belief <= 0.0)
        return 0;

    const double ratio = candidate / belief;
    const double tolerance = static_cast<double> (config_.metricAmbiguityRatio);

    if (std::fabs (ratio - kHalfTimeRatio) <= tolerance * kHalfTimeRatio)
        return -1;
    if (std::fabs (ratio - kDoubleTimeRatio) <= tolerance * kDoubleTimeRatio)
        return 1;
    return 0;
}

void MusicalClock::applyMetricCorrection (double factor)
{
    // Explicit belief correction, not slew: it takes effect at once. The same
    // wall-clock time now spans `factor` times as many beats.
    bpm_ = clampDouble (bpm_ * factor, config_.minAutoFollowBpm, config_.maxAutoFollowBpm);
    beatsElapsed_ *= factor;

    jumpStreak_ = 0;
    jumpDirection_ = 0;
}

double MusicalClock::beatsBetween (uint64_t fromSampleTime, uint64_t toSampleTime) const
{
    if (sampleRate_ <= 0.0)
        return 0.0;

    const double deltaSamples =
        static_cast<double> (toSampleTime) - static_cast<double> (fromSampleTime);
    return deltaSamples * bpm_ / (kSecondsPerMinute * sampleRate_);
}

void MusicalClock::applyPhaseCorrection (const RhythmObservation& observation)
{
    const double gain = static_cast<double> (config_.phaseCorrectionGain[modeIndex (mode_)]);
    if (gain <= 0.0)
        return;

    // Predicted phase at the *observation's* sample time, so correction does
    // not depend on whether the caller advanced before or after observing.
    const double predicted =
        wrap01 (beatsElapsed_ + beatsBetween (sampleTime_, observation.inputSampleTime));
    double absorbed = wrapSigned (static_cast<double> (observation.beatPhase01) - predicted) * gain;
    absorbed = clampDouble (absorbed,
                            -static_cast<double> (config_.maxPhaseErrorAbsorbed),
                            static_cast<double> (config_.maxPhaseErrorAbsorbed));
    beatsElapsed_ += absorbed;
}

void MusicalClock::alignPhaseTo (uint64_t atSampleTime, double targetPhase01)
{
    const double projected = beatsElapsed_ + beatsBetween (sampleTime_, atSampleTime);
    const double error = wrapSigned (targetPhase01 - wrap01 (projected));
    beatsElapsed_ += error;
}

void MusicalClock::updateConfidence (double target)
{
    target = clampDouble (target, 0.0, 1.0);

    if (target > confidence_)
        confidence_ += static_cast<double> (config_.confidenceAttackRate) * (target - confidence_);
    else
        confidence_ -= static_cast<double> (config_.confidenceReleaseRate) * (confidence_ - target);

    // While holding over, never publish below the configured floor: the
    // drummer stays anchored, just less certain.
    if (state_ == ClockLockState::Holdover)
        confidence_ = std::max (confidence_, static_cast<double> (config_.holdoverConfidenceFloor));

    confidence_ = clampDouble (confidence_, 0.0, 1.0);
}

void MusicalClock::updateTimedState()
{
    if (state_ == ClockLockState::Locked
        && secondsSinceUsableEvidence_ >= config_.holdoverEnterSeconds)
    {
        state_ = ClockLockState::Holdover;
    }
    else if (state_ == ClockLockState::Holdover
             && secondsSinceUsableEvidence_ >= config_.lostEnterSeconds)
    {
        state_ = ClockLockState::Lost;
    }
}

void MusicalClock::applyTimeBasedConfidenceDecay (double deltaSeconds)
{
    const double floor = (state_ == ClockLockState::Holdover)
                             ? static_cast<double> (config_.holdoverConfidenceFloor)
                             : 0.0;

    if (confidence_ > floor)
    {
        confidence_ = std::max (
            floor,
            confidence_ - static_cast<double> (config_.holdoverConfidenceDecayPerSecond) * deltaSeconds);
    }
}

void MusicalClock::applyTapTempo (uint64_t tapSampleTime)
{
    bool startsNewSequence = true;

    if (haveLastTap_ && tapSampleTime > lastTapSampleTime_)
    {
        const double gap =
            static_cast<double> (tapSampleTime - lastTapSampleTime_) / sampleRate_;

        if (gap <= config_.tapTempoResetSeconds)
        {
            startsNewSequence = false;
            if (gap > 0.0)
            {
                tapIntervalSum_ += gap;
                ++tapIntervalCount_;
            }
        }
    }

    if (startsNewSequence)
    {
        tapIntervalSum_ = 0.0;
        tapIntervalCount_ = 0;
    }

    lastTapSampleTime_ = tapSampleTime;
    haveLastTap_ = true;

    if (tapIntervalCount_ <= 0)
        return;

    const double period = tapIntervalSum_ / tapIntervalCount_;
    if (period <= 0.0)
        return;

    // Tap is an explicit human override: it bypasses slew (SPEC 10.2) and
    // establishes both tempo and phase (the most recent tap is a beat instant).
    bpm_ = clampDouble (kSecondsPerMinute / period,
                        config_.minAutoFollowBpm,
                        config_.maxAutoFollowBpm);
    alignPhaseTo (tapSampleTime, 0.0);

    state_ = ClockLockState::Locked;
    confidence_ = std::max (confidence_, static_cast<double> (config_.lockConfidenceThreshold));
    jumpStreak_ = 0;
    jumpDirection_ = 0;
    metricTwinStreak_ = 0;
    metricTwinDirection_ = 0;
}

void MusicalClock::applyResync (uint64_t atSampleTime, bool bar)
{
    if (bar)
    {
        const double projected = beatsElapsed_ + beatsBetween (sampleTime_, atSampleTime);
        const double barPhase = wrap01 (projected / static_cast<double> (beatsPerBar_));
        beatsElapsed_ += -barPhase * static_cast<double> (beatsPerBar_);
    }
    else
    {
        alignPhaseTo (atSampleTime, 0.0);
    }
    // An explicit resync overrides the normal damping limits (SPEC 10.2): the
    // requested boundary is established immediately.
}

void MusicalClock::advance (uint64_t samples, double sampleRate)
{
    if (sampleRate > 0.0)
        sampleRate_ = sampleRate;

    const double deltaSeconds = static_cast<double> (samples) / sampleRate_;
    sampleTime_ += samples;

    if (bpm_ > 0.0)
        beatsElapsed_ += bpm_ * deltaSeconds / kSecondsPerMinute;

    secondsSinceUsableEvidence_ += deltaSeconds;

    // The acquire window is wall-clock time with fresh evidence, not raw call
    // count, so lock quality is independent of the observation rate.
    if (state_ == ClockLockState::Acquiring
        && secondsSinceUsableEvidence_ < config_.holdoverEnterSeconds)
    {
        acquisitionSeconds_ += deltaSeconds;
    }

    updateTimedState();

    if (state_ == ClockLockState::Holdover || state_ == ClockLockState::Lost)
        applyTimeBasedConfidenceDecay (deltaSeconds);

    publish();
}

void MusicalClock::command (const ClockCommand& command)
{
    // SPEC-UNDERSPECIFIED: SPEC 10.2 says freeze pins the tempo against
    // *evidence*; it does not say whether an explicit human Half/Double/Tap/
    // Resync may override a freeze. The conservative reading chosen here is
    // that human intent beats an automatic lock, so every explicit tempo/phase
    // command still acts while tempoFrozen_ is true. The freeze flag itself is
    // changed only by FreezeTempo/ResumeFollow. List for the orchestrator.
    switch (command.type)
    {
        case ClockCommandType::None:
            break;

        case ClockCommandType::SetMode:
            // SPEC-UNDERSPECIFIED: RhythmTypes::ClockCommand has no arg0 field
            // even though the enum comment names one for SetMode. The only
            // available carrier is tapSampleTime, so it is read as the mode.
            // The clean path for callers is the setMode() method.
            {
                const int requested = static_cast<int> (command.tapSampleTime);
                if (requested >= 0 && requested < kTempoModeCount)
                    mode_ = static_cast<TempoMode> (requested);
            }
            break;

        case ClockCommandType::TapTempo:
            applyTapTempo (command.tapSampleTime == 0 ? sampleTime_ : command.tapSampleTime);
            break;

        case ClockCommandType::ResyncNextBeat:
            applyResync (command.tapSampleTime == 0 ? sampleTime_ : command.tapSampleTime, false);
            break;

        case ClockCommandType::ResyncNextBar:
            applyResync (command.tapSampleTime == 0 ? sampleTime_ : command.tapSampleTime, true);
            break;

        case ClockCommandType::HalfTime:
            applyMetricCorrection (kHalfTimeRatio);
            break;

        case ClockCommandType::DoubleTime:
            applyMetricCorrection (kDoubleTimeRatio);
            break;

        case ClockCommandType::FreezeTempo:
            tempoFrozen_ = true;
            break;

        case ClockCommandType::ResumeFollow:
            tempoFrozen_ = false;
            break;

        case ClockCommandType::Reset:
            reset();
            return;
    }

    publish();
}

ClockSnapshot MusicalClock::buildSnapshot() const
{
    ClockSnapshot result;
    result.generation = generation_;
    result.bpm = bpm_;
    result.beatPhase01 = wrap01 (beatsElapsed_);
    result.barPhase01 = wrap01 (beatsElapsed_ / static_cast<double> (beatsPerBar_));

    if (beatsPerBar_ > 0)
    {
        long long beatIndex = static_cast<long long> (std::floor (beatsElapsed_));
        long long inBar = beatIndex % beatsPerBar_;
        if (inBar < 0)
            inBar += beatsPerBar_;
        result.beatInBar = static_cast<int> (inBar) + 1;
    }
    else
    {
        result.beatInBar = 1;
    }

    result.beatsPerBar = beatsPerBar_;
    result.beatUnit = beatUnit_;
    result.confidence01 = static_cast<float> (confidence_);
    result.lockState = state_;
    result.tempoFrozen = tempoFrozen_;
    return result;
}

void MusicalClock::publish()
{
    ClockSnapshot next = buildSnapshot();

    if (snapshotsEqual (next, published_))
        return;

    ++generation_;
    next.generation = generation_;
    published_ = next;
}

ClockSnapshot MusicalClock::snapshot() const
{
    return published_;
}

} // namespace jam
