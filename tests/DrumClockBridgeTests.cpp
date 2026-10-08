// INT-DRUM-001 — Musical Clock -> actual DrumEngine integration.
//
// These drive the REAL DrumEngine (internal sampler + embedded assets path,
// hosted MIDI sink, and the fallback synth) through the bounded
// jam::DrumClockBridge command queue. They assert the properties the task
// requires and that unit tests on the adapter alone cannot:
//
//   * a join lands exactly on the next bar boundary at the correct within-block
//     offset, for block sizes that do not divide a bar;
//   * tempo is coherent with the join in BOTH command orders and phase-
//     continuous across a bar boundary;
//   * resync puts worker and renderer on the SAME absolute phase, so later
//     downbeats agree;
//   * stop / resync / Clear+Join are sample-exact and correctly ordered;
//   * repeated tempo snaps for one boundary coalesce, and genuine pressure
//     drops/rejections are counted without partial application;
//   * late attach / late commands apply on the absolute audio timeline;
//   * a second prepare at a new rate keeps working;
//   * 30 minutes do not drift, including a fractional-step BPM verified through
//     actual MIDI events;
//   * the internal sampler and the fallback synth both render, and the processor
//     skip guard is served before the first join;
//   * standalone manual transport behavior is unchanged;
//   * the injected callback allocates nothing when the MIDI scratch is reserved.
//
// This is an integration seam, NOT a claim of full production wiring, G4 or G1
// safety. A no-op sink measures only this engine; no claim is made about
// arbitrary third-party drum plugins.

#include "TestHarness.h"

