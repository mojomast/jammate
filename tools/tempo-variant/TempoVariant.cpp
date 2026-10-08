// TempoVariant implementation. See TempoVariant.h for the predeclared method.

#include "TempoVariant.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace tempo_variant
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

void TempoVariantTracker::reset (double sampleRate)
{
    // The caller's rate is forwarded to the wrapped backend unchanged (the
    // IRhythmTracker contract). Only the wrapper's OWN interval arithmetic uses
    // the finite-positive fallback; this is documented in the header so the
    // fallback is never mistaken for backend validation.
    if (inner_ != nullptr)
        inner_->reset (sampleRate);
    rate_ = (std::isfinite (sampleRate) && sampleRate > 0.0) ? sampleRate : 48000.0;
    resetRing();
    havePrevBeat_ = false;
    prevBeatSample_ = 0;
    blockIndex_ = 0;
}

void TempoVariantTracker::resetRing() noexcept
{
    ring_.fill (0.0);
    ringCount_ = 0;
}

void TempoVariantTracker::pushInterval (double interval) noexcept
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

double TempoVariantTracker::medianInterval() const noexcept
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

jam::RhythmObservation TempoVariantTracker::process (const jam::AnalysisFrame& frame)
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
            resetRing();
            havePrevBeat_ = false;
            rec.intervalState = IntervalState::FrameInvalid;
        }
        else if (ev > blockEndSample)
        {
            // A beat the backend places after the audio it was given cannot
            // anchor a consecutive interval; treat as malformed and reset.
            resetRing();
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
            resetRing();
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
                resetRing();
                rec.intervalState = IntervalState::MalformedReset;
            }
            else if (interval > kMaxIntervalSeconds)
            {
                resetRing();
                rec.intervalState = IntervalState::GapReset;
            }
            else if (interval < kMinIntervalSeconds)
            {
                resetRing();
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
        resetRing();
        havePrevBeat_ = false;
    }

    // The single changed field. Everything else is the wrapped backend's value.
    if (ringCount_ == kRequiredIntervals)
    {
        const double median = medianInterval();
        if (std::isfinite (median) && median > 0.0)
            obs.bpmCandidate = static_cast<float> (60.0 / median);
    }

    rec.ringCount = ringCount_;
    rec.ready = (ringCount_ == kRequiredIntervals);
    rec.variantBpm = static_cast<double> (obs.bpmCandidate);
    if (log_)
        log_ (rec);

    ++blockIndex_;
    return obs;
}

const char* TempoVariantTracker::id() const noexcept
{
    return "btrack-tempo-variant";
}

} // namespace tempo_variant
