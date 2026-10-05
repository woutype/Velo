#include "MainComponent.h"
#include <BinaryData.h>
#include <algorithm>
#include <optional>

namespace
{
    juce::var obj (std::initializer_list<std::pair<const char*, juce::var>> items)
    {
        auto* o = new juce::DynamicObject();
        for (auto& p : items) o->setProperty (p.first, p.second);
        return juce::var (o);
    }

    juce::var strings (const juce::StringArray& a)
    {
        juce::Array<juce::var> v;
        for (auto& s : a) v.add (s);
        return v;
    }

    juce::var ints (const juce::Array<int>& a)
    {
        juce::Array<juce::var> v;
        for (auto i : a) v.add (i);
        return v;
    }

    juce::String S (const juce::var& v, const char* k) { return v.getProperty (k, {}).toString(); }
    int    I (const juce::var& v, const char* k, int d = 0)       { auto p = v.getProperty (k, {}); return p.isVoid() ? d : (int) p; }
    double D (const juce::var& v, const char* k, double d = 0.0)  { auto p = v.getProperty (k, {}); return p.isVoid() ? d : (double) p; }
    bool   B (const juce::var& v, const char* k)                  { return (bool) v.getProperty (k, false); }

    juce::String mimeFor (const juce::String& name)
    {
        if (name.endsWithIgnoreCase (".html")) return "text/html";
        if (name.endsWithIgnoreCase (".css"))  return "text/css";
        if (name.endsWithIgnoreCase (".js"))   return "text/javascript";
        if (name.endsWithIgnoreCase (".svg"))  return "image/svg+xml";
        if (name.endsWithIgnoreCase (".png"))  return "image/png";
        if (name.endsWithIgnoreCase (".woff2")) return "font/woff2";
        return "application/octet-stream";
    }

    // serves the embedded ui/ files to the WebView
    std::optional<juce::WebBrowserComponent::Resource> provideResource (const juce::String& url)
    {
        auto path = url.upToFirstOccurrenceOf ("?", false, false).fromFirstOccurrenceOf ("/", false, false);
        if (path.isEmpty()) path = "index.html";

        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const char* name = BinaryData::namedResourceList[i];
            const juce::String original (BinaryData::getNamedResourceOriginalFilename (name));
            if (juce::File::createFileWithoutCheckingPath (original).getFileName() != path) continue;

            int size = 0;
            const char* data = BinaryData::getNamedResource (name, size);
            if (data == nullptr) return std::nullopt;

            juce::WebBrowserComponent::Resource r;
            r.data.assign (reinterpret_cast<const std::byte*> (data), reinterpret_cast<const std::byte*> (data) + size);
            r.mimeType = mimeFor (original);
            return r;
        }
        return std::nullopt;
    }

    double gainFromDb (double db, double minDb) { return db <= minDb + 0.05 ? 0.0 : juce::Decibels::decibelsToGain (db); }
}

