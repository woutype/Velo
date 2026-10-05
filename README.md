# Velo 3.0

Live VST3 monitor + virtual-cable streamer for Windows.
**Audio engine: C++ / JUCE 8. Interface: HTML + CSS + JS in an embedded WebView2 (Chromium) control.**

    mic (ASIO, MiniFuse) -> gain -> VST3 chain -> headphones     (buffer: MiniFuse Control Center, or Velo)
                                              \-> lock-free ring -> drift-correcting resampler -> virtual cable (buffer: Velo)

## Build (Windows 10/11, Visual Studio 2022, CMake 3.22+, internet on first configure)
    cmake -B build -G "Visual Studio 17 2022"
    cmake --build build --config Release
The first configure downloads JUCE, the ASIO SDK and the WebView2 SDK (NuGet).
`-DVELO_ENABLE_ASIO=OFF` builds without ASIO. The Microsoft Edge WebView2 Runtime must be installed (it ships with Windows 11 and current Windows 10).

## Project layout
    Source/   C++ engine: AudioEngine, PluginManager, PluginScanner (out-of-process), MainComponent (WebView bridge)
    ui/       the whole interface: index.html, app.css, app.js  (embedded into the exe at build time)
    ui/dev/   mock.js - open ui/index.html in Chrome/Edge to design the UI without building the C++ app

## Editing the design
Change `ui/app.css` / `ui/index.html` / `ui/app.js`, rebuild (or just refresh `ui/index.html` in a browser with the mock backend).
See docs/CHANGES.md for the audit and the bridge protocol.
