#include "TestHarness.h"
#include "DrumEngine.h"
#include "DrumGenerator.h"

#include <cstring>
#include <memory>
#include <utility>

namespace
{
using Pattern = juce::uint8[drum::numVoices][drum::maxStepsPerBar];
using Hits = std::vector<std::pair<int, int>>; // absolute sample, GM note

// A real AudioPluginInstance observes engine MIDI without an audio device/UI.
class FoundationSink final : public juce::AudioPluginInstance
{
public:
    const juce::String getName() const override { return "foundation MIDI sink"; }
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

struct Transport
{
    DrumEngine engine;
    FoundationSink sink;
    juce::AudioBuffer<float> audio { 2, 8192 };
    juce::MidiBuffer midi;
    Hits hits;
    int elapsed = 0;

    Transport()
    {
        engine.prepare (8000.0, 8192);
        engine.bpm.store (120.0f); // exactly 1000 samples per sixteenth
        engine.humanVel.store (0.0f);
        engine.humanTime.store (0.0f);
        engine.humanRR.store (0.0f);
        midi.ensureSize (65536);
        for (int b = 0; b < drum::barsPerSection; ++b)
        {
            engine.barUsed[b].store (true);
            for (int s = 0; s < drum::maxStepsPerBar; ++s)
                engine.pattern[b][drum::kick][s].store (1);
        }
    }

    void block (int n)
    {
        REQUIRE (n > 0 && n <= audio.getNumSamples());
        engine.process (audio, n, &sink, midi);
        for (const auto event : midi)
            if (event.getMessage().isNoteOn())
            {
                CHECK (event.getMessage().getChannel() == 10);
                CHECK (event.samplePosition >= 0 && event.samplePosition < n);
                hits.emplace_back (elapsed + event.samplePosition,
                                   event.getMessage().getNoteNumber());
            }
        elapsed += n;
    }

    void render (int n, int chunk = 128)
    {
        while (n > 0)
        {
            const int size = juce::jmin (n, chunk);
            block (size);
            n -= size;
        }
    }
};

void checkKickOnly (const char* spec, const char* expected)
{
    Pattern p;
    drum::parseSpec ({ "TEST", "legacy compatibility", 120, 0, false, spec }, p);
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < drum::maxStepsPerBar; ++s)
        {
            const int want = v == drum::kick && s < (int) std::strlen (expected)
                           ? expected[s] - '0' : 0;
            CHECK_MSG (p[v][s] == want, std::string (spec) + " voice="
                       + std::to_string (v) + " step=" + std::to_string (s));
        }
}
}

TEST_CASE (foundation_parser_legacy_invalid_numeric_contract)
{
    // No strict parser exists. These are version-sensitive compatibility cases,
    // not endorsements of accepting malformed syntax.
    checkKickOnly ("K:abc", "1");
    checkKickOnly ("K:-4", "11111");
    checkKickOnly ("K:4junk!", "00002");
    checkKickOnly ("K:!", "2");
    checkKickOnly ("K:.", "3");
    checkKickOnly ("K:5-2", "");
    checkKickOnly ("K:2-4/nope", "00111");
    checkKickOnly ("k:0|Kick:1|K0|X:2|K:3", "0001");
    checkKickOnly ("K:0!|K:0.", "3");
}

TEST_CASE (foundation_duplicate_labels_preserve_positional_payloads)
{
    // Factory rows/drag IDs are positional, not deduplicated by display label.
    // Known duplicate has distinct rhythm and tempo; pin both payloads without
    // pinning numeric indices (inserting a factory row must remain possible).
    const auto& lib = drum::library();
    std::vector<size_t> rows;
    for (size_t i = 0; i < lib.size(); ++i)
        if (juce::String (lib[i].genre) == "ROCK"
            && juce::String (lib[i].name) == "Shuffle rock")
            rows.push_back (i);
    REQUIRE (rows.size() == 2);
    CHECK (rows[0] != rows[1]);
    CHECK (lib[rows[0]].bpm == 132);
    CHECK (lib[rows[1]].bpm == 120);
    auto e = std::make_unique<DrumEngine>();
    Pattern p;
    for (int b = 0; b < 2; ++b)
    {
        const auto& g = lib[rows[(size_t) b]];
        drum::parseSpec (g, p);
        e->setMeter (b, g.num, g.den);
        e->setBarPattern (p, b);
        e->barNames[b] = juce::String (g.name);
    }
    CHECK (e->barNames[0] == e->barNames[1]);
    CHECK (e->pattern[0][drum::hat][1].load() == 0);
    CHECK (e->pattern[1][drum::hat][1].load() == 1);
    const auto first = e->barToString (0), second = e->barToString (1);
    CHECK (first != second);
    e->barFromString (second, 2);
    e->barFromString (first, 3);
    CHECK (e->barToString (2) == second);
    CHECK (e->barToString (3) == first);
}