//==============================================================================
MainComponent::MainComponent() : pluginManager (settings), audioEngine (pluginManager)
{
    pluginManager.loadCache();
    const auto crashed = pluginManager.handlePreviousCrash();

    // --- web UI --------------------------------------------------------------------
    using Opts = juce::WebBrowserComponent::Options;
    auto webOptions = Opts()
        .withBackend (Opts::Backend::webview2)
        .withWinWebView2Options (Opts::WinWebView2()
                                     .withUserDataFolder (Settings::getDataDir().getChildFile ("webview"))
                                     .withStatusBarDisabled()
                                     .withBackgroundColour (juce::Colour (0xff070b16)))
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider (&provideResource)
        .withEventListener ("cmd", [this] (juce::var v) { handleCommand (v); });

    web = std::make_unique<juce::WebBrowserComponent> (webOptions);
    addAndMakeVisible (*web);
    web->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    setSize (560, 900);

    // --- audio ----------------------------------------------------------------------
    const auto warnings = audioEngine.start (settings.getString ("mainDevice"), settings.getInt ("inCh", 0),
                                             settings.getInt ("outPair", 0), settings.getInt ("mainBuffer", 0),
                                             settings.getString ("vType", "Windows Audio"), settings.getString ("vDevice"),
                                             settings.getInt ("vBuffer", 0));
    audioEngine.addChangeListener (this);

    audioEngine.setInputGain ((float) gainFromDb (settings.getDouble ("gainIn", 0.0), -60.0));
    audioEngine.setMonitorGain ((float) gainFromDb (settings.getDouble ("gainMon", 0.0), -60.0));
    audioEngine.setVirtualGain ((float) gainFromDb (settings.getDouble ("gainVirt", 0.0), -60.0));
    audioEngine.setStreamActive (settings.getBool ("mic", false));
    audioEngine.setMonitorActive (settings.getBool ("mon", false));
    pluginManager.setGlobalBypass (settings.getBool ("fxBypass", false));

    if (crashed.isNotEmpty())
        startupMessage << "\"" << crashed << "\" crashed Velo last time and has been disabled.\n";
    startupMessage << warnings;

    // --- restore effect chain (async, one plugin at a time) ------------------------
    auto savedChain = settings.getXml ("chain");
    if (savedChain != nullptr && savedChain->getNumChildElements() > 0)
    {
        restoring = true;
        auto safe = juce::Component::SafePointer<MainComponent> (this);
        juce::Timer::callAfterDelay (300, [safe, chainXml = std::shared_ptr<juce::XmlElement> (savedChain.release())]
        {
            if (safe == nullptr) return;
            const double sr = safe->audioEngine.getMainSampleRate() > 0 ? safe->audioEngine.getMainSampleRate() : 48000.0;
            const int block = safe->audioEngine.getMainBufferSize() > 0 ? safe->audioEngine.getMainBufferSize() : 512;
            safe->pluginManager.restoreChainAsync (*chainXml, sr, block,
                [safe] { if (safe != nullptr) safe->sendChain(); },
                [safe] (const juce::StringArray& w)
                {
                    if (safe == nullptr) return;
                    safe->restoring = false;
                    if (safe->pluginManager.getNumSlots() == 0) safe->pluginManager.addSlot();
                    safe->sendChain();
                    if (w.size() > 0) safe->toast ("warn", w.joinIntoString ("\n"));
                });
        });
    }
    else pluginManager.addSlot();

    if (pluginManager.getNumPlugins() == 0)
    {
        auto safe = juce::Component::SafePointer<MainComponent> (this);
        juce::Timer::callAfterDelay (800, [safe] { if (safe != nullptr && safe->pluginManager.getNumPlugins() == 0) safe->startScan (false); });
    }

    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    audioEngine.removeChangeListener (this);
    saveSession();
    web.reset();
    audioEngine.shutdown();
}

void MainComponent::resized()
{
    if (web != nullptr) web->setBounds (getLocalBounds());
}

//== bridge ====================================================================
void MainComponent::emit (const char* id, const juce::var& payload)
{
    if (web != nullptr && uiReady) web->emitEventIfBrowserIsVisible (id, payload);
}

void MainComponent::toast (const char* kind, const juce::String& text)
{
    if (text.trim().isEmpty()) return;
    juce::Logger::writeToLog (juce::String (kind) + ": " + text);
    emit ("toast", obj ({ { "kind", kind }, { "text", text.trim() } }));
}

void MainComponent::sendAll()
{
    sendSettings(); sendDevices(); sendChain(); sendPlugins(); sendScan(); sendStats();
}

void MainComponent::sendSettings()
{
    emit ("settings", obj ({
        { "mic", settings.getBool ("mic", false) }, { "mon", settings.getBool ("mon", false) },
        { "fx", ! settings.getBool ("fxBypass", false) },
        { "gainIn", settings.getDouble ("gainIn", 0.0) }, { "gainMon", settings.getDouble ("gainMon", 0.0) },
        { "gainVirt", settings.getDouble ("gainVirt", 0.0) } }));
}

