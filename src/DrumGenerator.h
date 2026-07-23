#pragma once

#include "DrumEngine.h"
#include <juce_core/juce_core.h>
#include <vector>

//==============================================================================
// Motor de geração de grooves do módulo Bateria (fase 19).
//
// Porta a lógica do projeto midi-drums (fsecada01, MIT — ver THIRD_PARTY.md)
// para o nosso grid de steps: a partir de gênero + estilo + baterista +
// parâmetros (0..1) + papel do compasso (verso/refrão/ponte/breakdown/virada),
// escreve 1 COMPASSO numa pattern de 9 vozes, RESPEITANDO a fórmula (num/den):
// death em 4/4, djent em 7/8, prog em 5/4… tudo cai no mesmo gerador.
//
// Determinístico: mesma `seed` => mesmo groove. A UI incrementa a semente a
// cada clique para variar. Humanização/swing NÃO são gravados aqui — são
// aplicados no playback pelos atomics do DrumEngine.
namespace drum
{

struct GenParams
{
    juce::String genre  = "METAL";     // ver genGenres()
    juce::String style  = "progressive";
    juce::String drummer;              // vazio = nenhum; senão id de genDrummers()
    juce::String role   = "verse";     // verse|chorus|bridge|breakdown|fill
    float complexity = 0.6f;           // densidade de subdivisões, ghosts, variação
    float dynamics   = 0.6f;           // contraste de acentos/ghosts
    float fillFreq   = 0.2f;           // chance de mini-viradas em compassos não-vira
    int   num = 4, den = 4;            // fórmula do compasso
    juce::uint32 seed = 1;
};

/// Gera 1 compasso em `out` (zera antes). Retorna o nº de steps preenchidos
/// (= stepsForMeter(num,den)). Cobre até maxStepsPerBar.
int generateBar (const GenParams& p, juce::uint8 out[numVoices][maxStepsPerBar]);

// ---- metadados para a UI (dropdowns) ---------------------------------------
juce::StringArray genGenres();                              // gêneros do gerador
juce::StringArray genStyles (const juce::String& genre);   // estilos do gênero

struct GenDrummer
{
    const char* id;    // "hoglan"
    const char* name;  // "Gene Hoglan"
    const char* sig;   // assinatura curta (UTF-8)
};
const std::vector<GenDrummer>& genDrummers();
bool drummerFitsGenre (const juce::String& drummerId, const juce::String& genre);

} // namespace drum
