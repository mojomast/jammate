#pragma once

#include <cstddef>

namespace drum
{

// Bound for this engine's MIDI generation, before handing the buffer to a guest.
// DrumEngine::prepare clamps the rate to >=8000 Hz; stepLenSamples clamps tempo
// to <=260 BPM and swing to <=60%. The shortest step is therefore >=212.30
// samples. Allowing the scheduler's 0.5-sample due tolerance still leaves >211
// samples between steps, even if the UI changes tempo/swing during the block.
// Two extra steps cover an already-due step and endpoint rounding. Timeline and
// audition never run together; their transition can also emit 9 stop note-offs.
// The 64 pending slots can each emit at most one note-off per block.
// JUCE stores each three-byte note message with a 4-byte time and 2-byte length.
// This sizes storage on the prepare path; it does not call ensureSize in audio.
inline std::size_t midiScratchBytesForBlock (int maxBlockSize) noexcept
{
    const auto samples = static_cast<std::size_t> (maxBlockSize > 0 ? maxBlockSize : 0);
    const auto steps = samples / 211u + 2u;
    return (steps * 9u + 64u + 9u) * 9u;
}

} // namespace drum
