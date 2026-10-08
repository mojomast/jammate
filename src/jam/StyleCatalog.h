// StyleCatalog — a bounded, JUCE-free overlay of the compiled drum library.
//
// DEVPLAN STYLE-001 / SPEC.md sections 13.1-13.3.
//
// WHAT THIS IS
//   The product ships a flat, single-bar groove/fill library in
//   `src/DrumLibrary.cpp`: 570 anonymous entries grouped by genre. That library
//   is a warehouse, not a musical model. A style is the missing layer: it says
//   which of those entries are low/medium/high grooves and which are short,
//   long or transition fills, over which meter, BPM band and swing range, with
//   humanization and anti-repetition defaults. The Jam Director selects through
//   this layer instead of guessing from genre/name.
//
// WHY REFERENCES ARE (INDEX + FULL PROVENANCE), NOT NAMES
//   `src/DrumLibrary.cpp` has no string identifiers. Entries are identified by
//   `genre` + `name`, and that pair is NOT unique: 16 pairs collide and 27
//   names are duplicated across the library. `("FUNK","Linear funk")` is a
//   *groove* at index 63 and a *fill* at index 187; `("ROCK","Shuffle rock")`
//   appears at index 9 (swing 45) and index 535 (swing 33) as different
//   patterns. A name-derived id would resolve some of these to the wrong
//   pattern or the wrong kind, which is the worst failure mode available.
//   Positional indices are unique by construction and are already the key the
//   shipping UI uses (see `IDrumTransport.h`). Each reference therefore carries
//   the full actual-library provenance — genre, name, fill flag, spec hash and
//   meter — so a library change is detected rather than silently played.
//
// VALIDATION
//   `StyleCatalog::validate(probe)` checks EVERY shipped reference against a
//   caller-supplied `ILibraryProbe`. In the JUCE-free jam-core tests the probe
//   is fed a generated snapshot of the actual compiled library
//   (`tools/style-catalog/`), so refs are checked against real library data, not
//   against names. `libraryFingerprint()` records the sha256 of
//   `src/DrumLibrary.cpp` at freeze time; `tools/style-catalog/ --check`
//   recomputes it, so any library edit fails the regression instead of drifting.
//
// HEADER HYGIENE (SPEC.md 7.1 / 8.1): no JUCE, no STL containers, no
// std::string, no allocation. Only `<cstdint>`/`<cstddef>` and the frozen
// `IDrumTransport.h` identity type.

#pragma once

#include <cstddef>
#include <cstdint>

#include "IDrumTransport.h"

namespace jam
{

/** The six styles the adaptive wave must ship (SPEC.md 13.1, ADAPTIVE-WAVE-
 *  CONTRACT.md). Order is part of the external surface: UI and persistence use
 *  the enum value, never the display name. */
enum class StyleId : int
{
    Rock = 0,
    HardRockMetal,
    Blues,
    Funk,
    Pop,
    Shuffle
};

inline constexpr int kStyleCount = 6;

inline const char* toString (StyleId id) noexcept
{
    switch (id)
    {
        case StyleId::Rock:          return "Rock";
        case StyleId::HardRockMetal: return "Hard Rock / Metal";
        case StyleId::Blues:         return "Blues";
        case StyleId::Funk:          return "Funk";
        case StyleId::Pop:           return "Pop";
        case StyleId::Shuffle:       return "Shuffle";
    }
    return "Unknown";
}

/** Three intensity/complexity tiers. SPEC.md 13.1. */
enum class GrooveTier : int
{
    Low = 0,
    Medium,
    High
};

inline constexpr int kGrooveTierCount = 3;

inline const char* toString (GrooveTier tier) noexcept
{
    switch (tier)
    {
        case GrooveTier::Low:    return "low";
        case GrooveTier::Medium: return "medium";
        case GrooveTier::High:   return "high";
    }
    return "unknown";
}

/** Fill roles. The compiled library only carries a boolean `fill` flag, so the
 *  role is a style overlay decision rather than library metadata. */
enum class FillKind : int
{
    Short = 0,
    Long,
    Transition
};

inline constexpr int kFillKindCount = 3;

/** Compile-time capacity of a tier list. Structural, not a tunable: the arrays
 *  are inline, so nothing allocates after construction. */
inline constexpr int kMaxTierPatterns = 8;

/** Compile-time capacity of a fill-role list. */
inline constexpr int kMaxFillPatterns = 6;

/** Maximum supported meters per style. */
inline constexpr int kMaxStyleMeters = 4;

struct Meter
{
    int numerator = 4;
    int denominator = 4;

