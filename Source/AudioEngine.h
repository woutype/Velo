#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>
#include "PluginManager.h"

// Real-time engine:
//   mic (ASIO, e.g. Arturia MiniFuse) -> gain -> VST3 chain -> headphone monitor
//                                                          \-> lock-free ring -> drift-compensating resampler
//                                                              -> virtual cable (WASAPI / DirectSound) 
// The two devices run on different clocks, so the virtual-cable side continuously
// re-samples by a tiny PI-controlled ratio to keep the queue at a constant depth.
class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::ChangeBroadcaster,
                    private juce::ChangeListener
{
public:
    struct HeadphoneLatency
    {
        bool valid = false, estimated = false;
        double rate = 0, inMs = 0, outMs = 0, fxMs = 0, totalMs = 0;
        int bufferSamples = 0;
    };

    struct VirtualLatency
    {
        bool valid = false;
        double rate = 0, inMs = 0, fxMs = 0, queueMs = 0, deviceMs = 0, totalMs = 0;
        int bufferSamples = 0, underruns = 0, overruns = 0;
    };

    explicit AudioEngine (PluginManager&);
    ~AudioEngine() override;

    // returns human-readable warnings (empty if everything opened fine)
    juce::String start (const juce::String& preferredDevice, int inputChannel, int outputPair, int mainBuffer,
                        const juce::String& virtualType, const juce::String& virtualDevice, int virtualBuffer);
    void shutdown();

    //== main (headphones) device ============================================
    juce::String getMainTypeName() const;
    juce::StringArray getMainDevices (bool rescan = false);
    juce::String getMainDeviceName() const;
    juce::String openMainDevice (const juce::String& device, int inputChannel, int outputPair, int bufferSize);
    juce::String reopenMainWithDriverDefaults();
    juce::StringArray getInputChannelNames() const;
    juce::StringArray getOutputPairNames() const;
    juce::Array<int>  getMainBufferSizes() const;
    int  getMainBufferSize() const;
    double getMainSampleRate() const;
    bool hasControlPanel() const;
    bool showControlPanel();
    bool isMainRunning() const;

    //== virtual cable device =================================================
    juce::StringArray getVirtualTypes();
    juce::String getVirtualTypeName() const;
    juce::StringArray getVirtualDevices (bool rescan = false);
    juce::String getVirtualDeviceName() const;
    juce::String openVirtualDevice (const juce::String& type, const juce::String& device, int bufferSize);
    juce::Array<int> getVirtualBufferSizes() const;
    int  getVirtualBufferSize() const;
    bool isVirtualRunning() const { return virtualRunning.load(); }

    //== controls / metering ==================================================
    void setStreamActive (bool a)  { streamActive.store (a); }
    void setMonitorActive (bool a) { monitorActive.store (a); }
    void setInputGain (float g)    { inGain.store (g); }
    void setMonitorGain (float g)  { monGain.store (g); }
    void setVirtualGain (float g)  { virtGain.store (g); }

    float getInputPeak()   { return inputPeak.exchange (0.0f); }
    float getMonitorPeak() { return monitorPeak.exchange (0.0f); }
    float getVirtualPeak() { return virtualPeak.exchange (0.0f); }
    double getCpuUsage() const { return mainDeviceManager.getCpuUsage(); }
    int getBadBlockCount() const { return nanBlocks.load() + oversizedBlocks.load(); }

    HeadphoneLatency getHeadphoneLatency() const;
    VirtualLatency   getVirtualLatency() const;
    juce::String getLastError() const { return lastError; }

    //== AudioIODeviceCallback (main device) ==================================
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { sendChangeMessage(); }

    struct VirtualCallback : public juce::AudioIODeviceCallback
    {
        AudioEngine* parent = nullptr;
        juce::LagrangeInterpolator interp[2];
        std::vector<float> tmpIn[2], tmpOut[2];
        double curRatio = 1.0;
        bool primed = false;
        float gainPrev = 1.0f;

        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                               const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice*) override;
        void audioDeviceStopped() override;
        void audioDeviceError (const juce::String&) override { parent->lastError = "Virtual cable device error"; }
    };

    static constexpr int kRing = 1 << 16;                 // frames, power of two
    static constexpr int kRingMask = kRing - 1;

    PluginManager& pluginManager;
    juce::AudioDeviceManager mainDeviceManager, virtualDeviceManager;
    VirtualCallback virtualCallback;

    juce::AudioBuffer<float> scratch;
    juce::AudioBuffer<float> ring { 2, kRing };
    juce::MidiBuffer midi;
    std::atomic<uint64_t> wPos { 0 }, rPos { 0 };

    std::atomic<bool> streamActive { false }, monitorActive { false }, virtualRunning { false };
    std::atomic<float> inGain { 1.0f }, monGain { 1.0f }, virtGain { 1.0f };
    float inGainPrev = 1.0f, monGainPrev = 1.0f;

    std::atomic<float> inputPeak { 0.0f }, monitorPeak { 0.0f }, virtualPeak { 0.0f };
    std::atomic<double> srcRate { 0.0 }, dstRate { 0.0 };
    std::atomic<int> srcBlock { 0 }, dstBlock { 0 };
    std::atomic<float> queueMs { 0.0f };
    std::atomic<int> underruns { 0 }, overruns { 0 }, nanBlocks { 0 }, oversizedBlocks { 0 };
    juce::String lastError;
    bool started = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
