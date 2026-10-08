// DRUM-ADAPT-002 — adaptive pattern change on the REAL DrumEngine.
//
// These drive the actual DrumEngine (internal sampler + hosted MIDI sink) through
// the bounded jam::DrumClockBridge BarChange command, and assert what the
// portable bridge tests cannot:
//
//   * before any adaptation is commanded the injected render is byte-identical
//     to the pre-adaptive single-groove slice;
//   * a groove change lands exactly on the next bar boundary and never mid-bar;
//   * a fill plays exactly one bar and then reverts to the SELECTED groove, with
//     injectedGroove() reporting the selected/base groove throughout and
//     injectedFillPlaying() true only during the fill;
//   * repeated requests for the same bar coalesce (last wins);
//   * intensity scales velocities (and the internal-sampler peak) monotonically;
//   * swing moves off-beat sixteens without moving a downbeat, so the worker and
//     engine stay on one grid;
//   * humanization is bounded and forward-only;
//   * an unprepared index / stale / late command is rejected whole and the
//     current pattern keeps playing (no false accepted state);
//   * Stop / Clear / reset cancel a staged change and release injected mode so
//     the manual transport resumes;
//   * the injected callback allocates nothing with adaptation active.
//
// A no-op MIDI sink measures only this engine; no claim is made about arbitrary
// third-party plugins.

#include "TestHarness.h"

#include "DrumEngine.h"
#include "DrumMidiCapacity.h"
#include "DrumHeapProbe.h"
#include "jam/DrumClockBridge.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#define CHECK_EQ(a, b) CHECK ((a) == (b))
#define CHECK_NEAR(a, b, tol) CHECK (std::abs ((double) (a) - (double) (b)) <= (double) (tol))

namespace
{
using namespace jam;

class MidiSink final : public juce::AudioPluginInstance
{
public:
    const juce::String getName() const override { return "DRUM-ADAPT-002 MIDI sink"; }
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
    std::uint64_t sample = 0;
    std::uint64_t blockStart = 0;
    int note = 0;
    int velocity = 0;
};

// 120 BPM, 48 kHz, 4/4: one sixteenth = 6000 samples, one bar = 96000.
constexpr std::uint64_t kBar = 96000;
constexpr int kStep = 6000;

constexpr int kKick = 36;   // drum::gmNote[drum::kick]
constexpr int kSnare = 38;  // drum::gmNote[drum::snare]
constexpr int kRide = 51;   // drum::gmNote[drum::ride]
constexpr int kCrash = 49;  // drum::gmNote[drum::crash]

// Verified against the actual compiled library (indices are positional and the
// library is read-only): 0 ROCK/Basic, 5 ROCK/80s, 14 ROCK/16 on ride,
// 539 ROCK/Fill rock crescendo. All 4/4; 539 is a fill.
constexpr LibraryIndex kGrooveBasic = 0;
constexpr LibraryIndex kGroove80s = 5;
constexpr LibraryIndex kGrooveRide16 = 14;
constexpr LibraryIndex kFillCrescendo = 539;

const LibraryIndex kBankGrooves[] = { kGrooveBasic, kGroove80s, kGrooveRide16 };
const LibraryIndex kBankFills[] = { kFillCrescendo };

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
    float peakMagnitude = 0.0f;
    bool useGuest = true;

    bool setup (const LibraryIndex* grooves, int numGrooves,
                const LibraryIndex* fills, int numFills,
                double rate = 48000.0, int block = 512)
    {
        engine.prepare (rate, block);
        engine.humanVel.store (0.0f);
        engine.humanTime.store (0.0f);
        engine.humanRR.store (0.0f);
        engine.clickOn.store (false);
        engine.prepareInjectedBank (grooves, numGrooves, fills, numFills);
        engine.attachClockBridge (&bridge.commandQueue(), 0);
        bridge.prepare (rate, block);
        midi.ensureSize (drum::midiScratchBytesForBlock (8192));
        return engine.injectedBankSize() > 0;
    }

    void capture()
    {
        for (const auto event : midi)
        {
            const auto message = event.getMessage();
            if (message.isNoteOn())
                hits.push_back ({ lastBlockStart + (std::uint64_t) event.samplePosition,
                                  lastBlockStart, message.getNoteNumber(),
                                  message.getVelocity() });
            else if (message.isNoteOff())
                noteOffs.push_back ({ lastBlockStart + (std::uint64_t) event.samplePosition,
                                      lastBlockStart, message.getNoteNumber(), 0 });
        }
    }

    void block (int n)
    {
        lastBlockStart = elapsed;
        bridge.setClockSample (elapsed);
        engine.process (audio, n, useGuest ? &sink : nullptr, midi);
        if (! useGuest)
            peakMagnitude = std::max (peakMagnitude, audio.getMagnitude (0, 0, n));
        capture();
        elapsed += (std::uint64_t) n;
    }

    void render (std::uint64_t total, int blockSize = 512)
    {
        while (elapsed < total)
        {
            const int n = static_cast<int> (
                juce::jmin<std::uint64_t> (total - elapsed, (std::uint64_t) blockSize));
            block (n);
        }
    }
};

