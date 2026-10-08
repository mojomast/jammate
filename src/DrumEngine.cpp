#include "DrumEngine.h"

#include <BinaryData.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

namespace drum
{

// General MIDI, canal 10: bumbo 36, caixa 38, chimbal fechado 42, pedal 44,
// ride 51, crash 49, tom agudo 48, tom médio 45, surdo 41
const int gmNote[numVoices] = { 36, 38, 42, 44, 51, 49, 48, 45, 41 };

const char* const voiceIds[numVoices] =
    { "kick", "snare", "hat", "hatpedal", "ride", "crash", "tom1", "tom2", "floor" };

const char* const voiceNames[numVoices] =
    { "Kick", "Snare", "Hi-hat", "Hat pedal", "Ride", "Crash",
      "Tom 1", "Tom 2", "Floor" };

static int voiceForCode (const juce::String& code)
{
    if (code == "K") return kick;
    if (code == "S") return snare;
    if (code == "H") return hat;
    if (code == "P") return hatPedal;
    if (code == "R") return ride;
    if (code == "C") return crash;
    if (code == "T") return tom1;
    if (code == "U") return tom2;
    if (code == "F") return floorTom;
    return -1;
}

void parseSpec (const Groove& g, juce::uint8 out[numVoices][maxStepsPerBar])
{
    for (int v = 0; v < numVoices; ++v)
        for (int s = 0; s < maxStepsPerBar; ++s)
            out[v][s] = 0;

    for (const auto& tok : juce::StringArray::fromTokens (g.spec, "|", ""))
    {
        const int colon = tok.indexOfChar (':');
        if (colon < 0)
            continue;
        const int v = voiceForCode (tok.substring (0, colon).trim());
        if (v < 0)
            continue;

        for (auto item : juce::StringArray::fromTokens (tok.substring (colon + 1), ",", ""))
        {
            item = item.trim();
            if (item.isEmpty())
                continue;

            juce::uint8 val = 1;
            if (item.endsWithChar ('!')) { val = 2; item = item.dropLastCharacters (1); }
            else if (item.endsWithChar ('.')) { val = 3; item = item.dropLastCharacters (1); }

            int from = 0, to = 0, stride = 1;
            if (item.containsChar ('-'))
            {
                from = item.upToFirstOccurrenceOf ("-", false, false).getIntValue();
                auto rest = item.fromFirstOccurrenceOf ("-", false, false);
                to = rest.upToFirstOccurrenceOf ("/", false, false).getIntValue();
                if (rest.containsChar ('/'))
                    stride = juce::jmax (1, rest.fromFirstOccurrenceOf ("/", false, false).getIntValue());
            }
            else
                from = to = item.getIntValue();

            for (int s = from; s <= to; s += stride)
                if (s >= 0 && s < maxStepsPerBar)
                    out[v][s] = val;
        }
    }
}

} // namespace drum

//==============================================================================
/// Lock-free "max-hold" publish for a meter: keeps the loudest value seen since
/// the UI last drained it. std::atomic<float> has no fetch_max, so this is a
/// relaxed CAS loop - it runs at most once per voice per BLOCK (never per
/// sample) and the only other writer is the UI's exchange (0), so it settles
/// after one iteration in practice.
static inline void publishPeak (std::atomic<float>& dest, float v) noexcept
{
    float cur = dest.load (std::memory_order_relaxed);
    // Best-effort cosmetic meter: a UI exchange may win this one attempt.
    // Never retry on the audio callback merely to preserve a meter peak.
    if (v > cur)
        dest.compare_exchange_strong (cur, v, std::memory_order_relaxed);
}

//==============================================================================
void DrumEngine::prepare (double sampleRate, int)
{
    sr = juce::jmax (8000.0, sampleRate);
    samplesToNext = 0.0;
    nextStep = 0;
    playBar = 0;
    wasPlaying = false;
    countInLeft = 0;
    audSamplesToNext = 0.0;
    audStep = 0;
    wasAudition = false;
    pausedByAudition = false;
    auditionOn.store (false);
    for (auto& v : svoices)
        v.active = false;
    for (auto& o : pendingOffs)
        o.note = -1;
    anyVoiceActive.store (false);
    uiStep.store (-1);
    uiBar.store (-1);
    for (auto& p : uiVoicePeak)
        p.store (0.0f);
    uiMixPeak.store (0.0f);
    uiMixFromVst.store (false);

    // Injected clock state is audio-thread owned and starts clean; the queue
    // pointer (if any) and a previously prepared groove survive a re-prepare.
    resetInjectedTransport();
}

double DrumEngine::stepLenSamples (int stepIdx) const
{
    const double b = juce::jlimit (40.0f, 260.0f, bpm.load());
    const double base = 60.0 / b / 4.0 * sr;
    // swing: semicolcheias ímpares atrasam (pares alongam, ímpares encurtam)
    const double sw = juce::jlimit (0.0f, 60.0f, swingPct.load()) * 0.009;
    return (stepIdx % 2 == 0) ? base * (1.0 + sw) : base * (1.0 - sw);
}

