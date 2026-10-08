// Unit tests for jam::JamDirector (DEVPLAN DIRECTOR-001, SPEC.md 13.3/14/15).
//
// The director is a pure, deterministic, bounded policy object: no threads, no
// time source, no audio. These tests pin the behaviours the integration depends
// on:
//   - it does nothing until listening and only proposes from a usable lock;
//   - one pending proposal is retried unchanged until the caller acknowledges
//     it, and selection/repetition state advances only on acceptance;
//   - Holdover/Lost suppress fills and freeze changes;
//   - a session generation change resets stale musical state;
//   - energy is a gradual envelope, not a switch;
//   - no immediate groove repeat where alternatives exist;
//   - invalid settings are clamped, and identical seeds reproduce decisions.

#include "JamTest.h"

#include "jam/JamDirector.h"

#include <cmath>
#include <cstdint>

namespace
{

using namespace jam;

/** Deterministic synthetic rig: advances a bar phase and builds snapshots. */
struct Rig
{
    JamDirector director;
    std::uint64_t generation = 0;
    double phase = 0.0;   // bar phase in [0, 1)
    ClockLockState lock = ClockLockState::Locked;
    float confidence = 0.9f;
    bool echo = false;
    float energy = 0.5f;
    std::uint64_t session = 1;
    std::uint64_t cursor = 0;
    bool discontinuity = false;
    bool lifecycleAllows = true;

    DirectorDecision tick (double delta = 0.0, bool onset = false,
                           float onsetStrength = 0.0f, bool silence = false)
    {
        phase += delta;
        while (phase >= 1.0)
            phase -= 1.0;

        DirectorInputs in;
        in.sessionGeneration = session;
        in.audioCursor = cursor;
        in.discontinuity = discontinuity;
        in.clock.generation = ++generation;
        in.clock.bpm = 120.0;
        in.clock.beatPhase01 = 0.0;
        in.clock.barPhase01 = phase;
        in.clock.beatInBar = 1;
        in.clock.beatsPerBar = 4;
        in.clock.beatUnit = 4;
        in.clock.confidence01 = confidence;
        in.clock.lockState = lock;
        in.playbackEchoPlaying = echo;
        in.lifecycleAllowsPerformance = lifecycleAllows;
        in.energy01 = energy;
        in.onsetStrength01 = onsetStrength;
        in.onsetEvent = onset;
        in.silence = silence;
        return director.update (in);
    }

    /** Advance exactly one bar (four quarter-bar ticks). */
    DirectorDecision bar (bool onset = false, float onsetStrength = 0.0f)
    {
        DirectorDecision d;
        for (int i = 0; i < 4; ++i)
            d = tick (0.25, onset && i == 0, onsetStrength);
        return d;
    }
};

/** Reach Playing with a committed initial groove. The director only proposes
    once the audio-owner echo confirms playback, matching the live integration
    gate (policy requestedRunning && !stopPending && echo engaged). */
void toPlaying (Rig& rig)
{
    rig.director.notifySessionStarted();
    rig.echo = true;
    const DirectorDecision ready = rig.tick (0.0);
    CHECK (ready.state == DirectorState::ReadyToJoin);
    CHECK (! ready.hasBarChange);

    const DirectorDecision playing = rig.tick (0.0);
    REQUIRE (playing.state == DirectorState::Playing);
    REQUIRE (playing.hasBarChange);
    rig.director.acknowledgePublication (true);
    CHECK (rig.director.committedGroove() != kNoLibraryEntry);
}

} // namespace

JAM_TEST (jamdirector, idleDoesNothing)
{
    Rig rig;
    const DirectorDecision d = rig.tick (0.25);
    CHECK (d.state == DirectorState::Idle);
    CHECK (! d.hasBarChange);
    CHECK (rig.director.committedGroove() == kNoLibraryEntry);
}

