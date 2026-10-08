#include "DrumClockBridge.h"

#include <cmath>

namespace jam
{

namespace
{
// Unit conversion. Not a tunable: 60 seconds in a minute (same constant as
// MusicalClock.cpp / DrumTransportAdapter.cpp).
constexpr double kSecondsPerMinute = 60.0;

// Numeric guard when snapping a fractional boundary sample to the first integer
// sample at-or-after it. It only absorbs the representation error of the double
// multiply above it, never a musical amount (mirrors the adapter).
constexpr double kBoundaryEpsilon = 1e-6;

double wrap01 (double value)
{
    value = std::fmod (value, 1.0);
    if (value < 0.0)
        value += 1.0;
    return value;
}

// Clamp a finite musical parameter into the documented 0..1 domain. Out-of-range
// finite values are bounded rather than rejected so a director that overshoots by
// a rounding error still produces a valid command; NaN/inf are rejected by the
// caller before this is reached.
float clamp01 (float value) noexcept
{
    if (value < 0.0f)
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}
} // namespace

DrumClockBridge::DrumClockBridge (const DrumClockBridgeConfig& config) noexcept
    : config_ (config)
{
    if (config_.beatsPerBar < 1)
        config_.beatsPerBar = 4;
    if (config_.beatUnit < 1)
        config_.beatUnit = 4;

    beatsPerBar_ = config_.beatsPerBar;
    beatUnit_ = config_.beatUnit;
    sampleRate_ = config_.defaultSampleRate > 0.0 ? config_.defaultSampleRate : 48000.0;
    bpm_ = clampBpm (config_.initialBpm > 0.0 ? config_.initialBpm : 100.0);
}

void DrumClockBridge::prepare (double sampleRate, int maximumBlockSize) noexcept
{
    // Domain check: a non-positive rate has no valid grid. Refuse it instead of
    // publishing commands the engine would have to reject one by one.
    if (! (sampleRate > 0.0) || ! std::isfinite (sampleRate))
    {
        ++invalidRequestCount_;
        prepared_ = false;
        return;
    }

    sampleRate_ = sampleRate;
    maximumBlockSize_ = maximumBlockSize > 0 ? maximumBlockSize : 0;
    prepared_ = true;

    // Full grid reset. A re-prepare is a new session; leaving anchors or staged
    // changes behind would let the old grid leak into the new rate.
    playing_ = false;
    stopPending_ = false;
    stopBoundary_ = 0;
    haveClockSample_ = false;
    now_ = 0;
    anchorSample_ = 0;
    anchorBeat_ = 0.0;
    bpm_ = clampBpm (config_.initialBpm);
    haveStagedTempo_ = false;
    haveStagedResync_ = false;
    clearStagedBarChange();
    haveBarChangeGeneration_ = false;
    lastBarChangeGeneration_ = 0;
    haveSnapshot_ = false;
    lastSnapshotGeneration_ = 0;
    commandSeq_ = 0;
    publishedCount_ = 0;
    staleSnapshotCount_ = 0;
    staleBarChangeCount_ = 0;
    discontinuityCount_ = 0;
    invalidRequestCount_ = 0;

    // All roles must be quiescent for prepare(); drain any commands left by the
    // previous session so the new grid cannot be reinterpreted by stale ones.
    DrumClockCommand discarded;
    while (queue_.pop (discarded))
    {
    }

    // Rebaseline the cumulative queue drop counter for the new session.
    dropBaseline_ = queue_.droppedCount();
}

double DrumClockBridge::clampBpm (double bpm) const noexcept
{
    if (! std::isfinite (bpm) || bpm <= 0.0)
        return config_.initialBpm > 0.0 ? config_.initialBpm : 100.0;
    if (bpm < config_.minBpm)
        return config_.minBpm;
    if (bpm > config_.maxBpm)
        return config_.maxBpm;
    return bpm;
}

double DrumClockBridge::samplesPerBeat() const noexcept
{
    if (bpm_ <= 0.0 || sampleRate_ <= 0.0)
        return 0.0;
    return kSecondsPerMinute * sampleRate_ / bpm_;
}

double DrumClockBridge::samplesPerBar() const noexcept
{
    return samplesPerBeat() * static_cast<double> (beatsPerBar_);
}

double DrumClockBridge::beatsAt (std::uint64_t sample) const noexcept
{
    if (bpm_ <= 0.0 || sampleRate_ <= 0.0)
        return anchorBeat_;

    const double deltaSamples =
        static_cast<double> (sample) - static_cast<double> (anchorSample_);
    return anchorBeat_
         + deltaSamples * bpm_ / (kSecondsPerMinute * sampleRate_);
}

double DrumClockBridge::beatsAtAccountingStaged (std::uint64_t sample) const noexcept
{
    if (! haveStagedTempo_ || sample <= stagedBoundary_)
        return beatsAt (sample);

    const double atBoundary = beatsAt (stagedBoundary_);
    const double delta = static_cast<double> (sample - stagedBoundary_);
    return atBoundary + delta * stagedBpm_ / (kSecondsPerMinute * sampleRate_);
}

std::uint64_t DrumClockBridge::boundaryForBeat (double targetBeat) const noexcept
{
    const double spb = samplesPerBeat();
    if (spb <= 0.0)
        return now_;

    const double exact =
        static_cast<double> (anchorSample_) + (targetBeat - anchorBeat_) * spb;
    if (exact <= static_cast<double> (anchorSample_))
        return anchorSample_;

    const double snapped = exact - kBoundaryEpsilon;
    if (snapped <= 0.0)
        return 0;
    return static_cast<std::uint64_t> (std::ceil (snapped));
}

std::uint64_t DrumClockBridge::nextBarBoundarySample() const noexcept
{
    if (! prepared_ || bpm_ <= 0.0 || sampleRate_ <= 0.0)
        return now_;

    const double barBeats = static_cast<double> (beatsPerBar_);
    const double beatsNow = beatsAt (now_);
    const double targetBar = std::floor (beatsNow / barBeats) + 1.0;

    std::uint64_t boundary = boundaryForBeat (targetBar * barBeats);
    if (boundary <= now_)
        boundary = boundaryForBeat ((targetBar + 1.0) * barBeats);

    return boundary;
}

void DrumClockBridge::publish (DrumClockCommand command) noexcept
{
    command.sequence = ++commandSeq_;
    // push() is bounded, lock-free and drops+counts the incoming command when
    // the fixed-capacity queue is full (SPEC.md 8.3). Nothing here allocates.
    if (queue_.push (command))
        ++publishedCount_;
}

void DrumClockBridge::stageTempo (double bpm) noexcept
{
    const double clamped = clampBpm (bpm);
    if (clamped == bpm_ && ! haveStagedTempo_)
        return; // no change, nothing to stage or publish

    // The control worker may call applySnapshot() every tick while the clock's
    // snapshot generation advances (phase changes each tick). Without this
    // dedupe, a tempo staged for a future bar boundary would be republished on
    // every tick until that boundary, flooding the bounded command queue. The
    // tempo is staged once per ACCEPTED target value; a changed target
    // republishes (latest-target chronological coalescing at the same boundary).
    if (haveStagedTempo_ && clamped == stagedBpm_)
        return;

    const std::uint64_t boundary =
        haveStagedTempo_ ? stagedBoundary_ : nextBarBoundarySample();

    DrumClockCommand command;
    command.type = DrumClockCommandType::SetTempo;
    command.sampleTime = boundary;
    command.generation = lastSnapshotGeneration_;
    command.bpm = clamped;
    command.beatsPerBar = beatsPerBar_;
    command.beatUnit = beatUnit_;

    // Latch the staged state ONLY after the publish was accepted. publish() is
    // bounded and drops the incoming command when the queue is full; latching
    // first would make the dedupe above suppress every retry, leaving the
    // worker grid to apply a tempo the engine never received (permanent drift).
    // On a drop the old staged state is left EXACTLY unchanged so the next
    // worker tick retries.
    const std::uint64_t dropsBefore = queue_.droppedCount();
    publish (command);
    if (queue_.droppedCount() != dropsBefore)
    {
        ++invalidRequestCount_;
        return; // dropped: do not latch, retry on a later tick
    }

    haveStagedTempo_ = true;
    stagedBpm_ = clamped;
    stagedBoundary_ = boundary;
}

void DrumClockBridge::applyStagedState (std::uint64_t atSample) noexcept
{
    if (! haveStagedTempo_ || atSample < stagedBoundary_)
        return;

    // Re-anchor phase at the boundary with the OLD rate, then switch the rate,
    // so only the interval after the boundary changes. This is what keeps a
    // tempo change from shifting the already-elapsed grid.
    const std::uint64_t boundary = stagedBoundary_;
    anchorBeat_ = beatsAt (boundary);
    anchorSample_ = boundary;
    bpm_ = stagedBpm_;
    haveStagedTempo_ = false;
}

void DrumClockBridge::applyStagedResync (std::uint64_t atSample) noexcept
{
    if (! haveStagedResync_ || atSample < stagedResyncTarget_)
        return;

    const std::uint64_t target = stagedResyncTarget_;
    double corrected = beatsAtAccountingStaged (target);

    if (stagedResyncBar_)
    {
        // Drop the fractional BAR position, preserving the integer bar index so
        // the bar counter cannot jump or rewind. Mirrors MusicalClock /
        // DrumTransportAdapter.
        const double barPhase = wrap01 (corrected / static_cast<double> (beatsPerBar_));
        corrected -= barPhase * static_cast<double> (beatsPerBar_);
    }
    else
    {
        corrected -= wrap01 (corrected);
    }

    anchorBeat_ = corrected;
    anchorSample_ = target;
    haveStagedResync_ = false;
}

bool DrumClockBridge::setClockSample (std::uint64_t explicitSample) noexcept
{
    // Domain reject: above maxExplicitSample the double sample->beat map loses
    // integer samples. Refuse rather than publish an unrepresentable grid; the
    // previous state is left untouched.
    if (explicitSample > config_.maxExplicitSample)
    {
        ++invalidRequestCount_;
        return false;
    }

    // First call establishes the origin: nothing before it can be a
    // discontinuity because there was no grid to be discontinuous with.
    if (! haveClockSample_)
    {
        haveClockSample_ = true;
        now_ = explicitSample;
        anchorSample_ = explicitSample;
        anchorBeat_ = 0.0;
        applyStagedState (explicitSample);
        applyStagedResync (explicitSample);
        return true;
    }

    const std::uint64_t previous = now_;
    const bool discontinuity =
        explicitSample < previous
        || (explicitSample - previous) > config_.maxForwardJumpSamples;

    now_ = explicitSample;

    if (discontinuity)
    {
        // A backwards or implausible jump means the grid this worker derived no
        // longer describes the audio timeline. Forget it, re-anchor at the new
        // origin and tell the engine to stop rendering the stale grid. The
        // documented contract is: Clear loses the injected transport; a fresh
        // JoinAtBar is required to play again.
        ++discontinuityCount_;
        playing_ = false;
        stopPending_ = false;
        haveStagedTempo_ = false;
        haveStagedResync_ = false;
        clearStagedBarChange();
        anchorSample_ = explicitSample;
        anchorBeat_ = 0.0;

        DrumClockCommand command;
        command.type = DrumClockCommandType::Clear;
        command.sampleTime = explicitSample;
        command.generation = lastSnapshotGeneration_;
        publish (command);
        return false;
    }

    // Apply at most two staged transitions (one tempo, one resync) in
    // chronological order. Fixed and bounded, no loop. Ordering matters when one
    // update crosses BOTH targets: if the resync target precedes the staged
    // tempo boundary, the resync must read the OLD-tempo region first; if the
    // tempo boundary precedes the resync, the tempo re-anchors first so the
    // resync reads the NEW grid. Getting this wrong extrapolates the old region
    // at the new rate and drifts every later downbeat.
    const bool tempoDue = haveStagedTempo_ && explicitSample >= stagedBoundary_;
    const bool resyncDue = haveStagedResync_ && explicitSample >= stagedResyncTarget_;

    if (tempoDue && resyncDue && stagedResyncTarget_ < stagedBoundary_)
    {
        applyStagedResync (explicitSample); // T first (old-tempo region)
        applyStagedState (explicitSample);  // then B
    }
    else
    {
        applyStagedState (explicitSample);  // B first (or equal T/B)
        applyStagedResync (explicitSample);
    }

    // A staged BarChange is only a worker-side latch: the engine owns the
    // pattern application through its own event. Forget the latch once its bar
    // boundary is reached so the next request stages for the following bar.
    applyStagedBarChange (explicitSample);

    // The stop only becomes real when its boundary is reached: until then the
    // engine is still rendering and the worker position must say so.
    if (stopPending_ && explicitSample >= stopBoundary_)
    {
        playing_ = false;
        stopPending_ = false;
    }

    return true;
}

void DrumClockBridge::applySnapshot (const ClockSnapshot& snapshot) noexcept
{
    if (! prepared_)
    {
        ++invalidRequestCount_;
        return;
    }

    // Stale-generation guard: an older snapshot must never overrule a newer
    // belief. Generation 0 is the "unknown" spelling and is always accepted.
    if (haveSnapshot_ && snapshot.generation != 0
        && snapshot.generation <= lastSnapshotGeneration_)
    {
        ++staleSnapshotCount_;
        return;
    }

    if (snapshot.generation != 0)
        lastSnapshotGeneration_ = snapshot.generation;
    haveSnapshot_ = true;

    // Meter fields are deliberately ignored: the bridge and the engine agree on
    // one fixed meter for the injected groove, and accepting a second authority
    // here would reinterpret the grid mid-flight (double-authority bug).

    // Lost means the ensemble grid is not trustworthy. Hold the last tempo
    // rather than chase a belief the clock itself has stopped asserting.
    if (snapshot.lockState == ClockLockState::Lost)
        return;

    if (snapshot.bpm > 0.0 && std::isfinite (snapshot.bpm))
        stageTempo (snapshot.bpm);
}

bool DrumClockBridge::requestJoinAtNextBar (LibraryIndex groove) noexcept
{
    if (! prepared_)
    {
        ++invalidRequestCount_;
        return false;
    }

    const std::uint64_t boundary = nextBarBoundarySample();

    // Effective tempo at the join boundary: if a tempo change is already staged
    // for that same boundary, the join must carry it. Otherwise a snapshot that
    // arrives before the join would be published as a SetTempo command that the
    // join then supersedes, and the new bar would start at the stale tempo.
    const double effectiveBpm =
        (haveStagedTempo_ && stagedBoundary_ <= boundary) ? stagedBpm_ : bpm_;

    DrumClockCommand command;
    command.type = DrumClockCommandType::JoinAtBar;
    command.sampleTime = boundary;
    command.generation = lastSnapshotGeneration_;
    command.bpm = effectiveBpm;
    command.groove = groove;
    command.beatsPerBar = beatsPerBar_;
    command.beatUnit = beatUnit_;

    const std::uint64_t dropsBefore = queue_.droppedCount();
    publish (command);
    if (queue_.droppedCount() != dropsBefore)
    {
        ++invalidRequestCount_;
        return false; // queue full: the join was dropped, not half-published
    }

    // A join supersedes a stop that has not yet reached its boundary.
    stopPending_ = false;
    playing_ = true;
    return true;
}

bool DrumClockBridge::validBarChangePayload (const QueuedBarChange& change) const noexcept
{
    // Index domain: kNoLibraryEntry (-1) is the "no change" sentinel; anything
    // below it is a corrupt/overflowed index and is refused whole.
    if (change.groove < kNoLibraryEntry || change.fill < kNoLibraryEntry)
        return false;

    // Numeric domain: NaN/inf would poison the clamps and the engine gains.
    if (! std::isfinite (change.intensity01)
        || ! std::isfinite (change.swing01)
        || ! std::isfinite (change.humanizeVelocity)
        || ! std::isfinite (change.humanizeTiming)
        || ! std::isfinite (change.humanizeRoundRobin))
        return false;

    return true;
}

void DrumClockBridge::clearStagedBarChange() noexcept
{
    haveStagedChange_ = false;
    stagedChangeBoundary_ = 0;
    stagedChangeFields_ = 0;
    stagedChangeGroove_ = kNoLibraryEntry;
    stagedChangeFill_ = kNoLibraryEntry;
    stagedIntensity_ = 0.5f;
    stagedSwing_ = 0.0f;
    stagedHumanVel_ = 0.25f;
    stagedHumanTime_ = 0.15f;
    stagedHumanRR_ = 0.40f;
}

void DrumClockBridge::applyStagedBarChange (std::uint64_t atSample) noexcept
{
    if (! haveStagedChange_ || atSample < stagedChangeBoundary_)
        return;

    // No worker grid change is needed: a BarChange is purely musical. The latch
    // is dropped so the next request stages for the following bar boundary.
    haveStagedChange_ = false;
}

bool DrumClockBridge::stageBarChange (std::uint8_t fields,
                                      const QueuedBarChange& change) noexcept
{
    if (! prepared_)
    {
        ++invalidRequestCount_;
        return false;
    }

    if (! validBarChangePayload (change))
    {
        ++invalidRequestCount_;
        return false;
    }

    // Stale-decision guard: an older generation must never overrule a newer
    // committed decision. Generation 0 is the "unknown" spelling and is always
    // accepted; the same generation may refine the staged value.
    if (change.generation != 0 && haveBarChangeGeneration_
        && change.generation < lastBarChangeGeneration_)
    {
        ++staleBarChangeCount_;
        return false;
    }

    const float intensity = clamp01 (change.intensity01);
    const float swing = clamp01 (change.swing01);
    const float humanVel = clamp01 (change.humanizeVelocity);
    const float humanTime = clamp01 (change.humanizeTiming);
    const float humanRR = clamp01 (change.humanizeRoundRobin);

    // Coalesce an identical re-publication. The director may call this every
    // tick; without the dedupe a staged command would be republished until its
    // boundary and could exhaust the bounded queue (mirrors stageTempo()).
    if (haveStagedChange_
        && fields == stagedChangeFields_
        && change.groove == stagedChangeGroove_
        && change.fill == stagedChangeFill_
        && intensity == stagedIntensity_
        && swing == stagedSwing_
        && humanVel == stagedHumanVel_
        && humanTime == stagedHumanTime_
        && humanRR == stagedHumanRR_)
    {
        if (change.generation != 0)
        {
            haveBarChangeGeneration_ = true;
            lastBarChangeGeneration_ = change.generation;
        }
        return true;
    }

    // Strictly the next future bar: reuse the already-staged horizon while one
    // exists so repeated (even differing) updates for the same bar land on the
    // SAME boundary instead of drifting to the bar after it.
    const std::uint64_t boundary =
        haveStagedChange_ ? stagedChangeBoundary_ : nextBarBoundarySample();

    DrumClockCommand command;
    command.type = DrumClockCommandType::BarChange;
    command.sampleTime = boundary;
    command.generation = change.generation;
    command.groove = change.groove;
    command.fill = change.fill;
    command.changeFields = fields;
    command.intensity01 = intensity;
    command.swing01 = swing;
    command.humanizeVelocity = humanVel;
    command.humanizeTiming = humanTime;
    command.humanizeRoundRobin = humanRR;

    // Latch ONLY after the bounded publish is accepted. On a drop the previous
    // staging is left exactly as it was, the caller gets false and can retry;
    // nothing has been committed to the audio side.
    const std::uint64_t dropsBefore = queue_.droppedCount();
    publish (command);
    if (queue_.droppedCount() != dropsBefore)
    {
        ++invalidRequestCount_;
        return false;
    }

    haveStagedChange_ = true;
    stagedChangeBoundary_ = boundary;
    stagedChangeFields_ = fields;
    stagedChangeGroove_ = command.groove;
    stagedChangeFill_ = command.fill;
    stagedIntensity_ = intensity;
    stagedSwing_ = swing;
    stagedHumanVel_ = humanVel;
    stagedHumanTime_ = humanTime;
    stagedHumanRR_ = humanRR;

    if (change.generation != 0)
    {
        haveBarChangeGeneration_ = true;
        lastBarChangeGeneration_ = change.generation;
    }
    return true;
}

bool DrumClockBridge::requestBarChange (const QueuedBarChange& change) noexcept
{
    // A full bar change is authoritative for every field, including an explicit
    // "no fill" (fill stays kNoLibraryEntry): requesting a groove change must
    // not leave a previous fill hanging over the new groove.
    const std::uint8_t fields =
        DrumChangeField::Groove | DrumChangeField::Fill | DrumChangeField::Params;
    return stageBarChange (fields, change);
}

bool DrumClockBridge::requestFillAtNextBar (LibraryIndex fill) noexcept
{
    if (fill < 0)
    {
        ++invalidRequestCount_;
        return false;
    }

    // Fill-only: the selected groove and the director's intensity/swing/
    // humanization are left untouched (this is the user Fill button path).
    QueuedBarChange change;
    change.fill = fill;
    return stageBarChange (static_cast<std::uint8_t> (DrumChangeField::Fill), change);
}

bool DrumClockBridge::requestStopAtNextBar() noexcept
{
    if (! prepared_)
    {
        ++invalidRequestCount_;
        return false;
    }

    DrumClockCommand command;
    command.type = DrumClockCommandType::StopAtBar;
    command.sampleTime = nextBarBoundarySample();
    command.generation = lastSnapshotGeneration_;

    const std::uint64_t dropsBefore = queue_.droppedCount();
    publish (command);
    if (queue_.droppedCount() != dropsBefore)
    {
        ++invalidRequestCount_;
        return false; // dropped: do not enter a pending-stop state
    }

    // Pending commitment, not an immediate state change: the engine keeps
    // rendering until the boundary, so playing() must stay true until then.
    stopPending_ = true;
    stopBoundary_ = command.sampleTime;

    // DRUM-ADAPT-002: a stop at the next bar supersedes any adaptive change
    // staged for that same bar, so no pattern switch survives the stop.
    clearStagedBarChange();
    return true;
}

bool DrumClockBridge::requestStopNow() noexcept
{
    if (! prepared_)
    {
        ++invalidRequestCount_;
        return false;
    }

    // A bounded cancel/clear: the engine applies it at its next serviced block,
    // drops every pending event and leaves injected mode. No bar wait, no
    // device prepare, no forced manual play.
    DrumClockCommand command;
    command.type = DrumClockCommandType::Clear;
    command.sampleTime = now_;
    command.generation = lastSnapshotGeneration_;

    const std::uint64_t dropsBefore = queue_.droppedCount();
    publish (command);
    if (queue_.droppedCount() != dropsBefore)
    {
        // Full queue: the cancel was dropped, not half-published. Leave the
        // worker grid untouched so the caller can retry.
        ++invalidRequestCount_;
        return false;
    }

    // Accepted: update the worker grid state (never before acceptance).
    playing_ = false;
    stopPending_ = false;
    stopBoundary_ = 0;
    haveStagedTempo_ = false;
    haveStagedResync_ = false;
    clearStagedBarChange();
    return true;
}

bool DrumClockBridge::requestResyncNextBeat (std::uint64_t targetSample) noexcept
{
    if (! prepared_ || ! haveClockSample_ || targetSample < now_)
    {
        ++invalidRequestCount_;
        return false;
    }

    DrumClockCommand command;
    command.type = DrumClockCommandType::ResyncBeat;
    command.sampleTime = targetSample;
    command.generation = lastSnapshotGeneration_;
    // The beat that contains the target (floor, no rewind), expressed as a step
    // within the bar. The engine applies this exact phase, so both grids agree
    // on every later downbeat.
    {
        long long within = static_cast<long long> (
                               std::floor (beatsAtAccountingStaged (targetSample)))
                           % beatsPerBar_;
        if (within < 0)
            within += beatsPerBar_;
        command.phaseStep = static_cast<std::int32_t> (within) * 4;
    }

    const std::uint64_t dropsBefore = queue_.droppedCount();
    publish (command);
    if (queue_.droppedCount() != dropsBefore)
    {
        ++invalidRequestCount_;
        return false;
    }

    haveStagedResync_ = true;
    stagedResyncBar_ = false;
    stagedResyncTarget_ = targetSample;
    return true;
}

bool DrumClockBridge::requestResyncNextBar (std::uint64_t targetSample) noexcept
{
    if (! prepared_ || ! haveClockSample_ || targetSample < now_)
    {
        ++invalidRequestCount_;
        return false;
    }

    DrumClockCommand command;
    command.type = DrumClockCommandType::ResyncBar;
    command.sampleTime = targetSample;
    command.generation = lastSnapshotGeneration_;
    command.phaseStep = 0; // the target becomes a downbeat

    const std::uint64_t dropsBefore = queue_.droppedCount();
    publish (command);
    if (queue_.droppedCount() != dropsBefore)
    {
        ++invalidRequestCount_;
        return false;
    }

    haveStagedResync_ = true;
    stagedResyncBar_ = true;
    stagedResyncTarget_ = targetSample;
    return true;
}

void DrumClockBridge::resetForNewSession (double bpm) noexcept
{
    // All roles (producer, audio consumer, readers) must be quiescent and
    // joined: drain the queue here, then rebaseline the cumulative queue drop
    // counter so the new session starts at zero. A Clear published below is
    // counted against that baseline if (impossibly, for an empty queue) it drops.
    DrumClockCommand discarded;
    while (queue_.pop (discarded))
    {
    }
    dropBaseline_ = queue_.droppedCount();

    playing_ = false;
    stopPending_ = false;
    stopBoundary_ = 0;
    haveClockSample_ = false;
    haveStagedTempo_ = false;
    haveStagedResync_ = false;
    clearStagedBarChange();
    haveBarChangeGeneration_ = false;
    lastBarChangeGeneration_ = 0;
    haveSnapshot_ = false;
    lastSnapshotGeneration_ = 0;

    bpm_ = clampBpm (bpm > 0.0 ? bpm : config_.initialBpm);
    now_ = 0;
    anchorSample_ = 0;
    anchorBeat_ = 0.0;

    // Best-effort notification for a still-attached engine: lose the old grid.
    DrumClockCommand clear;
    clear.type = DrumClockCommandType::Clear;
    clear.sampleTime = 0;
    publish (clear);

    commandSeq_ = 0;
    publishedCount_ = 0;
    staleSnapshotCount_ = 0;
    staleBarChangeCount_ = 0;
    discontinuityCount_ = 0;
    invalidRequestCount_ = 0;
}

TransportPosition DrumClockBridge::position() const noexcept
{
    TransportPosition result;
    result.bpm = bpm_;
    result.samplePosition = now_;
    result.playing = playing_;

    if (! playing_)
    {
        result.bar = 0;
        result.beat = 0;
        return result;
    }

    const double beats = beatsAt (now_);
    const long long beatIndex = static_cast<long long> (std::floor (beats));

    long long bar0 = 0;
    long long inBar = beatIndex;
    if (beatsPerBar_ > 0)
    {
        bar0 = beatIndex / beatsPerBar_;
        inBar = beatIndex % beatsPerBar_;
        if (inBar < 0)
        {
            inBar += beatsPerBar_;
            --bar0;
        }
    }

    result.bar = static_cast<int> (bar0) + 1;
    result.beat = static_cast<int> (inBar) + 1;
    return result;
}

} // namespace jam