void DrumEngine::loadEmbeddedSamples()
{
    struct Item { const char* data; int size; };
    using namespace BinaryData;
    static const Item items[drum::numVoices][3] = {
        { { drum_kick_1_wav, drum_kick_1_wavSize }, { drum_kick_2_wav, drum_kick_2_wavSize }, { drum_kick_3_wav, drum_kick_3_wavSize } },
        { { drum_snare_1_wav, drum_snare_1_wavSize }, { drum_snare_2_wav, drum_snare_2_wavSize }, { drum_snare_3_wav, drum_snare_3_wavSize } },
        { { drum_hat_1_wav, drum_hat_1_wavSize }, { drum_hat_2_wav, drum_hat_2_wavSize }, { drum_hat_3_wav, drum_hat_3_wavSize } },
        { { drum_hatpedal_1_wav, drum_hatpedal_1_wavSize }, { drum_hatpedal_2_wav, drum_hatpedal_2_wavSize }, { drum_hatpedal_3_wav, drum_hatpedal_3_wavSize } },
        { { drum_ride_1_wav, drum_ride_1_wavSize }, { drum_ride_2_wav, drum_ride_2_wavSize }, { drum_ride_3_wav, drum_ride_3_wavSize } },
        { { drum_crash_1_wav, drum_crash_1_wavSize }, { drum_crash_2_wav, drum_crash_2_wavSize }, { drum_crash_3_wav, drum_crash_3_wavSize } },
        { { drum_tom1_1_wav, drum_tom1_1_wavSize }, { drum_tom1_2_wav, drum_tom1_2_wavSize }, { drum_tom1_3_wav, drum_tom1_3_wavSize } },
        { { drum_tom2_1_wav, drum_tom2_1_wavSize }, { drum_tom2_2_wav, drum_tom2_2_wavSize }, { drum_tom2_3_wav, drum_tom2_3_wavSize } },
        { { drum_floor_1_wav, drum_floor_1_wavSize }, { drum_floor_2_wav, drum_floor_2_wavSize }, { drum_floor_3_wav, drum_floor_3_wavSize } },
    };

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    bool allOk = true;

    for (int v = 0; v < drum::numVoices; ++v)
        for (int l = 0; l < 3; ++l)
        {
            auto stream = std::make_unique<juce::MemoryInputStream> (
                items[v][l].data, (size_t) items[v][l].size, false);
            std::unique_ptr<juce::AudioFormatReader> reader (
                fm.createReaderFor (std::move (stream)));
            if (reader == nullptr || reader->lengthInSamples <= 0)
            {
                allOk = false;
                continue;
            }
            auto& layer = sampleLayers[v][l];
            const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                           (juce::int64) reader->sampleRate * 6);
            layer.buf.setSize ((int) juce::jmin (2u, reader->numChannels), len);
            reader->read (&layer.buf, 0, len, 0, true, reader->numChannels > 1);
            layer.rate = reader->sampleRate;
        }

    samplesReady.store (allOk);
}

void DrumEngine::trigger (int synthType, float vel, int delaySamples)
{
    // chimbal: fechado/pedal cortam o aberto (choke com fade de ~15 ms)
    if (synthType == drum::hat || synthType == drum::hatPedal)
        for (auto& v : svoices)
            if (v.active && (v.type == drum::hat || v.type == drum::hatPedal))
                v.fading = true;

    SynthVoice* slot = nullptr;
    double oldest = -1.0;
    for (auto& v : svoices)
    {
        if (! v.active) { slot = &v; break; }
        if (v.t > oldest) { oldest = v.t; slot = &v; }
    }
    *slot = SynthVoice();
    slot->type = synthType;
    slot->active = true;
    slot->delay = juce::jmax (0, delaySamples);
    slot->vel = vel;
    slot->noise = 0x9e3779b9u + (juce::uint32) (synthType * 7919);

    if (synthType < drum::numVoices && samplesReady.load())
    {
        const int layerIdx = vel > 0.9f ? 2 : vel > 0.5f ? 1 : 0;
        const auto& layer = sampleLayers[synthType][layerIdx];
        if (layer.buf.getNumSamples() > 0)
        {
            slot->layer = &layer;
            slot->vel = vel > 0.5f ? 1.0f : 0.75f;
            // round-robin: micro-variação de afinação p/ não soar "metralhadora"
            const float rr = injAdaptive_ ? injHumanRR_ : humanRR.load();
            slot->rrPitch = 1.0f + nextRnd() * rr * 0.03f;
        }
    }
    anyVoiceActive.store (true);
}

void DrumEngine::fireHit (int v, int val, int sampleOffset,
                          juce::AudioPluginInstance* vst, juce::MidiBuffer& midi)
{
    if (val == 0)
        return;
    float vel = val == 2 ? 1.0f : val == 3 ? 0.28f : 0.68f;
    // KIT MIXER per-voice level (clamped: MIDI velocity is 7-bit)
    vel = juce::jlimit (0.0f, 1.0f, vel * voiceGain[v].load());

    // DRUM-ADAPT-002 intensity: only when the injected transport was handed an
    // intensity (injAdaptive_). Neutral 0.5 is exactly the legacy velocity, so a
    // join without an adaptation is byte-identical to the pre-adaptive slice.
    // The existing [0,1] clamp still bounds the result.
    if (injAdaptive_)
        vel = juce::jlimit (0.05f, 1.0f, vel * (0.5f + injIntensity01_));

    // humanização: velocity e micro-timing (offset só p/ frente, RT-safe)
    const float hv = injAdaptive_ ? injHumanVel_ : humanVel.load();
    const float ht = injAdaptive_ ? injHumanTime_ : humanTime.load();
    if (hv > 0.0f) vel = juce::jlimit (0.05f, 1.0f, vel * (1.0f + nextRnd() * hv * 0.35f));
    int off = sampleOffset;
    if (ht > 0.0f) off = juce::jmax (0, off + (int) (nextRnd() * ht * 0.018f * (float) sr));

    // KIT MIXER meter flash (UI consumes and decays this)
    uiVoiceFlash[v].store (juce::jmax (uiVoiceFlash[v].load(), vel));

    if (vst != nullptr)
    {
        const int note = drum::gmNote[v];
        midi.addEvent (juce::MidiMessage::noteOn (10, note,
                                                  (juce::uint8) (vel * 127.0f)), off);
        for (auto& o : pendingOffs)
            if (o.note < 0)
            {
                o.note = note;
                o.samplesLeft = off + (int) (0.25 * sr);
                break;
            }
    }
    else
        trigger (v, vel, off);
}

void DrumEngine::fireStep (int bar, int step, int sampleOffset,
                           juce::AudioPluginInstance* vst, juce::MidiBuffer& midi)
{
    if (barUsed[bar].load())
        for (int v = 0; v < drum::numVoices; ++v)
            fireHit (v, pattern[bar][v][step].load(), sampleOffset, vst, midi);

    if (clickOn.load() && step % 4 == 0)
        trigger (step == 0 ? 10 : 9, 1.0f, sampleOffset);
}

void DrumEngine::fireAuditionStep (int step, int sampleOffset,
                                   juce::AudioPluginInstance* vst, juce::MidiBuffer& midi)
{
    for (int v = 0; v < drum::numVoices; ++v)
        fireHit (v, auditionPat[v][step].load(), sampleOffset, vst, midi);
}