// Exact-timing adaptive change: intensity neutral (0.5), no swing, no
// humanization. The pattern/sample assertions use this so a change does not
// introduce deliberate jitter.
QueuedBarChange exactChange (LibraryIndex groove = kNoLibraryEntry,
                             LibraryIndex fill = kNoLibraryEntry)
{
    QueuedBarChange change;
    change.groove = groove;
    change.fill = fill;
    change.intensity01 = 0.5f;
    change.swing01 = 0.0f;
    change.humanizeVelocity = 0.0f;
    change.humanizeTiming = 0.0f;
    change.humanizeRoundRobin = 0.0f;
    return change;
}

long firstHit (const std::vector<Hit>& hits, int note)
{
    for (const auto& hit : hits)
        if (hit.note == note)
            return static_cast<long> (hit.sample);
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

bool hasHitExact (const std::vector<Hit>& hits, int note, std::uint64_t sample)
{
    for (const auto& hit : hits)
        if (hit.note == note && hit.sample == sample)
            return true;
    return false;
}

int velocityAt (const std::vector<Hit>& hits, int note, std::uint64_t sample)
{
    for (const auto& hit : hits)
        if (hit.note == note && hit.sample == sample)
            return hit.velocity;
    return -1;
}
} // namespace

//==============================================================================
// Before any adaptation is commanded, the bank path must render byte-identically
// to the pre-adaptive single-groove path. This is the "defaults preserve the
// exact previous Rock slice" gate.
//==============================================================================
TEST_CASE (drumadapt_default_slice_matches_pre_adaptive_path)
{
    std::vector<Hit> bankHits;
    {
        Rig rig;
        REQUIRE (rig.setup (kBankGrooves, 3, kBankFills, 1));
        REQUIRE (rig.bridge.requestJoinAtNextBar (kGrooveBasic));
        rig.render (kBar * 3, 512);
        bankHits = rig.hits;
    }

    std::vector<Hit> legacyHits;
    {
        DrumClockBridge bridge { [] { DrumClockBridgeConfig c; c.initialBpm = 120.0;
                                      c.defaultSampleRate = 48000.0; return c; }() };
        DrumEngine engine;
        MidiSink sink;
        juce::AudioBuffer<float> audio (2, 8192);
        juce::MidiBuffer midi;
        engine.prepare (48000.0, 512);
        engine.humanVel.store (0.0f);
        engine.humanTime.store (0.0f);
        engine.humanRR.store (0.0f);
        engine.clickOn.store (false);
        REQUIRE (engine.prepareInjectedGroove (kGrooveBasic));
        engine.attachClockBridge (&bridge.commandQueue(), 0);
        bridge.prepare (48000.0, 512);
        midi.ensureSize (drum::midiScratchBytesForBlock (8192));
        REQUIRE (bridge.requestJoinAtNextBar (kGrooveBasic));

        std::uint64_t elapsed = 0;
        std::uint64_t blockStart = 0;
        while (elapsed < kBar * 3)
        {
            const int n = (int) juce::jmin<std::uint64_t> (kBar * 3 - elapsed, 512u);
            blockStart = elapsed;
            bridge.setClockSample (elapsed);
            engine.process (audio, n, &sink, midi);
            for (const auto event : midi)
                if (event.getMessage().isNoteOn())
                    legacyHits.push_back ({ blockStart + (std::uint64_t) event.samplePosition,
                                            blockStart, event.getMessage().getNoteNumber(),
                                            event.getMessage().getVelocity() });
            elapsed += (std::uint64_t) n;
        }
    }

    REQUIRE (bankHits.size() == legacyHits.size());
    for (std::size_t i = 0; i < bankHits.size(); ++i)
    {
        CHECK_EQ (bankHits[i].sample, legacyHits[i].sample);
        CHECK_EQ (bankHits[i].note, legacyHits[i].note);
        CHECK_EQ (bankHits[i].velocity, legacyHits[i].velocity);
    }
}

//==============================================================================
// A groove change lands exactly on the next bar boundary: the bar in flight is
// untouched, the first bar after the change uses the new pattern.
//==============================================================================
TEST_CASE (drumadapt_groove_change_lands_exactly_on_next_bar)
{
    Rig rig;
    REQUIRE (rig.setup (kBankGrooves, 3, kBankFills, 1));
    REQUIRE (rig.bridge.requestJoinAtNextBar (kGrooveBasic));
    rig.render (kBar + 4096u, 512); // into bar 2 (starts at 96000)

    // Selected groove starts as Basic.
    CHECK_EQ (rig.engine.injectedSelectedGroove(), kGrooveBasic);

    CHECK (rig.bridge.requestBarChange (exactChange (kGroove80s)));
    CHECK_EQ (rig.bridge.barChangeBoundarySample(), 2 * kBar); // 192000

    rig.render (4 * kBar, 512);

    // Bar 2 [96000,192000) is still Basic: kick on step 10 at 156000.
    CHECK (hasHitExact (rig.hits, kKick, kBar + 10 * kStep)); // 156000
    // Bar 3 [192000,288000) is 80s: step 0 at 192000, step 6 at 228000,
    // step 8 at 240000, and NO kick on Basic's step 10 at 252000.
    CHECK (hasHitExact (rig.hits, kKick, 2 * kBar));
    CHECK (hasHitExact (rig.hits, kKick, 2 * kBar + 6 * kStep));
    CHECK (hasHitExact (rig.hits, kKick, 2 * kBar + 8 * kStep));
    CHECK (! hasHitExact (rig.hits, kKick, 2 * kBar + 10 * kStep));
    CHECK_EQ (rig.engine.injectedSelectedGroove(), kGroove80s);
    CHECK_EQ (rig.engine.injectedGroove(), kGroove80s);
}