#include "DrumEngine.h"
#include "DrumMidiCapacity.h"
#include "DrumHeapProbe.h"
#include "jam/DrumClockBridge.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// The JUCE harness provides CHECK / CHECK_MSG / REQUIRE only; give this file the
// same convenience spellings the protocol-neutral tests use, so a comparison
// failure is still a real recorded failure.
#define CHECK_EQ(a, b) CHECK ((a) == (b))
#define CHECK_NEAR(a, b, tol) CHECK (std::abs ((double) (a) - (double) (b)) <= (double) (tol))

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
    int order = 0;                 // insertion order within its block
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
    std::uint64_t lastBlockStart = 0;
    int lastBlockSize = 0;
    float peakMagnitude = 0.0f;
    bool storeHits = true;
    bool useGuest = true;
    double sampleRate = 48000.0;

    bool setup (double rate, int block, LibraryIndex rock, std::uint64_t attachSample = 0)
    {
        sampleRate = rate;
        engine.prepare (rate, block);
        engine.humanVel.store (0.0f);
        engine.humanTime.store (0.0f);
        engine.humanRR.store (0.0f);
        engine.clickOn.store (false);
        const bool ok = engine.prepareInjectedGroove (rock);
        engine.attachClockBridge (&bridge.commandQueue(), attachSample);
        bridge.prepare (rate, block);
        midi.ensureSize (drum::midiScratchBytesForBlock (8192));
        return ok;
    }

    void capture()
    {
        if (! storeHits)
            return;
        int order = 0;
        for (const auto event : midi)
        {
            const auto message = event.getMessage();
            if (message.isNoteOn())
                hits.push_back ({ lastBlockStart + (std::uint64_t) event.samplePosition,
                                  lastBlockStart, message.getNoteNumber(),
                                  message.getVelocity(), order++ });
            else if (message.isNoteOff())
                noteOffs.push_back ({ lastBlockStart + (std::uint64_t) event.samplePosition,
                                      lastBlockStart, message.getNoteNumber(), 0, order++ });
        }
    }

    void process (int n) { engine.process (audio, n, useGuest ? &sink : nullptr, midi); }

    void block (int n)
    {
        lastBlockStart = elapsed;
        lastBlockSize = n;
        bridge.setClockSample (elapsed); // explicit clock == audio timeline
        process (n);
        if (! useGuest)
            peakMagnitude = std::max (peakMagnitude, audio.getMagnitude (0, 0, n));
        capture();
        elapsed += (std::uint64_t) n;
    }

    // Processor-like guard: PluginProcessor skips process() when the engine is
    // not audible and no hosted kit is present.
    void blockGuarded (int n)
    {
        lastBlockStart = elapsed;
        lastBlockSize = n;
        bridge.setClockSample (elapsed);
        if (engine.isAudible())
            process (n);
        else
            midi.clear();
        if (! useGuest)
            peakMagnitude = std::max (peakMagnitude, audio.getMagnitude (0, 0, n));
        capture();
        elapsed += (std::uint64_t) n;
    }

    void render (std::uint64_t total, int blockSize, bool guarded = false)
    {
        while (elapsed < total)
        {
            const int n = static_cast<int> (
                juce::jmin<std::uint64_t> (total - elapsed, (std::uint64_t) blockSize));
            if (guarded)
                blockGuarded (n);
            else
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
        if ((note < 0 || hit.note == note) && hit.sample >= lo && hit.sample < hi)
            ++count;
    return count;
}

bool hasHitNear (const std::vector<Hit>& hits, int note, std::uint64_t sample, std::uint64_t tol)
{
    for (const auto& hit : hits)
        if (hit.note == note)
        {
            const std::uint64_t d = hit.sample > sample ? hit.sample - sample : sample - hit.sample;
            if (d <= tol)
                return true;
        }
    return false;
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
// Tempo is coherent with the join in BOTH command orders: a snapshot applied
// before or after the join in the same bar must make the first joined bar run at
// the new tempo (first interval 4800, not 6000).
//==============================================================================
TEST_CASE (intdrum_join_coherent_tempo_snapshot_before_and_after)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    std::vector<Hit> hits[2];
    double tempo[2] = { 0.0, 0.0 };

    for (int order = 0; order < 2; ++order)
    {
        const bool snapshotFirst = (order == 0);
        Rig rig;
        REQUIRE (rig.setup (48000.0, 512, rock));
        rig.block (512); // now = 512, mid bar-1

        if (snapshotFirst)
            rig.bridge.applySnapshot (lockedSnapshot (150.0, 1));
        REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
        if (! snapshotFirst)
            rig.bridge.applySnapshot (lockedSnapshot (150.0, 1));

        rig.render (140000u, 512);
        hits[order] = rig.hits;
        tempo[order] = rig.engine.injectedTempo();
    }

    for (int order = 0; order < 2; ++order)
    {
        // First joined bar: downbeat at 96000, step 8 at 96000 + 8*4800 = 134400.
        CHECK_MSG (firstHit (hits[order], kKick) == 96000,
                   order == 0 ? "snapshot-first" : "join-first");
        CHECK (hasHitNear (hits[order], kKick, 134400, 0));
        CHECK_NEAR (tempo[order], 150.0, 0.0);
    }

    // The two orders must produce byte-identical MIDI timing.
    REQUIRE (hits[0].size() == hits[1].size());
    for (std::size_t i = 0; i < hits[0].size(); ++i)
    {
        CHECK_EQ (hits[0][i].sample, hits[1][i].sample);
        CHECK_EQ (hits[0][i].note, hits[1][i].note);
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
    rig.render (96000u + 512u, 512);

    rig.bridge.applySnapshot (lockedSnapshot (150.0, 1));
    CHECK_EQ (rig.bridge.nextBarBoundarySample(), static_cast<std::uint64_t> (192000));

    rig.render (210000u, 512);

    CHECK (firstHit (rig.hits, kKick) == 96000);
    CHECK (countHitsIn (rig.hits, 180000, 180001, 42) == 1);
    CHECK (countHitsIn (rig.hits, 180001, 192000, -1) == 0);
    CHECK (countHitsIn (rig.hits, 192000, 192001, kKick) == 1);
    CHECK (countHitsIn (rig.hits, 192001, 201600, -1) == 0);
    CHECK (countHitsIn (rig.hits, 201600, 201601, 42) == 1);
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
        CHECK (hit.sample < 192000u);

    std::size_t stopReleases = 0;
    for (const auto& off : rig.noteOffs)
        if (off.sample == 192000u)
            ++stopReleases;
    CHECK (stopReleases >= 1u);
}

//==============================================================================
// GAP7: a pending stop must not move the worker position before its boundary.
//==============================================================================
TEST_CASE (intdrum_stop_pending_keeps_position_until_boundary)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 512, rock));
    rig.block (512);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (96000u + 512u, 512);

    REQUIRE (rig.bridge.requestStopAtNextBar());
    CHECK_EQ (rig.bridge.stopPending(), true);
    CHECK_EQ (rig.bridge.playing(), true); // still rendering to the boundary

    rig.render (100000u, 512);
    CHECK_EQ (rig.bridge.playing(), true);
    CHECK_EQ (rig.engine.injectedPlaying(), true);

    rig.render (200000u, 512);
    CHECK_EQ (rig.bridge.stopPending(), false);
    CHECK_EQ (rig.bridge.playing(), false);
    for (const auto& hit : rig.hits)
        CHECK (hit.sample < 192000u);
}