void DrumEngine::process (juce::AudioBuffer<float>& out, int n,
                          juce::AudioPluginInstance* vst, juce::MidiBuffer& midi)
{
    out.clear (0, 0, n);
    out.clear (1, 0, n);
    midi.clear();

    // Injected clock: consume the bounded worker -> audio command queue first.
    // When injected mode is engaged it owns the timeline; the manual UI
    // transport is ignored (but left untouched, so standalone use is unchanged
    // until the first Join arrives).
    serviceInjectedClock (vst, midi);
    const bool injNow = injActive_;

    // AUDITION: while on, loop the audition pattern and pause (not stop) the
    // timeline - its bar/step position is preserved and resumes afterwards.
    const bool audNow = ! injNow && auditionOn.load();
    const bool playNow = ! injNow && playing.load() && ! audNow;
    if (! playing.load())
        pausedByAudition = false;   // user stopped: next play restarts from 0

    if (playNow)
    {
        if (! wasPlaying)
        {
            if (pausedByAudition)
            {
                pausedByAudition = false;   // resume exactly where audition paused
            }
            else
            {
                nextStep = 0;
                playBar = 0;
                samplesToNext = 8.0;
                countInLeft = countInOn.load() ? drum::stepsPerBar : 0;
            }
            uiStep.store (-1);
            uiBar.store (-1);
        }

        double pos = 0.0;
        while (pos < (double) n)
        {
            if (samplesToNext <= 0.5)
            {
                const int offset = juce::jlimit (0, n - 1, (int) pos);
                if (countInLeft > 0)
                {
                    if (countInLeft % 4 == 0)
                        trigger (countInLeft == drum::stepsPerBar ? 10 : 9, 1.0f, offset);
                    --countInLeft;
                    samplesToNext += stepLenSamples (0);
                }
                else
                {
                    const int total = totalBars();
                    if (playBar >= total)
                        playBar = 0;
                    fireStep (playBar, nextStep, offset, vst, midi);
                    uiStep.store (nextStep);
                    uiBar.store (playBar);
                    samplesToNext += stepLenSamples (nextStep);
                    if (++nextStep >= barSteps (playBar))   // nº de steps deste compasso
                    {
                        nextStep = 0;
                        playBar = (playBar + 1) % total;
                    }
                }
                continue;
            }
            const double adv = juce::jmin (samplesToNext, (double) n - pos);
            samplesToNext -= adv;
            pos += adv;
        }
    }
    else if (wasPlaying)
    {
        uiStep.store (-1);
        uiBar.store (-1);
        if (vst != nullptr)
            for (int v = 0; v < drum::numVoices; ++v)
                midi.addEvent (juce::MidiMessage::noteOff (10, drum::gmNote[v]), 0);
        for (auto& o : pendingOffs)
            o.note = -1;
    }
    wasPlaying = playNow;

    // AUDITION loop - scheduled after the timeline stop-cleanup so a pause
    // triggered by the audition never clobbers the notes scheduled here.
    if (audNow)
    {
        if (! wasAudition)
        {
            audStep = 0;
            audSamplesToNext = 8.0;
            if (playing.load())
                pausedByAudition = true;   // timeline resumes from here later
        }
        const int aSteps = juce::jlimit (1, drum::maxStepsPerBar, auditionSteps.load());
        if (audStep >= aSteps)
            audStep = 0;
        double pos = 0.0;
        while (pos < (double) n)
        {
            if (audSamplesToNext <= 0.5)
            {
                const int offset = juce::jlimit (0, n - 1, (int) pos);
                fireAuditionStep (audStep, offset, vst, midi);
                audSamplesToNext += stepLenSamples (audStep);
                audStep = (audStep + 1) % aSteps;
                continue;
            }
            const double adv = juce::jmin (audSamplesToNext, (double) n - pos);
            audSamplesToNext -= adv;
            pos += adv;
        }
    }
    wasAudition = audNow;

    // Injected clock transport (independent of the manual timeline above).
    if (injNow)
        runInjectedLoop (n, vst, midi);

    // The injected timeline is absolute and advances every callback, whether or
    // not the injected transport is playing, so command sampleTimes stay in the
    // same timeline as the worker's explicit clock.
    if (n > 0)
        injSample_ += static_cast<std::uint64_t> (n);

    if (vst != nullptr)
        for (auto& o : pendingOffs)
            if (o.note >= 0)
            {
                if (o.samplesLeft < n)
                {
                    midi.addEvent (juce::MidiMessage::noteOff (10, o.note),
                                   juce::jmax (0, o.samplesLeft));
                    o.note = -1;
                }
                else
                    o.samplesLeft -= n;
            }

    if (vst != nullptr)
        vst->processBlock (out, midi);

    // sampler interno (samples reais/síntese + clique) soma por cima
    //
    // REAL per-piece metering: each voice accumulates its own peak in a STACK
    // array while it is being summed, and the whole thing is published once at
    // the end of the block (no atomic operation per sample).
    float voicePk[drum::numVoices] = {};

    bool any = false;
    float* L = out.getWritePointer (0);
    float* R = out.getWritePointer (1);
    for (auto& v : svoices)
    {
        if (! v.active)
            continue;
        any = true;

        static const float pans[11] = { 0.0f, 0.02f, 0.22f, 0.18f, 0.3f, -0.28f,
                                        -0.12f, 0.08f, 0.2f, 0.0f, 0.0f };
        const float pan = pans[juce::jlimit (0, 10, v.type)];
        const float gl = juce::jmin (1.0f, 1.0f - pan);
        const float gr = juce::jmin (1.0f, 1.0f + pan);

        // peak of THIS voice over the block (types 9/10 are the click, which is
        // not a kit piece and has no meter)
        float pk = 0.0f;

        if (v.layer != nullptr)
        {
            const auto& buf = v.layer->buf;
            const float* sl = buf.getReadPointer (0);
            const float* sr2 = buf.getNumChannels() > 1 ? buf.getReadPointer (1) : sl;
            const bool stereo = buf.getNumChannels() > 1;
            const double ratio = v.layer->rate / sr * v.rrPitch;
            const int len = buf.getNumSamples();

            for (int i = 0; i < n && v.active; ++i)
            {
                if (v.delay > 0) { --v.delay; continue; }
                const int i0 = (int) v.pos;
                if (i0 >= len - 1)
                {
                    v.active = false;
                    break;
                }
                const float frac = (float) (v.pos - i0);
                const float a = sl[i0] + (sl[i0 + 1] - sl[i0]) * frac;
                const float b = sr2[i0] + (sr2[i0 + 1] - sr2[i0]) * frac;
                const float gain = v.vel * v.fadeGain;
                const float ls = (stereo ? a : a * gl) * gain;
                const float rs = (stereo ? b : b * gr) * gain;
                L[i] += ls;
                R[i] += rs;
                pk = juce::jmax (pk, std::abs (ls), std::abs (rs));
                v.pos += ratio;
                if (v.fading)
                {
                    v.fadeGain *= 0.9975f;
                    if (v.fadeGain < 0.002f)
                        v.active = false;
                }
            }
        }
        else
        {
            const float gm = juce::jmax (gl, gr);
            for (int i = 0; i < n && v.active; ++i)
            {
                if (v.delay > 0) { --v.delay; continue; }
                const float s = synthSample (v);
                L[i] += s * gl;
                R[i] += s * gr;
                pk = juce::jmax (pk, std::abs (s) * gm);
            }
        }

        if (v.type >= 0 && v.type < drum::numVoices)
            voicePk[v.type] = juce::jmax (voicePk[v.type], pk);
    }
    anyVoiceActive.store (any);

    // ---- publish the REAL levels for the UI meters (max-hold, UI drains) ----
    for (int v = 0; v < drum::numVoices; ++v)
        if (voicePk[v] > 0.0f)
            publishPeak (uiVoicePeak[v], voicePk[v]);

    // whole drum bus, measured AFTER everything was summed - so it covers the
    // hosted VST3 too, the only level that exists in that mode
    const float mixPk = juce::jmax (out.getMagnitude (0, 0, n),
                                    out.getMagnitude (1, 0, n));
    if (mixPk > 0.0f)
        publishPeak (uiMixPeak, mixPk);
    uiMixFromVst.store (vst != nullptr);
}

