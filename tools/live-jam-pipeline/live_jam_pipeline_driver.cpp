// live-jam-pipeline driver (INT-LIVE-001)
//
// A portable, JUCE-free end-to-end exercise of the live slice's control core:
//
//   synthetic guitar tap -> LiveJamSession (bounded ring) -> analyzer (optional)
//     -> observations -> MusicalClock -> JamJoinPolicy -> DrumClockBridge
//     -> bounded command queue -> emulated audio-owner drum transport
//
// It is deterministic on purpose. The join/stop/echo trace is driven by injected
// observations computed from an exact sample clock (no wall-clock timing), and
// the emulated drum echo consumes the SAME bounded command queue the real
// DrumEngine consumes, so the joinPending -> drumsPlaying transition is real
// pipeline logic, not a stub answer.
//
// The chunking check does run the actual RhythmAnalyzer worker with a counting
// tracker and a bounded wait; it exists to prove an oversized callback split
// into <=2048-sample chunks is not truncated.
//
// This driver does NOT substitute for the orchestrator's actual-processor
// replay: it links the platform-neutral jam-core only. The replay harness must
// take a freshly built product and verify callback allocation/lock counters.
//
// Build (standalone, after building jam-core):
//   g++ -std=c++17 -O2 -I <repo>/src tools/live-jam-pipeline/live_jam_pipeline_driver.cpp <build>/libjam-core.a -lpthread -o live_jam_pipeline_driver
//
// Exit code 0 only when every invariant holds.

#include "jam/LiveJamSession.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr double kBpm = 120.0;
constexpr int kBlock = 512;
constexpr int kChunk = (int) jam::kMaxAnalysisBlock;
constexpr uint64_t kSamplesPerBeat = (uint64_t) (kSampleRate * 60.0 / kBpm); // 24000

int failures = 0;

void check (bool condition, const char* what)
{
    if (! condition)
    {
        std::printf ("  INVARIANT FAILED: %s\n", what);
        ++failures;
    }
}

// Deterministic phase-locked observation for the injected sample time.
jam::ObservationEnvelope scriptedObservation (uint64_t blockStart, int numSamples)
{
    jam::ObservationEnvelope envelope;
    const uint64_t event = blockStart;
    const double phase = (double) (event % kSamplesPerBeat) / (double) kSamplesPerBeat;
    envelope.observation.inputSampleTime = event;
    envelope.observation.sourceSampleRate = kSampleRate;
    envelope.observation.bpmCandidate = (float) kBpm;
    envelope.observation.beatPhase01 = (float) phase;
    envelope.observation.beatConfidence01 = 0.9f;
    envelope.observation.energyRmsDbfs = -18.0f;
    envelope.observation.phaseValid = true;
    envelope.observation.beatEvent = (event % kSamplesPerBeat) < (uint64_t) numSamples;
    envelope.streamGeneration = 1;
    envelope.blockStartSampleTime = event;
    envelope.inputHorizonSampleTime = blockStart + (uint64_t) numSamples;
    envelope.sourceSampleRate = kSampleRate;
    return envelope;
}

/** Emulated audio-owner drum transport. It consumes the same bridge queue the
    real DrumEngine consumes and publishes a DrumPlaybackEcho the same way. */
struct EmulatedDrum
{
    bool joined = false;
    bool playing = false;
    uint64_t joinBoundary = 0;
    uint64_t stopBoundary = 0;
    int joinsSeen = 0;
    int stopsSeen = 0;

    void pump (jam::LiveJamSession& session, uint64_t cursor)
    {
        jam::DrumClockCommand command;
        while (session.drumCommandQueue().pop (command))
        {
            switch (command.type)
            {
                case jam::DrumClockCommandType::JoinAtBar:
                    ++joinsSeen;
                    joined = true;
                    joinBoundary = command.sampleTime;
                    break;
                case jam::DrumClockCommandType::StopAtBar:
                    ++stopsSeen;
                    stopBoundary = command.sampleTime;
                    break;
                default:
                    break;
            }
        }

        if (joined && ! playing && cursor >= joinBoundary)
            playing = true;
        if (playing && stopsSeen > 0 && cursor >= stopBoundary)
            playing = false;

        jam::DrumPlaybackEcho echo;
        echo.attached = true;
        echo.injectedActive = joined;
        echo.injectedPlaying = playing;
        echo.samplePosition = cursor;
        session.publishDrumEcho (echo);
    }
};

struct CountingTracker : jam::IRhythmTracker
{
    std::atomic<uint64_t> samplesSeen { 0 };
    const char* id() const noexcept override { return "counting"; }
    void reset (double) override {}
    jam::RhythmObservation process (const jam::AnalysisFrame& frame) override
    {
        samplesSeen.fetch_add (frame.numSamples, std::memory_order_relaxed);
        jam::RhythmObservation observation;
        observation.inputSampleTime = frame.sampleTime;
        observation.silence = true;
        return observation;
    }
};