//==============================================================================
// BLOCK2 / N3: after a ResyncBeat on a non-aligned target the engine must be on
// the worker's absolute grid. The assertions pin the exact grid (including the
// discriminating step at 183000 and the true downbeat at 207000), so the old
// ceil-based engine phase (downbeat at 183000) fails them.
//==============================================================================
TEST_CASE (intdrum_resync_beat_phase_matches_worker_grid)
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
    CHECK_EQ (rig.bridge.nextBarBoundarySample(), static_cast<std::uint64_t> (207000));
    rig.render (220000u, 512);

    // Exact 16-step grid from the resync target at 120 BPM (6000/step).
    CHECK (countHitsIn (rig.hits, 111000, 111001, kKick) == 1);   // step 0
    CHECK (countHitsIn (rig.hits, 135000, 135001, 38) == 1);      // step 4 snare
    CHECK (countHitsIn (rig.hits, 159000, 159001, kKick) == 1);   // step 8
    CHECK (countHitsIn (rig.hits, 171000, 171001, kKick) == 1);   // step 10
    CHECK (countHitsIn (rig.hits, 183000, 183001, 38) == 1);      // step 12 snare
    CHECK (countHitsIn (rig.hits, 183000, 183001, kKick) == 0);   // old ceil downbeat
    CHECK (countHitsIn (rig.hits, 207000, 207001, kKick) == 1);   // true next downbeat
}

//==============================================================================
// N3: the stop is exact AND the discriminating 183000 downbeat is absent, so
// this fails under the old ceil engine phase.
//==============================================================================
TEST_CASE (intdrum_resync_beat_then_stop_at_actual_downbeat)
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

    const std::uint64_t nextBar = rig.bridge.nextBarBoundarySample();
    CHECK_EQ (nextBar, static_cast<std::uint64_t> (207000));
    CHECK (countHitsIn (rig.hits, 111000, 111001, kKick) == 1);
    CHECK (countHitsIn (rig.hits, 183000, 183001, kKick) == 0); // old ceil would kick

    REQUIRE (rig.bridge.requestStopAtNextBar());
    rig.render (nextBar + 4096u, 512);
    for (const auto& hit : rig.hits)
        CHECK (hit.sample < nextBar);
    CHECK (countHitsIn (rig.noteOffs, nextBar, nextBar + 1, -1) >= 1);
}

//==============================================================================
// N3: a tempo change after a ResyncBeat must take effect on the correct actual
// downbeat (207000), not on the old ceil downbeat.
//==============================================================================
TEST_CASE (intdrum_resync_beat_then_tempo_at_actual_downbeat)
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

    const std::uint64_t nextBar = rig.bridge.nextBarBoundarySample();
    CHECK_EQ (nextBar, static_cast<std::uint64_t> (207000));
    rig.bridge.applySnapshot (lockedSnapshot (150.0, 1));
    rig.render (nextBar + 16384u, 512);

    CHECK (countHitsIn (rig.hits, nextBar, nextBar + 1, kKick) == 1);
    CHECK (countHitsIn (rig.hits, nextBar + 1, nextBar + 9600, -1) == 0);
    CHECK (countHitsIn (rig.hits, nextBar + 9600, nextBar + 9600 + 1, 42) == 1);
    CHECK (countHitsIn (rig.hits, 183000, 183001, kKick) == 0);
}

//==============================================================================
// Resync bar then tempo: the downbeat target is a non-aligned sample and the new
// tempo must take effect there.
//==============================================================================
TEST_CASE (intdrum_resync_bar_then_tempo_at_actual_downbeat)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 512, rock));
    rig.block (512);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (100000u, 512);
    REQUIRE (rig.bridge.requestResyncNextBar (130000));
    rig.render (140000u, 512);

    const std::uint64_t nextBar = rig.bridge.nextBarBoundarySample();
    rig.bridge.applySnapshot (lockedSnapshot (150.0, 1));
    rig.render (nextBar + 16384u, 512);

    CHECK (countHitsIn (rig.hits, nextBar, nextBar + 1, kKick) == 1);
    CHECK (countHitsIn (rig.hits, nextBar + 1, nextBar + 9600, -1) == 0);
    CHECK (countHitsIn (rig.hits, nextBar + 9600, nextBar + 9600 + 1, 42) == 1);
}

//==============================================================================
// N5: a ResyncBeat whose target lies beyond a STAGED tempo boundary must use the
// piecewise effective clock, so the engine's new-tempo grid is preserved. With
// the old single-rate phase, the engine would put a downbeat (kick) at 192000
// instead of the step-4 snare.
//==============================================================================
TEST_CASE (intdrum_piecewise_resync_phase_across_staged_tempo)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 512, rock));
    rig.block (512);
    rig.bridge.applySnapshot (lockedSnapshot (150.0, 1)); // staged at 96000
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));      // join carries 150
    REQUIRE (rig.bridge.requestResyncNextBeat (192000));   // target > staged boundary

    rig.render (200000u, 512);

    CHECK (countHitsIn (rig.hits, 96000, 96001, kKick) == 1);   // join downbeat
    CHECK (countHitsIn (rig.hits, 192000, 192001, 38) == 1);    // step 4 snare
    CHECK (countHitsIn (rig.hits, 192000, 192001, kKick) == 0); // not a downbeat

    const std::uint64_t nextBar = rig.bridge.nextBarBoundarySample();
    CHECK_EQ (nextBar, static_cast<std::uint64_t> (249600));
    rig.render (nextBar + 4096u, 512);
    CHECK (countHitsIn (rig.hits, nextBar, nextBar + 1, kKick) == 1);
}