//==============================================================================
// Injected musical clock (INT-DRUM-001)
//==============================================================================
void DrumEngine::attachClockBridge (jam::DrumClockCommandQueue* queue,
                                   std::uint64_t audioSampleAtAttach) noexcept
{
    // The injected timeline IS the audio device sample timeline. attach declares
    // where that timeline currently is, so a late attach does not silently start
    // the engine at zero and delay the first join by the elapsed distance.
    clockQueue_ = queue;
    resetInjectedTransport();
    injSample_ = audioSampleAtAttach;
}

void DrumEngine::detachClockBridge() noexcept
{
    clockQueue_ = nullptr;
    resetInjectedState();
}

bool DrumEngine::prepareInjectedGroove (jam::LibraryIndex index)
{
    // Single-groove compatibility path. Any 4/4 entry is accepted here (the
    // pre-adaptive engine rendered whatever 4/4 entry it was given, grooves and
    // fills alike); the entry's fill flag is recorded so a later BarChange can
    // only use it in the matching role.
    const int slot = prepareInjectedBankEntry (index, /*requireFill=*/-1);
    if (slot < 0)
        return false;

    injSelGrooveSlot_ = slot;
    injCurrentSlot_ = slot;
    injRevertSlot_ = slot;
    injFillActive_ = false;
    injGroove_ = index;
    injPatternReady_ = true;
    return true;
}

int DrumEngine::prepareInjectedBank (const jam::LibraryIndex* grooves, int numGrooves,
                                    const jam::LibraryIndex* fills, int numFills)
{
    // A bank prepare is a full re-selection: clear the audio-owned state and
    // resolve every entry off the callback. All roles must be quiescent.
    injBankCount_ = 0;
    injSelGrooveSlot_ = -1;
    injCurrentSlot_ = -1;
    injRevertSlot_ = -1;
    injFillActive_ = false;
    injPatternReady_ = false;

    for (int i = 0; i < numGrooves; ++i)
        if (grooves != nullptr)
            (void) prepareInjectedBankEntry (grooves[i], /*requireFill=*/0);

    for (int i = 0; i < numFills; ++i)
        if (fills != nullptr)
            (void) prepareInjectedBankEntry (fills[i], /*requireFill=*/1);

    // Select the first accepted groove so a bank prepared once renders exactly
    // like prepareInjectedGroove(). A bank with no groove cannot render.
    for (int i = 0; i < injBankCount_; ++i)
        if (injBank_[i].valid && ! injBank_[i].isFill)
        {
            injSelGrooveSlot_ = i;
            injGroove_ = injBank_[i].index;
            injCurrentSlot_ = i;
            injRevertSlot_ = i;
            injPatternReady_ = true;
            break;
        }

    if (! injPatternReady_)
        injGroove_ = jam::kNoLibraryEntry;

    return injBankCount_;
}

int DrumEngine::prepareInjectedBankEntry (jam::LibraryIndex index, int requireFill)
{
    if (index < 0)
        return -1;

    const auto& lib = drum::library();
    if (index >= static_cast<jam::LibraryIndex> (lib.size()))
        return -1;

    const auto& g = lib[static_cast<std::size_t> (index)];
    // This task renders exactly the 4/4 grid the bridge assumes; a different
    // meter would reinterpret the phase and is refused rather than mistimed.
    if (g.num != 4 || g.den != 4)
        return -1;

    const bool isFill = g.fill;
    if (requireFill == 0 && isFill)
        return -1;  // a fill was listed as a groove
    if (requireFill == 1 && ! isFill)
        return -1;  // a groove was listed as a fill

    // Reuse an already-resolved identical entry (duplicates collapse).
    for (int i = 0; i < injBankCount_; ++i)
        if (injBank_[i].valid && injBank_[i].index == index && injBank_[i].isFill == isFill)
            return i;

    if (injBankCount_ >= kMaxInjectedBankPatterns)
        return -1; // bounded bank is full: the extra entry is skipped, not guessed

    const int slot = injBankCount_++;
    auto& entry = injBank_[slot];
    juce::uint8 parsed[drum::numVoices][drum::maxStepsPerBar];
    drum::parseSpec (g, parsed);
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < drum::maxStepsPerBar; ++s)
            entry.pattern[v][s] = parsed[v][s];
    entry.index = index;
    entry.isFill = isFill;
    entry.barSteps = drum::stepsPerBar;
    entry.valid = true;
    return slot;
}

int DrumEngine::findInjectedSlot (jam::LibraryIndex index, bool wantFill) const noexcept
{
    if (index < 0)
        return -1;
    for (int i = 0; i < injBankCount_; ++i)
        if (injBank_[i].valid && injBank_[i].index == index && injBank_[i].isFill == wantFill)
            return i;
    return -1;
}

jam::LibraryIndex DrumEngine::injectedSelectedGroove() const noexcept
{
    if (injSelGrooveSlot_ >= 0 && injSelGrooveSlot_ < injBankCount_)
        return injBank_[injSelGrooveSlot_].index;
    return jam::kNoLibraryEntry;
}

