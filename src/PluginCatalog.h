#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

//==============================================================================
// Catálogo EMBUTIDO de plugins VST3 recomendados (ver plugins/README.md).
// O app sabe o que está instalado, baixa dos releases oficiais e instala no
// diretório VST3 do usuário (%LOCALAPPDATA%\Programs\Common\VST3 — padrão da
// spec VST3, sem precisar de administrador).
namespace plugcat
{

struct Entry
{
    const char* id;          // "dragonfly"
    const char* name;        // nome exibido
    const char* category;    // categoria do menu (UTF-8)
    const char* license;     // "GPLv3"...
    const char* version;     // versão pinada
    const char* url;         // zip oficial ("" = instalação manual)
    const char* homepage;    // para os manuais / crédito
    const char* checkBundle; // bundle usado para detectar instalação
    int sizeMB;              // tamanho aproximado do download
    // se não-vazio, extrai SÓ os bundles que casarem este wildcard (para
    // zips que trazem extras — ex.: Surge vem com o sintetizador junto)
    const char* onlyBundle = "";
};

const std::vector<Entry>& entries();

juce::File systemVst3Dir(); // C:\Program Files\Common Files\VST3
juce::File userVst3Dir();   // %LOCALAPPDATA%\Programs\Common\VST3

bool isInstalled (const Entry&);
/// Versão registrada no manifesto (Documentos\Guitar Companion\plugins.json);
/// "" se o plugin não foi instalado pelo app.
juce::String installedVersion (const Entry&);
/// Bundles gravados no manifesto para este plugin (o que o app instalou).
juce::StringArray installedBundles (const Entry&);
/// true se dá para desinstalar pelo app (arquivos na pasta VST3 do usuário).
bool canUninstall (const Entry&);
/// Apaga os bundles do plugin da pasta VST3 do usuário e limpa o manifesto.
/// Síncrono (message thread). ATENÇÃO: descarregue dos slots antes.
bool uninstall (const Entry&, juce::String& error);

/// Baixa o zip oficial e extrai os bundles .vst3 no userVst3Dir().
/// Callbacks chegam na message thread; onProgress recebe 0..100.
void installAsync (const Entry&,
                   std::function<void (int)> onProgress,
                   std::function<void (bool, juce::String)> onDone);

} // namespace plugcat
