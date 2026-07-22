#include "PluginCatalog.h"

#include <juce_events/juce_events.h>

namespace plugcat
{

const std::vector<Entry>& entries()
{
    // Versões PINADAS — atualizar aqui (e no plugins/README.md) ao subir.
    // url terminando em .zip = instala direto (sem admin); .exe = baixa e
    // abre o INSTALADOR OFICIAL; vazia = abre o site. checkBundle aceita
    // wildcard (*).
    static const std::vector<Entry> list = {
        // ---- coleções (já validadas no pacote offline do repo)
        { "dragonfly", "Dragonfly Reverb", "Reverb & Ambi\xc3\xaancia", "GPLv3", "3.2.10",
          "https://github.com/michaelwillis/dragonfly-reverb/releases/download/3.2.10/dragonfly-reverb-3.2.10-win64.zip",
          "https://michaelwillis.github.io/dragonfly-reverb/",
          "DragonflyPlateReverb.vst3", 21 },
        { "airwindows", "Airwindows Consolidated", "Cole\xc3\xa7\xc3\xa3o Airwindows", "MIT", "2026-07-19",
          "https://github.com/baconpaul/airwin2rack/releases/download/DAWPlugin/AirwindowsConsolidated-2026-07-19-e4c4ca2-Windows.zip",
          "https://github.com/baconpaul/airwin2rack",
          "Airwindows Consolidated.vst3", 13 },
        { "zam", "Zam Plugins", "Pedais & Din\xc3\xa2mica (Zam)", "GPLv2+", "4.5",
          "https://github.com/zamaudio/zam-plugins/releases/download/4.5/zam-plugins-4.5-win64.zip",
          "https://www.zamaudio.com/",
          "ZamTube.vst3", 122 },

        // ---- pedais open source (zip: instala direto, sem admin)
        { "aidax", "AIDA-X (player neural)", "Neural / captures", "GPLv3", "1.1.0",
          "https://github.com/AidaDSP/AIDA-X/releases/download/1.1.0/AIDA-X-1.1.0-win64.zip",
          "https://github.com/AidaDSP/AIDA-X", "AIDA-X.vst3", 7 },
        { "fire", "Fire (distor\xc3\xa7\xc3\xa3o multibanda)", "Drives & Pedais", "GPLv3", "1.5.0",
          "https://github.com/jerryuhoo/Fire/releases/download/v1.5.0/Fire-1.5.0-Windows.zip",
          "https://github.com/jerryuhoo/Fire", "Fire.vst3", 7 },
        { "wolfshaper", "Wolf Shaper (waveshaper)", "Drives & Pedais", "GPLv3", "1.0.2",
          "https://github.com/wolf-plugins/wolf-shaper/releases/download/v1.0.2/wolf-shaper-v1.0.2%2B20230515144200-windows-x64.zip",
          "https://github.com/wolf-plugins/wolf-shaper", "wolf-shaper.vst3", 5 },
        { "peakeater", "PeakEater (clipper)", "Drives & Pedais", "GPLv3", "0.8.2",
          "https://github.com/vvvar/PeakEater/releases/download/v0.8.2/peakeater-v0.8.2-Windows-x86_64.zip",
          "https://github.com/vvvar/PeakEater", "peakeater.vst3", 4 },

        // ---- pedais open source (o projeto só publica instalador .exe:
        //      o app baixa e abre o instalador OFICIAL — pede admin dele)
        { "byod", "BYOD (pedalboard modular)", "Drives & Pedais", "GPLv3", "1.3.0",
          "https://github.com/Chowdhury-DSP/BYOD/releases/download/v1.3.0/BYOD-Win-64bit-1.3.0.exe",
          "https://github.com/Chowdhury-DSP/BYOD", "BYOD*.vst3", 25 },
        { "chowcentaur", "ChowCentaur (Klon Centaur)", "Drives & Pedais", "GPLv3", "1.4.0",
          "https://github.com/jatinchowdhury18/KlonCentaur/releases/download/v1.4.0/ChowCentaur-Win-1.4.0.exe",
          "https://github.com/jatinchowdhury18/KlonCentaur", "ChowCentaur*.vst3", 9 },
        { "chowtape", "ChowTapeModel (fita)", "Tape & Cor", "GPLv3", "2.11.4",
          "https://github.com/jatinchowdhury18/AnalogTapeModel/releases/download/v2.11.4/ChowTapeModel-Win-64bit-2.11.4.exe",
          "https://github.com/jatinchowdhury18/AnalogTapeModel", "CHOWTapeModel*.vst3", 10 },
        { "chowphaser", "ChowPhaser", "Modula\xc3\xa7\xc3\xa3o", "GPLv3", "1.1.1",
          "https://github.com/jatinchowdhury18/ChowPhaser/releases/download/v1.1.1/ChowPhaser-Win-1.1.1.exe",
          "https://github.com/jatinchowdhury18/ChowPhaser", "ChowPhaser*.vst3", 8 },
        { "tsm1n3", "TS-M1N3 (Tube Screamer neural)", "Drives & Pedais", "GPLv3", "1.2.0",
          "https://github.com/GuitarML/TS-M1N3/releases/download/v1.2.0/TS-M1N3-Win-1.2.0.exe",
          "https://github.com/GuitarML/TS-M1N3", "TS-M1N3*.vst3", 5 },
        { "proteus", "Proteus (player neural GuitarML)", "Neural / captures", "GPLv3", "1.2",
          "https://github.com/GuitarML/Proteus/releases/download/v1.2/Proteus-Win-1.2.0.exe",
          "https://github.com/GuitarML/Proteus", "Proteus*.vst3", 13 },
        { "valentine", "Valentine (compressor)", "Est\xc3\xba""dio", "GPLv3", "1.0.1",
          "https://github.com/tote-bag-labs/valentine/releases/download/v1.0.1/valentine-1.0.1-windows.exe",
          "https://github.com/tote-bag-labs/valentine", "Valentine*.vst3", 3 },

        // ---- download pelo site oficial
        { "ojd", "Schrammel OJD (overdrive)", "Drives & Pedais", "GPLv3", "",
          "", "https://schrammel.io", "OJD*.vst3", 0 },
        { "temper", "Temper (satura\xc3\xa7\xc3\xa3o)", "Drives & Pedais", "GPLv3", "",
          "", "https://creativeintent.co/product/temper", "Temper*.vst3", 0 },
        { "swankyamp", "Swanky Amp (amp valvulado)", "Amp sims", "GPLv3", "",
          "", "https://www.resonantdsp.com/swankyamp/", "SwankyAmp*.vst3", 0 },
        { "lsp", "LSP Plugins", "Est\xc3\xba""dio", "LGPLv3", "",
          "", "https://lsp-plug.in", "lsp-plugins*.vst3", 0 },
        { "valhalla", "Valhalla Supermassive", "Reverb & Ambi\xc3\xaancia", "freeware", "",
          "", "https://valhalladsp.com/shop/reverb/valhalla-supermassive/",
          "ValhallaSupermassive*.vst3", 0 },
    };
    return list;
}

juce::File systemVst3Dir()
{
    return juce::File ("C:\\Program Files\\Common Files\\VST3");
}

juce::File userVst3Dir()
{
    return juce::File::getSpecialLocation (juce::File::windowsLocalAppData)
        .getChildFile ("Programs").getChildFile ("Common").getChildFile ("VST3");
}

static bool dirHasBundle (const juce::File& dir, const juce::String& pattern)
{
    if (! dir.isDirectory())
        return false;
    if (! pattern.containsChar ('*'))
        return dir.getChildFile (pattern).exists();
    return ! dir.findChildFiles (juce::File::findFilesAndDirectories, false, pattern).isEmpty();
}

bool isInstalled (const Entry& e)
{
    return dirHasBundle (systemVst3Dir(), e.checkBundle)
           || dirHasBundle (userVst3Dir(), e.checkBundle);
}

static juce::File manifestFile()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("PedalForge NAM").getChildFile ("plugins.json");
}