jam::LibraryIndex DrumEngine::injectedActiveFill() const noexcept
{
    if (injFillActive_ && injCurrentSlot_ >= 0 && injCurrentSlot_ < injBankCount_)
        return injBank_[injCurrentSlot_].index;
    return jam::kNoLibraryEntry;
}

std::uint64_t DrumEngine::injectedDropCount() const noexcept
{
    if (clockQueue_ == nullptr)
        return 0;
    const std::uint64_t raw = clockQueue_->droppedCount();
    return raw >= injDropBaseline_ ? raw - injDropBaseline_ : 0;
}

void DrumEngine::resetInjectedTransport() noexcept
{
    injActive_ = false;
    injPlaying_ = false;
    injBpm_ = 100.0;
    injNextStep_ = 0;
    injPlayBar_ = 0;
    injSamplesToNext_ = 0.0;
    injSample_ = 0;
    injLastStepSample_ = 0;
    injStepsFired_ = 0;
    injCommands_ = 0;
    injRejected_ = 0;
    injLateCommandCount_ = 0;
    injBarChangeCount_ = 0;
    injStaleCommandCount_ = 0;
    injEventCount_ = 0;
    // Adaptive state is per-engagement: a fresh injected transport renders the
    // selected groove with the exact pre-adaptive slice until a BarChange says
    // otherwise. The prepared bank and selected groove survive (a re-prepare
    // must not forget the prepared groove).
    injFillActive_ = false;
    injCurrentSlot_ = injSelGrooveSlot_;
    injRevertSlot_ = injSelGrooveSlot_;
    resetInjectedAdaptive();
    injDropBaseline_ = clockQueue_ != nullptr ? clockQueue_->droppedCount() : 0;
}

void DrumEngine::resetInjectedState() noexcept
{
    resetInjectedTransport();

    injPatternReady_ = false;
    injGroove_ = jam::kNoLibraryEntry;
    injBankCount_ = 0;
    injSelGrooveSlot_ = -1;
    injCurrentSlot_ = -1;
    injRevertSlot_ = -1;
    for (auto& entry : injBank_)
    {
        entry.valid = false;
        entry.index = jam::kNoLibraryEntry;
        entry.isFill = false;
        for (int v = 0; v < drum::numVoices; ++v)
            for (int s = 0; s < drum::maxStepsPerBar; ++s)
                entry.pattern[v][s] = 0;
    }

    injBarSteps_ = drum::stepsPerBar;
    injPatternBars_ = 1;
}

void DrumEngine::resetInjectedAdaptive() noexcept
{
    injAdaptive_ = false;
    injIntensity01_ = 0.5f;
    injSwing01_ = 0.0f;
    injHumanVel_ = 0.0f;
    injHumanTime_ = 0.0f;
    injHumanRR_ = 0.0f;
}

void DrumEngine::endInjectedFillIfDue() noexcept
{
    if (! injFillActive_)
        return;
    // The fill has played its one bar: return to the selected groove for the
    // next downbeat. The switch happens when the bar wraps, i.e. exactly at the
    // bar boundary, so the change is never mid-bar.
    injFillActive_ = false;
    injCurrentSlot_ = injRevertSlot_ >= 0 ? injRevertSlot_ : injSelGrooveSlot_;
}

void DrumEngine::flushInjectedNotes (juce::AudioPluginInstance* vst,
                                     juce::MidiBuffer& midi, int offset) noexcept
{
    // A stop/Clear releases the hosted kit at the exact sample. This runs the
    // moment the stop/clear is applied, so a later Join in the same callback
    // inserts its note-ons after the release (MidiBuffer preserves insertion
    // order for equal timestamps).
    if (vst != nullptr)
        for (int v = 0; v < drum::numVoices; ++v)
            midi.addEvent (juce::MidiMessage::noteOff (10, drum::gmNote[v]), offset);
    for (auto& o : pendingOffs)
        o.note = -1;
}

void DrumEngine::dropInjectedEventsUpTo (std::uint64_t sample) noexcept
{
    int out = 0;
    for (int i = 0; i < injEventCount_; ++i)
    {
        const bool isBarChange =
            injEvents_[i].type == jam::DrumClockCommandType::BarChange;
        // A BarChange aimed exactly at the join boundary is the pattern for the
        // bar being joined, not a stale event, so it survives the join. Anything
        // strictly older is superseded.
        const bool keep = injEvents_[i].target > sample
                       || (isBarChange && injEvents_[i].target == sample);
        if (keep)
            injEvents_[out++] = injEvents_[i];
    }
    injEventCount_ = out;
}

int DrumEngine::serviceInjectedClock (juce::AudioPluginInstance* vst,
                                      juce::MidiBuffer& midi) noexcept
{
    if (clockQueue_ == nullptr)
        return 0;

    int applied = 0;
    jam::DrumClockCommand command;
    for (int i = 0; i < kMaxInjectedCommandsPerBlock; ++i)
    {
        if (! clockQueue_->pop (command))
            break;

        if (applyInjectedCommand (command, vst, midi))
        {
            ++applied;
            ++injCommands_;
        }
        else
        {
            ++injRejected_; // rejected whole, counted, never partially applied
        }
    }
    return applied;
}

