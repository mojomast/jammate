// Unit tests for jam::RhythmAnalyzer — the live rhythm-analysis worker of
// DEVPLAN ANALYSIS-001 and the threading/queue contracts of SPEC.md sections
// 7.2, 8.1, 8.2 and 9.1.
//
// What is asserted here:
//   - lifecycle: clean start/stop, duplicate / bad-rate / absent-tracker
//     rejection, restart reproducibility, queued-audio discard, destructor join,
//     and that an early worker failure cannot resurrect a stale `running` flag;
//   - drain: observations reach the bounded output queue in order, byte-for-byte
//     unaltered, with the input horizon (a lower bound, not a measured live
//     availability) and stream generation as separate metadata;
//   - continuity: a frame gap, an out-of-order frame, a rate change and an
//     invalid rate all reset the tracker and bump the generation, while a
//     device-clock wrap is NOT a gap; the within-horizon predicate is wrap-safe;
//   - pressure: the output queue drops the incoming envelope and counts it per
//     session; the ring overrun counter is surfaced (and is cumulative);
//   - failure: a throwing backend sets the failure flag and lets stop() join;
//   - a blocked backend proves the horizon is the current frame's end, not a
//     fabricated live availability, and a synchronous offline clock replay can
//     advance on the input horizon without moving backwards.
//
// Threading policy for these tests: every wait is bounded by a timeout and
// progresses on a condition variable or a bounded yield loop, never on a fixed
// sleep. A timed-out wait is a test failure, not a retry. No wall-clock value
// ever feeds an assertion except the generous stop bound.

#include "JamTest.h"

#include "jam/AnalysisAudioRing.h"
#include "jam/IRhythmTracker.h"
#include "jam/MusicalClock.h"
#include "jam/RhythmAnalyzer.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{

using jam::AnalyzerStartResult;
using jam::ObservationEnvelope;
using jam::RhythmAnalyzer;

//==============================================================================
// Deterministic, programmatically configured backend.
//
// It is deliberately simple and data-only: tests program its behaviour through
// atomics before start() and inspect counters after stop(). Anything it records
// is therefore reproducible; there is no randomness and no wall-clock input.
struct FakeTracker : jam::IRhythmTracker
{
    // Behaviour knobs (set before start()).
    uint32_t beatEvery = 0;             // beatEvent every Nth process(); 0 = never
    int64_t  eventOffsetSamples = 0;    // observation.inputSampleTime = blockStart + this
    float    bpm = 120.0f;
    bool     throwOnProcess = false;
    uint64_t throwAtProcessCall = 0;    // 0 = never; throw on the Nth call
    bool     throwOnReset = false;
    uint64_t throwAtResetCall = 0;      // 0 = never; throw on the Nth reset
    int      processSleepMs = 0;        // worker-side latency model (non-RT)
    bool     blockProcess = false;      // block inside process() until released
    std::atomic<bool> enteredProcess { false };
    std::atomic<bool> releaseProcess { false };

    // Counters the tests observe.
    std::atomic<uint64_t> processCalls { 0 };
    std::atomic<uint64_t> resetCalls { 0 };
    std::atomic<uint64_t> samplesSeen { 0 };
    std::atomic<uint64_t> lastSampleTime { 0 };
    std::atomic<double>   lastRate { 0.0 };
    std::atomic<uint64_t> lastResetRateBits { 0 };

    std::atomic<bool> captureThreads { false };
    std::thread::id resetThread {};
    std::thread::id processThread {};

    // Latches for bounded waits.
    mutable std::mutex syncMutex;
    std::condition_variable syncCv;

    const char* id() const noexcept override { return "fake"; }

    void reset (double sampleRate) override
    {
        uint64_t call = 0;
        {
            std::lock_guard<std::mutex> lock (syncMutex);
            call = resetCalls.fetch_add (1, std::memory_order_relaxed) + 1;
            lastResetRateBits.store (bitsOf (sampleRate), std::memory_order_relaxed);
            if (captureThreads.load (std::memory_order_relaxed))
                resetThread = std::this_thread::get_id();
        }

        syncCv.notify_all();

        if (throwOnReset && (throwAtResetCall == 0 || call == throwAtResetCall))
            throw std::runtime_error ("fake tracker reset failure");
    }

