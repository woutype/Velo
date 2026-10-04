#include "MainComponent.h"
#include "PluginSearchWindow.h"

MainComponent::MainComponent() : audioEngine(pluginManager) {
    audioEngine.initDevices();

    titleLabel.setText("VELO", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff8fafc));
    addAndMakeVisible(titleLabel);

    subTitleLabel.setText("ZERO-LATENCY VST3 MONITOR", juce::dontSendNotification);
    subTitleLabel.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    subTitleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff38bdf8));
    addAndMakeVisible(subTitleLabel);

    streamButton.setButtonText("MIC STREAM: OFF");
    streamButton.setClickingTogglesState(true);
    streamButton.setToggleState(false, juce::dontSendNotification);
    streamButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffef4444));
    streamButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffef4444));
    streamButton.onClick = [this] {
        bool active = streamButton.getToggleState();
        audioEngine.setStreamActive(active);
        auto col = active ? juce::Colour(0xff10b981) : juce::Colour(0xffef4444);
        streamButton.setColour(juce::TextButton::buttonColourId, col);
        streamButton.setColour(juce::TextButton::buttonOnColourId, col);
        streamButton.setButtonText(active ? "MIC STREAM: LIVE" : "MIC STREAM: MUTED");
    };
    addAndMakeVisible(streamButton);

    monitorButton.setButtonText("HEADPHONES: OFF");
    monitorButton.setClickingTogglesState(true);
    monitorButton.setToggleState(false, juce::dontSendNotification);
    monitorButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff475569));
    monitorButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff475569));
    monitorButton.onClick = [this] {
        bool active = monitorButton.getToggleState();
        audioEngine.setMonitorActive(active);
        auto col = active ? juce::Colour(0xff0284c7) : juce::Colour(0xff475569);
        monitorButton.setColour(juce::TextButton::buttonColourId, col);
        monitorButton.setColour(juce::TextButton::buttonOnColourId, col);
        monitorButton.setButtonText(active ? "HEADPHONES: ON" : "HEADPHONES: OFF");
    };
    addAndMakeVisible(monitorButton);

    addAndMakeVisible(routingCard);
    addAndMakeVisible(engineCard);
    addAndMakeVisible(rackCard);

    auto setupLbl = [this](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::FontOptions(12.0f));
        l.setColour(juce::Label::textColourId, juce::Colour(0xff94a3b8));
        addAndMakeVisible(l);
    };

    setupLbl(inputLabel, "Microphone Input (Arturia ASIO)");
    setupLbl(monitorLabel, "Direct Monitor (Headphones Out)");
    setupLbl(virtualLabel, "Broadcast Stream (Virtual Audio Cable)");
    setupLbl(sampleRateLabel, "Sample Rate");
    setupLbl(bufferSizeLabel, "ASIO Buffer Size");

    latencyValueLabel.setText("0.0 ms", juce::dontSendNotification);
    latencyValueLabel.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    latencyValueLabel.setJustificationType(juce::Justification::centred);
    latencyValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff38bdf8));
    addAndMakeVisible(latencyValueLabel);

    latencyDescLabel.setText("Headphones Direct ASIO Latency", juce::dontSendNotification);
    latencyDescLabel.setFont(juce::FontOptions(10.5f));
    latencyDescLabel.setJustificationType(juce::Justification::centred);
    latencyDescLabel.setColour(juce::Label::textColourId, juce::Colour(0xff64748b));
    addAndMakeVisible(latencyDescLabel);

    bypassAllBtn.setButtonText("ALL FX: ON");
    bypassAllBtn.setClickingTogglesState(true);
    bypassAllBtn.setToggleState(false, juce::dontSendNotification);
    bypassAllBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff059669));
    bypassAllBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff059669));
    bypassAllBtn.onClick = [this] {
        bool isBypassed = bypassAllBtn.getToggleState();
        pluginManager.setGlobalBypass(isBypassed);
        auto col = isBypassed ? juce::Colour(0xffd97706) : juce::Colour(0xff059669);
        bypassAllBtn.setColour(juce::TextButton::buttonColourId, col);
        bypassAllBtn.setColour(juce::TextButton::buttonOnColourId, col);
        bypassAllBtn.setButtonText(isBypassed ? "ALL FX: BYPASS" : "ALL FX: ON");
    };
    addAndMakeVisible(bypassAllBtn);

    scanStatusLabel.setText("Plugin engine ready", juce::dontSendNotification);
    scanStatusLabel.setFont(juce::FontOptions(11.0f));
    scanStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff38bdf8));
    addAndMakeVisible(scanStatusLabel);

    scanBtn.setButtonText("Scan VST3");
    scanBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1e293b));
    scanBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff38bdf8));
    scanBtn.onClick = [this] {
        scanBtn.setEnabled(false);
        scanBtn.setButtonText("Scanning...");
        scanStatusLabel.setText("Scanning directories...", juce::dontSendNotification);

        pluginManager.startScanAsync(
            [this](const juce::String& fileName) {
                scanStatusLabel.setText("Found: " + fileName, juce::dontSendNotification);
            },
            [this](int count) {
                scanBtn.setEnabled(true);
                scanBtn.setButtonText("Scan VST3");
                scanStatusLabel.setText("Active plugins: " + juce::String(count), juce::dontSendNotification);
            }
        );
    };
    addAndMakeVisible(scanBtn);

    addAndMakeVisible(inputCombo);
    addAndMakeVisible(monitorCombo);
    addAndMakeVisible(virtualCombo);
    addAndMakeVisible(sampleRateCombo);
    addAndMakeVisible(bufferSizeCombo);

    addAndMakeVisible(inputMeter);
    addAndMakeVisible(monitorMeter);
    addAndMakeVisible(virtualMeter);

    addSlotBtn.setButtonText("+ Add FX Slot");
    addSlotBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0e1726));
    addSlotBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff38bdf8));
    addSlotBtn.onClick = [this] { addNewSlot(); };
    rackContainer.addAndMakeVisible(addSlotBtn);

    rackViewport.setViewedComponent(&rackContainer, false);
    rackViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(rackViewport);

    auto firstSlot = std::make_unique<PluginSlotView>(
        0,
        [this](int idx) { choosePlugin(idx); },
        [this](int idx) { pluginManager.openEditor(idx); },
        [this](int idx, bool bp) { pluginManager.setSlotBypassed(idx, bp); },
        [this](int idx) { removeSlot(idx); }
    );
    rackContainer.addAndMakeVisible(firstSlot.get());
    slots.push_back(std::move(firstSlot));

    inputCombo.onChange = [this] { changeInputDevice(); };
    monitorCombo.onChange = [this] { changeMonitorDevice(); };
    virtualCombo.onChange = [this] { changeVirtualDevice(); };
    sampleRateCombo.onChange = [this] { changeSampleRate(); };
    bufferSizeCombo.onChange = [this] { changeBufferSize(); };

    pluginManager.loadCachedPlugins();
    int cachedCount = pluginManager.getPluginList().getTypes().size();
    if (cachedCount > 0)
        scanStatusLabel.setText("Active plugins: " + juce::String(cachedCount), juce::dontSendNotification);

    refreshDeviceLists();

    setSize(500, 830);
    startTimerHz(30);
}