bool DrumEngine::applyInjectedCommand (const jam::DrumClockCommand& command,
                                       juce::AudioPluginInstance* vst,
                                       juce::MidiBuffer& midi) noexcept
{
    switch (command.type)
    {
        case jam::DrumClockCommandType::JoinAtBar:
        {
            if (! injPatternReady_)
                return false;
            // A join may name any groove that is in the prepared bank (not only
            // the current selection): the orchestrator prepares a bank of
            // grooves and joins with the one it wants. An index not in the bank
            // is refused whole, exactly as before the adaptive bank existed.
            if (command.groove >= 0)
            {
                int slot = findInjectedSlot (command.groove, /*wantFill=*/false);
                if (slot < 0)
                    slot = findInjectedSlot (command.groove, /*wantFill=*/true);
                if (slot < 0)
                    return false; // a different groove was never prepared
                injSelGrooveSlot_ = slot;
                injGroove_ = command.groove;
            }
            if (! (command.bpm > 0.0) || ! std::isfinite (command.bpm))
                return false;

            // A join establishes a new grid: supersede only events at or before
            // the join, and carry the effective tempo the bridge resolved. This
            // makes the two command orders (snapshot-then-join and
            // join-then-snapshot) produce the same first bar.
            dropInjectedEventsUpTo (command.sampleTime);

            injActive_ = true;
            injPlaying_ = true;
            injBpm_ = command.bpm;
            injNextStep_ = 0;
            injPlayBar_ = 0;
            injFillActive_ = false;
            injCurrentSlot_ = injSelGrooveSlot_;
            injRevertSlot_ = injSelGrooveSlot_;
            // A join starts the exact pre-adaptive slice; the director must
            // command intensity/swing again if it wants them.
            resetInjectedAdaptive();
            injSamplesToNext_ = command.sampleTime > injSample_
                ? static_cast<double> (command.sampleTime - injSample_)
                : 0.0;          // late join: start at the block origin
            return true;
        }

        case jam::DrumClockCommandType::BarChange:
        {
            if (! injPatternReady_)
                return false;
            if (command.sampleTime < injSample_)
            {
                // A bar boundary already passed: applying it now would change
                // the pattern mid-bar, which the contract forbids. Count it late
                // and reject it whole.
                ++injLateCommandCount_;
                return false;
            }
            // Numeric domain (defence in depth; the bridge already screened it).
            if (! std::isfinite (command.intensity01)
                || ! std::isfinite (command.swing01)
                || ! std::isfinite (command.humanizeVelocity)
                || ! std::isfinite (command.humanizeTiming)
                || ! std::isfinite (command.humanizeRoundRobin))
                return false;

            // A named groove must be a prepared groove, a named fill a prepared
            // fill. Anything not in the immutable bank is rejected whole so the
            // engine can never render an unprepared (unvalidated) pattern.
            if (jam::hasField (command.changeFields, jam::DrumChangeField::Groove)
                && command.groove != jam::kNoLibraryEntry
                && findInjectedSlot (command.groove, /*wantFill=*/false) < 0)
            {
                ++injStaleCommandCount_;
                return false;
            }
            if (jam::hasField (command.changeFields, jam::DrumChangeField::Fill)
                && command.fill != jam::kNoLibraryEntry
                && findInjectedSlot (command.fill, /*wantFill=*/true) < 0)
            {
                ++injStaleCommandCount_;
                return false;
            }

            return insertInjectedEvent (command);
        }

        case jam::DrumClockCommandType::SetTempo:
            if (! (command.bpm > 0.0) || ! std::isfinite (command.bpm))
                return false;
            return insertInjectedEvent (command);

        case jam::DrumClockCommandType::StopAtBar:
            return insertInjectedEvent (command);

        case jam::DrumClockCommandType::ResyncBeat:
        case jam::DrumClockCommandType::ResyncBar:
            if (command.phaseStep < 0 || command.phaseStep >= injBarSteps_)
                return false;
            return insertInjectedEvent (command);

        case jam::DrumClockCommandType::Clear:
            // Discontinuity/lifecycle/stop-now: lose the injected transport,
            // keep the prepared pattern so a later Join can restart without a
            // message thread round-trip, and LEAVE INJECTED MODE so the legacy
            // manual sequencer/song controls are usable again without a device
            // prepare. Release notes immediately so a Clear+Join in the same
            // callback is ordered correctly. Future adaptive events and any
            // one-bar fill are cancelled and the adaptive overrides are dropped.
            injActive_ = false;
            injPlaying_ = false;
            injEventCount_ = 0;
            injSamplesToNext_ = 0.0;
            injFillActive_ = false;
            injCurrentSlot_ = injSelGrooveSlot_;
            injRevertSlot_ = injSelGrooveSlot_;
            resetInjectedAdaptive();
            flushInjectedNotes (vst, midi, 0);
            return true;

        case jam::DrumClockCommandType::None:
        default:
            return false;
    }
}

bool DrumEngine::insertInjectedEvent (const jam::DrumClockCommand& command) noexcept
{
    std::uint64_t target = command.sampleTime;
    if (target < injSample_)
    {
        target = injSample_; // late: apply at the current block origin
        ++injLateCommandCount_;
    }

    // Coalesce same type + same target (last wins). Repeated tempo snaps for one
    // boundary therefore cannot exhaust the bounded event capacity, and the
    // engine always ends up applying the last accepted value.
    for (int i = 0; i < injEventCount_; ++i)
    {
        if (injEvents_[i].target == target && injEvents_[i].type == command.type)
        {
            injEvents_[i].bpm = command.bpm;
            injEvents_[i].phaseStep = command.phaseStep;
            injEvents_[i].groove = command.groove;
            injEvents_[i].fill = command.fill;
            injEvents_[i].changeFields = command.changeFields;
            injEvents_[i].intensity01 = command.intensity01;
            injEvents_[i].swing01 = command.swing01;
            injEvents_[i].humanizeVelocity = command.humanizeVelocity;
            injEvents_[i].humanizeTiming = command.humanizeTiming;
            injEvents_[i].humanizeRoundRobin = command.humanizeRoundRobin;
            return true;
        }
    }

    if (injEventCount_ >= kMaxInjectedEvents)
        return false; // explicit overflow; the caller counts the rejection

    // Insertion sort by target. Distinct equal targets keep insertion order.
    int pos = injEventCount_;
    while (pos > 0 && injEvents_[pos - 1].target > target)
    {
        injEvents_[pos] = injEvents_[pos - 1];
        --pos;
    }

    injEvents_[pos].type = command.type;
    injEvents_[pos].target = target;
    injEvents_[pos].bpm = command.bpm;
    injEvents_[pos].phaseStep = command.phaseStep;
    injEvents_[pos].groove = command.groove;
    injEvents_[pos].fill = command.fill;
    injEvents_[pos].changeFields = command.changeFields;
    injEvents_[pos].intensity01 = command.intensity01;
    injEvents_[pos].swing01 = command.swing01;
    injEvents_[pos].humanizeVelocity = command.humanizeVelocity;
    injEvents_[pos].humanizeTiming = command.humanizeTiming;
    injEvents_[pos].humanizeRoundRobin = command.humanizeRoundRobin;
    ++injEventCount_;
    return true;
}