void MainComponent::sendDevices()
{
    deviceUiDirty = false;
    const auto ins = audioEngine.getInputChannelNames();
    const auto outs = audioEngine.getOutputPairNames();

    auto iface = obj ({
        { "list", strings (audioEngine.getMainDevices()) }, { "current", audioEngine.getMainDeviceName() },
        { "type", audioEngine.getMainTypeName() },
        { "ins", strings (ins) }, { "inSel", juce::jlimit (0, juce::jmax (0, ins.size() - 1), settings.getInt ("inCh", 0)) },
        { "outs", strings (outs) }, { "outSel", juce::jlimit (0, juce::jmax (0, outs.size() - 1), settings.getInt ("outPair", 0)) },
        { "sizes", ints (audioEngine.getMainBufferSizes()) }, { "size", audioEngine.getMainBufferSize() },
        { "rate", audioEngine.getMainSampleRate() }, { "hasPanel", audioEngine.hasControlPanel() } });

    auto virt = obj ({
        { "types", strings (audioEngine.getVirtualTypes()) }, { "type", audioEngine.getVirtualTypeName() },
        { "list", strings (audioEngine.getVirtualDevices()) }, { "current", audioEngine.getVirtualDeviceName() },
        { "sizes", ints (audioEngine.getVirtualBufferSizes()) }, { "size", audioEngine.getVirtualBufferSize() },
        { "rate", audioEngine.getVirtualLatency().rate }, { "running", audioEngine.isVirtualRunning() } });

    emit ("devices", obj ({ { "iface", iface }, { "virt", virt } }));
}

void MainComponent::sendChain()
{
    juce::Array<juce::var> a;
    for (int i = 0; i < pluginManager.getNumSlots(); ++i)
    {
        const int uid = pluginManager.getUidAt (i);
        const bool loading = std::find (loadingUids.begin(), loadingUids.end(), uid) != loadingUids.end();
        a.add (obj ({ { "uid", uid }, { "name", pluginManager.getSlotName (uid) },
                      { "bypassed", pluginManager.isSlotBypassed (uid) }, { "loading", loading } }));
    }
    emit ("chain", a);
}

void MainComponent::sendPlugins()
{
    juce::Array<juce::var> a;
    for (auto& d : pluginManager.getPluginTypes())
        a.add (obj ({ { "id", d.createIdentifierString() }, { "n", d.name }, { "m", d.manufacturerName },
                      { "c", d.category }, { "f", d.pluginFormatName }, { "i", d.isInstrument },
                      { "u", pluginManager.getUsage (d.createIdentifierString()) } }));
    emit ("plugins", a);
}

void MainComponent::sendScan()
{
    emit ("scan", obj ({ { "running", scanState.running }, { "cancelled", scanState.cancelled },
                         { "done", scanState.done }, { "total", scanState.total }, { "failed", scanState.failed },
                         { "current", scanState.current }, { "count", pluginManager.getNumPlugins() },
                         { "folders", strings (pluginManager.getScanFolders()) } }));
}

void MainComponent::sendStats()
{
    const auto hp = audioEngine.getHeadphoneLatency();
    const auto v = audioEngine.getVirtualLatency();

    const bool ok = audioEngine.isMainRunning();

    emit ("stats", obj ({
        { "ok", ok }, { "type", audioEngine.getMainTypeName() },
        { "cpu", audioEngine.getCpuUsage() }, { "bad", audioEngine.getBadBlockCount() },
        { "hp", obj ({ { "valid", hp.valid }, { "est", hp.estimated }, { "in", hp.inMs }, { "out", hp.outMs }, { "fx", hp.fxMs },
                       { "total", hp.totalMs }, { "buf", hp.bufferSamples }, { "rate", hp.rate } }) },
        { "v", obj ({ { "valid", v.valid }, { "in", v.inMs }, { "fx", v.fxMs }, { "queue", v.queueMs }, { "dev", v.deviceMs },
                      { "total", v.totalMs }, { "buf", v.bufferSamples }, { "rate", v.rate },
                      { "under", v.underruns }, { "over", v.overruns } }) } }));
}

