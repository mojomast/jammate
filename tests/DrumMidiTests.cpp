#include "TestHarness.h"
#include "DrumEngine.h"
#include "DrumMidiCapacity.h"

#include <algorithm>
#include <cstdlib>
#include <new>

#include "DrumHeapProbe.h"

namespace
{
class MidiSink final : public juce::AudioPluginInstance
{
public:
    const juce::String getName() const override { return "MIDI capacity probe"; }
    void fillInPluginDescription (juce::PluginDescription&) const override {}
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

struct Result
{
    std::size_t maxBytes = 0;
    std::size_t heapAllocations = 0;
    std::size_t heapDeallocations = 0;
    bool stableStorage = true;
};

Result exercise (double rate, int block, std::size_t reservation)
{
    DrumEngine engine;
    engine.prepare (rate, block);
    engine.bpm.store (260.0f);
    engine.swingPct.store (60.0f);
    engine.humanVel.store (1.0f);
    engine.humanTime.store (1.0f);
    engine.clickOn.store (false);
    for (int bar = 0; bar < drum::maxBars; ++bar)
    {
        engine.barUsed[bar].store (true);
        // Mixed meters exercise odd/even swing steps and frequent bar resets.
        engine.barNum[bar].store (bar % 3 == 0 ? 1 : 7);
        engine.barDen[bar].store (bar % 3 == 0 ? 16 : 4);
        for (int voice = 0; voice < drum::numVoices; ++voice)
            for (int step = 0; step < drum::maxStepsPerBar; ++step)
                engine.pattern[bar][voice][step].store (2);
    }
    for (int voice = 0; voice < drum::numVoices; ++voice)
        for (int step = 0; step < drum::maxStepsPerBar; ++step)
            engine.auditionPat[voice][step].store (2);

    MidiSink guest;
    juce::AudioBuffer<float> audio (2, block);
    juce::MidiBuffer midi;
    midi.ensureSize (reservation);
    const auto* storage = midi.data.begin();
    Result result;
    const int blocks = static_cast<int> (rate * 2.0 / block) + 2;
    engine.playing.store (true);
    for (int i = 0; i < blocks; ++i)
    {
        // Exercise stop cleanup plus audition, resume, and tempo/swing changes.
        engine.auditionOn.store (i >= blocks / 3 && i < 2 * blocks / 3);
        engine.bpm.store ((i % 3 == 0) ? 40.0f : 260.0f);
        engine.swingPct.store ((i % 2 == 0) ? 0.0f : 60.0f);
        if (i + 1 == blocks) engine.playing.store (false);

        drumprobe::beginMeasure();
        engine.process (audio, block, &guest, midi);
        drumprobe::endMeasure();
        result.heapAllocations += drumprobe::allocations();
        result.heapDeallocations += drumprobe::deallocations();
        result.maxBytes = std::max (result.maxBytes, static_cast<std::size_t> (midi.data.size()));
        result.stableStorage &= storage == midi.data.begin();
    }
    return result;
}
}

TEST_CASE (midi_old_256_byte_reservation_is_insufficient)
{
    const auto result = exercise (8000.0, 8192, 256);
    CHECK (result.maxBytes > 256);
#if defined(DRUM_MIDI_HEAP_PROBE)
    CHECK (result.heapAllocations > 0);
#endif
    INFO_MSG ("old reservation peak MIDI bytes: " + std::to_string (result.maxBytes));
}

TEST_CASE (midi_reserved_bound_survives_dense_patterns_and_transitions)
{
    std::size_t peak = 0;
    for (const double rate : { 8000.0, 44100.0, 48000.0, 96000.0 })
        for (const int block : { 128, 512, 2048, 8192 })
        {
            const auto bound = drum::midiScratchBytesForBlock (block);
            const auto result = exercise (rate, block, bound);
            CHECK (result.maxBytes <= bound);
            CHECK (result.stableStorage);
#if defined(DRUM_MIDI_HEAP_PROBE)
            CHECK (result.heapAllocations == 0);
            CHECK (result.heapDeallocations == 0);
#endif
            peak = std::max (peak, result.maxBytes);
        }
    INFO_MSG ("reserved-path peak MIDI bytes: " + std::to_string (peak));
}