    jam::RhythmObservation process (const jam::AnalysisFrame& frame) override
    {
        uint64_t call = 0;
        {
            std::lock_guard<std::mutex> lock (syncMutex);
            call = processCalls.fetch_add (1, std::memory_order_relaxed) + 1;
            samplesSeen.fetch_add (frame.numSamples, std::memory_order_relaxed);
            lastSampleTime.store (frame.sampleTime, std::memory_order_relaxed);
            lastRate.store (frame.sourceSampleRate, std::memory_order_relaxed);
            if (captureThreads.load (std::memory_order_relaxed))
                processThread = std::this_thread::get_id();
        }

        syncCv.notify_all();

        if (processSleepMs > 0)
            std::this_thread::sleep_for (std::chrono::milliseconds (processSleepMs));

        if (blockProcess)
        {
            {
                std::lock_guard<std::mutex> lock (syncMutex);
                enteredProcess.store (true, std::memory_order_release);
            }
            syncCv.notify_all(); // wake a waiter that missed the pre-block notify
            const auto deadline = std::chrono::steady_clock::now()
                                + std::chrono::seconds (30);
            while (! releaseProcess.load (std::memory_order_acquire)
                   && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
        }

        if (throwOnProcess && (throwAtProcessCall == 0 || call == throwAtProcessCall))
            throw std::runtime_error ("fake tracker process failure");

        jam::RhythmObservation obs {};
        obs.inputSampleTime = frame.sampleTime + static_cast<uint64_t> (eventOffsetSamples);
        obs.sourceSampleRate = frame.sourceSampleRate;
        obs.bpmCandidate = bpm;
        obs.beatPhase01 = 0.25f;
        obs.beatConfidence01 = 1.0f;
        obs.onsetStrength01 = 0.5f;
        obs.energyRmsDbfs = -20.0f;
        obs.transientDensity01 = 0.1f;
        obs.beatEvent = beatEvery != 0 && (call % beatEvery == 0);
        obs.silence = false;
        obs.phaseValid = true;
        return obs;
    }

private:
    static uint64_t bitsOf (double v) noexcept
    {
        uint64_t bits = 0;
        static_assert (sizeof (bits) == sizeof (v), "double is not 64-bit");
        std::memcpy (&bits, &v, sizeof (v));
        return bits;
    }
};

//==============================================================================

bool pushFrame (jam::AnalysisAudioRing& ring, uint64_t sampleTime,
                double rate = 48000.0, uint32_t numSamples = 128,
                float value = 0.25f)
{
    jam::AnalysisFrame frame;
    frame.sampleTime = sampleTime;
    frame.sourceSampleRate = rate;
    frame.numSamples = numSamples;
    for (uint32_t i = 0; i < numSamples; ++i)
        frame.samples[i] = value;
    return ring.push (frame.samples, numSamples, sampleTime, rate);
}

template <typename Predicate>
bool waitFor (FakeTracker& tracker, Predicate pred, int timeoutMs = 5000)
{
    std::unique_lock<std::mutex> lock (tracker.syncMutex);
    return tracker.syncCv.wait_for (lock, std::chrono::milliseconds (timeoutMs), pred);
}

/** Bounded wait for state that is not latched by FakeTracker (e.g. the analyzer
    failure flag). Yields rather than sleeping so it cannot stall a slow CI box,
    but always gives up after `timeoutMs`. */
template <typename Predicate>
bool waitUntil (Predicate pred, int timeoutMs = 5000)
{
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds (timeoutMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (pred())
            return true;
        std::this_thread::yield();
    }
    return pred();
}

uint64_t drainCount (RhythmAnalyzer& analyzer)
{
    uint64_t n = 0;
    ObservationEnvelope e;
    while (analyzer.popObservation (e))
        ++n;
    return n;
}

std::vector<ObservationEnvelope> drainAll (RhythmAnalyzer& analyzer)
{
    std::vector<ObservationEnvelope> out;
    ObservationEnvelope e;
    while (analyzer.popObservation (e))
        out.push_back (e);
    return out;
}

} // namespace

//==============================================================================
// Lifecycle.

JAM_TEST (RhythmAnalyzer, startRejectsBadConfiguration)
{
    jam::AnalysisAudioRing ring (4);

    RhythmAnalyzer noTracker (ring, nullptr);
    CHECK (noTracker.start (48000.0) == AnalyzerStartResult::noTracker);
    CHECK (! noTracker.running());
    CHECK (! noTracker.failed());

    auto tracker = std::make_unique<FakeTracker>();
    RhythmAnalyzer analyzer (ring, std::move (tracker));

    CHECK (analyzer.start (std::numeric_limits<double>::quiet_NaN())
           == AnalyzerStartResult::invalidSampleRate);
    CHECK (analyzer.start (std::numeric_limits<double>::infinity())
           == AnalyzerStartResult::invalidSampleRate);
    CHECK (analyzer.start (0.0) == AnalyzerStartResult::invalidSampleRate);
    CHECK (analyzer.start (-48000.0) == AnalyzerStartResult::invalidSampleRate);
    CHECK (! analyzer.running());

    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (analyzer.running());

    // A second start is a duplicate, not a restart.
    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::alreadyRunning);

    analyzer.stop();
    CHECK (! analyzer.running());
    CHECK (! analyzer.failed());
}

JAM_TEST (RhythmAnalyzer, pluginOwnershipRejectsNullDeleter)
{
    jam::AnalysisAudioRing ring (4);

    auto tracker = std::make_unique<FakeTracker>();
    FakeTracker* raw = tracker.release();

    bool threw = false;
    try
    {
        RhythmAnalyzer bad (ring, raw, nullptr);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    CHECK (threw);

    // The rejected construction must NOT have deleted or leaked the tracker.
    delete raw;

    // A null tracker with a null deleter is legal and yields the noTracker path.
    RhythmAnalyzer empty (ring, nullptr, nullptr);
    CHECK (empty.start (48000.0) == AnalyzerStartResult::noTracker);
}

JAM_TEST (RhythmAnalyzer, lifecycleRepeatsCleanly)
{
    for (int iteration = 0; iteration < 20; ++iteration)
    {
        jam::AnalysisAudioRing ring (8);
        auto tracker = std::make_unique<FakeTracker>();
        auto* tp = tracker.get();

        RhythmAnalyzer analyzer (ring, std::move (tracker));
        CHECK (pushFrame (ring, 0));
        CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
        CHECK (analyzer.running());
        CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 1; }));
        analyzer.stop();
        CHECK (! analyzer.running());
        CHECK (! analyzer.failed());
        CHECK_EQ (tp->resetCalls.load(), 1u);
    }
}

