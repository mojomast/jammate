// INT-DRUM-001 — Musical Clock -> actual DrumEngine integration.
//
// These drive the REAL DrumEngine (internal sampler + embedded assets path,
// hosting a real juce::AudioPluginInstance MIDI sink) through the bounded
// jam::DrumClockBridge command queue. They assert the properties the task
// requires and that unit tests on the adapter alone cannot:
//
//   * a join lands exactly on the next bar boundary, at the correct within-block
//     offset, for block sizes that do not divide a bar;
//   * a tempo change is applied on a bar boundary without shifting the grid
//     (phase continuity);
//   * stop / resync land on their exact target sample;
//   * queue pressure drops are counted and late/invalid commands are never
//     partially applied;
//   * 30 minutes of injected playback do not drift;
//   * standalone manual transport behavior is unchanged when no bridge is
//     attached;
//   * the injected callback allocates nothing when the MIDI scratch is reserved.
//
// This is an integration seam, NOT a claim of full production wiring, G4 or G1
// safety. A no-op sink measures only this engine; no claim is made about
// arbitrary third-party drum plugins.

#include "TestHarness.h"

#include "DrumEngine.h"
#include "DrumMidiCapacity.h"
#include "jam/DrumClockBridge.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>

// The JUCE harness provides CHECK / CHECK_MSG / REQUIRE only; give this file the
// same convenience spellings the protocol-neutral tests use, so a comparison
// failure is still a real recorded failure.
#define CHECK_EQ(a, b) CHECK ((a) == (b))
#define CHECK_NEAR(a, b, tol) CHECK (std::abs ((double) (a) - (double) (b)) <= (double) (tol))

//==============================================================================
// ELF linker wrapping observes the C heap calls made by the statically linked
// JUCE/engine objects, including MidiBuffer's realloc. C++ new is routed through
// the same probe. This measures our engine with a no-op guest, not arbitrary
// VSTs. (Mirrors tests/DrumMidiTests.cpp; that file is not part of this driver.)
namespace probe
{
#if defined(DRUM_MIDI_HEAP_PROBE)
thread_local bool measuring = false;
thread_local std::size_t allocations = 0;
thread_local std::size_t deallocations = 0;
#endif
}

