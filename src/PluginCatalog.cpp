#include "PluginCatalog.h"

#include <juce_events/juce_events.h>

namespace plugcat
{

const std::vector<Entry>& entries()
{
    // Versões PINADAS — atualizar aqui (e no plugins/README.md) ao subir.
    static const std::vector<Entry> list = {
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
        { "lsp", "LSP Plugins", "Est\xc3\xba""dio (LSP)", "LGPLv3", "",
          "", "https://lsp-plug.in", "lsp-plugins.vst3", 0 },
        { "valhalla", "Valhalla Supermassive", "Reverb & Ambi\xc3\xaancia", "freeware", "",
          "", "https://valhalladsp.com/shop/reverb/valhalla-supermassive/",
          "ValhallaSupermassive.vst3", 0 },
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

bool isInstalled (const Entry& e)
{
    return systemVst3Dir().getChildFile (e.checkBundle).exists()
           || userVst3Dir().getChildFile (e.checkBundle).exists();
}

static juce::File manifestFile()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("GuitarRig NAM").getChildFile ("plugins.json");
}

juce::String installedVersion (const Entry& e)
{
    const auto parsed = juce::JSON::parse (manifestFile().loadFileAsString());
    return parsed.getProperty (e.id, "").toString();
}

static void writeManifest (const Entry& e)
{
    auto parsed = juce::JSON::parse (manifestFile().loadFileAsString());
    auto* obj = parsed.getDynamicObject();
    juce::var root = obj != nullptr ? parsed : juce::var (new juce::DynamicObject());
    root.getDynamicObject()->setProperty (e.id, juce::String (e.version));
    manifestFile().getParentDirectory().createDirectory();
    manifestFile().replaceWithText (juce::JSON::toString (root, true));
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

        auto tempZip = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("guitarrig-" + juce::String (entry.id) + ".zip");
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

        // ---- extrai só os bundles .vst3 (com a subárvore) no dir do usuário
        progress (92);
        juce::ZipFile zip (tempZip);
        const auto dest = userVst3Dir();
        dest.createDirectory();
        int extracted = 0;

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

        writeManifest (entry);
        finish (true, juce::String (entry.name) + " " + entry.version + " instalado");
    });
}

} // namespace plugcat
