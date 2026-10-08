// FixtureMetricsDump — per-fixture full-metric JSON for one plugin (TRACK-005).
//
// The TRACK-004 diagnostic CLI exports acquisition diagnostics and the
// aggregate, but NOT the per-fixture metrics (so the silence-acceleration
// diagnostic cannot be attributed to a fixture from its artefacts alone). This
// small tool reuses the UNMODIFIED rhythmeval source maths (Metrics.cpp,
// Manifest.cpp, BackendRunner.cpp) and the same plugin convention to dump the
// complete `fixtureMetricsToJson` record per fixture. Its aggregate is
// cross-checked against the diagnostic CLI's summary.json.

#include "BackendRunner.h"
#include "Json.h"
#include "Manifest.h"
#include "Metrics.h"

#include "jam/IRhythmTracker.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#  include <dlfcn.h>
#  define FMD_HAVE_DLOPEN 1
#else
#  define FMD_HAVE_DLOPEN 0
#endif

namespace
{

using CreateFn = jam::IRhythmTracker* (*)();
using DestroyFn = void (*) (jam::IRhythmTracker*);

template <typename Fn>
Fn loadSymbol (void* library, const char* name)
{
    void* symbol = ::dlsym (library, name);
    Fn fn = nullptr;
    static_assert (sizeof (Fn) == sizeof (symbol), "size mismatch");
    std::memcpy (&fn, &symbol, sizeof (fn));
    return fn;
}

struct Lib
{
    void* handle = nullptr;
    CreateFn create = nullptr;
    DestroyFn destroy = nullptr;
    ~Lib() { if (handle) ::dlclose (handle); }
    struct Deleter
    {
        DestroyFn destroy = nullptr;
        void operator() (jam::IRhythmTracker* p) const { if (p && destroy) destroy (p); }
    };
    using Ptr = std::unique_ptr<jam::IRhythmTracker, Deleter>;
    Ptr makeOwned() const { return Ptr (create ? create() : nullptr, Deleter {destroy}); }
};

} // namespace

int main (int argc, char** argv)
{
    std::string corpus, out, backendLib, backendName;
    std::size_t blockFrames = 128;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> std::string { return (i + 1 < argc) ? argv[++i] : std::string(); };
        if (a == "--corpus") corpus = next();
        else if (a == "--out") out = next();
        else if (a == "--backend-lib") backendLib = next();
        else if (a == "--backend") backendName = next();
        else if (a == "--block") blockFrames = static_cast<std::size_t> (std::strtoul (next().c_str(), nullptr, 10));
        else { std::cerr << "unknown option " << a << "\n"; return 2; }
    }
    if (corpus.empty() || out.empty() || backendLib.empty())
    { std::cerr << "usage: tempo-variant-metrics --corpus <dir> --backend-lib <so> --out <file> [--backend name] [--block n]\n"; return 2; }

#if FMD_HAVE_DLOPEN
    void* h = ::dlopen (backendLib.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (h == nullptr) { std::cerr << "dlopen failed: " << ::dlerror() << "\n"; return 2; }
    Lib lib;
    lib.handle = h;
    lib.create = loadSymbol<CreateFn> (h, "jam_rhythm_create");
    lib.destroy = loadSymbol<DestroyFn> (h, "jam_rhythm_destroy");
    if (! lib.create || ! lib.destroy) { std::cerr << "missing factory\n"; return 2; }
#else
    std::cerr << "dlopen unsupported\n"; return 2;
#endif

    rhythmeval::Manifest manifest;
    try { manifest = rhythmeval::readManifestFile (corpus + "/manifest.json"); }
    catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 2; }

    const double tol = manifest.beatToleranceSeconds;
    rhythmjson::Value root = rhythmjson::Value::makeObject();
    root.set ("backend", rhythmjson::Value::makeString (backendName));
    root.set ("blockFrames", rhythmjson::Value::makeNumber (static_cast<double> (blockFrames)));
    rhythmjson::Value arr = rhythmjson::Value::makeArray();
    std::vector<rhythmeval::FixtureMetrics> all;

    for (const rhythmeval::ManifestFixture& fx : manifest.fixtures)
    {
        rhythmeval::WavData audio;
        try { audio = rhythmeval::readWavMono16 (corpus + "/" + fx.file); }
        catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 2; }

        Lib::Ptr backend = lib.makeOwned();
        if (! backend) { std::cerr << "null factory\n"; return 2; }
        rhythmeval::BackendRunner runner (blockFrames);
        const rhythmeval::ObservationSeries series = runner.run (*backend, audio);
        const rhythmeval::RhythmTruth truth = rhythmeval::toTruth (fx);
        const rhythmeval::FixtureMetrics m = rhythmeval::scoreFixture (truth, series, tol, 0.0);
        all.push_back (m);
        arr.push (rhythmeval::fixtureMetricsToJson (m));
    }
    root.set ("fixtures", arr);
    root.set ("aggregate",
              rhythmeval::aggregateMetricsToJson (rhythmeval::aggregateFixtures (all)));

    std::ofstream f (out.c_str(), std::ios::binary);
    if (! f.good()) { std::cerr << "cannot write " << out << "\n"; return 2; }
    f << root.dump() << "\n";
    std::cout << "wrote " << out << "\n";
    return 0;
}
