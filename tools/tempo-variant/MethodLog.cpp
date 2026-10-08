// CsvMethodLog implementation. See MethodLog.h.

#include "MethodLog.h"

#include <cstdio>

namespace tempo_variant
{

CsvMethodLog::CsvMethodLog (const std::string& path)
    : out_ (path.c_str(), std::ios::binary)
{
    out_ << "blockIndex,blockStartSeconds,blockEndSeconds,beatEvent,eventSeconds,"
            "intervalMeasured,intervalSeconds,intervalState,ringCount,ready,"
            "baseBpm,variantBpm\n";
}

CsvMethodLog::~CsvMethodLog() = default;

void CsvMethodLog::operator() (const MethodRecord& rec)
{
    char buf[300];
    // intervalSeconds is an EMPTY cell when no genuine interval was measured;
    // a measured value (including a rejected gap/malformed interval) is written.
    char interval[32];
    if (rec.intervalMeasured)
        std::snprintf (interval, sizeof interval, "%.9f", rec.intervalSeconds);
    else
        interval[0] = '\0';
    std::snprintf (buf, sizeof buf,
                   "%llu,%.9f,%.9f,%d,%.9f,%d,%s,%s,%zu,%d,%.9f,%.9f\n",
                   static_cast<unsigned long long> (rec.blockIndex),
                   rec.blockStartSeconds, rec.blockEndSeconds,
                   rec.beatEvent ? 1 : 0, rec.eventSeconds,
                   rec.intervalMeasured ? 1 : 0, interval,
                   toString (rec.intervalState),
                   rec.ringCount, rec.ready ? 1 : 0,
                   rec.baseBpm, rec.variantBpm);
    out_ << buf;
}

} // namespace tempo_variant
