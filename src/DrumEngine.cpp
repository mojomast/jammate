#include "DrumEngine.h"

#include <BinaryData.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace drum
{

// General MIDI, canal 10: bumbo 36, caixa 38, chimbal fechado 42, pedal 44,
// ride 51, crash 49, tom agudo 48, tom médio 45, surdo 41
const int gmNote[numVoices] = { 36, 38, 42, 44, 51, 49, 48, 45, 41 };

const char* const voiceIds[numVoices] =
    { "kick", "snare", "hat", "hatpedal", "ride", "crash", "tom1", "tom2", "floor" };

const char* const voiceNames[numVoices] =
    { "Bumbo", "Caixa", "Chimbal", "P. Chimbal", "Ride", "Crash",
      "Tom 1", "Tom 2", "Surdo" };

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

void parseSpec (const Groove& g, juce::uint8 out[numVoices][numSteps], bool fillOnly)
{
    if (! fillOnly)
        for (int v = 0; v < numVoices; ++v)
            for (int s = 0; s < numSteps; ++s)
                out[v][s] = 0;
    else
        for (int v = 0; v < numVoices; ++v)
            for (int s = stepsPerBar; s < numSteps; ++s)
                out[v][s] = 0;

    const int base = fillOnly ? stepsPerBar : 0;
    const int span = (g.bars >= 2 && ! fillOnly) ? numSteps : stepsPerBar;

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
                if (s >= 0 && s < span)
                {
                    out[v][base + s] = val;
                    // spec de 1 compasso repete no 2º (quando não é virada)
                    if (! fillOnly && g.bars < 2 && base + s + stepsPerBar < numSteps)
                        out[v][s + stepsPerBar] = val;
                }
        }
    }
}

} // namespace drum

//==============================================================================
void DrumEngine::prepare (double sampleRate, int)
{
    sr = juce::jmax (8000.0, sampleRate);
    samplesToNext = 0.0;
    nextStep = 0;
    wasPlaying = false;
    countInLeft = 0;
    for (auto& v : svoices)
        v.active = false;
    for (auto& o : pendingOffs)
        o.note = -1;
    anyVoiceActive.store (false);
    uiStep.store (-1);
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
    // message thread, uma vez, antes do áudio. Decodifica os WAVs do
    // GMRockKit embutidos no binário (3 camadas de velocity por voz).
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
    // chimbal: fechado/pedal cortam o aberto (choke com fade de ~15 ms
    // para não estalar)
    if (synthType == drum::hat || synthType == drum::hatPedal)
        for (auto& v : svoices)
            if (v.active && (v.type == drum::hat || v.type == drum::hatPedal))
                v.fading = true;

    // rouba a voz mais antiga se o pool encher (32 é folga para 9 vozes)
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

    // sample real (quando embutido): camada por velocity — ghost/normal/acento
    if (synthType < drum::numVoices && samplesReady.load())
    {
        const int layerIdx = vel > 0.9f ? 2 : vel > 0.5f ? 1 : 0;
        const auto& layer = sampleLayers[synthType][layerIdx];
        if (layer.buf.getNumSamples() > 0)
        {
            slot->layer = &layer;
            // as camadas já carregam a dinâmica; só um trim leve no ghost
            slot->vel = vel > 0.5f ? 1.0f : 0.75f;
        }
    }
    anyVoiceActive.store (true);
}

void DrumEngine::advanceSection()
{
    const int nSec = juce::jlimit (1, drum::maxSections, numSections.load());
    if (songMode.load())
    {
        if (++repeatsDone >= juce::jmax (1, sectionRepeats[playSec].load()))
        {
            repeatsDone = 0;
            playSec = (playSec + 1) % nSec;
        }
    }
    else
    {
        // fora do song mode segue a seção selecionada (troca ao fim do padrão)
        playSec = juce::jlimit (0, nSec - 1, editSection.load());
    }
}

void DrumEngine::fireStep (int section, int step, int sampleOffset,
                           juce::AudioPluginInstance* vst, juce::MidiBuffer& midi)
{
    for (int v = 0; v < drum::numVoices; ++v)
    {
        const int val = pattern[section][v][step].load();
        if (val == 0)
            continue;
        const float vel = val == 2 ? 1.0f : val == 3 ? 0.28f : 0.68f;

        if (vst != nullptr)
        {
            const int note = drum::gmNote[v];
            midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) (vel * 127.0f)),
                           sampleOffset);
            for (auto& o : pendingOffs)
                if (o.note < 0)
                {
                    o.note = note;
                    o.samplesLeft = sampleOffset + (int) (0.25 * sr);
                    break;
                }
        }
        else
            trigger (v, vel, sampleOffset);
    }

    if (clickOn.load() && step % 4 == 0)
        trigger (step % drum::stepsPerBar == 0 ? 10 : 9, 1.0f, sampleOffset);
}