TEST_CASE (foundation_meter_fallbacks_and_fractional_grid_contract)
{
    CHECK (drum::stepsForMeter (2, 2) == 16);
    CHECK (drum::stepsForMeter (1, 16) == 1);
    CHECK (drum::stepsForMeter (16, 16) == 16);
    CHECK (drum::stepsForMeter (16, 2) == 32);
    // Unsupported denominators are currently accepted, rounded, then capped.
    CHECK (drum::stepsForMeter (1, 3) == 5);
    CHECK (drum::stepsForMeter (1, 32) == 1);
    auto e = std::make_unique<DrumEngine>();
    CHECK (e->meterNum (0) == 4 && e->meterDen (0) == 4);
    e->setMeter (0, 0, 0);
    CHECK (e->meterNum (0) == 1 && e->meterDen (0) == 4);
    CHECK (e->barSteps (0) == 4);
    e->setMeter (0, 99, -8);
    CHECK (e->meterNum (0) == 16 && e->meterDen (0) == 4);
    CHECK (e->barSteps (0) == 32);
}

TEST_CASE (foundation_codec_silent_used_and_absent_bar_are_distinct)
{
    auto e = std::make_unique<DrumEngine>();
    e->setMeter (0, 7, 8);
    const auto zeros = juce::String::repeatedString ("0", 126);
    e->barFromString (zeros, 0);
    CHECK (e->barUsed[0].load());
    CHECK (e->barToString (0) == zeros);
    e->barFromString ("garbage", 0);
    CHECK (e->barUsed[0].load());
    CHECK (e->barToString (0) == zeros);
    e->barFromString ({}, 0); // processor also passes this for absent bar fields
    CHECK (! e->barUsed[0].load());
    CHECK (e->barToString (0).isEmpty());
    CHECK (e->meterNum (0) == 7 && e->meterDen (0) == 8);
}

TEST_CASE (foundation_codec_reload_meter_first_and_clear_hidden_tail)
{
    auto e = std::make_unique<DrumEngine>();
    e->setMeter (0, 7, 4);
    e->barFromString (juce::String::repeatedString ("2", 252), 0);
    e->setMeter (0, 1, 16);
    e->barFromString ("123012301EXTRA", 0);
    CHECK (e->barToString (0) == "123012301");
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 1; s < drum::maxStepsPerBar; ++s)
            CHECK (e->pattern[0][v][s].load() == 0);
    e->setMeter (0, 7, 4);
    CHECK (e->pattern[0][drum::floorTom][27].load() == 0);
    CHECK (e->pattern[0][drum::snare][0].load() == 2);
    CHECK (e->barToString (0).length() == 252);
}

TEST_CASE (foundation_generator_interleaved_calls_replay_complete_saved_bar)
{
    Pattern original, noise, replay;
    auto e = std::make_unique<DrumEngine>();
    for (const char* role : { "chorus", "bridge", "breakdown", "fill" })
        for (const juce::uint32 seed : { 0u, 0xffffffffu })
        {
            drum::GenParams gp;
            gp.genre = "METAL"; gp.style = "progressive";
            gp.num = 7; gp.den = 8; gp.role = role; gp.seed = seed;
            gp.complexity = 0.85f; gp.fillFreq = 0.5f;
            const int steps = drum::generateBar (gp, original);
            auto other = gp;
            other.seed = 12345; other.role = "verse"; other.num = 3;
            drum::generateBar (other, noise); // no hidden RNG/call-order state
            CHECK (drum::generateBar (gp, replay) == steps);
            CHECK (std::memcmp (original, replay, sizeof (original)) == 0);
            e->setMeter (0, gp.num, gp.den);
            e->setBarPattern (original, 0);
            const auto saved = e->barToString (0);
            e->clearBar (0);
            e->barFromString (saved, 0);
            for (int v = 0; v < drum::numVoices; ++v)
                for (int s = 0; s < drum::maxStepsPerBar; ++s)
                    CHECK (e->pattern[0][v][s].load() == original[v][s]);
        }
}