//==============================================================================
// A fill plays for exactly one bar, then the SELECTED groove returns. The
// selected/base groove identity is reported throughout; fillPlaying is true only
// during the fill.
//==============================================================================
TEST_CASE (drumadapt_fill_plays_one_bar_then_reverts_to_selected_groove)
{
    Rig rig;
    REQUIRE (rig.setup (kBankGrooves, 3, kBankFills, 1));
    REQUIRE (rig.bridge.requestJoinAtNextBar (kGrooveBasic));
    rig.render (kBar + 4096u, 512); // bar 2

    // Change the selected groove to 80s AND ask for a one-bar fill: the fill
    // must revert to the NEW selected groove, not Basic.
    CHECK (rig.bridge.requestBarChange (exactChange (kGroove80s, kFillCrescendo)));
    CHECK_EQ (rig.bridge.barChangeBoundarySample(), 2 * kBar);

    // Through the fill bar, then one more bar.
    rig.render (2 * kBar + 512u, 512); // inside the fill bar
    CHECK (rig.engine.injectedFillPlaying());
    CHECK_EQ (rig.engine.injectedActiveFill(), kFillCrescendo);
    CHECK_EQ (rig.engine.injectedGroove(), kGroove80s); // base identity stable

    rig.render (4 * kBar, 512);

    // Fill bar [192000,288000): crescendo has snare on every even step.
    CHECK (hasHitExact (rig.hits, kSnare, 2 * kBar));               // step 0
    CHECK (hasHitExact (rig.hits, kSnare, 2 * kBar + 2 * kStep));   // step 2
    CHECK (hasHitExact (rig.hits, kKick, 2 * kBar + 4 * kStep));    // crescendo kick
    CHECK (hasHitExact (rig.hits, kCrash, 2 * kBar));

    // After one bar the fill is gone and 80s (selected) returns: kick on step 6
    // at 3*bar + 6*step, and no snare on the crescendo's even steps.
    CHECK (! rig.engine.injectedFillPlaying());
    CHECK_EQ (rig.engine.injectedActiveFill(), kNoLibraryEntry);
    CHECK_EQ (rig.engine.injectedSelectedGroove(), kGroove80s);
    CHECK (hasHitExact (rig.hits, kKick, 3 * kBar + 6 * kStep));
    CHECK (! hasHitExact (rig.hits, kSnare, 3 * kBar + 2 * kStep));
    CHECK (! hasHitExact (rig.hits, kSnare, 3 * kBar));
}

//==============================================================================
// Integration regression (actual005): a phase correction (resync bar) staged
// BEFORE the join must move the injected downbeat to the correction target. The
// join and any later fill then land on the SAME grid the bridge clock asserts,
// and the fill lasts exactly one bar (the failure observed a 11264-sample fill
// because the join ignored the staged resync and the fill started at engine
// step 14).
//==============================================================================
TEST_CASE (drumadapt_resync_then_fill_is_one_aligned_bar)
{
    Rig rig;
    REQUIRE (rig.setup (kBankGrooves, 3, kBankFills, 1));
    rig.bridge.setClockSample (0);

    // A phase correction to a downbeat that is NOT the pre-correction next bar.
    REQUIRE (rig.bridge.requestResyncNextBar (72000));
    REQUIRE (rig.bridge.requestJoinAtNextBar (kGrooveBasic));
    rig.render (96000u + 1024u, 512);

    // The injected grid re-phased to the resync target: downbeat at 72000, and
    // NO downbeat at the old 96000 (that sample is step 4, a snare, not a kick).
    CHECK (hasHitExact (rig.hits, kKick, 72000));
    CHECK (! hasHitExact (rig.hits, kKick, 96000));

    // A fill for the bridge's next bar must target the post-resync downbeat.
    REQUIRE (rig.bridge.requestFillAtNextBar (kFillCrescendo));
    CHECK_EQ (rig.bridge.barChangeBoundarySample(), static_cast<std::uint64_t> (168000));

    std::uint64_t fillStart = 0, fillEnd = 0;
    bool fill = false, reverted = false;
    while (rig.elapsed < 168000u + 2 * 96000u + 1024u)
    {
        rig.block (512);
        const bool active = rig.engine.injectedFillPlaying();
        if (active && ! fill) fillStart = rig.engine.injectedSamplePosition();
        if (! active && fill && ! reverted) fillEnd = rig.engine.injectedSamplePosition();
        fill = fill || active;
        reverted = reverted || (fill && ! active);
    }

    CHECK (fill);
    CHECK (reverted);
    const long long duration = static_cast<long long> (fillEnd) - static_cast<long long> (fillStart);
    const long long diff = duration - 96000;
    CHECK_MSG (diff <= 512 && diff >= -512,
               "fill duration=" + std::to_string (duration));
    // The fill's crash fires on its downbeat, and the selected groove is intact.
    CHECK (hasHitExact (rig.hits, kCrash, 168000));
    CHECK (! rig.engine.injectedFillPlaying());
    CHECK_EQ (rig.engine.injectedSelectedGroove(), kGrooveBasic);
}

