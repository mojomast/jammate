#include "TestHarness.h"

#include "DrumEngine.h"

#include <map>
#include <set>
#include <string>

//==============================================================================
// Invariants of the factory library (~570 hand-written entries in
// DrumLibrary.cpp). These are DATA tests: they catch typos that the very
// lenient parseSpec would otherwise swallow silently (a groove that plays
// nothing, a voice code that does not exist, a hit past the end of the bar,
// a genre that never shows up in the UI).
namespace
{

using Pattern = juce::uint8[drum::numVoices][drum::maxStepsPerBar];

const char* const kVoiceCodes = "KSHPRCTUF";

/// Re-implements the tokenizer of parseSpec, but REPORTING what parseSpec
/// silently drops. Returns a human readable problem, or an empty string.
std::string specProblem (const juce::String& spec)
{
    if (spec.trim().isEmpty())
        return "empty spec";

    for (const auto& tok : juce::StringArray::fromTokens (spec, "|", ""))
    {
        if (tok.trim().isEmpty())
            continue;

        const int colon = tok.indexOfChar (':');
        if (colon < 0)
            return ("token without ':' -> " + tok).toStdString();

        const juce::String code = tok.substring (0, colon).trim();
        if (code.length() != 1 || juce::String (kVoiceCodes).indexOfChar (code[0]) < 0)
            return ("unknown voice code -> " + code).toStdString();

        const juce::String items = tok.substring (colon + 1).trim();
        if (items.isEmpty())
            return ("voice without steps -> " + tok).toStdString();

        for (auto item : juce::StringArray::fromTokens (items, ",", ""))
        {
            item = item.trim();
            if (item.isEmpty())
                continue;
            if (item.endsWithChar ('!') || item.endsWithChar ('.'))
                item = item.dropLastCharacters (1);
            if (item.isEmpty())
                return ("step is only a suffix -> " + tok).toStdString();
            if (! item.containsOnly ("0123456789-/"))
                return ("non-numeric step '" + item + "' -> " + tok).toStdString();
            if (item.startsWithChar ('-'))
                return ("negative step '" + item + "' -> " + tok).toStdString();
        }
    }
    return {};
}

std::string label (const drum::Groove& g)
{
    return std::string (g.genre != nullptr ? g.genre : "<null>") + " / "
         + std::string (g.name != nullptr ? g.name : "<null>");
}

} // namespace

//==============================================================================
TEST_CASE (library_is_populated)
{
    const auto& lib = drum::library();
    REQUIRE (lib.size() > 100);
    INFO_MSG ("library entries: " + std::to_string (lib.size()));

    int fills = 0;
    for (const auto& g : lib)
        if (g.fill)
            ++fills;
    CHECK_MSG (fills > 0, "the library must contain fills as well as grooves");
    INFO_MSG ("fills: " + std::to_string (fills));
}

TEST_CASE (library_metadata_is_sane)
{
    for (const auto& g : drum::library())
    {
        REQUIRE (g.genre != nullptr && g.name != nullptr && g.spec != nullptr);

        CHECK_MSG (juce::String (g.genre).trim().isNotEmpty(), label (g) + ": empty genre");
        CHECK_MSG (juce::String (g.name).trim().isNotEmpty(),  label (g) + ": empty name");

        // bpm 0 means "keep the current tempo" (used by fills)
        CHECK_MSG (g.bpm == 0 || (g.bpm >= 30 && g.bpm <= 300),
                   label (g) + ": bpm out of range (" + std::to_string (g.bpm) + ")");

        CHECK_MSG (g.swing >= 0 && g.swing <= 60,
                   label (g) + ": swing out of 0..60 (" + std::to_string (g.swing) + ")");

        CHECK_MSG (g.num >= 1 && g.num <= 16,
                   label (g) + ": time signature numerator out of 1..16");
        CHECK_MSG (g.den == 2 || g.den == 4 || g.den == 8 || g.den == 16,
                   label (g) + ": time signature denominator is not a power of two");
        CHECK_MSG (drum::stepsForMeter (g.num, g.den) <= drum::maxStepsPerBar,
                   label (g) + ": the meter does not fit the step grid");

        // UTF-8: the project was bitten 3 times by juce::String(const char*)
        // decoding literals as Latin-1. A mojibake name is invalid UTF-8.
        CHECK_MSG (juce::CharPointer_UTF8::isValidString (g.name, 512),
                   label (g) + ": the name is not valid UTF-8");
        CHECK_MSG (juce::CharPointer_UTF8::isValidString (g.genre, 128),
                   label (g) + ": the genre is not valid UTF-8");
    }
}

