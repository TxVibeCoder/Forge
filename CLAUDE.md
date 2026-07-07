# Forge — Claude Code Working Agreement

Forge is a native, **Session-first DAW** on JUCE + Tracktion Engine (C++20, Windows/MSVC). This file
governs **how** we build it — tenets, principles, gotchas, and the multi-CLI wave process. The **what/why**
lives in the docs linked at the bottom.

Public, pseudonymous repo (`github.com/TxVibeCoder/Forge`, AGPLv3). Keep it that way — see *Public-repo hygiene*.

---

## Session start

Before working, read (in order): **`docs/HANDOFF.md`** (pick-up-cold state — what shipped, what's next,
gotchas, current `origin/main` tip) → **`docs/DIRECTION.md`** (the authoritative product brief; read before
planning any feature) → **`docs/STATUS.md`** (the living roadmap). If any is stale or ambiguous, **ask before
inferring the mission** — the devlogs are history, not session anchors.

**Document precedence:** DIRECTION governs *what Forge is*; HANDOFF governs *where we are*; STATUS governs
*what's next*; this file governs *how we work*; the maintainer's memory governs *cross-session facts*. If they
conflict, surface it.

---

## Collaboration tenets

- **Say "I don't know" rather than guess plausibly.** A confident fiction costs more untangling than an honest
  uncertainty.
- **The maintainer reads diffs for intent, not line-by-line correctness — so test, don't just compile.** Prove
  behavior (selftests, adversarial verification); don't assert it.
- **Ask before structural moves.** One-file fixes and targeted refactors: go. Architecture changes, file
  reorganizations, or edits spanning more than ~5 files / ~20 steps: confirm the approach first.
- **Explain *why*, not just *what*** — name the tradeoff and the rejected alternative.
- **"Weird" may be history, not a bug.** Forge's arrangement-first code became Session building blocks; check
  HANDOFF/devlogs before "fixing" an oddity.
- **Conservative when scope is ambiguous** — expanding is cheap; undoing an overreach isn't.

---

## Engineering principles

- **Log fallible seams (standing build principle).** Every fallible op either succeeds or leaves a diagnostic
  via `FORGE_LOG_*` (`src/core/Log.h`; full rule + checklist in **`docs/LOGGING.md`**). **Never** log on the
  audio/RT thread or in a per-tick poll/paint; autosave logs only on failure; user-facing failures also hit the
  status strip.
- **Shared utilities over duplication.** Route engine ops through seams (`ProjectSession`, `RecordController`,
  `PluginHost`, `EngineHelpers`); views make no raw `te::` calls they don't have to. New cross-cutting capability
  → a domain-agnostic helper + thin adapters, never copy-paste between call sites.
- **One canonical source per fact; no drift-prone mirrors.** Don't assert doc↔code parity that no build step
  enforces.
- **No unverified claims in docs.** Counts, paths, commit hashes, and statuses must be derivable from a canonical
  source (filesystem, git, selftest output) and verified before commit. (We paid this twice in stale
  commit-status this project.)
- **File hygiene.** No stray files in the repo root beyond the essentials; **use git, never `*.bak`/`*.old`
  copies**; descriptive names (no `utils.cpp`, `temp.h`).

---

## Build & verify (Forge-specific — load-bearing)

- **One integration build, one target:**
  `& "C:\Program Files\CMake\bin\cmake.exe" --build ".\build" --config Debug` (full path — winget doesn't refresh
  PATH in these shells).
- **Kill `Forge.exe` before building or runtime-testing** — a running exe → `LNK1168` and holds the WASAPI
  device: `Get-Process Forge | Stop-Process -Force`. Use a 45–90 s build timeout.
