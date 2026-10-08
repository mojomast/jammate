// Unit tests for jam::RhythmAnalyzer — the live rhythm-analysis worker of
// DEVPLAN ANALYSIS-001 and the threading/queue contracts of SPEC.md sections
// 7.2, 8.1, 8.2 and 9.1.
//
// What is asserted here:
//   - lifecycle: clean start/stop, duplicate / bad-rate / absent-tracker
//     rejection, restart reproducibility and queue flush, destructor join;
//   - drain: observations reach the bounded output queue in order, byte-for-byte
//     unaltered, with causal availability and stream generation as separate
//     metadata;
//   - continuity: a frame gap, an out-of-order frame, a rate change and an
//     invalid rate all reset the tracker and bump the generation, while a
//     device-clock wrap is NOT a gap;
//   - pressure: the output queue drops the incoming envelope and counts it; the
//     ring overrun counter is surfaced;
//   - failure: a throwing backend sets the failure flag and lets stop() join;
//   - a future clock consumer can use availability (not event time) to advance
//     without moving forward in time incorrectly.
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
        const uint64_t call = resetCalls.fetch_add (1, std::memory_order_relaxed) + 1;
        lastResetRateBits.store (bitsOf (sampleRate), std::memory_order_relaxed);
        if (captureThreads.load (std::memory_order_relaxed))
            resetThread = std::this_thread::get_id();

        syncCv.notify_all();

        if (throwOnReset && (throwAtResetCall == 0 || call == throwAtResetCall))
            throw std::runtime_error ("fake tracker reset failure");
    }

    jam::RhythmObservation process (const jam::AnalysisFrame& frame) override
    {
        const uint64_t call = processCalls.fetch_add (1, std::memory_order_relaxed) + 1;
        samplesSeen.fetch_add (frame.numSamples, std::memory_order_relaxed);
        lastSampleTime.store (frame.sampleTime, std::memory_order_relaxed);
        lastRate.store (frame.sourceSampleRate, std::memory_order_relaxed);
        if (captureThreads.load (std::memory_order_relaxed))
            processThread = std::this_thread::get_id();

        syncCv.notify_all();

        if (processSleepMs > 0)
            std::this_thread::sleep_for (std::chrono::milliseconds (processSleepMs));

        if (throwOnProcess && (throwAtProcessCall == 0 || call == throwAtProcessCall))
            throw std::runtime_error ("fake tracker process failure");

        jam::RhythmObservation obs {};
        obs.inputSampleTime = static_cast<uint64_t> (
            static_cast<int64_t> (frame.sampleTime) + eventOffsetSamples);
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
        CHECK_EQ (e.availabilitySampleTime, (static_cast<uint64_t> (i) + 1) * 128);
        CHECK_EQ (e.sourceSampleRate, 48000.0);
        CHECK_EQ (e.observation.inputSampleTime, static_cast<uint64_t> (i) * 128);
        CHECK_EQ (e.observation.sourceSampleRate, 48000.0);
        CHECK_EQ (e.streamGeneration, 1u);
        CHECK (e.observationCausal());
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

JAM_TEST (RhythmAnalyzer, availabilityIsSeparateFromEventTime)
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
    CHECK_EQ (e.availabilitySampleTime, 128u);            // end of input block
    CHECK_EQ (e.observation.inputSampleTime, 500u);       // event, unaltered
    CHECK (! e.observationCausal());                      // 500 > 128
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

    CHECK_EQ (envelopes[0].availabilitySampleTime,
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

    fill();
    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 10; }));
    analyzer.stop();
    const auto run2 = drainAll (analyzer);

    REQUIRE (run1.size() == 5u);
    REQUIRE (run2.size() == 5u);

    for (std::size_t i = 0; i < 5; ++i)
    {
        CHECK_EQ (run2[i].observation.inputSampleTime, run1[i].observation.inputSampleTime);
        CHECK_EQ (run2[i].observation.bpmCandidate, run1[i].observation.bpmCandidate);
        CHECK_EQ (run2[i].availabilitySampleTime, run1[i].availabilitySampleTime);
        CHECK_EQ (run2[i].sequence, static_cast<uint64_t> (i)); // sequence restarts
    }

    // The restart is tagged so a consumer can tell the sessions apart.
    CHECK_GE (run2[0].streamGeneration, run1[0].streamGeneration + 1);
}

JAM_TEST (RhythmAnalyzer, startFlushesQueuedEvidence)
{
    jam::AnalysisAudioRing ring (16);
    auto tracker = std::make_unique<FakeTracker>();
    auto* tp = tracker.get();

    RhythmAnalyzer analyzer (ring, std::move (tracker));
    for (int i = 0; i < 3; ++i)
        CHECK (pushFrame (ring, static_cast<uint64_t> (i) * 128));

    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 3; }));
    analyzer.stop();

    // Leftover evidence from the first session is present...
    CHECK_EQ (analyzer.stats().enqueuedObservations, 3u);

    // ...and a restart must flush it so the new session starts clean.
    CHECK (analyzer.start (48000.0) == AnalyzerStartResult::started);
    ObservationEnvelope e;
    CHECK (! analyzer.popObservation (e));
    analyzer.stop();
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

    CHECK (! analyzer.running());
    CHECK (std::strlen (analyzer.failureMessage()) > 0);

    // Exactly one frame was processed before the throwing call.
    CHECK_EQ (analyzer.stats().processedFrames, 1u);

    analyzer.stop();
    CHECK (! analyzer.running());
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
            if (e.availabilitySampleTime != e.blockStartSampleTime + 128)
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

JAM_TEST (RhythmAnalyzer, futureClockConsumerAdvancesOnAvailability)
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
    uint64_t lastAvailability = 0;
    bool neverBackwards = true;
    uint64_t previousClockGeneration = clock.snapshot().generation;

    for (const auto& e : envelopes)
    {
        // Advance by the causal availability, which is monotone. Event time may
        // be earlier; using it to advance would risk moving time backwards.
        const uint64_t delta = e.availabilitySampleTime - lastAvailability;
        if (delta >= (uint64_t { 1 } << 63)) // modular "negative"
            neverBackwards = false;

        clock.advance (delta, e.sourceSampleRate);
        clock.observe (e.observation);
        lastAvailability = e.availabilitySampleTime;

        const uint64_t generation = clock.snapshot().generation;
        if (generation < previousClockGeneration)
            neverBackwards = false;
        previousClockGeneration = generation;
    }

    CHECK (neverBackwards);
    CHECK_EQ (lastAvailability, 8u * 128u);
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
    CHECK (waitFor (*tp, [tp] { return tp->processCalls.load() >= 3; }));

    const auto st = analyzer.stats();
    CHECK (st.running);
    CHECK (st.streamGeneration >= 1u);
    CHECK_EQ (st.processedFrames, 3u);
    CHECK_EQ (st.processedSamples, 3u * 64u);
    CHECK_EQ (st.enqueuedObservations, 3u);
    CHECK_EQ (st.droppedObservations, 0u);

    analyzer.stop();
    CHECK (! analyzer.stats().running);
}
