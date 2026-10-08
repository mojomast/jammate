#include "JamLivePresenter.h"

#include "../jam/RhythmTypes.h"
#include <cmath>

namespace
{
using Cmd = jam::JamLiveCommandType;

const char* commandName (Cmd t) noexcept
{
    switch (t)
    {
        case Cmd::Start:          return "Start";
        case Cmd::Stop:           return "Stop";
        case Cmd::StopAtNextBar:  return "Stop at next bar";
        case Cmd::TapTempo:       return "Tap tempo";
        case Cmd::ResyncNextBeat: return "Resync next beat";
        case Cmd::ResyncNextBar:  return "Resync next bar";
        case Cmd::HalfTime:       return "Half time";
        case Cmd::DoubleTime:     return "Double time";
        case Cmd::FreezeTempo:    return "Freeze tempo";
        case Cmd::ResumeFollow:   return "Resume follow";
        case Cmd::SetMode:        return "Set mode";
        case Cmd::Reset:          return "Reset";
        case Cmd::SetStyle:       return "Style";
        case Cmd::SetIntensity:   return "Intensity";
        case Cmd::SetComplexity:  return "Complexity";
        case Cmd::SetFillAmount:  return "Fill amount";
        case Cmd::RequestFill:    return "Fill";
    }
    return "Unknown";
}

const char* backendName (jam::JamLiveBackend b) noexcept
{
    switch (b)
    {
        case jam::JamLiveBackend::unavailable:     return "unavailable";
        case jam::JamLiveBackend::experimentalBTrack: return "experimental BTrack";
        case jam::JamLiveBackend::injectedTest:    return "injected test";
    }
    return "unknown";
}

const char* failureName (jam::JamLiveFailure f) noexcept
{
    switch (f)
    {
        case jam::JamLiveFailure::none:             return "none";
        case jam::JamLiveFailure::unavailableBackend: return "backend unavailable";
        case jam::JamLiveFailure::invalidDevice:    return "invalid device";
        case jam::JamLiveFailure::workerFailure:    return "worker failure";
    }
    return "unknown";
}

JamViewState::Lock lockOf (jam::ClockLockState s) noexcept
{
    switch (s)
    {
        case jam::ClockLockState::Locked:   return JamViewState::Lock::locked;
        case jam::ClockLockState::Holdover: return JamViewState::Lock::holdover;
        case jam::ClockLockState::Lost:     return JamViewState::Lock::lost;
        case jam::ClockLockState::Acquiring: return JamViewState::Lock::acquiring;
    }
    return JamViewState::Lock::acquiring;
}

const char* intentName (JamUiIntent::Kind k) noexcept
{
    using K = JamUiIntent::Kind;
    switch (k)
    {
        case K::style:          return "Style selection";
        case K::intensity:      return "Intensity";
        case K::complexity:     return "Complexity";
        case K::fillAmount:     return "Fill amount";
        case K::followTightness:return "Follow tightness";
        case K::fill:           return "Fill";
        case K::breakBar:       return "Break";
        default:                return "That control";
    }
}
}

JamLivePresenter::JamLivePresenter (jam::IJamLiveControl& c) noexcept : control (c)
{
    resetCache();
}

void JamLivePresenter::resetCache() noexcept
{
    live = jam::JamLiveState {};
    haveLive = false;
    lastAccepted = false;
    hasPending = false;
    pendingWantsStart = false;
    haveGeneration = false;
    lastSeenGeneration = 0;
    lastSeenAudioSampleTime = 0;
    feedback.clear();
    present();
}