TEST_CASE (foundation_transport_stop_flushes_once_and_restart_starts_bar_zero)
{
    Transport t;
    t.block (32);
    CHECK (t.hits.empty());
    CHECK (t.engine.uiBar.load() == -1 && t.engine.uiStep.load() == -1);
    t.engine.playing.store (true);
    t.render (2200);
    REQUIRE (t.hits.size() == 3);
    CHECK (t.hits[0].first == 40 && t.hits[2].first == 2040);
    CHECK (t.engine.uiStep.load() == 2);
    t.engine.playing.store (false);
    t.block (128);
    CHECK (t.engine.uiBar.load() == -1 && t.engine.uiStep.load() == -1);
    int offs = 0;
    for (const auto event : t.midi)
    {
        CHECK (event.getMessage().isNoteOff());
        CHECK (event.samplePosition == 0);
        ++offs;
    }
    CHECK (offs == drum::numVoices);
    t.block (128);
    CHECK (t.midi.isEmpty());
    const int restart = t.elapsed;
    t.engine.playing.store (true);
    t.block (32);
    REQUIRE (t.hits.size() == 4);
    CHECK (t.hits.back().first == restart + 8);
    CHECK (t.engine.uiBar.load() == 0 && t.engine.uiStep.load() == 0);
}

TEST_CASE (foundation_transport_meter_lengths_and_timeline_wrap)
{
    Transport t;
    t.engine.setMeter (0, 1, 16);
    t.engine.setMeter (1, 3, 8);
    t.engine.setMeter (2, 1, 16);
    t.engine.setMeter (3, 1, 16);
    t.engine.playing.store (true);
    t.render (9);
    CHECK (t.engine.uiBar.load() == 0 && t.engine.uiStep.load() == 0);
    t.render (1000);
    CHECK (t.engine.uiBar.load() == 1 && t.engine.uiStep.load() == 0);
    t.render (5000);
    CHECK (t.engine.uiBar.load() == 1 && t.engine.uiStep.load() == 5);
    t.render (1000);
    CHECK (t.engine.uiBar.load() == 2 && t.engine.uiStep.load() == 0);
    t.render (1000);
    CHECK (t.engine.uiBar.load() == 3 && t.engine.uiStep.load() == 0);
    t.render (1000);
    REQUIRE (t.hits.size() == 10);
    CHECK (t.hits.front().first == 8 && t.hits.back().first == 9008);
    // 1 + 6 + 1 + 1 steps = nine steps, then wrap to the first bar.
    CHECK (t.engine.uiBar.load() == 0 && t.engine.uiStep.load() == 0);
}

TEST_CASE (foundation_transport_count_in_is_legacy_fixed_four_four)
{
    Transport t;
    t.engine.setMeter (0, 1, 16);
    t.engine.countInOn.store (true);
    t.engine.playing.store (true);
    t.render (16008);
    CHECK (t.hits.empty());
    CHECK (t.engine.uiBar.load() == -1 && t.engine.uiStep.load() == -1);
    t.block (1);
    REQUIRE (t.hits.size() == 1);
    CHECK (t.hits[0].first == 16008);
    CHECK (t.engine.uiBar.load() == 0 && t.engine.uiStep.load() == 0);
    // Count-in is sixteen straight steps, even when bar zero has one step.
}

TEST_CASE (foundation_transport_bpm_mid_interval_preserves_pending_deadline)
{
    Transport t;
    t.engine.playing.store (true);
    t.render (508);
    t.engine.bpm.store (60.0f);
    t.render (2600);
    const Hits expected { { 8, 36 }, { 1008, 36 }, { 3008, 36 } };
    CHECK (t.hits == expected);
    // The already scheduled 120-BPM interval remains; no bar-boundary queue.
}

TEST_CASE (foundation_transport_bpm_change_at_bar_boundary_uses_new_next_interval)
{
    Transport t;
    t.engine.playing.store (true);
    t.render (16008); // exactly before the next bar's first scheduled hit
    REQUIRE (t.hits.size() == 16);
    t.engine.bpm.store (60.0f);
    t.block (1);
    CHECK (t.engine.uiBar.load() == 1 && t.engine.uiStep.load() == 0);
    t.render (2000);
    REQUIRE (t.hits.size() == 18);
    CHECK (t.hits[16].first == 16008);
    CHECK (t.hits[17].first == 18008);
}

TEST_CASE (foundation_transport_block_partition_preserves_unswung_midi_timing)
{
    Transport small, uneven;
    small.engine.playing.store (true);
    uneven.engine.playing.store (true);
    small.render (20009, 128);
    uneven.render (20009, 333);
    REQUIRE (small.hits.size() == 21);
    CHECK (small.hits == uneven.hits);
    CHECK (small.engine.uiBar.load() == uneven.engine.uiBar.load());
    CHECK (small.engine.uiStep.load() == uneven.engine.uiStep.load());
}
