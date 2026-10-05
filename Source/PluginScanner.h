#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <atomic>
#include <memory>

// ---------------------------------------------------------------------------
// Out-of-process, parallel, incremental VST3 scanner.
//  * Every plugin file is probed in a separate helper process (this same exe
//    started with --velo-scan:<pipe>), so a crashing / hanging plugin can never
//    take Velo down. Failures are blacklisted and skipped next time.
//  * Unchanged plugins are never re-probed (modification-time cache).
//  * Up to a few helper processes scan in parallel.
// ---------------------------------------------------------------------------
namespace velo
{
    inline const char* kScanUid = "velo-scan";

    // Runs inside the helper process.
    class ScanWorker : public juce::ChildProcessWorker
    {
    public:
        ScanWorker();
        bool initialise (const juce::String& commandLine) { return initialiseFromCommandLine (commandLine, kScanUid); }
        void handleMessageFromCoordinator (const juce::MemoryBlock&) override;
        void handleConnectionLost() override { juce::JUCEApplicationBase::quit(); }

    private:
        juce::AudioPluginFormatManager formats;
    };

    struct ScanProgress
    {
        int done = 0, total = 0, found = 0, failed = 0;
        juce::String current;
    };

    struct ScanResult
    {
        int totalPlugins = 0, newlyFound = 0, failed = 0, removed = 0;
        bool cancelled = false;
        juce::StringArray failedNames;
    };

    class PluginScanService : private juce::Thread
    {
    public:
        PluginScanService (juce::KnownPluginList&, juce::AudioPluginFormatManager&, std::function<void()> saveCache);
        ~PluginScanService() override;

        void start (bool fullRescan, const juce::StringArray& extraFolders,
                    std::function<void (const ScanProgress&)> onProgress,
                    std::function<void (const ScanResult&)> onDone);
        void cancel();
        bool isScanning() const { return isThreadRunning(); }

    private:
        void run() override;

        juce::KnownPluginList& list;
        juce::AudioPluginFormatManager& formats;
        std::function<void()> saveCache;

        bool fullRescan = false;
        juce::StringArray folders;
        std::function<void (const ScanProgress&)> progressCb;
        std::function<void (const ScanResult&)> doneCb;
        std::shared_ptr<std::atomic<bool>> alive { std::make_shared<std::atomic<bool>> (true) };
    };
}
