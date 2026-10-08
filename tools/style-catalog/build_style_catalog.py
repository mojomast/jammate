#!/usr/bin/env python3
"""Author-time generator for src/jam/StyleCatalog.cpp.

One-shot authoring tool; the shipped file is normal C++ and is verified by
style_catalog_provenance.py against the live library.
"""
import re, hashlib, os

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(REPO, "src", "DrumLibrary.cpp")
OUT = os.path.join(REPO, "src", "jam", "StyleCatalog.cpp")

src = open(SRC, encoding="utf-8").read()
start = src.index("static const std::vector<Groove> lib = {")
start = src.index("{", start) + 1
end = src.index("\n    };", start)
body = src[start:end]
pat = re.compile(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,'
                 r'\s*(\d+)\s*,\s*(\d+)\s*,\s*(true|false)\s*,\s*'
                 r'"((?:[^"\\]|\\.)*)"\s*(?:,\s*(\d+)\s*,\s*(\d+)\s*)?\}')
E = {}
for i, m in enumerate(pat.finditer(body)):
    g, n, b, s, f, spec, num, den = m.groups()
    E[i] = dict(i=i, g=g, n=n, b=int(b), s=int(s), f=(f == "true"),
                spec=spec, num=int(num or 4), den=int(den or 4))

def fnv32(s):
    h = 0x811c9dc5
    for c in s.encode("utf-8"):
        h ^= c
        h = (h * 0x01000193) & 0xffffffff
    return h

def ref(i):
    e = E[i]
    fill = "true" if e["f"] else "false"
    return (f'            {{ {i}, "{e["g"]}", "{e["n"]}", {fill}, '
            f'0x{fnv32(e["spec"]):08X}u, {e["num"]}, {e["den"]} }},'
            f'  // bpm {e["b"]} swing {e["s"]}')

def block(indices):
    return "\n".join(ref(i) for i in indices)

styles = [
    dict(var="Rock", ident="StyleId::Rock", name="Rock", token="rock",
         minB=70, ideal=110, maxB=175, swing=0.0, smin=0.0, smax=0.0,
         low=[2, 3, 8], med=[0, 1, 5, 13, 17, 18], high=[6, 11, 14, 16, 19],
         sho=[412, 415, 168, 165], lng=[413, 416, 169], trans=[539, 195]),
    dict(var="HardRockMetal", ident="StyleId::HardRockMetal", name="Hard Rock / Metal",
         token="hard-rock-metal", minB=60, ideal=140, maxB=250,
         swing=0.0, smin=0.0, smax=0.0,
         low=[43, 50, 497, 501], med=[46, 48, 51, 473, 475],
         high=[41, 42, 47, 471, 494],
         sho=[483, 486, 490, 202], lng=[484, 487, 493, 203], trans=[485, 489, 204]),
    dict(var="Blues", ident="StyleId::Blues", name="Blues", token="blues",
         minB=55, ideal=100, maxB=150, swing=0.55, smin=0.30, smax=0.66,
         low=[68, 71, 73], med=[67, 70, 72], high=[69, 74],
         sho=[196, 170, 171], lng=[169, 173], trans=[196, 178]),
    dict(var="Funk", ident="StyleId::Funk", name="Funk", token="funk",
         minB=70, ideal=100, maxB=160, swing=0.15, smin=0.0, smax=0.60,
         low=[65, 55, 260, 265], med=[54, 57, 59, 62, 63, 545],
         high=[56, 60, 66, 263, 267],
         sho=[187, 194, 549, 190], lng=[198, 207, 191], trans=[195, 198]),
    dict(var="Pop", ident="StyleId::Pop", name="Pop", token="pop",
         minB=60, ideal=115, maxB=170, swing=0.0, smin=0.0, smax=0.0,
         low=[23, 31, 338], med=[20, 26, 27, 29, 30],
         high=[22, 28, 32, 540, 541],
         sho=[544, 348, 350], lng=[349, 351], trans=[544, 195]),
    dict(var="Shuffle", ident="StyleId::Shuffle", name="Shuffle", token="shuffle",
         minB=55, ideal=100, maxB=150, swing=0.55, smin=0.25, smax=0.66,
         low=[68, 71, 546], med=[67, 61, 9], high=[69, 74, 535],
         sho=[196, 207, 172], lng=[198, 169], trans=[196, 195]),
]