juce::String installedVersion (const Entry& e)
{
    const auto parsed = juce::JSON::parse (manifestFile().loadFileAsString());
    const auto v = parsed.getProperty (e.id, "");
    if (v.isObject())
        return v.getProperty ("version", "").toString();
    return v.toString(); // formato antigo: só a versão
}

juce::StringArray installedBundles (const Entry& e)
{
    juce::StringArray out;
    const auto parsed = juce::JSON::parse (manifestFile().loadFileAsString());
    const auto v = parsed.getProperty (e.id, "");
    if (auto* arr = v.getProperty ("bundles", juce::var()).getArray())
        for (const auto& b : *arr)
            out.add (b.toString());
    return out;
}

static void writeManifest (const Entry& e, const juce::StringArray& bundles)
{
    auto parsed = juce::JSON::parse (manifestFile().loadFileAsString());
    juce::var root = parsed.getDynamicObject() != nullptr
                         ? parsed
                         : juce::var (new juce::DynamicObject());

    auto* item = new juce::DynamicObject();
    item->setProperty ("version", juce::String (e.version));
    juce::Array<juce::var> arr;
    for (const auto& b : bundles)
        arr.add (b);
    item->setProperty ("bundles", arr);

    root.getDynamicObject()->setProperty (e.id, juce::var (item));
    manifestFile().getParentDirectory().createDirectory();
    manifestFile().replaceWithText (juce::JSON::toString (root, true));
}

