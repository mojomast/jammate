// Corpus manifest reader for the rhythm evaluation tool (EVAL-002).
//
// Reads testdata/rhythm/manifest.json into a typed structure and converts each
// fixture into the pure `RhythmTruth` the metrics consume. Reading is strict:
// a missing required field is an error, not a zero, because a manifest that
// half-parses would make EVAL-002 score a corpus that is not the one on disk.
//
// This file performs I/O; it is deliberately separate from Metrics.h/.cpp so
// the metrics can be tested without a filesystem.

#pragma once

#include "Metrics.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace rhythmeval
{

class ManifestError : public std::runtime_error
{
public:
    explicit ManifestError (const std::string& what) : std::runtime_error (what) {}
};

struct ManifestFixture
{
    std::string name;
    std::string file;
    std::string sha256;
    std::string license;
    std::string provenance;
    std::string notes;
    std::string tempoProfile;

    double durationSeconds = 0.0;
    double sampleRate = 48000.0;

    int meterNumerator = 0;
    int meterDenominator = 0;
    int beatsPerBar = 0;

    bool hasNominalBpm = false;
    double nominalBpm = 0.0;
    double bpmStart = 0.0;
    double bpmEnd = 0.0;
    double rampStartSeconds = 0.0;
    double rampEndSeconds = 0.0;

    std::vector<double> beats;
    std::vector<double> onsets;
    std::vector<SilenceSpan> silenceSpans;
    std::vector<SilenceSpan> trueSilenceSpans;
    std::vector<int> silentBeats;
    std::vector<std::string> tags;
};

struct Manifest
{
    int schemaVersion = 0;
    std::string corpusId;
    double beatToleranceSeconds = kBeatMatchToleranceSeconds;
    std::vector<ManifestFixture> fixtures;
};

/** Parses manifest JSON text. Throws ManifestError with a precise message on
    malformed JSON or a missing required field. */
Manifest parseManifest (const std::string& jsonText);

/** Reads `path` and parses it. Throws ManifestError when the file cannot be
    read. */
Manifest readManifestFile (const std::string& path);

/** Converts a manifest fixture into the pure metric input. */
RhythmTruth toTruth (const ManifestFixture& fixture);

} // namespace rhythmeval
