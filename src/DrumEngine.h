#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>
#include <vector>

#include "jam/DrumClockBridge.h"

//==============================================================================
// Módulo Bateria (fase 18, layout v4) — o baterista virtual do rig.
//
// Modelo: GROOVE = 1 COMPASSO (16 semicolcheias × 9 vozes). A música é uma
// timeline de SEÇÕES de 4 compassos (até 8 seções = 32 compassos); cada
// compasso guarda sua própria cópia da pattern (célula: 0 = nada · 1 = toque ·
// 2 = acento · 3 = ghost). Compasso não usado = silêncio.
//
// A timeline vive em atomics (UI escreve, áudio lê — sem locks). O som sai do
// sampler interno (samples reais do GMRockKit embutidos, síntese de reserva)
// ou de um VST3 de bateria hospedado (o processor injeta a instância; o
// engine agenda o MIDI GM canal 10). O clique do metrônomo é sempre interno.
namespace drum
{

constexpr int numVoices = 9;
constexpr int stepsPerBar = 16;      // base 4/4 (resolução de semicolcheia)
constexpr int maxStepsPerBar = 32;   // teto p/ métricas maiores (7/4=28, 5/4=20…)
constexpr int barsPerSection = 4;
constexpr int maxSections = 8;
constexpr int maxBars = maxSections * barsPerSection; // 32

/// Nº de steps (semicolcheias) de uma métrica num/den, limitado ao teto.
inline int stepsForMeter (int num, int den) noexcept
{
    if (num < 1 || den < 1) return stepsPerBar;
    return juce::jlimit (1, maxStepsPerBar, (int) juce::roundToInt (num * 16.0 / den));
}

enum Voice { kick = 0, snare, hat, hatPedal, ride, crash, tom1, tom2, floorTom };

extern const int gmNote[numVoices];             // General MIDI (canal 10)
extern const char* const voiceIds[numVoices];   // "kick"... (persistência)
extern const char* const voiceNames[numVoices]; // "Kick"... (UI, UTF-8)

//==============================================================================
// Biblioteca de fábrica: grooves e viradas de 1 COMPASSO, por gênero.
// spec compacto: vozes separadas por '|' — K bumbo · S caixa · H chimbal ·
// P pedal · R ride · C crash · T tom1 · U tom2 · F surdo. Sufixos: '!'
// acento, '.' ghost. Faixa "0-14/2" = steps 0..14 pulando 2.
struct Groove
{
    const char* genre;  // "ROCK"... (gênero real, inclusive nas viradas)
    const char* name;   // UTF-8
    int bpm;            // 0 = mantém o andamento atual
    int swing;          // 0..60 (%)
    bool fill;          // true = virada; false = groove
    const char* spec;
    int num = 4;        // fórmula de compasso (padrão 4/4)
    int den = 4;
};

const std::vector<Groove>& library();
juce::StringArray genres(); // ordem de exibição dos gêneros com entradas

/// Aplica o spec numa pattern de 1 compasso (zera antes). out cobre até
/// maxStepsPerBar (métricas maiores que 4/4 usam mais steps).
void parseSpec (const Groove&, juce::uint8 out[numVoices][maxStepsPerBar]);

} // namespace drum

