#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "AudioEngine.h"
#include "PluginManager.h"
#include "LevelMeter.h"
#include "PluginSlotView.h"

class CardContainer : public juce::Component {
public:
    CardContainer(const juce::String& title = "") : headerTitle(title) {}

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff0d1527));
        g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour(juce::Colour(0xff1e293b));
        g.drawRoundedRectangle(bounds, 8.0f, 1.0f);

        if (headerTitle.isNotEmpty()) {
            g.setColour(juce::Colour(0xff64748b));
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(headerTitle.toUpperCase(), 14, 8, getWidth() - 28, 16, juce::Justification::centredLeft);
        }
    }

private:
    juce::String headerTitle;
};

class MainComponent : public juce::Component, private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshDeviceLists();
    void updateRatesAndBuffers();
    void updateHeadphonesLatency();

    void changeInputDevice();
    void changeMonitorDevice();
    void changeVirtualDevice();
    void changeSampleRate();
    void changeBufferSize();

    void choosePlugin(int slotIdx);
    void addNewSlot();
    void removeSlot(int slotIdx);
    void rebuildRackLayout();

    PluginManager pluginManager;
    AudioEngine audioEngine;

    float curInputLevel = 0.0f;
    float curMonitorLevel = 0.0f;
    float curVirtualLevel = 0.0f;

    CardContainer routingCard { "Audio Routing & Monitoring" };
    CardContainer engineCard { "ASIO Engine & Direct Latency" };
    CardContainer rackCard { "VST3 Processing Chain" };

    juce::TextButton streamButton;
    juce::TextButton monitorButton;
    juce::TextButton bypassAllBtn;
    juce::TextButton scanBtn;
    juce::TextButton addSlotBtn;

    juce::Label titleLabel;
    juce::Label subTitleLabel;

    juce::Label inputLabel;
    juce::Label monitorLabel;
    juce::Label virtualLabel;
    juce::Label sampleRateLabel;
    juce::Label bufferSizeLabel;
    juce::Label scanStatusLabel;
    juce::Label latencyValueLabel;
    juce::Label latencyDescLabel;

    juce::ComboBox inputCombo;
    juce::ComboBox monitorCombo;
    juce::ComboBox virtualCombo;
    juce::ComboBox sampleRateCombo;
    juce::ComboBox bufferSizeCombo;

    LevelMeter inputMeter;
    LevelMeter monitorMeter;
    LevelMeter virtualMeter;

    juce::Viewport rackViewport;
    juce::Component rackContainer;
    std::vector<std::unique_ptr<PluginSlotView>> slots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};