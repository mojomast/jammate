// BtrackGrid — read-only arithmetic restatement of BTrack 1.0.7's tempo->lag
// quantiser (TRACK-004). No vendored file is modified.
//
// The two load-bearing lines are, verbatim, in
// third_party/BTrack/src/BTrack.cpp:
//
//   line 435: beatPeriod = round ((60.0 * 44100.0) /
//                                 (((2 * maxIndex) + 80) * ((double) hopSize)));
//   line 438: estimatedTempo = 60.0 / ((((double) hopSize) / 44100.0) * beatPeriod);
//
// The tempo search grid is 41 candidates of 80..160 BPM in steps of 2
// (`2*maxIndex + 80`). Each candidate is snapped to an INTEGER number of
// detection-function hops (`beatPeriod`), and the reported BPM is recomputed
// from that integer hop count. This file enumerates the resulting staircase so
// the harness can decide, with arithmetic, whether a reported value like
// 123.05 is an exact grid output and what its nearest neighbours are.

#pragma once

#include <string>
#include <vector>

namespace tracker_diag
{

struct BtrackGridRow
{
    int    maxIndex = 0;
    double candidateBpm = 0.0;      // 2*maxIndex + 80
    double rawLagHops = 0.0;        // unrounded
    int    lagHops = 0;             // round(rawLagHops)
    double effectiveBpm = 0.0;      // reported BPM for this candidate
    double lagSeconds = 0.0;        // lagHops * hopSize / timescale
};

/** Enumerates the candidate grid exactly as BTrack.cpp computes it. Defaults
    are the adapter's configuration: hop 512, timescale 44100 (the value
    hard-coded in calculateTempo). */
std::vector<BtrackGridRow> btrackTempoGrid (int hopSize = 512,
                                            double timescale = 44100.0,
                                            int maxIndexLow = 0,
                                            int maxIndexHigh = 40);

/** Recomputes the reported BPM for an integer hop lag (line 438). */
double btrackBpmForLagHops (int lagHops, int hopSize = 512,
                            double timescale = 44100.0);

/** The grid candidate whose reported BPM is closest to `bpm`, with the
    absolute error. Returns false only when the grid is empty. */
bool nearestGridCandidate (double bpm,
                           BtrackGridRow& out,
                           int hopSize = 512,
                           double timescale = 44100.0);

/** A compact text staircase of the grid (for the artifact). */
std::string btrackGridCsv (int hopSize = 512, double timescale = 44100.0);

} // namespace tracker_diag
