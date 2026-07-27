#include "TestHarness.h"

#include "DrumGenerator.h"

#include <set>
#include <string>

//==============================================================================
// drum::generateBar - the procedural groove generator. It is deterministic
// (same seed => same bar) and must NEVER write outside the step grid of the
// requested time signature, whatever the parameter soup thrown at it.
namespace
{

using Pattern = juce::uint8[drum::numVoices][drum::maxStepsPerBar];

struct Meter { int num, den; };

const Meter kMeters[] =
    { { 4, 4 }, { 3, 4 }, { 5, 4 }, { 7, 4 }, { 6, 8 }, { 7, 8 }, { 9, 8 }, { 5, 8 }, { 11, 8 } };

const char* const kRoles[] = { "verse", "chorus", "bridge", "breakdown", "fill" };

std::string key (const Pattern p, int steps)
{
    std::string k;
    k.reserve ((size_t) (drum::numVoices * steps));
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < steps; ++s)
            k += (char) ('0' + p[v][s]);
    return k;
}

int hitCount (const Pattern p, int steps)
{
    int n = 0;
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < steps; ++s)
            if (p[v][s] != 0)
                ++n;
    return n;
}

std::string describe (const drum::GenParams& p)
{
    return (p.genre + "/" + p.style + "/" + p.role + " "
            + juce::String (p.num) + "/" + juce::String (p.den)
            + " seed=" + juce::String ((int) p.seed)
            + (p.drummer.isEmpty() ? juce::String() : " drummer=" + p.drummer)).toStdString();
}

/// The invariants that must hold for EVERY generated bar.
void checkBar (const drum::GenParams& gp, const Pattern p, int returned)
{
    const int steps = drum::stepsForMeter (gp.num, gp.den);
    CHECK_MSG (returned == steps, describe (gp) + ": generateBar returned " + std::to_string (returned)
                                  + " instead of " + std::to_string (steps));

    int hits = 0;
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < drum::maxStepsPerBar; ++s)
        {
            const juce::uint8 c = p[v][s];
            if (c == 0)
                continue;
            CHECK_MSG (c <= 3, describe (gp) + ": cell value " + std::to_string ((int) c) + " outside 0..3");
            CHECK_MSG (s < steps, describe (gp) + ": hit at step " + std::to_string (s)
                                  + ", past the end of the bar (" + std::to_string (steps) + " steps)");
            ++hits;
        }

    CHECK_MSG (hits > 0, describe (gp) + ": generated a SILENT bar");
    CHECK_MSG (hits < drum::numVoices * steps, describe (gp) + ": every cell of every voice is on");
}

} // namespace

//==============================================================================
TEST_CASE (generator_sweep_all_invariants)
{
    Pattern p;
    int bars = 0;

    for (const auto& genre : drum::genGenres())
        for (const auto& style : drum::genStyles (genre))
            for (const char* role : kRoles)
                for (const auto& m : kMeters)
                    for (juce::uint32 seed = 1; seed <= 4; ++seed)
                    {
                        drum::GenParams gp;
                        gp.genre = genre;
                        gp.style = style;
                        gp.role = role;
                        gp.num = m.num;
                        gp.den = m.den;
                        gp.seed = seed;
                        gp.complexity = 0.15f + 0.2f * (float) (seed % 4);
                        gp.dynamics   = 0.9f - 0.2f * (float) (seed % 3);
                        gp.fillFreq   = 0.25f * (float) (seed % 4);

                        // pre-dirty: generateBar must clear the whole grid
                        for (int v = 0; v < drum::numVoices; ++v)
                            for (int s = 0; s < drum::maxStepsPerBar; ++s)
                                p[v][s] = 7;

                        checkBar (gp, p, drum::generateBar (gp, p));
                        ++bars;
                    }

    INFO_MSG ("bars generated: " + std::to_string (bars));
}

TEST_CASE (generator_sweep_with_every_drummer)
{
    Pattern p;

    for (const auto& d : drum::genDrummers())
        for (const auto& genre : drum::genGenres())
            for (const auto& m : kMeters)
                for (const char* role : kRoles)
                {
                    drum::GenParams gp;
                    gp.genre = genre;
                    gp.style = drum::genStyles (genre)[0];
                    gp.drummer = d.id;
                    gp.role = role;
                    gp.num = m.num;
                    gp.den = m.den;
                    gp.seed = 12345u;
                    gp.complexity = 0.8f;
                    gp.fillFreq = 0.5f;

                    checkBar (gp, p, drum::generateBar (gp, p));
                }
}