for st in styles:
    for tier, key in (("Low", "low"), ("Medium", "med"), ("High", "high")):
        for i in st[key]:
            assert not E[i]["f"], f"{st['var']}/{tier} {i} is a fill"
    for kind, key in (("Short", "sho"), ("Long", "lng"), ("Transition", "trans")):
        for i in st[key]:
            assert E[i]["f"], f"{st['var']}/{kind} fill {i} is a groove"
    for key in ("low", "med", "high", "sho", "lng", "trans"):
        assert len(st[key]) <= (8 if key in ("low", "med", "high") else 6)

builders = []
for st in styles:
    builders.append(f'''StyleDescriptor build{st['var']}()
{{
    StyleDescriptor d {{}};
    d.id = {st['ident']};
    d.name = "{st['name']}";
    d.shortName = "{st['token']}";
    d.minBpm = {st['minB']};
    d.idealBpm = {st['ideal']};
    d.maxBpm = {st['maxB']};
    d.meters[0] = Meter {{ 4, 4 }};
    d.meterCount = 1;
    d.defaultSwing01 = {st['swing']}f;
    d.minSwing01 = {st['smin']}f;
    d.maxSwing01 = {st['smax']}f;
    d.minRepetitionDistanceBars = 4;

    static const PatternProvenance low[] = {{
{block(st['low'])}
    }};
    static const PatternProvenance medium[] = {{
{block(st['med'])}
    }};
    static const PatternProvenance high[] = {{
{block(st['high'])}
    }};
    static const PatternProvenance shortFills[] = {{
{block(st['sho'])}
    }};
    static const PatternProvenance longFills[] = {{
{block(st['lng'])}
    }};
    static const PatternProvenance transitionFills[] = {{
{block(st['trans'])}
    }};

    copyTier (d, GrooveTier::Low, low, (int) (sizeof (low) / sizeof (low[0])));
    copyTier (d, GrooveTier::Medium, medium, (int) (sizeof (medium) / sizeof (medium[0])));
    copyTier (d, GrooveTier::High, high, (int) (sizeof (high) / sizeof (high[0])));
    copyFill (d, FillKind::Short, shortFills,
              (int) (sizeof (shortFills) / sizeof (shortFills[0])));
    copyFill (d, FillKind::Long, longFills, (int) (sizeof (longFills) / sizeof (longFills[0])));
    copyFill (d, FillKind::Transition, transitionFills,
              (int) (sizeof (transitionFills) / sizeof (transitionFills[0])));
    return d;
}}''')

