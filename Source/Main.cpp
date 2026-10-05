#include <juce_gui_extra/juce_gui_extra.h>
#include <cstdlib>
#include "MainComponent.h"
#include "PluginScanner.h"
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
    // Helper (scanner) process: die silently instead of showing a crash dialog.
    void scannerCrashHandler (void*) { std::_Exit (1); }

    // Main process: leave a stack trace in the log before going down.
    void appCrashHandler (void*)
    {
        juce::Logger::writeToLog ("*** CRASH ***\n" + juce::SystemStats::getStackBacktrace());
    }

    // Dark native title bar (Windows 10 20H1+/11) so the window frame matches the UI.
    void darkenTitleBar (juce::Component& c)
    {
#if JUCE_WINDOWS
        if (auto* peer = c.getPeer())
            if (auto hwnd = (HWND) peer->getNativeHandle())
                if (auto lib = LoadLibraryA ("dwmapi.dll"))
                {
                    using Fn = HRESULT (WINAPI*) (HWND, DWORD, LPCVOID, DWORD);
                    if (auto fn = (Fn) GetProcAddress (lib, "DwmSetWindowAttribute"))
                    {
                        const BOOL dark = TRUE;
                        const COLORREF caption = RGB (0x07, 0x0b, 0x16);
                        fn (hwnd, 20, &dark, sizeof (dark));          // DWMWA_USE_IMMERSIVE_DARK_MODE
                        fn (hwnd, 35, &caption, sizeof (caption));    // DWMWA_CAPTION_COLOR (Windows 11)
                        fn (hwnd, 34, &caption, sizeof (caption));    // DWMWA_BORDER_COLOR  (Windows 11)
                    }
                    FreeLibrary (lib);
                }
#else
        juce::ignoreUnused (c);
#endif
    }
}

class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow (const juce::String& name)
        : DocumentWindow (name, juce::Colour (0xff070b16), DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent(), true);
        setResizable (true, true);
        setResizeLimits (440, 640, 980, 1200);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        darkenTitleBar (*this);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class VeloApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Velo"; }
    const juce::String getApplicationVersion() override { return "3.0.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }   // scanner helper processes are instances too

    void initialise (const juce::String& commandLine) override
    {
        // --- helper mode: probe one plugin file at a time for the main process ---
        if (commandLine.contains ("--" + juce::String (velo::kScanUid) + ":"))
        {
            juce::SystemStats::setApplicationCrashHandler (scannerCrashHandler);
            scanWorker = std::make_unique<velo::ScanWorker>();
            if (! scanWorker->initialise (commandLine))
                quit();
            return;
        }

        logger.reset (juce::FileLogger::createDateStampedLogger ("Velo", "velo_", ".log", "--- Velo " + getApplicationVersion() + " started ---"));
        juce::Logger::setCurrentLogger (logger.get());
        juce::SystemStats::setApplicationCrashHandler (appCrashHandler);
        juce::Process::setPriority (juce::Process::HighPriority);

        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow.reset();
        scanWorker.reset();
        juce::Logger::setCurrentLogger (nullptr);
        logger.reset();
    }

    void systemRequestedQuit() override { quit(); }

    void unhandledException (const std::exception* e, const juce::String& file, int line) override
    {
        juce::Logger::writeToLog ("Unhandled exception: " + juce::String (e != nullptr ? e->what() : "unknown")
                                  + " at " + file + ":" + juce::String (line));
    }

private:
    std::unique_ptr<juce::FileLogger> logger;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<velo::ScanWorker> scanWorker;
};

START_JUCE_APPLICATION (VeloApp)