TEST_CASE (generator_is_deterministic_for_a_given_seed)
{
    Pattern a, b;

    for (const auto& genre : drum::genGenres())
        for (const auto& style : drum::genStyles (genre))
            for (juce::uint32 seed : { 1u, 7u, 4242u, 0u })
            {
                drum::GenParams gp;
                gp.genre = genre;
                gp.style = style;
                gp.role = "verse";
                gp.seed = seed;
                gp.complexity = 0.75f;
                gp.fillFreq = 0.4f;

                const int n1 = drum::generateBar (gp, a);
                const int n2 = drum::generateBar (gp, b);

                CHECK (n1 == n2);
                CHECK_MSG (key (a, n1) == key (b, n2),
                           describe (gp) + ": two calls with the same seed differ");
            }
}

TEST_CASE (generator_different_seeds_give_different_grooves)
{
    // Only meaningful for a style that actually consults the RNG.
    Pattern p;
    std::set<std::string> distinct;

    for (juce::uint32 seed = 1; seed <= 32; ++seed)
    {
        drum::GenParams gp;
        gp.genre = "METAL";
        gp.style = "progressive";
        gp.role = "verse";
        gp.seed = seed;
        gp.complexity = 0.85f;
        gp.fillFreq = 0.5f;
        distinct.insert (key (p, drum::generateBar (gp, p)));
    }

    CHECK_MSG (distinct.size() >= 4, "32 seeds produced only " + std::to_string (distinct.size())
                                     + " distinct grooves - the seed is barely used");
    INFO_MSG ("distinct grooves out of 32 seeds: " + std::to_string (distinct.size()));
}

TEST_CASE (generator_complexity_drives_density)
{
    Pattern p;
    int lowHits = 0, highHits = 0;

    for (const auto& genre : drum::genGenres())
        for (const auto& style : drum::genStyles (genre))
            for (juce::uint32 seed = 1; seed <= 16; ++seed)
            {
                drum::GenParams gp;
                gp.genre = genre;
                gp.style = style;
                gp.role = "verse";
                gp.seed = seed;
                gp.fillFreq = 0.0f;      // isolate complexity from the mini-fill
                gp.dynamics = 0.5f;

                gp.complexity = 0.05f;
                lowHits += hitCount (p, drum::generateBar (gp, p));

                gp.complexity = 0.95f;
                highHits += hitCount (p, drum::generateBar (gp, p));
            }

    INFO_MSG ("hits with complexity 0.05: " + std::to_string (lowHits)
              + " / with 0.95: " + std::to_string (highHits));
    CHECK_MSG (highHits > lowHits, "raising complexity did not add any note");
}

TEST_CASE (generator_role_fill_opens_with_a_crash)
{
    Pattern p;

    for (const auto& genre : drum::genGenres())
        for (const auto& m : kMeters)
        {
            drum::GenParams gp;
            gp.genre = genre;
            gp.style = drum::genStyles (genre)[0];
            gp.role = "fill";
            gp.num = m.num;
            gp.den = m.den;
            gp.seed = 99u;

            const int steps = drum::generateBar (gp, p);
            CHECK_MSG (p[drum::crash][0] == 2, describe (gp) + ": a fill must start on an accented crash");
            CHECK_MSG (p[drum::kick][0] != 0,  describe (gp) + ": a fill must start with the kick");

            int toms = 0;
            for (int s = 0; s < steps; ++s)
                toms += (p[drum::tom1][s] != 0) + (p[drum::tom2][s] != 0) + (p[drum::floorTom][s] != 0);
            CHECK_MSG (toms > 0, describe (gp) + ": a fill without a single tom");
        }
}

