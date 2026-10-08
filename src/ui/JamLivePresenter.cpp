#include "JamLivePresenter.h"

#include "../jam/RhythmTypes.h"

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
    feedback.clear();
    present();
}

bool JamLivePresenter::mapIntent (const JamUiIntent& in, jam::JamLiveCommand& out) noexcept
{
    using K = JamUiIntent::Kind;
    out = jam::JamLiveCommand {};
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
            const int id = (int) in.value < 1 ? 1 : ((int) in.value > 3 ? 3 : (int) in.value);
            out.type = Cmd::SetMode;
            out.value = (double) (id - 1);   // combo id 1..3 -> TempoMode 0..2
            return true;
        }
        case K::style:
        case K::intensity:
        case K::complexity:
        case K::fillAmount:
        case K::followTightness:
        case K::fill:
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
        feedback = juce::String (intentName (in.kind)) + " is not implemented in this build.";
        present();
        return false;
    }

    // One button: the next start toggle is a stop once Start was accepted (or the
    // audio owner echoes playback), so a scheduled stop is never re-armed.
    if (in.kind == JamUiIntent::Kind::startStop
        && (live.requestedRunning || live.drumsPlaying))
        cmd.type = Cmd::Stop;

    lastCommand = (int) cmd.type;
    lastCommandValue_ = cmd.value;

    const bool ok = control.submitJamCommand (cmd);
    lastAccepted = ok;
    if (ok)
        feedback = juce::String (commandName (cmd.type))
                   + (cmd.type == Cmd::Stop ? " requested." : " accepted by the queue - not yet applied.");
    else
    {
        ++rejects;
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
    if (! live.requestedRunning)
        return "Stopped";
    if (live.drumsPlaying)
        return "Playing";
    if (live.joinPending)
        return "Waiting for a usable clock lock";
    return "Listening for the guitar";
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
    v.availability = "Style, intensity, complexity, fills and tightness are not implemented in this build.";

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
        v.running = live.requestedRunning || live.drumsPlaying;
        v.nextIntent = nextIntentText();
        v.diagnostics = composeDiagnostics();
    }
    else
    {
        v.diagnostics = "No coherent telemetry from the live pipeline yet; showing the cold default.";
        v.nextIntent = "Waiting for the live pipeline";
    }

    v.commandFeedback = feedback;
    view = v;
}
