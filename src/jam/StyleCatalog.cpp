// StyleCatalog — the six shipped style overlays over the compiled drum library.
//
// REVIEW-GENERATED. The descriptor tables below were produced from the measured
// `src/DrumLibrary.cpp` at sha256
//   68008b4b139750e337a6166f6881e5724eee577b196efd8f467bf9562153d636
// by the authoring tool `tools/style-catalog/build_style_catalog.py`. The
// shipped file is normal C++ and is read directly by
// `tools/style-catalog/style_catalog_provenance.py --check`, which re-parses
// this file and re-resolves every reference against the live library. Edit the
// tables here, then re-run the verifier; do not regenerate blindly.
//
// The per-reference comments repeat the library bpm/swing only as a review aid.

#include "jam/StyleCatalog.h"

#include <cstring>

namespace jam
{

namespace
{
void copyTier (StyleDescriptor& d, GrooveTier tier, const PatternProvenance* refs, int n) noexcept
{
    const int c = n < kMaxTierPatterns ? n : kMaxTierPatterns;
    for (int i = 0; i < c; ++i)
        d.grooves[(int) tier][i] = refs[i];
    d.grooveCount[(int) tier] = c;
}

void copyFill (StyleDescriptor& d, FillKind kind, const PatternProvenance* refs, int n) noexcept
{
    const int c = n < kMaxFillPatterns ? n : kMaxFillPatterns;
    for (int i = 0; i < c; ++i)
        d.fills[(int) kind][i] = refs[i];
    d.fillCount[(int) kind] = c;
}

StyleDescriptor buildRock()
{
    StyleDescriptor d {};
    d.id = StyleId::Rock;
    d.name = "Rock";
    d.shortName = "rock";
    d.minBpm = 70;
    d.idealBpm = 110;
    d.maxBpm = 175;
    d.meters[0] = Meter { 4, 4 };
    d.meterCount = 1;
    d.defaultSwing01 = 0.0f;
    d.minSwing01 = 0.0f;
    d.maxSwing01 = 0.0f;
    d.minRepetitionDistanceBars = 4;

    static const PatternProvenance low[] = {
            { 2, "ROCK", "Half-time", false, 0xCA5BC6ADu, 4, 4 },  // bpm 88 swing 0
            { 3, "ROCK", "Half-time 16", false, 0x63F1DEDDu, 4, 4 },  // bpm 92 swing 0
            { 8, "ROCK", "Power ballad", false, 0x20A28638u, 4, 4 },  // bpm 72 swing 0
    };
    static const PatternProvenance medium[] = {
            { 0, "ROCK", "Basic", false, 0xB771340Eu, 4, 4 },  // bpm 104 swing 0
            { 1, "ROCK", "Basic w/ crash", false, 0x23CDFCD3u, 4, 4 },  // bpm 104 swing 0
            { 5, "ROCK", "80s", false, 0x02992999u, 4, 4 },  // bpm 112 swing 0
            { 13, "ROCK", "Grunge", false, 0x777C60CAu, 4, 4 },  // bpm 100 swing 0
            { 17, "ROCK", "Broken rock", false, 0x30A0D98Fu, 4, 4 },  // bpm 108 swing 0
            { 18, "ROCK", "Half-time 16", false, 0x3FDD40AAu, 4, 4 },  // bpm 84 swing 0
    };
    static const PatternProvenance high[] = {
            { 6, "ROCK", "Eighths on kick", false, 0x1C1074A0u, 4, 4 },  // bpm 128 swing 0
            { 11, "ROCK", "Four on the floor", false, 0x718ADE72u, 4, 4 },  // bpm 130 swing 0
            { 14, "ROCK", "16 on ride", false, 0xEAAC41C0u, 4, 4 },  // bpm 118 swing 0
            { 16, "ROCK", "Driving 8ths", false, 0x1AFD80B9u, 4, 4 },  // bpm 150 swing 0
            { 19, "ROCK", "Arena", false, 0xE94F92CCu, 4, 4 },  // bpm 128 swing 0
    };
    static const PatternProvenance shortFills[] = {
            { 412, "ROCK", "Rock 3", true, 0xD409D962u, 4, 4 },  // bpm 115 swing 0
            { 415, "ROCK", "Rock 4", true, 0x8B4B3209u, 4, 4 },  // bpm 115 swing 0
            { 168, "GENERAL", "Short roll", true, 0xDD8D0DA5u, 4, 4 },  // bpm 0 swing 0
            { 165, "GENERAL", "Snare build", true, 0xEA91737Cu, 4, 4 },  // bpm 0 swing 0
    };
    static const PatternProvenance longFills[] = {
            { 413, "ROCK", "Indie 2", true, 0x888B4292u, 4, 4 },  // bpm 63 swing 0
            { 416, "ROCK", "Indie 3", true, 0xD6BAEA94u, 4, 4 },  // bpm 63 swing 0
            { 169, "GENERAL", "Long roll", true, 0xA3BF63B9u, 4, 4 },  // bpm 0 swing 0
    };
    static const PatternProvenance transitionFills[] = {
            { 539, "ROCK", "Fill rock crescendo", true, 0x2E13D4E5u, 4, 4 },  // bpm 0 swing 0
            { 195, "GENERAL", "Crescendo", true, 0x58E2C105u, 4, 4 },  // bpm 0 swing 0
    };

    copyTier (d, GrooveTier::Low, low, (int) (sizeof (low) / sizeof (low[0])));
    copyTier (d, GrooveTier::Medium, medium, (int) (sizeof (medium) / sizeof (medium[0])));
    copyTier (d, GrooveTier::High, high, (int) (sizeof (high) / sizeof (high[0])));
    copyFill (d, FillKind::Short, shortFills,
              (int) (sizeof (shortFills) / sizeof (shortFills[0])));
    copyFill (d, FillKind::Long, longFills, (int) (sizeof (longFills) / sizeof (longFills[0])));
    copyFill (d, FillKind::Transition, transitionFills,
              (int) (sizeof (transitionFills) / sizeof (transitionFills[0])));
    return d;
}
StyleDescriptor buildHardRockMetal()
{
    StyleDescriptor d {};
    d.id = StyleId::HardRockMetal;
    d.name = "Hard Rock / Metal";
    d.shortName = "hard-rock-metal";
    d.minBpm = 60;
    d.idealBpm = 140;
    d.maxBpm = 250;
    d.meters[0] = Meter { 4, 4 };
    d.meterCount = 1;
    d.defaultSwing01 = 0.0f;
    d.minSwing01 = 0.0f;
    d.maxSwing01 = 0.0f;
    d.minRepetitionDistanceBars = 4;

    static const PatternProvenance low[] = {
            { 43, "METAL", "Heavy half-time", false, 0x7C4C2E54u, 4, 4 },  // bpm 96 swing 0
            { 50, "METAL", "Doom", false, 0x6DDEFADAu, 4, 4 },  // bpm 60 swing 0
            { 497, "METAL", "Doom verse", false, 0x418AC338u, 4, 4 },  // bpm 70 swing 0
            { 501, "METAL", "Doom chorus", false, 0x9495ADABu, 4, 4 },  // bpm 75 swing 0
    };
    static const PatternProvenance medium[] = {
            { 46, "METAL", "Groove metal", false, 0x6892933Cu, 4, 4 },  // bpm 108 swing 0
            { 48, "METAL", "Djent", false, 0x36A56100u, 4, 4 },  // bpm 100 swing 0
            { 51, "METAL", "Metalcore", false, 0xBBD71E31u, 4, 4 },  // bpm 120 swing 0
            { 473, "METAL", "Groove metal ride", false, 0x20CE931Fu, 4, 4 },  // bpm 112 swing 0
            { 475, "METAL", "Djent chug", false, 0x0E2B8B59u, 4, 4 },  // bpm 104 swing 0
    };
    static const PatternProvenance high[] = {
            { 41, "METAL", "Double kick", false, 0x7A5678F1u, 4, 4 },  // bpm 150 swing 0
            { 42, "METAL", "Thrash", false, 0x679EA4B1u, 4, 4 },  // bpm 184 swing 0
            { 47, "METAL", "Double gallop", false, 0x6A0ECAB1u, 4, 4 },  // bpm 140 swing 0
            { 471, "METAL", "Thrash gallop", false, 0x285A1F74u, 4, 4 },  // bpm 176 swing 0
            { 494, "METAL", "Heavy verse", false, 0x6A667054u, 4, 4 },  // bpm 150 swing 0
    };
    static const PatternProvenance shortFills[] = {
            { 483, "METAL", "Double bass roll", true, 0x2AD4B842u, 4, 4 },  // bpm 0 swing 0
            { 486, "METAL", "Gallop fill", true, 0x89120C17u, 4, 4 },  // bpm 0 swing 0
            { 490, "METAL", "Herta metal", true, 0xBB7D6E5Fu, 4, 4 },  // bpm 0 swing 0
            { 202, "METAL", "Metal gallop", true, 0x1A80E201u, 4, 4 },  // bpm 0 swing 0
    };
    static const PatternProvenance longFills[] = {
            { 484, "METAL", "Toms + double kick", true, 0x3F18DABBu, 4, 4 },  // bpm 0 swing 0
            { 487, "METAL", "Gravity roll", true, 0xD0C4526Eu, 4, 4 },  // bpm 0 swing 0
            { 493, "METAL", "Double kick outro", true, 0x9EAB6C27u, 4, 4 },  // bpm 0 swing 0
            { 203, "METAL", "Double bass fill", true, 0xEF015C1Bu, 4, 4 },  // bpm 0 swing 0
    };
    static const PatternProvenance transitionFills[] = {
            { 485, "METAL", "Blast fill", true, 0x5B83740Du, 4, 4 },  // bpm 0 swing 0
            { 489, "METAL", "Crash + double bass", true, 0xC986AEA0u, 4, 4 },  // bpm 0 swing 0
            { 204, "METAL", "Final blast", true, 0x599BEA1Eu, 4, 4 },  // bpm 0 swing 0
    };

    copyTier (d, GrooveTier::Low, low, (int) (sizeof (low) / sizeof (low[0])));
    copyTier (d, GrooveTier::Medium, medium, (int) (sizeof (medium) / sizeof (medium[0])));
    copyTier (d, GrooveTier::High, high, (int) (sizeof (high) / sizeof (high[0])));
    copyFill (d, FillKind::Short, shortFills,
              (int) (sizeof (shortFills) / sizeof (shortFills[0])));
    copyFill (d, FillKind::Long, longFills, (int) (sizeof (longFills) / sizeof (longFills[0])));
    copyFill (d, FillKind::Transition, transitionFills,
              (int) (sizeof (transitionFills) / sizeof (transitionFills[0])));
    return d;
}
StyleDescriptor buildBlues()
{
    StyleDescriptor d {};
    d.id = StyleId::Blues;
    d.name = "Blues";
    d.shortName = "blues";
    d.minBpm = 55;
    d.idealBpm = 100;
    d.maxBpm = 150;
    d.meters[0] = Meter { 4, 4 };
    d.meterCount = 1;
    d.defaultSwing01 = 0.55f;
    d.minSwing01 = 0.3f;
    d.maxSwing01 = 0.66f;
    d.minRepetitionDistanceBars = 4;

    static const PatternProvenance low[] = {
            { 68, "BLUES", "Slow blues", false, 0x4EA4B17Du, 4, 4 },  // bpm 60 swing 60
            { 71, "BLUES", "Brushes", false, 0xE1696164u, 4, 4 },  // bpm 72 swing 40
            { 73, "BLUES", "12/8 slow", false, 0xB2814AC9u, 4, 4 },  // bpm 66 swing 60
    };
    static const PatternProvenance medium[] = {
            { 67, "BLUES", "Shuffle", false, 0x6A403D9Eu, 4, 4 },  // bpm 120 swing 55
            { 70, "BLUES", "Rumba blues", false, 0x87D8CF5Cu, 4, 4 },  // bpm 104 swing 30
            { 72, "BLUES", "Chicago", false, 0xEE5B44C3u, 4, 4 },  // bpm 96 swing 55
    };
    static const PatternProvenance high[] = {
            { 69, "BLUES", "Texas shuffle", false, 0xCC106873u, 4, 4 },  // bpm 126 swing 55
            { 74, "BLUES", "Boogie shuffle", false, 0x0B7D3E9Du, 4, 4 },  // bpm 132 swing 50
    };
    static const PatternProvenance shortFills[] = {
            { 196, "BLUES", "Shuffle fill", true, 0x99E34ED5u, 4, 4 },  // bpm 0 swing 50
            { 170, "GENERAL", "Press roll", true, 0x9007BD22u, 4, 4 },  // bpm 0 swing 0
            { 171, "GENERAL", "Snare flam", true, 0xB77E5D64u, 4, 4 },  // bpm 0 swing 0
    };
    static const PatternProvenance longFills[] = {
            { 169, "GENERAL", "Long roll", true, 0xA3BF63B9u, 4, 4 },  // bpm 0 swing 0
            { 173, "GENERAL", "Sextuplet", true, 0x54076B78u, 4, 4 },  // bpm 0 swing 40
    };
    static const PatternProvenance transitionFills[] = {
            { 196, "BLUES", "Shuffle fill", true, 0x99E34ED5u, 4, 4 },  // bpm 0 swing 50
            { 178, "GENERAL", "Snare and toms", true, 0xAF11D037u, 4, 4 },  // bpm 0 swing 0
    };

    copyTier (d, GrooveTier::Low, low, (int) (sizeof (low) / sizeof (low[0])));
    copyTier (d, GrooveTier::Medium, medium, (int) (sizeof (medium) / sizeof (medium[0])));
    copyTier (d, GrooveTier::High, high, (int) (sizeof (high) / sizeof (high[0])));
    copyFill (d, FillKind::Short, shortFills,
              (int) (sizeof (shortFills) / sizeof (shortFills[0])));
    copyFill (d, FillKind::Long, longFills, (int) (sizeof (longFills) / sizeof (longFills[0])));
    copyFill (d, FillKind::Transition, transitionFills,
              (int) (sizeof (transitionFills) / sizeof (transitionFills[0])));
    return d;
}
StyleDescriptor buildFunk()
{
    StyleDescriptor d {};
    d.id = StyleId::Funk;
    d.name = "Funk";
    d.shortName = "funk";
    d.minBpm = 70;
    d.idealBpm = 100;
    d.maxBpm = 160;
    d.meters[0] = Meter { 4, 4 };
    d.meterCount = 1;
    d.defaultSwing01 = 0.15f;
    d.minSwing01 = 0.0f;
    d.maxSwing01 = 0.6f;
    d.minRepetitionDistanceBars = 4;

    static const PatternProvenance low[] = {
            { 65, "FUNK", "Half-time funk", false, 0x5EA8CC60u, 4, 4 },  // bpm 88 swing 0
            { 55, "FUNK", "Ghost groove", false, 0xAF5C1CB6u, 4, 4 },  // bpm 92 swing 8
            { 260, "FUNK", "Groove 3", false, 0x25C39666u, 4, 4 },  // bpm 84 swing 0
            { 265, "FUNK", "Funk 2", false, 0x9485E023u, 4, 4 },  // bpm 84 swing 0
    };
    static const PatternProvenance medium[] = {
            { 54, "FUNK", "Funk 16", false, 0x8B9A3FC0u, 4, 4 },  // bpm 96 swing 0
            { 57, "FUNK", "Open hat", false, 0x70D8C188u, 4, 4 },  // bpm 102 swing 0
            { 59, "FUNK", "Boogaloo", false, 0x46955AFEu, 4, 4 },  // bpm 112 swing 15
            { 62, "FUNK", "Cold Sweat", false, 0xD72BFEEFu, 4, 4 },  // bpm 112 swing 0
            { 63, "FUNK", "Linear funk", false, 0x57C44B4Eu, 4, 4 },  // bpm 104 swing 0
            { 545, "FUNK", "Funk 16th ghost", false, 0xBA1F8C5Eu, 4, 4 },  // bpm 96 swing 0
    };
    static const PatternProvenance high[] = {
            { 56, "FUNK", "The One (JB)", false, 0x02B198D5u, 4, 4 },  // bpm 108 swing 0
            { 60, "FUNK", "Funk rock", false, 0x48030B2Cu, 4, 4 },  // bpm 106 swing 0
            { 66, "FUNK", "P-Funk", false, 0x0FA805C3u, 4, 4 },  // bpm 100 swing 0
            { 263, "FUNK", "Fast 1", false, 0x6ABFB561u, 4, 4 },  // bpm 125 swing 0
            { 267, "FUNK", "Chacha", false, 0xC1D58FA1u, 4, 4 },  // bpm 124 swing 0
    };
    static const PatternProvenance shortFills[] = {
            { 187, "FUNK", "Linear funk", true, 0x562C9046u, 4, 4 },  // bpm 0 swing 0
            { 194, "FUNK", "Funk chop", true, 0x49750EACu, 4, 4 },  // bpm 0 swing 0
            { 549, "FUNK", "Fill funk 16th", true, 0x42DE0943u, 4, 4 },  // bpm 0 swing 0
            { 190, "GENERAL", "Kick and snare", true, 0x8CAFF3A8u, 4, 4 },  // bpm 0 swing 0
    };
    static const PatternProvenance longFills[] = {
            { 198, "FUNK", "Second line fill", true, 0xD9D02353u, 4, 4 },  // bpm 0 swing 20
            { 207, "FUNK", "Purdie fill", true, 0x0B3171B5u, 4, 4 },  // bpm 0 swing 55
            { 191, "GENERAL", "Alternating", true, 0x15C9520Fu, 4, 4 },  // bpm 0 swing 0
    };
    static const PatternProvenance transitionFills[] = {
            { 195, "GENERAL", "Crescendo", true, 0x58E2C105u, 4, 4 },  // bpm 0 swing 0
            { 198, "FUNK", "Second line fill", true, 0xD9D02353u, 4, 4 },  // bpm 0 swing 20
    };

    copyTier (d, GrooveTier::Low, low, (int) (sizeof (low) / sizeof (low[0])));
    copyTier (d, GrooveTier::Medium, medium, (int) (sizeof (medium) / sizeof (medium[0])));
    copyTier (d, GrooveTier::High, high, (int) (sizeof (high) / sizeof (high[0])));
    copyFill (d, FillKind::Short, shortFills,
              (int) (sizeof (shortFills) / sizeof (shortFills[0])));
    copyFill (d, FillKind::Long, longFills, (int) (sizeof (longFills) / sizeof (longFills[0])));
    copyFill (d, FillKind::Transition, transitionFills,
              (int) (sizeof (transitionFills) / sizeof (transitionFills[0])));
    return d;
}
StyleDescriptor buildPop()
{
    StyleDescriptor d {};
    d.id = StyleId::Pop;
    d.name = "Pop";
    d.shortName = "pop";
    d.minBpm = 60;
    d.idealBpm = 115;
    d.maxBpm = 170;
    d.meters[0] = Meter { 4, 4 };
    d.meterCount = 1;
    d.defaultSwing01 = 0.0f;
    d.minSwing01 = 0.0f;
    d.maxSwing01 = 0.0f;
    d.minRepetitionDistanceBars = 4;

    static const PatternProvenance low[] = {
            { 23, "POP", "Pop ballad", false, 0xE70D9E72u, 4, 4 },  // bpm 68 swing 0
            { 31, "POP", "Ballad 16", false, 0x1B1AB2E6u, 4, 4 },  // bpm 72 swing 0
            { 338, "POP", "Soft 1", false, 0x9CE4244Cu, 4, 4 },  // bpm 83 swing 0
    };
    static const PatternProvenance medium[] = {
            { 20, "POP", "Pop eighths", false, 0x6FFD1EABu, 4, 4 },  // bpm 100 swing 0
            { 26, "POP", "Syncopated pop", false, 0x5D8B58C3u, 4, 4 },  // bpm 104 swing 0
            { 27, "POP", "Pop rock", false, 0xB771340Eu, 4, 4 },  // bpm 116 swing 0
            { 29, "POP", "Handclap", false, 0xB0D33E99u, 4, 4 },  // bpm 108 swing 0
            { 30, "POP", "Modern (kick 7)", false, 0x61CC8FE3u, 4, 4 },  // bpm 102 swing 0
    };
    static const PatternProvenance high[] = {
            { 22, "POP", "Dance-pop 16", false, 0x0F4BC39Eu, 4, 4 },  // bpm 122 swing 0
            { 28, "POP", "Electro pop", false, 0xCD40B5AEu, 4, 4 },  // bpm 120 swing 0
            { 32, "POP", "Four-floor pop", false, 0xEB001433u, 4, 4 },  // bpm 124 swing 0
            { 540, "POP", "Four-on-the-floor", false, 0x585B2526u, 4, 4 },  // bpm 118 swing 0
            { 541, "POP", "Dance pop clap", false, 0x092B3821u, 4, 4 },  // bpm 120 swing 0
    };
    static const PatternProvenance shortFills[] = {
            { 544, "POP", "Simple pop fill", true, 0x1F737078u, 4, 4 },  // bpm 0 swing 0
            { 348, "POP", "Pop 6", true, 0x4450C7F6u, 4, 4 },  // bpm 142 swing 0
            { 350, "POP", "Pop 7", true, 0xEF680669u, 4, 4 },  // bpm 142 swing 0
    };
    static const PatternProvenance longFills[] = {
            { 349, "POP", "Soft 4", true, 0x5D5D06F3u, 4, 4 },  // bpm 83 swing 0
            { 351, "POP", "Soft 5", true, 0x5E229B7Du, 4, 4 },  // bpm 83 swing 0
    };
    static const PatternProvenance transitionFills[] = {
            { 544, "POP", "Simple pop fill", true, 0x1F737078u, 4, 4 },  // bpm 0 swing 0
            { 195, "GENERAL", "Crescendo", true, 0x58E2C105u, 4, 4 },  // bpm 0 swing 0
    };

    copyTier (d, GrooveTier::Low, low, (int) (sizeof (low) / sizeof (low[0])));
    copyTier (d, GrooveTier::Medium, medium, (int) (sizeof (medium) / sizeof (medium[0])));
    copyTier (d, GrooveTier::High, high, (int) (sizeof (high) / sizeof (high[0])));
    copyFill (d, FillKind::Short, shortFills,
              (int) (sizeof (shortFills) / sizeof (shortFills[0])));
    copyFill (d, FillKind::Long, longFills, (int) (sizeof (longFills) / sizeof (longFills[0])));
    copyFill (d, FillKind::Transition, transitionFills,
              (int) (sizeof (transitionFills) / sizeof (transitionFills[0])));
    return d;
}
StyleDescriptor buildShuffle()
{
    StyleDescriptor d {};
    d.id = StyleId::Shuffle;
    d.name = "Shuffle";
    d.shortName = "shuffle";
    d.minBpm = 55;
    d.idealBpm = 100;
    d.maxBpm = 150;
    d.meters[0] = Meter { 4, 4 };
    d.meterCount = 1;
    d.defaultSwing01 = 0.55f;
    d.minSwing01 = 0.25f;
    d.maxSwing01 = 0.66f;
    d.minRepetitionDistanceBars = 4;

    static const PatternProvenance low[] = {
            { 68, "BLUES", "Slow blues", false, 0x4EA4B17Du, 4, 4 },  // bpm 60 swing 60
            { 71, "BLUES", "Brushes", false, 0xE1696164u, 4, 4 },  // bpm 72 swing 40
            { 546, "FUNK", "Purdie shuffle", false, 0xA9EF26ADu, 4, 4 },  // bpm 98 swing 50
    };
    static const PatternProvenance medium[] = {
            { 67, "BLUES", "Shuffle", false, 0x6A403D9Eu, 4, 4 },  // bpm 120 swing 55
            { 61, "FUNK", "Purdie shuffle", false, 0x10DC0C86u, 4, 4 },  // bpm 100 swing 55
            { 9, "ROCK", "Shuffle rock", false, 0xCA6D59DCu, 4, 4 },  // bpm 132 swing 45
    };
    static const PatternProvenance high[] = {
            { 69, "BLUES", "Texas shuffle", false, 0xCC106873u, 4, 4 },  // bpm 126 swing 55
            { 74, "BLUES", "Boogie shuffle", false, 0x0B7D3E9Du, 4, 4 },  // bpm 132 swing 50
            { 535, "ROCK", "Shuffle rock", false, 0x057D304Fu, 4, 4 },  // bpm 120 swing 33
    };
    static const PatternProvenance shortFills[] = {
            { 196, "BLUES", "Shuffle fill", true, 0x99E34ED5u, 4, 4 },  // bpm 0 swing 50
            { 207, "FUNK", "Purdie fill", true, 0x0B3171B5u, 4, 4 },  // bpm 0 swing 55
            { 172, "GENERAL", "Herta", true, 0xA8E96D4Fu, 4, 4 },  // bpm 0 swing 0
    };
    static const PatternProvenance longFills[] = {
            { 198, "FUNK", "Second line fill", true, 0xD9D02353u, 4, 4 },  // bpm 0 swing 20
            { 169, "GENERAL", "Long roll", true, 0xA3BF63B9u, 4, 4 },  // bpm 0 swing 0
    };
    static const PatternProvenance transitionFills[] = {
            { 196, "BLUES", "Shuffle fill", true, 0x99E34ED5u, 4, 4 },  // bpm 0 swing 50
            { 195, "GENERAL", "Crescendo", true, 0x58E2C105u, 4, 4 },  // bpm 0 swing 0
    };

    copyTier (d, GrooveTier::Low, low, (int) (sizeof (low) / sizeof (low[0])));
    copyTier (d, GrooveTier::Medium, medium, (int) (sizeof (medium) / sizeof (medium[0])));
    copyTier (d, GrooveTier::High, high, (int) (sizeof (high) / sizeof (high[0])));
    copyFill (d, FillKind::Short, shortFills,
              (int) (sizeof (shortFills) / sizeof (shortFills[0])));
    copyFill (d, FillKind::Long, longFills, (int) (sizeof (longFills) / sizeof (longFills[0])));
    copyFill (d, FillKind::Transition, transitionFills,
              (int) (sizeof (transitionFills) / sizeof (transitionFills[0])));
    return d;
}
} // namespace

std::uint32_t patternSpecHash (const char* spec) noexcept
{
    if (spec == nullptr)
        return 0;
    std::uint32_t h = 0x811c9dc5u;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*> (spec); *p != 0; ++p)
    {
        h ^= static_cast<std::uint32_t> (*p);
        h *= 0x01000193u;
    }
    return h;
}