JAM_TEST (jamdirector, noProposalWhileAcquiring)
{
    Rig rig;
    rig.lock = ClockLockState::Acquiring;
    rig.confidence = 0.1f;
    rig.director.notifySessionStarted();

    const DirectorDecision d = rig.tick (0.0);
    CHECK (d.state == DirectorState::Listening);
    CHECK (! d.hasBarChange);
}

JAM_TEST (jamdirector, readyToJoinThenPlayingOnEcho)
{
    Rig rig;
    rig.director.notifySessionStarted();

    const DirectorDecision ready = rig.tick (0.0);
    CHECK (ready.state == DirectorState::ReadyToJoin);
    CHECK (! ready.hasBarChange);   // no proposal until playback is engaged

    rig.echo = true;
    const DirectorDecision playing = rig.tick (0.0);
    CHECK (playing.state == DirectorState::Playing);
    REQUIRE (playing.hasBarChange);
    rig.director.acknowledgePublication (true);

    const auto report = rig.director.report();
    CHECK (report.acceptedPublications == 1);
    CHECK (report.committedGroove != kNoLibraryEntry);
}

JAM_TEST (jamdirector, rejectedPublicationRetriesUnchangedAndDoesNotCommit)
{
    Rig rig;
    toPlaying (rig);
    // Force a fresh, uncommitted proposal: switch style clears the selection.
    DirectorSettings s = rig.director.settings();
    s.style = StyleId::Funk;
    rig.director.setSettings (s);

    const DirectorDecision first = rig.tick (0.0);
    REQUIRE (first.hasBarChange);

    const std::uint64_t acceptedBefore = rig.director.report().acceptedPublications;
    rig.director.acknowledgePublication (false);
    CHECK (rig.director.committedGroove() == kNoLibraryEntry);
    CHECK (rig.director.report().rejectedPublications == 1);

    const DirectorDecision second = rig.tick (0.0);
    REQUIRE (second.hasBarChange);
    CHECK (second.barChange.groove == first.barChange.groove);
    CHECK (second.barChange.intensity01 == first.barChange.intensity01);
    CHECK (rig.director.committedGroove() == kNoLibraryEntry);

    rig.director.acknowledgePublication (true);
    CHECK (rig.director.committedGroove() == first.barChange.groove);
    CHECK (rig.director.report().acceptedPublications == acceptedBefore + 1);
}

JAM_TEST (jamdirector, repeatedCursorTicksDoNotAdvanceState)
{
    Rig rig;
    toPlaying (rig);

    rig.cursor = 5000;
    rig.energy = 1.0f;
    const DirectorDecision first = rig.tick (0.0);

    rig.energy = 1.0f;
    const DirectorDecision duplicate = rig.tick (0.0);   // same cursor
    CHECK_EQ (duplicate.intensityEnvelope01, first.intensityEnvelope01);
    CHECK_EQ (duplicate.barsObserved, first.barsObserved);

    // A genuinely new cursor continues to advance the envelope.
    rig.cursor = 5100;
    const DirectorDecision advanced = rig.tick (0.0);
    CHECK (advanced.intensityEnvelope01 > duplicate.intensityEnvelope01);
}

JAM_TEST (jamdirector, repeatedCursorReemitsPendingWithoutReDeciding)
{
    Rig rig;
    rig.director.notifySessionStarted();
    rig.echo = true;
    rig.cursor = 42;
    rig.tick (0.0);                                   // Listening -> ReadyToJoin
    rig.cursor = 43;
    const DirectorDecision first = rig.tick (0.0);    // Playing + proposal
    REQUIRE (first.hasBarChange);

    const DirectorDecision duplicate = rig.tick (0.0); // same cursor 43
    REQUIRE (duplicate.hasBarChange);
    CHECK (duplicate.barChange.groove == first.barChange.groove);
    CHECK (duplicate.barChange.fill == first.barChange.fill);
}