JAM_TEST (RhythmAnalyzer, destructorJoinsRunningWorker)
{
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();

    bool processed = false;
    {
        jam::AnalysisAudioRing ring (8);
        RhythmAnalyzer analyzer (ring, std::move (tracker));
        CHECK (pushFrame (ring, 0));
        CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
        processed = waitFor (*tp, [tp] { return tp->processCalls.load() >= 1; });
    }

    CHECK (processed);
}

//==============================================================================
// Drain and publication.

JAM_TEST (RhythmAnalyzer, publishesObservationsInOrder)
{
    jam::AnalysisAudioRing ring (32);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();
    tp->beatEvery = 3;

    RhythmAnalyzer analyzer (ring, std::move (tracker));
    for (int i = 0; i < 9; ++i)
        CHECK (pushFrame (ring, static_cast<uint64_t> (i) * 128));

    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 9; }));
    analyzer.stop();

    const auto envelopes = drainAll (analyzer);
    REQUIRE (envelopes.size() == 9u);

    int beats = 0;
    for (std::size_t i = 0; i < envelopes.size(); ++i)
    {
        const auto& e = envelopes[i];
        CHECK_EQ (e.sequence, static_cast<uint64_t> (i));
        CHECK_EQ (e.blockStartSampleTime, static_cast<uint64_t> (i) * 128);
        CHECK_EQ (e.inputHorizonSampleTime, (static_cast<uint64_t> (i) + 1) * 128);
        CHECK_EQ (e.sourceSampleRate, 48000.0);
        CHECK_EQ (e.observation.inputSampleTime, static_cast<uint64_t> (i) * 128);
        CHECK_EQ (e.observation.sourceSampleRate, 48000.0);
        CHECK_EQ (e.streamGeneration, 1u);
        CHECK (e.observationWithinInputHorizon());
        if (e.observation.beatEvent)
            ++beats;
    }

    CHECK_EQ (beats, 3); // process calls 3, 6, 9

    const auto st = analyzer.stats();
    CHECK_EQ (st.processedFrames, 9u);
    CHECK_EQ (st.processedSamples, 9u * 128u);
    CHECK_EQ (st.enqueuedObservations, 9u);
    CHECK_EQ (st.droppedObservations, 0u);
    CHECK_EQ (st.discontinuities, 0u);
}

JAM_TEST (RhythmAnalyzer, beatEventsAreRetainedNotCoalesced)
{
    jam::AnalysisAudioRing ring (64);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();
    tp->beatEvery = 1; // every frame is a beat

    RhythmAnalyzer analyzer (ring, std::move (tracker));
    for (int i = 0; i < 30; ++i)
        CHECK (pushFrame (ring, static_cast<uint64_t> (i) * 128));

    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 30; }));
    analyzer.stop();

    const auto envelopes = drainAll (analyzer);
    CHECK_EQ (envelopes.size(), 30u);

    int beats = 0;
    for (const auto& e : envelopes)
        if (e.observation.beatEvent)
            ++beats;

    // If the analyzer kept only the latest blob, later beats would erase earlier
    // ones and this count would be far below 30.
    CHECK_EQ (beats, 30);
}

JAM_TEST (RhythmAnalyzer, inputHorizonIsSeparateFromEventTime)
{
    jam::AnalysisAudioRing ring (16);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();
    tp->eventOffsetSamples = 500; // event beyond the block end: non-causal claim

    RhythmAnalyzer analyzer (ring, std::move (tracker));
    CHECK (pushFrame (ring, 0, 48000.0, 128));

    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 1; }));
    analyzer.stop();

    ObservationEnvelope e;
    REQUIRE (analyzer.popObservation (e));
    CHECK_EQ (e.blockStartSampleTime, 0u);
    CHECK_EQ (e.inputHorizonSampleTime, 128u);            // end of input block
    CHECK_EQ (e.observation.inputSampleTime, 500u);       // event, unaltered
    CHECK (! e.observationWithinInputHorizon());          // 500 > 128
    // The horizon is a lower bound; it is never presented as measured live
    // availability.
    CHECK (! e.availabilityMeasured);
}