MainComponent::~MainComponent() {
    stopTimer();
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff060a12));
}

void MainComponent::addNewSlot() {
    int idx = pluginManager.addSlot();
    auto newSlot = std::make_unique<PluginSlotView>(
        idx,
        [this](int i) { choosePlugin(i); },
        [this](int i) { pluginManager.openEditor(i); },
        [this](int i, bool bp) { pluginManager.setSlotBypassed(i, bp); },
        [this](int i) { removeSlot(i); }
    );
    rackContainer.addAndMakeVisible(newSlot.get());
    slots.push_back(std::move(newSlot));
    rebuildRackLayout();
}

void MainComponent::removeSlot(int slotIdx) {
    if (slots.size() <= 1) {
        pluginManager.clearPluginInSlot(0);
        slots[0]->setPluginName("");
        return;
    }

    pluginManager.removeSlot(slotIdx);
    slots.erase(slots.begin() + slotIdx);

    for (int i = 0; i < (int)slots.size(); ++i)
        slots[i]->updateSlotIndex(i);

    rebuildRackLayout();
}

void MainComponent::rebuildRackLayout() {
    int w = rackViewport.getWidth() > 0 ? rackViewport.getWidth() - 10 : 440;
    int h = (int)slots.size() * 38 + 44;
    rackContainer.setSize(w, h);

    int y = 0;
    for (auto& s : slots) {
        s->setBounds(0, y, w, 34);
        y += 38;
    }
    addSlotBtn.setBounds(0, y, w, 32);
}