//==============================================================================
class DrumEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);

    /// Thread de áudio. Escreve (substitui) o conteúdo de `out` (estéreo,
    /// canais 0/1, n amostras): agenda os steps do bloco, toca o VST (se
    /// houver) com o MIDI agendado e soma o sampler interno + clique.
    void process (juce::AudioBuffer<float>& out, int n,
                  juce::AudioPluginInstance* drumVst, juce::MidiBuffer& midiScratch);

    bool isAudible() const noexcept
    {
        // An attached bridge means the audio callback must run even while no
        // voice is sounding: the injected transport has to service commands and
        // advance its absolute timeline, and PluginProcessor skips process()
        // when this is false and no hosted kit is present. Without the bridge
        // term, a pending Join would never be consumed on the internal-sampler
        // path.
        return clockQueue_ != nullptr
            || playing.load() || auditionOn.load() || anyVoiceActive.load();
    }

    /// True once the embedded GMRockKit samples decoded successfully (message
    /// thread / quiescent read; the internal sampler then uses real samples).
    bool samplesLoaded() const noexcept { return samplesReady.load(); }

    // ---- timeline compartilhada UI <-> áudio --------------------------------
    std::atomic<juce::uint8> pattern[drum::maxBars][drum::numVoices][drum::maxStepsPerBar] = {};
    std::atomic<bool> barUsed[drum::maxBars] = {};  // false = silêncio
    std::atomic<int> numSections { 1 };             // seções ativas (×4 compassos)
    // fórmula de compasso por compasso (0 = 4/4 padrão)
    std::atomic<int> barNum[drum::maxBars] = {};
    std::atomic<int> barDen[drum::maxBars] = {};
    int meterNum (int bar) const noexcept { const int n = barNum[bar].load(); return n > 0 ? n : 4; }
    int meterDen (int bar) const noexcept { const int d = barDen[bar].load(); return d > 0 ? d : 4; }
    int barSteps (int bar) const noexcept { return drum::stepsForMeter (meterNum (bar), meterDen (bar)); }
    void setMeter (int bar, int num, int den);

    // ---- injected musical clock (INT-DRUM-001) ------------------------------
    // The real Musical-Clock -> DrumEngine seam. The bridge (worker thread) owns
    // the bounded command queue; the engine is handed a pointer and is its single
    // audio-thread consumer. The injected transport is independent of the manual
    // UI timeline (playing/uiBar/uiStep) and only engages when a Join command
    // arrives, so standalone manual drum behavior is untouched.
    //
    // LIFECYCLE (message thread, all roles quiescent): prepare(), optionally
    // prepareInjectedGroove(), then attachClockBridge(). prepare() preserves a
    // previously prepared groove; only detachClockBridge() clears it. A join is
    // refused until a 4/4 groove is prepared.
    //
    // THREADING CONTRACT: the injected*() getters are audio-owner/quiescent-only
    // diagnostics. They read plain audio-thread state and MUST NOT be polled from
    // the message thread or the director while the callback is running.
    void attachClockBridge (jam::DrumClockCommandQueue* queue,
                            std::uint64_t audioSampleAtAttach = 0) noexcept;
    void detachClockBridge() noexcept;

    /// Message thread, all roles quiescent: resolve `drum::library()[index]` into
    /// the POD pattern the injected transport renders. Must not run during audio.
    /// Returns false for an out-of-range index or a groove that is not the 4/4
    /// contract this task supports.
    bool prepareInjectedGroove (jam::LibraryIndex index);

    bool injectedActive() const noexcept { return injActive_; }
    bool injectedPlaying() const noexcept { return injPlaying_; }
    jam::LibraryIndex injectedGroove() const noexcept { return injGroove_; }
    double injectedTempo() const noexcept { return injBpm_; }
    std::uint64_t injectedSamplePosition() const noexcept { return injSample_; }
    std::uint64_t injectedCommandCount() const noexcept { return injCommands_; }
    std::uint64_t injectedRejectedCount() const noexcept { return injRejected_; }
    std::uint64_t injectedLateCount() const noexcept { return injLateCommandCount_; }
    std::uint64_t injectedStepsFired() const noexcept { return injStepsFired_; }
    std::uint64_t injectedLastStepSample() const noexcept { return injLastStepSample_; }
    std::uint64_t injectedDropCount() const noexcept;
    int injectedNextStep() const noexcept { return injNextStep_; }

    std::atomic<float> bpm { 104.0f };
    std::atomic<float> swingPct { 0.0f };   // 0..60
    std::atomic<float> level { 0.8f };      // 0..1.5
    // KIT MIXER (vNext): per-voice gain 0..1.5 applied on the hit velocity
    // (internal sampler AND the MIDI sent to the hosted kit)
    std::atomic<float> voiceGain[drum::numVoices] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                                      1.0f, 1.0f, 1.0f, 1.0f };
    // humanização (0..1): variação de velocity, micro-timing e round-robin
    std::atomic<float> humanVel { 0.30f };
    std::atomic<float> humanTime { 0.20f };
    std::atomic<float> humanRR { 0.40f };
    std::atomic<bool> playing { false };
    std::atomic<bool> clickOn { false };
    std::atomic<bool> countInOn { false };
    std::atomic<bool> useVst { false };     // fonte: interno (false) ou VST3

    std::atomic<int> uiBar { -1 };          // compasso global tocando (-1 parado)
    std::atomic<int> uiStep { -1 };         // step dentro do compasso

    // ---- library AUDITION (vNext): loop a 1-bar pattern WITHOUT touching the
    // timeline. The UI writes auditionPat/auditionSteps and flips auditionOn;
    // while it is on, process() loops that pattern at the current BPM and the
    // timeline transport stays paused with its position preserved (it resumes
    // from the same bar/step when the audition stops).
    std::atomic<bool> auditionOn { false };
    std::atomic<juce::uint8> auditionPat[drum::numVoices][drum::maxStepsPerBar] = {};
    std::atomic<int> auditionSteps { drum::stepsPerBar };

    // per-voice trigger flash for the KIT MIXER meters: the audio thread stores
    // the hit velocity here; the UI reads (and consumes) it and decays its own
    // copy towards zero. Purely cosmetic - races are harmless.
    // NOTE: this is the TRIGGER (velocity of the scheduled hit), NOT the audio
    // level - see uiVoicePeak below for the real measurement.
    std::atomic<float> uiVoiceFlash[drum::numVoices] = {};

    // ---- REAL audio metering (vNext) ----------------------------------------
    // Everything in this block is written ONCE PER BLOCK by the audio thread
    // (never once per sample) and is meant to be DRAINED by the UI: read it
    // with exchange (0.0f), which both consumes the value and re-arms the
    // max-hold. The engine only publishes the LOUDEST peak seen since the last
    // drain (linear amplitude 0..1+, NOT dB, NOT smoothed); every bit of
    // ballistics (attack/decay/peak-hold) belongs to the UI, exactly like it
    // already does for uiVoiceFlash. Max-hold instead of a plain store because
    // several audio blocks happen per UI frame (~12 at 48 kHz / 128 samples vs
    // 30 Hz) and a plain store would drop the drum transients that live in the
    // blocks the UI never looked at.
    // While the transport is silent the processor does not even call process(),
    // so the values simply stay at 0 after the UI drains them.

    // Real peak of each drum piece, measured on the audio the INTERNAL SAMPLER
    // actually summed into the drum bus (post per-voice gain/velocity/pan,
    // pre module LEVEL). Overlapping hits of the same piece take the max, not
    // the sum. Stays at 0 whenever a hosted drum VST3 is the source (see
    // uiMixFromVst): that instance is 0-in/2-out, so per-piece audio does not
    // exist there and uiVoiceFlash is the only per-piece information available.
    std::atomic<float> uiVoicePeak[drum::numVoices] = {};

    // Real peak of the WHOLE drum bus for the last processed block. Works in
    // BOTH modes: it is measured on the output buffer at the very end of
    // process(), so it already includes the hosted VST3 and the metronome
    // click. Measured BEFORE the module LEVEL fader - multiply by level.load()
    // for a post-fader reading.
    std::atomic<float> uiMixPeak { 0.0f };

    // Source of the audio measured above for the last processed block:
    // true = hosted drum VST3 (uiVoicePeak is meaningless/zero, the UI should
    // label the per-piece meters as triggers), false = internal sampler.
    // Plain state flag - do NOT drain it, read it with load().
    std::atomic<bool> uiMixFromVst { false };

    // nome do groove aplicado em cada compasso — SÓ message thread (UI e
    // persistência; o áudio nunca lê)
    juce::String barNames[drum::maxBars];

    // papel do compasso p/ o gerador — SÓ message thread. 0 = auto (segue o
    // arco verso/refrão/ponte/virada pela posição); 1..5 = verso/refrão/ponte/
    // breakdown/virada explícitos.
    int barRole[drum::maxBars] = {};

    int totalBars() const noexcept
    {
        return juce::jlimit (1, drum::maxSections, numSections.load())
               * drum::barsPerSection;
    }

    // ---- persistência/edição (message thread; via atomics) ------------------
    juce::String barToString (int bar) const;             // dígitos "0123..." (steps do compasso)
    void barFromString (const juce::String&, int bar);    // marca barUsed
    void setBarPattern (const juce::uint8 p[drum::numVoices][drum::maxStepsPerBar],
                        int bar);                          // preenche barSteps(bar) steps
    void clearBar (int bar);

    /// Decodifica os samples embutidos do GMRockKit (GPL — ver
    /// assets/drums/ORIGEM.txt) — chamar UMA vez, na message thread.
    void loadEmbeddedSamples();

