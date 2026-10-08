// CsvMethodLog — writes TempoStable MethodRecords to a CSV (TRACK-008).
//
// Diagnostic-only. One file per wrapper instance. The scorer CLI constructs one
// backend and resets it per fixture (manifest order), so a single raw log is
// split by block count into per-fixture windows. Every block is written so the
// confirmation transition is exact.

#pragma once

#include "TempoStable.h"

#include <fstream>
#include <string>

namespace tempo_stable
{

class CsvMethodLog
{
public:
    explicit CsvMethodLog (const std::string& path);
    ~CsvMethodLog();

    bool ok() const noexcept { return out_.good(); }

    void operator() (const MethodRecord& rec);

private:
    std::ofstream out_;
};

} // namespace tempo_stable