#if defined(DRUM_MIDI_HEAP_PROBE)
extern "C" void* __real_malloc (std::size_t);
extern "C" void* __real_realloc (void*, std::size_t);
extern "C" void __real_free (void*);
extern "C" void* __wrap_malloc (std::size_t bytes)
{
    if (probe::measuring) ++probe::allocations;
    return __real_malloc (bytes);
}
extern "C" void* __wrap_realloc (void* p, std::size_t bytes)
{
    if (probe::measuring) ++probe::allocations;
    return __real_realloc (p, bytes);
}
extern "C" void __wrap_free (void* p)
{
    if (probe::measuring && p != nullptr) ++probe::deallocations;
    __real_free (p);
}
void* operator new (std::size_t bytes)
{
    if (void* p = std::malloc (bytes > 0 ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t bytes) { return ::operator new (bytes); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
#endif

namespace
{
using namespace jam;

// A real AudioPluginInstance observing engine MIDI without a device/UI.
class MidiSink final : public juce::AudioPluginInstance
{
public:
    const juce::String getName() const override { return "INT-DRUM-001 MIDI sink"; }
    void fillInPluginDescription (juce::PluginDescription&) const override {}
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

struct Hit
{
    std::uint64_t sample = 0;      // absolute sample of the event
    std::uint64_t blockStart = 0;  // start of the block it was rendered in
    int note = 0;
    int velocity = 0;
};

// The first 4/4 ROCK groove, used by every test.
LibraryIndex rockGroove()
{
    const auto& lib = drum::library();
    for (std::size_t i = 0; i < lib.size(); ++i)
        if (juce::String (lib[i].genre) == "ROCK" && ! lib[i].fill
            && lib[i].num == 4 && lib[i].den == 4)
            return static_cast<LibraryIndex> (i);
    return kNoLibraryEntry;
}

constexpr int kKick = 36; // drum::gmNote[drum::kick]

struct Rig
{
    DrumClockBridge bridge { [] { DrumClockBridgeConfig c; c.initialBpm = 120.0;
                                  c.defaultSampleRate = 48000.0; return c; }() };
    DrumEngine engine;
    MidiSink sink;
    juce::AudioBuffer<float> audio { 2, 8192 };
    juce::MidiBuffer midi;
    std::vector<Hit> hits;
    std::vector<Hit> noteOffs;
    std::uint64_t elapsed = 0;
    bool storeHits = true;
    double sampleRate = 48000.0;

    bool setup (double rate, int block, LibraryIndex rock)
    {
        sampleRate = rate;
        engine.prepare (rate, block);
        engine.humanVel.store (0.0f);
        engine.humanTime.store (0.0f);
        engine.humanRR.store (0.0f);
        engine.clickOn.store (false);
        const bool ok = engine.prepareInjectedGroove (rock);
        engine.attachClockBridge (&bridge.commandQueue());
        bridge.prepare (rate, block);
        midi.ensureSize (drum::midiScratchBytesForBlock (8192));
        return ok;
    }

    void block (int n)
    {
        const std::uint64_t blockStart = elapsed;
        bridge.setClockSample (elapsed); // explicit clock == audio timeline
        engine.process (audio, n, &sink, midi);

        if (storeHits)
            for (const auto event : midi)
            {
                const auto message = event.getMessage();
                if (message.isNoteOn())
                    hits.push_back ({ blockStart + (std::uint64_t) event.samplePosition,
                                      blockStart, message.getNoteNumber(),
                                      message.getVelocity() });
                else if (message.isNoteOff())
                    noteOffs.push_back ({ blockStart + (std::uint64_t) event.samplePosition,
                                          blockStart, message.getNoteNumber(), 0 });
            }
        elapsed += static_cast<std::uint64_t> (n);
    }

    void render (std::uint64_t total, int blockSize)
    {
        while (elapsed < total)
        {
            const int n = static_cast<int> (
                juce::jmin<std::uint64_t> (total - elapsed, (std::uint64_t) blockSize));
            block (n);
        }
    }
};

ClockSnapshot lockedSnapshot (double bpm, std::uint64_t generation)
{
    ClockSnapshot snapshot;
    snapshot.bpm = bpm;
    snapshot.generation = generation;
    snapshot.beatsPerBar = 4;
    snapshot.beatUnit = 4;
    snapshot.lockState = ClockLockState::Locked;
    return snapshot;
}

std::int64_t firstHit (const std::vector<Hit>& hits, int note)
{
    for (const auto& hit : hits)
        if (hit.note == note)
            return static_cast<std::int64_t> (hit.sample);
    return -1;
}

std::size_t countHitsIn (const std::vector<Hit>& hits,
                         std::uint64_t lo, std::uint64_t hi, int note)
{
    std::size_t count = 0;
    for (const auto& hit : hits)
        if (hit.note == note && hit.sample >= lo && hit.sample < hi)
            ++count;
    return count;
}
} // namespace

//==============================================================================
// A join lands exactly on the next bar boundary for block sizes that do not
// divide a bar, so the boundary falls mid-block and must not be rounded.
//==============================================================================
TEST_CASE (intdrum_join_lands_exactly_on_next_bar_boundary)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    for (const int block : { 333, 700, 1000 })
    {
        Rig rig;
        REQUIRE (rig.setup (48000.0, block, rock));
        rig.block (block); // move the explicit clock off zero
        REQUIRE (rig.bridge.requestJoinAtNextBar (rock));

        rig.render (96000u + (std::uint64_t) block, block);

        const std::int64_t kick = firstHit (rig.hits, kKick);
        CHECK_MSG (kick == 96000, "block=" + std::to_string (block)
                   + " first kick=" + std::to_string (kick));

        // The hit is genuinely inside its block, not snapped to the block start.
        for (const auto& hit : rig.hits)
            if (hit.note == kKick)
            {
                const std::uint64_t offset = hit.sample - hit.blockStart;
                CHECK_EQ (offset, static_cast<std::uint64_t> (96000 % block));
                break;
            }
    }
}

//==============================================================================
// Tempo updates are quantised to a bar boundary and preserve phase: the last
// interval of the old bar is unchanged, the first interval of the new bar uses
// the new tempo.
//==============================================================================
TEST_CASE (intdrum_tempo_change_lands_on_bar_boundary_phase_continuous)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 512, rock));
    rig.block (512);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (96000u + 512u, 512); // joined; bar 2 runs 96000..192000 at 120 BPM

    rig.bridge.applySnapshot (lockedSnapshot (150.0, 1));
    CHECK_EQ (rig.bridge.nextBarBoundarySample(), static_cast<std::uint64_t> (192000));

    rig.render (210000u, 512);

    // Old bar at 120 BPM: step 14 (a hat) at 180000, then nothing until the
    // downbeat. New bar at 150 BPM: step 2 (hat) at 192000 + 2*4800 = 201600.
    CHECK (firstHit (rig.hits, kKick) == 96000);
    CHECK (countHitsIn (rig.hits, 180000, 180001, 42) == 1); // hat at 180000
    CHECK (countHitsIn (rig.hits, 180001, 192000, -1) == 0); // nothing skipped
    CHECK (countHitsIn (rig.hits, 192000, 192001, kKick) == 1); // downbeat kept
    CHECK (countHitsIn (rig.hits, 192001, 201600, -1) == 0); // one 4800 interval
    CHECK (countHitsIn (rig.hits, 201600, 201601, 42) == 1); // new-tempo hat
}