void MainComponent::resized() {
    auto area = getLocalBounds().reduced(16);

    auto topHeader = area.removeFromTop(38);
    titleLabel.setBounds(topHeader.removeFromLeft(70));
    subTitleLabel.setBounds(topHeader.removeFromLeft(200));

    auto topBtns = area.removeFromTop(40);
    streamButton.setBounds(topBtns.removeFromLeft(topBtns.getWidth() / 2 - 5));
    topBtns.removeFromLeft(10);
    monitorButton.setBounds(topBtns);
    area.removeFromTop(14);

    routingCard.setBounds(area.removeFromTop(206));
    auto rArea = routingCard.getBounds().reduced(14);
    rArea.removeFromTop(16);

    auto layoutRow = [&rArea](juce::Label& lbl, juce::ComboBox& box, LevelMeter& meter) {
        lbl.setBounds(rArea.removeFromTop(16));
        rArea.removeFromTop(2);
        box.setBounds(rArea.removeFromTop(26));
        rArea.removeFromTop(3);
        meter.setBounds(rArea.removeFromTop(4));
        rArea.removeFromTop(8);
    };

    layoutRow(inputLabel, inputCombo, inputMeter);
    layoutRow(monitorLabel, monitorCombo, monitorMeter);
    layoutRow(virtualLabel, virtualCombo, virtualMeter);

    area.removeFromTop(12);

    engineCard.setBounds(area.removeFromTop(106));
    auto eArea = engineCard.getBounds().reduced(14);
    eArea.removeFromTop(16);

    auto leftCol = eArea.removeFromLeft(250);
    eArea.removeFromLeft(14);

    auto row1 = leftCol.removeFromTop(32);
    sampleRateLabel.setBounds(row1.removeFromLeft(90));
    sampleRateCombo.setBounds(row1);
    leftCol.removeFromTop(6);

    auto row2 = leftCol.removeFromTop(32);
    bufferSizeLabel.setBounds(row2.removeFromLeft(90));
    bufferSizeCombo.setBounds(row2);

    latencyValueLabel.setBounds(eArea.removeFromTop(32));
    latencyDescLabel.setBounds(eArea.removeFromTop(18));

    area.removeFromTop(12);

    rackCard.setBounds(area);
    auto rcArea = rackCard.getBounds().reduced(14);
    rcArea.removeFromTop(14);

    auto rackControlRow = rcArea.removeFromTop(26);
    scanBtn.setBounds(rackControlRow.removeFromRight(95));
    rackControlRow.removeFromRight(8);
    bypassAllBtn.setBounds(rackControlRow.removeFromRight(105));
    scanStatusLabel.setBounds(rackControlRow);

    rcArea.removeFromTop(8);
    rackViewport.setBounds(rcArea);
    rebuildRackLayout();
}

void MainComponent::choosePlugin(int slotIdx) {
    if (pluginManager.getPluginList().getTypes().isEmpty()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "VST3 Plugins", "No plugins found yet. Click 'Scan VST3' to discover your plugins.");
        return;
    }

    new PluginSearchWindow(pluginManager.getPluginList(), [this, slotIdx](const juce::PluginDescription& desc) {
        auto setup = audioEngine.getMainManager().getAudioDeviceSetup();
        if (pluginManager.loadPlugin(slotIdx, desc, setup.sampleRate, setup.bufferSize))
            slots[slotIdx]->setPluginName(desc.name);
    });
}

void MainComponent::refreshDeviceLists() {
    if (auto* asio = audioEngine.getMainManager().getCurrentDeviceTypeObject()) {
        asio->scanForDevices();
        bool isAsio = (asio->getTypeName() == "ASIO");

        inputCombo.clear(juce::dontSendNotification);
        monitorCombo.clear(juce::dontSendNotification);

        if (isAsio) {
            auto devs = asio->getDeviceNames();
            for (int i = 0; i < devs.size(); ++i) {
                inputCombo.addItem(devs[i] + " - Mic In 1", i + 1);
                monitorCombo.addItem(devs[i] + " - Out 1/2", i + 1);
            }

            auto current = audioEngine.getMainManager().getCurrentAudioDevice();
            if (current != nullptr) {
                int idx = devs.indexOf(current->getName()) + 1;
                inputCombo.setSelectedId(idx, juce::dontSendNotification);
                monitorCombo.setSelectedId(idx, juce::dontSendNotification);
            } else if (!devs.isEmpty()) {
                inputCombo.setSelectedId(1, juce::dontSendNotification);
                monitorCombo.setSelectedId(1, juce::dontSendNotification);
            }
        } else {
            auto inDevs = asio->getDeviceNames(true);
            auto outDevs = asio->getDeviceNames(false);

            for (int i = 0; i < inDevs.size(); ++i)
                inputCombo.addItem(inDevs[i], i + 1);

            for (int i = 0; i < outDevs.size(); ++i)
                monitorCombo.addItem(outDevs[i], i + 1);

            auto setup = audioEngine.getMainManager().getAudioDeviceSetup();
            int inIdx = inDevs.indexOf(setup.inputDeviceName) + 1;
            int outIdx = outDevs.indexOf(setup.outputDeviceName) + 1;
            inputCombo.setSelectedId(inIdx > 0 ? inIdx : 1, juce::dontSendNotification);
            monitorCombo.setSelectedId(outIdx > 0 ? outIdx : 1, juce::dontSendNotification);
        }

        updateRatesAndBuffers();
    }

    if (auto* winType = audioEngine.getVirtualManager().getCurrentDeviceTypeObject()) {
        winType->scanForDevices();
        auto outDevices = winType->getDeviceNames(false);

        virtualCombo.clear(juce::dontSendNotification);
        for (int i = 0; i < outDevices.size(); ++i) {
            virtualCombo.addItem(outDevices[i], i + 1);
            if (outDevices[i].containsIgnoreCase("cable") || outDevices[i].containsIgnoreCase("virtual"))
                virtualCombo.setSelectedId(i + 1, juce::dontSendNotification);
        }
        if (virtualCombo.getSelectedId() == 0 && !outDevices.isEmpty())
            virtualCombo.setSelectedId(1, juce::dontSendNotification);

        changeVirtualDevice();
    }
}

