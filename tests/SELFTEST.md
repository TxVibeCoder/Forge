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

Arms track 1 to the first wave input, records ~1s, stops (keeping the take), verifies a clip.

| field | meaning | PASS requires |
|---|---|---|
| `availableInputDevices` / `currentDeviceInputChans` / `deviceTypesInputs` | device diagnostics | — |
| `inputDeviceCount` | engine wave-in devices | > 0 |
| `trackArmed` | input assigned + armed to track 1 | 1 |
| `recordingStarted` | transport entered record | 1 |
| `recordedClipCount` | clips on track 1 after stop | ≥ 1 |
| `recordedFileExists` | the take's source file exists | 1 |
| `recordedClipLengthSecs` | take length | > 0 |
| `recordError` | last setTarget/arm error, if any | empty |

> Note: on a machine whose active audio config exposes no input (e.g. a Bluetooth A2DP
> output headset), `inputDeviceCount=0` and this test FAILs by design — that is an
> environment limitation, not a Forge bug. Select an input via the **Audio** dialog.