void DrumEngine::applyInjectedEvent (std::uint64_t nowSample, int offset,
                                     juce::AudioPluginInstance* vst,
                                     juce::MidiBuffer& midi) noexcept
{
    if (injEventCount_ <= 0)
        return;

    const InjectedEvent ev = injEvents_[0];
    for (int i = 1; i < injEventCount_; ++i)
        injEvents_[i - 1] = injEvents_[i];
    --injEventCount_;

    switch (ev.type)
    {
        case jam::DrumClockCommandType::SetTempo:
            // Applied exactly at the (bar) boundary, before the downbeat step
            // schedules its next interval, so already-elapsed time is untouched.
            injBpm_ = ev.bpm;
            break;

        case jam::DrumClockCommandType::BarChange:
            applyInjectedBarChange (ev);
            break;

        case jam::DrumClockCommandType::StopAtBar:
            // Bounded musical stop at the bar boundary. Leave injected mode so
            // the legacy manual transport is available again without a device
            // prepare; a later JoinAtBar re-engages it. Future adaptive events
            // (including a queued BarChange/Fill) are cancelled and the adaptive
            // overrides are dropped so a later join starts from the default slice.
            injActive_ = false;
            injPlaying_ = false;
            injEventCount_ = 0;
            injFillActive_ = false;
            injCurrentSlot_ = injSelGrooveSlot_;
            injRevertSlot_ = injSelGrooveSlot_;
            resetInjectedAdaptive();
            flushInjectedNotes (vst, midi, offset); // exact ordered release
            break;

        case jam::DrumClockCommandType::ResyncBar:
        case jam::DrumClockCommandType::ResyncBeat:
            // Single absolute phase statement, computed by the worker with the
            // same floor rule, so both grids agree on every later downbeat.
            if (ev.phaseStep >= 0 && ev.phaseStep < injBarSteps_)
                injNextStep_ = ev.phaseStep;
            injSamplesToNext_ = ev.target > nowSample
                ? static_cast<double> (ev.target - nowSample) : 0.0;
            break;

        default:
            break;
    }
}

void DrumEngine::applyInjectedBarChange (const InjectedEvent& ev) noexcept
{
    const bool grooveSet = jam::hasField (ev.changeFields, jam::DrumChangeField::Groove);
    const bool fillSet = jam::hasField (ev.changeFields, jam::DrumChangeField::Fill);
    const bool paramsSet = jam::hasField (ev.changeFields, jam::DrumChangeField::Params);

    // Groove first: a fill reverts to whatever groove this bar selects.
    if (grooveSet && ev.groove != jam::kNoLibraryEntry)
    {
        const int slot = findInjectedSlot (ev.groove, /*wantFill=*/false);
        if (slot >= 0)
        {
            injSelGrooveSlot_ = slot;
            injGroove_ = ev.groove;
        }
    }

    if (fillSet)
    {
        int fillSlot = -1;
        if (ev.fill != jam::kNoLibraryEntry)
            fillSlot = findInjectedSlot (ev.fill, /*wantFill=*/true);

        if (fillSlot >= 0)
        {
            // One-bar fill, then back to the selected groove at the next bar.
            injCurrentSlot_ = fillSlot;
            injRevertSlot_ = injSelGrooveSlot_;
            injFillActive_ = true;
        }
        else
        {
            // Explicit "no fill".
            injCurrentSlot_ = injSelGrooveSlot_;
            injFillActive_ = false;
        }
    }
    else if (grooveSet)
    {
        injCurrentSlot_ = injSelGrooveSlot_;
        injFillActive_ = false;
    }

    if (paramsSet)
    {
        injAdaptive_ = true;
        injIntensity01_ = juce::jlimit (0.0f, 1.0f, ev.intensity01);
        injSwing01_ = juce::jlimit (0.0f, 1.0f, ev.swing01);
        injHumanVel_ = juce::jlimit (0.0f, 1.0f, ev.humanizeVelocity);
        injHumanTime_ = juce::jlimit (0.0f, 1.0f, ev.humanizeTiming);
        injHumanRR_ = juce::jlimit (0.0f, 1.0f, ev.humanizeRoundRobin);
    }

    ++injBarChangeCount_;
}

void DrumEngine::runInjectedLoop (int n, juce::AudioPluginInstance* vst,
                                  juce::MidiBuffer& midi) noexcept
{
    const std::uint64_t blockStart = injSample_;
    double pos = 0.0;

    while (pos < static_cast<double> (n))
    {
        const std::uint64_t abs = blockStart + static_cast<std::uint64_t> (pos);
        const int offset = juce::jlimit (0, n - 1, static_cast<int> (pos));

        // Apply every event that is due at (or before) the current sample before
        // firing the step, so a tempo change at a downbeat takes effect on that
        // downbeat's next interval.
        while (injEventCount_ > 0 && injEvents_[0].target <= abs)
            applyInjectedEvent (abs, offset, vst, midi);

        if (! injPlaying_)
        {
            if (injEventCount_ == 0)
                break;
            const std::uint64_t nextTarget = injEvents_[0].target;
            double advance = nextTarget > abs
                ? static_cast<double> (nextTarget - abs) : 0.0;
            advance = juce::jmin (advance, static_cast<double> (n) - pos);
            if (advance <= 0.0)
            {
                applyInjectedEvent (abs, offset, vst, midi);
                continue;
            }
            pos += advance;
            continue;
        }

        if (injSamplesToNext_ <= 0.5)
        {
            fireInjectedStep (injNextStep_, offset, vst, midi);
            injLastStepSample_ = abs;
            ++injStepsFired_;

            injSamplesToNext_ += injectedStepLen (injNextStep_);
            if (++injNextStep_ >= injBarSteps_)
            {
                injNextStep_ = 0;
                // A one-bar fill ends exactly on the bar boundary: the next step
                // 0 is rendered from the selected groove again.
                endInjectedFillIfDue();
                injPlayBar_ = (injPlayBar_ + 1) % injPatternBars_;
            }
            continue;
        }

        const double blockRemaining = static_cast<double> (n) - pos;
        double advance = juce::jmin (injSamplesToNext_, blockRemaining);

        // Do not step over an event that falls before the next step deadline:
        // a resync target is not necessarily on the step grid, and applying it
        // at the next step instead of at the target would mistime it.
        if (injEventCount_ > 0 && injEvents_[0].target > abs)
        {
            const double toEvent = static_cast<double> (injEvents_[0].target - abs);
            if (advance > toEvent)
                advance = toEvent;
        }

        if (advance <= 0.0)
        {
            applyInjectedEvent (abs, offset, vst, midi);
            continue;
        }

        injSamplesToNext_ -= advance;
        pos += advance;
    }
}