//==============================================================================
// Residual 2: a ResyncBeat/Bar target T BEFORE a staged tempo boundary B, with
// the worker's update crossing both, must still leave the engine on the worker's
// grid: the engine's next downbeat equals bridge.nextBarBoundarySample(). Under
// the old unconditional tempo-then-resync order the worker drifts (e.g. 226800)
// while the engine renders at 196800.
//==============================================================================
TEST_CASE (intdrum_resync_before_staged_tempo_engine_agrees)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    const auto run = [&] (bool barResync, std::uint64_t expectedNextBar)
    {
        Rig rig;
        REQUIRE (rig.setup (48000.0, 512, rock));
        rig.block (512);
        REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
        rig.render (100000u, 512);

        rig.bridge.applySnapshot (lockedSnapshot (150.0, 1)); // B = 192000
        if (barResync)
            REQUIRE (rig.bridge.requestResyncNextBar (150000));  // T < B
        else
            REQUIRE (rig.bridge.requestResyncNextBeat (150000));

        rig.render (193000u, 512); // crosses both T and B
        const std::uint64_t nextBar = rig.bridge.nextBarBoundarySample();
        CHECK_MSG (nextBar == expectedNextBar,
                   "barResync=" + std::to_string (barResync ? 1 : 0));

        rig.render (nextBar + 4096u, 512);
        CHECK (countHitsIn (rig.hits, nextBar, nextBar + 1, kKick) == 1);
    };

    run (false, 196800u); // ResyncBeat
    run (true, 235200u);  // ResyncBar
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
// BLOCK6: Clear then Join in the same callback must release old notes BEFORE the
// new downbeat hits (no stale flush after the new note-ons).
//==============================================================================
TEST_CASE (intdrum_clear_then_join_same_callback_orders_release_first)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 512, rock));
    rig.block (512);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (96000u + 512u, 512);

    // Clear, then re-join at the current block origin so both are applied in the
    // same callback and the new downbeat fires at offset 0.
    DrumClockCommand clear;
    clear.type = DrumClockCommandType::Clear;
    clear.sampleTime = rig.elapsed;
    DrumClockCommand join;
    join.type = DrumClockCommandType::JoinAtBar;
    join.sampleTime = rig.elapsed;
    join.bpm = 120.0;
    join.groove = rock;
    REQUIRE (rig.bridge.commandQueue().push (clear));
    REQUIRE (rig.bridge.commandQueue().push (join));

    rig.block (512);
    const std::uint64_t at = rig.lastBlockStart;

    int releaseOrder = 1 << 30;
    int firstOnOrder = 1 << 30;
    for (const auto& off : rig.noteOffs)
        if (off.sample == at)
            releaseOrder = std::min (releaseOrder, off.order);
    for (const auto& hit : rig.hits)
        if (hit.sample == at)
            firstOnOrder = std::min (firstOnOrder, hit.order);

    REQUIRE (releaseOrder < (1 << 30));
    REQUIRE (firstOnOrder < (1 << 30));
    CHECK (releaseOrder < firstOnOrder); // release inserted before the new hits
}

//==============================================================================
// GAP4: many tempo snaps for one boundary coalesce; the engine uses the last
// accepted tempo, and genuine capacity overflow is counted.
//==============================================================================
TEST_CASE (intdrum_same_boundary_tempo_snaps_coalesce)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 512, rock));
    rig.block (512);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (96000u + 512u, 512);

    const double tempos[5] = { 140.0, 150.0, 160.0, 170.0, 180.0 };
    for (int i = 0; i < 5; ++i)
        rig.bridge.applySnapshot (lockedSnapshot (tempos[i], (std::uint64_t) (1 + i)));

    // Render across the boundary at 192000: the coalesced event applies the last
    // accepted tempo there, not the first.
    rig.render (192000u + 1024u, 512);
    CHECK_NEAR (rig.engine.injectedTempo(), 180.0, 0.0);

    // Distinct-target overflow: the bounded event store rejects and counts.
    const std::uint64_t base = rig.elapsed;
    for (int i = 0; i < 10; ++i)
    {
        DrumClockCommand command;
        command.type = DrumClockCommandType::SetTempo;
        command.sampleTime = base + 100000u + (std::uint64_t) i * 1000u;
        command.bpm = 100.0 + i;
        REQUIRE (rig.bridge.commandQueue().push (command));
    }
    rig.render (base + 8u * 512u, 512);
    CHECK (rig.engine.injectedRejectedCount() >= 2u);
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

    // Capacity is 16: 40 requests mean 24 dropped incoming joins.
    CHECK_EQ (rejectedByBridge, 24);
    CHECK_EQ (rig.bridge.queueDropCount(), static_cast<std::uint64_t> (24));

    // Drain (4 per block): all 16 queued joins are serviced, none invented.
    rig.render (512u + 8u * 512u, 512);
    CHECK_EQ (rig.engine.injectedCommandCount(), static_cast<std::uint64_t> (16));
    CHECK_EQ (rig.engine.injectedDropCount(), static_cast<std::uint64_t> (24));

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

    // A join naming an unprepared groove is refused, not partially engaged.
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
// A late command (target already in the past) is applied at the block origin and
// counted as late, never silently dropped.
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
    late.sampleTime = 0;
    late.bpm = 150.0;
    REQUIRE (rig.bridge.commandQueue().push (late));
    rig.block (512);

    CHECK_NEAR (rig.engine.injectedTempo(), 150.0, 0.0);
    CHECK (rig.engine.injectedLateCount() >= 1u);
}