JAM_TEST (jamdirector, stopForgetsPendingWithoutCommitting)
{
    Rig rig;
    toPlaying (rig);
    DirectorSettings s = rig.director.settings();
    s.requestFill = true;
    rig.director.setSettings (s);
    const DirectorDecision pending = rig.tick (0.0);
    REQUIRE (pending.hasBarChange);

    rig.director.notifyStopRequested();
    CHECK (! rig.director.publicationPending());
    CHECK (rig.director.report().cancelledPublications == 1);
    CHECK (rig.director.state() == DirectorState::Stopping);

    const DirectorDecision after = rig.tick (0.0);
    CHECK (! after.hasBarChange);
}

JAM_TEST (jamdirector, lostForgetsPending)
{
    Rig rig;
    toPlaying (rig);
    DirectorSettings s = rig.director.settings();
    s.requestFill = true;
    rig.director.setSettings (s);
    REQUIRE (rig.tick (0.0).hasBarChange);

    rig.lock = ClockLockState::Lost;
    rig.confidence = 0.1f;
    const DirectorDecision lost = rig.tick (0.0);
    CHECK (lost.state == DirectorState::Reacquiring);
    CHECK (! rig.director.publicationPending());
    CHECK (rig.director.report().cancelledPublications == 1);
}

JAM_TEST (jamdirector, discontinuityForgetsPendingAndResetsStaleState)
{
    Rig rig;
    toPlaying (rig);
    rig.bar();
    CHECK (rig.director.report().barsObserved == 1);

    DirectorSettings s = rig.director.settings();
    s.requestFill = true;
    rig.director.setSettings (s);
    REQUIRE (rig.tick (0.0).hasBarChange);

    rig.discontinuity = true;
    rig.tick (0.0);
    CHECK (rig.director.report().staleResets == 1);
    CHECK (! rig.director.publicationPending());
    CHECK (rig.director.report().barsObserved == 0);
}

JAM_TEST (jamdirector, reportExposesSettingsIntensityAndPendingProposal)
{
    Rig rig;
    toPlaying (rig);
    DirectorSettings s = rig.director.settings();
    s.style = StyleId::Blues;
    s.intensity01 = 0.8f;
    s.complexity01 = 0.2f;
    s.fillAmount01 = 0.5f;
    rig.director.setSettings (s);

    const DirectorDecision d = rig.tick (0.0);
    REQUIRE (d.hasBarChange);
    const DirectorReport r = rig.director.report();
    CHECK (r.settings.style == StyleId::Blues);
    CHECK_EQ (r.settings.intensity01, 0.8f);
    CHECK_EQ (r.settings.complexity01, 0.2f);
    CHECK_EQ (r.settings.fillAmount01, 0.5f);
    CHECK (r.pendingAck);
    CHECK_EQ (r.pendingGroove, d.barChange.groove);
    CHECK_EQ (r.pendingChange.groove, d.barChange.groove);
    CHECK (r.intensityEnvelope01 > 0.0f);
}

JAM_TEST (jamdirector, holdoverHoldsAndSuppressesFills)
{
    Rig rig;
    toPlaying (rig);
    DirectorSettings s = rig.director.settings();
    s.fillAmount01 = 1.0f;
    rig.director.setSettings (s);

    rig.lock = ClockLockState::Holdover;
    const DirectorDecision hold = rig.tick (0.0);
    CHECK (hold.state == DirectorState::Holdover);

    for (int i = 0; i < 40; ++i)
    {
        const DirectorDecision d = rig.bar (true, 1.0f);
        CHECK (d.state == DirectorState::Holdover);
        if (d.hasBarChange)
            rig.director.acknowledgePublication (true);
    }
    CHECK (rig.director.committedFill() == kNoLibraryEntry);
    CHECK (rig.director.report().fillsEmitted == 0);
}

JAM_TEST (jamdirector, lostWithEchoReacquiresWithoutEchoStops)
{
    Rig rig;
    toPlaying (rig);

    rig.lock = ClockLockState::Lost;
    rig.confidence = 0.1f;
    rig.echo = true;
    CHECK (rig.tick (0.0).state == DirectorState::Reacquiring);

    rig.lock = ClockLockState::Locked;
    rig.confidence = 0.9f;
    CHECK (rig.tick (0.0).state == DirectorState::Playing);

    rig.echo = false;
    CHECK (rig.tick (0.0).state == DirectorState::Stopping);
}

