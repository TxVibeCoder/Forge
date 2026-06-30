# Device init + recording robustness (RecordController, EngineHelpers)

Area: audio-device initialisation for recording, RecordController robustness, and the
hardware-free record self-test. Owner files: `src/engine/EngineHelpers.h`,
`src/engine/RecordController.h`, `src/engine/RecordController.cpp`.

> **Update — startup-latency hardening (2026-06-30, supersedes the startup model below).**
> `initialiseAudioForRecording()` was **removed**. Startup is now **output-only**: the engine is
> constructed with a `ForgeEngineBehaviour` whose `shouldOpenAudioInputByDefault()` returns false,
> so no capture device is opened on the message thread at launch (that open could stall 25–77 s when
> the default device changed). The input-fix logic documented here — detect an empty input name →
> pick the type's default capture device → re-apply preserving the output with
> `treatAsChosenDevice=false` — now lives in **`EngineHelpers::ensureRecordingInputOpen()`**, called
> lazily on the first arm/record (`onArmToggled` / `toggleRecordTake` / `beginSelfTestRecording`),
> with two fixes from the startup-latency review: it requests **explicit** input channels (the
> 0-input startup would otherwise open the mic with an empty channel mask → silent capture), and it
> **restores the working output-only setup if the combined in+out open fails** (so a failed arm can't
> kill playback). The root-cause analysis (§"Root-cause analysis") and the "Verifying a real take"
> steps below remain valid. Full writeup: `integration.md` → Wave 5.

## What changed

### `EngineHelpers.h` (additive only — all existing inline fns untouched)
- **`initialiseAudioForRecording (te::Engine&)`** — the contract function `main.cpp` now calls
  in `MainComponent`'s ctor (it already calls it at `src/main.cpp:151`). Replaces the old
  `engine.getDeviceManager().initialise()` call. Opens wave INPUT channels for recording while
  preserving a previously-saved OUTPUT device.
- **`getAvailableWaveInputDeviceNames (te::Engine&) -> juce::StringArray`** — diagnostic helper.
  Queries the JUCE driver layer (`AudioIODeviceType::getDeviceNames(true)`) for OS-visible
  capture endpoints, independent of what is currently open.
- **`installSyntheticInputForSelftest (te::Engine&, sampleRate, blockSize, numIn, numOut)
  -> te::HostedAudioDeviceInterface*`** — hardware-free synthetic-input path for
  `--selftest-record`. Switches the engine onto a `HostedAudioDeviceInterface` so it exposes
  virtual wave inputs the test can feed. Returns the interface to pump, or nullptr on refusal.

### `RecordController.h` / `.cpp`
- `enableInputs()` now `rescanWaveDeviceList()` first and `dispatchPendingUpdates()` after, so
  the (async) enable/stereo/monitor changes are flushed before a subsequent `record()`.
- `getInputDeviceCount()` rescans before counting.
- New `getAvailableInputDeviceNames() const -> juce::StringArray` returns the engine-level
  wave-in device names (post-rescan) for logging/UI.
- `armFirstInputToTrack()` rewritten for robust enumeration + precise errors:
  - Early-outs with a specific message when `getNumWaveInDevices() == 0`, distinguishing
    "no input device exists at all" from "a device exists but no input channels are open".
  - Skips disabled instances with a named reason; aggregates `setTarget` errors; reports how
    many wave instances were seen if none could be armed.
  - Signature unchanged: `bool armFirstInputToTrack (te::Edit&, te::AudioTrack&)` (main.cpp
    call sites at `src/main.cpp:396` and `:422` keep compiling).
- `RecordController.cpp` now also `#include "engine/EngineHelpers.h"` (needed for the
  diagnostic name lookup in the error path).

## Design decisions
- **Preserve output via the engine itself, fix only the input.** `te::DeviceManager::initialise()`
  already restores the saved `audio_device_setup` XML (`loadSettings()` ->
  `juce::AudioDeviceManager::initialise(..., audioXml.get(), true)`), so the saved OUTPUT is
  preserved when XML exists. We therefore do NOT pre-capture/forcibly re-pick the output; we
  only add an input if one is missing, and re-apply with `treatAsChosenDevice=false` so we never
  rewrite the stored setup. This minimises the blast radius and keeps the user's output choice.
- **`treatAsChosenDevice=false`** on the input re-apply: the auto-selected default input is a
  convenience, not the user's explicit choice. This means `createStateXml()` won't persist it,
  so the saved output is never overwritten by our auto-input, and the user's next explicit pick
  in the Audio dialog wins.
- **Synthetic input uses `HostedAudioDeviceInterface`, not WASAPI loopback.** Loopback would
  require registering a custom device type via an `EngineBehaviour` override (touches files we
  don't own and changes global device enumeration). The hosted interface is self-contained,
  deterministic, and the engine's documented testing hook (its `Parameters` even has
  `inputLatencyNumSamples` "For testing only").

