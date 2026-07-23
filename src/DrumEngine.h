#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

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
extern const char* const voiceNames[numVoices]; // "Bumbo"... (UI, UTF-8)

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
};

const std::vector<Groove>& library();
juce::StringArray genres(); // ordem de exibição dos gêneros com entradas

/// Aplica o spec numa pattern de 1 compasso (zera antes).
void parseSpec (const Groove&, juce::uint8 out[numVoices][stepsPerBar]);

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

    bool isAudible() const noexcept { return playing.load() || anyVoiceActive.load(); }

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

    std::atomic<float> bpm { 104.0f };
    std::atomic<float> swingPct { 0.0f };   // 0..60
    std::atomic<float> level { 0.8f };      // 0..1.5
    std::atomic<bool> playing { false };
    std::atomic<bool> clickOn { false };
    std::atomic<bool> countInOn { false };
    std::atomic<bool> useVst { false };     // fonte: interno (false) ou VST3

    std::atomic<int> uiBar { -1 };          // compasso global tocando (-1 parado)
    std::atomic<int> uiStep { -1 };         // step dentro do compasso

    // nome do groove aplicado em cada compasso — SÓ message thread (UI e
    // persistência; o áudio nunca lê)
    juce::String barNames[drum::maxBars];

    int totalBars() const noexcept
    {
        return juce::jlimit (1, drum::maxSections, numSections.load())
               * drum::barsPerSection;
    }

    // ---- persistência/edição (message thread; via atomics) ------------------
    juce::String barToString (int bar) const;             // dígitos "0123..." (steps do compasso)
    void barFromString (const juce::String&, int bar);    // marca barUsed
    void setBarPattern (const juce::uint8 p[drum::numVoices][drum::stepsPerBar],
                        int bar);                          // grooves da lib (16 steps)
    void clearBar (int bar);

    /// Decodifica os samples embutidos do GMRockKit (GPL — ver
    /// assets/drums/ORIGEM.txt) — chamar UMA vez, na message thread.
    void loadEmbeddedSamples();

private:
    void fireStep (int bar, int step, int sampleOffset,
                   juce::AudioPluginInstance* vst, juce::MidiBuffer& midi);
    void trigger (int synthType, float vel, int delaySamples);
    double stepLenSamples (int stepIdx) const;

    double sr = 48000.0;
    double samplesToNext = 0.0;
    int nextStep = 0;    // step dentro do compasso
    int playBar = 0;     // compasso global tocando (thread de áudio)
    int countInLeft = 0;
    bool wasPlaying = false;

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
};