sha = hashlib.sha256(src.encode("utf-8")).hexdigest()
header = f'''// StyleCatalog — the six shipped style overlays over the compiled drum library.
//
// REVIEW-GENERATED. The descriptor tables below were produced from the measured
// `src/DrumLibrary.cpp` at sha256
//   {sha}
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
{{

namespace
{{
void copyTier (StyleDescriptor& d, GrooveTier tier, const PatternProvenance* refs, int n) noexcept
{{
    const int c = n < kMaxTierPatterns ? n : kMaxTierPatterns;
    for (int i = 0; i < c; ++i)
        d.grooves[(int) tier][i] = refs[i];
    d.grooveCount[(int) tier] = c;
}}

void copyFill (StyleDescriptor& d, FillKind kind, const PatternProvenance* refs, int n) noexcept
{{
    const int c = n < kMaxFillPatterns ? n : kMaxFillPatterns;
    for (int i = 0; i < c; ++i)
        d.fills[(int) kind][i] = refs[i];
    d.fillCount[(int) kind] = c;
}}

{chr(10).join(builders)}
}} // namespace

std::uint32_t patternSpecHash (const char* spec) noexcept
{{
    if (spec == nullptr)
        return 0;
    std::uint32_t h = 0x811c9dc5u;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*> (spec); *p != 0; ++p)
    {{
        h ^= static_cast<std::uint32_t> (*p);
        h *= 0x01000193u;
    }}
    return h;
}}

const StyleDescriptor& StyleCatalog::styleAt (int index) noexcept
{{
    static const StyleDescriptor styles[kStyleCount] = {{
        buildRock(),
        buildHardRockMetal(),
        buildBlues(),
        buildFunk(),
        buildPop(),
        buildShuffle()
    }};
    if (index < 0 || index >= kStyleCount)
        index = 0;
    return styles[index];
}}

const StyleDescriptor& StyleCatalog::style (StyleId id) noexcept
{{
    return styleAt (static_cast<int> (id));
}}

const char* StyleCatalog::styleName (StyleId id) noexcept
{{
    return style (id).name;
}}

bool StyleCatalog::findStyleByName (const char* shortName, StyleId& out) noexcept
{{
    if (shortName == nullptr)
        return false;
    for (int i = 0; i < kStyleCount; ++i)
    {{
        if (std::strcmp (styleAt (i).shortName, shortName) == 0)
        {{
            out = static_cast<StyleId> (i);
            return true;
        }}
    }}
    return false;
}}

const char* StyleCatalog::libraryFingerprint() noexcept
{{
    return "{sha}";
}}

CatalogValidation StyleCatalog::validate (const ILibraryProbe& probe) noexcept
{{
    CatalogValidation result {{}};
    result.ok = true;

    auto recordIssue = [&result] (StyleId styleId, CatalogRefKind kind, int tierOrFillKind,
                                  int slot, LibraryIndex index, const char* message) noexcept
    {{
        result.ok = false;
        if (result.issues < kMaxCatalogIssues)
            result.firstIssues[result.issues] = CatalogIssue {{ styleId, kind, tierOrFillKind,
                                                                slot, index, message }};
        ++result.issues;
    }};

    for (int si = 0; si < kStyleCount; ++si)
    {{
        const StyleDescriptor& d = styleAt (si);

        for (int tier = 0; tier < kGrooveTierCount; ++tier)
        {{
            if (d.grooveCount[tier] <= 0)
                recordIssue (d.id, CatalogRefKind::groove, tier, -1, kNoLibraryEntry,
                             "empty groove tier");
            for (int slot = 0; slot < d.grooveCount[tier]; ++slot)
            {{
                const PatternProvenance& p = d.grooves[tier][slot];
                ++result.checked;
                LibraryEntry e {{}};
                if (!probe.lookup (p.index, e) || !e.valid)
                {{
                    recordIssue (d.id, CatalogRefKind::groove, tier, slot, p.index,
                                 "index not present in library");
                    continue;
                }}
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
            }}
        }}

        for (int kind = 0; kind < kFillKindCount; ++kind)
        {{
            if (d.fillCount[kind] <= 0)
                recordIssue (d.id, CatalogRefKind::fill, kind, -1, kNoLibraryEntry,
                             "empty fill role");
            for (int slot = 0; slot < d.fillCount[kind]; ++slot)
            {{
                const PatternProvenance& p = d.fills[kind][slot];
                ++result.checked;
                LibraryEntry e {{}};
                if (!probe.lookup (p.index, e) || !e.valid)
                {{
                    recordIssue (d.id, CatalogRefKind::fill, kind, slot, p.index,
                                 "index not present in library");
                    continue;
                }}
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
            }}
        }}
    }}

    return result;
}}

}} // namespace jam
'''

with open(OUT, "w", encoding="utf-8") as f:
    f.write(header)

print("wrote", OUT, len(header), "bytes; refs:",
      sum(len(st[k]) for st in styles for k in ("low", "med", "high", "sho", "lng", "trans")))
print("sha256(src/DrumLibrary.cpp) =", sha)