## Root-cause analysis: `getNumWaveInDevices() == 0` even with a mic present
The decisive chain (file:line references are in the vendored engine):
1. Tracktion builds its wave-in list in `DeviceManager::AvailableWaveDeviceList`
   (`playback/tracktion_DeviceManager.cpp:91`). For the non-"description" path it calls
   `describeStandardDevices(... isInput=true)` which reads
   `device.getInputChannelNames()` of `deviceManager.getCurrentAudioDevice()`
   (`tracktion_DeviceManager.cpp:114`). **The wave-in count is derived purely from the ACTIVE
   input channels of the currently-open `juce::AudioIODevice`.** No open input channels => zero
   wave inputs, regardless of whether a mic exists in the OS.
2. `getCurrentAudioDevice()` is opened by `loadSettings()`
   (`tracktion_DeviceManager.cpp:911`). With saved XML it calls
   `juce::AudioDeviceManager::initialise(ins, outs, audioXml, true)` ->
   `initialiseFromXML()` (`juce_AudioDeviceManager.cpp:403`). At line 423 the input device name
   is taken verbatim from the XML attribute `audioInputDeviceName`; if the user never explicitly
   chose an input, **that attribute is empty**.
3. `initialiseFromXML` then calls `setAudioDeviceSetup(setup, true)`
   (`juce_AudioDeviceManager.cpp:446`). Unlike `setCurrentAudioDeviceType`
   (`:687`), `setAudioDeviceSetup` does **not** call `insertDefaultDeviceNames`, so an empty
   `inputDeviceName` stays empty. `type->createDevice(outputName, inputName="")`
   (`:774`) opens the device **output-only**.
4. `updateSetupChannels` (`:711`) clears the input channels when the input name is empty
   (`:715-717`). So `getActiveInputChannels()` is empty, `getInputChannelNames()` is empty, and
   step 1 yields zero wave inputs.

So the symptom is not "the mic is invisible to Windows" — it is "the saved/opened device has no
input device name, so JUCE opened it output-only." `initialiseAudioForRecording()` fixes this by
detecting the empty input name and selecting the type's default capture device
(`AudioIODeviceType::getDefaultDeviceIndex(true)` / `getDeviceNames(true)`), then re-opening with
`useDefaultInputChannels=true` while keeping the restored output name.

Secondary contributors worth knowing:
- **Default channels at init.** `main.cpp` requests 512/512, so input channels ARE requested;
  the problem is the empty input *name*, not the requested channel count. (`insertDefaultDeviceNames`
  only fills a default input name when `numInputChansNeeded > 0`, and only on the
  `initialiseDefault`/`setCurrentAudioDeviceType` paths — never on the XML-restore path.)
- **JUCE current-device-type input pairing.** On Windows the default type is WASAPI (shared).
  Input and output can be different physical devices; the open device's input-name being empty is
  what zeroes the inputs. Switching device type via the Audio dialog goes through
  `setCurrentAudioDeviceType` which DOES call `insertDefaultDeviceNames`, which is why picking a
  type/device in-dialog tends to "fix" inputs — exactly what we now do programmatically at startup.
- **Windows microphone privacy.** If Settings > Privacy & security > Microphone denies desktop-app
  access, WASAPI/DirectSound still *enumerate* the endpoint (so `getDeviceNames(true)` is
  non-empty) but opening/reading yields silence or an open failure. In that case our
  `setAudioDeviceSetup` may return an error (which we tolerate) and inputs stay at 0. This is an
  OS setting we cannot change from code; it must be granted by the user.

## Verifying a real take once an input is chosen via the in-app Audio dialog
1. Launch the app normally. Open the Audio Settings dialog (Control Bar -> Audio Settings;
   `EngineHelpers::showAudioDeviceSettings`). Under "Active input channels", select your mic and
   tick at least one input channel. Confirm the input meter responds to sound. Close the dialog.
2. The status strip should show the device is open. (Optionally log
   `RecordController::getAvailableInputDeviceNames()` / `getInputDeviceCount()` — they should be
   >= 1 now.)
3. Arm + record: the existing `toggleRecordTake()` path (main.cpp) calls `enableInputs()` then
   `armFirstInputToTrack(edit, track0)` then `transport.record(false)`. Speak/play for ~2s.
4. Stop (`transport.stop(false,false)`), then `session.save()`.
5. Verify a take exists: track 0 should contain a new `te::WaveAudioClip` whose
   `getSourceFileReference().getFile()` exists on disk and whose
   `getEditTimeRange().getLength().inSeconds() > 0` (this is exactly the pass criteria
   `finishSelfTestRecording()` already checks in main.cpp:438-453).
6. Play it back to confirm the captured audio is audible.

