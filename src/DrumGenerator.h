#pragma once

#include "DrumEngine.h"
#include <juce_core/juce_core.h>
#include <vector>

//==============================================================================
// Groove generation engine for the Drums module (phase 19).
//
// Ports the logic from the midi-drums project (fsecada01, MIT - see
// THIRD_PARTY.md) onto our step grid: from genre + style + drummer +
// parameters (0..1) + bar role (verse/chorus/bridge/breakdown/fill), it
// writes 1 BAR into a 9-voice pattern, HONORING the time signature (num/den):
// death in 4/4, djent in 7/8, prog in 5/4... it all runs through the same
// generator.
//
// Deterministic: same `seed` => same groove. The UI bumps the seed on every
// click to vary. Humanize/swing are NOT written here - they are applied at
// playback time by the DrumEngine atomics.
namespace drum
{

struct GenParams
{
    juce::String genre  = "METAL";     // see genGenres()
    juce::String style  = "progressive";
    juce::String drummer;              // empty = none; otherwise an id from genDrummers()
    juce::String role   = "verse";     // verse|chorus|bridge|breakdown|fill
    float complexity = 0.6f;           // density of subdivisions, ghosts, variation
    float dynamics   = 0.6f;           // accent/ghost contrast
    float fillFreq   = 0.2f;           // chance of mini-fills in non-fill bars
    int   num = 4, den = 4;            // time signature
    juce::uint32 seed = 1;
};

/// Generates 1 bar into `out` (zeroes it first). Returns the number of steps
/// filled (= stepsForMeter(num,den)). Covers up to maxStepsPerBar.
int generateBar (const GenParams& p, juce::uint8 out[numVoices][maxStepsPerBar]);

// ---- metadata for the UI (dropdowns) ---------------------------------------
juce::StringArray genGenres();                              // generator genres
juce::StringArray genStyles (const juce::String& genre);   // styles for the genre

struct GenDrummer
{
    const char* id;    // "hoglan"
    const char* name;  // "Gene Hoglan"
    const char* sig;   // short signature (UTF-8)
};
const std::vector<GenDrummer>& genDrummers();
bool drummerFitsGenre (const juce::String& drummerId, const juce::String& genre);

} // namespace drum
