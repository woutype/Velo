#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <atomic>
#include <memory>
#include <vector>
#include "PluginScanner.h"
#include "Settings.h"

// Owns the VST3 plugin list (cached), the background scanner and the FX chain.
// All mutating calls happen on the message thread; process() runs on the audio
// thread and never blocks (try-lock: on contention the block passes through dry).
class PluginManager
{
public:
    using LoadDone = std::function<void (bool ok, const juce::String& message)>;

    explicit PluginManager (Settings&);
    ~PluginManager();

    //== plugin list / scanning =============================================
    void loadCache();
    void saveCache();
    juce::String handlePreviousCrash();      // returns name of the plugin that crashed last session (if any)
    void startScan (bool fullRescan,
                    std::function<void (const velo::ScanProgress&)> onProgress,
                    std::function<void (const velo::ScanResult&)> onDone);
    void cancelScan()          { scanner.cancel(); }
    bool isScanning() const    { return scanner.isScanning(); }
    juce::Array<juce::PluginDescription> getPluginTypes() const { return pluginList.getTypes(); }
    int  getNumPlugins() const { return pluginList.getNumTypes(); }
    juce::StringArray getScanFolders() const;
    void addScanFolder (const juce::File&);
    void removeScanFolder (const juce::String&);
    int  getUsage (const juce::String& pluginId) const;
    void noteUsed (const juce::PluginDescription&);

    //== chain ================================================================
    int  addSlot();                                  // returns slot uid
    void removeSlot (int uid);
    void clearSlot (int uid);
    void moveSlot (int uid, int newIndex);
    int  getNumSlots() const;
    int  getUidAt (int index) const;
    juce::String getSlotName (int uid) const;
    bool isSlotLoaded (int uid) const;
    bool isSlotBypassed (int uid) const;
    void setSlotBypassed (int uid, bool);
    void setGlobalBypass (bool b)      { globalBypass.store (b, std::memory_order_relaxed); }
    bool isGlobalBypassed() const      { return globalBypass.load (std::memory_order_relaxed); }

    void loadPluginAsync (int uid, const juce::PluginDescription&, double sampleRate, int blockSize,
                          LoadDone done, std::shared_ptr<juce::MemoryBlock> state = nullptr);
    void openEditor (int uid);
    void closeEditor (int uid);
    int  getTotalLatencySamples() const;

    std::unique_ptr<juce::XmlElement> saveChainState() const;
    void restoreChainAsync (const juce::XmlElement& chain, double sampleRate, int blockSize,
                            std::function<void()> onSlotChanged,
                            std::function<void (const juce::StringArray& warnings)> onDone);

    //== audio thread =========================================================
    void prepareToPlay (double sampleRate, int blockSize);
    void release();
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

private:
    struct Slot
    {
        int uid = 0;
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        std::unique_ptr<juce::DocumentWindow> editor;
        juce::PluginDescription desc;
        std::atomic<bool> bypassed { false };
    };

    Slot* find (int uid) const;
    void restoreNext (std::shared_ptr<std::vector<std::unique_ptr<juce::XmlElement>>> items, size_t index,
                      std::shared_ptr<juce::StringArray> warnings, double sr, int block,
                      std::function<void()> onChanged, std::function<void (const juce::StringArray&)> onDone);

    Settings& settings;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList pluginList;
    juce::CriticalSection chainLock;
    juce::CriticalSection cacheLock;
    std::vector<std::unique_ptr<Slot>> slots;
    std::atomic<bool> globalBypass { false };
    int nextUid = 1;
    std::shared_ptr<std::atomic<bool>> alive { std::make_shared<std::atomic<bool>> (true) };
    velo::PluginScanService scanner;
};