//== commands ==================================================================
void MainComponent::handleCommand (const juce::var& v)
{
    const auto c = S (v, "c");

    if (c == "ready")
    {
        uiReady = true;
        wasShowing = true;
        sendAll();
        if (startupMessage.trim().isNotEmpty()) { toast ("warn", startupMessage); startupMessage.clear(); }
    }
    else if (c == "switch")
    {
        const auto k = S (v, "k");
        const bool on = B (v, "v");
        if (k == "mic")      { audioEngine.setStreamActive (on);  settings.setBool ("mic", on); }
        else if (k == "mon") { audioEngine.setMonitorActive (on); settings.setBool ("mon", on); }
        else if (k == "fx")  { pluginManager.setGlobalBypass (! on); settings.setBool ("fxBypass", ! on); }
    }
    else if (c == "gain")
    {
        const auto k = S (v, "k");
        const double db = D (v, "db");
        if (k == "in")        { audioEngine.setInputGain ((float) gainFromDb (db, -60.0));   settings.setDouble ("gainIn", db); }
        else if (k == "mon")  { audioEngine.setMonitorGain ((float) gainFromDb (db, -60.0)); settings.setDouble ("gainMon", db); }
        else if (k == "virt") { audioEngine.setVirtualGain ((float) gainFromDb (db, -60.0)); settings.setDouble ("gainVirt", db); }
    }
    else if (c == "iface")
    {
        const auto name = S (v, "name");
        if (name.isEmpty()) return;
        const auto err = audioEngine.openMainDevice (name, 0, 0, 0);
        settings.setString ("mainDevice", name);
        settings.setInt ("inCh", 0); settings.setInt ("outPair", 0); settings.setInt ("mainBuffer", 0);
        if (err.isNotEmpty()) toast ("error", name + ": " + err + "\nIs another program using this device?");
        sendDevices(); sendStats();
    }
    else if (c == "channels")
    {
        const int inCh = I (v, "inCh"), outPair = I (v, "outPair");
        settings.setInt ("inCh", inCh); settings.setInt ("outPair", outPair);
        const auto err = audioEngine.openMainDevice (audioEngine.getMainDeviceName(), inCh, outPair, settings.getInt ("mainBuffer", 0));
        if (err.isNotEmpty()) toast ("error", err);
        sendDevices(); sendStats();
    }
    else if (c == "hpBuffer")
    {
        const int size = I (v, "size");
        if (size <= 0) return;
        settings.setInt ("mainBuffer", size);
        const auto err = audioEngine.openMainDevice (audioEngine.getMainDeviceName(), settings.getInt ("inCh", 0),
                                                     settings.getInt ("outPair", 0), size);
        if (err.isNotEmpty()) toast ("error", err);
        sendDevices(); sendStats();
    }
    else if (c == "panel")          openControlPanel();
    else if (c == "defaults")
    {
        toast ("error", audioEngine.reopenMainWithDriverDefaults());
        sendDevices();
    }
    else if (c == "rescanDevices")
    {
        audioEngine.getMainDevices (true);
        audioEngine.getVirtualDevices (true);
        sendDevices();
    }
    else if (c == "virtual")        applyVirtual (S (v, "type"), S (v, "device"), I (v, "size"));
    else if (c == "scan")           startScan (B (v, "full"));
    else if (c == "scanStop")       { pluginManager.cancelScan(); }
    else if (c == "addFolder")
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Choose a folder with VST3 plugins");
        auto safe = juce::Component::SafePointer<MainComponent> (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe, chooser] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr || ! fc.getResult().isDirectory()) return;
                                  safe->pluginManager.addScanFolder (fc.getResult());
                                  safe->startScan (false);
                              });
    }
    else if (c == "removeFolder")   { pluginManager.removeScanFolder (S (v, "path")); sendScan(); }
    else if (c == "addSlot")        { pluginManager.addSlot(); sendChain(); }
    else if (c == "removeSlot")
    {
        const int uid = I (v, "uid", -1);
        if (pluginManager.getNumSlots() <= 1) pluginManager.clearSlot (uid);
        else pluginManager.removeSlot (uid);
        sendChain(); saveSession();
    }
    else if (c == "moveSlot")       { pluginManager.moveSlot (I (v, "uid", -1), I (v, "index")); sendChain(); saveSession(); }
    else if (c == "bypass")         { pluginManager.setSlotBypassed (I (v, "uid", -1), B (v, "v")); sendChain(); saveSession(); }
    else if (c == "editor")         pluginManager.openEditor (I (v, "uid", -1));
    else if (c == "load")
    {
        const auto id = S (v, "id");
        for (auto& d : pluginManager.getPluginTypes())
            if (d.createIdentifierString() == id) { startPluginLoad (I (v, "uid", -1), d); return; }
        toast ("error", "That plugin is no longer in the list. Rescan and try again.");
    }
}

