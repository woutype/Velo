#include "AudioEngine.h"
#include <cmath>

namespace
{
    constexpr int kMaxVirtualBlock = 8192;
    constexpr int kMaxVirtualIn    = kMaxVirtualBlock * 8;   // enough for 8x rate ratio

    inline void applyRamp (float* d, int n, float from, float to)
    {
        if (from == to)
        {
            if (to != 1.0f) juce::FloatVectorOperations::multiply (d, to, n);
            return;
        }
        const float inc = (to - from) / (float) n;
        float g = from;
        for (int i = 0; i < n; ++i) { g += inc; d[i] *= g; }
    }

    inline float peakOf (const float* d, int n)
    {
        auto r = juce::FloatVectorOperations::findMinAndMax (d, n);
        return juce::jmax (std::abs (r.getStart()), std::abs (r.getEnd()));
    }

    inline void raisePeak (std::atomic<float>& a, float v)
    {
        if (v > a.load (std::memory_order_relaxed)) a.store (v, std::memory_order_relaxed);
    }

    inline bool isClean (const float* d, int n)
    {
        for (int i = 0; i < n; ++i) if (! std::isfinite (d[i])) return false;
        return true;
    }

    juce::AudioIODeviceType* findType (juce::AudioDeviceManager& m, const juce::String& name)
    {
        for (auto* t : m.getAvailableDeviceTypes())
            if (t->getTypeName() == name) return t;
        return nullptr;
    }
}

//==============================================================================
AudioEngine::AudioEngine (PluginManager& pm) : pluginManager (pm)
{
    virtualCallback.parent = this;
    scratch.setSize (2, 8192);
    ring.clear();
    for (int c = 0; c < 2; ++c)
    {
        virtualCallback.tmpIn[c].assign ((size_t) kMaxVirtualIn, 0.0f);
        virtualCallback.tmpOut[c].assign ((size_t) kMaxVirtualBlock, 0.0f);
    }
    mainDeviceManager.addChangeListener (this);
    virtualDeviceManager.addChangeListener (this);
}

AudioEngine::~AudioEngine() { shutdown(); }

void AudioEngine::shutdown()
{
    if (! started) return;
    started = false;
    mainDeviceManager.removeChangeListener (this);
    virtualDeviceManager.removeChangeListener (this);
    mainDeviceManager.removeAudioCallback (this);
    virtualDeviceManager.removeAudioCallback (&virtualCallback);
    virtualDeviceManager.closeAudioDevice();
    mainDeviceManager.closeAudioDevice();
}

juce::String AudioEngine::start (const juce::String& preferredDevice, int inCh, int outPair, int mainBuffer,
                                 const juce::String& vType, const juce::String& vDevice, int vBuffer)
{
    juce::StringArray warnings;

    //-- main interface (ASIO preferred) ---------------------------------------
    mainDeviceManager.getAvailableDeviceTypes();          // populate list
    const bool haveAsio = findType (mainDeviceManager, "ASIO") != nullptr;
    if (haveAsio) mainDeviceManager.setCurrentAudioDeviceType ("ASIO", true);
    else          warnings.add ("ASIO is not available in this build/system - falling back to Windows Audio (higher latency).");

    auto err = mainDeviceManager.initialise (1, 2, nullptr, true);
    if (err.isNotEmpty()) warnings.add ("Audio interface: " + err);

    if (auto* type = mainDeviceManager.getCurrentDeviceTypeObject())
    {
        type->scanForDevices();
        auto devices = type->getDeviceNames (false);

        juce::String chosen;
        if (devices.contains (preferredDevice)) chosen = preferredDevice;
        for (auto& d : devices)
            if (chosen.isEmpty() && (d.containsIgnoreCase ("minifuse") || d.containsIgnoreCase ("arturia"))) chosen = d;
        if (chosen.isEmpty() && ! devices.isEmpty()) chosen = devices[0];

        if (chosen.isNotEmpty())
        {
            err = openMainDevice (chosen, inCh, outPair, mainBuffer);
            if (err.isNotEmpty()) warnings.add (chosen + ": " + err + " (is another program using the device?)");
        }
        else warnings.add ("No audio interface found.");
    }

    mainDeviceManager.addAudioCallback (this);

    //-- virtual cable ---------------------------------------------------------
    err = virtualDeviceManager.initialise (0, 2, nullptr, true);
    if (err.isNotEmpty()) warnings.add ("Virtual cable: " + err);

    juce::String type = vType;
    if (type.isEmpty() || findType (virtualDeviceManager, type) == nullptr) type = "Windows Audio";
    if (findType (virtualDeviceManager, type) == nullptr && ! virtualDeviceManager.getAvailableDeviceTypes().isEmpty())
        type = virtualDeviceManager.getAvailableDeviceTypes()[0]->getTypeName();

    juce::String device = vDevice;
    auto vDevices = [&]() -> juce::StringArray
    {
        if (auto* t = findType (virtualDeviceManager, type)) { t->scanForDevices(); return t->getDeviceNames (false); }
        return {};
    }();
    if (! vDevices.contains (device))
    {
        device = {};
        for (auto& d : vDevices)
            if (d.containsIgnoreCase ("cable") || d.containsIgnoreCase ("virtual")) { device = d; break; }
        if (device.isEmpty() && ! vDevices.isEmpty()) device = vDevices[0];
    }
    if (device.isNotEmpty())
    {
        err = openVirtualDevice (type, device, vBuffer);
        if (err.isNotEmpty()) warnings.add ("Virtual cable: " + err);
    }
    virtualDeviceManager.addAudioCallback (&virtualCallback);

    started = true;
    return warnings.joinIntoString ("\n");
}

