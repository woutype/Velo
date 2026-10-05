#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>
#include "Settings.h"
#include "AudioEngine.h"
#include "PluginManager.h"

// Hosts the HTML/CSS/JS interface (ui/ folder, embedded as BinaryData) in a WebView2 control
// and bridges it to the C++ audio engine with two named events:
//   JS -> C++  "cmd"      { c: "<command>", ...args }
//   C++ -> JS  "settings" "devices" "chain" "plugins" "scan" "stats" "meters" "toast"
class MainComponent : public juce::Component,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { deviceUiDirty = true; }

    // bridge
    void handleCommand (const juce::var&);
    void emit (const char* id, const juce::var& payload);
    void toast (const char* kind, const juce::String& text);
    void sendAll();
    void sendSettings();
    void sendDevices();
    void sendChain();
    void sendPlugins();
    void sendScan();
    void sendStats();

    // actions
    void openControlPanel();
    void startScan (bool full);
    void startPluginLoad (int uid, const juce::PluginDescription&);
    void applyVirtual (const juce::String& type, juce::String device, int size);
    void saveSession();

    Settings settings;
    PluginManager pluginManager;
    AudioEngine audioEngine;
    std::unique_ptr<juce::WebBrowserComponent> web;

    struct ScanState
    {
        bool running = false, cancelled = false;
        int done = 0, total = 0, failed = 0;
        juce::String current;
    } scanState;

    std::vector<int> loadingUids;
    juce::String startupMessage;
    bool uiReady = false, deviceUiDirty = false, restoring = false, wasShowing = false;
    int tick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
