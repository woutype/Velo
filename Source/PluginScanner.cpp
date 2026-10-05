#include "PluginScanner.h"
#include <thread>
#include <set>
#include <vector>

namespace velo
{
namespace
{
    constexpr int kTimeoutMs  = 60000;
    constexpr int kMaxWorkers = 3;

    enum class ProbeStatus { ok, failed, aborted };

    // Lives on a scan thread; talks to one helper process.
    class ScanClient : public juce::ChildProcessCoordinator
    {
    public:
        ~ScanClient() override { killWorkerProcess(); }

        ProbeStatus probe (const juce::String& format, const juce::String& id,
                           const std::function<bool()>& shouldAbort,
                           std::unique_ptr<juce::XmlElement>& out, juce::String& error)
        {
            gotResponse.reset();
            lost = false;
            { const juce::ScopedLock sl (lock); result.reset(); }

            if (! launched)
            {
                auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
                if (! launchWorkerProcess (exe, kScanUid, 0, 0))
                {
                    error = "could not start scanner process";
                    return ProbeStatus::failed;
                }
                launched = true;
            }

            const juce::String msg = format + "|" + id;
            if (! sendMessageToWorker (juce::MemoryBlock (msg.toRawUTF8(), msg.getNumBytesAsUTF8())))
            {
                error = "scanner process not reachable";
                launched = false;
                return ProbeStatus::failed;
            }

            int waited = 0;
            while (! gotResponse.wait (100))
            {
                waited += 100;
                if (shouldAbort()) { killWorkerProcess(); launched = false; return ProbeStatus::aborted; }
                if (waited >= kTimeoutMs) { killWorkerProcess(); launched = false; error = "timeout"; return ProbeStatus::failed; }
            }

            if (lost.load())
            {
                launched = false;
                error = "plugin crashed during scan";
                return ProbeStatus::failed;
            }

            const juce::ScopedLock sl (lock);
            out = std::move (result);
            return out != nullptr ? ProbeStatus::ok : ProbeStatus::failed;
        }

        void handleMessageFromWorker (const juce::MemoryBlock& mb) override
        {
            auto xml = juce::XmlDocument::parse (mb.toString());
            { const juce::ScopedLock sl (lock); result = std::move (xml); }
            gotResponse.signal();
        }

        void handleConnectionLost() override
        {
            lost = true;
            gotResponse.signal();
        }

