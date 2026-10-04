#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "PluginManager.h"

class AudioEngine : public juce::AudioIODeviceCallback {
public:
    AudioEngine(PluginManager& pm);
    ~AudioEngine() override;

    void initDevices();
    
    juce::AudioDeviceManager& getMainManager() { return mainDeviceManager; }
    juce::AudioDeviceManager& getVirtualManager() { return virtualDeviceManager; }

    void setStreamActive(bool active) { streamActive.store(active, std::memory_order_relaxed); }
    void setMonitorActive(bool active) { monitorActive.store(active, std::memory_order_relaxed); }

    float getInputPeak() { return inputPeak.exchange(0.0f, std::memory_order_relaxed); }
    float getMonitorPeak() { return monitorPeak.exchange(0.0f, std::memory_order_relaxed); }
    float getVirtualPeak() { return virtualPeak.exchange(0.0f, std::memory_order_relaxed); }

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    struct VirtualCallback : public juce::AudioIODeviceCallback {
        AudioEngine* parent = nullptr;

        void audioDeviceIOCallbackWithContext(const float* const*, int,
                                              float* const* outputChannelData,
                                              int numOutputChannels, int numSamples,
                                              const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
        void audioDeviceStopped() override {}
    };

    PluginManager& pluginManager;
    juce::AudioDeviceManager mainDeviceManager;
    juce::AudioDeviceManager virtualDeviceManager;
    VirtualCallback virtualCallback;

    juce::AbstractFifo fifo { 16384 };
    juce::AudioBuffer<float> fifoBuffer;
    juce::AudioBuffer<float> scratchBuffer;

    std::atomic<bool> streamActive { true };
    std::atomic<bool> monitorActive { true };

    std::atomic<float> inputPeak { 0.0f };
    std::atomic<float> monitorPeak { 0.0f };
    std::atomic<float> virtualPeak { 0.0f };
};