void DrumEngine::fireInjectedStep (int step, int sampleOffset,
                                   juce::AudioPluginInstance* vst,
                                   juce::MidiBuffer& midi) noexcept
{
    if (injCurrentSlot_ < 0 || injCurrentSlot_ >= injBankCount_)
        return;
    const auto& pattern = injBank_[injCurrentSlot_].pattern;
    if (step < 0 || step >= injBank_[injCurrentSlot_].barSteps)
        return;
    for (int v = 0; v < drum::numVoices; ++v)
        fireHit (v, pattern[v][step], sampleOffset, vst, midi);
}

double DrumEngine::injectedStepLen (int stepIdx) const noexcept
{
    // Injected grid: the bridge carries tempo only, so without an adaptive
    // swing command every sixteenth is the same length. When a swing amount was
    // commanded, even sixteenths lengthen and odd ones shorten by the SAME sw
    // fraction, so the eight pairs in a 4/4 bar sum to exactly 16 base steps and
    // the next downbeat is unmoved: swing never undermines the clock phase.
    const double b = juce::jlimit (40.0f, 260.0f, static_cast<float> (injBpm_));
    const double base = 60.0 / b / 4.0 * sr;
    if (injAdaptive_ && injSwing01_ > 0.0f)
    {
        const double sw = juce::jlimit (0.0, 1.0, static_cast<double> (injSwing01_))
                          * 60.0 * 0.009;
        return (stepIdx % 2 == 0) ? base * (1.0 + sw) : base * (1.0 - sw);
    }
    return base;
}

//==============================================================================
// Síntese de reserva (usada só se os samples embutidos faltarem) + clique.
float DrumEngine::synthSample (SynthVoice& v) const
{
    struct P { float f0, f1, fDec, oscAmp, oscDec, nzAmp, nzDec, nzHp, dur; };
    static const P table[11] = {
        { 110, 42, 0.075f, 1.05f, 0.11f, 0.30f, 0.012f, 0.30f, 0.45f }, // bumbo
        { 190, 160, 0.05f, 0.30f, 0.08f, 0.85f, 0.075f, 0.55f, 0.30f }, // caixa
        { 0, 0, 1, 0, 1, 0.45f, 0.022f, 0.90f, 0.09f },                 // chimbal
        { 0, 0, 1, 0, 1, 0.28f, 0.016f, 0.90f, 0.06f },                 // pedal
        { 5200, 5200, 1, 0.04f, 0.25f, 0.22f, 0.16f, 0.80f, 0.60f },    // ride
        { 4300, 4300, 1, 0.03f, 0.40f, 0.42f, 0.42f, 0.70f, 1.40f },    // crash
        { 230, 150, 0.10f, 0.80f, 0.10f, 0.10f, 0.02f, 0.40f, 0.35f },  // tom 1
        { 185, 118, 0.11f, 0.80f, 0.11f, 0.10f, 0.02f, 0.40f, 0.38f },  // tom 2
        { 140, 85, 0.13f, 0.90f, 0.13f, 0.10f, 0.02f, 0.40f, 0.45f },   // surdo
        { 1600, 1600, 1, 0.14f, 0.012f, 0, 1, 0, 0.05f },               // clique
        { 2100, 2100, 1, 0.16f, 0.012f, 0, 1, 0, 0.05f },               // clique forte
    };
    const P& p = table[juce::jlimit (0, 10, v.type)];

    const float t = (float) v.t;
    v.t += 1.0 / sr;
    if (t > p.dur)
    {
        v.active = false;
        return 0.0f;
    }

    float s = 0.0f;
    if (p.oscAmp > 0.0f)
    {
        const float f = p.f1 + (p.f0 - p.f1) * std::exp (-t / p.fDec);
        v.phase += (double) f / sr;
        s += p.oscAmp * std::exp (-t / p.oscDec)
             * std::sin ((float) (v.phase * juce::MathConstants<double>::twoPi));
    }
    if (p.nzAmp > 0.0f)
    {
        v.noise = v.noise * 1664525u + 1013904223u;
        const float white = ((float) (v.noise >> 9) / 4194304.0f) - 1.0f;
        v.hpState += p.nzHp * (white - v.hpState);
        s += p.nzAmp * std::exp (-t / p.nzDec) * (white - v.hpState);
    }
    return s * v.vel * 0.9f;
}

//==============================================================================
juce::String DrumEngine::barToString (int bar) const
{
    const int b = juce::jlimit (0, drum::maxBars - 1, bar);
    if (! barUsed[b].load())
        return {};
    const int steps = barSteps (b);
    juce::String out;
    out.preallocateBytes (drum::numVoices * drum::maxStepsPerBar + 8);
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < steps; ++s)
            out << juce::String ((int) pattern[b][v][s].load());
    return out;
}

void DrumEngine::barFromString (const juce::String& str, int bar)
{
    const int b = juce::jlimit (0, drum::maxBars - 1, bar);
    if (str.isEmpty())
    {
        clearBar (b);
        return;
    }
    const int steps = barSteps (b);   // a métrica já deve estar setada
    int i = 0;
    for (int v = 0; v < drum::numVoices; ++v)
    {
        for (int s = 0; s < steps; ++s)
        {
            const juce::juce_wchar c = i < str.length() ? str[i] : '0';
            pattern[b][v][s].store (c >= '0' && c <= '3' ? (juce::uint8) (c - '0') : 0);
            ++i;
        }
        for (int s = steps; s < drum::maxStepsPerBar; ++s)
            pattern[b][v][s].store (0);
    }
    barUsed[b].store (true);
}

void DrumEngine::setBarPattern (const juce::uint8 p[drum::numVoices][drum::maxStepsPerBar],
                                int bar)
{
    const int b = juce::jlimit (0, drum::maxBars - 1, bar);
    const int steps = barSteps (b);   // preenche os steps do compasso; zera o resto
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < drum::maxStepsPerBar; ++s)
            pattern[b][v][s].store (s < steps ? p[v][s] : (juce::uint8) 0);
    barUsed[b].store (true);
}

void DrumEngine::clearBar (int bar)
{
    const int b = juce::jlimit (0, drum::maxBars - 1, bar);
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < drum::maxStepsPerBar; ++s)
            pattern[b][v][s].store (0);
    barUsed[b].store (false);
}

void DrumEngine::setMeter (int bar, int num, int den)
{
    const int b = juce::jlimit (0, drum::maxBars - 1, bar);
    barNum[b].store (juce::jlimit (1, 16, num));
    barDen[b].store (den);
}
