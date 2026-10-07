// jam-core identification and persistence schema surface.
//
// This translation unit exists for one reason that is not cosmetic: a static
// library with no translation units is not a buildable state, and the
// identification/schema constants below are genuinely needed by the modules
// that come next — STYLE-001 (style descriptor schemaVersion, SPEC.md 13.2) and
// PERSIST-001 (versioned session state, SPEC.md 23).
//
// Keeping them here means the version numbers are defined exactly once and
// that "current version" is queryable at runtime by the diagnostics view and by
// the evaluation harness when it writes a report header.
//
// Real-time rules: header-only, dependency-free, no allocation. Safe to include
// from anything, including test code.

#pragma once

namespace jam
{

/** Descriptor schema version written into the style descriptors under
    assets/styles. SPEC.md 13.2. A reader must reject a version it does not
    understand rather than guess. */
inline constexpr int kStyleSchemaVersion = 1;

/** Session/settings schema version written by the Jam persistence layer.
    SPEC.md 23: "Version all new persistent structures." */
inline constexpr int kSessionSchemaVersion = 1;

/** Human-readable core version, used in diagnostics and evaluation reports.
    Kept in sync with the product version by hand; it is identification only and
    must never be parsed for behaviour. */
const char* jamCoreVersion() noexcept;

} // namespace jam