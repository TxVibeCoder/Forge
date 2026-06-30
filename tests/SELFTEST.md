# Forge self-test contract

Forge drives itself headless for CI-free verification. Both modes write a key=value report
to `%TEMP%\forge_phase0_selftest.log` then quit. `result=PASS|FAIL` is the bottom line.

## `Forge --selftest` (playback / arrange path)

Imports a generated 440 Hz tone onto track 1, plays it, then samples state at ~3s.

| field | meaning | PASS requires |
|---|---|---|
| `device` | current audio output device name | non-empty (device open) |
| `sampleRate` / `bufferSize` | device config | — |
| `editFile` | project path on disk | — |
| `editLoaded` | 1 if an existing project was loaded (vs newly created) | — |
| `numTracks` | audio track count | ≥ 1 |
| `importedClip` | 1 if the tone imported as a clip | 1 |
| `clipLengthSecs` | imported clip length | > 0 |
| `numClipComponents` | clip rects built on track 1 | ≥ 1 |
| `playheadX` | playhead pixel x for the current position | — (informational) |
| `transportReadout` | 1 if the transport bar readout has text | — (informational) |
| `hasContext` | 1 if the playback context is allocated | — |
| `playing` / `position` | transport state | `playing==1` OR `position>0.05` |

## `Forge --selftest-record` (recording path)

**Event-driven** (mirrors the real arm path, which must NOT block the message loop): phase 1 opens
the capture input lazily (`ensureRecordingInputOpen`) and yields; phase 2 (≈200 ms later, after
Tracktion's async wave-input-list rebuild has delivered) arms track 1 + records; phase 3 (≈1.5 s
later) stops keeping the take, then verifies a clip on disk. Doing this synchronously in one callback
silently starves the async rebuild (`getNumWaveInDevices()` stays 0) — that was the old harness's
false FAIL; see `docs/devlog/device-recording.md`.

| field | meaning | PASS requires |
|---|---|---|
| `availableInputDevices` | OS-visible capture endpoint names | — (diagnostic) |
| `openInputError` / `deviceAfterOpen` / `inputChansAfterOpen` | lazy-open diagnostics | — |
| `inputDeviceCount` | engine wave-in devices | > 0 |
| `trackArmed` | input assigned + armed to track 1 | 1 |
| `recordingStarted` | transport entered record | 1 |
| `recordedClipCount` | clips on track 1 after stop | ≥ 1 |
| `recordedFileExists` | the take's source file exists | 1 |
| `recordedClipLengthSecs` | take length | > 0 |
| `recordedPeakMagnitude` | peak |sample| of the captured file | — (informational; > 0 ⇒ real signal flowed) |
| `recordError` | last setTarget/arm error, if any | empty |

> Note: on a machine whose active audio config exposes no capture endpoint at all (e.g. a Bluetooth
> A2DP output headset with no mic), `inputDeviceCount=0` and this test FAILs — that is an environment
> limitation, not a Forge bug. Select an input via the **Audio** dialog. On a box with a working input
> this test PASSes and captures a real take (verified 2026-06-30: clip + file + length + non-zero peak).
