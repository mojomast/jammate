// Deterministic synthetic tests for the Musical Clock.
//
// These tests exist to prove the musical properties the product depends on:
// the clock, not the tracker, owns tempo; it acquires, holds, loses and
// recovers; it resists isolated syncopation; it follows gradual drift; and its
// output is reproducible. SPEC.md 19 and 21.2, DEVPLAN CLOCK-001.
//
// Everything here is driven by sample time. There is no wall-clock, no
// sleeping, and the only randomness is a self-contained LCG seeded by the test.

#include "JamTest.h"

#include "jam/JamConfig.h"
#include "jam/MusicalClock.h"
#include "jam/RhythmTypes.h"

#include <cmath>
#include <string>
#include <vector>

namespace
{

using namespace jam;

constexpr double kSr = 48000.0;

// Small deterministic LCG so "noise" is identical on every machine and run.
struct Lcg
{
    uint32_t state;
    explicit Lcg (uint32_t seed) : state (seed) {}
    uint32_t next()
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }
    double uniform01() { return static_cast<double> (next()) / 4294967296.0; }
    double symmetric() { return uniform01() * 2.0 - 1.0; }
};

RhythmObservation makeBeat (uint64_t sampleTime, float bpm, float phase = 0.0f,
                            float confidence = 1.0f)
{
    RhythmObservation observation;
    observation.inputSampleTime = sampleTime;
    observation.sourceSampleRate = kSr;
    observation.bpmCandidate = bpm;
    observation.beatPhase01 = phase;
    observation.beatConfidence01 = confidence;
    observation.onsetStrength01 = 1.0f;
    observation.energyRmsDbfs = -18.0f;
    observation.transientDensity01 = 0.5f;
    observation.beatEvent = true;
    observation.silence = false;
    observation.phaseValid = true;
    return observation;
}

RhythmObservation makeSilence (uint64_t sampleTime)
{
    RhythmObservation observation;
    observation.inputSampleTime = sampleTime;
    observation.sourceSampleRate = kSr;
    observation.bpmCandidate = 0.0f;
    observation.beatPhase01 = 0.0f;
    observation.beatConfidence01 = 0.0f;
    observation.onsetStrength01 = 0.0f;
    observation.energyRmsDbfs = -90.0f;
    observation.transientDensity01 = 0.0f;
    observation.beatEvent = false;
    observation.silence = true;
    observation.phaseValid = false;
    return observation;
}

// Drives a clock with synthetic musical time. Optional `log` makes every
// mutating step append a snapshot line, which the determinism test compares.
struct Driver
{
    MusicalClock clock;
    uint64_t t = 0;
    double timeSeconds = 0.0;
    std::vector<std::string>* log = nullptr;

    explicit Driver (const ClockConfig& config = ClockConfig()) : clock (config) {}

    std::string line() const
    {
        const ClockSnapshot s = clock.snapshot();
        return std::to_string (static_cast<unsigned long long> (s.generation)) + "|"
             + std::to_string (s.bpm) + "|"
             + std::to_string (s.beatPhase01) + "|"
             + std::to_string (s.barPhase01) + "|"
             + std::to_string (static_cast<int> (s.lockState)) + "|"
             + std::to_string (s.beatInBar) + "|"
             + std::to_string (static_cast<double> (s.confidence01));
    }

    void record()
    {
        if (log != nullptr)
            log->push_back (line());
    }

    void advanceTo (double seconds)
    {
        if (seconds <= timeSeconds)
            return;
        const uint64_t target = static_cast<uint64_t> (std::llround (seconds * kSr));
        if (target <= t)
        {
            timeSeconds = seconds;
            return;
        }
        clock.advance (target - t, kSr);
        t = target;
        timeSeconds = seconds;
        record();
    }

    void observeBeat (float bpm, float phase = 0.0f, float confidence = 1.0f)
    {
        clock.observe (makeBeat (t, bpm, phase, confidence));
        record();
    }

