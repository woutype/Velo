#include "AudioEngine.h"

AudioEngine::AudioEngine(PluginManager& pm) : pluginManager(pm) {
    virtualCallback.parent = this;
    fifo.setTotalSize(32768);
    fifoBuffer.setSize(2, 32768);
    scratchBuffer.setSize(2, 4096);
}

AudioEngine::~AudioEngine() {
    mainDeviceManager.removeAudioCallback(this);
    virtualDeviceManager.removeAudioCallback(&virtualCallback);
}

void AudioEngine::initDevices() {
    mainDeviceManager.setCurrentAudioDeviceType("ASIO", true);
    
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    mainDeviceManager.getAudioDeviceSetup(setup);

    if (auto* asioType = mainDeviceManager.getCurrentDeviceTypeObject()) {
        if (asioType->getTypeName() == "ASIO") {
            asioType->scanForDevices();
            auto devs = asioType->getDeviceNames();
            for (auto& d : devs) {
                if (d.containsIgnoreCase("minifuse") || d.containsIgnoreCase("arturia")) {
                    setup.inputDeviceName = d;
                    setup.outputDeviceName = d;
                    break;
                }
            }
        }
    }

    mainDeviceManager.initialise(1, 2, nullptr, true, {}, &setup);

    virtualDeviceManager.setCurrentAudioDeviceType("Windows Audio", true);
    virtualDeviceManager.initialise(0, 2, nullptr, true);

    mainDeviceManager.addAudioCallback(this);
    virtualDeviceManager.addAudioCallback(&virtualCallback);
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                  int numInputChannels,
                                                  float* const* outputChannelData,
                                                  int numOutputChannels,
                                                  int numSamples,
                                                  const juce::AudioIODeviceCallbackContext&) {
    for (int ch = 0; ch < numOutputChannels; ++ch) {
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);
    }

    if (numInputChannels == 0 || inputChannelData[0] == nullptr || numSamples > scratchBuffer.getNumSamples())
        return;

    const float* mic = inputChannelData[0];

    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float s = std::abs(mic[i]);
        if (s > peak) peak = s;
    }
    inputPeak.store(peak, std::memory_order_relaxed);

    if (!streamActive.load(std::memory_order_relaxed))
        return;

    scratchBuffer.copyFrom(0, 0, mic, numSamples);
    scratchBuffer.copyFrom(1, 0, mic, numSamples);

    juce::MidiBuffer emptyMidi;
    pluginManager.process(scratchBuffer, emptyMidi);

    const float* procL = scratchBuffer.getReadPointer(0);
    const float* procR = scratchBuffer.getReadPointer(1);

    if (monitorActive.load(std::memory_order_relaxed)) {
        if (numOutputChannels > 0 && outputChannelData[0] != nullptr)
            juce::FloatVectorOperations::copy(outputChannelData[0], procL, numSamples);
        if (numOutputChannels > 1 && outputChannelData[1] != nullptr)
            juce::FloatVectorOperations::copy(outputChannelData[1], procR, numSamples);

        float monPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i) {
            float s = std::max(std::abs(procL[i]), std::abs(procR[i]));
            if (s > monPeak) monPeak = s;
        }
        monitorPeak.store(monPeak, std::memory_order_relaxed);
    }

    int start1, size1, start2, size2;
    fifo.prepareToWrite(numSamples, start1, size1, start2, size2);

    if (size1 > 0) {
        fifoBuffer.copyFrom(0, start1, procL, size1);
        fifoBuffer.copyFrom(1, start1, procR, size1);
    }
    if (size2 > 0) {
        fifoBuffer.copyFrom(0, start2, procL + size1, size2);
        fifoBuffer.copyFrom(1, start2, procR + size1, size2);
    }
    fifo.finishedWrite(size1 + size2);
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    if (device != nullptr) {
        int bufSize = std::max(device->getCurrentBufferSizeSamples(), 2048);
        scratchBuffer.setSize(2, bufSize, false, false, true);
        pluginManager.prepareToPlay(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    }
}

void AudioEngine::audioDeviceStopped() {
    pluginManager.release();
}

void AudioEngine::VirtualCallback::audioDeviceIOCallbackWithContext(const float* const*, int,
                                                                   float* const* outputChannelData,
                                                                   int numOutputChannels, int numSamples,
                                                                   const juce::AudioIODeviceCallbackContext&) {
    for (int ch = 0; ch < numOutputChannels; ++ch) {
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);
    }

    if (parent == nullptr) return;

    int start1, size1, start2, size2;
    parent->fifo.prepareToRead(numSamples, start1, size1, start2, size2);

    float peak = 0.0f;
    if (size1 > 0) {
        for (int ch = 0; ch < numOutputChannels; ++ch) {
            auto* ptr = parent->fifoBuffer.getReadPointer(ch % 2, start1);
            juce::FloatVectorOperations::copy(outputChannelData[ch], ptr, size1);
            for (int i = 0; i < size1; ++i) {
                float s = std::abs(ptr[i]);
                if (s > peak) peak = s;
            }
        }
    }
    if (size2 > 0) {
        for (int ch = 0; ch < numOutputChannels; ++ch) {
            auto* ptr = parent->fifoBuffer.getReadPointer(ch % 2, start2);
            juce::FloatVectorOperations::copy(outputChannelData[ch] + size1, ptr, size2);
            for (int i = 0; i < size2; ++i) {
                float s = std::abs(ptr[i]);
                if (s > peak) peak = s;
            }
        }
    }
    parent->fifo.finishedRead(size1 + size2);
    parent->virtualPeak.store(peak, std::memory_order_relaxed);
}