- **Selftest floor** (must pass after any change — THIRTY-SEVEN gates as of W20; W20 adds ONE new gate,
  `--selftest-stepclip` (the Step Clip drum-grid seam: create + auto-built grid + born-audible + cell-toggle +
  undo), for frontier Wave 10's Step Clips. Frontier Wave 9 (LFO modifiers) was SKIPPED — see the deferred
  recipe in `docs/wave-9-lfo-recipe.local.md`):
  `--selftest` (playback),
  `--selftest-record`, `--selftest-session`, `--selftest-midi`, `--selftest-midilearn`, `--selftest-midiinput`,
  `--selftest-controlsurface`, `--selftest-lufs`, `--selftest-automation`, `--selftest-sync`,
  `--selftest-livesync`, `--selftest-lcd`, `--selftest-menu`, `--selftest-tray`, `--selftest-popout`,
  `--selftest-undo`, `--selftest-taptempo`, `--selftest-slotdelete`, `--selftest-addtrack`, `--selftest-scene`,
  `--selftest-dragdrop`, `--selftest-sessionmixer`, `--selftest-demo`, `--selftest-sendarrange`,
  `--selftest-followaction`, `--selftest-launchmode`, `--selftest-duplicate`, `--selftest-slotmove`,
  `--selftest-quantise`, `--selftest-scenerename`, `--selftest-scenedelete`, `--selftest-scenereorder`,
  `--selftest-capture`, `--selftest-scenesend`, `--selftest-sessionmaster`, `--selftest-peakhold`,
  `--selftest-stepclip`;
  (⚠ new gate names that CONTAIN an existing name must be
  ordered longest-first in the ladders — `-sessionmixer`/`-sessionmaster` ⊃ `-session`, and `-scenerename`/
  `-scenedelete`/`-scenereorder`/`-scenesend` ⊃ `-scene`; verify the report's `mode=` line)
  `--screenshot` renders the 11-state matrix (incl. the window-level `shell_window`, the >16-scene
  `session_scenes`, and the W20 `session_stepgrid` drawer) to `%TEMP%\forge_shot_*.png`. Full contract:
  `tests/SELFTEST.md`. Reports →
  `%TEMP%\forge_phase0_selftest.log`. First clone: `git submodule update --init --recursive`.
- **In a multi-CLI wave, CLIs DO NOT build.** One build dir + a device lock means concurrent builds collide.
  Each CLI edits + self-reviews for compile-safety; the **orchestrator** owns `main.cpp` / `CMakeLists.txt` / the
  single integration build + the selftest floor at consolidation.

---

## Forge gotchas (hard-won)

- **Nested block comments corrupt declarations:** an inline `/*…*/` inside a `/**…*/` doc comment closes the doc
  comment early — the rest parses as garbage and can silently drop the following declaration. Never nest them.
- **SessionView threading (R1):** pads cache NO `te::ClipSlot*`/`Clip*`; re-resolve via the const `getClipSlot`
  every tick; message-thread only; teardown stops the 25 Hz timer FIRST, then clears columns/state.
- **MIDI beats are content-relative** (beat 0 = clip start); edit `getSequence()`, never `getSequenceLooped()`.
  Slot MIDI clips use the free `insertMIDIClip(owner, name, range)` (name **before** range), not the AudioTrack
  member overload.
- **MIDI slot recording is transport-driven, not launch-driven** — arm the slot's `itemID` + `transport.record()`;
  never `launchSlot` on the record path. Capture must be **slot-only** (disarm the track's MIDI target first) or
  notes double-capture to the arrangement.
- **`AudioTrack::playSlotClips` is the engine's per-track Session↔Arrange playback switch (W10)** — the arranger
  node is gated on `!playSlotClips`, the flag **latches TRUE** the moment a slot on that track plays, and
  **nothing in the engine's live path clears it**. Any op that wants a track's **arrange** clips audible must set
  it false (the engine's Session→Arrange handoff — it stops that track's still-playing slots). A clip that's
  visibly on the timeline can still be silent because of this.
- **A clip copied out of a `ClipSlot` carries slot-normalized state (W10)** — auto-tempo on, a full-length loop
  range, `start=0`. To place it on the linear timeline as a plain one-shot, `disableLooping()` +
  `setAutoTempo(false)`. **Never** `setLoopRangeBeats({})` to clear the loop — it re-asserts `setAutoTempo(true)`.
- **The FOLLOWACTIONS auto-plant footgun (W11)** — writing a clip's follow-action DURATION
  (`followActionBeats`/`followActionNumLoops` > 0) on an **empty** action list makes the engine auto-add a
  `currentGroupRoundRobin` action (`tracktion_Clip.cpp:524-545` → `tracktion_FollowActions.cpp:498`). Always set
  the action **type explicitly** after ensuring an action exists (and/or pre-create the action before writing a
  duration). `Clip::getFollowActions()` also **lazily creates** the FOLLOWACTIONS child — a const reader must
  guard on `state.getChildWithName(IDs::FOLLOWACTIONS).isValid()` before calling it, or a pure read dirties the
  edit.
- **The per-clip launch-quantise engine seam is a verbatim TYPO + INVERTED (W12).** The method is spelled
  `Clip::setUsesGlobalLaunchQuatisation(bool)` / `usesGlobalLaunchQuatisation()` — **"Quatisation", missing the
  `n`** — auto-correcting to "Quantisation" fails to compile / silently fails to override. Semantics are
  **inverted**: `setUsesGlobalLaunchQuatisation(false)` **ENABLES** the per-clip override (the clip's own
  `getLaunchQuantisation()->type`); `true` inherits the Edit-global. To set an override write **both** the flag
  (false) **and** the type — the resolver gates on the flag first, so a type-only write is a silent no-op. Unlike
  FOLLOWACTIONS, `Clip::getLaunchQuantisation()` builds over the clip's **existing** state node (not
  `getOrCreateChildWithName`) and its ctor only `referTo`s, so a const read does **not** dirty the edit — but a
  const inherit-check should still read only the flag and never call `getLaunchQuantisation()` (a needless
  C++-member build).
- **Inserting a clip into a ClipSlot RE-LOOPS a one-shot (W13).** `te::insertClipWithState(clipSlot, state)`
  re-imposes slot normalization (`tracktion_ClipOwner.cpp:372-381`): it sets a full-length loop on any
  freshly-inserted clip that reads `!isLooping()`. So duplicating / moving / copying a **one-shot** slot clip
  silently brings it back **looping** unless you re-assert `disableLooping()` on the returned clip AFTER the
  insert (capture `wasOneShot` before). Use `disableLooping()`, NOT `setLoopRangeBeats({})` (which re-asserts
  auto-tempo — the W5/W10 gotcha). A gate whose fixture clips are born looping will NOT catch this (adversarial
  QC did).
- **The engine's quantise grid is a fraction of a BEAT, not a note-value (W14).** A `te::QuantisationType`
  type-name like `"1/4"` means a quarter of a BEAT (`beatFraction == 0.25`), so a piano-roll grid of 0.25 beats
  maps to `"1/4"` — NOT `"1/16"` (which is 0.0625). Map by matching the fraction value, never the note-name, or
  notes snap 4× finer than the visible grid. `QuantisationType::roundBeatToNearest` already folds `setProportion`
  (the 0-100% strength) into the result (`orig + proportion*(snapped-orig)`) — do NOT hand-lerp. Use a LOCAL
  `QuantisationType`, never `clip.getQuantisation()` (the clip's persistent playback quantise).
- **Scene reorder has NO engine seam — and per-track slot trees can be UNEVEN (W15).** There is no
  `SceneList::moveScene`; reorder is a raw `ValueTree::moveChild(from, to, um)` on the public `SceneList::state`
  AND every track's `ClipSlotList::state` (both `ValueTreeObjectList`s that auto-resync on a child move), all in
  ONE transaction. The footgun: a freshly `appendAudioTrack`'d track materialises slots only on demand (up to the
  touched row via `ensureNumberOfSlots(sceneIndex+1)`), so it can hold a **filled** slot yet have **fewer total
  slots than the scene count**. Moving/deleting must NOT skip a short track (that desyncs its clip from the moved
  scene and PERSISTS to disk) — **pad every track to `getNumScenes()` first** (`ensureNumberOfSlots`, a no-op for
  full tracks), then move/delete in lockstep. Rename (`Scene::name`, a UM-bound `CachedValue`), delete
  (`SceneList::deleteScene` — scene row + `clip->removeFromParent()` + `deleteSlot`, all UM-bound) and reorder all
  ride the user undo stack; the shell seals one transaction per gesture via `onEditMutated`. And a scene mutation
  MUST `rebuild()` — the 25 Hz poll watches TRACK count only, never scene count — but DEFER the rebuild
  (`callAsync` + `SafePointer`): a rename commit fires from inside the row's own `TextEditor` callback and
  `rebuild()` would delete that row mid-stack (a UAF).
- **`FourOscPlugin`'s mod-matrix flush unconditionally wipes the redo stack on EVERY save — confirmed engine
  defect, not fixed (W16).** `FourOscPlugin::flushPluginStateToValueTree()`
  (`libs/tracktion_engine/.../tracktion_FourOscPlugin.cpp:1393-1424`) calls `state.addChild(mm, -1, um)`
  **unconditionally** on every flush — i.e. every `session.save()`, which the shell's `doUndo()`/`doRedo()`
  **always** call — even when the mod-matrix is completely **empty** (source-traced: `modMatrixChildren=0` still
  triggers it). `ValueTree::addChild` has **no equality gate** (unlike `setProperty`, which skips a no-op write),
  so this is a genuine new `UndoManager` action on every save, which (a) discards the pending redo stack and
  (b) becomes a new top-of-stack entry ahead of whatever the real undo/redo just did — so the **next** Ctrl+Z
  undoes this phantom action, not the user's actual next target. Reachable on **any** edit containing a
  `FourOscPlugin` — Forge's own default instrument, auto-created on every MIDI track by `createMidiClipInSlot`,
  i.e. nearly every real Forge project. **Practical impact: Redo is unavailable immediately after any Undo**, and
  a second Ctrl+Z can silently consume the phantom instead of the intended prior step. **Not fixed** — the fix
  lives in vendored `libs/tracktion_engine` (a plugin-specific override most Forge waves never touch), and per
  the project's standing "do not fork the engine" default this is a **maintainer decision**, not something to
  patch unilaterally. Any gate/feature that asserts `um.canUndo()`/`um.canRedo()` immediately after a real
  `doUndo()`/`doRedo()` **will be unreliable** — assert on **content-level** state instead (slot occupancy,
  marker/note counts), exactly as `--selftest-undo`'s `shellUndoRevertsOnlyMarker` and `--selftest-popout`'s
  `undoFiredThroughPopoutKey`/`redoFiredThroughPopoutKey` do. `--selftest-undo`'s `redoAvailableAfterSingleUndo`
  field monitors this defect (informational, non-gating — logs `FORGE_LOG_WARN` when false) so a future engine
  fix would show up as a signal, not silently.
- **A view that stays bound to a clip after the user moves on must not let an UNRELATED undo/redo silently
  reset its selection or scroll (W16).** `PianoRollView` stays bound to whatever MIDI clip was last opened even
  after the user switches to editing something else — so the shell's app-wide `undoOrRedo()` fan-out (which
  must resync the roll after a note-content edit so it never dangles a stale `te::MidiNote&`, see the dim-5
  history below) can fire on a completely unrelated gesture (e.g. a Session scene rename) while a clip still
  happens to be bound. The naive fix — re-call `setMidiClip()` unconditionally — rebuilds unconditionally
  (`rebuildNotes()` always clears `selection` and `scrollToClipPitchRange()` always resnaps the viewport), so
  it silently wiped a multi-note selection and reset manual scroll on **every** unrelated undo/redo (caught by
  adversarial QC, not the original build). Fixed via `PianoRollView::refreshAfterExternalEdit()`: compare the
  bound clip's *live* note set (by `te::MidiNote*` identity, since a structural add/remove destroys-and-recreates
  the object while a move/resize mutates it in place) against what's currently displayed — only a genuine
  structural divergence calls the destructive `rebuildNotes()`; anything else (including "nothing changed") calls
  the cheap, non-destructive `layoutNotes()` (repositions existing components, touches neither selection nor
  scroll). The general lesson: a fan-out step reacting to "something, somewhere, changed" must diff against what
  it actually owns before doing anything destructive — "safe to call repeatedly" (a doc-comment claim) is not
  the same as "safe to call for an unrelated reason."
