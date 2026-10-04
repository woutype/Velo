#include "PluginManager.h"
#include <windows.h>
#include <objbase.h>

class SafePluginWindow : public juce::DocumentWindow {
public:
    SafePluginWindow(juce::AudioPluginInstance& plug, std::function<void()> onClose)
        : DocumentWindow(plug.getName(), juce::Colour(0xff0f172a), DocumentWindow::closeButton),
          onCloseCallback(onClose) {
        setUsingNativeTitleBar(true);
        if (auto* editor = plug.createEditorIfNeeded()) {
            setContentOwned(editor, true);
            setResizable(editor->isResizable(), false);
        }
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override {
        if (onCloseCallback)
            onCloseCallback();
    }

private:
    std::function<void()> onCloseCallback;
};

PluginManager::PluginManager() : juce::Thread("PluginScanThread") {
    formatManager.addDefaultFormats();
    addSlot();
}

PluginManager::~PluginManager() {
    stopThread(5000);
    const juce::ScopedLock sl(pluginLock);
    for (auto& s : slots)
        s.editorWindow.reset();
    slots.clear();
}

int PluginManager::addSlot() {
    const juce::ScopedLock sl(pluginLock);
    slots.emplace_back();
    return (int)slots.size() - 1;
}

void PluginManager::removeSlot(int slotIdx) {
    const juce::ScopedLock sl(pluginLock);
    if (slotIdx >= 0 && slotIdx < (int)slots.size()) {
        slots[slotIdx].editorWindow.reset();
        slots.erase(slots.begin() + slotIdx);
    }
}

int PluginManager::getNumSlots() {
    const juce::ScopedLock sl(pluginLock);
    return (int)slots.size();
}

void PluginManager::setSlotBypassed(int slotIdx, bool isBypassed) {
    const juce::ScopedLock sl(pluginLock);
    if (slotIdx >= 0 && slotIdx < (int)slots.size())
        slots[slotIdx].bypassed = isBypassed;
}

void PluginManager::setGlobalBypass(bool isBypassed) {
    globalBypass.store(isBypassed, std::memory_order_relaxed);
}

void PluginManager::clearPluginInSlot(int slotIdx) {
    const juce::ScopedLock sl(pluginLock);
    if (slotIdx >= 0 && slotIdx < (int)slots.size()) {
        slots[slotIdx].editorWindow.reset();
        slots[slotIdx].plugin.reset();
        slots[slotIdx].bypassed = false;
    }
}

void PluginManager::closeSlotEditor(int slotIdx) {
    juce::MessageManager::callAsync([this, slotIdx]() {
        const juce::ScopedLock sl(pluginLock);
        if (slotIdx >= 0 && slotIdx < (int)slots.size())
            slots[slotIdx].editorWindow.reset();
    });
}

juce::File PluginManager::getCacheFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Velo")
        .getChildFile("PluginCache.xml");
}

void PluginManager::loadCachedPlugins() {
    auto file = getCacheFile();
    if (file.existsAsFile()) {
        std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
        if (xml != nullptr)
            pluginList.recreateFromXml(*xml);
    }
}

void PluginManager::startScanAsync(std::function<void(const juce::String&)> onProgress,
                                   std::function<void(int)> onComplete) {
    if (isThreadRunning()) return;
    progressCallback = onProgress;
    completeCallback = onComplete;
    startThread();
}

void PluginManager::run() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    juce::FileSearchPath paths;
    paths.add(juce::File("C:\\Program Files\\Common Files\\VST3"));
    paths.add(juce::File("C:\\Program Files (x86)\\Common Files\\VST3"));

    juce::File customDir("E:\\Installed Program");
    if (customDir.isDirectory()) {
        juce::Array<juce::File> results;
        customDir.findChildFiles(results, juce::File::findFilesAndDirectories, false, "*.vst3");
        for (auto& f : results)
            paths.add(f);

        auto commonVst = customDir.getChildFile("Common Files").getChildFile("VST3");
        if (commonVst.isDirectory())
            paths.add(commonVst);

        auto subVst = customDir.getChildFile("VST3");
        if (subVst.isDirectory())
            paths.add(subVst);
    }

    auto saveCache = [this]() {
        auto file = getCacheFile();
        file.getParentDirectory().createDirectory();
        std::unique_ptr<juce::XmlElement> xml(pluginList.createXml());
        if (xml != nullptr)
            xml->writeTo(file);
    };

    for (int i = 0; i < formatManager.getNumFormats(); ++i) {
        if (threadShouldExit()) break;
        if (auto* format = formatManager.getFormat(i)) {
            juce::PluginDirectoryScanner scanner(pluginList, *format, paths, true, juce::File(), false);
            juce::String name;
            int counter = 0;
            while (scanner.scanNextFile(true, name)) {
                if (threadShouldExit()) break;
                if (name.isNotEmpty()) {
                    juce::String shortName = juce::File(name).getFileName();
                    juce::MessageManager::callAsync([this, shortName]() {
                        if (progressCallback)
                            progressCallback(shortName);
                    });
                    if (++counter % 5 == 0)
                        saveCache();
                }
            }
        }
    }

    saveCache();

    int totalFound = pluginList.getTypes().size();
    juce::MessageManager::callAsync([this, totalFound]() {
        if (completeCallback)
            completeCallback(totalFound);
    });

    CoUninitialize();
}

bool PluginManager::loadPlugin(int slotIdx, const juce::PluginDescription& desc, double sampleRate, int blockSize) {
    juce::String err;
    auto instance = formatManager.createPluginInstance(desc, sampleRate, blockSize, err);
    if (instance == nullptr) return false;

    instance->enableAllBuses();
    instance->prepareToPlay(sampleRate, blockSize);

    const juce::ScopedLock sl(pluginLock);
    if (slotIdx >= 0 && slotIdx < (int)slots.size()) {
        slots[slotIdx].editorWindow.reset();
        slots[slotIdx].plugin = std::move(instance);
        slots[slotIdx].bypassed = false;
        return true;
    }
    return false;
}

void PluginManager::openEditor(int slotIdx) {
    const juce::ScopedLock sl(pluginLock);
    if (slotIdx < 0 || slotIdx >= (int)slots.size() || slots[slotIdx].plugin == nullptr)
        return;

    if (slots[slotIdx].editorWindow != nullptr) {
        slots[slotIdx].editorWindow->toFront(true);
        return;
    }

    if (slots[slotIdx].plugin->hasEditor()) {
        slots[slotIdx].editorWindow = std::make_unique<SafePluginWindow>(
            *slots[slotIdx].plugin,
            [this, slotIdx]() { closeSlotEditor(slotIdx); }
        );
    }
}

void PluginManager::prepareToPlay(double sampleRate, int blockSize) {
    const juce::ScopedLock sl(pluginLock);
    for (auto& s : slots) {
        if (s.plugin != nullptr)
            s.plugin->prepareToPlay(sampleRate, blockSize);
    }
}

void PluginManager::release() {
    const juce::ScopedLock sl(pluginLock);
    for (auto& s : slots) {
        if (s.plugin != nullptr)
            s.plugin->releaseResources();
    }
}

void PluginManager::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    if (globalBypass.load(std::memory_order_relaxed))
        return;

    const juce::ScopedLock sl(pluginLock);
    for (auto& s : slots) {
        if (s.plugin != nullptr && !s.bypassed && !s.plugin->isSuspended())
            s.plugin->processBlock(buffer, midi);
    }
}