bool canUninstall (const Entry& e)
{
    // só desinstalamos o que está na pasta do usuário (sistema exige admin)
    auto bundles = installedBundles (e);
    if (bundles.isEmpty())
        return dirHasBundle (userVst3Dir(), e.checkBundle);
    for (const auto& b : bundles)
        if (userVst3Dir().getChildFile (b).exists())
            return true;
    return false;
}

bool uninstall (const Entry& e, juce::String& error)
{
    auto bundles = installedBundles (e);
    if (bundles.isEmpty()) // sem manifesto: resolve o wildcard na pasta do usuário
        for (const auto& f : userVst3Dir().findChildFiles (
                 juce::File::findFilesAndDirectories, false, e.checkBundle))
            bundles.add (f.getFileName());

    bool anyDeleted = false, anyFailed = false;
    for (const auto& b : bundles)
    {
        auto f = userVst3Dir().getChildFile (b);
        if (! f.exists())
            continue;
        if (f.deleteRecursively())
            anyDeleted = true;
        else
            anyFailed = true; // em uso? (módulo ainda carregado)
    }

    if (anyFailed)
    {
        error = juce::String (juce::CharPointer_UTF8 (
            "Arquivo em uso \xe2\x80\x94 remova o plugin dos slots e tente de novo"));
        return false;
    }
    if (! anyDeleted)
    {
        error = juce::String (juce::CharPointer_UTF8 (
            "Instalado na pasta do sistema \xe2\x80\x94 remova pelo instalador/admin"));
        return false;
    }

    // limpa o manifesto
    auto parsed = juce::JSON::parse (manifestFile().loadFileAsString());
    if (auto* obj = parsed.getDynamicObject())
    {
        obj->removeProperty (e.id);
        manifestFile().replaceWithText (juce::JSON::toString (parsed, true));
    }
    return true;
}