bool chunkingCheck()
{
    auto tracker = std::make_unique<CountingTracker>();
    auto* raw = tracker.get();
    jam::LiveJamSessionConfig config;
    config.availableBackend = jam::JamLiveBackend::injectedTest;
    config.audioRingCapacity = 8;
    jam::LiveJamSession session (config);
    session.setTracker (std::move (tracker));
    session.prepare (kSampleRate, kBlock, false);

    // A 4096-frame callback split into two <=2048 chunks, as the processor does.
    float chunk[jam::kMaxAnalysisBlock] = {};
    session.pushAudio (chunk, jam::kMaxAnalysisBlock, 0, kSampleRate);
    session.pushAudio (chunk, jam::kMaxAnalysisBlock, jam::kMaxAnalysisBlock, kSampleRate);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);
    while (raw->samplesSeen.load (std::memory_order_relaxed) < 4096
           && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for (std::chrono::milliseconds (1));

    const bool ok = raw->samplesSeen.load (std::memory_order_relaxed) == 4096;
    session.release();
    return ok;
}

} // namespace

int main()
{
    std::printf ("live-jam-pipeline driver: deterministic 120 BPM trace\n");

    jam::LiveJamSessionConfig config;
    config.availableBackend = jam::JamLiveBackend::injectedTest;
    config.audioRingCapacity = 8;
    jam::LiveJamSession session (config);
    // The scripted observations are injected, so a null tracker would be fine,
    // but a real tracker keeps the backend "available" as in production.
    session.setTracker (std::make_unique<CountingTracker>());
    session.prepare (kSampleRate, kBlock, false);

    EmulatedDrum drum;
    bool requestedStart = false;
    bool sawJoin = false;
    bool sawJoinPending = false;
    bool sawPlaying = false;
    bool stopSent = false;
    bool sawStop = false;
    bool sawStopPending = false;

    const uint64_t totalSamples = (uint64_t) (kSampleRate * 12.0); // 12 s ~ 4 bars

    for (uint64_t cursor = 0; cursor + (uint64_t) kBlock <= totalSamples;
         cursor += (uint64_t) kBlock)
    {
        // Advance the audio cursor, inject the scripted observation for this
        // block, then step control. The cursor must lead the observation or it
        // is (correctly) rejected as a future event.
        session.publishAudioCursor (cursor + (uint64_t) kBlock);
        session.injectObservationForTesting (scriptedObservation (cursor, kBlock));
        session.stepControlForTesting();

        const jam::JamLiveState state = [&session] {
            jam::JamLiveState s;
            (void) session.readState (s);
            return s;
        }();

        // Around 9 s, request Start; around 11 s, request Stop.
        if (! requestedStart && cursor >= (uint64_t) (kSampleRate * 9.0))
        {
            session.submitCommand ({ jam::JamLiveCommandType::Start, 0.0 });
            requestedStart = true;
        }
        if (requestedStart && ! state.drumsPlaying && state.joinPending)
            sawJoinPending = true;
        if (state.drumsPlaying)
            sawPlaying = true;

        if (requestedStart && sawPlaying && ! stopSent
            && cursor >= (uint64_t) (kSampleRate * 11.0))
        {
            session.submitCommand ({ jam::JamLiveCommandType::Stop, 0.0 });
            stopSent = true;
        }
        if (stopSent && ! state.requestedRunning)
            sawStopPending = true;

        drum.pump (session, cursor);

        if ((cursor % (kSamplesPerBeat * 8)) == 0)
        {
            std::printf ("  t=%6.2fs lock=%-8s bpm=%6.1f req=%d joinPending=%d playing=%d\n",
                         (double) cursor / kSampleRate,
                         jam::toString (state.clock.lockState),
                         state.clock.bpm,
                         state.requestedRunning ? 1 : 0,
                         state.joinPending ? 1 : 0,
                         state.drumsPlaying ? 1 : 0);
        }
    }

    // Let the stop land and confirm no automatic rejoin.
    for (int i = 0; i < 200; ++i)
    {
        drum.pump (session,
                   totalSamples + (uint64_t) i * (uint64_t) kBlock);
        session.publishAudioCursor (totalSamples + (uint64_t) i * (uint64_t) kBlock);
        session.stepControlForTesting();
    }

    const jam::JamLiveState finalState = [&session] {
        jam::JamLiveState s;
        (void) session.readState (s);
        return s;
    }();

    sawJoin = drum.joinsSeen > 0;
    sawStop = drum.stopsSeen > 0;

    std::printf ("summary: joins=%d stops=%d requestedRunning=%d drumsPlaying=%d\n",
                 drum.joinsSeen, drum.stopsSeen,
                 finalState.requestedRunning ? 1 : 0,
                 finalState.drumsPlaying ? 1 : 0);

    check (sawJoin, "a join was requested from a usable lock");
    check (sawJoinPending, "joinPending was visible before drumsPlaying");
    check (sawPlaying, "drumsPlaying echoed after the join");
    check (sawStopPending, "requestedRunning cleared on Stop");
    check (sawStop, "a stop was committed on Stop");
    check (! finalState.requestedRunning, "no automatic resume after Stop");
    check (! finalState.drumsPlaying, "drums stopped after Stop");

    std::printf ("chunking: oversized callback split at %d ... ", kChunk);
    const bool chunked = chunkingCheck();
    check (chunked, "oversized callback was not truncated");
    std::printf ("%s\n", chunked ? "ok" : "FAILED");

    if (failures == 0)
    {
        std::printf ("RESULT: PASS\n");
        return 0;
    }

    std::printf ("RESULT: FAIL (%d invariant(s))\n", failures);
    return 1;
}