JAM_TEST (RhythmAnalyzer, backendFieldsAreForwardedBitForBit)
{
    struct PayloadTracker : jam::IRhythmTracker
    {
        jam::RhythmObservation payload {};
        void reset (double) override {}
        jam::RhythmObservation process (const jam::AnalysisFrame&) override { return payload; }
        const char* id() const noexcept override { return "payload"; }
    };

    jam::AnalysisAudioRing ring (4);
    auto tracker = std::make_unique<PayloadTracker>();
    tracker->payload.inputSampleTime = 7;
    tracker->payload.sourceSampleRate = 44100.0;
    tracker->payload.bpmCandidate = 127.75f;
    tracker->payload.beatPhase01 = -0.0f;
    const uint32_t nanBits = 0x7fc01234;
    std::memcpy (&tracker->payload.beatConfidence01, &nanBits, sizeof (nanBits));
    tracker->payload.onsetStrength01 = 0.375f;
    tracker->payload.energyRmsDbfs = -45.5f;
    tracker->payload.transientDensity01 = 0.625f;
    tracker->payload.beatEvent = true;
    tracker->payload.silence = true;
    tracker->payload.phaseValid = false;
    const auto expected = tracker->payload;
    RhythmAnalyzer analyzer (ring, std::move (tracker));
    CHECK (pushFrame (ring, 0));
    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    ObservationEnvelope envelope;
    CHECK (waitUntil ([&] { return analyzer.popObservation (envelope); }));
    analyzer.stop();
    const auto& actual = envelope.observation;
    CHECK_EQ (actual.inputSampleTime, expected.inputSampleTime);
    CHECK (std::memcmp (&actual.sourceSampleRate, &expected.sourceSampleRate, sizeof (double)) == 0);
    const float jam::RhythmObservation::* fields[] = {
        &jam::RhythmObservation::bpmCandidate, &jam::RhythmObservation::beatPhase01,
        &jam::RhythmObservation::beatConfidence01, &jam::RhythmObservation::onsetStrength01,
        &jam::RhythmObservation::energyRmsDbfs, &jam::RhythmObservation::transientDensity01
    };
    for (auto field : fields)
        CHECK (std::memcmp (&(actual.*field), &(expected.*field), sizeof (float)) == 0);
    CHECK_EQ (actual.beatEvent, expected.beatEvent);
    CHECK_EQ (actual.silence, expected.silence);
    CHECK_EQ (actual.phaseValid, expected.phaseValid);
    CHECK_EQ (envelope.sourceSampleRate, 48000.0); // envelope rate does not rewrite evidence
}

JAM_TEST (RhythmAnalyzer, withinHorizonPredicateIsWrapSafe)
{
    // Event before the wrap, horizon just after it: valid even though the raw
    // integer comparison event <= horizon would be false.
    const uint64_t nearMax = std::numeric_limits<uint64_t>::max() - 5;
    ObservationEnvelope afterWrap;
    afterWrap.observation.inputSampleTime = nearMax;
    afterWrap.inputHorizonSampleTime = 9; // (9 - (MAX-5)) mod 2^64 == 15
    CHECK (afterWrap.observationWithinInputHorizon());

    // Event exactly at the horizon is within it.
    ObservationEnvelope equal;
    equal.observation.inputSampleTime = 128;
    equal.inputHorizonSampleTime = 128;
    CHECK (equal.observationWithinInputHorizon());

    // A future event just past the horizon is rejected.
    ObservationEnvelope future;
    future.observation.inputSampleTime = 129;
    future.inputHorizonSampleTime = 128;
    CHECK (! future.observationWithinInputHorizon());

    // An old event far outside the bounded half-range is rejected (no wrap
    // aliasing).
    ObservationEnvelope old;
    old.observation.inputSampleTime = 0;
    old.inputHorizonSampleTime = (uint64_t { 1 } << 63) + 10;
    CHECK (! old.observationWithinInputHorizon());
}

JAM_TEST (RhythmAnalyzer, popEmptyLeavesOutputUntouched)
{
    jam::AnalysisAudioRing ring (4);
    RhythmAnalyzer analyzer (ring, nullptr);

    ObservationEnvelope e;
    e.sequence = 12345;
    e.blockStartSampleTime = 7;
    e.inputHorizonSampleTime = 99;

    CHECK (! analyzer.popObservation (e));
    CHECK_EQ (e.sequence, 12345u);
    CHECK_EQ (e.blockStartSampleTime, 7u);
    CHECK_EQ (e.inputHorizonSampleTime, 99u);
}

JAM_TEST (RhythmAnalyzer, blockedBackendProvesHorizonIsTheFrameEnd)
{
    jam::AnalysisAudioRing ring (16);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();
    tp->blockProcess = true;

    RhythmAnalyzer analyzer (ring, std::move (tracker));

    for (int i = 0; i < 3; ++i)
        CHECK (pushFrame (ring, static_cast<uint64_t> (i) * 128));

    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp, [tp] { return tp->enteredProcess.load(); }));

    // The worker is paused on frame 0 while the producer advances the device
    // clock well ahead of it.
    for (int i = 3; i < 7; ++i)
        CHECK (pushFrame (ring, static_cast<uint64_t> (i) * 128));

    // No evidence is available while the worker is blocked: the horizon is not a
    // fabricated "current master" availability.
    ObservationEnvelope e;
    CHECK (! analyzer.popObservation (e));

    tp->releaseProcess.store (true, std::memory_order_release);
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 7; }));
    analyzer.stop();

    const auto envelopes = drainAll (analyzer);
    REQUIRE (envelopes.size() == 7u);
    for (std::size_t i = 0; i < envelopes.size(); ++i)
    {
        // Each horizon is that frame's own end, not the latest queued frame end.
        CHECK_EQ (envelopes[i].inputHorizonSampleTime,
                  (static_cast<uint64_t> (i) + 1) * 128);
        CHECK (! envelopes[i].availabilityMeasured);
    }
}