//== main device ===============================================================
juce::String AudioEngine::getMainTypeName() const { return mainDeviceManager.getCurrentAudioDeviceType(); }

juce::StringArray AudioEngine::getMainDevices (bool rescan)
{
    if (auto* t = mainDeviceManager.getCurrentDeviceTypeObject())
    {
        if (rescan) t->scanForDevices();
        return t->getDeviceNames (false);
    }
    return {};
}

juce::String AudioEngine::getMainDeviceName() const
{
    if (auto* d = mainDeviceManager.getCurrentAudioDevice()) return d->getName();
    return {};
}

juce::String AudioEngine::openMainDevice (const juce::String& device, int inCh, int outPair, int bufferSize)
{
    auto* type = mainDeviceManager.getCurrentDeviceTypeObject();
    if (type == nullptr) return "No audio driver";

    auto configure = [&] (juce::AudioDeviceManager::AudioDeviceSetup& s)
    {
        s.outputDeviceName = device;
        if (type->hasSeparateInputsAndOutputs())
        {
            auto ins = type->getDeviceNames (true);
            const int di = type->getDefaultDeviceIndex (true);
            s.inputDeviceName = ins.contains (device) ? device : (juce::isPositiveAndBelow (di, ins.size()) ? ins[di] : juce::String());
        }
        else s.inputDeviceName = device;
        s.sampleRate = 0.0;                    // driver decides (MiniFuse Control Center)
        s.bufferSize = bufferSize;             // 0 = driver default
    };

    auto setup = mainDeviceManager.getAudioDeviceSetup();
    const bool sameDevice = (getMainDeviceName() == device);
    if (! sameDevice)
    {
        setup.useDefaultInputChannels = true;
        setup.useDefaultOutputChannels = true;
    }
    configure (setup);
    auto err = mainDeviceManager.setAudioDeviceSetup (setup, true);
    if (err.isNotEmpty()) return err;

    // apply the channel selection now that the real channel lists are known
    if (auto* dev = mainDeviceManager.getCurrentAudioDevice())
    {
        const int nIn  = dev->getInputChannelNames().size();
        const int nOut = dev->getOutputChannelNames().size();

        setup = mainDeviceManager.getAudioDeviceSetup();
        setup.useDefaultInputChannels = false;
        setup.useDefaultOutputChannels = false;
        setup.inputChannels.clear();
        setup.outputChannels.clear();
        if (nIn > 0)  setup.inputChannels.setBit (juce::jlimit (0, nIn - 1, inCh));
        if (nOut > 0)
        {
            const int first = juce::jlimit (0, juce::jmax (0, nOut - 1), outPair * 2);
            setup.outputChannels.setBit (first);
            if (first + 1 < nOut) setup.outputChannels.setBit (first + 1);
        }
        configure (setup);
        err = mainDeviceManager.setAudioDeviceSetup (setup, true);
    }
    return err;
}

juce::String AudioEngine::reopenMainWithDriverDefaults()
{
    const auto name = getMainDeviceName();
    if (name.isEmpty()) return {};
    auto setup = mainDeviceManager.getAudioDeviceSetup();
    setup.bufferSize = 0;
    setup.sampleRate = 0.0;
    return mainDeviceManager.setAudioDeviceSetup (setup, true);
}

juce::StringArray AudioEngine::getInputChannelNames() const
{
    if (auto* d = mainDeviceManager.getCurrentAudioDevice()) return d->getInputChannelNames();
    return {};
}

