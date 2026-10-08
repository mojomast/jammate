// CsvMethodLog implementation. See MethodLog.h.

#include "MethodLog.h"

#include <cstdio>

namespace tempo_stable
{

CsvMethodLog::CsvMethodLog (const std::string& path)
    : out_ (path.c_str(), std::ios::binary)
{
    out_ << "blockIndex,blockStartSeconds,blockEndSeconds,beatEvent,eventSeconds,"
            "intervalMeasured,intervalSeconds,intervalState,ringCount,ringFull,"
            "stableCount,confirmed,baseBpm,derivedBpm,derivedValid,emittedBpm\n";
}

CsvMethodLog::~CsvMethodLog() = default;

void CsvMethodLog::operator() (const MethodRecord& rec)
{
    char buf[380];
    // intervalSeconds is an EMPTY cell when no genuine interval was measured;
    // a measured value (including a rejected gap/malformed interval) is written.
    char interval[32];
    if (rec.intervalMeasured)
        std::snprintf (interval, sizeof interval, "%.9f", rec.intervalSeconds);
    else
        interval[0] = '\0';
    char derived[32];
    if (rec.derivedValid)
        std::snprintf (derived, sizeof derived, "%.9f", rec.derivedBpm);
    else
        derived[0] = '\0';
    std::snprintf (buf, sizeof buf,
                   "%llu,%.9f,%.9f,%d,%.9f,%d,%s,%s,%zu,%d,%d,%d,%.9f,%s,%d,%.9f\n",
                   static_cast<unsigned long long> (rec.blockIndex),
                   rec.blockStartSeconds, rec.blockEndSeconds,
                   rec.beatEvent ? 1 : 0, rec.eventSeconds,
                   rec.intervalMeasured ? 1 : 0, interval,
                   toString (rec.intervalState),
                   rec.ringCount, rec.ringFull ? 1 : 0,
                   rec.stableCount, rec.confirmed ? 1 : 0,
                   rec.baseBpm, derived, rec.derivedValid ? 1 : 0,
                   rec.emittedBpm);
    out_ << buf;
}

} // namespace tempo_stable