//==============================================================================
// Integration regression (actual005 clock): with the frozen 47*512 = 24064
// sample beat (119.680851 BPM) a one-bar fill must last one bar (~96256) and
// start on a downbeat, within callback observation resolution.
//==============================================================================
TEST_CASE (drumadapt_fill_lasts_one_bar_at_non_120_clock)
{
    Rig rig;
    REQUIRE (rig.setup (kBankGrooves, 3, kBankFills, 1));
    rig.bridge.setClockSample (0);
    rig.bridge.applySnapshot ([] {
        ClockSnapshot s;
        s.bpm = 48000.0 * 60.0 / 24064.0; // 47 taps of 512 -> 119.680851 BPM
        s.generation = 1;
        s.lockState = ClockLockState::Locked;
        return s;
    }());
    REQUIRE (rig.bridge.requestJoinAtNextBar (kGrooveBasic));
    rig.render (2 * 96000u + 1024u, 512);

    // The join carried the staged clock tempo.
    REQUIRE (rig.engine.injectedTempo() > 119.0 && rig.engine.injectedTempo() < 120.0);

    REQUIRE (rig.bridge.requestFillAtNextBar (kFillCrescendo));
    const std::uint64_t target = rig.bridge.barChangeBoundarySample();

    std::uint64_t fillStart = 0, fillEnd = 0;
    bool fill = false, reverted = false;
    while (rig.elapsed < target + 2 * 96000u + 2048u)
    {
        rig.block (512);
        const bool active = rig.engine.injectedFillPlaying();
        if (active && ! fill) fillStart = rig.engine.injectedSamplePosition();
        if (! active && fill && ! reverted) fillEnd = rig.engine.injectedSamplePosition();
        fill = fill || active;
        reverted = reverted || (fill && ! active);
    }

    CHECK (fill);
    CHECK (reverted);
    const long long duration = static_cast<long long> (fillEnd) - static_cast<long long> (fillStart);
    const long long diff = duration - 96000;
    CHECK_MSG (diff <= 1024 && diff >= -1024,
               "fill duration=" + std::to_string (duration));
    // The fill's crash starts the fill bar on the bridge's target downbeat.
    CHECK (hasHitExact (rig.hits, kCrash, target));
    CHECK_EQ (rig.engine.injectedSelectedGroove(), kGrooveBasic);
}

//==============================================================================
// A fill commanded together with swing starts on the (unchanged) downbeat and
// still lasts one bar: the swung intervals sum to the bar, so the reversion is
// exact and the phase is preserved.
//==============================================================================
TEST_CASE (drumadapt_fill_on_swung_grid_is_one_bar)
{
    Rig rig;
    REQUIRE (rig.setup (kBankGrooves, 3, kBankFills, 1));
    REQUIRE (rig.bridge.requestJoinAtNextBar (kGrooveBasic));
    rig.render (kBar + 1024u, 512);

    QueuedBarChange change = exactChange (kGrooveBasic, kFillCrescendo);
    change.swing01 = 1.0f;
    REQUIRE (rig.bridge.requestBarChange (change));
    CHECK_EQ (rig.bridge.barChangeBoundarySample(), 2 * kBar);

    std::uint64_t fillStart = 0, fillEnd = 0;
    bool fill = false, reverted = false;
    while (rig.elapsed < 4 * kBar + 1024u)
    {
        rig.block (512);
        const bool active = rig.engine.injectedFillPlaying();
        if (active && ! fill) fillStart = rig.engine.injectedSamplePosition();
        if (! active && fill && ! reverted) fillEnd = rig.engine.injectedSamplePosition();
        fill = fill || active;
        reverted = reverted || (fill && ! active);
    }

    CHECK (fill);
    CHECK (reverted);
    const long long duration = static_cast<long long> (fillEnd) - static_cast<long long> (fillStart);
    const long long diff = duration - 96000;
    CHECK_MSG (diff <= 1024 && diff >= -1024,
               "swung fill duration=" + std::to_string (duration));
    // The fill's crash starts the fill bar on the downbeat.
    CHECK (hasHitExact (rig.hits, kCrash, 2 * kBar));
    // The next downbeat after the fill is exact (swing sums to one bar).
    CHECK (hasHitExact (rig.hits, kKick, 3 * kBar));
}