    void observeSilence()
    {
        clock.observe (makeSilence (t));
        record();
    }
};

// Emits steady beats at `bpm` until `untilSeconds`. `dropEvery` > 0 skips the
// observation but still advances time, modelling a missed attack.
void feedSteady (Driver& driver, float bpm, double untilSeconds,
                 float confidence = 1.0f, int dropEvery = 0)
{
    const double period = 60.0 / bpm;
    int index = 0;
    double next = driver.timeSeconds;

    while (true)
    {
        next += period;
        if (next > untilSeconds + 1e-9)
            break;

        ++index;
        driver.advanceTo (next);
        if (dropEvery > 0 && (index % dropEvery) == 0)
            continue;
        driver.observeBeat (bpm, 0.0f, confidence);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// 1. Perfect 120 BPM
// ---------------------------------------------------------------------------
JAM_TEST (MusicalClock, perfect120)
{
    // Musical property: a clean 120 BPM pulse is acquired and the published
    // tempo is the clock's own smoothed belief, within 1% of truth.
    Driver driver;
    feedSteady (driver, 120.0f, 12.0);

    const ClockSnapshot snapshot = driver.clock.snapshot();
    CHECK (snapshot.lockState == ClockLockState::Locked);
    CHECK_NEAR (snapshot.bpm, 120.0, 1.2);            // 1%
    CHECK_GE (snapshot.confidence01, 0.65f);          // lock threshold
    CHECK_NEAR (snapshot.beatsPerBar, 4, 0);
}

// ---------------------------------------------------------------------------
// 2. Noisy 120 BPM
// ---------------------------------------------------------------------------
JAM_TEST (MusicalClock, noisy120)
{
    // Musical property: jittered evidence still locks on the true tempo
    // (SPEC 19: locked BPM relative error <= 2%).
    Driver driver;
    Lcg rng (0x1234ABCDu);

    const double period = 0.5;
    double next = driver.timeSeconds;
    while (next + period <= 14.0)
    {
        next += period;
        driver.advanceTo (next);
        const double jitter = rng.symmetric() * 0.01;         // +/-1%
        const float bpm = static_cast<float> (120.0 * (1.0 + jitter));
        const float phase = static_cast<float> (rng.symmetric() * 0.02);
        driver.clock.observe (makeBeat (driver.t, bpm, phase, 0.9f));
    }

    const ClockSnapshot snapshot = driver.clock.snapshot();
    CHECK (snapshot.lockState == ClockLockState::Locked);
    CHECK_NEAR (snapshot.bpm, 120.0, 2.4);            // 2%
}

// ---------------------------------------------------------------------------
// 3. Extra offbeat onsets (syncopation)
// ---------------------------------------------------------------------------
JAM_TEST (MusicalClock, extraOffbeatOnsets)
{
    // Musical property: one isolated syncopated event must not move the
    // published tempo (SPEC 19: "no tempo jump from one isolated syncopated
    // event"). The extra onset also carries a metric-twin candidate.
    Driver driver;
    feedSteady (driver, 120.0f, 10.0);

    REQUIRE (driver.clock.snapshot().lockState == ClockLockState::Locked);
    const double before = driver.clock.snapshot().bpm;

    double nextMain = driver.timeSeconds;
    for (int i = 0; i < 16; ++i)
    {
        nextMain += 0.5;
        driver.advanceTo (nextMain);
        driver.observeBeat (120.0f, 0.0f, 0.9f);

        if ((i % 2) == 0)
        {
            driver.advanceTo (nextMain + 0.25);
            driver.observeBeat (240.0f, 0.5f, 0.3f);   // offbeat, 2x candidate
        }
    }

    const ClockSnapshot snapshot = driver.clock.snapshot();
    CHECK (snapshot.lockState == ClockLockState::Locked);
    CHECK_NEAR (snapshot.bpm, before, 1.5);
}

// ---------------------------------------------------------------------------
// 4. Missing beats
// ---------------------------------------------------------------------------
JAM_TEST (MusicalClock, missingBeats)
{
    // Musical property: dropped attacks do not make the clock drift or fall
    // out of lock; it free-runs on its own grid.
    Driver driver;
    feedSteady (driver, 120.0f, 14.0, 1.0f, 4);   // drop every fourth beat

    const ClockSnapshot snapshot = driver.clock.snapshot();
    CHECK (snapshot.lockState == ClockLockState::Locked);
    CHECK_NEAR (snapshot.bpm, 120.0, 2.4);

    const double phaseBefore = snapshot.beatPhase01;
    driver.advanceTo (driver.timeSeconds + 0.2);   // 0.4 of a beat, off-grid
    const double phaseAfter = driver.clock.snapshot().beatPhase01;
    CHECK (phaseAfter >= 0.0);
    CHECK (phaseAfter < 1.0);
    CHECK (phaseAfter != phaseBefore);
}

// ---------------------------------------------------------------------------
// 5. 120 -> 130 BPM gradual ramp
// ---------------------------------------------------------------------------
JAM_TEST (MusicalClock, ramp120to130)
{
    // Musical property: Follow tracks a controlled gradual tempo ramp with
    // bounded slew and no abrupt step (SPEC 19), and converges on the new
    // tempo once it is held.
    Driver driver;
    feedSteady (driver, 120.0f, 10.0);
    REQUIRE (driver.clock.snapshot().lockState == ClockLockState::Locked);

    const double maxAllowed =
        static_cast<double> (ClockConfig().maxSlewRatioPerObservation[
            static_cast<int> (TempoMode::Follow)]);

    double previous = driver.clock.snapshot().bpm;
    double maxStep = 0.0;
    const int rampBeats = 20;

    for (int i = 1; i <= rampBeats; ++i)
    {
        const double bpm = 120.0 + 10.0 * (static_cast<double> (i) / rampBeats);
        const double period = 60.0 / bpm;
        driver.advanceTo (driver.timeSeconds + period);
        driver.observeBeat (static_cast<float> (bpm), 0.0f, 1.0f);

        const double now = driver.clock.snapshot().bpm;
        maxStep = std::max (maxStep, std::fabs (now - previous) / previous);
        previous = now;
    }

    feedSteady (driver, 130.0f, driver.timeSeconds + 12.0);

    CHECK_LE (maxStep, maxAllowed + 1e-6);
    CHECK_NEAR (driver.clock.snapshot().bpm, 130.0, 2.6);   // 2%
}

// ---------------------------------------------------------------------------
// 6. Half-time candidate
// ---------------------------------------------------------------------------
JAM_TEST (MusicalClock, halfTimeCandidate)
{
    // Musical property: the half-time reading of the same performance must not
    // be adopted on sight; sustained confirmation may switch the belief.
    Driver driver;
    feedSteady (driver, 120.0f, 10.0);
    REQUIRE (driver.clock.snapshot().lockState == ClockLockState::Locked);

    const double before = driver.clock.snapshot().bpm;
    CHECK_NEAR (before, 120.0, 2.4);

    for (int i = 0; i < 3; ++i)                       // below confirmation count
    {
        driver.advanceTo (driver.timeSeconds + 1.0);
        driver.observeBeat (60.0f, 0.0f, 0.9f);
    }
    CHECK_NEAR (driver.clock.snapshot().bpm, before, 2.4);   // still 120

    for (int i = 0; i < 4; ++i)                       // crosses confirmation count
    {
        driver.advanceTo (driver.timeSeconds + 1.0);
        driver.observeBeat (60.0f, 0.0f, 0.9f);
    }
    CHECK (driver.clock.snapshot().lockState == ClockLockState::Locked);
    CHECK_NEAR (driver.clock.snapshot().bpm, 60.0, 1.5);
}

// ---------------------------------------------------------------------------
// 7. Double-time candidate
// ---------------------------------------------------------------------------
JAM_TEST (MusicalClock, doubleTimeCandidate)
{
    // Musical property: mirror of the half-time case at a lower base tempo so
    // the doubled value stays inside the auto-follow range.
    Driver driver;
    feedSteady (driver, 100.0f, 10.0);
    REQUIRE (driver.clock.snapshot().lockState == ClockLockState::Locked);

    const double before = driver.clock.snapshot().bpm;
    CHECK_NEAR (before, 100.0, 2.4);

    for (int i = 0; i < 3; ++i)
    {
        driver.advanceTo (driver.timeSeconds + 0.3);
        driver.observeBeat (200.0f, 0.0f, 0.9f);
    }
    CHECK_NEAR (driver.clock.snapshot().bpm, before, 2.4);   // still 100

    for (int i = 0; i < 4; ++i)
    {
        driver.advanceTo (driver.timeSeconds + 0.3);
        driver.observeBeat (200.0f, 0.0f, 0.9f);
    }
    CHECK (driver.clock.snapshot().lockState == ClockLockState::Locked);
    CHECK_NEAR (driver.clock.snapshot().bpm, 200.0, 3.0);
}

// ---------------------------------------------------------------------------
// 8. Two bars of silence
// ---------------------------------------------------------------------------
JAM_TEST (MusicalClock, twoBarSilence)
{
    // Musical property: short silence puts the clock into Holdover, where it
    // keeps a coherent advancing grid and retains confidence above zero.
    Driver driver;
    feedSteady (driver, 120.0f, 10.0);
    REQUIRE (driver.clock.snapshot().lockState == ClockLockState::Locked);

    const double phaseAtStart = driver.clock.snapshot().beatPhase01;
    bool phaseAdvanced = false;

    for (int i = 0; i < 16; ++i)                      // 4 s == two bars at 120
    {
        driver.advanceTo (driver.timeSeconds + 0.25);
        const double phase = driver.clock.snapshot().beatPhase01;
        CHECK (phase >= 0.0);
        CHECK (phase < 1.0);
        if (std::fabs (phase - phaseAtStart) > 1e-9)
            phaseAdvanced = true;
        driver.observeSilence();
    }

    const ClockSnapshot snapshot = driver.clock.snapshot();
    CHECK (snapshot.lockState == ClockLockState::Holdover);
    CHECK (snapshot.confidence01 > 0.0f);
    CHECK (snapshot.confidence01 <= 1.0f);
    CHECK (phaseAdvanced);
    CHECK_NEAR (snapshot.bpm, 120.0, 2.4);
}

// ---------------------------------------------------------------------------
// 9. Prolonged silence
// ---------------------------------------------------------------------------
JAM_TEST (MusicalClock, prolongedSilence)
{
    // Musical property: long silence stops phase being trusted and reports
    // Lost, from which a new lock can form.
    Driver driver;
    feedSteady (driver, 120.0f, 10.0);
    REQUIRE (driver.clock.snapshot().lockState == ClockLockState::Locked);

    for (int i = 0; i < 16; ++i)                      // 8 s of silence
    {
        driver.advanceTo (driver.timeSeconds + 0.5);
        driver.observeSilence();
    }

    CHECK (driver.clock.snapshot().lockState == ClockLockState::Lost);
}

// ---------------------------------------------------------------------------
// 10. Explicit resync
// ---------------------------------------------------------------------------
JAM_TEST (MusicalClock, explicitResync)
{
    // Musical property: ResyncNextBeat lands a beat boundary exactly on the
    // requested sample time, and ResyncNextBar lands a downbeat (SPEC 19).
    {
        Driver driver;
        feedSteady (driver, 120.0f, 10.0);
        REQUIRE (driver.clock.snapshot().lockState == ClockLockState::Locked);

        driver.advanceTo (driver.timeSeconds + 0.17);        // knock phase off
        const uint64_t target = driver.t + 12000;            // ~0.25 s ahead

        ClockCommand command;
        command.type = ClockCommandType::ResyncNextBeat;
        command.tapSampleTime = target;
        driver.clock.command (command);

        driver.advanceTo (static_cast<double> (target) / kSr);
        CHECK_NEAR (driver.clock.snapshot().beatPhase01, 0.0, 0.01);
    }

    {
        Driver driver;
        feedSteady (driver, 120.0f, 10.0);
        REQUIRE (driver.clock.snapshot().lockState == ClockLockState::Locked);

        const uint64_t target = driver.t + 9000;
        ClockCommand command;
        command.type = ClockCommandType::ResyncNextBar;
        command.tapSampleTime = target;
        driver.clock.command (command);

        driver.advanceTo (static_cast<double> (target) / kSr);
        CHECK_NEAR (driver.clock.snapshot().barPhase01, 0.0, 0.01);
        CHECK_EQ (driver.clock.snapshot().beatInBar, 1);
    }
}

// ---------------------------------------------------------------------------
// 11. Freeze / unfreeze
// ---------------------------------------------------------------------------
JAM_TEST (MusicalClock, freezeAndUnfreeze)
{
    // Musical property: FreezeTempo pins the BPM against tracker evidence;
    // ResumeFollow lets the clock follow again.
    Driver driver;
    feedSteady (driver, 120.0f, 10.0);
    REQUIRE (driver.clock.snapshot().lockState == ClockLockState::Locked);

    ClockCommand freeze;
    freeze.type = ClockCommandType::FreezeTempo;
    driver.clock.command (freeze);
    CHECK (driver.clock.snapshot().tempoFrozen);

    const double frozen = driver.clock.snapshot().bpm;

    for (int i = 1; i <= 10; ++i)                     // evidence climbs to 150
    {
        const double bpm = 120.0 + 3.0 * i;
        driver.advanceTo (driver.timeSeconds + 60.0 / bpm);
        driver.observeBeat (static_cast<float> (bpm), 0.0f, 1.0f);
    }
    CHECK_NEAR (driver.clock.snapshot().bpm, frozen, 0.01);

    ClockCommand resume;
    resume.type = ClockCommandType::ResumeFollow;
    driver.clock.command (resume);
    CHECK (! driver.clock.snapshot().tempoFrozen);

    feedSteady (driver, 150.0f, driver.timeSeconds + 12.0);
    CHECK_GE (driver.clock.snapshot().bpm, frozen + 5.0);
}

// ---------------------------------------------------------------------------
// 12. Tap tempo
// ---------------------------------------------------------------------------
JAM_TEST (MusicalClock, tapTempo)
{
    // Musical property: a human tap sequence establishes tempo and phase
    // immediately, without any tracker evidence (SPEC 5.6/10.2).
    MusicalClock clock;

    uint64_t tap = 0;
    for (int i = 0; i < 4; ++i)
    {
        tap += static_cast<uint64_t> (std::llround (0.5 * kSr));   // 120 BPM
        ClockCommand command;
        command.type = ClockCommandType::TapTempo;
        command.tapSampleTime = tap;
        clock.command (command);
    }

    CHECK (clock.snapshot().lockState == ClockLockState::Locked);
    CHECK_NEAR (clock.snapshot().bpm, 120.0, 1.2);
    CHECK_NEAR (clock.snapshot().beatPhase01, 0.0, 0.01);
}

// ---------------------------------------------------------------------------
// 13. Fixed vs Follow vs Loose
// ---------------------------------------------------------------------------
JAM_TEST (MusicalClock, modesDifferInReactivity)
{
    // Musical property: on the same evidence a step below the jump-rejection
    // threshold moves Fixed not at all, Follow by the full slew allowance and
    // Loose by much less (SPEC 19: "Loose Follow is measurably less reactive
    // than Follow").
    const auto stepDelta = [] (TempoMode mode) -> double
    {
        Driver driver;
        driver.clock.setMode (mode);
        feedSteady (driver, 120.0f, 10.0);

        const double before = driver.clock.snapshot().bpm;
        driver.advanceTo (driver.timeSeconds + 60.0 / 128.0);
        driver.observeBeat (128.0f, 0.0f, 1.0f);      // +6.7%, below rejection
        return driver.clock.snapshot().bpm - before;
    };

    const double fixedDelta = stepDelta (TempoMode::Fixed);
    const double followDelta = stepDelta (TempoMode::Follow);
    const double looseDelta = stepDelta (TempoMode::Loose);

    CHECK_NEAR (fixedDelta, 0.0, 1e-9);
    CHECK (followDelta > 0.0);
    CHECK (looseDelta > 0.0);
    CHECK (followDelta > looseDelta * 3.0);           // measurably more reactive
}

// ---------------------------------------------------------------------------
// 14. Manual half / double correction
// ---------------------------------------------------------------------------
JAM_TEST (MusicalClock, manualHalfDouble)
{
    // Musical property: the half-time and double-time buttons take effect
    // immediately, unlike the passive metric-candidate tracking.
    Driver driver;
    feedSteady (driver, 120.0f, 10.0);
    REQUIRE (driver.clock.snapshot().lockState == ClockLockState::Locked);

    ClockCommand half;
    half.type = ClockCommandType::HalfTime;
    driver.clock.command (half);
    CHECK_NEAR (driver.clock.snapshot().bpm, 60.0, 1.2);

    ClockCommand twice;
    twice.type = ClockCommandType::DoubleTime;
    driver.clock.command (twice);
    CHECK_NEAR (driver.clock.snapshot().bpm, 120.0, 1.2);
}

// ---------------------------------------------------------------------------
// 15. Determinism
// ---------------------------------------------------------------------------
JAM_TEST (MusicalClock, deterministic)
{
    // Musical property: identical evidence produces an identical snapshot
    // sequence, bit for bit. Required by DEVPLAN 21.2.
    const auto runScenario = [] (int which) -> std::vector<std::string>
    {
        Driver driver;
        std::vector<std::string> log;
        driver.log = &log;

        if (which == 1)
        {
            feedSteady (driver, 120.0f, 12.0);
        }
        else if (which == 5)
        {
            feedSteady (driver, 120.0f, 10.0);
            const int rampBeats = 20;
            for (int i = 1; i <= rampBeats; ++i)
            {
                const double bpm = 120.0 + 10.0 * (static_cast<double> (i) / rampBeats);
                driver.advanceTo (driver.timeSeconds + 60.0 / bpm);
                driver.observeBeat (static_cast<float> (bpm), 0.0f, 1.0f);
            }
            feedSteady (driver, 130.0f, driver.timeSeconds + 6.0);
        }
        else
        {
            feedSteady (driver, 120.0f, 10.0);
            for (int i = 0; i < 8; ++i)
            {
                driver.advanceTo (driver.timeSeconds + 0.5);
                driver.observeSilence();
            }
        }
        return log;
    };

    CHECK (runScenario (1) == runScenario (1));
    CHECK (runScenario (5) == runScenario (5));
    CHECK (runScenario (8) == runScenario (8));
}

// ---------------------------------------------------------------------------
// 16. No raw bypass
// ---------------------------------------------------------------------------
JAM_TEST (MusicalClock, singleWildObservationDoesNotMoveTempo)
{
    // Musical property: no tracker bpmCandidate is ever written straight
    // through. One wild observation (3x the belief) must not move the
    // published tempo.
    Driver driver;
    feedSteady (driver, 120.0f, 10.0);
    REQUIRE (driver.clock.snapshot().lockState == ClockLockState::Locked);

    const double before = driver.clock.snapshot().bpm;

    driver.advanceTo (driver.timeSeconds + 0.5);
    driver.observeBeat (360.0f, 0.0f, 1.0f);

    CHECK_NEAR (driver.clock.snapshot().bpm, before, 0.01);
    CHECK (driver.clock.snapshot().lockState == ClockLockState::Locked);
}