void MainComponent::applyVirtual (const juce::String& type, juce::String device, int size)
{
    if (type.isEmpty()) return;

    if (device.isEmpty())
    {
        // new driver type: open it, then pick a sensible default device
        audioEngine.openVirtualDevice (type, {}, 0);
        auto devs = audioEngine.getVirtualDevices (true);
        for (auto& d : devs)
            if (d.containsIgnoreCase ("cable") || d.containsIgnoreCase ("virtual")) { device = d; break; }
        if (device.isEmpty() && ! devs.isEmpty()) device = devs[0];
        size = 0;
    }

    if (device.isNotEmpty())
    {
        const auto err = audioEngine.openVirtualDevice (type, device, juce::jmax (0, size));
        if (err.isNotEmpty()) toast ("error", err);
        settings.setString ("vType", type);
        settings.setString ("vDevice", device);
        settings.setInt ("vBuffer", juce::jmax (0, size));
    }
    sendDevices(); sendStats();
}

void MainComponent::openControlPanel()
{
    if (! audioEngine.showControlPanel())
    {
        toast ("warn", "This driver has no control panel. Open the interface's own utility (for example MiniFuse Control Center) instead.");
        return;
    }
    // the driver may change buffer size / sample rate: adopt its settings afterwards
    settings.setInt ("mainBuffer", 0);
    auto safe = juce::Component::SafePointer<MainComponent> (this);
    juce::Timer::callAfterDelay (1200, [safe]
    {
        if (safe == nullptr) return;
        safe->audioEngine.reopenMainWithDriverDefaults();
        safe->deviceUiDirty = true;
    });
}

void MainComponent::startScan (bool full)
{
    if (scanState.running) return;
    scanState = {};
    scanState.running = true;
    sendScan();

    auto safe = juce::Component::SafePointer<MainComponent> (this);
    pluginManager.startScan (full,
        [safe] (const velo::ScanProgress& p)
        {
            if (safe == nullptr) return;
            safe->scanState.done = p.done; safe->scanState.total = p.total;
            safe->scanState.failed = p.failed; safe->scanState.current = p.current;
            safe->sendScan();
        },
        [safe] (const velo::ScanResult& r)
        {
            if (safe == nullptr) return;
            safe->scanState.running = false;
            safe->scanState.cancelled = r.cancelled;
            safe->scanState.failed = r.failed;
            if (r.failed > 0 && ! r.cancelled)
                juce::Logger::writeToLog ("Skipped plugins: " + r.failedNames.joinIntoString (", "));
            safe->sendScan();
            safe->sendPlugins();
        });
}

void MainComponent::startPluginLoad (int uid, const juce::PluginDescription& desc)
{
    loadingUids.push_back (uid);
    sendChain();

    auto safe = juce::Component::SafePointer<MainComponent> (this);
    // small delay so the "Loading" state reaches the screen before the (blocking) plugin instantiation
    juce::Timer::callAfterDelay (60, [safe, uid, desc]
    {
        if (safe == nullptr) return;
        const double sr = safe->audioEngine.getMainSampleRate() > 0 ? safe->audioEngine.getMainSampleRate() : 48000.0;
        const int block = safe->audioEngine.getMainBufferSize() > 0 ? safe->audioEngine.getMainBufferSize() : 512;

        safe->pluginManager.loadPluginAsync (uid, desc, sr, block,
            [safe, uid, desc] (bool ok, const juce::String& message)
            {
                if (safe == nullptr) return;
                safe->loadingUids.erase (std::remove (safe->loadingUids.begin(), safe->loadingUids.end(), uid), safe->loadingUids.end());
                safe->sendChain();
                if (ok) { safe->pluginManager.noteUsed (desc); safe->saveSession(); safe->sendPlugins(); }
                else safe->toast ("error", "Could not load \"" + desc.name + "\":\n" + message);
            });
    });
}

void MainComponent::saveSession()
{
    if (restoring) return;          // never overwrite the saved chain with a half-restored one
    if (auto chain = pluginManager.saveChainState())
        settings.setXml ("chain", chain.get());
    settings.save();
}

//==============================================================================
void MainComponent::timerCallback()
{
    ++tick;

    // events are dropped while the window is minimised: resend everything when it comes back
    const bool showing = web != nullptr && web->isShowing();
    if (showing && ! wasShowing && uiReady) sendAll();
    wasShowing = showing;
    if (! showing) return;

    emit ("meters", obj ({ { "i", audioEngine.getInputPeak() }, { "m", audioEngine.getMonitorPeak() },
                           { "v", audioEngine.getVirtualPeak() } }));

    if (deviceUiDirty) { sendDevices(); sendStats(); }
    if (tick % 8 == 0) sendStats();
    if (tick % 1800 == 0) saveSession();          // autosave every minute
}