//==============================================================================
// Repeated requests for the same bar coalesce; the last one wins.
//==============================================================================
TEST_CASE (drumadapt_coalesced_requests_last_wins)
{
    Rig rig;
    REQUIRE (rig.setup (kBankGrooves, 3, kBankFills, 1));
    REQUIRE (rig.bridge.requestJoinAtNextBar (kGrooveBasic));
    rig.render (kBar + 1024u, 512);

    CHECK (rig.bridge.requestBarChange (exactChange (kGroove80s)));
    CHECK (rig.bridge.requestBarChange (exactChange (kGrooveRide16)));
    CHECK (rig.bridge.requestBarChange (exactChange (kGroove80s))); // last wins

    rig.render (3 * kBar, 512);

    // 80s wins: kick step 6, no ride-on-every-16 (that would be ride-16).
    CHECK (hasHitExact (rig.hits, kKick, 2 * kBar + 6 * kStep));
    CHECK (! hasHitExact (rig.hits, kRide, 2 * kBar + kStep));
    CHECK_EQ (rig.engine.injectedBarChangeCount(), static_cast<std::uint64_t> (1));
}

//==============================================================================
// Intensity scales velocities monotonically (neutral 0.5 == legacy velocity) and
// reshapes the internal-sampler peak.
//==============================================================================
TEST_CASE (drumadapt_intensity_is_monotonic_and_neutral_at_default)
{
    struct Measured { int vel = -1; float peak = 0.0f; };

    // Rig owns atomics and is non-copyable, so only scalars leave the lambda.
    const auto measure = [] (float intensity, bool guest) -> Measured
    {
        Rig rig;
        rig.useGuest = guest;
        rig.setup (kBankGrooves, 3, kBankFills, 1);
        if (! rig.bridge.requestJoinAtNextBar (kGrooveBasic))
            return {};
        rig.render (kBar + 1024u, 512);

        QueuedBarChange change = exactChange (kGrooveBasic);
        change.intensity01 = intensity;
        if (! rig.bridge.requestBarChange (change))
            return {};
        rig.render (2 * kBar + 2048u, 512); // apply the change at 192000
        rig.peakMagnitude = 0.0f;           // measure the adapted bar only
        rig.render (3 * kBar, 512);
        return { velocityAt (rig.hits, kKick, 2 * kBar), rig.peakMagnitude };
    };

    // Baseline: the join itself (no adaptation) renders the legacy velocity on
    // the third-bar downbeat, which is the bar the change applies to.
    int baselineVel = -1;
    {
        Rig rig;
        rig.setup (kBankGrooves, 3, kBankFills, 1);
        if (rig.bridge.requestJoinAtNextBar (kGrooveBasic))
        {
            rig.render (3 * kBar, 512);
            baselineVel = velocityAt (rig.hits, kKick, 2 * kBar);
        }
    }

    const Measured low = measure (0.0f, true);
    const Measured mid = measure (0.5f, true);
    const Measured high = measure (1.0f, true);
    const Measured lowAudio = measure (0.0f, false);
    const Measured highAudio = measure (1.0f, false);

    CHECK (low.vel > 0 && mid.vel > 0 && high.vel > 0);
    CHECK_MSG (low.vel < mid.vel, "low intensity must soften");
    CHECK_MSG (mid.vel <= high.vel, "higher intensity must not be softer");
    CHECK_MSG (mid.vel == baselineVel, "neutral 0.5 must equal the legacy velocity");
    CHECK_MSG (lowAudio.peak < highAudio.peak,
               "intensity must reshape the internal-sampler peak");
}

//==============================================================================
// Swing moves off-beat sixteenths but not a downbeat: the bar sums to the same
// length, so the next downbeat is unmoved and the engine stays on one grid.
//==============================================================================
TEST_CASE (drumadapt_swing_moves_offbeats_not_downbeats)
{
    Rig rig;
    REQUIRE (rig.setup (kBankGrooves, 3, kBankFills, 1));
    REQUIRE (rig.bridge.requestJoinAtNextBar (kGrooveRide16)); // ride on all 16
    rig.render (kBar + 1024u, 512);

    QueuedBarChange change = exactChange (kGrooveRide16);
    change.swing01 = 1.0f; // 60%
    REQUIRE (rig.bridge.requestBarChange (change));
    rig.render (4 * kBar, 512);

    // Straight bar 2: ride on every step, downbeat kick at 96000.
    CHECK (hasHitExact (rig.hits, kRide, kBar + kStep));

    // Swung bar 3 [192000,288000): step 1 ride is pushed later than the straight
    // 6000 whereas the beat-2 snare (step 4) and the next downbeat stay put.
    const std::uint64_t swungStep1 = kBar * 2 + 9240; // 6000 * 1.54
    CHECK_MSG (hasHitExact (rig.hits, kRide, swungStep1),
               "swung step 1 must land at 9240");
    CHECK (! hasHitExact (rig.hits, kRide, kBar * 2 + kStep)); // not the straight spot
    CHECK (hasHitExact (rig.hits, kSnare, kBar * 2 + 4 * kStep)); // beat 2 unmoved
    CHECK (hasHitExact (rig.hits, kKick, kBar * 3));               // next downbeat
}