//==============================================================================
// GAP1: the internal sampler (embedded GMRockKit) actually renders the injected
// groove, and the processor skip guard is served before the first join.
//==============================================================================
TEST_CASE (intdrum_internal_sampler_renders_injected_groove)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    rig.useGuest = false;
    REQUIRE (rig.setup (48000.0, 512, rock));
    rig.engine.loadEmbeddedSamples();
    CHECK (rig.engine.samplesLoaded());
    CHECK (rig.engine.isAudible()); // attached bridge keeps the callback running

    rig.block (512);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (96000u + 2048u, 512); // guarded path is exercised separately

    CHECK (rig.peakMagnitude > 0.0f); // real sampled audio, not silence
    CHECK (firstHit (rig.hits, kKick) == -1); // no hosted MIDI on this path
}

//==============================================================================
// GAP1: with no embedded samples the fallback synth still renders energy.
//==============================================================================
TEST_CASE (intdrum_fallback_synth_renders_injected_groove)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    rig.useGuest = false;
    REQUIRE (rig.setup (48000.0, 512, rock));
    CHECK (! rig.engine.samplesLoaded()); // fallback path

    rig.block (512);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (96000u + 2048u, 512);

    CHECK (rig.peakMagnitude > 0.0f);
}

//==============================================================================
// GAP1: with the processor's isAudible() skip guard, the first join is still
// consumed (the bridge term keeps the engine audible).
//==============================================================================
TEST_CASE (intdrum_processor_skip_guard_services_bridge_before_first_join)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    rig.useGuest = false;
    REQUIRE (rig.setup (48000.0, 512, rock));

    CHECK (rig.engine.isAudible()); // otherwise process() would be skipped

    rig.blockGuarded (512);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (96000u + 2048u, 512, /*guarded=*/true);

    CHECK (rig.peakMagnitude > 0.0f);
}

//==============================================================================
// GAP2: a late attach declares the current absolute audio sample, so the first
// join lands on the next bar with no extra silence.
//==============================================================================
TEST_CASE (intdrum_late_attach_uses_absolute_timeline)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    rig.bridge.prepare (48000.0, 512);
    rig.engine.prepare (48000.0, 512);
    rig.engine.humanVel.store (0.0f);
    rig.engine.humanTime.store (0.0f);
    rig.engine.humanRR.store (0.0f);
    rig.engine.clickOn.store (false);
    REQUIRE (rig.engine.prepareInjectedGroove (rock));
    rig.audio.setSize (2, 8192);
    rig.midi.ensureSize (drum::midiScratchBytesForBlock (8192));

    // Run 48000 samples with no bridge attached. This only advances the engine's
    // absolute timeline; the worker grid does not exist yet.
    rig.audio.setSize (2, 8192);
    rig.midi.ensureSize (drum::midiScratchBytesForBlock (8192));
    while (rig.elapsed < 48000u)
    {
        const int n = static_cast<int> (
            juce::jmin<std::uint64_t> (48000u - rig.elapsed, 512u));
        rig.engine.process (rig.audio, n, nullptr, rig.midi);
        rig.elapsed += (std::uint64_t) n;
    }

    // Late attach at the current absolute sample, then the explicit clock starts
    // at that same sample.
    rig.engine.attachClockBridge (&rig.bridge.commandQueue(), 48000);
    rig.bridge.setClockSample (48000); // explicit origin at 48000
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock)); // next bar = 144000

    rig.render (150000u, 512);

    // No extra delay: the first downbeat is exactly at 48000 + 96000.
    const std::int64_t kick = firstHit (rig.hits, kKick);
    CHECK_EQ (kick, static_cast<std::int64_t> (144000));
}