const StyleDescriptor& StyleCatalog::styleAt (int index) noexcept
{
    static const StyleDescriptor styles[kStyleCount] = {
        buildRock(),
        buildHardRockMetal(),
        buildBlues(),
        buildFunk(),
        buildPop(),
        buildShuffle()
    };
    if (index < 0 || index >= kStyleCount)
        index = 0;
    return styles[index];
}

const StyleDescriptor& StyleCatalog::style (StyleId id) noexcept
{
    return styleAt (static_cast<int> (id));
}

const char* StyleCatalog::styleName (StyleId id) noexcept
{
    return style (id).name;
}

bool StyleCatalog::findStyleByName (const char* shortName, StyleId& out) noexcept
{
    if (shortName == nullptr)
        return false;
    for (int i = 0; i < kStyleCount; ++i)
    {
        if (std::strcmp (styleAt (i).shortName, shortName) == 0)
        {
            out = static_cast<StyleId> (i);
            return true;
        }
    }
    return false;
}

const char* StyleCatalog::libraryFingerprint() noexcept
{
    return "68008b4b139750e337a6166f6881e5724eee577b196efd8f467bf9562153d636";
}

CatalogValidation StyleCatalog::validate (const ILibraryProbe& probe) noexcept
{
    CatalogValidation result {};
    result.ok = true;

    auto recordIssue = [&result] (StyleId styleId, CatalogRefKind kind, int tierOrFillKind,
                                  int slot, LibraryIndex index, const char* message) noexcept
    {
        result.ok = false;
        if (result.issues < kMaxCatalogIssues)
            result.firstIssues[result.issues] = CatalogIssue { styleId, kind, tierOrFillKind,
                                                                slot, index, message };
        ++result.issues;
    };

    for (int si = 0; si < kStyleCount; ++si)
    {
        const StyleDescriptor& d = styleAt (si);

        for (int tier = 0; tier < kGrooveTierCount; ++tier)
        {
            if (d.grooveCount[tier] <= 0)
                recordIssue (d.id, CatalogRefKind::groove, tier, -1, kNoLibraryEntry,
                             "empty groove tier");
            for (int slot = 0; slot < d.grooveCount[tier]; ++slot)
            {
                const PatternProvenance& p = d.grooves[tier][slot];
                ++result.checked;
                LibraryEntry e {};
                if (!probe.lookup (p.index, e) || !e.valid)
                {
                    recordIssue (d.id, CatalogRefKind::groove, tier, slot, p.index,
                                 "index not present in library");
                    continue;
                }
                if (e.fill)
                    recordIssue (d.id, CatalogRefKind::groove, tier, slot, p.index,
                                 "referenced as a groove but library marks it a fill");
                if (std::strcmp (e.genre, p.genre) != 0)
                    recordIssue (d.id, CatalogRefKind::groove, tier, slot, p.index,
                                 "genre mismatch");
                if (std::strcmp (e.name, p.name) != 0)
                    recordIssue (d.id, CatalogRefKind::groove, tier, slot, p.index,
                                 "name mismatch");
                if (e.meter != p.meter)
                    recordIssue (d.id, CatalogRefKind::groove, tier, slot, p.index,
                                 "meter mismatch");
                if (patternSpecHash (e.spec) != p.specHash)
                    recordIssue (d.id, CatalogRefKind::groove, tier, slot, p.index,
                                 "spec hash mismatch");
                bool supported = false;
                for (int mi = 0; mi < d.meterCount; ++mi)
                    supported = supported || d.meters[mi] == p.meter;
                if (!supported)
                    recordIssue (d.id, CatalogRefKind::groove, tier, slot, p.index,
                                 "meter not supported by style");
            }
        }

        for (int kind = 0; kind < kFillKindCount; ++kind)
        {
            if (d.fillCount[kind] <= 0)
                recordIssue (d.id, CatalogRefKind::fill, kind, -1, kNoLibraryEntry,
                             "empty fill role");
            for (int slot = 0; slot < d.fillCount[kind]; ++slot)
            {
                const PatternProvenance& p = d.fills[kind][slot];
                ++result.checked;
                LibraryEntry e {};
                if (!probe.lookup (p.index, e) || !e.valid)
                {
                    recordIssue (d.id, CatalogRefKind::fill, kind, slot, p.index,
                                 "index not present in library");
                    continue;
                }
                if (! e.fill)
                    recordIssue (d.id, CatalogRefKind::fill, kind, slot, p.index,
                                 "referenced as a fill but library marks it a groove");
                if (std::strcmp (e.genre, p.genre) != 0)
                    recordIssue (d.id, CatalogRefKind::fill, kind, slot, p.index,
                                 "genre mismatch");
                if (std::strcmp (e.name, p.name) != 0)
                    recordIssue (d.id, CatalogRefKind::fill, kind, slot, p.index,
                                 "name mismatch");
                if (e.meter != p.meter)
                    recordIssue (d.id, CatalogRefKind::fill, kind, slot, p.index,
                                 "meter mismatch");
                if (patternSpecHash (e.spec) != p.specHash)
                    recordIssue (d.id, CatalogRefKind::fill, kind, slot, p.index,
                                 "spec hash mismatch");
                bool supported = false;
                for (int mi = 0; mi < d.meterCount; ++mi)
                    supported = supported || d.meters[mi] == p.meter;
                if (!supported)
                    recordIssue (d.id, CatalogRefKind::fill, kind, slot, p.index,
                                 "meter not supported by style");
            }
        }
    }

    return result;
}

} // namespace jam