bool JamLivePresenter::mapIntent (const JamUiIntent& in, jam::JamLiveCommand& out) noexcept
{
    using K = JamUiIntent::Kind;
    out = jam::JamLiveCommand {};
    if (! std::isfinite (in.value))
        return false;
    switch (in.kind)
    {
        case K::startStop:   out.type = Cmd::Start;          return true;
        case K::tap:         out.type = Cmd::TapTempo;       return true;
        case K::resync:      out.type = Cmd::ResyncNextBeat; return true;
        case K::resyncBar:   out.type = Cmd::ResyncNextBar;  return true;
        case K::half:        out.type = Cmd::HalfTime;       return true;
        case K::doubleTempo: out.type = Cmd::DoubleTime;     return true;
        case K::freeze:      out.type = Cmd::FreezeTempo;    return true;
        case K::resume:      out.type = Cmd::ResumeFollow;   return true;
        case K::stopNextBar: out.type = Cmd::StopAtNextBar;  return true;
        case K::reset:       out.type = Cmd::Reset;          return true;
        case K::mode:
        {
            const int id = static_cast<int> (juce::jlimit (1.0, 3.0, in.value));
            out.type = Cmd::SetMode;
            out.value = (double) (id - 1);   // combo id 1..3 -> TempoMode 0..2
            return true;
        }
        case K::style:
            if (in.value < 1.0 || in.value > 6.0 || std::floor (in.value) != in.value)
                return false;
            out.type = Cmd::SetStyle;
            out.value = in.value - 1.0;
            return true;
        case K::intensity:
        case K::complexity:
        case K::fillAmount:
            if (in.value < 0.0 || in.value > 100.0)
                return false;
            out.type = in.kind == K::intensity ? Cmd::SetIntensity
                     : in.kind == K::complexity ? Cmd::SetComplexity : Cmd::SetFillAmount;
            out.value = in.value / 100.0;
            return true;
        case K::fill:
            out.type = Cmd::RequestFill;
            return true;
        case K::followTightness:
        case K::breakBar:
            return false;   // no frozen command: must not look applied
    }
    return false;
}

bool JamLivePresenter::submit (const JamUiIntent& in) noexcept
{
    ++submits;

    jam::JamLiveCommand cmd;
    if (! mapIntent (in, cmd))
    {
        ++rejects;
        lastAccepted = false;
        const bool unsupported = in.kind == JamUiIntent::Kind::followTightness
                                 || in.kind == JamUiIntent::Kind::breakBar;
        feedback = juce::String (intentName (in.kind))
                   + (unsupported ? " is not implemented in this build." : " rejected: invalid value.");
        present();
        return false;
    }

    // One button, toggled from the effective desired bit: a pending local
    // request overrides the engine echo until acknowledged, so a fast second
    // click can cancel a pending join, and a recreated editor that already sees
    // the engine playing correctly offers Stop first.
    const bool isStartStop = in.kind == JamUiIntent::Kind::startStop;
    const bool isStopNow = in.kind == JamUiIntent::Kind::stopNextBar
                           || in.kind == JamUiIntent::Kind::reset;
    bool desired = false;
    if (isStartStop)
    {
        desired = ! effectiveDesired();
        cmd.type = desired ? Cmd::Start : Cmd::Stop;
    }

    lastCommand = (int) cmd.type;
    lastCommandValue_ = cmd.value;

    const bool ok = control.submitJamCommand (cmd);
    lastAccepted = ok;
    if (ok)
    {
        if (isStartStop)
        {
            hasPending = true;
            pendingWantsStart = desired;
        }
        else if (isStopNow)
        {
            hasPending = true;
            pendingWantsStart = false;   // Stop-next-bar / Reset intend a stopped state
        }
        feedback = juce::String (commandName (cmd.type))
                   + (cmd.type == Cmd::Stop ? " requested." : " accepted by the queue - not yet applied.");
    }
    else
    {
        ++rejects;
        // A rejected Start must never look accepted: the latch is unchanged.
        feedback = juce::String (commandName (cmd.type))
                   + " rejected: the live command queue is full or unavailable.";
    }
    present();
    return ok;
}