- **Never arm recording synchronously in one blocking callback** — yield for the async device-list rebuild
  (`rescanMidiDeviceList` for MIDI, `dispatchPendingUpdates` for wave) before arming/checking.
- **A capture/accumulation seam that spans real time must resolve by IDENTITY at commit, never by re-reading
  the cell it started from (W17 — a QC-caught confirmed defect, fixed before ship).** Wave 7's performance
  capture samples `LaunchHandle::getPlayedRange()` over a live performance and, at commit, stamps one clip per
  captured span onto the Arrangement. The naive resolve — `getClipSlot(track,scene)->getClip()` at commit
  time — reads "whatever occupies that cell NOW", which silently diverges from "whatever occupied it WHEN the
  span was captured" the instant a user clears/replaces a clip in that slot mid-capture-session (an ordinary
  jam move — swapping a loop variation while other tracks keep playing). The fix:
  `ProjectSession::sealCaptureSpan`/`performanceCaptureTick` capture the clip's `te::EditItemID` at
  span-**OPEN** time (from the freshly-verified-non-null clip, never re-derived at seal or commit), and
  `stopPerformanceCapture` resolves the source at commit via `te::findClipForID(*edit, span.clipID)` — an
  edit-wide identity lookup that still finds a since-MOVED clip and cleanly degrades (a logged skip, never a
  silent wrong-content stamp) if the clip was genuinely deleted. **General lesson: any seam that captures state
  now but acts on it later (spanning a live-editable window) must key its later action on a stable identity
  (`EditItemID`), never on a positional/index resolve that can silently point at different content by the time
  the action runs.**