    friend bool operator== (const Meter& a, const Meter& b) noexcept
    {
        return a.numerator == b.numerator && a.denominator == b.denominator;
    }
    friend bool operator!= (const Meter& a, const Meter& b) noexcept { return ! (a == b); }
};

/** Provenance of one compiled-library reference. Every field is checked against
 *  the actual library by `StyleCatalog::validate`. */
struct PatternProvenance
{
    LibraryIndex index = kNoLibraryEntry;
    const char* genre = "";   // actual `Groove::genre`
    const char* name = "";    // actual `Groove::name` (UTF-8)
    bool fill = false;        // actual `Groove::fill`
    std::uint32_t specHash = 0; // FNV-1a 32 of actual `Groove::spec`
    Meter meter {};           // actual meter (defaults to 4/4 when unspecified)
};

/** One style overlay. All arrays are bounded inline storage. */
struct StyleDescriptor
{
    StyleId id = StyleId::Rock;
    const char* name = "";       // display name, e.g. "Hard Rock / Metal"
    const char* shortName = "";  // persistence/UI token, e.g. "hard-rock-metal"

    // BPM band. ADVISORY ONLY: the Musical Clock is the sole tempo authority
    // (SPEC.md 10), so the Jam Director never clamps tempo to this band. It is
    // metadata for UI/tuning and for choosing a style, not a tempo control.
    int minBpm = 0;
    int idealBpm = 0;
    int maxBpm = 0;

    Meter meters[kMaxStyleMeters] {};
    int meterCount = 0;

    PatternProvenance grooves[kGrooveTierCount][kMaxTierPatterns] {};
    int grooveCount[kGrooveTierCount] {};

    PatternProvenance fills[kFillKindCount][kMaxFillPatterns] {};
    int fillCount[kFillKindCount] {};

    // Humanization defaults copied into every `QueuedBarChange`. SPEC.md 13.2.
    float humanizeVelocity = 0.25f;
    float humanizeTiming = 0.15f;
    float humanizeRoundRobin = 0.40f;

    // Swing range and default (fraction of a step, 0..0.66). SPEC.md 13.1. The
    // director emits `defaultSwing01`; min/max are ADVISORY hints for UI/tuning,
    // not a runtime clamp.
    float defaultSwing01 = 0.0f;
    float minSwing01 = 0.0f;
    float maxSwing01 = 0.0f;

    // Bars before a committed groove may be selected again. CONSUMED by the Jam
    // Director as the effective anti-repetition window (DirectorConfig's
    // minimumRepetitionDistance is the fallback when this is <= 0). SPEC.md 13.3.
    int minRepetitionDistanceBars = 4;

    bool supportsMeter (const Meter& m) const noexcept
    {
        for (int i = 0; i < meterCount; ++i)
            if (meters[i] == m)
                return true;
        return false;
    }
};

/** A real compiled-library entry handed to `StyleCatalog::validate`. */
struct LibraryEntry
{
    bool valid = false;
    LibraryIndex index = kNoLibraryEntry;
    const char* genre = "";
    const char* name = "";
    bool fill = false;
    const char* spec = "";
    Meter meter {};
};

/** Read-only view of the actual compiled library. The test/provenance layer
 *  implements this; `src/jam` never links `DrumLibrary.cpp` (it needs JUCE). */
class ILibraryProbe
{
public:
    virtual ~ILibraryProbe() = default;
    virtual int size() const noexcept = 0;
    virtual bool lookup (LibraryIndex index, LibraryEntry& out) const noexcept = 0;
};

enum class CatalogRefKind : int { groove = 0, fill = 1 };

inline constexpr int kMaxCatalogIssues = 24;

struct CatalogIssue
{
    StyleId style = StyleId::Rock;
    CatalogRefKind kind = CatalogRefKind::groove;
    int tierOrFillKind = -1;
    int slot = -1;
    LibraryIndex index = kNoLibraryEntry;
    const char* message = "";
};

struct CatalogValidation
{
    bool ok = true;
    int checked = 0;
    int issues = 0;
    CatalogIssue firstIssues[kMaxCatalogIssues] {};
};

/** FNV-1a 32-bit hash of a compiled pattern spec, with 32-bit wraparound. The
 *  same routine is implemented by `tools/style-catalog/`, so the C++ catalog
 *  and the provenance tooling agree bit-for-bit. */
std::uint32_t patternSpecHash (const char* spec) noexcept;

class StyleCatalog
{
public:
    StyleCatalog() = delete;

    static int count() noexcept { return kStyleCount; }

    /** Bounds-checked style access. Out-of-range returns style 0 (Rock) rather
     *  than dereferencing an invalid index. */
    static const StyleDescriptor& style (StyleId id) noexcept;
    static const StyleDescriptor& styleAt (int index) noexcept;

    static const char* styleName (StyleId id) noexcept;
    static bool findStyleByName (const char* shortName, StyleId& out) noexcept;

    /** Checks EVERY shipped reference against `probe`. Returns the count of
     *  checks and the first `kMaxCatalogIssues` problems. */
    static CatalogValidation validate (const ILibraryProbe& probe) noexcept;

    /** sha256 of `src/DrumLibrary.cpp` at the commit that froze these refs. */
    static const char* libraryFingerprint() noexcept;
};

} // namespace jam