bool JamLivePresenter::poll() noexcept
{
    ++polls;

    // The facade writes the whole state on true and leaves it unchanged on false;
    // only a coherent read replaces our cache, so a failed read keeps the previous
    // whole snapshot until the worker publishes the next one.
    jam::JamLiveState next {};
    const bool ok = control.readJamLiveState (next);
    if (ok)
    {
        const bool genChanged = haveGeneration && next.sessionGeneration != lastSeenGeneration;
        // "Fresh" means the worker published a new cursor, not a repeated snapshot
        // from its latest-value cache. Only a fresh state may acknowledge a
        // pending request, so a cold queued Start is never cleared by the older
        // false snapshot that predates the worker consuming it.
        const bool fresh = ! haveLive || genChanged
                           || next.audioSampleTime != lastSeenAudioSampleTime;
        // A stronger known state (release, re-prepare, failure, unavailable
        // backend) clears the pending request and is reported truthfully.
        const bool hardReset = ! next.prepared || genChanged
                               || next.failure != jam::JamLiveFailure::none
                               || next.backend == jam::JamLiveBackend::unavailable;
        if (hardReset)
        {
            hasPending = false;
        }
        else if (hasPending && fresh)
        {
            // Start is acknowledged by the request bit going true; a Stop is
            // acknowledged only once the request bit and the audio echo are both
            // false, so a queued Stop keeps showing STOP QUEUED until sound stops.
            const bool ack = pendingWantsStart ? next.requestedRunning
                                            : (! next.requestedRunning && ! next.drumsPlaying);
            if (ack)
                hasPending = false;
        }
        haveGeneration = true;
        lastSeenGeneration = next.sessionGeneration;
        lastSeenAudioSampleTime = next.audioSampleTime;
        live = next;
        haveLive = true;
    }
    else
    {
        ++readFailures;
    }
    present();
    return ok;
}

juce::String JamLivePresenter::nextIntentText() const
{
    if (live.failure != jam::JamLiveFailure::none)
        return failureName (live.failure);
    if (! live.prepared)
        return "Not prepared";
    if (stopQueued())
        return "Stop queued";
    if (startQueued() && ! live.requestedRunning)
        return "Start queued";
    if (live.drumsPlaying)
        return live.fillPlaying ? "Fill playing (audio echo)"
             : live.adaptiveChangePending ? "Change queued for a bar" : "Playing";
    if (live.requestedRunning && live.joinPending)
        return "Waiting for a usable clock lock";
    if (live.requestedRunning)
        return "Listening for the guitar";
    return "Stopped";
}

juce::String JamLivePresenter::statusText() const
{
    if (! haveLive)
        return live.failure != jam::JamLiveFailure::none ? "UNAVAILABLE" : "NOT CONNECTED";
    if (live.failure != jam::JamLiveFailure::none)
        return "UNAVAILABLE";
    if (! live.prepared)
        return "NOT PREPARED";
    // A queued Stop stays visible even while the old echo still plays: the label
    // re-arms (effective desired false) but the status must not claim stopped.
    if (stopQueued())
        return "STOP QUEUED - WAITING FOR THE ENGINE";
    if (startQueued() && ! live.requestedRunning)
        return "START QUEUED - WAITING FOR THE ENGINE";
    if (live.drumsPlaying)
        return "PLAYING (AUDIO ECHO)";
    if (live.requestedRunning && live.joinPending)
        return "ARMED - WAITING FOR THE CLOCK";
    if (live.requestedRunning)
        return "ARMED - LISTENING";
    return "PREPARED - STOPPED";
}