//==============================================================================
// Stop at the next bar is exact: no note is rendered at or after the boundary,
// and the hosted kit's notes are released on it.
//==============================================================================
TEST_CASE (intdrum_stop_at_next_bar_is_exact_and_releases_notes)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 512, rock));
    rig.block (512);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (96000u + 512u, 512);

    REQUIRE (rig.bridge.requestStopAtNextBar());
    rig.render (200000u, 512);

    for (const auto& hit : rig.hits)
        CHECK (hit.sample < 192000u); // nothing fires on/after the stop boundary

    std::size_t stopReleases = 0;
    for (const auto& off : rig.noteOffs)
        if (off.sample == 192000u)
            ++stopReleases;
    CHECK (stopReleases >= 1u); // the stop released the hosted notes on the boundary
}

//==============================================================================
// Resync bar: a downbeat lands exactly on an arbitrary target sample.
//==============================================================================
TEST_CASE (intdrum_resync_bar_places_downbeat_on_target)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 512, rock));
    rig.block (512);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (100000u, 512);
    REQUIRE (rig.bridge.requestResyncNextBar (110000));
    rig.render (120000u, 512);

    CHECK (countHitsIn (rig.hits, 110000, 110001, kKick) == 1);
}

//==============================================================================
// Resync beat: a beat step lands exactly on an arbitrary target sample.
//==============================================================================
TEST_CASE (intdrum_resync_beat_places_a_step_on_target)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 512, rock));
    rig.block (512);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (100000u, 512);
    REQUIRE (rig.bridge.requestResyncNextBeat (111000));
    rig.render (120000u, 512);

    std::size_t atTarget = 0;
    for (const auto& hit : rig.hits)
        if (hit.sample == 111000u)
            ++atTarget;
    CHECK (atTarget > 0);
}

//==============================================================================
// Pressure: the bounded queue drops and counts the incoming command; the engine
// rejects invalid commands as a whole without touching transport state.
//==============================================================================
TEST_CASE (intdrum_pressure_drops_counted_and_no_partial_commands)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 512, rock));
    rig.block (512);

    int rejectedByBridge = 0;
    for (int i = 0; i < 40; ++i)
        if (! rig.bridge.requestJoinAtNextBar (rock))
            ++rejectedByBridge;

    CHECK (rejectedByBridge >= 1);
    CHECK (rig.bridge.queueDropCount() >= 1u);

    // Let the engine drain the queue (max 4 commands per block).
    rig.render (512u + 64u * 512u, 512);
    CHECK_EQ (rig.engine.injectedDropCount(), rig.bridge.queueDropCount());

    // An invalid bpm is rejected as a whole; the running tempo is untouched.
    const double tempoBefore = rig.engine.injectedTempo();
    DrumClockCommand bad;
    bad.type = DrumClockCommandType::SetTempo;
    bad.sampleTime = rig.elapsed;
    bad.bpm = std::nan ("");
    REQUIRE (rig.bridge.commandQueue().push (bad));
    rig.block (512);
    CHECK (rig.engine.injectedRejectedCount() >= 1u);
    CHECK_NEAR (rig.engine.injectedTempo(), tempoBefore, 0.0);

    // A join naming a groove that was never prepared is refused, not partially
    // engaged.
    DrumClockCommand wrongGroove;
    wrongGroove.type = DrumClockCommandType::JoinAtBar;
    wrongGroove.sampleTime = rig.elapsed;
    wrongGroove.bpm = 120.0;
    wrongGroove.groove = 9999;
    REQUIRE (rig.bridge.commandQueue().push (wrongGroove));
    rig.block (512);
    CHECK (rig.engine.injectedRejectedCount() >= 2u);
    CHECK (rig.engine.injectedGroove() == rock);
}

//==============================================================================
// A late command (target already in the past) is applied at the block origin,
// never silently dropped.
//==============================================================================
TEST_CASE (intdrum_late_command_applies_at_block_origin)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 512, rock));
    rig.block (512);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (96000u + 512u, 512);

    DrumClockCommand late;
    late.type = DrumClockCommandType::SetTempo;
    late.sampleTime = 0; // far in the past
    late.bpm = 150.0;
    REQUIRE (rig.bridge.commandQueue().push (late));
    rig.block (512);

    CHECK_NEAR (rig.engine.injectedTempo(), 150.0, 0.0);
}

