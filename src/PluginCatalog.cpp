#include "PluginCatalog.h"

#include <juce_events/juce_events.h>

namespace plugcat
{

const std::vector<Entry>& entries()
{
    // PINNED versions - update here (and in plugins/README.md) when bumping.
    // RULE (user's request): ONLY plugins with a portable DIRECT-download zip
    // are included - install by extracting the .vst3 and uninstall by deleting
    // the file, no installer. Projects that only ship an .exe (BYOD, Chow*,
    // GuitarML, Valentine) or download via a website are left out. checkBundle
    // accepts a wildcard (*).
    static const std::vector<Entry> list = {
        // ---- collections (already validated in the repo's offline bundle)
        { "dragonfly", "Dragonfly Reverb", "Reverb & Ambience", "GPLv3", "3.2.10",
          "https://github.com/michaelwillis/dragonfly-reverb/releases/download/3.2.10/dragonfly-reverb-3.2.10-win64.zip",
          "https://michaelwillis.github.io/dragonfly-reverb/",
          "DragonflyPlateReverb.vst3", 21 },
        { "airwindows", "Airwindows Consolidated", "Airwindows Collection", "MIT", "2026-07-19",
          "https://github.com/baconpaul/airwin2rack/releases/download/DAWPlugin/AirwindowsConsolidated-2026-07-19-e4c4ca2-Windows.zip",
          "https://github.com/baconpaul/airwin2rack",
          "Airwindows Consolidated.vst3", 13 },
        { "zam", "Zam Plugins", "Pedals & Dynamics (Zam)", "GPLv2+", "4.5",
          "https://github.com/zamaudio/zam-plugins/releases/download/4.5/zam-plugins-4.5-win64.zip",
          "https://www.zamaudio.com/",
          "ZamTube.vst3", 122 },

        // ---- open source pedals (zip: installs directly, no admin)
        { "aidax", "AIDA-X (neural player)", "Neural / captures", "GPLv3", "1.1.0",
          "https://github.com/AidaDSP/AIDA-X/releases/download/1.1.0/AIDA-X-1.1.0-win64.zip",
          "https://github.com/AidaDSP/AIDA-X", "AIDA-X.vst3", 7 },
        { "fire", "Fire (multiband distortion)", "Drives & Pedals", "GPLv3", "1.5.0",
          "https://github.com/jerryuhoo/Fire/releases/download/v1.5.0/Fire-1.5.0-Windows.zip",
          "https://github.com/jerryuhoo/Fire", "Fire.vst3", 7 },
        { "wolfshaper", "Wolf Shaper (waveshaper)", "Drives & Pedals", "GPLv3", "1.0.2",
          "https://github.com/wolf-plugins/wolf-shaper/releases/download/v1.0.2/wolf-shaper-v1.0.2%2B20230515144200-windows-x64.zip",
          "https://github.com/wolf-plugins/wolf-shaper", "wolf-shaper.vst3", 5 },
        { "peakeater", "PeakEater (clipper)", "Drives & Pedals", "GPLv3", "0.8.2",
          "https://github.com/vvvar/PeakEater/releases/download/v0.8.2/peakeater-v0.8.2-Windows-x86_64.zip",
          "https://github.com/vvvar/PeakEater", "peakeater.vst3", 4 },
        { "surgefx", "Surge XT Effects (multi-fx)", "Reverb & Ambience", "GPLv3", "1.3.4",
          "https://github.com/surge-synthesizer/releases-xt/releases/download/1.3.4/surge-xt-win64-1.3.4-pluginsonly.zip",
          "https://surge-synthesizer.github.io/",
          "Surge XT Effects.vst3", 49, "Surge XT Effects.vst3" },
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
    return v.toString(); // old format: just the version
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
    // we only uninstall what's in the user folder (system requires admin)
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
    if (bundles.isEmpty()) // no manifest: resolve the wildcard in the user folder
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
            anyFailed = true; // in use? (module still loaded)
    }

    if (anyFailed)
    {
        error = juce::String ("File in use - remove the plugin from the slots and try again");
        return false;
    }
    if (! anyDeleted)
    {
        error = juce::String ("Installed in the system folder - remove via the installer/admin");
        return false;
    }

    // clears the manifest
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
    static juce::ThreadPool pool { 1 }; // one download at a time

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

        // ---- download the official zip to a temp file
        juce::WebInputStream stream (juce::URL (entry.url), false);
        stream.connect (nullptr);
        if (stream.getStatusCode() != 200)
        {
            finish (false, "Download failed (HTTP "
                               + juce::String (stream.getStatusCode()) + ")");
            return;
        }

        auto tempZip = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("pedalforge-" + juce::String (entry.id) + ".zip");
        {
            juce::FileOutputStream out (tempZip);
            if (! out.openedOk())
            {
                finish (false, juce::String ("No access to the temp file"));
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

        // ---- extract only the .vst3 bundles (with the subtree) into the user dir
        progress (92);
        juce::ZipFile zip (tempZip);
        const auto dest = userVst3Dir();
        dest.createDirectory();
        int extracted = 0;
        juce::StringArray bundles; // top-level names, for the manifest/uninstall

        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto* e = zip.getEntry (i);
            if (e == nullptr || e->filename.endsWithChar ('/'))
                continue;

            // relative path starting from the "*.vst3" segment
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
            if (entry.onlyBundle != nullptr && entry.onlyBundle[0] != 0
                && ! parts[bundleIdx].matchesWildcard (entry.onlyBundle, true))
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
            finish (false, juce::String ("The package contained no VST3 bundles"));
            return;
        }

        writeManifest (entry, bundles);
        finish (true, juce::String (juce::CharPointer_UTF8 (entry.name))
                          + " " + entry.version + " installed");
    });
}

} // namespace plugcat
