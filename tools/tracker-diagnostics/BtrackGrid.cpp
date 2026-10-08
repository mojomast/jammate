// BtrackGrid implementation. See BtrackGrid.h.

#include "BtrackGrid.h"

#include <cmath>
#include <cstdio>

namespace tracker_diag
{

double btrackBpmForLagHops (int lagHops, int hopSize, double timescale)
{
    if (lagHops <= 0 || hopSize <= 0 || ! (timescale > 0.0))
        return 0.0;
    return 60.0 / ((static_cast<double> (hopSize) / timescale)
                   * static_cast<double> (lagHops));
}

std::vector<BtrackGridRow> btrackTempoGrid (int hopSize, double timescale,
                                            int maxIndexLow, int maxIndexHigh)
{
    std::vector<BtrackGridRow> rows;
    for (int m = maxIndexLow; m <= maxIndexHigh; ++m)
    {
        BtrackGridRow r;
        r.maxIndex = m;
        r.candidateBpm = 2.0 * static_cast<double> (m) + 80.0;
        r.rawLagHops = (60.0 * timescale)
                       / (r.candidateBpm * static_cast<double> (hopSize));
        r.lagHops = static_cast<int> (std::llround (r.rawLagHops));
        r.effectiveBpm = btrackBpmForLagHops (r.lagHops, hopSize, timescale);
        r.lagSeconds = static_cast<double> (r.lagHops)
                       * static_cast<double> (hopSize) / timescale;
        rows.push_back (r);
    }
    return rows;
}

bool nearestGridCandidate (double bpm, BtrackGridRow& out,
                           int hopSize, double timescale)
{
    const std::vector<BtrackGridRow> rows =
        btrackTempoGrid (hopSize, timescale, 0, 40);
    bool have = false;
    double best = 0.0;
    for (const BtrackGridRow& r : rows)
    {
        const double err = std::fabs (r.effectiveBpm - bpm);
        if (! have || err < best)
        {
            have = true;
            best = err;
            out = r;
        }
    }
    return have;
}

std::string btrackGridCsv (int hopSize, double timescale)
{
    std::string out = "maxIndex,candidateBpm,rawLagHops,lagHops,effectiveBpm,lagSeconds\n";
    char buf[160];
    for (const BtrackGridRow& r : btrackTempoGrid (hopSize, timescale, 0, 40))
    {
        std::snprintf (buf, sizeof buf, "%d,%.1f,%.6f,%d,%.6f,%.9f\n",
                       r.maxIndex, r.candidateBpm, r.rawLagHops, r.lagHops,
                       r.effectiveBpm, r.lagSeconds);
        out += buf;
    }
    return out;
}

} // namespace tracker_diag
