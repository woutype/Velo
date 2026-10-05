# Audit and changes

## Weak points found in 1.0 -> fixed
| Problem | Fix |
|---|---|
| Plugins scanned in-process: one bad VST3 could kill the app, slow, full rescan every time | Out-of-process scanner (helper = same exe, `--velo-scan`), up to 3 parallel workers, 60 s timeout, bad plugins blacklisted, only changed files re-probed, atomic cache writes, Stop button |
| Hard-coded `E:\Installed Program` and VST3 paths | Standard VST3 locations from JUCE + user-managed folders (Plugins > ... > Add plugin folder); the old E: path is migrated on first run only if it exists |
| Plugin search: plain substring, no ranking, window deleted itself inside its own callback | Ranked fuzzy search (prefix > word start > substring > "ProQ" key > fuzzy), multi-word AND, Effects/Instruments filter, most-used first, keyboard control |
| Plugin loading blocked the GUI without feedback, errors ignored, no crash tracking | Async load with "Loading" state, error toasts, crash marker: a plugin that killed Velo is disabled on next start |
| Audio thread: blocking lock, mono copied twice, no NaN protection, fixed FIFO, no clock-drift handling | Try-lock (pass-through while the chain is edited), denormal guard, NaN/Inf muting, clipping guard, lock-free ring + PI-controlled Lagrange resampler for the cable, dropout/overflow counters |
| Slot indices used as ids | Stable slot ids, drag-and-drop reordering |
| No persistence | Devices, channels, buffers, gains, switches, whole FX chain (with plugin state) restored; autosave each minute |
| Plain dialogs, no logs | Toasts in the UI, log file in `%APPDATA%/Velo`, crash stack trace, unhandled-exception hook |

## New stack (3.0)
JUCE's own widgets cannot do real blur, easing or modern typography. The interface is now web tech inside WebView2, in the style of your two reference images: dark navy glass cards (backdrop blur), pill buttons, iOS switches, sliding segmented control, glass slider thumbs, level meters with peak hold. The audio path stays 100 % C++; the UI only exchanges small JSON events with it (no audio crosses the bridge).

## Signature element: live signal path
The top of the Live page draws the real signal as voice-bars travelling along wires: microphone -> effects -> headphones / virtual cable. Each wire shows the live meter history of its own stage (blue = mic, violet = headphones, teal = cable, amber = hot peaks), nodes show gain, active effects and the delay of each output, and a muted mic or an unused output visibly goes quiet. Colours match the latency cards below.

## Latency and buffers
* Headphones card: input + output driver latency + FX chain delay, as a stacked bar. Estimated values are labelled.
* Virtual cable card: microphone + FX + sync queue + cable device latency, as a stacked bar.
* Headphone buffer: **Control Center** button opens the MiniFuse panel through the ASIO driver (Velo then adopts the driver's setting); the buffer chips also offer the sizes the driver reports.
* Virtual cable buffer: chosen inside Velo (Devices > Virtual cable).

## Bridge protocol (ui <-> C++)
JS -> C++ `cmd` events: ready, switch, gain, iface, channels, hpBuffer, panel, defaults, rescanDevices, virtual, scan, scanStop, addFolder, removeFolder, addSlot, removeSlot, moveSlot, bypass, editor, load.
C++ -> JS events: settings, devices, chain, plugins, scan, stats (4 Hz), meters (30 Hz), toast.

## Honest limits
* Not compiled here (no Windows/JUCE toolchain in my environment). The UI was rendered and tested in a headless browser against a mock backend; the C++ side may need small fixes on the first build.
* WebView2 CMake/NuGet integration (`NEEDS_WEBVIEW2`) needs internet at configure time.
* A virtual cable driver (VB-Cable etc.) must be installed; Velo cannot create the driver.
* Windows shared mode adds its own ~10-30 ms; use an exclusive-mode path for the lowest cable latency.