//==============================================================================
// Humanization timing is bounded micro-jitter around the scheduler grid: it never
// becomes a wholesale pattern change and never moves a hit by more than the
// documented 0.018 s factor. (The pre-existing engine jitters forwards and
// backwards, clamping only at the block origin.)
//==============================================================================
TEST_CASE (drumadapt_humanize_timing_is_bounded_jitter)
{
    Rig rig;
    REQUIRE (rig.setup (kBankGrooves, 3, kBankFills, 1));
    REQUIRE (rig.bridge.requestJoinAtNextBar (kGrooveRide16));
    rig.render (kBar + 1024u, 512);

    QueuedBarChange change = exactChange (kGrooveRide16);
    change.humanizeTiming = 0.5f; // max jitter 0.5 * 0.018 * 48000 = 432 samples
    REQUIRE (rig.bridge.requestBarChange (change));
    rig.render (3 * kBar, 512);

    // Every ride hit in bar 3 must sit within the jitter bound of a straight
    // sixteenth grid position. Recover the grid by rounding to the nearest step.
    const long bound = static_cast<long> (0.5 * 0.018 * 48000.0) + 2;
    int checked = 0;
    for (const auto& hit : rig.hits)
    {
        if (hit.note != kRide || hit.sample < 2 * kBar || hit.sample >= 3 * kBar)
            continue;
        const std::uint64_t rel = hit.sample - 2 * kBar;
        const std::uint64_t grid = ((rel + kStep / 2) / kStep) * kStep;
        const long shift = static_cast<long> (hit.sample) - static_cast<long> (2 * kBar + grid);
        const long magnitude = shift < 0 ? -shift : shift;
        const std::string note = "rel=" + std::to_string (rel) + " grid="
                                 + std::to_string (grid) + " shift=" + std::to_string (shift);
        CHECK_MSG (magnitude <= bound, note);
        ++checked;
    }
    CHECK (checked >= 16);
}

//==============================================================================
// An index that was never prepared is rejected whole by the engine and the
// current pattern keeps playing: no false accepted state.
//==============================================================================
TEST_CASE (drumadapt_unprepared_index_rejected_and_pattern_keeps_playing)
{
    // Bank deliberately omits kGroove80s.
    const LibraryIndex grooves[] = { kGrooveBasic };
    Rig rig;
    REQUIRE (rig.setup (grooves, 1, kBankFills, 1));
    REQUIRE (rig.bridge.requestJoinAtNextBar (kGrooveBasic));
    rig.render (kBar + 1024u, 512);

    // The bridge cannot see the bank, so it accepts structurally; the engine
    // rejects the command and counts it.
    CHECK (rig.bridge.requestBarChange (exactChange (kGroove80s)));
    const std::uint64_t rejectedBefore = rig.engine.injectedRejectedCount();
    rig.render (3 * kBar, 512);

    CHECK (rig.engine.injectedRejectedCount() > rejectedBefore);
    CHECK (rig.engine.injectedStaleCommandCount() >= 1u);
    CHECK_EQ (rig.engine.injectedSelectedGroove(), kGrooveBasic);
    // Basic keeps playing: kick on step 10 of bar 3.
    CHECK (hasHitExact (rig.hits, kKick, 2 * kBar + 10 * kStep));
}

//==============================================================================
// A join and a bar change aimed at the same bar compose: the joined bar renders
// the requested pattern.
//==============================================================================
TEST_CASE (drumadapt_join_and_change_same_bar_compose)
{
    Rig rig;
    REQUIRE (rig.setup (kBankGrooves, 3, kBankFills, 1));

    CHECK (rig.bridge.requestJoinAtNextBar (kGrooveBasic));
    CHECK (rig.bridge.requestBarChange (exactChange (kGroove80s)));
    rig.render (2 * kBar, 512);

    // First joined bar already uses 80s: step 6 kick, no Basic step-10 kick.
    CHECK (hasHitExact (rig.hits, kKick, kBar + 6 * kStep));
    CHECK (! hasHitExact (rig.hits, kKick, kBar + 10 * kStep));
    CHECK_EQ (rig.engine.injectedGroove(), kGroove80s);
}

