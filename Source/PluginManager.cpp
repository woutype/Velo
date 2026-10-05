#include "PluginManager.h"

namespace
{
    class PluginEditorWindow : public juce::DocumentWindow
    {
    public:
        PluginEditorWindow (juce::AudioPluginInstance& plugin, std::function<void()> onClose)
            : DocumentWindow (plugin.getName(), juce::Colour (0xff0f172a), DocumentWindow::closeButton),
              closeCb (std::move (onClose))
        {
            setUsingNativeTitleBar (true);
            juce::AudioProcessorEditor* editor = plugin.hasEditor() ? plugin.createEditorIfNeeded() : nullptr;
            if (editor == nullptr) editor = new juce::GenericAudioProcessorEditor (plugin);
            setContentOwned (editor, true);
            setResizable (editor->isResizable(), false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { if (closeCb) closeCb(); }

    private:
        std::function<void()> closeCb;
    };

    void configureLayout (juce::AudioPluginInstance& p)
    {
        if (p.getBusCount (true) == 0 && p.getBusCount (false) == 0) return;
        const auto layout = p.getBusesLayout();

        auto trySet = [&] (const juce::AudioChannelSet& set)
        {
            auto l = layout;
            if (l.inputBuses.size() > 0)  l.inputBuses.getReference (0) = set;
            if (l.outputBuses.size() > 0) l.outputBuses.getReference (0) = set;
            if (p.checkBusesLayoutSupported (l)) { p.setBusesLayout (l); return true; }
            return false;
        };

        if (! trySet (juce::AudioChannelSet::stereo()))
            trySet (juce::AudioChannelSet::mono());
    }
}

//==============================================================================
PluginManager::PluginManager (Settings& s)
    : settings (s),
      scanner (pluginList, formatManager, [this] { saveCache(); })
{
    formatManager.addDefaultFormats();

    // first run: keep the legacy custom folder if it exists on this machine
    if (! settings.hasKey ("scanFolders"))
    {
        juce::StringArray f;
        juce::File legacy ("E:\\Installed Program");
        if (legacy.isDirectory()) f.add (legacy.getFullPathName());
        settings.setList ("scanFolders", f);
    }
}

PluginManager::~PluginManager()
{
    *alive = false;
    scanner.cancel();
    std::vector<std::unique_ptr<Slot>> old;
    { const juce::ScopedLock sl (chainLock); old.swap (slots); }
    for (auto& s : old) { s->editor.reset(); s->plugin.reset(); }
}

//== cache / scanning ==========================================================
static juce::File cacheFile() { return Settings::getDataDir().getChildFile ("PluginCache.xml"); }

void PluginManager::loadCache()
{
    auto f = cacheFile();
    if (! f.existsAsFile()) return;
    if (auto xml = juce::XmlDocument::parse (f))
        pluginList.recreateFromXml (*xml);
}

void PluginManager::saveCache()
{
    const juce::ScopedLock sl (cacheLock);
    if (auto xml = pluginList.createXml())
    {
        juce::TemporaryFile tmp (cacheFile());
        if (xml->writeTo (tmp.getFile()))
            tmp.overwriteTargetFileWithTemporary();
    }
}

juce::String PluginManager::handlePreviousCrash()
{
    const auto marker = settings.getString ("loadingPlugin");
    if (marker.isEmpty()) return {};

    settings.remove ("loadingPlugin");
    settings.save();

    const auto name = marker.upToFirstOccurrenceOf ("|", false, false);
    const auto file = marker.fromFirstOccurrenceOf ("|", false, false);
    if (file.isNotEmpty())
    {
        for (auto& d : pluginList.getTypes())
            if (d.fileOrIdentifier == file) pluginList.removeType (d);
        pluginList.addToBlacklist (file);
        saveCache();
    }
    return name;
}

void PluginManager::startScan (bool full, std::function<void (const velo::ScanProgress&)> onProgress,
                               std::function<void (const velo::ScanResult&)> onDone)
{
    scanner.start (full, getScanFolders(), std::move (onProgress), std::move (onDone));
}

juce::StringArray PluginManager::getScanFolders() const { return settings.getList ("scanFolders"); }

void PluginManager::addScanFolder (const juce::File& f)
{
    auto a = getScanFolders();
    a.addIfNotAlreadyThere (f.getFullPathName());
    settings.setList ("scanFolders", a);
    settings.save();
}

void PluginManager::removeScanFolder (const juce::String& path)
{
    auto a = getScanFolders();
    a.removeString (path);
    settings.setList ("scanFolders", a);
    settings.save();
}

int PluginManager::getUsage (const juce::String& id) const
{
    for (auto& line : settings.getList ("pluginUsage"))
        if (line.upToLastOccurrenceOf ("=", false, false) == id)
            return line.fromLastOccurrenceOf ("=", false, false).getIntValue();
    return 0;
}

void PluginManager::noteUsed (const juce::PluginDescription& d)
{
    const auto id = d.createIdentifierString();
    auto lines = settings.getList ("pluginUsage");
    int count = 1;
    for (int i = 0; i < lines.size(); ++i)
        if (lines[i].upToLastOccurrenceOf ("=", false, false) == id)
        {
            count = lines[i].fromLastOccurrenceOf ("=", false, false).getIntValue() + 1;
            lines.remove (i);
            break;
        }
    lines.add (id + "=" + juce::String (count));
    while (lines.size() > 300) lines.remove (0);
    settings.setList ("pluginUsage", lines);
    settings.save();
}

//== chain =====================================================================
PluginManager::Slot* PluginManager::find (int uid) const
{
    for (auto& s : slots) if (s->uid == uid) return s.get();
    return nullptr;
}

int PluginManager::addSlot()
{
    auto s = std::make_unique<Slot>();
    s->uid = nextUid++;
    const int uid = s->uid;
    const juce::ScopedLock sl (chainLock);
    slots.push_back (std::move (s));
    return uid;
}

void PluginManager::removeSlot (int uid)
{
    std::unique_ptr<Slot> victim;
    {
        const juce::ScopedLock sl (chainLock);
        for (size_t i = 0; i < slots.size(); ++i)
            if (slots[i]->uid == uid) { victim = std::move (slots[i]); slots.erase (slots.begin() + (long) i); break; }
    }
    if (victim) { victim->editor.reset(); victim->plugin.reset(); }
}

void PluginManager::moveSlot (int uid, int newIndex)
{
    const juce::ScopedLock sl (chainLock);
    for (size_t i = 0; i < slots.size(); ++i)
        if (slots[i]->uid == uid)
        {
            auto s = std::move (slots[i]);
            slots.erase (slots.begin() + (long) i);
            newIndex = juce::jlimit (0, (int) slots.size(), newIndex);
            slots.insert (slots.begin() + newIndex, std::move (s));
            return;
        }
}

void PluginManager::clearSlot (int uid)
{
    std::unique_ptr<juce::AudioPluginInstance> old;
    std::unique_ptr<juce::DocumentWindow> ed;
    {
        const juce::ScopedLock sl (chainLock);
        if (auto* s = find (uid)) { old = std::move (s->plugin); ed = std::move (s->editor); s->bypassed = false; s->desc = {}; }
    }
    ed.reset();
    old.reset();
}

int PluginManager::getNumSlots() const { return (int) slots.size(); }
int PluginManager::getUidAt (int i) const { return juce::isPositiveAndBelow (i, (int) slots.size()) ? slots[(size_t) i]->uid : -1; }
juce::String PluginManager::getSlotName (int uid) const { auto* s = find (uid); return (s && s->plugin) ? s->desc.name : juce::String(); }
bool PluginManager::isSlotLoaded (int uid) const { auto* s = find (uid); return s && s->plugin != nullptr; }
bool PluginManager::isSlotBypassed (int uid) const { auto* s = find (uid); return s && s->bypassed.load(); }
void PluginManager::setSlotBypassed (int uid, bool b) { if (auto* s = find (uid)) s->bypassed = b; }

void PluginManager::loadPluginAsync (int uid, const juce::PluginDescription& desc, double sr, int block,
                                     LoadDone done, std::shared_ptr<juce::MemoryBlock> state)
{
    // crash guard: if the plugin takes the whole process down, we know who did it on next start
    settings.setString ("loadingPlugin", desc.name + "|" + desc.fileOrIdentifier);
    settings.save();

    auto flag = alive;
    formatManager.createPluginInstanceAsync (desc, sr, block,
        [this, flag, uid, desc, sr, block, done, state] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err)
        {
            if (! flag->load()) return;
            settings.remove ("loadingPlugin");
            settings.save();

            if (inst == nullptr)
            {
                if (done) done (false, err.isEmpty() ? "Plugin failed to load" : err);
                return;
            }

            try
            {
                configureLayout (*inst);
                inst->setRateAndBufferSizeDetails (sr, block);
                if (state != nullptr && state->getSize() > 0)
                    inst->setStateInformation (state->getData(), (int) state->getSize());
                inst->prepareToPlay (sr, block);
            }
            catch (const std::exception& e)
            {
                if (done) done (false, juce::String ("Plugin error: ") + e.what());
                return;
            }

            std::unique_ptr<juce::AudioPluginInstance> oldPlugin;
            std::unique_ptr<juce::DocumentWindow> oldEditor;
            bool found = false;
            {
                const juce::ScopedLock sl (chainLock);
                if (auto* s = find (uid))
                {
                    found = true;
                    oldPlugin = std::move (s->plugin);
                    oldEditor = std::move (s->editor);
                    s->plugin = std::move (inst);
                    s->desc = desc;
                    s->bypassed = false;
                }
            }
            oldEditor.reset();   // editor must die before its plugin
            oldPlugin.reset();

            if (done) done (found, found ? juce::String() : juce::String ("Slot was removed"));
        });
}

void PluginManager::openEditor (int uid)
{
    auto* s = find (uid);
    if (s == nullptr || s->plugin == nullptr) return;
    if (s->editor != nullptr) { s->editor->toFront (true); return; }

    auto flag = alive;
    s->editor = std::make_unique<PluginEditorWindow> (*s->plugin, [this, flag, uid]
    {
        juce::MessageManager::callAsync ([this, flag, uid] { if (flag->load()) closeEditor (uid); });
    });
}

void PluginManager::closeEditor (int uid)
{
    if (auto* s = find (uid)) s->editor.reset();
}

int PluginManager::getTotalLatencySamples() const
{
    int total = 0;
    for (auto& s : slots)
        if (s->plugin != nullptr && ! s->bypassed.load()) total += juce::jmax (0, s->plugin->getLatencySamples());
    return total;
}

//== persistence ===============================================================
std::unique_ptr<juce::XmlElement> PluginManager::saveChainState() const
{
    auto root = std::make_unique<juce::XmlElement> ("CHAIN");
    for (auto& s : slots)
    {
        if (s->plugin == nullptr) continue;
        auto* e = root->createNewChildElement ("SLOT");
        e->setAttribute ("bypassed", s->bypassed.load());
        e->addChildElement (s->desc.createXml().release());
        try
        {
            juce::MemoryBlock mb;
            s->plugin->getStateInformation (mb);
            e->setAttribute ("state", mb.toBase64Encoding());
        }
        catch (...) {}
    }
    return root;
}

void PluginManager::restoreChainAsync (const juce::XmlElement& chain, double sr, int block,
                                       std::function<void()> onChanged,
                                       std::function<void (const juce::StringArray&)> onDone)
{
    auto items = std::make_shared<std::vector<std::unique_ptr<juce::XmlElement>>>();
    for (auto* e : chain.getChildIterator())
        items->push_back (std::make_unique<juce::XmlElement> (*e));

    restoreNext (items, 0, std::make_shared<juce::StringArray>(), sr, block, std::move (onChanged), std::move (onDone));
}

void PluginManager::restoreNext (std::shared_ptr<std::vector<std::unique_ptr<juce::XmlElement>>> items, size_t index,
                                 std::shared_ptr<juce::StringArray> warnings, double sr, int block,
                                 std::function<void()> onChanged, std::function<void (const juce::StringArray&)> onDone)
{
    if (index >= items->size())
    {
        if (onDone) onDone (*warnings);
        return;
    }

    auto* e = (*items)[index].get();
    juce::PluginDescription desc;
    auto* descXml = e->getFirstChildElement();

    auto proceed = [this, items, index, warnings, sr, block, onChanged, onDone, flag = alive]
    {
        if (flag->load()) restoreNext (items, index + 1, warnings, sr, block, onChanged, onDone);
    };

    if (descXml == nullptr || ! desc.loadFromXml (*descXml)) { proceed(); return; }

    if (pluginList.getBlacklistedFiles().contains (desc.fileOrIdentifier))
    {
        warnings->add (desc.name + " was skipped (it crashed earlier)");
        proceed();
        return;
    }

    const int uid = addSlot();
    if (onChanged) onChanged();

    auto state = std::make_shared<juce::MemoryBlock>();
    state->fromBase64Encoding (e->getStringAttribute ("state"));
    const bool bypassed = e->getBoolAttribute ("bypassed");
    const auto name = desc.name;

    loadPluginAsync (uid, desc, sr, block,
        [this, uid, bypassed, name, warnings, onChanged, proceed] (bool ok, const juce::String& msg)
        {
            if (ok) setSlotBypassed (uid, bypassed);
            else { warnings->add (name + ": " + msg); removeSlot (uid); }
            if (onChanged) onChanged();
            proceed();
        },
        state);
}

//== audio thread ==============================================================
void PluginManager::prepareToPlay (double sr, int block)
{
    const juce::ScopedLock sl (chainLock);
    for (auto& s : slots)
        if (s->plugin != nullptr)
        {
            s->plugin->setRateAndBufferSizeDetails (sr, block);
            s->plugin->prepareToPlay (sr, block);
        }
}

void PluginManager::release()
{
    const juce::ScopedLock sl (chainLock);
    for (auto& s : slots)
        if (s->plugin != nullptr) s->plugin->releaseResources();
}

void PluginManager::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    if (globalBypass.load (std::memory_order_relaxed)) return;

    const juce::ScopedTryLock sl (chainLock);
    if (! sl.isLocked()) return;          // chain is being edited: pass audio through for this block

    const int n = buffer.getNumSamples();
    float* ptrs[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };

    for (auto& s : slots)
    {
        auto* p = s->plugin.get();
        if (p == nullptr || s->bypassed.load (std::memory_order_relaxed) || p->isSuspended()) continue;

        const int pluginChannels = juce::jlimit (1, 2, juce::jmax (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels()));
        juce::AudioBuffer<float> view (ptrs, pluginChannels, n);
        midi.clear();
        p->processBlock (view, midi);
        if (pluginChannels == 1)
            juce::FloatVectorOperations::copy (ptrs[1], ptrs[0], n);
    }
}