- **Viewport scroll:** the viewed component's top-left position *is* the scroll offset — size it with `setSize`,
  never `setBounds(0,0,…)` (which yanks the scroll to the top on any relayout).
- **JUCE lock types:** `juce::CriticalSection::ScopedLockType` / `ScopedUnlockType` (the bare `ScopedLock` is the
  global alias, not a member of `CriticalSection`).
- **PowerShell cwd drifts after a Bash `cd`** — use the absolute `build` path with cmake.

---

## Public-repo hygiene (NON-NEGOTIABLE)

Forge is a **public, pseudonymous** repo — everything committed is world-readable, so the identity of the
maintainer and their machine must never leak. **Sanitize before every push.**

**Author identity.** Commits use the pseudonymous `TxVibeCoder` GitHub-noreply address — never a real name or
personal email. Verify before pushing: `git config user.name` → `TxVibeCoder`, `git config user.email` →
`TxVibeCoder@users.noreply.github.com`. Commit trailer on every commit:
`Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.

**Scrub categories — what must never appear in a committed file:** the maintainer's real name, real email, or
employer; any personal `C:\Users\…` (or other machine-local) absolute path; the names of the maintainer's *other*
private projects/repos; API keys or credentials of any kind.

**Pre-push scan (run on every push).** Grep the tracked set for those categories — ripgrep respects `.gitignore`,
so it skips the local-only files by design:

```
rg -n --glob '!libs/**' 'C:\\Users\\|<real-name>|<real-email>|<employer>|<private-repo-names>'
```

The literal denylist values (real name / email / employer / private-repo names) are **deliberately kept out of
this doc** — they live in the maintainer's local, gitignored scrub list / memory, so the scrub instructions are
themselves scrub-safe. A clean scan matches **only** gitignored files (`*.local.md`, `*.log`); **any tracked-file
hit is a blocker** — fix before pushing. For a large or agent-authored diff, add a quick semantic pass (a reviewer
skimming for subtler leaks: a stray path in a comment, a real name in prose, a private project referenced in a
devlog).

**Local-only escape valves (never pushed):** runtime logs live under `%APPDATA%\Forge\logs` and are `.gitignore`d
(`*.log`, `forge.log*`); working notes that reference private context use the `*.local.md` convention
(gitignored) — e.g. the side-project assessment lives in a `*.local.md` so it stays off the public repo.

**If a leak already reached history** (not just the working tree): a new commit cannot unpublish it — rewrite
history with `python -m git_filter_repo` (the `git-filter-repo` shim isn't on PATH here, and it drops the
`origin` remote as a safety step, so **re-add `origin` before pushing**), then force-push. Done once this project
to scrub a stray absolute path from all history. Prefer catching leaks **pre-commit** — history rewrites are
disruptive and require a force-push.

---

## Wave Orchestration Rule (multi-CLI parallel builds)

When 2+ Claude Code CLIs work concurrently on **file-disjoint** territories, the orchestrator runs this pattern.
**Single-CLI sessions skip it entirely.**

**No auto-sync, no sentinel.** Forge has no auto-commit timer, so there is no `.wave-active` sentinel and no
liveness health check — the machinery that pauses `AUTO_SYNC.bat` on other projects has nothing to pause here.
The guard that *does* matter is **scoped commits** (pillar 3).

**Pillar 1 — Territory allocation (file-disjoint).** Each CLI owns a file/dir for the wave. Max 2 CLIs per file,
non-overlapping regions only; 3-way same-file collisions surface to the orchestrator **before** launch (one
defers). When several changes land in one file, run them as a **serial spine** (one continuing CLI, or
back-to-back CLIs — each started only **after the previous is committed**, never concurrently), with genuinely
disjoint prompts running in **parallel** alongside. **Verify-before-implement:** grep the target first — a
roadmap item already done → mark `SKIPPED-STALE`, don't redo it. **Re-Read before Edit** — distrust a Read from
earlier in the wave; a neighbor may have committed over it.

**Pillar 2 — Per-CLI handoff packets.** A per-wave directory `wave-<N>-cli-prompts/`:
- `README.md` — orchestrator control doc: thesis, baseline commit hash, the selftest floor, a **run-structure**
  section (serial-spine vs. parallel track table), the prompt table (*prompt · item · territory · track·order ·
  risk · effort*), any embedded product-decision calls **with a default baked in** (so no CLI blocks on the
  maintainer), an explicit **out-of-scope** list, and the **consolidation** steps.
- one self-contained `P#-<slug>.md` per prompt — paste the whole file into a fresh CLI. Each carries: shared
  **Context** (project + path, session-start reads, mission, **"take-as-given" facts** so the CLI doesn't
  re-litigate proven results), **YOUR TERRITORY** (exact files) + **NOT yours** (shared files — *propose the
  change, don't edit it; the orchestrator applies the one-liner*), the **build rule** (do NOT build; self-review
  for compile-safety; orchestrator owns the build), **non-negotiables** (Forge principles + scoped commits + no
  push + **hard-stop-and-flag** on `main.cpp`/`CMakeLists.txt`/shared views), the **change** (phased, with
  STOP-and-propose guards on risky phases), **acceptance criteria**, and the **scoped commit** line. On finish
  each CLI writes `P#-results.md` and reports a one-line status.