private:
    /// One hit of one voice: velocity from the cell state (hit/accent/ghost) ×
    /// voiceGain, humanized, then routed to the VST (GM MIDI) or the sampler.
    void fireHit (int voice, int val, int sampleOffset,
                  juce::AudioPluginInstance* vst, juce::MidiBuffer& midi);
    void fireStep (int bar, int step, int sampleOffset,
                   juce::AudioPluginInstance* vst, juce::MidiBuffer& midi);
    void fireAuditionStep (int step, int sampleOffset,
                           juce::AudioPluginInstance* vst, juce::MidiBuffer& midi);
    void trigger (int synthType, float vel, int delaySamples);
    double stepLenSamples (int stepIdx) const;
    float nextRnd() noexcept   // ruído barato [-1,1) — só thread de áudio
    {
        humRng ^= humRng << 13; humRng ^= humRng >> 17; humRng ^= humRng << 5;
        return ((float) (humRng >> 9) / 4194304.0f) - 1.0f;
    }
    juce::uint32 humRng = 0x2545F491u;

    double sr = 48000.0;
    double samplesToNext = 0.0;
    int nextStep = 0;    // step dentro do compasso
    int playBar = 0;     // compasso global tocando (thread de áudio)
    int countInLeft = 0;
    bool wasPlaying = false;

    // audition sequencer (audio thread only)
    double audSamplesToNext = 0.0;
    int audStep = 0;
    bool wasAudition = false;
    bool pausedByAudition = false;   // timeline paused (not stopped) by audition

    struct PendingOff { int note = -1; int samplesLeft = 0; };
    PendingOff pendingOffs[64];

    // ---- sampler interno: samples reais (GMRockKit, 3 camadas de velocity
    // por voz) com síntese leve de reserva ------------------------------------
    struct Layer
    {
        juce::AudioBuffer<float> buf;  // 1 ou 2 canais
        double rate = 44100.0;
    };
    Layer sampleLayers[drum::numVoices][3];  // [voz][ghost/normal/acento]
    std::atomic<bool> samplesReady { false };

    struct SynthVoice
    {
        int type = -1;       // índice drum::Voice, 9 = clique fraco, 10 = forte
        bool active = false;
        int delay = 0;
        const Layer* layer = nullptr; // sample em uso (null = síntese)
        double pos = 0.0;
        float rrPitch = 1.0f; // round-robin: micro-variação de afinação
        bool fading = false; // choke: fade rápido em vez de corte seco
        float fadeGain = 1.0f;
        double t = 0.0;
        double phase = 0.0;
        float hpState = 0.0f;
        juce::uint32 noise = 22222;
        float vel = 1.0f;
    };
    static constexpr int maxSynthVoices = 32;
    SynthVoice svoices[maxSynthVoices];
    std::atomic<bool> anyVoiceActive { false };

    float synthSample (SynthVoice&) const;

    // ---- injected clock engine (INT-DRUM-001) -------------------------------
    // All of this state is audio-thread owned. The worker only writes the
    // lock-free command queue; it never touches these fields.
    static constexpr int kMaxInjectedCommandsPerBlock = 4;
    static constexpr int kMaxInjectedEvents = 8;

    struct InjectedEvent
    {
        jam::DrumClockCommandType type = jam::DrumClockCommandType::None;
        std::uint64_t target = 0;   // absolute injected-timeline sample
        double bpm = 0.0;
        int phaseStep = -1;         // Resync: step within the bar that lands on target
    };

    /// Audio thread: consume up to kMaxInjectedCommandsPerBlock commands. Each
    /// command is validated as a whole; an invalid or over-capacity command is
    /// rejected and counted, never partially applied. A Clear releases hosted
    /// notes immediately, before any later Join in the same callback can fire.
    int serviceInjectedClock (juce::AudioPluginInstance* vst,
                              juce::MidiBuffer& midi) noexcept;
    bool applyInjectedCommand (const jam::DrumClockCommand& command,
                               juce::AudioPluginInstance* vst,
                               juce::MidiBuffer& midi) noexcept;

    /// Insert one bounded event. Same type + same target coalesces (last wins),
    /// so repeated tempo snaps for one boundary cannot exhaust the capacity.
    bool insertInjectedEvent (const jam::DrumClockCommand& command) noexcept;

    void applyInjectedEvent (std::uint64_t nowSample, int offset,
                             juce::AudioPluginInstance* vst,
                             juce::MidiBuffer& midi) noexcept;
    void runInjectedLoop (int n, juce::AudioPluginInstance* vst,
                          juce::MidiBuffer& midi) noexcept;
    void fireInjectedStep (int step, int sampleOffset,
                           juce::AudioPluginInstance* vst,
                           juce::MidiBuffer& midi) noexcept;
    void flushInjectedNotes (juce::AudioPluginInstance* vst,
                             juce::MidiBuffer& midi, int offset) noexcept;
    double injectedStepLen (int stepIdx) const noexcept;

    /// Remove scheduled events at or before `sample`; a fresh join supersedes
    /// only what it replaces, never tempo/resync aimed at a later boundary.
    void dropInjectedEventsUpTo (std::uint64_t sample) noexcept;

    void resetInjectedTransport() noexcept; // keeps a prepared groove
    void resetInjectedState() noexcept;     // also forgets a prepared groove

    jam::DrumClockCommandQueue* clockQueue_ = nullptr;

    bool injPatternReady_ = false;
    jam::LibraryIndex injGroove_ = jam::kNoLibraryEntry;
    juce::uint8 injPattern_[drum::numVoices][drum::maxStepsPerBar] = {};
    int injBarSteps_ = drum::stepsPerBar;
    int injPatternBars_ = 1;

    bool injActive_ = false;      // injected mode engaged by a Join
    bool injPlaying_ = false;     // injected transport currently rendering
    double injBpm_ = 100.0;
    int injNextStep_ = 0;
    int injPlayBar_ = 0;
    double injSamplesToNext_ = 0.0;

    std::uint64_t injSample_ = 0;         // absolute injected timeline
    std::uint64_t injLastStepSample_ = 0;
    std::uint64_t injStepsFired_ = 0;
    std::uint64_t injCommands_ = 0;
    std::uint64_t injRejected_ = 0;
    std::uint64_t injLateCommandCount_ = 0;
    // Shared queue drop count is cumulative; rebaselined per injected session.
    std::uint64_t injDropBaseline_ = 0;

    InjectedEvent injEvents_[kMaxInjectedEvents] = {};
    int injEventCount_ = 0;
};
