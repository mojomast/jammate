#include "TestHarness.h"

#include "DrumEngine.h"

//==============================================================================
// drum::parseSpec - the compact groove format "K:0,4!,8." used by the whole
// factory library. Cell values: 0 = nothing, 1 = hit, 2 = accent, 3 = ghost.
namespace
{

using Pattern = juce::uint8[drum::numVoices][drum::maxStepsPerBar];

/// Builds a throwaway Groove around a spec so parseSpec can be called directly.
drum::Groove specOf (const char* spec, int num = 4, int den = 4)
{
    return { "TEST", "Spec under test", 120, 0, false, spec, num, den };
}

void fillWithGarbage (Pattern p)
{
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < drum::maxStepsPerBar; ++s)
            p[v][s] = 7;   // out-of-domain value: parseSpec must wipe it
}

int countNonZero (const Pattern p)
{
    int n = 0;
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < drum::maxStepsPerBar; ++s)
            if (p[v][s] != 0)
                ++n;
    return n;
}

int countVoice (const Pattern p, int voice)
{
    int n = 0;
    for (int s = 0; s < drum::maxStepsPerBar; ++s)
        if (p[voice][s] != 0)
            ++n;
    return n;
}

} // namespace

//==============================================================================
TEST_CASE (spec_basic_hit_accent_ghost)
{
    Pattern p;
    fillWithGarbage (p);
    drum::parseSpec (specOf ("K:0,4!,8."), p);

    CHECK (p[drum::kick][0] == 1);   // plain hit
    CHECK (p[drum::kick][4] == 2);   // '!' = accent
    CHECK (p[drum::kick][8] == 3);   // '.' = ghost
    CHECK (countVoice (p, drum::kick) == 3);
    CHECK (countNonZero (p) == 3);   // nothing else was touched
}

TEST_CASE (spec_clears_the_output_buffer_first)
{
    Pattern p;
    fillWithGarbage (p);
    drum::parseSpec (specOf (""), p);
    CHECK_MSG (countNonZero (p) == 0, "an empty spec must produce a silent bar");

    fillWithGarbage (p);
    drum::parseSpec (specOf ("S:4"), p);
    CHECK (countNonZero (p) == 1);
    CHECK (p[drum::snare][4] == 1);
}

TEST_CASE (spec_all_voice_codes)
{
    Pattern p;
    drum::parseSpec (specOf ("K:0|S:1|H:2|P:3|R:4|C:5|T:6|U:7|F:8"), p);

    const int expected[drum::numVoices] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    const int voice[drum::numVoices] =
        { drum::kick, drum::snare, drum::hat, drum::hatPedal, drum::ride,
          drum::crash, drum::tom1, drum::tom2, drum::floorTom };

    for (int i = 0; i < drum::numVoices; ++i)
    {
        CHECK (p[voice[i]][expected[i]] == 1);
        CHECK (countVoice (p, voice[i]) == 1);
    }
    CHECK (countNonZero (p) == drum::numVoices);
}

TEST_CASE (spec_ranges_and_stride)
{
    Pattern p;
    drum::parseSpec (specOf ("H:0-14/2"), p);
    CHECK (countVoice (p, drum::hat) == 8);
    for (int s = 0; s < 16; ++s)
        CHECK (p[drum::hat][s] == (s % 2 == 0 ? 1 : 0));

    // a range without '/' has stride 1 (every step of the range)
    drum::parseSpec (specOf ("S:4-7"), p);
    CHECK (countVoice (p, drum::snare) == 4);
    CHECK (p[drum::snare][4] == 1 && p[drum::snare][7] == 1);

    // the suffix applies to the WHOLE range
    drum::parseSpec (specOf ("S:0-6/2!"), p);
    CHECK (countVoice (p, drum::snare) == 4);
    for (int s : { 0, 2, 4, 6 })
        CHECK (p[drum::snare][s] == 2);

    drum::parseSpec (specOf ("H:1-5/2."), p);
    for (int s : { 1, 3, 5 })
        CHECK (p[drum::hat][s] == 3);
    CHECK (countVoice (p, drum::hat) == 3);
}

TEST_CASE (spec_stride_zero_does_not_hang)
{
    // "/0" would be an infinite loop without the jmax(1, ...) guard.
    Pattern p;
    drum::parseSpec (specOf ("H:0-6/0"), p);
    CHECK (countVoice (p, drum::hat) == 7);   // falls back to stride 1
}