JAM_TEST (jamdirector, stopRequestIsExposedButNotIssuedAsStop)
{
    Rig rig;
    toPlaying (rig);
    rig.director.notifyStopRequested();
    const DirectorDecision d = rig.tick (0.0);
    CHECK (d.state == DirectorState::Stopping);
    CHECK (! d.hasBarChange);
    // The director proposes musical intent; the live stop is JamJoinPolicy's.
    CHECK (! d.intent.requestStop);
}

JAM_TEST (jamdirector, sessionGenerationChangeResetsStaleState)
{
    Rig rig;
    toPlaying (rig);
    rig.bar();
    CHECK (rig.director.report().barsObserved > 0);

    rig.session = 2;
    const DirectorDecision d = rig.tick (0.0);
    CHECK (rig.director.report().staleResets == 1);
    CHECK (d.state == DirectorState::ReadyToJoin);
    CHECK (rig.director.committedGroove() == kNoLibraryEntry);
    CHECK (rig.director.report().barsObserved == 0);
}

JAM_TEST (jamdirector, invalidSettingsAreClamped)
{
    Rig rig;
    DirectorSettings bad;
    bad.style = static_cast<StyleId> (99);
    bad.intensity01 = -3.0f;
    bad.complexity01 = 12.0f;
    bad.fillAmount01 = std::nanf ("");
    rig.director.setSettings (bad);

    const DirectorSettings s = rig.director.settings();
    CHECK (s.style == StyleId::Rock);
    CHECK_EQ (s.intensity01, 0.0f);
    CHECK_EQ (s.complexity01, 1.0f);
    CHECK_EQ (s.fillAmount01, kDirectorNeutralFillAmount);
}

JAM_TEST (jamdirector, intensityEnvelopeIsGradual)
{
    Rig rig;
    toPlaying (rig);

    rig.energy = 0.9f;
    const DirectorDecision first = rig.tick (0.0);
    CHECK (first.intensityEnvelope01 > 0.5f);
    CHECK (first.intensityEnvelope01 < 0.66f);   // not the target on the first tick

    const float afterOne = first.intensityEnvelope01;
    for (int i = 0; i < 60; ++i)
        rig.tick (0.0);
    CHECK (rig.director.report().intensityEnvelope01 > afterOne);
    CHECK_NEAR (rig.director.report().intensityEnvelope01, 0.66f, 0.005f);

    rig.energy = 0.1f;
    for (int i = 0; i < 60; ++i)
        rig.tick (0.0);
    CHECK_NEAR (rig.director.report().intensityEnvelope01, 0.34f, 0.01f);
}

JAM_TEST (jamdirector, tierPromotionRequiresSustainedEnergy)
{
    Rig rig;
    toPlaying (rig);

    rig.energy = 1.0f;   // target 0.70, above the High edge
    rig.tick (0.0);
    CHECK (rig.director.report().tier == GrooveTier::Medium);

    for (int i = 0; i < 60; ++i)
        rig.tick (0.0);
    CHECK (rig.director.report().tier == GrooveTier::High);
}

JAM_TEST (jamdirector, explicitFillIsHonouredAndCommitted)
{
    Rig rig;
    toPlaying (rig);

    DirectorSettings s = rig.director.settings();
    s.requestFill = true;
    rig.director.setSettings (s);

    const DirectorDecision d = rig.tick (0.0);
    REQUIRE (d.hasBarChange);
    CHECK (d.barChange.fill != kNoLibraryEntry);
    CHECK (d.fillRequested);

    rig.director.acknowledgePublication (true);
    CHECK (rig.director.committedFill() == d.barChange.fill);
    CHECK (rig.director.report().fillsEmitted == 1);
}

