#include "TestHarness.h"

#include "DrumEngine.h"

#include <memory>
#include <string>

//==============================================================================
// DrumEngine bar codec: barToString / barFromString are what the project state
// (and the song presets) are made of, so a round-trip that loses a ghost note
// or mixes up the voices silently corrupts every saved session.
namespace
{

using Pattern = juce::uint8[drum::numVoices][drum::maxStepsPerBar];

std::unique_ptr<DrumEngine> makeEngine()
{
    auto e = std::make_unique<DrumEngine>();   // ~10 kB of atomics: keep it off the stack
    e->prepare (48000.0, 128);
    return e;
}

void zero (Pattern p)
{
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < drum::maxStepsPerBar; ++s)
            p[v][s] = 0;
}

/// Deterministic pseudo-pattern covering every cell value.
void fillDeterministic (Pattern p, int steps, unsigned salt)
{
    zero (p);
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < steps; ++s)
            p[v][s] = (juce::uint8) ((v * 7 + s * 3 + (int) salt) % 4);   // 0..3
}

bool sameUpTo (const DrumEngine& e, const Pattern expected, int bar, int steps)
{
    for (int v = 0; v < drum::numVoices; ++v)
    {
        for (int s = 0; s < steps; ++s)
            if (e.pattern[bar][v][s].load() != expected[v][s])
                return false;

        for (int s = steps; s < drum::maxStepsPerBar; ++s)
            if (e.pattern[bar][v][s].load() != 0)
                return false;
    }
    return true;
}

} // namespace

//==============================================================================
TEST_CASE (codec_unused_bar_serialises_to_nothing)
{
    auto e = makeEngine();
    for (int bar = 0; bar < drum::maxBars; ++bar)
        CHECK (e->barToString (bar).isEmpty());

    Pattern p;
    fillDeterministic (p, drum::stepsPerBar, 0);
    e->setBarPattern (p, 5);
    CHECK (e->barToString (5).isNotEmpty());

    e->clearBar (5);
    CHECK_MSG (e->barToString (5).isEmpty(), "a cleared bar must serialise to an empty string");
}

TEST_CASE (codec_round_trip_in_4_4)
{
    auto e = makeEngine();
    const int bar = 2;
    Pattern p;
    fillDeterministic (p, drum::stepsPerBar, 1);

    e->setBarPattern (p, bar);
    const juce::String encoded = e->barToString (bar);

    CHECK_MSG (encoded.length() == drum::numVoices * drum::stepsPerBar,
               "encoded length is " + std::to_string (encoded.length()));
    CHECK (encoded.containsOnly ("0123"));

    e->clearBar (bar);
    CHECK (e->barToString (bar).isEmpty());

    e->barFromString (encoded, bar);
    CHECK_MSG (sameUpTo (*e, p, bar, drum::stepsPerBar), "the 4/4 round-trip changed the pattern");
    CHECK (e->barToString (bar) == encoded);
}

TEST_CASE (codec_round_trip_in_odd_meters)
{
    auto e = makeEngine();
    const int meters[][2] = { { 3, 4 }, { 5, 4 }, { 7, 4 }, { 5, 8 }, { 7, 8 }, { 9, 8 }, { 11, 8 } };

    int bar = 0;
    for (const auto& m : meters)
    {
        e->setMeter (bar, m[0], m[1]);
        const int steps = e->barSteps (bar);
        CHECK (steps == drum::stepsForMeter (m[0], m[1]));

        Pattern p;
        fillDeterministic (p, steps, (unsigned) bar);
        e->setBarPattern (p, bar);

        const juce::String encoded = e->barToString (bar);
        CHECK_MSG (encoded.length() == drum::numVoices * steps,
                   std::to_string (m[0]) + "/" + std::to_string (m[1]) + ": wrong encoded length");

        e->clearBar (bar);
        e->barFromString (encoded, bar);
        CHECK_MSG (sameUpTo (*e, p, bar, steps),
                   std::to_string (m[0]) + "/" + std::to_string (m[1]) + ": the round-trip changed the pattern");
        ++bar;
    }
}

TEST_CASE (codec_setBarPattern_clamps_to_the_meter)
{
    auto e = makeEngine();
    const int bar = 1;
    e->setMeter (bar, 4, 4);          // 16 steps

    Pattern p;
    fillDeterministic (p, drum::maxStepsPerBar, 3);   // fills all 32 steps
    for (int v = 0; v < drum::numVoices; ++v)
        p[v][20] = 2;                                  // clearly past the end

    e->setBarPattern (p, bar);

    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = drum::stepsPerBar; s < drum::maxStepsPerBar; ++s)
            CHECK_MSG (e->pattern[bar][v][s].load() == 0,
                       "setBarPattern kept a hit past the end of a 4/4 bar");

    CHECK (e->barToString (bar).length() == drum::numVoices * drum::stepsPerBar);
}