//==============================================================================
// GAP3: a second prepare at a different rate keeps the prepared groove and
// rebuilds a coherent grid; the bridge's re-prepare clears its old state.
//==============================================================================
TEST_CASE (intdrum_second_prepare_at_new_rate_keeps_groove)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 512, rock));
    rig.block (512);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (96000u + 512u, 512);
    CHECK (rig.engine.injectedGroove() == rock);
    CHECK (rig.bridge.samplePosition() > 0u);

    // N4: queue a stale command, then cause real queue drops, then prove
    // prepare() drains the queue and rebaselines the per-session drop counters.
    DrumClockCommand junk;
    junk.type = DrumClockCommandType::SetTempo;
    junk.sampleTime = 5u;
    junk.bpm = 100.0;
    REQUIRE (rig.bridge.commandQueue().push (junk));

    for (int i = 0; i < 40; ++i)
        rig.bridge.requestJoinAtNextBar (rock);
    CHECK (rig.bridge.queueDropCount() > 0u);
    CHECK (rig.engine.injectedDropCount() > 0u);

    // Quiescent re-prepare at 96 kHz. Patterns are rate-independent and survive;
    // the bridge forgets its old grid and drains stale commands.
    rig.engine.prepare (96000.0, 512);
    rig.bridge.prepare (96000.0, 512);
    CHECK_EQ (rig.engine.injectedGroove(), rock);
    CHECK_EQ (rig.engine.injectedActive(), false);
    CHECK_EQ (rig.bridge.samplePosition(), static_cast<std::uint64_t> (0));
    CHECK_EQ (rig.bridge.playing(), false);
    CHECK_EQ (rig.bridge.queueDropCount(), static_cast<std::uint64_t> (0));
    CHECK_EQ (rig.engine.injectedDropCount(), static_cast<std::uint64_t> (0));

    // Old queued commands were drained by prepare().
    DrumClockCommand leftover;
    CHECK_EQ (rig.bridge.popCommand (leftover), false);

    rig.hits.clear();
    rig.noteOffs.clear();
    rig.elapsed = 0;
    rig.bridge.setClockSample (0);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock)); // 96 kHz bar = 192000
    rig.render (96000u + 98304u, 512); // past 192000 + a bit

    CHECK_EQ (firstHit (rig.hits, kKick), static_cast<std::int64_t> (192000));
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
    rig.storeHits = false;
    REQUIRE (rig.setup (48000.0, 4096, rock));
    rig.block (4096);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));

    const std::uint64_t join = 96000u;
    const std::uint64_t step = 6000u;
    const std::uint64_t total = 48000ull * 1800ull;

    rig.render (total, 4096);

    const std::uint64_t fired = rig.engine.injectedStepsFired();
    const std::uint64_t expected = ((total - 1u) - join) / step + 1u;

    CHECK_NEAR (static_cast<double> (fired), static_cast<double> (expected), 1.0);
    CHECK_EQ (rig.engine.injectedLastStepSample(), join + (fired - 1u) * step);
    CHECK_EQ (rig.engine.injectedSamplePosition(), total);
}

//==============================================================================
// GAP8: 30 minutes at a NON-divisor BPM whose sixteenth is fractional, verified
// through actual MIDI note events (not just tick counts).
//==============================================================================
TEST_CASE (intdrum_thirty_minute_fractional_bpm_midi_no_drift)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 1024, rock));
    rig.block (1024);
    rig.bridge.applySnapshot (lockedSnapshot (127.0, 1)); // 720000/127 = 5669.29...
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));

    const std::uint64_t join = 96000u;
    const double step = 720000.0 / 127.0;
    const std::uint64_t total = 48000ull * 1800ull;

    rig.render (total, 1024);

    CHECK (firstHit (rig.hits, kKick) == static_cast<std::int64_t> (join));

    // Last rendered step against the closed form.
    const std::uint64_t last = rig.engine.injectedLastStepSample();
    const long long k = std::llround ((double) (last - join) / step);
    CHECK_NEAR ((double) last, (double) join + (double) k * step, 2.0);

    // A late downbeat must still land on the closed-form sample (verified through
    // an actual kick event, not an internal counter).
    const std::uint64_t barSteps = 16u;
    const std::uint64_t m = static_cast<std::uint64_t> (
        ((double) (total - 1u) - (double) join) / (step * (double) barSteps));
    const std::uint64_t expectedDownbeat = static_cast<std::uint64_t> (
        std::floor ((double) join + (double) m * step * (double) barSteps));
    CHECK (hasHitNear (rig.hits, kKick, expectedDownbeat, 2u));
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
    engine.bpm.store (120.0f);
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
// INT-LIVE-001 STOPDECISION: a bounded Stop (requestStopNow) leaves injected
// mode, so the legacy manual transport is usable again WITHOUT a device prepare;
// no stuck flags, and the engine stays attached/servicing the clock queue.
//==============================================================================
TEST_CASE (intdrum_stop_now_releases_injected_mode_and_manual_resumes)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 512, rock));
    rig.block (512);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (96000u + 512u, 512);
    CHECK (rig.engine.injectedActive());
    CHECK (rig.engine.injectedPlaying());

    REQUIRE (rig.bridge.requestStopNow());
    rig.block (512);
    CHECK (! rig.engine.injectedActive());
    CHECK (! rig.engine.injectedPlaying());
    CHECK (rig.engine.isAudible()); // still attached, servicing the clock queue

    // Legacy manual transport works again with no device prepare.
    rig.engine.playing.store (true);
    rig.engine.bpm.store (120.0f);
    rig.engine.barUsed[0].store (true);
    for (int s = 0; s < drum::maxStepsPerBar; ++s)
        rig.engine.pattern[0][drum::kick][s].store (1);
    rig.hits.clear();
    const std::uint64_t start = rig.elapsed;
    rig.render (start + 4096u, 512);
    CHECK (firstHit (rig.hits, kKick) >= 0);
}