JAM_TEST (jamdirector, explicitBreakRequestsLowGrooveAndBreakIntent)
{
    Rig rig;
    toPlaying (rig);

    DirectorSettings s = rig.director.settings();
    s.requestBreak = true;
    rig.director.setSettings (s);

    const DirectorDecision d = rig.tick (0.0);
    REQUIRE (d.hasBarChange);
    CHECK (d.breakRequested);
    CHECK (d.intent.requestBreak);
    CHECK (d.barChange.fill == kNoLibraryEntry);
}

JAM_TEST (jamdirector, automaticFillsAreSuppressedAtLowConfidence)
{
    Rig rig;
    toPlaying (rig);

    DirectorSettings s = rig.director.settings();
    s.fillAmount01 = 1.0f;
    rig.director.setSettings (s);

    rig.confidence = 0.50f;   // below fillConfidenceThreshold, but still Locked
    for (int i = 0; i < 40; ++i)
    {
        const DirectorDecision d = rig.bar (true, 1.0f);
        if (d.hasBarChange)
            rig.director.acknowledgePublication (true);
    }
    CHECK (rig.director.committedFill() == kNoLibraryEntry);
    CHECK (rig.director.report().fillsEmitted == 0);
    CHECK (rig.director.report().fillsSuppressed > 0);
}

JAM_TEST (jamdirector, moderateConfidenceIsNotHardSuppressed)
{
    Rig rig;
    toPlaying (rig);

    DirectorSettings s = rig.director.settings();
    s.fillAmount01 = 1.0f;
    rig.director.setSettings (s);

    rig.confidence = 0.60f;   // between the fill and join thresholds
    for (int i = 0; i < 12; ++i)
    {
        const DirectorDecision d = rig.bar (true, 1.0f);
        if (d.hasBarChange)
            rig.director.acknowledgePublication (true);
    }
    CHECK (rig.director.report().fillsSuppressed == 0);
}

JAM_TEST (jamdirector, policyStopPendingSuppressesAndForgetsProposals)
{
    Rig rig;
    toPlaying (rig);

    DirectorSettings s = rig.director.settings();
    s.requestFill = true;
    rig.director.setSettings (s);
    REQUIRE (rig.tick (0.0).hasBarChange);

    // Simulate JamJoinPolicy: requestedRunning but a stop is pending.
    rig.lifecycleAllows = false;
    const DirectorDecision d = rig.tick (0.0);
    CHECK (! d.hasBarChange);
    CHECK (! rig.director.publicationPending());
    CHECK (rig.director.report().cancelledPublications == 1);
}

JAM_TEST (jamdirector, noImmediateGrooveRepeatWhereAlternativesExist)
{
    Rig rig;
    toPlaying (rig);

    LibraryIndex last = kNoLibraryEntry;
    bool have = false;
    int changes = 0;
    for (int i = 0; i < 40; ++i)
    {
        const DirectorDecision d = rig.bar();
        if (d.hasBarChange)
            rig.director.acknowledgePublication (true);

        const LibraryIndex c = rig.director.committedGroove();
        if (! have || c != last)
        {
            if (have)
            {
                CHECK (c != last);
                ++changes;
            }
            last = c;
            have = true;
        }
    }
    CHECK (changes >= 3);
}

JAM_TEST (jamdirector, barsObservedAdvancesOncePerBarAndHolds)
{
    Rig rig;
    toPlaying (rig);

    rig.tick (0.25);
    rig.tick (0.25);
    rig.tick (0.25);
    const DirectorDecision wrap = rig.tick (0.25);
    CHECK (wrap.barsObserved == 1);

    // Repeating the same bar phase must not advance the phrase counter.
    for (int i = 0; i < 50; ++i)
        rig.tick (0.0);
    CHECK (rig.director.report().barsObserved == 1);
}