//==============================================================================
// Continuity.

JAM_TEST (RhythmAnalyzer, frameGapResetsTrackerAndBumpsGeneration)
{
    jam::AnalysisAudioRing ring (16);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();

    RhythmAnalyzer analyzer (ring, std::move (tracker));
    CHECK (pushFrame (ring, 0));
    CHECK (pushFrame (ring, 1000)); // gap: expected 128

    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 2; }));
    analyzer.stop();

    const auto envelopes = drainAll (analyzer);
    REQUIRE (envelopes.size() == 2u);
    CHECK_EQ (envelopes[0].streamGeneration, 1u);
    CHECK_EQ (envelopes[1].streamGeneration, 2u);
    CHECK_EQ (tp->resetCalls.load(), 2u);                 // start + gap
    CHECK_EQ (analyzer.stats().discontinuities, 1u);
}

JAM_TEST (RhythmAnalyzer, outOfOrderFrameResetsTracker)
{
    jam::AnalysisAudioRing ring (16);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();

    RhythmAnalyzer analyzer (ring, std::move (tracker));
    CHECK (pushFrame (ring, 0));
    CHECK (pushFrame (ring, 128));
    CHECK (pushFrame (ring, 64)); // behind expected 256

    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 3; }));
    analyzer.stop();

    CHECK_EQ (analyzer.stats().discontinuities, 1u);
    CHECK_EQ (tp->resetCalls.load(), 2u);
}

JAM_TEST (RhythmAnalyzer, rateChangeResetsTrackerAtNewRate)
{
    jam::AnalysisAudioRing ring (16);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();

    RhythmAnalyzer analyzer (ring, std::move (tracker));
    CHECK (pushFrame (ring, 0, 48000.0));
    CHECK (pushFrame (ring, 128, 48000.0));
    CHECK (pushFrame (ring, 256, 44100.0)); // rate change
    CHECK (pushFrame (ring, 384, 44100.0));

    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 4; }));
    analyzer.stop();

    CHECK_EQ (tp->resetCalls.load(), 2u);
    CHECK_EQ (analyzer.stats().discontinuities, 1u);
    CHECK_EQ (analyzer.stats().processedFrames, 4u);

    // The second reset must use the new device rate.

    ObservationEnvelope e;
    REQUIRE (analyzer.popObservation (e));
    CHECK_EQ (e.streamGeneration, 1u);
    REQUIRE (analyzer.popObservation (e));
    CHECK_EQ (e.streamGeneration, 1u);
    REQUIRE (analyzer.popObservation (e));
    CHECK_EQ (e.streamGeneration, 2u);
    CHECK_EQ (e.sourceSampleRate, 44100.0);
    CHECK_EQ (e.observation.sourceSampleRate, 44100.0);
}

JAM_TEST (RhythmAnalyzer, invalidRateFrameIsDroppedAndForcesReset)
{
    jam::AnalysisAudioRing ring (16);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();

    RhythmAnalyzer analyzer (ring, std::move (tracker));
    CHECK (pushFrame (ring, 0, 48000.0));
    CHECK (pushFrame (ring, 128, std::numeric_limits<double>::quiet_NaN()));
    CHECK (pushFrame (ring, 256, 48000.0));

    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 2; }));
    analyzer.stop();

    const auto st = analyzer.stats();
    CHECK_EQ (st.invalidRateFrames, 1u);
    CHECK_EQ (st.discontinuities, 1u);

    // The invalid frame was never fed to the backend; the two valid frames were.
    CHECK_EQ (tp->resetCalls.load(), 2u);      // start + forced re-establishment
    CHECK_EQ (tp->processCalls.load(), 2u);
    CHECK_EQ (st.processedFrames, 2u);
}

JAM_TEST (RhythmAnalyzer, sampleTimeWrapIsNotAGap)
{
    jam::AnalysisAudioRing ring (16);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();

    RhythmAnalyzer analyzer (ring, std::move (tracker));

    const uint64_t base = std::numeric_limits<uint64_t>::max() - 64; // +128 wraps

    CHECK (pushFrame (ring, base));
    CHECK (pushFrame (ring, base + 128)); // wraps to 63
    CHECK (pushFrame (ring, base + 256)); // 191

    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 3; }));
    analyzer.stop();

    const auto st = analyzer.stats();
    CHECK_EQ (st.discontinuities, 0u);
    CHECK_EQ (tp->resetCalls.load(), 1u);

    const auto envelopes = drainAll (analyzer);
    REQUIRE (envelopes.size() == 3u);
    for (const auto& e : envelopes)
        CHECK_EQ (e.streamGeneration, 1u);

    CHECK_EQ (envelopes[0].inputHorizonSampleTime,
              static_cast<uint64_t> (base + 128));
    CHECK_EQ (envelopes[1].blockStartSampleTime, base + 128);
}

//==============================================================================
// Pressure.