juce::String JamLivePresenter::composeDiagnostics() const
{
    juce::String d;
    d << "Generation " << (long long) live.sessionGeneration
      << "   audio " << (unsigned long long) live.audioSampleTime
      << "   event " << (unsigned long long) live.lastEventSampleTime
      << "   horizon " << (unsigned long long) live.lastInputHorizonSampleTime
      << "   receipt " << (unsigned long long) live.lastReceiptSampleTime;
    d << "\nCandidate " << juce::String (live.candidateBpm, 1)
      << " BPM   clock " << juce::String (live.clock.bpm, 1)
      << " BPM   confidence " << juce::String (live.clock.confidence01, 2)
      << "   beatPhase " << juce::String (live.clock.beatPhase01, 2)
      << "   barPhase " << juce::String (live.clock.barPhase01, 2);
    d << "\nDrops: analysis " << (unsigned long long) live.analysisDrops
      << "  observation " << (unsigned long long) live.observationDrops
      << "  user cmd " << (unsigned long long) live.userCommandDrops
      << "  drum cmd " << (unsigned long long) live.drumCommandDrops
      << "   discontinuities " << (unsigned long long) live.discontinuities
      << "   sampleRate " << juce::String (live.sampleRate, 0);
    return d;
}

void JamLivePresenter::present() noexcept
{
    JamViewState v;
    v.simulated = false;
    v.mode = 2;                       // default presentation is Follow
    v.backendName = "unavailable";
    v.failureName = "none";
    v.availability = "Break and follow tightness are not implemented. Style and fills change at bar boundaries.";

    if (haveLive)
    {
        v.hasState = true;
        v.prepared = live.prepared;
        v.requestedRunning = live.requestedRunning;
        v.joinPending = live.joinPending;
        v.drumsPlaying = live.drumsPlaying;
        v.tempoFrozen = live.clock.tempoFrozen;
        v.backend = (int) live.backend;
        v.backendName = backendName (live.backend);
        v.failure = (int) live.failure;
        v.failureName = failureName (live.failure);
        v.modeId = (int) live.mode;
        v.mode = juce::jlimit (1, 3, (int) live.mode + 1);
        v.style = live.styleIndex + 1;
        v.amounts = { live.intensity01 * 100.0, live.complexity01 * 100.0,
                      live.fillAmount01 * 100.0, 0.0 };
        v.sessionGeneration = live.sessionGeneration;
        v.audioSampleTime = live.audioSampleTime;
        v.receiptMeasured = live.receiptMeasured;
        v.receiptLagSamples = live.receiptMeasured
                                  && live.audioSampleTime >= live.lastReceiptSampleTime
                              ? live.audioSampleTime - live.lastReceiptSampleTime : 0;
        v.analysisDrops = live.analysisDrops;
        v.observationDrops = live.observationDrops;
        v.userCommandDrops = live.userCommandDrops;
        v.drumCommandDrops = live.drumCommandDrops;
        v.discontinuities = live.discontinuities;
        v.candidateBpm = (double) live.candidateBpm;
        v.clockBpm = live.clock.bpm;
        v.confidence = live.clock.confidence01;
        v.inputLevel = juce::jlimit (0.0f, 1.0f, live.inputPeak);
        v.lock = lockOf (live.clock.lockState);
        v.beat = live.clock.beatInBar;
        v.beatsPerBar = live.clock.beatsPerBar;
        v.beatUnit = live.clock.beatUnit;
        v.beatPhase01 = (float) live.clock.beatPhase01;
        v.barPhase01 = (float) live.clock.barPhase01;
        v.bar = 0;                     // no bar counter is published; never invented
        v.nextIntent = nextIntentText();
        v.diagnostics = composeDiagnostics();
    }
    else
    {
        v.diagnostics = "No coherent telemetry from the live pipeline yet; showing the cold default.";
        v.nextIntent = "Waiting for the live pipeline";
    }

    // The LABEL/selection uses exactly the effective desired bit (pending local
    // request overrides the echo until acknowledged). The status line reports the
    // audio-owner echo separately, so a scheduled command is never claimed as
    // sound.
    v.startQueued = startQueued();
    v.stopQueued = stopQueued();
    v.running = effectiveDesired();
    v.statusText = statusText();
    v.commandFeedback = feedback;
    view = v;
}
