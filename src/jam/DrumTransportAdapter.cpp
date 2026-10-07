#include "DrumTransportAdapter.h"

#include <algorithm>
#include <cmath>

namespace jam
{

namespace
{
// Unit conversion. Not a tunable: 60 seconds in a minute (same constant as
// MusicalClock.cpp).
constexpr double kSecondsPerMinute = 60.0;

// Numeric guard used when snapping a fractional boundary sample to the first
// integer sample at-or-after it. Not a musical tunable; it only absorbs the
// representation error of the double multiplication above it.
constexpr double kBoundaryEpsilon = 1e-6;

double wrap01 (double value)
{
    value = std::fmod (value, 1.0);
    if (value < 0.0)
        value += 1.0;
    return value;
}
} // namespace

DrumTransportAdapter::DrumTransportAdapter (const TransportConfig& config)
    : config_ (config)
{
    // sampleRate_ has a fixed member initialiser; honour the configured default
    // so the tests (and any pre-audio command path) see a deterministic rate.
    sampleRate_ = config_.defaultSampleRate > 0.0 ? config_.defaultSampleRate : 48000.0;

    if (config_.maxPendingChanges < 1)
        config_.maxPendingChanges = 1;
    if (config_.maxPendingChanges > kMaxPendingChanges)
        config_.maxPendingChanges = kMaxPendingChanges;
}

void DrumTransportAdapter::prepare (double sampleRate, int maximumBlockSize)
{
    if (sampleRate > 0.0)
        sampleRate_ = sampleRate;
    maximumBlockSize_ = maximumBlockSize;
}

void DrumTransportAdapter::startTransport (double bpm, int beatsPerBar, int beatUnit)
{
    bpm_ = clampBpm (bpm);
    clockBpm_ = bpm_;

    if (beatsPerBar > 0)
        beatsPerBar_ = beatsPerBar;
    if (beatUnit > 0)
        beatUnit_ = beatUnit;

    // Sample clock restarts at zero and the bar-1 downbeat is at sample 0.
    samplePosition_ = 0;
    anchorSample_ = 0;
    anchorBeat_ = 0.0;
    lastBoundarySample_ = 0;

    havePendingTempo_ = false;
    pendingStop_ = false;
    playing_ = true;

    record (TransportEventType::Start, 0, bpm_);

    // SPEC.md product principle 4: changes only happen on musical boundaries.
    // A change queued while stopped is held and applied here, on the bar-1
    // downbeat, rather than being forgotten.
    if (pendingCount_ > 0)
    {
        lastBoundarySample_ = 0;
        consumePending (0);
    }
}

void DrumTransportAdapter::stopTransport()
{
    playing_ = false;
    pendingStop_ = false;
    // Position is deliberately left at the sample clock's last value so callers
    // can inspect where the transport stopped. bar/beat read as 0 while stopped
    // per the TransportPosition contract.
}

void DrumTransportAdapter::setClockTempo (double bpm)
{
    // Tempo-only legacy entry point. Routed through applyTempo semantics via a
    // synthesised snapshot so there is a single tempo path; lock state is NOT
    // touched, because this call carries no lock information.
    if (bpm <= 0.0)
        return;

    const double clamped = clampBpm (bpm);
    if (tempoFrozen_)
    {
        ++tempoIgnoredCount_;
        return;
    }

    clockBpm_ = clamped;
    pendingTempo_ = clamped;
    havePendingTempo_ = true;
}

void DrumTransportAdapter::applyClock (const ClockSnapshot& snapshot)
{
    clockGeneration_ = snapshot.generation;
    lockState_ = snapshot.lockState;
    tempoFrozen_ = snapshot.tempoFrozen;

    // Meter is set by startTransport(). A live meter change would reinterpret
    // the bar grid mid-flight, which is a musical decision this seam does not
    // own; it is recorded as a limitation for DRUM-001 rather than guessed at.
    if (snapshot.beatsPerBar > 0 && ! playing_)
        beatsPerBar_ = snapshot.beatsPerBar;
    if (snapshot.beatUnit > 0 && ! playing_)
        beatUnit_ = snapshot.beatUnit;

    setClockTempo (snapshot.bpm);
}

void DrumTransportAdapter::requestResyncNextBeat (std::uint64_t targetSampleTime)
{
    havePendingResync_ = true;
    pendingResyncIsBar_ = false;
    pendingResyncTarget_ = targetSampleTime;
}

void DrumTransportAdapter::requestResyncNextBar (std::uint64_t targetSampleTime)
{
    havePendingResync_ = true;
    pendingResyncIsBar_ = true;
    pendingResyncTarget_ = targetSampleTime;
}

bool DrumTransportAdapter::enqueue (const PendingChange& change) noexcept
{
    // Stale-generation policy for groove/fill changes: a change stamped with an
    // older clock generation than the newest pending bar change is a
    // superseded request (the director made a fresher decision). Drop it and
    // count it, rather than letting an old decision overrule a new one.
    if (change.origin == 0 && change.generation < lastBarGeneration_)
    {
        ++staleDropCount_;
        return false;
    }

    if (pendingCount_ >= config_.maxPendingChanges)
    {
        // SPEC.md 8.3 overflow policy, mirroring AnalysisAudioRing: drop the
        // INCOMING change, count it, return immediately. Already-queued changes
        // are untouched.
        ++overflowCount_;
        return false;
    }

    pending_[pendingCount_++] = change;

    if (change.origin == 0)
        lastBarGeneration_ = change.generation;

    return true;
}

void DrumTransportAdapter::queueBarChange (const QueuedBarChange& change)
{
    PendingChange pending;
    pending.generation = change.generation;
    pending.groove = change.groove;
    pending.fill = change.fill;
    pending.intensity01 = change.intensity01;
    pending.swing01 = change.swing01;
    pending.humanizeVelocity = change.humanizeVelocity;
    pending.humanizeTiming = change.humanizeTiming;
    pending.humanizeRoundRobin = change.humanizeRoundRobin;
    pending.origin = 0;
    enqueue (pending);
}

void DrumTransportAdapter::requestFillAtNextBar (LibraryIndex fill)
{
    if (fill == kNoLibraryEntry)
        return; // explicit "no fill" is a no-op, not a pending change

    PendingChange pending;
    pending.fill = fill;
    pending.origin = 1;
    enqueue (pending);
}

void DrumTransportAdapter::requestStopAtNextBar()
{
    // Stop is a transport command, not a musical change: it lives in its own
    // flag so a full musical-change buffer can never silently drop a stop.
    pendingStop_ = true;
}

void DrumTransportAdapter::consumePending (std::uint64_t boundarySample) noexcept
{
    // FIFO consumption, but the *observable boundary effect* is coalesced by
    // kind: when several bar changes (or several fills) were queued for this
    // boundary, only the last-queued one writes the applied state, so the
    // boundary is applied exactly once (requirement 3). Every queued entry is
    // still retired exactly once and counted as consumed, and the stale-
    // generation guard in enqueue() has already rejected superseded requests.
    int lastBarIndex = -1;
    int lastFillIndex = -1;

    for (int i = 0; i < pendingCount_; ++i)
    {
        if (pending_[i].origin == 0)
            lastBarIndex = i;
        else
            lastFillIndex = i;
    }

    if (lastBarIndex >= 0)
    {
        const PendingChange& change = pending_[lastBarIndex];
        applied_.generation = change.generation;
        applied_.groove = change.groove;
        applied_.fill = change.fill;
        applied_.intensity01 = change.intensity01;
        applied_.swing01 = change.swing01;
        applied_.humanizeVelocity = change.humanizeVelocity;
        applied_.humanizeTiming = change.humanizeTiming;
        applied_.humanizeRoundRobin = change.humanizeRoundRobin;
        ++appliedBarChangeCount_;
        record (TransportEventType::BarChangeApplied, boundarySample, 0.0, change.groove);
    }

    if (lastFillIndex >= 0)
    {
        applied_.fill = pending_[lastFillIndex].fill;
        ++appliedFillCount_;
        record (TransportEventType::FillApplied, boundarySample, 0.0,
                pending_[lastFillIndex].fill);
    }

    consumedChangeCount_ += static_cast<std::uint64_t> (pendingCount_);
    pendingCount_ = 0;
    lastBarGeneration_ = 0;
}

bool DrumTransportAdapter::handleBarBoundary (std::uint64_t boundarySample) noexcept
{
    if (pendingStop_)
    {
        // Stop supersedes any pending musical change: applying a groove and then
        // immediately stopping would be wasted work, so the changes are dropped
        // deliberately and counted (documented policy; see task note).
        droppedAtStopCount_ += static_cast<std::uint64_t> (pendingCount_);
        pendingCount_ = 0;
        lastBarGeneration_ = 0;
        pendingStop_ = false;
        playing_ = false;
        lastBoundarySample_ = boundarySample;
        record (TransportEventType::Stop, boundarySample);
        return true;
    }

    // SPEC.md 10.1: Lost means the ensemble grid is not trustworthy. The
    // judgement (documented for the orchestrator) is to DEFER a queued change
    // until the lock recovers rather than apply it against a grid that does not
    // match the guitarist. Nothing is discarded; the next trusted boundary
    // applies it.
    if (! config_.honourChangesWhileLost
        && lockState_ == ClockLockState::Lost
        && pendingCount_ > 0)
    {
        ++deferredBoundaryCount_;
        return false;
    }

    if (pendingCount_ > 0)
        consumePending (boundarySample);

    return false;
}

void DrumTransportAdapter::advance (std::uint64_t numSamples)
{
    if (! playing_)
        return; // time only passes while the transport runs

    const std::uint64_t blockStart = samplePosition_;
    const std::uint64_t blockEnd = blockStart + numSamples;

    // Bar index before any change in this block, using the mapping in effect at
    // the block start. Used to detect a boundary crossing exactly.
    const long long barBefore = barIndexAt (blockStart);

    // A pending tempo is applied at the block-start boundary (a safe instant
    // between blocks). Re-anchoring at the current position preserves phase
    // continuity: only the rate changes, not where we are.
    if (havePendingTempo_)
    {
        reanchorAt (blockStart);
        bpm_ = pendingTempo_;
        havePendingTempo_ = false;
        record (TransportEventType::TempoApplied, blockStart, bpm_);
    }

    // An explicit resync is applied at its exact target sample once the block
    // reaches it. Re-anchoring at the target keeps the correction from shifting
    // previously elapsed position.
    if (havePendingResync_ && pendingResyncTarget_ <= blockEnd)
    {
        const std::uint64_t target =
            pendingResyncTarget_ < blockStart ? blockStart : pendingResyncTarget_;
        applyResyncAt (target);
        havePendingResync_ = false;
    }

    const long long barAfter = barIndexAt (blockEnd);

    if (barAfter > barBefore)
    {
        std::uint64_t boundary = boundarySampleFor (barBefore);
        if (boundary < blockStart)
            boundary = blockStart;
        if (boundary > blockEnd)
            boundary = blockEnd;

        lastBoundarySample_ = boundary;
        if (handleBarBoundary (boundary))
        {
            // Stopped mid-block: the transport's position is the boundary, not
            // the end of the block, so it stays inspectable and exact.
            samplePosition_ = boundary;
            return;
        }
    }

    samplePosition_ = blockEnd;
}

double DrumTransportAdapter::samplesPerBeat() const noexcept
{
    if (bpm_ <= 0.0 || sampleRate_ <= 0.0)
        return 0.0;
    return kSecondsPerMinute * sampleRate_ / bpm_;
}

double DrumTransportAdapter::beatsAt (std::uint64_t sample) const noexcept
{
    if (bpm_ <= 0.0 || sampleRate_ <= 0.0)
        return anchorBeat_;

    // Both operands widened before subtracting so this is safe even if a caller
    // reads position() before the resync target is reached.
    const double deltaSamples =
        static_cast<double> (sample) - static_cast<double> (anchorSample_);
    return anchorBeat_
         + deltaSamples * bpm_ / (kSecondsPerMinute * sampleRate_);
}

long long DrumTransportAdapter::barIndexAt (std::uint64_t sample) const noexcept
{
    if (beatsPerBar_ <= 0)
        return 0;
    return static_cast<long long> (std::floor (beatsAt (sample)
                                               / static_cast<double> (beatsPerBar_)));
}

std::uint64_t DrumTransportAdapter::boundarySampleFor (long long barBefore) const noexcept
{
    const double targetBeat =
        static_cast<double> (barBefore + 1) * static_cast<double> (beatsPerBar_);
    const double exact =
        static_cast<double> (anchorSample_)
        + (targetBeat - anchorBeat_) * samplesPerBeat();

    if (exact <= static_cast<double> (anchorSample_))
        return anchorSample_;

    const double snapped = exact - kBoundaryEpsilon;
    if (snapped <= 0.0)
        return 0;
    return static_cast<std::uint64_t> (std::ceil (snapped));
}

void DrumTransportAdapter::reanchorAt (std::uint64_t sample) noexcept
{
    anchorBeat_ = beatsAt (sample);
    anchorSample_ = sample;
}

void DrumTransportAdapter::applyResyncAt (std::uint64_t targetSample) noexcept
{
    const double beats = beatsAt (targetSample);
    double corrected = beats;

    if (pendingResyncIsBar_)
    {
        // Drop the fractional part of the BAR position, preserving the integer
        // bar index so the bar counter does not jump or rewind. This mirrors
        // MusicalClock::applyResync(bar=true). A downbeat now lands on target.
        const double barPhase = wrap01 (beats / static_cast<double> (beatsPerBar_));
        corrected = beats - barPhase * static_cast<double> (beatsPerBar_);
    }
    else
    {
        // Drop the fractional part of the BEAT position, preserving the integer
        // beat index (no rewind) while placing a beat onset on target.
        corrected = beats - wrap01 (beats);
    }

    // Re-anchor at the target so the correction only affects samples at or
    // after it; already-elapsed position is untouched.
    anchorBeat_ = corrected;
    anchorSample_ = targetSample;

    record (pendingResyncIsBar_ ? TransportEventType::ResyncBar
                                : TransportEventType::ResyncBeat,
            targetSample, corrected);
}

double DrumTransportAdapter::beatPhase01() const noexcept
{
    return wrap01 (beatsAt (samplePosition_));
}

double DrumTransportAdapter::barPhase01() const noexcept
{
    if (beatsPerBar_ <= 0)
        return 0.0;
    return wrap01 (beatsAt (samplePosition_) / static_cast<double> (beatsPerBar_));
}

TransportPosition DrumTransportAdapter::position() const
{
    TransportPosition result;
    result.bpm = bpm_;
    result.samplePosition = samplePosition_;
    result.playing = playing_;

    if (! playing_)
    {
        result.bar = 0;
        result.beat = 0;
        return result;
    }

    const double beats = beatsAt (samplePosition_);
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

double DrumTransportAdapter::clampBpm (double bpm) const noexcept
{
    if (bpm < config_.minBpm)
        return config_.minBpm;
    if (bpm > config_.maxBpm)
        return config_.maxBpm;
    return bpm;
}

void DrumTransportAdapter::record (TransportEventType type, std::uint64_t sampleTime,
                                   double value, std::int32_t index) noexcept
{
    ++totalEventCount_;

    if (eventCount_ >= kMaxTransportEvents)
        return;

    TransportEvent& e = events_[eventCount_++];
    e.sampleTime = sampleTime;
    e.value = value;
    e.index = index;
    e.type = type;
}

} // namespace jam