- Naming: `P#` (partition) when prompts→CLIs isn't 1:1 (a serial spine may be one CLI running several prompts);
  `CLI-N` when it is 1:1.

**Pillar 3 — Scoped commits (the load-bearing guard).** N CLIs commit in ONE shared working tree, so a bare
`git add -A` scoops a neighbor's unstaged files. Every CLI scopes **both** the add and the commit:
`git add -- <its-paths>` **and** `git commit -m "[P# W<N>] <ticket> — <summary>" -- <its-paths>`. No push — the
orchestrator decides push timing at consolidation.

**Orchestrator runbook.** *Start:* write the README + packets (disjoint territories, max 2/file), record the
baseline hash + selftest floor, launch the CLIs. *Runs:* CLIs verify → edit → self-review → scoped-commit →
`P#-results.md` → one-line status. *Consolidation* (after all report DONE/ABSTAIN/SKIPPED): apply the flagged
`main.cpp`/CMake/seam wiring, run the **single integration build + the selftest floor** against the baseline,
fix regressions, then the consolidation commit (docs/devlog), **sanitize**, and push.

---

*Rules, tenets, and gotchas live here. Product brief → `docs/DIRECTION.md` · state → `docs/HANDOFF.md` · roadmap
→ `docs/STATUS.md` · logging → `docs/LOGGING.md` · architecture → `docs/ARCHITECTURE.md`.*
