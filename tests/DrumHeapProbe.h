// Shared callback-allocation probe for the drum test binaries (INT-DRUM-001).
//
// The ELF linker wraps malloc/realloc/free and routes C++ new/delete through
// them, so a test can measure the C heap touched by the statically linked
// JUCE/engine objects during one audio callback. This must be a SINGLE
// definition across the whole test binary: both the existing DrumMidiTests and
// the new DrumClockBridgeTests measure with it, and the combined root target
// links both. Defining the wrappers per test file would be a duplicate symbol.
//
// The measurement is meaningful only with a no-op guest plugin; it makes no
// claim about arbitrary third-party VST3s.
#pragma once

#include <cstddef>

namespace drumprobe
{
#if defined(DRUM_MIDI_HEAP_PROBE)
/** Zero the counters and start counting. */
void beginMeasure() noexcept;
/** Stop counting (counters keep their values until the next beginMeasure). */
void endMeasure() noexcept;
std::size_t allocations() noexcept;
std::size_t deallocations() noexcept;
#endif
} // namespace drumprobe
