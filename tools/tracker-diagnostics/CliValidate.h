// CliValidate — strict command-line integer validation (TRACK-004).
//
// A malformed or out-of-range block size must be rejected, not silently
// clamped to a different size than the run reports. These helpers are pure so
// the rejection rule is unit-testable without spawning the binary.

#pragma once

#include <cstddef>
#include <limits>
#include <string>

namespace tracker_diag
{

/** Parses an entire string as an unsigned decimal integer. "128x", "-1", "",
    "1 2" are all rejected. */
inline bool parseStrictSize (const std::string& text, std::size_t& out)
{
    if (text.empty())
        return false;
    std::size_t value = 0;
    for (char c : text)
    {
        if (c < '0' || c > '9')
            return false;
        const auto digit = static_cast<std::size_t> (c - '0');
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

/** Strictly parses a block size in [1, maxBlock]. Anything else is rejected. */
inline bool parseBlockFrames (const std::string& text, std::size_t maxBlock,
                              std::size_t& out)
{
    std::size_t value = 0;
    if (! parseStrictSize (text, value))
        return false;
    if (value == 0 || value > maxBlock)
        return false;
    out = value;
    return true;
}

} // namespace tracker_diag