juce::StringArray AudioEngine::getOutputPairNames() const
{
    juce::StringArray pairs;
    if (auto* d = mainDeviceManager.getCurrentAudioDevice())
    {
        auto names = d->getOutputChannelNames();
        for (int i = 0; i < names.size(); i += 2)
            pairs.add (i + 1 < names.size() ? names[i] + " / " + names[i + 1] : names[i]);
    }
    return pairs;
}

juce::Array<int> AudioEngine::getMainBufferSizes() const
{
    if (auto* d = mainDeviceManager.getCurrentAudioDevice()) return d->getAvailableBufferSizes();
    return {};
}

int AudioEngine::getMainBufferSize() const
{
    if (auto* d = mainDeviceManager.getCurrentAudioDevice()) return d->getCurrentBufferSizeSamples();
    return 0;
}

double AudioEngine::getMainSampleRate() const
{
    if (auto* d = mainDeviceManager.getCurrentAudioDevice()) return d->getCurrentSampleRate();
    return 0.0;
}

bool AudioEngine::hasControlPanel() const
{
    if (auto* d = mainDeviceManager.getCurrentAudioDevice()) return d->hasControlPanel();
    return false;
}

bool AudioEngine::showControlPanel()
{
    if (auto* d = mainDeviceManager.getCurrentAudioDevice()) return d->showControlPanel();
    return false;
}

bool AudioEngine::isMainRunning() const
{
    auto* d = mainDeviceManager.getCurrentAudioDevice();
    return d != nullptr && d->isOpen() && d->isPlaying();
}

//== virtual device ============================================================
juce::StringArray AudioEngine::getVirtualTypes()
{
    juce::StringArray names;
    for (auto* t : virtualDeviceManager.getAvailableDeviceTypes())
        if (t->getTypeName() != "ASIO") names.add (t->getTypeName());   // never fight the main ASIO driver
    return names;
}

juce::String AudioEngine::getVirtualTypeName() const { return virtualDeviceManager.getCurrentAudioDeviceType(); }

juce::StringArray AudioEngine::getVirtualDevices (bool rescan)
{
    if (auto* t = virtualDeviceManager.getCurrentDeviceTypeObject())
    {
        if (rescan) t->scanForDevices();
        return t->getDeviceNames (false);
    }
    return {};
}

juce::String AudioEngine::getVirtualDeviceName() const
{
    if (auto* d = virtualDeviceManager.getCurrentAudioDevice()) return d->getName();
    return {};
}

juce::String AudioEngine::openVirtualDevice (const juce::String& type, const juce::String& device, int bufferSize)
{
    if (virtualDeviceManager.getCurrentAudioDeviceType() != type)
        virtualDeviceManager.setCurrentAudioDeviceType (type, true);

    if (device.isNotEmpty() && device == getMainDeviceName())
        return "The virtual cable must not use the same device as the headphones.";

    auto setup = virtualDeviceManager.getAudioDeviceSetup();
    setup.outputDeviceName = device;
    setup.inputDeviceName = {};
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.useDefaultOutputChannels = true;
    setup.sampleRate = 0.0;
    setup.bufferSize = bufferSize;               // 0 = driver default; otherwise chosen in Velo
    return virtualDeviceManager.setAudioDeviceSetup (setup, true);
}

juce::Array<int> AudioEngine::getVirtualBufferSizes() const
{
    if (auto* d = virtualDeviceManager.getCurrentAudioDevice()) return d->getAvailableBufferSizes();
    return {};
}

int AudioEngine::getVirtualBufferSize() const
{
    if (auto* d = virtualDeviceManager.getCurrentAudioDevice()) return d->getCurrentBufferSizeSamples();
    return 0;
}

//== latency ===================================================================
AudioEngine::HeadphoneLatency AudioEngine::getHeadphoneLatency() const
{
    HeadphoneLatency r;
    auto* d = mainDeviceManager.getCurrentAudioDevice();
    if (d == nullptr || ! d->isOpen()) return r;

    r.rate = d->getCurrentSampleRate();
    r.bufferSamples = d->getCurrentBufferSizeSamples();
    if (r.rate <= 0.0) return r;

    int inL = d->getInputLatencyInSamples(), outL = d->getOutputLatencyInSamples();
    if (inL + outL <= 0) { inL = outL = r.bufferSamples; r.estimated = true; }

    r.inMs  = inL * 1000.0 / r.rate;
    r.outMs = outL * 1000.0 / r.rate;
    r.fxMs  = pluginManager.getTotalLatencySamples() * 1000.0 / r.rate;
    r.totalMs = r.inMs + r.outMs + r.fxMs;
    r.valid = true;
    return r;
}