JAM_TEST (jamdirector, identicalSeedsReproduceDecisions)
{
    Rig a;
    Rig b;
    toPlaying (a);
    toPlaying (b);

    DirectorSettings s;
    s.intensity01 = 0.7f;
    s.complexity01 = 0.6f;
    s.fillAmount01 = 0.6f;
    a.director.setSettings (s);
    b.director.setSettings (s);

    for (int i = 0; i < 80; ++i)
    {
        const bool onset = (i % 5 == 0);
        const DirectorDecision da = a.bar (onset, 0.9f);
        const DirectorDecision db = b.bar (onset, 0.9f);

        CHECK_EQ (static_cast<int> (da.state), static_cast<int> (db.state));
        CHECK_EQ (static_cast<int> (da.tier), static_cast<int> (db.tier));
        CHECK_EQ (da.hasBarChange, db.hasBarChange);
        CHECK_EQ (da.barChange.groove, db.barChange.groove);
        CHECK_EQ (da.barChange.fill, db.barChange.fill);

        if (da.hasBarChange)
        {
            a.director.acknowledgePublication (true);
            b.director.acknowledgePublication (true);
        }
    }
    CHECK_EQ (a.director.committedGroove(), b.director.committedGroove());
}

JAM_TEST (jamdirector, styleSwitchAbandonsSelectionForNewStyle)
{
    Rig rig;
    toPlaying (rig);
    const LibraryIndex before = rig.director.committedGroove();
    REQUIRE (before != kNoLibraryEntry);

    DirectorSettings s = rig.director.settings();
    s.style = StyleId::Funk;
    rig.director.setSettings (s);

    CHECK (rig.director.committedGroove() == kNoLibraryEntry);
    const DirectorDecision d = rig.tick (0.0);
    REQUIRE (d.hasBarChange);
    rig.director.acknowledgePublication (true);

    // The new groove must be one of the Funk references, never the old one.
    const StyleDescriptor& funk = StyleCatalog::style (StyleId::Funk);
    bool found = false;
    for (int tier = 0; tier < kGrooveTierCount; ++tier)
        for (int i = 0; i < funk.grooveCount[tier]; ++i)
            found = found || funk.grooves[tier][i].index == rig.director.committedGroove();
    CHECK (found);
    CHECK (rig.director.committedGroove() != before);
}

JAM_TEST (jamdirector, publishPendingHelperFeedsAck)
{
    Rig rig;
    rig.director.notifySessionStarted();
    rig.echo = true;
    rig.tick (0.0);   // Listening -> ReadyToJoin
    rig.tick (0.0);   // ReadyToJoin -> Playing + initial proposal
    REQUIRE (rig.director.publicationPending());

    int calls = 0;
    bool result = rig.director.publishPending ([&calls] (const QueuedBarChange&) {
        ++calls;
        return false;   // simulate a full queue
    });
    CHECK (! result);
    CHECK_EQ (calls, 1);
    CHECK (rig.director.publicationPending());
    CHECK (rig.director.report().rejectedPublications == 1);

    result = rig.director.publishPending ([&calls] (const QueuedBarChange&) {
        ++calls;
        return true;
    });
    CHECK (result);
    CHECK (! rig.director.publicationPending());
    CHECK (rig.director.report().acceptedPublications == 1);
}

JAM_TEST (jamdirector, committedChangeCarriesStyleSwingAndHumanize)
{
    Rig rig;
    DirectorSettings s;
    s.style = StyleId::Blues;
    rig.director.reset (s);
    toPlaying (rig);

    const StyleDescriptor& blues = StyleCatalog::style (StyleId::Blues);
    DirectorSettings fill = rig.director.settings();
    fill.requestFill = true;
    rig.director.setSettings (fill);
    const DirectorDecision d = rig.tick (0.0);
    REQUIRE (d.hasBarChange);
    CHECK_EQ (d.barChange.swing01, blues.defaultSwing01);
    CHECK_EQ (d.barChange.humanizeVelocity, blues.humanizeVelocity);
    CHECK_EQ (d.barChange.humanizeTiming, blues.humanizeTiming);
    CHECK_EQ (d.barChange.humanizeRoundRobin, blues.humanizeRoundRobin);
}
