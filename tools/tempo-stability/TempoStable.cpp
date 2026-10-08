// TempoStable implementation. See TempoStable.h for the predeclared method.

#include "TempoStable.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace tempo_stable
{

const char* toString (IntervalState s) noexcept
{
    switch (s)
    {
        case IntervalState::NoBeat:          return "none";
        case IntervalState::FirstBeat:       return "first_beat";
        case IntervalState::Accepted:        return "accepted";
        case IntervalState::MalformedReset:  return "malformed_reset";
        case IntervalState::GapReset:        return "gap_reset";
        case IntervalState::OutOfOrderReset: return "out_of_order_reset";
        case IntervalState::FrameInvalid:    return "frame_invalid_reset";
    }
    return "unknown";
}

void TempoStableTracker::reset (double sampleRate)
{
    // The caller's rate is forwarded to the wrapped backend unchanged (the
    // IRhythmTracker contract). Only the wrapper's OWN interval arithmetic uses
    // the finite-positive fallback; this is documented in the header so the
    // fallback is never mistaken for backend validation.
    if (inner_ != nullptr)
        inner_->reset (sampleRate);
    rate_ = (std::isfinite (sampleRate) && sampleRate > 0.0) ? sampleRate : 48000.0;
    resetState();
    havePrevBeat_ = false;
    prevBeatSample_ = 0;
    blockIndex_ = 0;
}

void TempoStableTracker::resetState() noexcept
{
    ring_.fill (0.0);
    ringCount_ = 0;
    havePrevDerived_ = false;
    prevDerived_ = 0.0;
    stableCount_ = 0;
    confirmed_ = false;
}

void TempoStableTracker::pushInterval (double interval) noexcept
{
    if (ringCount_ < kRingCapacity)
    {
        ring_[ringCount_++] = interval;
        return;
    }
    // Shift left (fixed 4-element move; no allocation).
    for (std::size_t i = 1; i < kRingCapacity; ++i)
        ring_[i - 1] = ring_[i];
    ring_[kRingCapacity - 1] = interval;
}

double TempoStableTracker::medianInterval() const noexcept
{
    std::array<double, kRingCapacity> a = ring_;
    for (std::size_t i = 1; i < kRingCapacity; ++i)
    {
        const double key = a[i];
        std::size_t j = i;
        while (j > 0 && a[j - 1] > key) { a[j] = a[j - 1]; --j; }
        a[j] = key;
    }
    const std::size_t mid = kRingCapacity / 2;               // 2 for capacity 4
    return 0.5 * (a[mid - 1] + a[mid]);
}

void TempoStableTracker::updateConfirmation (double derived) noexcept
{
    if (! std::isfinite (derived) || ! (derived > 0.0))
        return;

    if (! havePrevDerived_)
    {
        prevDerived_ = derived;
        havePrevDerived_ = true;
        stableCount_ = 0;
        return;
    }

    const bool agrees = std::isfinite (prevDerived_) && prevDerived_ > 0.0
                        && std::fabs (derived - prevDerived_)
                               <= kAgreementFraction * prevDerived_;
    stableCount_ = agrees ? stableCount_ + 1 : 0;
    prevDerived_ = derived;

    if (stableCount_ >= kConfirmUpdates)
        confirmed_ = true;
}

jam::RhythmObservation TempoStableTracker::process (const jam::AnalysisFrame& frame)
{
    jam::RhythmObservation obs;
    if (inner_ != nullptr)
        obs = inner_->process (frame);

    const double baseBpm = static_cast<double> (obs.bpmCandidate);

    // Reject invalid frame metadata BEFORE it can wrap the unsigned clocks.
    const bool frameInvalid =
        frame.numSamples > jam::kMaxAnalysisBlock
        || frame.sampleTime > std::numeric_limits<std::uint64_t>::max() - frame.numSamples;
    const std::uint64_t blockEndSample =
        frameInvalid ? frame.sampleTime
                     : frame.sampleTime + static_cast<std::uint64_t> (frame.numSamples);

    MethodRecord rec;
    rec.blockIndex = blockIndex_;
    rec.blockStartSeconds = static_cast<double> (frame.sampleTime) / rate_;
    rec.blockEndSeconds = static_cast<double> (blockEndSample) / rate_;
    rec.baseBpm = baseBpm;

    if (obs.beatEvent)
    {
        const std::uint64_t ev = obs.inputSampleTime;
        rec.beatEvent = true;
        rec.eventSeconds = static_cast<double> (ev) / rate_;

        if (frameInvalid)
        {
            resetState();
            havePrevBeat_ = false;
            rec.intervalState = IntervalState::FrameInvalid;
        }
        else if (ev > blockEndSample)
        {
            // A beat the backend places after the audio it was given cannot
            // anchor a consecutive interval; treat as malformed and reset.
            resetState();
            havePrevBeat_ = false;
            rec.intervalState = IntervalState::MalformedReset;
        }
        else if (! havePrevBeat_)
        {
            prevBeatSample_ = ev;
            havePrevBeat_ = true;
            rec.intervalState = IntervalState::FirstBeat;
        }
        else if (ev <= prevBeatSample_)
        {
            // Duplicate or non-monotonic sample time. This MUST be checked
            // before the unsigned subtraction, which would otherwise wrap to a
            // huge positive "interval". The interval is MISSING, not measured.
            resetState();
            prevBeatSample_ = ev;
            havePrevBeat_ = true;
            rec.intervalState = IntervalState::OutOfOrderReset;
        }
        else
        {
            const double interval =
                static_cast<double> (ev - prevBeatSample_) / rate_;
            rec.intervalMeasured = true;
            rec.intervalSeconds = interval;

            if (! std::isfinite (interval))
            {
                resetState();
                rec.intervalState = IntervalState::MalformedReset;
            }
            else if (interval > kMaxIntervalSeconds)
            {
                resetState();
                rec.intervalState = IntervalState::GapReset;
            }
            else if (interval < kMinIntervalSeconds)
            {
                resetState();
                rec.intervalState = IntervalState::MalformedReset;
            }
            else
            {
                pushInterval (interval);
                rec.intervalState = IntervalState::Accepted;
            }

            prevBeatSample_ = ev;
            havePrevBeat_ = true;
        }
    }
    else if (frameInvalid)
    {
        // No beat, but the block metadata is unusable; be conservative and
        // clear state rather than carry a potentially wrapped clock.
        resetState();
        havePrevBeat_ = false;
    }

    // Derived estimate, confirmation gate and emission. The ring only changes on
    // a beat event with an accepted interval; a full-ring accepted update is the
    // only moment the confirmation state advances.
    if (ringCount_ == kRingCapacity)
    {
        const double median = medianInterval();
        if (std::isfinite (median) && median > 0.0)
        {
            const double derived = 60.0 / median;
            rec.derivedBpm = derived;
            rec.derivedValid = true;
            if (rec.intervalState == IntervalState::Accepted)
                updateConfirmation (derived);
        }
    }

    if (confirmed_)
    {
        // Committed: emit the derived value when valid, else the backend's value.
        if (rec.derivedValid)
            obs.bpmCandidate = static_cast<float> (rec.derivedBpm);
    }
    // Otherwise obs.bpmCandidate is the wrapped backend's forwarded value.

    rec.ringCount = ringCount_;
    rec.ringFull = (ringCount_ == kRingCapacity);
    rec.stableCount = stableCount_;
    rec.confirmed = confirmed_;
    rec.emittedBpm = static_cast<double> (obs.bpmCandidate);
    if (log_)
        log_ (rec);

    ++blockIndex_;
    return obs;
}

const char* TempoStableTracker::id() const noexcept
{
    return "btrack-tempo-stable";
}

} // namespace tempo_stable
