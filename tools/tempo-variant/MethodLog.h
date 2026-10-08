// CsvMethodLog — writes TempoVariant MethodRecords to a CSV (TRACK-005).
//
// Diagnostic-only. One file per wrapper instance (the existing
// tracker-diagnostics CLI creates one backend per fixture, in manifest order;
// the run script maps instance index -> fixture name). Every block is written
// so the readiness transition is exact; the committed per-beat artefact is a
// filtered projection of this raw log, produced by extract_method_history.py.

#pragma once

#include "TempoVariant.h"

#include <fstream>
#include <string>

namespace tempo_variant
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

} // namespace tempo_variant