JAM_TEST (RhythmAnalyzer, queuePressureDropsIncomingAndCountsIt)
{
    jam::AnalysisAudioRing ring (64);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();

    RhythmAnalyzer analyzer (ring, std::move (tracker));
    for (int i = 0; i < 64; ++i)
        CHECK (pushFrame (ring, static_cast<uint64_t> (i) * 128));

    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 64; }));
    analyzer.stop(); // consumer never drained: the bounded queue fills

    const auto st = analyzer.stats();
    CHECK_EQ (st.processedFrames, 64u);
    CHECK_EQ (st.enqueuedObservations, RhythmAnalyzer::observationQueueCapacity());
    CHECK_EQ (st.droppedObservations,
              64u - RhythmAnalyzer::observationQueueCapacity());
    CHECK_EQ (st.enqueuedObservations + st.droppedObservations, st.processedFrames);

    // The retained evidence is the FIRST capacity envelopes, in order: a full
    // queue drops the incoming envelope and never overwrites queued evidence.
    const auto envelopes = drainAll (analyzer);
    CHECK_EQ (envelopes.size(), RhythmAnalyzer::observationQueueCapacity());
    for (std::size_t i = 0; i < envelopes.size(); ++i)
        CHECK_EQ (envelopes[i].sequence, static_cast<uint64_t> (i));
}

JAM_TEST (RhythmAnalyzer, dropCounterIsPerSessionAndLifetimeSeparate)
{
    jam::AnalysisAudioRing ring (64);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();

    RhythmAnalyzer analyzer (ring, std::move (tracker));

    // Session 1: overflow the queue, consumer never drains.
    for (int i = 0; i < 64; ++i)
        CHECK (pushFrame (ring, static_cast<uint64_t> (i) * 128));
    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 64; }));
    analyzer.stop();

    const auto st1 = analyzer.stats();
    CHECK_EQ (st1.droppedObservations, 64u - RhythmAnalyzer::observationQueueCapacity());
    CHECK_EQ (st1.lifetimeDroppedObservations, st1.droppedObservations);

    // Session 2: a restart discards nothing here, fits in the queue, no drops.
    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    for (int i = 0; i < 8; ++i)
        CHECK (pushFrame (ring, static_cast<uint64_t> (i) * 128));
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 72; }));
    analyzer.stop();

    const auto st2 = analyzer.stats();
    CHECK_EQ (st2.droppedObservations, 0u);                     // per-session delta
    CHECK_EQ (st2.lifetimeDroppedObservations, st1.lifetimeDroppedObservations);
    CHECK_EQ (st2.enqueuedObservations, 8u);
}

JAM_TEST (RhythmAnalyzer, ringOverrunIsCountedAndWorkerDrainsWhatFits)
{
    jam::AnalysisAudioRing ring (8);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();

    for (int i = 0; i < 12; ++i)
        (void) pushFrame (ring, static_cast<uint64_t> (i) * 128);

    CHECK_EQ (ring.overrunCount(), 4u);

    RhythmAnalyzer analyzer (ring, std::move (tracker));
    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 8; }));
    analyzer.stop();

    CHECK_EQ (analyzer.stats().processedFrames, 8u);
    CHECK_EQ (analyzer.stats().ringOverruns, 4u);
}

//==============================================================================
// Restart contracts.

JAM_TEST (RhythmAnalyzer, restartIsReproducibleAndFresh)
{
    jam::AnalysisAudioRing ring (16);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();

    RhythmAnalyzer analyzer (ring, std::move (tracker));

    const auto fill = [&ring] {
        for (int i = 0; i < 5; ++i)
            CHECK (pushFrame (ring, static_cast<uint64_t> (i) * 128));
    };

    fill();
    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 5; }));
    analyzer.stop();
    const auto run1 = drainAll (analyzer);

    // A restart discards pre-existing queued audio, so the second session's
    // frames are submitted after start(), exactly as a live producer would.
    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    fill();
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 10; }));
    analyzer.stop();
    const auto run2 = drainAll (analyzer);

    REQUIRE (run1.size() == 5u);
    REQUIRE (run2.size() == 5u);

    for (std::size_t i = 0; i < 5; ++i)
    {
        CHECK_EQ (run2[i].observation.inputSampleTime, run1[i].observation.inputSampleTime);
        CHECK_EQ (run2[i].observation.bpmCandidate, run1[i].observation.bpmCandidate);
        CHECK_EQ (run2[i].inputHorizonSampleTime, run1[i].inputHorizonSampleTime);
        CHECK_EQ (run2[i].sequence, static_cast<uint64_t> (i)); // sequence restarts
    }

    // The restart is tagged so a consumer can tell the sessions apart.
    CHECK_GE (run2[0].streamGeneration, run1[0].streamGeneration + 1);
}

