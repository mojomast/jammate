#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

//==============================================================================
// Módulo Bateria (fase 18) — sequencer de bateria que acompanha o guitarrista.
//
// Modelo: 2 compassos de 4/4 × 16 semicolcheias = 32 steps, 9 vozes.
// Cada célula: 0 = nada · 1 = toque · 2 = acento · 3 = ghost (mesma convenção
// do mockup em docs/design/bateria-mockup.html).
//
// A pattern vive em atomics (UI escreve, áudio lê — sem locks). O som sai do
// sampler interno sintetizado (funciona "de fábrica") ou de um VST3 de
// bateria hospedado (o processor injeta a instância; o engine só agenda o
// MIDI). O clique do metrônomo é sempre interno.
namespace drum
{

constexpr int numVoices = 9;
constexpr int numSteps = 32;
constexpr int stepsPerBar = 16;

enum Voice { kick = 0, snare, hat, hatPedal, ride, crash, tom1, tom2, floorTom };

extern const int gmNote[numVoices];             // General MIDI (canal 10)
extern const char* const voiceIds[numVoices];   // "kick"... (persistência)
extern const char* const voiceNames[numVoices]; // "Bumbo"... (UI, UTF-8)

//==============================================================================
// Biblioteca de fábrica: grooves e viradas, por gênero.
// spec compacto: vozes separadas por '|', cada uma "K:0,4!,8." —
//   K bumbo · S caixa · H chimbal · P pedal do chimbal · R ride · C crash ·
//   T tom1 · U tom2 · F surdo
//   step com sufixo '!' = acento, '.' = ghost; faixa "0-14/2" = de 0 a 14
//   pulando 2 (o sufixo vale para a faixa toda).
struct Groove
{
    const char* genre;  // "ROCK"... ("VIRADA" = fill de 1 compasso)
    const char* name;   // UTF-8
    int bpm;
    int swing;          // 0..60 (%)
    int bars;           // 1 = repete nos dois compassos; 2 = spec com 32 steps
    const char* spec;
};

const std::vector<Groove>& library();
juce::StringArray genres(); // ordem de exibição (sem "VIRADA")

/// Aplica o spec numa matriz [voz][step] (zera antes). fillOnly=true escreve
/// só no 2º compasso (steps 16..31) — usado pelas viradas.
void parseSpec (const Groove&, juce::uint8 out[numVoices][numSteps],
                bool fillOnly = false);

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

    /// true se há algo para ouvir (tocando ou caudas do sintetizador) —
    /// o processor pula o mix quando não há.
    bool isAudible() const noexcept { return playing.load() || anyVoiceActive.load(); }

    // ---- estado compartilhado UI <-> áudio ----------------------------------
    std::atomic<juce::uint8> pattern[drum::numVoices][drum::numSteps] = {};
    std::atomic<float> bpm { 104.0f };
    std::atomic<float> swingPct { 0.0f };   // 0..60
    std::atomic<float> level { 0.8f };      // 0..1.5
    std::atomic<bool> playing { false };
    std::atomic<bool> clickOn { false };
    std::atomic<bool> countInOn { false };
    std::atomic<bool> useVst { false };     // fonte: interno (false) ou VST3
    std::atomic<int> uiStep { -1 };         // playhead para a UI (-1 = parado)

    // ---- persistência (message thread; lê/escreve os atomics) ---------------
    juce::String patternToString() const;          // "0120..." 32×9 dígitos
    void patternFromString (const juce::String&);
    void setPattern (const juce::uint8 p[drum::numVoices][drum::numSteps]);

private:
    void fireStep (int step, int sampleOffset,
                   juce::AudioPluginInstance* vst, juce::MidiBuffer& midi);
    void trigger (int synthType, float vel, int delaySamples);
    double stepLenSamples (int stepIdx) const;

    double sr = 48000.0;
    double samplesToNext = 0.0;
    int nextStep = 0;
    int countInLeft = 0;
    bool wasPlaying = false;

    // note-offs pendentes para o VST (bateria é one-shot, mas mandamos o
    // off ~1/4 de segundo depois por educação com samplers que sustentam)
    struct PendingOff { int note = -1; int samplesLeft = 0; };
    PendingOff pendingOffs[64];

    // ---- sampler interno (síntese leve, sem samples externos) ---------------
    struct SynthVoice
    {
        int type = -1;       // índice drum::Voice, 9 = clique fraco, 10 = forte
        bool active = false;
        int delay = 0;       // amostras até o ataque
        double t = 0.0;      // segundos desde o ataque
        double phase = 0.0;
        float hpState = 0.0f;
        juce::uint32 noise = 22222;
        float vel = 1.0f;
    };
    static constexpr int maxSynthVoices = 32;
    SynthVoice svoices[maxSynthVoices];
    std::atomic<bool> anyVoiceActive { false };

    float synthSample (SynthVoice&) const; // avança 1 amostra da voz
};