TEST_CASE (generator_role_chorus_accents_the_downbeat)
{
    Pattern p;

    for (const auto& genre : drum::genGenres())
        for (juce::uint32 seed = 1; seed <= 8; ++seed)
        {
            drum::GenParams gp;
            gp.genre = genre;
            gp.style = drum::genStyles (genre)[0];
            gp.role = "chorus";
            gp.seed = seed;
            gp.complexity = 0.7f;

            drum::generateBar (gp, p);
            CHECK_MSG (p[drum::crash][0] == 2, describe (gp) + ": a chorus must crash on beat 1");
            CHECK_MSG (p[drum::hat][0] == 0,   describe (gp) + ": the hat must give way to the crash on beat 1");
        }
}

TEST_CASE (generator_weckl_keeps_the_groove_linear)
{
    // "Linear" = hands and feet never land on the same step.
    Pattern p;

    for (const char* genre : { "JAZZ", "FUNK", "ROCK" })
        for (juce::uint32 seed = 1; seed <= 8; ++seed)
        {
            drum::GenParams gp;
            gp.genre = genre;
            gp.style = drum::genStyles (genre)[0];
            gp.drummer = "weckl";
            gp.role = "verse";
            gp.seed = seed;
            gp.complexity = 0.8f;

            const int steps = drum::generateBar (gp, p);
            for (int s = 0; s < steps; ++s)
                CHECK_MSG (! (p[drum::kick][s] != 0 && p[drum::snare][s] != 0),
                           describe (gp) + ": kick and snare together on step " + std::to_string (s));
        }
}

TEST_CASE (generator_degenerate_meters_stay_in_range)
{
    // The UI clamps the numerator to 1..16, but nothing stops a 1/4 or a 16/4
    // bar from reaching the generator. Nothing may be written outside the grid.
    Pattern p;

    for (const Meter m : { Meter { 1, 4 }, Meter { 2, 4 }, Meter { 16, 4 },
                           Meter { 1, 8 }, Meter { 3, 8 }, Meter { 12, 8 },
                           Meter { 2, 2 }, Meter { 4, 16 } })
        for (const char* role : kRoles)
        {
            drum::GenParams gp;
            gp.genre = "METAL";
            gp.style = "doom";       // takes the half-time branch
            gp.role = role;
            gp.num = m.num;
            gp.den = m.den;
            gp.seed = 5u;

            const int steps = drum::generateBar (gp, p);
            CHECK (steps >= 1 && steps <= drum::maxStepsPerBar);

            for (int v = 0; v < drum::numVoices; ++v)
                for (int s = steps; s < drum::maxStepsPerBar; ++s)
                    CHECK_MSG (p[v][s] == 0, describe (gp) + ": wrote past the end of the bar");
        }
}

TEST_CASE (generator_metadata_is_consistent)
{
    const auto genres = drum::genGenres();
    REQUIRE (genres.size() > 0);

    for (const auto& g : genres)
    {
        const auto styles = drum::genStyles (g);
        CHECK_MSG (styles.size() > 0, ("no style for the genre " + g).toStdString());
        for (const auto& s : styles)
            CHECK_MSG (s.isNotEmpty(), ("empty style in " + g).toStdString());
    }

    // an unknown genre must not return an empty list (the UI would show nothing)
    CHECK (drum::genStyles ("NOT-A-GENRE").size() > 0);

    std::set<std::string> ids;
    for (const auto& d : drum::genDrummers())
    {
        REQUIRE (d.id != nullptr && d.name != nullptr && d.sig != nullptr);
        CHECK (juce::String (d.id).isNotEmpty());
        CHECK (juce::String (d.name).isNotEmpty());
        CHECK_MSG (ids.insert (std::string (d.id)).second,
                   std::string ("duplicated drummer id: ") + d.id);

        // the signature line carries UTF-8 middle dots - the MSVC/Latin-1 trap
        CHECK_MSG (juce::CharPointer_UTF8::isValidString (d.sig, 256),
                   std::string ("the signature of ") + d.name + " is not valid UTF-8");
        CHECK_MSG (juce::CharPointer_UTF8::isValidString (d.name, 256),
                   std::string ("the name of ") + d.id + " is not valid UTF-8");

        // every drummer must be offered by at least one generator genre
        bool fits = false;
        for (const auto& g : genres)
            fits = fits || drum::drummerFitsGenre (d.id, g);
        CHECK_MSG (fits, std::string ("drummer ") + d.id + " does not fit any generator genre");
    }

    CHECK_MSG (drum::drummerFitsGenre ("", "METAL"), "an empty drummer id means 'none' and must be allowed");
}