TEST_CASE (spec_later_items_overwrite_earlier_ones)
{
    // The library relies on this: "H:0-14/2,0!,8!" = eighths with 2 accents.
    Pattern p;
    drum::parseSpec (specOf ("H:0-14/2,0!,8!"), p);
    CHECK (p[drum::hat][0] == 2);
    CHECK (p[drum::hat][8] == 2);
    CHECK (p[drum::hat][2] == 1);
    CHECK (countVoice (p, drum::hat) == 8);
}

TEST_CASE (spec_invalid_input_is_ignored_not_crashing)
{
    Pattern p;

    // unknown voice letter
    drum::parseSpec (specOf ("X:0,4|Z:8"), p);
    CHECK_MSG (countNonZero (p) == 0, "unknown voice codes must be dropped");

    // token without ':'
    drum::parseSpec (specOf ("K0,4"), p);
    CHECK (countNonZero (p) == 0);

    // empty tokens / stray separators
    drum::parseSpec (specOf ("||K:0||"), p);
    CHECK (countNonZero (p) == 1);
    CHECK (p[drum::kick][0] == 1);

    // empty item list
    drum::parseSpec (specOf ("K:"), p);
    CHECK (countNonZero (p) == 0);

    // whitespace around the code and the items
    drum::parseSpec (specOf (" K : 0 , 4 "), p);
    CHECK (countVoice (p, drum::kick) == 2);
    CHECK (p[drum::kick][0] == 1 && p[drum::kick][4] == 1);
}

TEST_CASE (spec_steps_out_of_range_never_write_out_of_bounds)
{
    Pattern p;

    // way past the end of the grid: silently dropped (guard s < maxStepsPerBar)
    drum::parseSpec (specOf ("K:40,100,999"), p);
    CHECK_MSG (countNonZero (p) == 0, "steps >= maxStepsPerBar must be dropped");

    // a range that starts inside and ends outside is clipped, not truncated
    drum::parseSpec (specOf ("K:28-99"), p);
    CHECK (countVoice (p, drum::kick) == drum::maxStepsPerBar - 28);
    CHECK (p[drum::kick][drum::maxStepsPerBar - 1] == 1);

    // parseSpec fills the WHOLE 32-step grid (odd meters use more than 16
    // steps); clamping to the bar's own step count is setBarPattern's job.
    drum::parseSpec (specOf ("S:20"), p);
    CHECK_MSG (p[drum::snare][20] == 1, "parseSpec covers up to maxStepsPerBar");
}

TEST_CASE (spec_known_lenient_parsing_quirks)
{
    // Documented, NOT asserted as desirable: juce::String::getIntValue()
    // returns 0 for anything non-numeric, so garbage lands on step 0. It is
    // harmless (no out-of-bounds) but it hides typos in the library.
    Pattern p;

    drum::parseSpec (specOf ("K:abc"), p);
    if (p[drum::kick][0] != 0)
        INFO_MSG ("non-numeric step 'abc' silently becomes step 0");

    drum::parseSpec (specOf ("K:-4"), p);
    if (countVoice (p, drum::kick) > 1)
        INFO_MSG ("a leading '-' is read as the range 0-4, not as a negative step");

    // Whatever the interpretation is, it must stay inside the grid.
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < drum::maxStepsPerBar; ++s)
            CHECK (p[v][s] <= 3);
}

TEST_CASE (spec_stepsForMeter_matches_the_grid)
{
    CHECK (drum::stepsForMeter (4, 4) == 16);
    CHECK (drum::stepsForMeter (3, 4) == 12);
    CHECK (drum::stepsForMeter (5, 4) == 20);
    CHECK (drum::stepsForMeter (7, 4) == 28);
    CHECK (drum::stepsForMeter (7, 8) == 14);
    CHECK (drum::stepsForMeter (5, 8) == 10);
    CHECK (drum::stepsForMeter (6, 8) == 12);
    CHECK (drum::stepsForMeter (9, 8) == 18);
    CHECK (drum::stepsForMeter (11, 8) == 22);

    // degenerate input falls back to 4/4 and never exceeds the grid
    CHECK (drum::stepsForMeter (0, 4) == drum::stepsPerBar);
    CHECK (drum::stepsForMeter (4, 0) == drum::stepsPerBar);
    CHECK (drum::stepsForMeter (-1, -1) == drum::stepsPerBar);
    CHECK (drum::stepsForMeter (32, 4) == drum::maxStepsPerBar);
    CHECK (drum::stepsForMeter (99, 1) == drum::maxStepsPerBar);
    CHECK (drum::stepsForMeter (1, 16) >= 1);
}