    private:
        juce::WaitableEvent gotResponse;
        juce::CriticalSection lock;
        std::unique_ptr<juce::XmlElement> result;
        std::atomic<bool> lost { false };
        bool launched = false;
    };
}

//==============================================================================
ScanWorker::ScanWorker()
{
    formats.addDefaultFormats();
}

void ScanWorker::handleMessageFromCoordinator (const juce::MemoryBlock& mb)
{
    const auto msg = mb.toString();
    const int sep = msg.indexOfChar ('|');
    const auto formatName = msg.substring (0, sep);
    const auto id = msg.substring (sep + 1);

    juce::XmlElement root ("RESULT");
    try
    {
        for (int i = 0; i < formats.getNumFormats(); ++i)
        {
            auto* f = formats.getFormat (i);
            if (f == nullptr || f->getName() != formatName) continue;

            juce::OwnedArray<juce::PluginDescription> found;
            f->findAllTypesForFile (found, id);
            for (auto* d : found)
                root.addChildElement (d->createXml().release());
        }
    }
    catch (...) {}

    const auto text = root.toString();
    sendMessageToCoordinator (juce::MemoryBlock (text.toRawUTF8(), text.getNumBytesAsUTF8()));
}

//==============================================================================
PluginScanService::PluginScanService (juce::KnownPluginList& l, juce::AudioPluginFormatManager& f, std::function<void()> save)
    : juce::Thread ("VeloPluginScan"), list (l), formats (f), saveCache (std::move (save)) {}

PluginScanService::~PluginScanService()
{
    *alive = false;
    stopThread (15000);
}

void PluginScanService::start (bool full, const juce::StringArray& extra,
                               std::function<void (const ScanProgress&)> onProgress,
                               std::function<void (const ScanResult&)> onDone)
{
    if (isThreadRunning()) return;
    fullRescan = full;
    folders = extra;
    progressCb = std::move (onProgress);
    doneCb = std::move (onDone);
    startThread (juce::Thread::Priority::background);
}

void PluginScanService::cancel()
{
    signalThreadShouldExit();
}

void PluginScanService::run()
{
    struct Job { juce::AudioPluginFormat* format; juce::String id; };
    std::vector<Job> jobs;
    ScanResult result;
    const int typesBefore = list.getNumTypes();

    if (fullRescan)
    {
        list.clear();
        list.clearBlacklistedFiles();
    }
    else
    {
        // drop plugins whose files have disappeared
        for (auto& d : list.getTypes())
            for (int i = 0; i < formats.getNumFormats(); ++i)
                if (auto* f = formats.getFormat (i))
                    if (f->getName() == d.pluginFormatName && ! f->doesPluginStillExist (d))
                    {
                        list.removeType (d);
                        ++result.removed;
                    }
    }

    for (int i = 0; i < formats.getNumFormats(); ++i)
    {
        auto* f = formats.getFormat (i);
        if (f == nullptr) continue;

        juce::FileSearchPath paths = f->getDefaultLocationsToSearch();
        for (auto& dir : folders) paths.add (juce::File (dir));

        std::set<juce::String> seen;
        for (auto& id : f->searchPathsForPlugins (paths, true, false))
        {
            if (! seen.insert (id.toLowerCase()).second) continue;
            if (! fullRescan && list.isListingUpToDate (id, *f)) continue;
            if (list.getBlacklistedFiles().contains (id)) continue;
            jobs.push_back ({ f, id });
        }
    }

    struct State
    {
        std::atomic<int> next { 0 }, done { 0 }, found { 0 }, failed { 0 };
        std::atomic<bool> progressPending { false };
        juce::CriticalSection nameLock;
        juce::String currentName;
        juce::StringArray failedNames;
    };
    auto st = std::make_shared<State>();
    const int total = (int) jobs.size();

    auto postProgress = [this, st, total]()
    {
        if (! progressCb || st->progressPending.exchange (true)) return;
        auto flag = alive;
        auto cb = progressCb;
        juce::MessageManager::callAsync ([flag, cb, st, total]
        {
            st->progressPending = false;
            if (! flag->load()) return;
            ScanProgress p;
            p.done = st->done.load(); p.total = total; p.found = st->found.load(); p.failed = st->failed.load();
            { const juce::ScopedLock sl (st->nameLock); p.current = st->currentName; }
            cb (p);
        });
    };

    auto shouldAbort = [this] { return threadShouldExit(); };

    auto worker = [&]()
    {
        std::unique_ptr<ScanClient> client;
        while (! threadShouldExit())
        {
            const int idx = st->next++;
            if (idx >= total) break;
            auto& job = jobs[(size_t) idx];
            { const juce::ScopedLock sl (st->nameLock); st->currentName = juce::File (job.id).getFileName(); }

            if (client == nullptr) client = std::make_unique<ScanClient>();

            std::unique_ptr<juce::XmlElement> xml;
            juce::String err;
            const auto status = client->probe (job.format->getName(), job.id, shouldAbort, xml, err);
            if (status == ProbeStatus::aborted) break;

            int count = 0;
            if (status == ProbeStatus::ok && xml != nullptr)
            {
                for (auto* e : xml->getChildIterator())
                {
                    juce::PluginDescription d;
                    if (d.loadFromXml (*e)) { list.addType (d); ++count; }
                }
            }

            if (count > 0) st->found += count;
            else
            {
                list.addToBlacklist (job.id);
                ++st->failed;
                { const juce::ScopedLock sl (st->nameLock); st->failedNames.add (juce::File (job.id).getFileName()); }
                juce::Logger::writeToLog ("Scan failed: " + job.id + " (" + (err.isEmpty() ? juce::String ("no plugin types") : err) + ")");
                client.reset();   // fresh helper process after a failure
            }

            const int d = ++st->done;
            if (d % 20 == 0 && saveCache) saveCache();
            postProgress();
        }
    };

    if (total > 0)
    {
        const int n = juce::jmax (1, juce::jmin (kMaxWorkers, total, juce::jmax (1, juce::SystemStats::getNumCpus() / 2)));
        std::vector<std::thread> pool;
        for (int i = 0; i < n; ++i) pool.emplace_back (worker);
        for (auto& t : pool) t.join();
    }

    if (saveCache) saveCache();

    result.cancelled = threadShouldExit();
    result.totalPlugins = list.getNumTypes();
    result.newlyFound = juce::jmax (0, result.totalPlugins - typesBefore + result.removed);
    result.failed = st->failed.load();
    { const juce::ScopedLock sl (st->nameLock); result.failedNames = st->failedNames; }

    auto flag = alive;
    auto cb = doneCb;
    juce::MessageManager::callAsync ([flag, cb, result]
    {
        if (flag->load() && cb) cb (result);
    });
}
}