## Unfinished (with why)
- **Synthetic-input self-test is not yet WIRED.** `installSyntheticInputForSelftest()` exists and
  is correct, but driving it requires a `processBlock()` loop, which lives in `main.cpp`
  (not an owned file). Until main.cpp drives it, `--selftest-record` still depends on real
  hardware and will FAIL on a box with no open input. See "Integration required".
- **No automatic Windows-privacy detection.** We surface a hint in the error string but cannot
  query/instruct the OS permission from here without extra platform code (out of scope + would
  need a new .cpp / CMake edit).
- **First-ever-run output is still the OS default.** With no saved XML there is nothing to
  restore, so the output is whatever Windows defaults to. By design (documented tradeoff).

## Integration required
1. **Wire the synthetic-input self-test (in `src/main.cpp`, owned by the shell agent).**
   In `MainComponent`'s ctor, for `mode == SelfTest::record`, BEFORE `session.openOrCreate(...)`
   and instead of (or right after) `EngineHelpers::initialiseAudioForRecording(engine)`, call:
   ```cpp
   te::HostedAudioDeviceInterface* synthInput = nullptr;
   if (mode == SelfTest::record)
       synthInput = EngineHelpers::installSyntheticInputForSelftest (engine, 44100.0, 512, 1, 2);
   else
       EngineHelpers::initialiseAudioForRecording (engine);
   ```
   Then DRIVE it after `transport.record(false)` so the engine receives samples. The hosted
   interface is pull/push: the host must call `processBlock` repeatedly. Add a small driver, e.g.
   a `juce::Thread` (or a `HighResolutionTimer`) that for ~1 second does:
   ```cpp
   const int bs = 512; double sr = 44100.0; double phase = 0.0;
   juce::AudioBuffer<float> buf (2 /*max(in,out)*/, bs);
   juce::MidiBuffer midi;
   // per block:
   buf.clear(); midi.clear();
   auto* in = buf.getWritePointer (0);
   const double dphi = juce::MathConstants<double>::twoPi * 440.0 / sr;
   for (int i = 0; i < bs; ++i) { in[i] = 0.2f * (float) std::sin (phase); phase += dphi; }
   synthInput->processBlock (buf, midi);   // engine reads input ch0, writes outputs
   // sleep ~ bs/sr seconds between blocks (≈11.6 ms)
   ```
   Keep this driver ONLY in the record-selftest branch; do NOT install the hosted interface for
   `SelfTest::none` or `SelfTest::playback` (they need real output hardware — installing it would
   regress the playback selftest). `installSyntheticInputForSelftest` already enables the wave
   inputs, so `recorder.enableInputs()` + `armFirstInputToTrack(...)` + `transport.record(false)`
   work unchanged; `rcInputDeviceCount` will be >= 1 and the existing pass criteria hold with no
   microphone.
2. **(Already done) main.cpp ctor calls `EngineHelpers::initialiseAudioForRecording(engine)`**
   at `src/main.cpp:151`. No further change needed for the normal/playback paths.
3. **Optional UI hook:** the Audio Settings dialog / status strip can call
   `RecordController::getAvailableInputDeviceNames()` or
   `EngineHelpers::getAvailableWaveInputDeviceNames(engine)` to show why recording is unavailable.

## Risks to verify at build time
- `te::HostedAudioDeviceInterface` and `te::HostedAudioDeviceInterface::Parameters` must be
  visible through `<JuceHeader.h>`. They are declared in
  `modules/tracktion_engine/playback/tracktion_HostedAudioDevice.h`, part of the engine module,
  so `JuceHeader.h` pulls them in. If a forward-decl issue appears, that header is the include.
- `te::DeviceManager::defaultNumChannelsToOpen` is a public `static constexpr int`
  (`tracktion_DeviceManager.h:33`) — used unqualified-through-`te::DeviceManager::`. Confirm it
  resolves (no ODR/linkage surprise; constexpr is fine inline).
- `juce::AudioDeviceManager::getCurrentDeviceTypeObject()`, `getAudioDeviceSetup()`,
  `setAudioDeviceSetup(setup, false)` are all public (verified in `juce_AudioDeviceManager.h`).
  `AudioIODeviceType::getDeviceNames(bool)`, `getDefaultDeviceIndex(bool)`, `scanForDevices()`
  are public pure-virtuals (`juce_AudioIODeviceType.h:89/98/107`).
- `setAudioDeviceSetup` returning a non-empty error is tolerated via `juce::ignoreUnused`. Verify
  no `[[nodiscard]]`-related warning-as-error (it returns `String`, not nodiscard).
- `RecordController.cpp` including `engine/EngineHelpers.h` pulls a header-only file into a 2nd
  TU; all functions are `inline`, so no duplicate-symbol risk. Confirm the include path
  `engine/EngineHelpers.h` matches the existing `engine/RecordController.h` include style (it does).
- `edit.getAllInputDevices()` returns `juce::Array<InputDeviceInstance*>`; iterating with
  `for (auto* instance : ...)` is the same pattern as the original code.