//==============================================================================
// A cancel/clear before the first join must not engage injected mode, and the
// manual transport is immediately available.
//==============================================================================
TEST_CASE (intdrum_clear_before_first_join_releases_mode)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 512, rock));
    CHECK (! rig.engine.injectedActive());

    REQUIRE (rig.bridge.requestStopNow());
    rig.block (512);
    CHECK (! rig.engine.injectedActive());
    CHECK (rig.engine.isAudible());

    rig.engine.playing.store (true);
    rig.engine.bpm.store (120.0f);
    rig.engine.barUsed[0].store (true);
    for (int s = 0; s < drum::maxStepsPerBar; ++s)
        rig.engine.pattern[0][drum::kick][s].store (1);
    rig.hits.clear();
    const std::uint64_t start = rig.elapsed;
    rig.render (start + 4096u, 512);
    CHECK (firstHit (rig.hits, kKick) >= 0);
}

//==============================================================================
// A musical StopAtNextBar releases injected mode exactly at its boundary (note
// releases ordered there) and the manual transport resumes.
//==============================================================================
TEST_CASE (intdrum_stop_at_next_bar_releases_mode_and_manual_resumes)
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
        CHECK (hit.sample < 192000u);
    CHECK (countHitsIn (rig.noteOffs, 192000u, 192001u, -1) >= 1);
    CHECK (! rig.engine.injectedActive());
    CHECK (rig.engine.isAudible());

    rig.engine.playing.store (true);
    rig.engine.bpm.store (120.0f);
    rig.engine.barUsed[0].store (true);
    for (int s = 0; s < drum::maxStepsPerBar; ++s)
        rig.engine.pattern[0][drum::kick][s].store (1);
    rig.hits.clear();
    const std::uint64_t start = rig.elapsed;
    rig.render (start + 4096u, 512);
    CHECK (firstHit (rig.hits, kKick) >= 0);
}

//==============================================================================
// INT-LIVE-001 P3: a StopNow after an accepted StopAtNextBar must cancel the
// delayed bar stop at the engine (no stop event / voice flush at the old bar),
// leaving a clean transport for the manual sequencer.
//==============================================================================
TEST_CASE (intdrum_bar_stop_then_clear_cancels_delayed_bar_stop)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 512, rock));
    rig.block (512);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (96000u + 512u, 512);
    CHECK (rig.engine.injectedPlaying());

    REQUIRE (rig.bridge.requestStopAtNextBar()); // queued for 192000
    REQUIRE (rig.bridge.requestStopNow());       // immediate cancel/Clear
    rig.block (512);                              // services both in order

    CHECK (! rig.engine.injectedActive());
    CHECK (! rig.engine.injectedPlaying());
    const std::uint64_t clearAt = rig.lastBlockStart;

    rig.hits.clear();
    rig.noteOffs.clear();
    rig.render (200000u, 512); // well past the cancelled bar boundary

    // The delayed StopAtBar must not be applied at 192000, and no stale voice
    // is released there.
    CHECK_EQ (countHitsIn (rig.noteOffs, 192000u, 192001u, -1),
              static_cast<std::size_t> (0));
    CHECK_EQ (countHitsIn (rig.hits, clearAt, 200000u, -1),
              static_cast<std::size_t> (0));

    // Manual transport resumes with a clean new note.
    rig.engine.playing.store (true);
    rig.engine.bpm.store (120.0f);
    rig.engine.barUsed[0].store (true);
    for (int s = 0; s < drum::maxStepsPerBar; ++s)
        rig.engine.pattern[0][drum::kick][s].store (1);
    rig.hits.clear();
    const std::uint64_t start = rig.elapsed;
    rig.render (start + 4096u, 512);
    CHECK (firstHit (rig.hits, kKick) >= 0);
}