void installAsync (const Entry& entry,
                   std::function<void (int)> onProgress,
                   std::function<void (bool, juce::String)> onDone)
{
    static juce::ThreadPool pool { 1 }; // um download por vez

    pool.addJob ([entry, onProgress, onDone]
    {
        auto progress = [onProgress] (int pct)
        {
            juce::MessageManager::callAsync ([onProgress, pct] { onProgress (pct); });
        };
        auto finish = [onDone] (bool ok, juce::String msg)
        {
            juce::MessageManager::callAsync ([onDone, ok, msg] { onDone (ok, msg); });
        };

        // ---- download do zip oficial para um temporário
        juce::WebInputStream stream (juce::URL (entry.url), false);
        stream.connect (nullptr);
        if (stream.getStatusCode() != 200)
        {
            finish (false, "Falha no download (HTTP "
                               + juce::String (stream.getStatusCode()) + ")");
            return;
        }

        const bool isExeInstaller = juce::String (entry.url).endsWithIgnoreCase (".exe");
        auto tempZip = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("pedalforge-" + juce::String (entry.id)
                                          + (isExeInstaller ? ".exe" : ".zip"));
        {
            juce::FileOutputStream out (tempZip);
            if (! out.openedOk())
            {
                finish (false, "Sem acesso ao arquivo tempor\xc3\xa1rio");
                return;
            }
            out.setPosition (0);
            out.truncate();

            const auto total = stream.getTotalLength();
            juce::HeapBlock<char> buf (1 << 16);
            juce::int64 read = 0;
            int lastPct = -1;
            while (! stream.isExhausted())
            {
                const int n = stream.read (buf.getData(), 1 << 16);
                if (n <= 0)
                    break;
                out.write (buf.getData(), (size_t) n);
                read += n;
                if (total > 0)
                {
                    const int pct = (int) (read * 90 / total); // 90% = download
                    if (pct != lastPct)
                    {
                        lastPct = pct;
                        progress (pct);
                    }
                }
            }
        }

        // ---- projeto que só publica instalador .exe: abre o oficial e o
        //      usuário segue o assistente (é o instalador dele pedindo admin)
        if (isExeInstaller)
        {
            progress (100);
            const bool launched = tempZip.startAsProcess();
            finish (launched,
                    launched ? juce::String (entry.name)
                                   + juce::String (juce::CharPointer_UTF8 (
                                       ": siga o instalador oficial que abriu"))
                             : juce::String ("Falha ao abrir o instalador"));
            return;
        }

        // ---- extrai só os bundles .vst3 (com a subárvore) no dir do usuário
        progress (92);
        juce::ZipFile zip (tempZip);
        const auto dest = userVst3Dir();
        dest.createDirectory();
        int extracted = 0;
        juce::StringArray bundles; // nomes de topo, para o manifesto/desinstalar

        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto* e = zip.getEntry (i);
            if (e == nullptr || e->filename.endsWithChar ('/'))
                continue;

            // caminho relativo a partir do segmento "*.vst3"
            juce::StringArray parts = juce::StringArray::fromTokens (e->filename, "/", "");
            int bundleIdx = -1;
            for (int p = 0; p < parts.size(); ++p)
                if (parts[p].endsWithIgnoreCase (".vst3"))
                {
                    bundleIdx = p;
                    break;
                }
            if (bundleIdx < 0)
                continue;

            bundles.addIfNotAlreadyThere (parts[bundleIdx]);

            juce::File target = dest;
            for (int p = bundleIdx; p < parts.size(); ++p)
                target = target.getChildFile (parts[p]);
            target.getParentDirectory().createDirectory();

            std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
            if (in == nullptr)
                continue;
            juce::FileOutputStream out (target);
            if (! out.openedOk())
                continue;
            out.setPosition (0);
            out.truncate();
            out.writeFromInputStream (*in, -1);
            ++extracted;
        }

        tempZip.deleteFile();

        if (extracted == 0)
        {
            finish (false, "O pacote n\xc3\xa3o continha bundles VST3");
            return;
        }

        writeManifest (entry, bundles);
        finish (true, juce::String (entry.name) + " " + entry.version + " instalado");
    });
}

} // namespace plugcat
