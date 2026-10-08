// Actual processor integration with deterministic observations and zero guitar.
// Proves adaptive transport/audio and callback paths, not guitar or hardware.
#include "PluginProcessor.h"
#include "jam/IRhythmTracker.h"
#include "RtProbeInstrumentation.h"
#include <juce_events/juce_events.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>

namespace
{
struct Tracker final : jam::IRhythmTracker
{
    void reset (double) override {}
    const char* id() const noexcept override { return "adaptive-injected-test"; }
    jam::RhythmObservation process (const jam::AnalysisFrame& f) override
    {
        jam::RhythmObservation o;
        o.inputSampleTime = f.sampleTime;
        o.sourceSampleRate = f.sourceSampleRate;
        o.bpmCandidate = 120.0f;
        o.beatPhase01 = static_cast<float> (std::fmod (f.sampleTime / (f.sourceSampleRate * 0.5), 1.0));
        o.beatConfidence01 = 0.99f;
        o.phaseValid = true;
        o.energyRmsDbfs = -20.0f;
        o.onsetStrength01 = 0.8f;
        o.transientDensity01 = 0.5f;
        return o;
    }
};
int failures = 0;
void check (bool ok, const char* message)
{
    std::printf ("%s %s\n", ok ? "PASS" : "FAIL", message);
    if (! ok) ++failures;
}
}

int main (int argc, char** argv)
{
    if (argc != 2) { std::fprintf (stderr, "usage: adaptive_jam_replay TRACE.jsonl\n"); return 2; }
    auto* trace = std::fopen (argv[1], "w");
    if (trace == nullptr) return 2;
    juce::ScopedJuceInitialiser_GUI gui;
    rtprobe::initInstrumentation();
    auto proc = std::make_unique<GuitarCompanionProcessor>();
    for (const auto* id : { "ampOn", "cabOn", "gateOn", "compOn", "odOn", "eqOn",
                            "modOn", "delayOn", "revOn", "pitchOn", "limOn", "looperOn" })
        if (auto* p = proc->apvts.getParameter (id)) p->setValueNotifyingHost (0.0f);
    check (proc->setJamTrackerForTesting (std::make_unique<Tracker>()), "tracker injection accepted before prepare");
    proc->prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    midi.ensureSize (65536);
    jam::JamLiveState state;
    std::uint64_t blocks = 0, nonzero = 0, mismatches = 0;
    double squareSum = 0.0;
    float peak = 0.0f;
    rtprobe::resetAll();
    auto block = [&]
    {
        buffer.clear(); midi.clear();
        rtprobe::arm(); proc->processBlock (buffer, midi); rtprobe::disarm();
        ++blocks;
        const auto& engine = proc->drumEngine;
        if (engine.injectedSamplePosition() != blocks * 512) ++mismatches;
        double squares = 0.0;
        for (int i = 0; i < 512; ++i)
        {
            const float v = buffer.getSample (0, i);
            squares += v * v;
            peak = std::max (peak, std::abs (v));
        }
        squareSum += squares;
        if (squares > 1.0e-10) ++nonzero;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
        proc->readJamLiveState (state); // retain whole previous state on false
        std::fprintf (trace, "{\"block\":%llu,\"audio_sample\":%llu,\"engine_playing\":%s,"
                      "\"engine_groove\":%d,\"style\":%d,\"fill_echo\":%s,\"echo_playing\":%s,"
                      "\"steps\":%llu,\"rms\":%.9g}\n",
                      static_cast<unsigned long long> (blocks),
                      static_cast<unsigned long long> (engine.injectedSamplePosition()),
                      engine.injectedPlaying() ? "true" : "false", engine.injectedGroove(),
                      state.styleIndex, state.fillPlaying ? "true" : "false", state.drumsPlaying ? "true" : "false",
                      static_cast<unsigned long long> (engine.injectedStepsFired()), std::sqrt (squares / 512));
    };
    auto send = [&] (jam::JamLiveCommandType type, double value = 0.0)
    { check (proc->submitJamCommand ({ type, value }), "live command accepted by bounded queue"); };
    send (jam::JamLiveCommandType::Start);
    for (int i = 0; i < 1400 && ! proc->drumEngine.injectedPlaying(); ++i) block();
    check (proc->drumEngine.injectedPlaying(), "actual initial join");
    send (jam::JamLiveCommandType::SetFillAmount, 0.0); // explicit fills only
    bool styleChanged = false;
    for (int style = 0; style < 6; ++style)
    {
        const auto priorGroove = proc->drumEngine.injectedGroove();
        send (jam::JamLiveCommandType::SetStyle, style);
        send (jam::JamLiveCommandType::SetIntensity, 0.75);
        send (jam::JamLiveCommandType::SetComplexity, 0.65);
        for (int i = 0; i < 400; ++i) block();
        check (state.styleIndex == style, "worker applied selected style");
        check (proc->drumEngine.injectedPlaying(), "style transition retains actual playback");
        styleChanged = styleChanged || proc->drumEngine.injectedGroove() != priorGroove;
    }
    check (styleChanged, "style controls change actual prepared groove");
    send (jam::JamLiveCommandType::RequestFill);
    bool fill = false, reverted = false;
    for (int i = 0; i < 600; ++i)
    {
        block();
        fill = fill || state.fillPlaying;
        reverted = reverted || (fill && ! state.fillPlaying);
    }
    check (fill && reverted, "audio-owner fill echo and one-bar reversion");
    send (jam::JamLiveCommandType::Stop);
    for (int i = 0; i < 30 && proc->drumEngine.injectedActive(); ++i) block();
    check (! proc->drumEngine.injectedActive(), "Stop releases injected ownership");
    check (mismatches == 0, "all callbacks advance one coherent sample domain");
    check (nonzero > 100 && peak > 0.01f, "actual internal-kit output with zero guitar input");
    const auto measured = rtprobe::snapshot();
    check (rtprobe::allocCallTotal (measured) == 0 && rtprobe::freeCallTotal (measured) == 0
           && measured.lockCalls == 0 && measured.trylockCalls == 0 && measured.condWaitCalls == 0,
           "armed callback paths contain no detected allocation/free/lock/wait");
    std::printf ("MEASURE blocks=%llu nonzero=%llu rms=%.9g peak=%.9g alloc=%llu free=%llu locks=%llu waits=%llu\n",
                 static_cast<unsigned long long> (blocks), static_cast<unsigned long long> (nonzero),
                 std::sqrt (squareSum / (blocks * 512)), peak,
                 static_cast<unsigned long long> (rtprobe::allocCallTotal (measured)),
                 static_cast<unsigned long long> (rtprobe::freeCallTotal (measured)),
                 static_cast<unsigned long long> (measured.lockCalls),
                 static_cast<unsigned long long> (measured.condWaitCalls));
    proc->releaseResources();
    proc->readJamLiveState (state);
    check (! state.prepared && ! state.drumsPlaying, "released facade is cold");
    std::fclose (trace);
    return failures == 0 ? 0 : 1;
}