//==============================================================================
// StopNow cancels a staged fill, releases injected mode and reports no fill; the
// manual transport then resumes.
//==============================================================================
TEST_CASE (drumadapt_stop_now_cancels_fill_and_manual_recovers)
{
    Rig rig;
    REQUIRE (rig.setup (kBankGrooves, 3, kBankFills, 1));
    REQUIRE (rig.bridge.requestJoinAtNextBar (kGrooveBasic));
    rig.render (kBar + 1024u, 512);

    CHECK (rig.bridge.requestBarChange (exactChange (kNoLibraryEntry, kFillCrescendo)));
    CHECK (rig.bridge.barChangePending());
    CHECK (rig.bridge.requestStopNow());
    CHECK (! rig.bridge.barChangePending());

    rig.block (512);
    CHECK (! rig.engine.injectedActive());
    CHECK (! rig.engine.injectedFillPlaying());
    CHECK_EQ (rig.engine.injectedActiveFill(), kNoLibraryEntry);
    CHECK (rig.engine.isAudible());

    // The fill never plays; the manual transport is immediately usable.
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
// A stop at the next bar cancels the adaptive change staged for that same bar.
//==============================================================================
TEST_CASE (drumadapt_stop_at_next_bar_cancels_change)
{
    Rig rig;
    REQUIRE (rig.setup (kBankGrooves, 3, kBankFills, 1));
    REQUIRE (rig.bridge.requestJoinAtNextBar (kGrooveBasic));
    rig.render (kBar + 1024u, 512);

    CHECK (rig.bridge.requestBarChange (exactChange (kGroove80s)));
    CHECK (rig.bridge.barChangePending());
    CHECK (rig.bridge.requestStopAtNextBar());
    CHECK (! rig.bridge.barChangePending());

    rig.render (3 * kBar, 512);
    CHECK (! rig.engine.injectedActive());
    CHECK (! rig.engine.injectedFillPlaying());
    // No kick after the stop boundary, and specifically none from 80s.
    CHECK (! hasHitExact (rig.hits, kKick, 2 * kBar + 6 * kStep));
}

//==============================================================================
// prepareInjectedBank validates meter and kind off the callback.
//==============================================================================
TEST_CASE (drumadapt_bank_rejects_wrong_meter_and_kind)
{
    DrumEngine engine;
    engine.prepare (48000.0, 512);

    // 506 is a 7/8 METAL groove; using it as a groove must be refused. 0 listed
    // as a fill is a kind mismatch and must be refused. 539 is a valid fill.
    const LibraryIndex grooves[] = { kGrooveBasic, 506 };
    const LibraryIndex fills[] = { kGrooveBasic, kFillCrescendo };
    const int accepted = engine.prepareInjectedBank (grooves, 2, fills, 2);

    CHECK_EQ (accepted, 2); // 0 (groove) + 539 (fill)
    CHECK_EQ (engine.injectedBankSize(), 2);
    CHECK_EQ (engine.injectedSelectedGroove(), kGrooveBasic);

    // The compatibility path still refuses a non-4/4 entry.
    CHECK (! engine.prepareInjectedGroove (506));
    CHECK (engine.prepareInjectedGroove (kGroove80s));
}

//==============================================================================
// A re-prepare keeps the prepared bank and selected groove (INT-DRUM-001
// compatibility) and adaptive state restarts clean.
//==============================================================================
TEST_CASE (drumadapt_reprepare_preserves_bank_and_selection)
{
    Rig rig;
    REQUIRE (rig.setup (kBankGrooves, 3, kBankFills, 1));
    REQUIRE (rig.bridge.requestJoinAtNextBar (kGrooveBasic));
    rig.render (kBar + 1024u, 512);
    CHECK (rig.bridge.requestBarChange (exactChange (kGroove80s)));
    rig.render (2 * kBar + 4096u, 512); // apply to 80s
    CHECK_EQ (rig.engine.injectedSelectedGroove(), kGroove80s);

    rig.engine.prepare (96000.0, 512);
    CHECK_EQ (rig.engine.injectedBankSize(), 4);
    CHECK_EQ (rig.engine.injectedSelectedGroove(), kGroove80s);
    CHECK (! rig.engine.injectedAdaptiveActive());

    rig.bridge.prepare (96000.0, 512);
    rig.elapsed = 0;
    rig.hits.clear();
    REQUIRE (rig.bridge.requestJoinAtNextBar (kGroove80s));
    rig.render (3 * kBar, 512); // 96 kHz: one bar = 192000
    CHECK (hasHitExact (rig.hits, kKick, 2 * 96000)); // first joined downbeat
}

//==============================================================================
// A late command (its bar boundary already passed) is rejected rather than
// changing the pattern mid-bar.
//==============================================================================
TEST_CASE (drumadapt_late_bar_change_rejected_no_midbar_switch)
{
    Rig rig;
    REQUIRE (rig.setup (kBankGrooves, 3, kBankFills, 1));
    REQUIRE (rig.bridge.requestJoinAtNextBar (kGrooveBasic));
    rig.render (2 * kBar + 4096u, 512); // now in bar 3

    // Hand the engine a BarChange aimed at a boundary already in the past.
    DrumClockCommand late;
    late.type = DrumClockCommandType::BarChange;
    late.sampleTime = kBar; // 96000, long passed
    late.groove = kGroove80s;
    late.changeFields = static_cast<std::uint8_t> (
        DrumChangeField::Groove | DrumChangeField::Params);
    late.intensity01 = 0.5f;
    late.humanizeVelocity = 0.0f;
    late.humanizeTiming = 0.0f;
    late.humanizeRoundRobin = 0.0f;
    rig.bridge.commandQueue().push (late);

    const std::uint64_t rejectedBefore = rig.engine.injectedRejectedCount();
    rig.render (4 * kBar, 512);

    CHECK (rig.engine.injectedRejectedCount() > rejectedBefore);
    CHECK_EQ (rig.engine.injectedSelectedGroove(), kGrooveBasic); // no mid-bar switch
}

//==============================================================================
// Integration fix (six-style catalogue): the fixed bounded bank must hold the
// completed catalogue's distinct indices with headroom. Capacity is 128 inline
// slots; a bank far larger than the old 16 must prepare and play, and the
// callback must stay allocation-free while a high bank slot is selected and a
// high-slot fill plays one bar and reverts.
//==============================================================================
TEST_CASE (drumadapt_large_bank_capacity_and_callback_bounded)
{
    // Mirror the parent's quiescent catalogue preparation: collect the actual
    // 4/4 library entries, reserving fill slots, up to the fixed capacity.
    DrumEngine capacityProbe;
    const int cap = capacityProbe.injectedBankCapacity();
    CHECK (cap > 16);

    std::vector<LibraryIndex> allGrooves, allFills;
    for (std::size_t i = 0; i < drum::library().size(); ++i)
    {
        const auto& g = drum::library()[i];
        if (g.num != 4 || g.den != 4)
            continue;
        (g.fill ? allFills : allGrooves).push_back (static_cast<LibraryIndex> (i));
    }
    REQUIRE (static_cast<int> (allGrooves.size()) >= cap - 4);
    REQUIRE (allFills.size() >= 4u);

    const int fillReserve = 4;
    std::vector<LibraryIndex> grooves, fills;
    for (auto idx : allGrooves)
    {
        if (static_cast<int> (grooves.size() + fills.size()) >= cap - fillReserve)
            break;
        grooves.push_back (idx);
    }
    for (auto idx : allFills)
    {
        if (static_cast<int> (grooves.size() + fills.size()) >= cap)
            break;
        fills.push_back (idx);
    }

    Rig rig;
    REQUIRE (rig.setup (grooves.data(), static_cast<int> (grooves.size()),
                        fills.data(), static_cast<int> (fills.size())));
    CHECK_EQ (rig.engine.injectedBankSize(), cap); // the whole bank fit
    CHECK_EQ (rig.engine.injectedBankCapacity(), cap);

    const LibraryIndex highGroove = grooves.back(); // a slot well beyond 16
    const LibraryIndex highFill = fills.back();     // the last bank slot

    REQUIRE (rig.bridge.requestJoinAtNextBar (grooves.front()));
    rig.render (kBar + 1024u, 512); // bar 2
    CHECK_EQ (rig.engine.injectedSelectedGroove(), grooves.front());

    CHECK (rig.bridge.requestBarChange (exactChange (highGroove, highFill)));
    CHECK_EQ (rig.bridge.barChangeBoundarySample(), 2 * kBar);

#if defined(DRUM_MIDI_HEAP_PROBE)
    // Bounded, allocation-free callback scan across the change boundary, the
    // one-bar fill and its reversion to the high-slot selected groove.
    std::size_t alloc = 0;
    std::size_t freeN = 0;
    while (rig.elapsed < 4 * kBar)
    {
        const int n = static_cast<int> (
            juce::jmin<std::uint64_t> (4 * kBar - rig.elapsed, 512u));
        rig.bridge.setClockSample (rig.elapsed); // worker side, outside the probe
        drumprobe::beginMeasure();
        rig.engine.process (rig.audio, n, &rig.sink, rig.midi);
        drumprobe::endMeasure();
        alloc += drumprobe::allocations();
        freeN += drumprobe::deallocations();
        rig.elapsed += static_cast<std::uint64_t> (n);
    }
    CHECK_EQ (alloc, static_cast<std::size_t> (0));
    CHECK_EQ (freeN, static_cast<std::size_t> (0));
#else
    rig.render (4 * kBar, 512);
#endif

    CHECK_EQ (rig.engine.injectedSelectedGroove(), highGroove);
    CHECK (! rig.engine.injectedFillPlaying());
    CHECK_EQ (rig.engine.injectedActiveFill(), kNoLibraryEntry);
    CHECK_EQ (rig.engine.injectedBarChangeCount(), static_cast<std::uint64_t> (1));
    CHECK (rig.engine.injectedStepsFired() > 0u);
}

//==============================================================================
#if defined(DRUM_MIDI_HEAP_PROBE)
// The injected callback must allocate nothing with a bank, a groove change and a
// one-bar fill all in flight.
TEST_CASE (drumadapt_adaptive_callback_allocates_nothing)
{
    Rig rig;
    REQUIRE (rig.setup (kBankGrooves, 3, kBankFills, 1));
    REQUIRE (rig.bridge.requestJoinAtNextBar (kGrooveBasic));
    rig.render (kBar + 1024u, 512);

    CHECK (rig.bridge.requestBarChange (exactChange (kGroove80s, kFillCrescendo)));
    rig.bridge.applySnapshot ([] { ClockSnapshot s; s.bpm = 132.0; s.generation = 1;
                                   s.lockState = ClockLockState::Locked; return s; }());

    std::size_t alloc = 0;
    std::size_t freeN = 0;
    for (int i = 0; i < 4000; ++i)
    {
        rig.bridge.setClockSample (rig.elapsed); // worker side, outside the probe
        drumprobe::beginMeasure();
        rig.engine.process (rig.audio, 512, &rig.sink, rig.midi);
        drumprobe::endMeasure();
        alloc += drumprobe::allocations();
        freeN += drumprobe::deallocations();
        rig.elapsed += 512;
    }

    CHECK_EQ (alloc, static_cast<std::size_t> (0));
    CHECK_EQ (freeN, static_cast<std::size_t> (0));
}
#endif