//==============================================================================
// Thirty minutes of injected playback at a constant tempo must not drift: the
// step count matches the closed form and every step sits on the exact grid.
//==============================================================================
TEST_CASE (intdrum_thirty_minute_horizon_does_not_drift)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    rig.storeHits = false; // millions of hits would dominate the run time
    REQUIRE (rig.setup (48000.0, 4096, rock));
    rig.block (4096);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));

    const std::uint64_t join = 96000u;      // first bar boundary
    const std::uint64_t step = 6000u;       // 120 BPM @ 48 kHz
    const std::uint64_t total = 48000ull * 1800ull; // 30 minutes

    rig.render (total, 4096);

    const std::uint64_t fired = rig.engine.injectedStepsFired();
    const std::uint64_t expected = ((total - 1u) - join) / step + 1u;

    CHECK_NEAR (static_cast<double> (fired), static_cast<double> (expected), 1.0);
    // Anti-drift: the last step is exactly on the closed-form grid, not merely
    // close, so no per-step rounding has accumulated over the run.
    CHECK_EQ (rig.engine.injectedLastStepSample(), join + (fired - 1u) * step);
    CHECK_EQ (rig.engine.injectedSamplePosition(), total);
}

//==============================================================================
// With no bridge attached the manual transport is byte-for-byte the old
// behavior (regression guard for the process() routing change).
//==============================================================================
TEST_CASE (intdrum_standalone_manual_transport_is_unchanged)
{
    DrumEngine engine;
    MidiSink sink;
    engine.prepare (8000.0, 8192);
    engine.bpm.store (120.0f); // 1000 samples per sixteenth at 8 kHz
    engine.humanVel.store (0.0f);
    engine.humanTime.store (0.0f);
    engine.humanRR.store (0.0f);
    for (int b = 0; b < drum::barsPerSection; ++b)
    {
        engine.barUsed[b].store (true);
        for (int s = 0; s < drum::maxStepsPerBar; ++s)
            engine.pattern[b][drum::kick][s].store (1);
    }

    juce::AudioBuffer<float> audio (2, 8192);
    juce::MidiBuffer midi;
    midi.ensureSize (drum::midiScratchBytesForBlock (8192));
    engine.playing.store (true);

    std::vector<std::uint64_t> kicks;
    std::uint64_t elapsed = 0;
    while (elapsed < 2200u)
    {
        const int n = (int) juce::jmin<std::uint64_t> (2200u - elapsed, 128u);
        engine.process (audio, n, &sink, midi);
        for (const auto event : midi)
            if (event.getMessage().isNoteOn() && event.getMessage().getNoteNumber() == kKick)
                kicks.push_back (elapsed + (std::uint64_t) event.samplePosition);
        elapsed += (std::uint64_t) n;
    }

    REQUIRE (kicks.size() == 3u);
    CHECK_EQ (kicks[0], static_cast<std::uint64_t> (8));
    CHECK_EQ (kicks[1], static_cast<std::uint64_t> (1008));
    CHECK_EQ (kicks[2], static_cast<std::uint64_t> (2008));
    CHECK_EQ (engine.uiBar.load(), 0);
}

//==============================================================================
// The injected audio callback allocates nothing when the MIDI scratch is
// reserved (measured with the same ELF wrapping the foundation tests use).
//==============================================================================
#if defined(DRUM_MIDI_HEAP_PROBE)
TEST_CASE (intdrum_injected_callback_allocates_nothing)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 512, rock));
    rig.block (512);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (96000u + 512u, 512);

    // Publish a tempo change that will be applied during the measured window.
    rig.bridge.applySnapshot (lockedSnapshot (150.0, 2));

    std::size_t totalAlloc = 0;
    std::size_t totalFree = 0;
    for (int i = 0; i < 4000; ++i)
    {
        rig.bridge.setClockSample (rig.elapsed); // worker side, outside the probe
        probe::allocations = probe::deallocations = 0;
        probe::measuring = true;
        rig.engine.process (rig.audio, 512, &rig.sink, rig.midi);
        probe::measuring = false;
        totalAlloc += probe::allocations;
        totalFree += probe::deallocations;
        rig.elapsed += 512;
    }

    CHECK_EQ (totalAlloc, static_cast<std::size_t> (0));
    CHECK_EQ (totalFree, static_cast<std::size_t> (0));
}
#endif