TEST_CASE (library_every_groove_is_well_formed)
{
    for (const auto& g : drum::library())
    {
        const auto problem = specProblem (g.spec);
        CHECK_MSG (problem.empty(), label (g) + ": " + problem);
    }
}

TEST_CASE (library_every_groove_produces_a_playable_bar)
{
    Pattern p;
    int totalHits = 0;

    for (const auto& g : drum::library())
    {
        drum::parseSpec (g, p);

        const int steps = drum::stepsForMeter (g.num, g.den);
        int hits = 0, beyond = 0, badValue = 0;

        for (int v = 0; v < drum::numVoices; ++v)
            for (int s = 0; s < drum::maxStepsPerBar; ++s)
            {
                const juce::uint8 c = p[v][s];
                if (c == 0)
                    continue;
                ++hits;
                if (c > 3)   ++badValue;
                if (s >= steps) ++beyond;
            }

        CHECK_MSG (hits > 0, label (g) + ": the spec parses to a SILENT bar");
        CHECK_MSG (badValue == 0, label (g) + ": cell value outside 0..3");
        CHECK_MSG (beyond == 0, label (g) + ": " + std::to_string (beyond)
                                + " hit(s) past the end of a " + std::to_string (g.num)
                                + "/" + std::to_string (g.den) + " bar (" + std::to_string (steps) + " steps)");
        totalHits += hits;
    }

    INFO_MSG ("total hits parsed from the library: " + std::to_string (totalHits));
}

TEST_CASE (library_genres_are_all_reachable_from_the_ui)
{
    const auto listed = drum::genres();
    REQUIRE (listed.size() > 0);

    // no duplicates in the display order
    for (int i = 0; i < listed.size(); ++i)
        for (int j = i + 1; j < listed.size(); ++j)
            CHECK_MSG (listed[i] != listed[j],
                       ("genres() lists '" + listed[i] + "' twice").toStdString());

    // every genre used by the data must be selectable in the UI, otherwise
    // those grooves are dead weight (genres() only returns known names)
    std::set<std::string> used;
    for (const auto& g : drum::library())
        used.insert (std::string (g.genre));

    for (const auto& u : used)
        CHECK_MSG (listed.contains (juce::String (juce::CharPointer_UTF8 (u.c_str()))),
                   "genre '" + u + "' has grooves but is NOT returned by drum::genres()");

    // and every listed genre must have at least one entry
    for (const auto& l : listed)
        CHECK_MSG (used.count (l.toStdString()) > 0,
                   ("genres() lists '" + l + "' but no groove uses it").toStdString());

    INFO_MSG ("genres: " + std::to_string ((size_t) listed.size()));
}

TEST_CASE (library_duplicate_names_report_informative)
{
    // KNOWN ISSUE, deliberately NOT a failure: the library ships 16 duplicated
    // (genre, name) pairs today. They are only a UI annoyance (two identical
    // rows in the browser), so this case reports them instead of going red.
    // If the data is ever de-duplicated, tighten this into a CHECK.
    std::map<std::string, int> seen;
    for (const auto& g : drum::library())
        ++seen[std::string (g.genre) + " / " + std::string (g.name)];

    int dupes = 0;
    for (const auto& kv : seen)
        if (kv.second > 1)
        {
            ++dupes;
            INFO_MSG ("duplicate x" + std::to_string (kv.second) + ": " + kv.first);
        }

    INFO_MSG ("duplicate (genre,name) pairs: " + std::to_string (dupes) + " (known issue, not a failure)");
}

TEST_CASE (library_voice_tables_are_consistent)
{
    std::set<int> notes;
    std::set<std::string> ids;

    for (int v = 0; v < drum::numVoices; ++v)
    {
        REQUIRE (drum::voiceIds[v] != nullptr && drum::voiceNames[v] != nullptr);
        CHECK (juce::String (drum::voiceIds[v]).isNotEmpty());
        CHECK (juce::String (drum::voiceNames[v]).isNotEmpty());
        CHECK_MSG (juce::CharPointer_UTF8::isValidString (drum::voiceNames[v], 128),
                   std::string ("voice name is not valid UTF-8: ") + drum::voiceNames[v]);

        // General MIDI percussion range (channel 10)
        CHECK_MSG (drum::gmNote[v] >= 27 && drum::gmNote[v] <= 87,
                   std::string ("GM note out of the percussion range for ") + drum::voiceIds[v]);

        CHECK_MSG (notes.insert (drum::gmNote[v]).second,
                   std::string ("duplicated GM note for ") + drum::voiceIds[v]);
        CHECK_MSG (ids.insert (std::string (drum::voiceIds[v])).second,
                   std::string ("duplicated voice id: ") + drum::voiceIds[v]);
    }
}