void DrumEngine::process (juce::AudioBuffer<float>& out, int n,
                          juce::AudioPluginInstance* vst, juce::MidiBuffer& midi)
{
    out.clear (0, 0, n);
    out.clear (1, 0, n);
    midi.clear();

    const bool playNow = playing.load();

    // ---- transporte: agenda os steps que caem neste bloco
    if (playNow)
    {
        if (! wasPlaying)
        {
            nextStep = 0;
            samplesToNext = 8.0;
            countInLeft = countInOn.load() ? drum::stepsPerBar : 0;
            repeatsDone = 0;
            playSec = songMode.load()
                          ? 0
                          : juce::jlimit (0, juce::jlimit (1, drum::maxSections,
                                                           numSections.load()) - 1,
                                          editSection.load());
            uiStep.store (-1);
        }

        double pos = 0.0;
        while (pos < (double) n)
        {
            if (samplesToNext <= 0.5)
            {
                const int offset = juce::jlimit (0, n - 1, (int) pos);
                if (countInLeft > 0)
                {
                    // 1 compasso só de clique antes da pattern
                    if (countInLeft % 4 == 0)
                        trigger (countInLeft == drum::stepsPerBar ? 10 : 9, 1.0f, offset);
                    --countInLeft;
                    samplesToNext += stepLenSamples (0);
                }
                else
                {
                    fireStep (playSec, nextStep, offset, vst, midi);
                    uiStep.store (nextStep);
                    uiSection.store (playSec);
                    samplesToNext += stepLenSamples (nextStep);
                    nextStep = (nextStep + 1) % drum::numSteps;
                    if (nextStep == 0)
                        advanceSection();
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
        uiSection.store (-1);
        if (vst != nullptr)
            for (int v = 0; v < drum::numVoices; ++v)
                midi.addEvent (juce::MidiMessage::noteOff (10, drum::gmNote[v]), 0);
        for (auto& o : pendingOffs)
            o.note = -1;
    }
    wasPlaying = playNow;

    // ---- note-offs agendados
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

    // ---- VST de bateria escreve primeiro (substitui o buffer)...
    if (vst != nullptr)
        vst->processBlock (out, midi);

    // ---- ...e o sampler interno (samples reais/síntese + clique) soma por cima
    bool any = false;
    float* L = out.getWritePointer (0);
    float* R = out.getWritePointer (1);
    for (auto& v : svoices)
    {
        if (! v.active)
            continue;
        any = true;

        // pan sutil por voz para abrir o kit
        static const float pans[11] = { 0.0f, 0.02f, 0.22f, 0.18f, 0.3f, -0.28f,
                                        -0.12f, 0.08f, 0.2f, 0.0f, 0.0f };
        const float pan = pans[juce::jlimit (0, 10, v.type)];
        const float gl = juce::jmin (1.0f, 1.0f - pan);
        const float gr = juce::jmin (1.0f, 1.0f + pan);

        if (v.layer != nullptr)
        {
            // sample real: interpolação linear + resample pela razão de taxas
            const auto& buf = v.layer->buf;
            const float* sl = buf.getReadPointer (0);
            const float* sr2 = buf.getNumChannels() > 1 ? buf.getReadPointer (1) : sl;
            const bool stereo = buf.getNumChannels() > 1;
            const double ratio = v.layer->rate / sr;
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
                // sample estéreo já traz a imagem do kit; mono usa o pan
                L[i] += (stereo ? a : a * gl) * gain;
                R[i] += (stereo ? b : b * gr) * gain;
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
            for (int i = 0; i < n && v.active; ++i)
            {
                if (v.delay > 0) { --v.delay; continue; }
                const float s = synthSample (v);
                L[i] += s * gl;
                R[i] += s * gr;
            }
        }
    }
    anyVoiceActive.store (any);
}

//==============================================================================
// Síntese por voz — leve (senoide com sweep + ruído filtrado), pensada como
// kit de trabalho: o som "bom" vem do VST; este garante que funciona sozinho.
float DrumEngine::synthSample (SynthVoice& v) const
{
    struct P { float f0, f1, fDec, oscAmp, oscDec, nzAmp, nzDec, nzHp, dur; };
    // f0->f1 (Hz) com decay fDec; amplitudes/decays (s); nzHp = alpha do
    // one-pole highpass do ruído (0..1, maior = mais agudo); dur = corte
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
juce::String DrumEngine::patternToString (int section) const
{
    const int sec = juce::jlimit (0, drum::maxSections - 1,
                                  section < 0 ? editSection.load() : section);
    juce::String out;
    out.preallocateBytes (drum::numVoices * drum::numSteps + 8);
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < drum::numSteps; ++s)
            out << juce::String ((int) pattern[sec][v][s].load());
    return out;
}

void DrumEngine::patternFromString (const juce::String& str, int section)
{
    const int sec = juce::jlimit (0, drum::maxSections - 1,
                                  section < 0 ? editSection.load() : section);
    int i = 0;
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < drum::numSteps; ++s)
        {
            const juce::juce_wchar c = i < str.length() ? str[i] : '0';
            pattern[sec][v][s].store (c >= '0' && c <= '3' ? (juce::uint8) (c - '0') : 0);
            ++i;
        }
}

void DrumEngine::setPattern (const juce::uint8 p[drum::numVoices][drum::numSteps],
                             int section)
{
    const int sec = juce::jlimit (0, drum::maxSections - 1,
                                  section < 0 ? editSection.load() : section);
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < drum::numSteps; ++s)
            pattern[sec][v][s].store (p[v][s]);
}