AudioEngine::VirtualLatency AudioEngine::getVirtualLatency() const
{
    VirtualLatency r;
    auto* v = virtualDeviceManager.getCurrentAudioDevice();
    auto* m = mainDeviceManager.getCurrentAudioDevice();
    if (v == nullptr || ! v->isOpen()) return r;

    const double vr = v->getCurrentSampleRate();
    r.rate = vr;
    r.bufferSamples = v->getCurrentBufferSizeSamples();
    if (vr <= 0.0) return r;

    r.deviceMs = juce::jmax (v->getOutputLatencyInSamples(), r.bufferSamples) * 1000.0 / vr;
    r.queueMs = queueMs.load();
    if (m != nullptr && m->isOpen() && m->getCurrentSampleRate() > 0.0)
    {
        const double mr = m->getCurrentSampleRate();
        const int inL = m->getInputLatencyInSamples() > 0 ? m->getInputLatencyInSamples() : m->getCurrentBufferSizeSamples();
        r.inMs = inL * 1000.0 / mr;
        r.fxMs = pluginManager.getTotalLatencySamples() * 1000.0 / mr;
    }
    r.totalMs = r.inMs + r.fxMs + r.queueMs + r.deviceMs;
    r.underruns = underruns.load();
    r.overruns = overruns.load();
    r.valid = true;
    return r;
}

//== audio callbacks ===========================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (device == nullptr) return;
    const int block = device->getCurrentBufferSizeSamples();
    const double rate = device->getCurrentSampleRate();

    scratch.setSize (2, juce::jmax (block * 2, 4096), false, false, true);
    srcRate.store (rate);
    srcBlock.store (block);
    rPos.store (wPos.load());
    inGainPrev = inGain.load();
    monGainPrev = monGain.load();
    pluginManager.prepareToPlay (rate, block);
}

void AudioEngine::audioDeviceStopped() { pluginManager.release(); }

void AudioEngine::audioDeviceError (const juce::String& message) { lastError = message; }

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                    int n, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);

    if (n <= 0) return;
    if (n > scratch.getNumSamples()) { ++oversizedBlocks; return; }
    if (numIn <= 0 || in[0] == nullptr) return;

    float* l = scratch.getWritePointer (0);
    float* r = scratch.getWritePointer (1);

    juce::FloatVectorOperations::copy (l, in[0], n);
    const float ig = inGain.load (std::memory_order_relaxed);
    applyRamp (l, n, inGainPrev, ig);
    inGainPrev = ig;
    raisePeak (inputPeak, peakOf (l, n));

    if (! streamActive.load (std::memory_order_relaxed)) return;

    juce::FloatVectorOperations::copy (r, l, n);

    float* ptrs[2] = { l, r };
    juce::AudioBuffer<float> view (ptrs, 2, n);
    midi.clear();
    pluginManager.process (view, midi);

    if (! isClean (l, n) || ! isClean (r, n))      // a misbehaving plugin must never reach the ears
    {
        ++nanBlocks;
        juce::FloatVectorOperations::clear (l, n);
        juce::FloatVectorOperations::clear (r, n);
    }

    // headphone monitor
    if (monitorActive.load (std::memory_order_relaxed))
    {
        const float mg = monGain.load (std::memory_order_relaxed);
        float pk = 0.0f;
        for (int c = 0; c < numOut && c < 2; ++c)
        {
            if (out[c] == nullptr) continue;
            juce::FloatVectorOperations::copy (out[c], c == 0 ? l : r, n);
            applyRamp (out[c], n, monGainPrev, mg);
            juce::FloatVectorOperations::clip (out[c], out[c], -1.0f, 1.0f, n);
            pk = juce::jmax (pk, peakOf (out[c], n));
        }
        monGainPrev = mg;
        raisePeak (monitorPeak, pk);
    }

    // virtual cable queue
    if (virtualRunning.load (std::memory_order_relaxed))
    {
        const uint64_t w = wPos.load (std::memory_order_relaxed);
        const uint64_t rd = rPos.load (std::memory_order_acquire);
        if (w - rd + (uint64_t) n > (uint64_t) kRing) { ++overruns; return; }

        const int start = (int) (w & (uint64_t) kRingMask);
        const int first = juce::jmin (n, kRing - start);
        ring.copyFrom (0, start, l, first);
        ring.copyFrom (1, start, r, first);
        if (first < n)
        {
            ring.copyFrom (0, 0, l + first, n - first);
            ring.copyFrom (1, 0, r + first, n - first);
        }
        wPos.store (w + (uint64_t) n, std::memory_order_release);
    }
}