//==============================================================================
// INT-LIVE-001 final: a dropped staged-tempo publish must not drift the engine.
// With the queue full the worker must not latch 150; the bridge and the actual
// engine both stay at 120 across the phantom boundary, and a later accepted 150
// converges at the following bar without disturbing the note phase.
//==============================================================================
TEST_CASE (intdrum_dropped_staged_tempo_cannot_drift_engine)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 512, rock));
    rig.block (512);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (96000u + 512u, 512);
    CHECK (rig.engine.injectedActive());
    CHECK_NEAR (rig.engine.injectedTempo(), 120.0, 0.0);

    // Fill the command queue so the tempo stage is dropped.
    DrumClockCommand filler;
    filler.type = DrumClockCommandType::None;
    while (rig.bridge.commandQueue().push (filler))
    {
    }
    const std::uint64_t dropsBefore = rig.bridge.queueDropCount();
    rig.bridge.applySnapshot (lockedSnapshot (150.0, 100));
    CHECK_EQ (rig.bridge.queueDropCount(), dropsBefore + 1u);
    CHECK_NEAR (rig.bridge.bpm(), 120.0, 0.0); // not latched

    // Cross the boundary the phantom 150 would have landed on: neither the
    // worker grid nor the actual renderer may move.
    const std::uint64_t phantom = rig.bridge.nextBarBoundarySample();
    rig.render (phantom + 4096u, 512);
    CHECK_NEAR (rig.bridge.bpm(), 120.0, 0.0);
    CHECK_NEAR (rig.engine.injectedTempo(), 120.0, 0.0);

    // A later accepted 150 converges at the following bar, and the downbeat
    // lands on the grid (note phase unaffected).
    rig.bridge.applySnapshot (lockedSnapshot (150.0, 200));
    const std::uint64_t b2 = rig.bridge.nextBarBoundarySample();
    rig.render (b2 + 8192u, 512);
    CHECK_NEAR (rig.bridge.bpm(), 150.0, 0.0);
    CHECK_NEAR (rig.engine.injectedTempo(), 150.0, 0.0);
    CHECK (countHitsIn (rig.hits, b2, b2 + 1u, kKick) >= 1);
}

//==============================================================================
// The injected audio callback allocates nothing when the MIDI scratch is
// reserved (measured with the shared ELF wrapping).
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

    rig.bridge.applySnapshot (lockedSnapshot (150.0, 2));

    std::size_t totalAlloc = 0;
    std::size_t totalFree = 0;
    for (int i = 0; i < 4000; ++i)
    {
        rig.bridge.setClockSample (rig.elapsed); // worker side, outside the probe
        drumprobe::beginMeasure();
        rig.engine.process (rig.audio, 512, &rig.sink, rig.midi);
        drumprobe::endMeasure();
        totalAlloc += drumprobe::allocations();
        totalFree += drumprobe::deallocations();
        rig.elapsed += 512;
    }

    CHECK_EQ (totalAlloc, static_cast<std::size_t> (0));
    CHECK_EQ (totalFree, static_cast<std::size_t> (0));
}

// INT-LIVE-001: the stop paths (StopAtBar application and Clear/Cancel) must
// also allocate nothing. The probe above only covered a tempo/join loop.
TEST_CASE (intdrum_stop_callbacks_allocate_nothing)
{
    const LibraryIndex rock = rockGroove();
    REQUIRE (rock >= 0);

    Rig rig;
    REQUIRE (rig.setup (48000.0, 512, rock));
    rig.block (512);
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (96000u + 512u, 512);
    REQUIRE (rig.bridge.requestStopAtNextBar());

    std::size_t alloc = 0;
    std::size_t freeN = 0;
    const std::uint64_t boundary = rig.bridge.nextBarBoundarySample();
    const auto measureBlock = [&] (int n)
    {
        rig.bridge.setClockSample (rig.elapsed); // worker side, outside the probe
        drumprobe::beginMeasure();
        rig.engine.process (rig.audio, n, &rig.sink, rig.midi);
        drumprobe::endMeasure();
        alloc += drumprobe::allocations();
        freeN += drumprobe::deallocations();
        rig.elapsed += static_cast<std::uint64_t> (n);
    };

    // Measure every callback through the StopAtBar application at the boundary.
    while (rig.elapsed <= boundary)
        measureBlock (512);

    // Measure the Clear/Cancel service block.
    REQUIRE (rig.bridge.requestJoinAtNextBar (rock));
    rig.render (rig.elapsed + 1024u, 512);
    REQUIRE (rig.bridge.requestStopNow());
    measureBlock (512);

    CHECK_EQ (alloc, static_cast<std::size_t> (0));
    CHECK_EQ (freeN, static_cast<std::size_t> (0));
}
#endif
