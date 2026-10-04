#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <vector>
#include <functional>
#include <atomic>

class PluginManager : private juce::Thread {
public:
    PluginManager();
    ~PluginManager() override;

    void loadCachedPlugins();
    void startScanAsync(std::function<void(const juce::String&)> onProgress,
                        std::function<void(int)> onComplete);

    bool isScanning() const { return isThreadRunning(); }
    const juce::KnownPluginList& getPluginList() const { return pluginList; }

    int addSlot();
    void removeSlot(int slotIdx);
    int getNumSlots();

    void setSlotBypassed(int slotIdx, bool isBypassed);
    void setGlobalBypass(bool isBypassed);
    bool isGlobalBypassed() const { return globalBypass.load(std::memory_order_relaxed); }

    bool loadPlugin(int slotIdx, const juce::PluginDescription& desc, double sampleRate, int blockSize);
    void clearPluginInSlot(int slotIdx);
    void openEditor(int slotIdx);

    void prepareToPlay(double sampleRate, int blockSize);
    void release();
    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    juce::CriticalSection& getLock() { return pluginLock; }

private:
    struct Slot {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        std::unique_ptr<juce::DocumentWindow> editorWindow;
        bool bypassed = false;
    };

    void run() override;
    juce::File getCacheFile();
    void closeSlotEditor(int slotIdx);

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList pluginList;
    juce::CriticalSection pluginLock;

    std::vector<Slot> slots;
    std::atomic<bool> globalBypass { false };

    std::function<void(const juce::String&)> progressCallback;
    std::function<void(int)> completeCallback;
};