//== virtual cable callbacks ===================================================
void AudioEngine::VirtualCallback::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    if (d == nullptr) return;
    parent->dstRate.store (d->getCurrentSampleRate());
    parent->dstBlock.store (d->getCurrentBufferSizeSamples());
    primed = false;
    gainPrev = parent->virtGain.load();
    parent->rPos.store (parent->wPos.load());
    parent->virtualRunning.store (true);
}

void AudioEngine::VirtualCallback::audioDeviceStopped()
{
    parent->virtualRunning.store (false);
    primed = false;
}

void AudioEngine::VirtualCallback::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out,
                                                                    int numOut, int n,
                                                                    const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);

    auto& e = *parent;
    if (n <= 0 || n > kMaxVirtualBlock) return;

    const double sr = e.srcRate.load(), dr = e.dstRate.load();
    if (sr <= 0.0 || dr <= 0.0 || ! e.streamActive.load (std::memory_order_relaxed))
    {
        primed = false;
        e.rPos.store (e.wPos.load (std::memory_order_acquire), std::memory_order_release);
        e.queueMs.store (0.0f);
        return;
    }

    const double base = sr / dr;
    const uint64_t w = e.wPos.load (std::memory_order_acquire);
    uint64_t rd = e.rPos.load (std::memory_order_relaxed);
    double fill = (double) (w - rd);
    const double target = juce::jmax (2.0 * n * base, 2.0 * (double) e.srcBlock.load()) + 64.0;

    if (! primed)
    {
        if (fill < target) return;                       // build up the safety cushion first
        primed = true;
        curRatio = base;
        interp[0].reset();
        interp[1].reset();
    }

    if (fill > target * 4.0 + n * base)                  // far too much queued: skip ahead
    {
        rd = w - (uint64_t) target;
        fill = target;
        ++e.overruns;
    }

    // PI-style drift control: queue too full -> consume slightly faster, and vice versa (+-0.5 % max)
    const double err = (fill - target) / target;
    const double desired = base * (1.0 + juce::jlimit (-0.005, 0.005, err * 0.01));
    curRatio += (desired - curRatio) * 0.02;

    const int need = (int) std::ceil (n * curRatio) + 4;
    const int total = need + 8;
    if (total > (int) tmpIn[0].size())
        return;

    if (fill < (double) total)
    {
        primed = false;
        ++e.underruns;
        e.rPos.store (rd, std::memory_order_release);
        return;
    }

    const int start = (int) (rd & (uint64_t) kRingMask);
    const int first = juce::jmin (total, kRing - start);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* dst = tmpIn[ch].data();
        juce::FloatVectorOperations::copy (dst, e.ring.getReadPointer (ch, start), first);
        if (first < total)
            juce::FloatVectorOperations::copy (dst + first, e.ring.getReadPointer (ch, 0), total - first);
    }

    const int used = interp[0].process (curRatio, tmpIn[0].data(), tmpOut[0].data(), n);
    interp[1].process (curRatio, tmpIn[1].data(), tmpOut[1].data(), n);
    e.rPos.store (rd + (uint64_t) used, std::memory_order_release);

    const float g = e.virtGain.load (std::memory_order_relaxed);
    applyRamp (tmpOut[0].data(), n, gainPrev, g);
    applyRamp (tmpOut[1].data(), n, gainPrev, g);
    gainPrev = g;

    float pk = 0.0f;
    for (int c = 0; c < numOut; ++c)
    {
        if (out[c] == nullptr) continue;
        if (numOut == 1)
        {
            juce::FloatVectorOperations::copy (out[c], tmpOut[0].data(), n);
            juce::FloatVectorOperations::add (out[c], tmpOut[1].data(), n);
            juce::FloatVectorOperations::multiply (out[c], 0.5f, n);
        }
        else juce::FloatVectorOperations::copy (out[c], tmpOut[c & 1].data(), n);

        juce::FloatVectorOperations::clip (out[c], out[c], -1.0f, 1.0f, n);
        pk = juce::jmax (pk, peakOf (out[c], n));
    }
    raisePeak (e.virtualPeak, pk);

    const float q = (float) (fill / sr * 1000.0);
    e.queueMs.store (e.queueMs.load() * 0.9f + q * 0.1f);
}