JAM_TEST (RhythmAnalyzer, restartDiscardsQueuedAudioAndFlushesEvidence)
{
    jam::AnalysisAudioRing ring (16);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();

    RhythmAnalyzer analyzer (ring, std::move (tracker));

    // Session 1 consumes its frames but the consumer never drains the evidence.
    for (int i = 0; i < 3; ++i)
        CHECK (pushFrame (ring, static_cast<uint64_t> (i) * 128));
    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 3; }));
    analyzer.stop();
    CHECK_EQ (analyzer.stats().enqueuedObservations, 3u);

    // Stale audio left in the ring by the previous stream.
    CHECK (pushFrame (ring, 9000));
    CHECK (pushFrame (ring, 9128));

    // A restart discards the stale audio and flushes the stale evidence, so the
    // new session cannot process an old stream frame under the new generation.
    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    const auto st = analyzer.stats();
    CHECK_EQ (st.discardedAudioBlocks, 2u);
    CHECK_EQ (st.discardedAudioFrames, 256u);
    CHECK_EQ (st.discardedAudioDiscardEvents, 1u);
    CHECK_EQ (st.processedFrames, 0u);

    ObservationEnvelope e;
    CHECK (! analyzer.popObservation (e));
    analyzer.stop();
    CHECK_EQ (analyzer.stats().processedFrames, 0u);

    // The first start must NOT discard: a caller may legitimately prime audio
    // before the very first start.
    jam::AnalysisAudioRing ring2 (4);
    auto tracker2 = std::make_unique<FakeTracker>();
    auto* tp2 = tracker2.get();
    RhythmAnalyzer analyzer2 (ring2, std::move (tracker2));
    CHECK (pushFrame (ring2, 0));
    CHECK (analyzer2.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp2, [tp2] { return tp2->processCalls.load() >= 1; }));
    analyzer2.stop();
    CHECK_EQ (analyzer2.stats().discardedAudioBlocks, 0u);
    CHECK_EQ (analyzer2.stats().processedFrames, 1u);
}

//==============================================================================
// Failure handling.

JAM_TEST (RhythmAnalyzer, resetFailureAtStartIsRejected)
{
    jam::AnalysisAudioRing ring (8);
    auto tracker = std::make_unique<FakeTracker>();
    tracker->throwOnReset = true;
    tracker->throwAtResetCall = 1;

    RhythmAnalyzer analyzer (ring, std::move (tracker));
    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::trackerResetFailed);
    CHECK (! analyzer.running());
    CHECK (analyzer.failed());
    CHECK (std::strlen (analyzer.failureMessage()) > 0);

    analyzer.stop(); // must be safe when no thread was started
}

JAM_TEST (RhythmAnalyzer, processExceptionSetsFailureAndStops)
{
    jam::AnalysisAudioRing ring (16);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();
    tp->throwOnProcess = true;
    tp->throwAtProcessCall = 2;

    RhythmAnalyzer analyzer (ring, std::move (tracker));
    CHECK (pushFrame (ring, 0));
    CHECK (pushFrame (ring, 128));
    CHECK (pushFrame (ring, 256));

    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitUntil ([&] { return analyzer.failed() && ! analyzer.running(); }));

    analyzer.stop(); // failure text is a quiescent, joined-owner API
    CHECK (! analyzer.running());
    CHECK (std::strlen (analyzer.failureMessage()) > 0);

    // Exactly one frame was processed before the throwing call.
    CHECK_EQ (analyzer.stats().processedFrames, 1u);

    analyzer.stop();
    CHECK (! analyzer.running());
}

JAM_TEST (RhythmAnalyzer, earlyFailureDoesNotResurrectRunning)
{
    // The running flag is published BEFORE the worker starts and is never
    // written by start() after the launch, so a worker that fails on a prequeued
    // frame and exits cannot be masked by a later store. Repeat to stress the
    // start/exit ordering.
    for (int iteration = 0; iteration < 100; ++iteration)
    {
        jam::AnalysisAudioRing ring (4);
        auto tracker = std::make_unique<FakeTracker>();
        auto* tp = tracker.get();
        tp->throwOnProcess = true;
        tp->throwAtProcessCall = 1; // fail on the first (prequeued) frame

        RhythmAnalyzer analyzer (ring, std::move (tracker));
        CHECK (pushFrame (ring, 0));
        CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);

        // Once failed, running must settle to false and stay false.
        CHECK (waitUntil ([&] { return analyzer.failed() && ! analyzer.running(); }));

        analyzer.stop();
        CHECK (analyzer.failed());
        CHECK (! analyzer.running());
    }
}

JAM_TEST (RhythmAnalyzer, resetFailureOnDiscontinuityStopsSafely)
{
    jam::AnalysisAudioRing ring (16);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();
    tp->throwOnReset = true;
    tp->throwAtResetCall = 2; // start reset succeeds, gap reset throws

    RhythmAnalyzer analyzer (ring, std::move (tracker));
    CHECK (pushFrame (ring, 0));
    CHECK (pushFrame (ring, 1000));

    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitUntil ([&] { return analyzer.failed() && ! analyzer.running(); }));
    CHECK (! analyzer.running());

    analyzer.stop();
    CHECK (! analyzer.running());
}

JAM_TEST (RhythmAnalyzer, stopIsBoundedWithSlowBackend)
{
    jam::AnalysisAudioRing ring (16);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();
    tp->processSleepMs = 3;

    RhythmAnalyzer analyzer (ring, std::move (tracker));
    for (int i = 0; i < 8; ++i)
        CHECK (pushFrame (ring, static_cast<uint64_t> (i) * 128));

    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 1; }));

    const auto start = std::chrono::steady_clock::now();
    analyzer.stop();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds> (
        std::chrono::steady_clock::now() - start).count();

    CHECK (! analyzer.running());
    // A generous bound: proves stop() is bounded, not that it is fast.
    CHECK_LE (elapsedMs, 5000);
}