void MainComponent::updateRatesAndBuffers() {
    if (auto* dev = audioEngine.getMainManager().getCurrentAudioDevice()) {
        sampleRateCombo.clear(juce::dontSendNotification);
        for (auto rate : dev->getAvailableSampleRates())
            sampleRateCombo.addItem(juce::String((int)rate) + " Hz", (int)rate);
        sampleRateCombo.setSelectedId((int)dev->getCurrentSampleRate(), juce::dontSendNotification);

        bufferSizeCombo.clear(juce::dontSendNotification);
        for (auto buf : dev->getAvailableBufferSizes())
            bufferSizeCombo.addItem(juce::String(buf) + " spl", buf);
        bufferSizeCombo.setSelectedId(dev->getCurrentBufferSizeSamples(), juce::dontSendNotification);

        updateHeadphonesLatency();
    }
}

void MainComponent::updateHeadphonesLatency() {
    if (auto* dev = audioEngine.getMainManager().getCurrentAudioDevice()) {
        double rate = dev->getCurrentSampleRate();
        int inLat = dev->getInputLatencyInSamples();
        int outLat = dev->getOutputLatencyInSamples();
        int bufSize = dev->getCurrentBufferSizeSamples();

        int totalHwSamples = inLat + outLat;
        if (totalHwSamples <= 0)
            totalHwSamples = bufSize * 2;

        double ms = rate > 0.0 ? (totalHwSamples * 1000.0 / rate) : 0.0;

        latencyValueLabel.setText(juce::String(ms, 1) + " ms", juce::dontSendNotification);
        latencyDescLabel.setText(juce::String(bufSize) + " spl @ " + juce::String((int)rate) + " Hz (ASIO Direct)", juce::dontSendNotification);
    }
}

void MainComponent::changeInputDevice() {
    auto devName = inputCombo.getText();
    if (devName.isEmpty()) return;

    if (devName.contains(" - Mic In"))
        devName = devName.upToFirstOccurrenceOf(" - Mic In", false, false);

    auto setup = audioEngine.getMainManager().getAudioDeviceSetup();
    setup.inputDeviceName = devName;
    setup.outputDeviceName = devName;
    audioEngine.getMainManager().setAudioDeviceSetup(setup, true);
    updateRatesAndBuffers();
}

void MainComponent::changeMonitorDevice() {
    changeInputDevice();
}

void MainComponent::changeVirtualDevice() {
    auto devName = virtualCombo.getText();
    if (devName.isEmpty()) return;
    auto setup = audioEngine.getVirtualManager().getAudioDeviceSetup();
    setup.outputDeviceName = devName;
    setup.inputDeviceName = "";
    audioEngine.getVirtualManager().setAudioDeviceSetup(setup, true);
}

void MainComponent::changeSampleRate() {
    int rate = sampleRateCombo.getSelectedId();
    if (rate <= 0) return;
    auto setup = audioEngine.getMainManager().getAudioDeviceSetup();
    setup.sampleRate = rate;
    audioEngine.getMainManager().setAudioDeviceSetup(setup, true);
    updateHeadphonesLatency();
}

void MainComponent::changeBufferSize() {
    int size = bufferSizeCombo.getSelectedId();
    if (size <= 0) return;
    auto setup = audioEngine.getMainManager().getAudioDeviceSetup();
    setup.bufferSize = size;
    audioEngine.getMainManager().setAudioDeviceSetup(setup, true);
    updateHeadphonesLatency();
}

void MainComponent::timerCallback() {
    auto decay = [](float val, float& cur, LevelMeter& m) {
        if (val > cur) cur = val;
        else cur *= 0.82f;
        if (cur < 0.001f) cur = 0.0f;
        m.setLevel(cur);
    };

    decay(audioEngine.getInputPeak(), curInputLevel, inputMeter);
    decay(audioEngine.getMonitorPeak(), curMonitorLevel, monitorMeter);
    decay(audioEngine.getVirtualPeak(), curVirtualLevel, virtualMeter);
}