TEST_CASE (codec_empty_string_clears_the_bar)
{
    auto e = makeEngine();
    Pattern p;
    fillDeterministic (p, drum::stepsPerBar, 2);
    e->setBarPattern (p, 7);

    e->barFromString ({}, 7);
    CHECK (e->barToString (7).isEmpty());
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < drum::maxStepsPerBar; ++s)
            CHECK (e->pattern[7][v][s].load() == 0);
}

TEST_CASE (codec_tolerates_corrupt_input)
{
    auto e = makeEngine();
    const int bar = 4;

    // out-of-domain digits and letters become 0 instead of garbage cells
    juce::String junk;
    for (int i = 0; i < drum::numVoices * drum::stepsPerBar; ++i)
        junk += juce::String::charToString ((juce::juce_wchar) ("0123456789abZ#" [i % 14]));

    e->barFromString (junk, bar);
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < drum::maxStepsPerBar; ++s)
            CHECK_MSG (e->pattern[bar][v][s].load() <= 3, "a corrupt string produced a cell value > 3");

    // a string shorter than the bar zero-fills the tail instead of reading junk
    e->barFromString ("321", bar);
    CHECK (e->pattern[bar][drum::kick][0].load() == 3);
    CHECK (e->pattern[bar][drum::kick][1].load() == 2);
    CHECK (e->pattern[bar][drum::kick][2].load() == 1);
    for (int s = 3; s < drum::maxStepsPerBar; ++s)
        CHECK (e->pattern[bar][drum::kick][s].load() == 0);
    for (int v = 1; v < drum::numVoices; ++v)
        for (int s = 0; s < drum::maxStepsPerBar; ++s)
            CHECK (e->pattern[bar][v][s].load() == 0);

    // a string LONGER than the bar simply ignores the extra characters
    juce::String tooLong;
    for (int i = 0; i < drum::numVoices * drum::maxStepsPerBar + 50; ++i)
        tooLong += "1";
    e->barFromString (tooLong, bar);
    CHECK (e->barToString (bar).length() == drum::numVoices * e->barSteps (bar));
}

TEST_CASE (codec_bar_index_is_clamped)
{
    auto e = makeEngine();
    Pattern p;
    fillDeterministic (p, drum::stepsPerBar, 5);

    // out-of-range indices must be clamped, never write out of bounds
    e->setBarPattern (p, -10);
    CHECK (e->barToString (0).isNotEmpty());

    e->setBarPattern (p, drum::maxBars + 99);
    CHECK (e->barToString (drum::maxBars - 1).isNotEmpty());

    e->clearBar (-1);
    e->clearBar (drum::maxBars + 5);
    CHECK (e->barToString (0).isEmpty());
    CHECK (e->barToString (drum::maxBars - 1).isEmpty());

    e->setMeter (-3, 7, 8);
    CHECK (e->barSteps (0) == drum::stepsForMeter (7, 8));
    e->setMeter (0, 0, 0);                       // degenerate meter -> defaults
    CHECK (e->meterNum (0) >= 1);
    CHECK (e->meterDen (0) >= 1);
    CHECK (e->barSteps (0) >= 1 && e->barSteps (0) <= drum::maxStepsPerBar);
}

TEST_CASE (codec_totalBars_follows_the_sections)
{
    auto e = makeEngine();
    for (int s = 1; s <= drum::maxSections; ++s)
    {
        e->numSections.store (s);
        CHECK (e->totalBars() == s * drum::barsPerSection);
    }

    e->numSections.store (0);
    CHECK (e->totalBars() == drum::barsPerSection);
    e->numSections.store (999);
    CHECK (e->totalBars() == drum::maxBars);
}

TEST_CASE (codec_round_trip_of_the_whole_library)
{
    // End-to-end: library spec -> parseSpec -> timeline -> string -> timeline.
    // This is exactly the path taken when a groove is applied to a bar and the
    // session is saved and reloaded.
    auto e = makeEngine();
    const int bar = 3;
    Pattern p;
    int checked = 0;

    for (const auto& g : drum::library())
    {
        e->clearBar (bar);
        e->setMeter (bar, g.num, g.den);
        const int steps = e->barSteps (bar);

        drum::parseSpec (g, p);
        for (int v = 0; v < drum::numVoices; ++v)      // the timeline only keeps
            for (int s = steps; s < drum::maxStepsPerBar; ++s)  // the bar's own steps
                p[v][s] = 0;

        e->setBarPattern (p, bar);
        const juce::String encoded = e->barToString (bar);
        REQUIRE (encoded.length() == drum::numVoices * steps);

        e->clearBar (bar);
        e->barFromString (encoded, bar);

        const std::string what = std::string (g.genre) + " / " + std::string (g.name);
        CHECK_MSG (sameUpTo (*e, p, bar, steps), what + ": the round-trip changed the pattern");
        ++checked;
    }

    INFO_MSG ("library grooves round-tripped: " + std::to_string (checked));
}