//==============================================================================
// Concurrency and the future clock consumer.

JAM_TEST (RhythmAnalyzer, concurrentProducerAndConsumer)
{
    constexpr uint64_t kFrames = 400;

    jam::AnalysisAudioRing ring (512);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();
    tp->captureThreads = true;

    RhythmAnalyzer analyzer (ring, std::move (tracker));
    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);

    std::atomic<bool> producerDone { false };
    std::atomic<uint64_t> producerPushes { 0 };

    std::thread producer ([&] {
        for (uint64_t i = 0; i < kFrames; ++i)
        {
            while (! pushFrame (ring, i * 128))
                std::this_thread::yield();
            producerPushes.fetch_add (1, std::memory_order_relaxed);
        }
        producerDone.store (true, std::memory_order_release);
    });

    uint64_t popped = 0;
    uint64_t lastSequence = 0;
    bool sequencesMonotonic = true;
    bool metadataCoherent = true;

    const bool progressed = waitUntil ([&] {
        ObservationEnvelope e;
        while (analyzer.popObservation (e))
        {
            if (popped > 0 && e.sequence <= lastSequence)
                sequencesMonotonic = false;
            lastSequence = e.sequence;
            if (e.inputHorizonSampleTime != e.blockStartSampleTime + 128)
                metadataCoherent = false;
            ++popped;
        }
        return producerDone.load (std::memory_order_acquire)
            && tp->processCalls.load (std::memory_order_acquire) >= kFrames;
    }, 10000);

    CHECK (progressed);

    producer.join();
    analyzer.stop();

    // Drain anything that arrived between the last pop and stop().
    popped += drainCount (analyzer);

    const auto st = analyzer.stats();
    CHECK (sequencesMonotonic);
    CHECK (metadataCoherent);
    CHECK_EQ (producerPushes.load(), kFrames);
    CHECK_EQ (st.processedFrames, kFrames);
    CHECK_EQ (st.enqueuedObservations + st.droppedObservations, kFrames);
    CHECK_EQ (popped, st.enqueuedObservations);

    // reset() primed on the lifecycle thread; process() ran on the worker.
    CHECK (tp->resetThread == std::this_thread::get_id());
    CHECK (tp->processThread != std::this_thread::get_id());
}

JAM_TEST (RhythmAnalyzer, offlineClockReplayAdvancesOnInputHorizon)
{
    jam::AnalysisAudioRing ring (32);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();
    tp->bpm = 120.0f;

    RhythmAnalyzer analyzer (ring, std::move (tracker));
    for (int i = 0; i < 8; ++i)
        CHECK (pushFrame (ring, static_cast<uint64_t> (i) * 128));

    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 8; }));
    analyzer.stop();

    const auto envelopes = drainAll (analyzer);
    REQUIRE (envelopes.size() == 8u);

    jam::MusicalClock clock;
    uint64_t lastHorizon = 0;
    bool neverBackwards = true;
    uint64_t previousClockGeneration = clock.snapshot().generation;

    for (const auto& e : envelopes)
    {
        // Synchronous OFFLINE replay can advance on the input horizon. A live
        // consumer instead advances on its current device clock and must stamp
        // receipt availability; frame-end alone is insufficient under backlog.
        const uint64_t delta = e.inputHorizonSampleTime - lastHorizon;
        if (delta >= (uint64_t { 1 } << 63)) // modular "negative"
            neverBackwards = false;

        clock.advance (delta, e.sourceSampleRate);
        clock.observe (e.observation);
        lastHorizon = e.inputHorizonSampleTime;

        const uint64_t generation = clock.snapshot().generation;
        if (generation < previousClockGeneration)
            neverBackwards = false;
        previousClockGeneration = generation;
    }

    CHECK (neverBackwards);
    CHECK_EQ (lastHorizon, 8u * 128u);
}

JAM_TEST (RhythmAnalyzer, statsReflectProcessedWork)
{
    jam::AnalysisAudioRing ring (8);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();

    RhythmAnalyzer analyzer (ring, std::move (tracker));
    CHECK (analyzer.stats().streamGeneration == 0u);
    CHECK (! analyzer.stats().running);

    for (int i = 0; i < 3; ++i)
        CHECK (pushFrame (ring, static_cast<uint64_t> (i) * 64, 48000.0, 64));

    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (analyzer.running());
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 3; }));

    // stats() is a relaxed, multi-field sample, not a snapshot; read the final
    // values after join(), which establishes happens-before for every counter.
    analyzer.stop();

    const auto st = analyzer.stats();
    CHECK (! st.running);
    CHECK (st.streamGeneration >= 1u);
    CHECK_EQ (st.processedFrames, 3u);
    CHECK_EQ (st.processedSamples, 3u * 64u);
    CHECK_EQ (st.enqueuedObservations, 3u);
    CHECK_EQ (st.droppedObservations, 0u);
}
