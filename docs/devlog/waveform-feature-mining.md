# Waveform User Guide → Forge — Feature Mining

*Mined 2026-07-02 from the **Waveform User Guide** (Tracktion Corporation, ~397 pp.). Organized around the
maintainer's stated driver — **UX, navigation, and workflow first** — with capability features condensed at the
end. Not a spec; a candidate-feature backlog to feed [DIRECTION.md](../DIRECTION.md) / [STATUS.md](../STATUS.md).*

> The guide PDF itself is a copyrighted third-party doc and **must not be committed** (public-repo hygiene). It
> should live outside the tree or be `.gitignore`d. This mining note carries no verbatim copyrighted text — only
> feature descriptions + page pointers.

---

## Why this source is unusually actionable

**Waveform is Tracktion's own DAW, built on the same Tracktion Engine Forge is built on.** So its feature set is
close to a map of *what Forge's engine already exposes*. When the guide documents follow actions, step clips,
modifiers, launch modes, comping, or tempo maps, those ride on engine primitives (`LaunchHandle`, `StepClip`,
modifier nodes, `Edit` follow-actions, tempo sequence) reachable through the same seams Forge already uses. A
feature being in Waveform is decent evidence it's a *wiring + UI* job for Forge, not a from-scratch DSP build.

## How to read this

Each row carries:
- **Fit** — value to Forge's **Session-first, controller-driven** identity through the UX/nav/workflow lens:
  ★★★ = identity-core or high everyday-UX leverage · ★★ = strong · ★ = secondary-surface / nice-to-have.
- **Engine** — `native` (Tracktion primitive exists), `wiring` (compose from existing seams), `?` (unverified).
- **Forge today** — current state, so nothing already shipped gets re-proposed.
- **(g.NN)** — page(s) in the Waveform guide, for verification.

Fit is judged for *Forge*, not for Waveform — a great Waveform feature that fights the Session-first direction
(e.g. video) scores low here on purpose.

---

# Part 1 — Workflow & UX (the primary lens)

## 1.1 The Actions panel — context-follows-selection *(★★★, native/wiring, g.48, 70–75)*

Waveform's central UX paradigm: **one panel that watches your selection** and shows the relevant
actions+properties for *whatever* is selected — empty arrangement → edit-level actions; track → track actions;
clip → clip actions; plugin/automation-point → theirs. Rows aren't just buttons: a row can be a toggle, slider,
text field, or dropdown, so you change a value **in place** without opening a separate inspector. It can dock
left, dock right, float, or be a sidebar tab, and it has a **lock** so it can stay pinned to one item.

- **Forge today:** the **channel tray** (W04a) is the closest thing but it's **track-mixing-only** (pan/sends/
  inserts/fader for the selected track). There is no general "actions for the current selection" surface; clip
  properties live in the bottom DetailView, MIDI in the piano-roll drawer, plugins in floating windows.
- **Why it matters for Forge:** this is the single biggest *workflow* idea in the guide. A selection-driven
  Actions panel would unify the scattered inspectors (clip / slot / scene / track / plugin) into one predictable
  place — a big legibility + "where do I change this?" win, and it maps cleanly onto a controller's soft-knob
  row later.

## 1.2 Quick Actions bar — user-assigned command boards *(★★★, wiring, g.74)*

Distinct from the Actions panel: **rows of buttons you assign to specific commands** — your own fixed shortcut
board that does **not** change with selection. Sits above the transport (always in view) or as a floating window;
each layout saves as a named "Shortcuts" file so you can keep different boards for different jobs (tracking vs.
mixing vs. performing).

- **Forge today:** none. Shortcuts are a fixed built-in set.
- **Fit:** very high for a **performance** tool — a customizable on-screen button board is exactly what a
  live/clip-launch player reaches for, and it's a natural companion to the hardware-controller direction.

## 1.3 Saved layouts / workspaces *(★★★, wiring, g.57–58)*

A **Layout** captures panel visibility/size/dock-state (browser, mixer, MIDI editor, global tracks, strips, the
Quick Actions bar…), the active side-panel tab, and mixer strip layout — saved as named, **application-wide**
presets (not per-project). Ships four factory layouts (Default / Arrange / Mixer / Minimal). Deliberately does
*not* store zoom/track-heights/playhead, so recalling a layout rearranges panels without disturbing your view of
the music.

- **Forge today:** tear-off popouts (mixer / piano-roll, W04b) with position persistence, but **no saved
  multi-panel layouts**. INTERFACE.md Phase 7 explicitly lists "saved layouts to do."
- **Fit:** high — a "Perform" layout (grid maximised) vs. an "Edit" layout (piano-roll + tray) vs. "Mix" is
  precisely the kind of mode-switching a Session-first app wants.

## 1.4 Customizable keyboard shortcuts *(★★, wiring, g.19, 104–106, 378–380)*

A full **Keyboard Shortcuts** settings page: every action is a searchable command you bind to any key; **reset to
defaults**, **save/load key-mappings** to a file (move your setup between machines), and a **"View as HTML"**
export of the whole map to a searchable/printable page. Commands grouped by category (Application, Automation,
Browser, Clip, Editing…).

- **Forge today:** a fixed hard-coded set (B/E/F8/F9/F11/Space/R/Ctrl+…). Not user-remappable, not discoverable
  as a list.
- **Fit:** medium-high — pairs with 1.1/1.2; the HTML/searchable shortcut list is also a cheap **discoverability**
  win.

## 1.5 Contextual help & discoverability *(★★, wiring, g.36–37, 44–46)*

Two layered help systems: **Pop-up help** (hover + press **F1** for a fuller explanation of the item under the
pointer, toggleable) and **roll-over help** (a one-line description of the hovered control shown in a fixed corner,
appears automatically). Plus in-app links to the user-guide PDF and the shortcut list.

- **Forge today:** app-wide tooltips (Phase 1) — good, but single-tier. No F1 deep help, no persistent rollover
  description zone.
- **Fit:** medium — low-cost, and roll-over help in a status-strip-adjacent zone fits Forge's existing status
  strip.

## 1.6 Session / clip-launcher workflow — the expressiveness gap *(★★★, native, g.345–356)*

Waveform's clip launcher (same engine domain as Forge's primary surface) exposes launcher behaviour Forge's grid
doesn't yet:

| Sub-feature | What it adds | Forge today | (g.) |
|---|---|---|---|
| **Launch modes** Trigger / Gate / Toggle / Repeat | Per-slot launch behaviour (hold-to-play, press-on/off, re-fire) | Trigger only | 348 |
| **Follow actions** | Per-clip auto-sequencing: after N beats/loops → prev/next/first/last/**random**/**round-robin**, **track & group scopes**, **probability weights** | none (DIRECTION open question) | 351–354 |
| **Performance recording** | Global-record **captures the live launch performance into the Arrange timeline** as clips — the Session→Arrange bridge | none | 351 |
| **Launch quantisation** global + **per-clip override** | Choose the launch grid; a clip can opt out | fixed 1 bar | 345–346 |
| **Legato / Nudge / Offset** | Seamless variation swaps; shift a clip's playhead by the quantise amount | none | 347 |
| **"Has stop/rec button" slot property** | Let a long loop keep running through a scene change | scene launch stops all | 348 |
| **Limit record length** | Auto-stop slot recording after N bars (instant looper) | manual stop | 350 |
| **Back-to-arranger buttons** | Toggle a track between launcher clips and arranger clips | n/a (views separate) | 346 |

**This is the highest-value cluster in the whole guide for Forge** — it deepens the *primary surface*, it's all
engine-native on primitives Forge already drives, and **performance recording is literally the "compose in
Session → arrange linearly" workflow the DIRECTION brief promises but hasn't built.**

## 1.7 Browser & content workflow *(★★, native/wiring, g.48, 59–65)*

A tabbed side-browser: **Search** (unified loops/presets/plugins/racks, filtered by category + **tags**),
**Files** (plain filesystem tree with **bookmarks** for your sample folders), **Tracks**, **Groups**, **Markers**.
Key workflow pieces:
- **Drag-and-drop onto tracks** as the primary insert gesture (Shift-drag spreads files across consecutive
  tracks); drag files *in* from the OS to add to the library. *(g.62)*
- **Audition / preview strip** at the bottom — hear a loop before dropping it; **live preset preview** plays a
  synth preset through your MIDI input before you commit. *(g.63, 65)*
- **Smart lists** — Packs / **Edit** (all assets used) / **Project** / Recent, with a **Consolidate** button that
  copies external files into the project folder (session-audit + safe-to-move). *(g.61)*
- **Tags bar** that self-populates from the current results for fast cross-filtering. *(g.60)*

- **Forge today:** a `FileTreeComponent` browser, **double-click to import**. INTERFACE Phase 3 lists
  "drag-onto-track + value popups to do." No audition/preview, no tags, no bookmarks, no consolidate.
- **Fit:** medium-high — **audition-before-drop** and **drag-to-track** are core sample-DAW ergonomics for a
  clip/loop workflow; **Consolidate** is a real project-hygiene win.

## 1.8 Track organization — tags, folders, groups *(★★, native, g.66–69, 274–282)*

- **Track tags + "Show Only Tagged Tracks"** *(g.66–67)* — tag tracks (Drums, Verse…) then collapse the timeline
  to just the tagged set (parents/children preserved). Scales a big session down to what you're working on.
- **Folder tracks** *(g.274–277)* — collapse related tracks into one foldable unit; the folder clip can be split/
  rearranged to move whole sections; carries a **VCA** that proportionally rides the child faders.
- **Submix tracks** *(g.279–280)* — like a folder but audio actually flows through it, so you can bus-process the
  group.
- **Edit Mix Groups / VCA** *(g.68–69, 281–282)* — link volume/mute/pan/solo across arbitrary tracks; a track can
  be in several groups (coloured slices in the header).

- **Forge today:** flat track list; aux A/B sends + return strips (W01 P3); no tags, folders, submixes, or VCA
  groups.
- **Fit:** medium — most valuable once sessions get large; folder/collapse is a navigation win, VCA is a mixing
  win.

---

# Part 2 — Navigation & moving around

*All engine/JUCE-level UI behaviour; mostly `wiring` on Forge's existing `TimelineView`/viewport.*

- **Zoom vocabulary** *(★★★, g.39–40)* — Up/Down keys zoom, drag on the timeline-just-above-the-cursor zooms, a
  **quick-zoom marquee** (Ctrl+Alt-drag a box → zoom to it; Ctrl+Alt-click steps back through remembered zoom
  levels), plus a full **Zoom actions menu** (right-click timeline) with **zoom-to-selection** — all bindable to
  keys. *Forge today: snap-division selector + ruler; zoom-to-selection / marquee zoom not evident.*
- **Cursor-positioning modes** *(★★, g.40–41)* — click timeline / track background / clip body to locate; arrow
  keys step; a **"Timeline drag action → drag to position transport"** mode; toggles for whether clicking the
  background locates the cursor. *Forge today: arrange playhead click/drag scrub.*
- **Separate edit cursor** *(★★, g.375)* — an optional second cursor that follows the mouse so editing/zoom/paste
  act there **without moving the playhead**. Nice for editing while playing.
- **Marked region (In/Out) as the universal scope** *(★★★, g.49, 76–77)* — the I/O markers define loop **and**
  the target of many edit ops; **press A to set the marked region over the current selection** (works on clips
  and marker clips → instant "loop this section"). *Forge today: transport loop button exists; a selection-driven
  marked-region concept doesn't.*
- **Markers & marker navigation** *(★★, native, g.76–81)* — add via **Return** (auto next number), **type a
  number + Return to jump to that marker** (during playback or idle), renumber-all trick, a **Markers browser
  tab** (double-click name → jump), and split **Bars&Beats vs. Timecode** marker lanes (F10 cycles hidden/normal/
  split). *Forge today: `MarkerBar` (W01 P5) renders markers on the timeline; no number-jump navigation or
  browser list.*
- **Nudging** *(★★, g.85, 167)* — Shift+arrows nudge clips **and** notes by one grid increment (Shift-↑/↓ =
  pitch for notes, Shift-←/→ = time); works on multi-selections and moves clips track-to-track. *Forge today:
  drag-move + snap; no keyboard nudge.*
- **Scroll behaviour + return-to-start-on-stop** *(★, g.39, 43)* — auto-scroll-to-keep-cursor-visible toggle
  (transport **S**), and **Return-cursor-to-start-on-stop** toggle (also on right-click timeline).
- **Configurable mouse-wheel / right-drag** *(★★, g.377)* — wheel = scroll-or-zoom (modifier swaps), right-drag =
  zoom-or-scroll, per-area overrides, and "wheel scrolls the inline MIDI grid when over a MIDI clip." A small
  setting, big everyday-feel difference.
- **Track height cycling + display scaling** *(★★, g.359, 357, 376)* — double-click a track header to cycle
  through configurable heights; a global **Display scale 50–300%** (HiDPI/laptop); "Simplify UI" + "Animate
  panels" toggles. *Forge today: fixed-height session pads (46 px); no UI scale setting.*

---

# Part 3 — Editing ergonomics

## 3.1 Clip editing gestures *(★★, native/wiring, g.86–92, 121–125, 163–164)*

- **Six-handle clip model** — trim (hollow), **slip** (solid — move content inside a fixed window), **reframe**
  (hollow box — move the window, leave content), and stretch handles; the same handle grammar on audio **and**
  MIDI clips. *(g.86–88, 163)*
- **Split-while-dragging** — hold the mouse and press **/** repeatedly to make many splits without lifting.
  *(g.89)*
- **Merge / render clips to one** — collapse a shredded edit back to a single clip. *(g.92, 125)*
- **Auto-crossfade on overlap** + one-click **edge fades** (7 ms anti-click) + **pitch-fade** option on a fade
  handle. *(g.5, 90–91, 375)*
- **Forge today:** clip drag-move + snap + snap-division; DetailView fades; anti-click edge fade on import
  (W01 P6). No slip/reframe handles, no split-key, no merge, no overlap auto-crossfade.

## 3.2 MIDI editor ergonomics *(★★★ for a clip DAW, native/wiring, g.164–173, 359, 376)*

The guide's MIDI editor is dense with everyday-feel features Forge's piano-roll lacks:

- **Step entry mode** *(g.170, 376)* — toggle Step; notes you play land at the cursor and advance by the snap
  grid. Enter parts without playing in time. *(FEATURE_CATALOG "Standard".)*
- **Hear-notes-while-editing** *(g.171)* — a speaker toggle auditions pitches as you drag notes.
- **Per-note automation lane** *(g.166)* — draw CC/volume/articulation curves *per selected note*, any controller
  via a Type picker.
- **Velocity editor + multi-velocity edit** *(g.170–171)* — a velocity stalk lane; Ctrl-select all the C4s and
  drag once to move them together; Shift-drag a note to scrub its velocity.
- **Select a whole pitch row via the keybed** (Ctrl-click a PRV key) *(g.170)*.
- **Note colours** *(g.169)* — colour kicks/snares/hats differently for visual organization.
- **MIDI Event List** *(g.172–173)* — a floating numeric table of every event (add/transpose/quantise/groove/
  chord menus, per-type filters) for exact-value editing.
- **MIDI note names** *(g.343)* — name note 36 "Kick", 38 "Snare" → the piano-roll shows names, not C1/D1.
- **Line / pencil / eraser tools** *(g.168–169)*.
- **Forge today:** piano-roll with draw/move/resize/delete, multi-select (click/shift/marquee), copy-paste, a
  **velocity lane**, velocity shading (W6). Missing: step entry, hear-while-editing, per-note CC, note colours,
  event list, note names, pitch-row select, line/eraser tools.
- **Fit:** high — for a beat/clip-centric DAW the MIDI editor is a daily surface; step entry + note names +
  hear-while-editing are the highest-leverage adds.

## 3.3 Recording workflow *(★★, native, g.114–116, 133–144)*

- **Retrospective record** *(★★★, g.114–115)* — Waveform continuously buffers input; if you played something
  great while *not* recording, one click drops the buffered audio onto the track. Tailor-made for a
  performance/live tool — catches happy accidents. *(FEATURE_CATALOG doesn't even list this; it's a standout.)*
- **Loop-record into take lanes** *(g.133–134, 160–162)* — cycle the marked region, each pass stacks a take;
  works for audio and MIDI.
- **Comping — swipe + comp groups** *(g.135–144)* — expand takes, **swipe** the best phrase from any take to
  promote it to the composite, adjustable comp crossfade; **Comp Groups** extend swipe-comping across multiple
  tracks at once; render-and-replace when done.
- **Abort-and-delete take** *(g.114)* — kill a bad take instantly.
- **Forge today:** transport record, count-in/metronome (W01 P1), MIDI record into slots (W7). No retrospective
  record, no take lanes, no comping.
- **Fit:** retrospective record ★★★ (on-brand for live capture); comping ★★ (more of an arrange-side/tracking
  feature).

## 3.4 Plugin-interaction UX *(★, native/wiring, g.204–210, 322–324)*

Duplicate a plugin with **D**, delete with Delete/Backspace *(g.204)*; a **docked Plugin panel** as an
alternative to floating editors *(g.208–210)*; a **Visual Plugin Selector** with thumbnail images + type icons
(VST3/AU) and type-to-filter *(g.322–324)*; right-click to set a plugin's **Quick Control** parameter *(g.205)*.
*Forge today: floating plugin windows + a scan dialog + insert menu; no docked panel, no visual/thumbnail
selector.*

---

# Part 4 — Capability features (condensed)

*Less about UX, more about "what the engine can do." Ranked by fit; most are `native`. These extend what a clip
can contain or how sound is made — feeding the grid.*

- **Step Clips — in-line step sequencer clip type** *(★★★, native, g.183–199)* — a first-class clip type: drum
  grid, per-step velocity/groove, **per-row destination routing** (kick → one instrument, hat → another),
  pattern sections + variations. Enormously on-brand for a beat/clip DAW and a native Tracktion clip. **Top
  capability pick.**
- **Modifiers — LFO / envelope-follower / step / random / MIDI-tracker** *(★★, native, g.23, 325–329)* —
  modulate any plugin parameter with no automation drawn (Bitwig-style). FEATURE_CATALOG "Pro."
- **MIDI effects — arpeggiator / chord player / note repeater / MIDI note names** *(★★, native, g.339–343)* —
  real-time MIDI processors in the chain.
- **More built-in instruments — Wavetable, Subtractive, Bass Osc, Multi/Micro/Drum Samplers, Rompler** *(★★,
  native, g.230–254)* — Forge ships only 4OSC; these are Tracktion built-ins, likely surface-able with modest
  wiring. A **drum sampler / finger-drum** path is called out as a possible wave in DIRECTION.
- **Plugin Racks + Macros + Faceplates** *(★, native, g.255–268)* — containers with parallel/side-chain routing,
  macro knobs, custom faceplate UIs.
- **Warp Time / time-stretch / clip pitch** *(★, native, g.126–128, 334–335)* — bend audio to the grid.
- **Groove Doctor / quantise / groove templates** *(★★, native, g.129–132, 177–182)* — swing/groove; Forge's
  piano-roll has no quantise yet.
- **Clip Layers / clip FX** *(★, native, g.330–338)* — per-clip volume/fade/pitch/warp/plugin as stacked layers.
- **Tempo track / tempo map + time-sig changes** *(★★, native, g.41–43)* — tempo curve with points/curvature,
  tap tempo, insert-tempo-change-at-cursor. Forge has a fixed project tempo and no tempo track.
- **Chord track + progression builder + suggestions** *(★, native, g.200–201, 364–365)* — generate chords/
  bassline/melody in key.
- **Delivery: MP3 export, archive/consolidate edit, freeze point/track freeze** *(★★, native, g.228, 305,
  311–312)* — MP3 export, collect-all-files archive, and CPU-offload freeze.
- **Skip for Forge:** cloud **AI assistant** *(g.319–321)* and **video track** *(g.309–310)* — off-identity.

---

# Part 5 — Already in Forge (don't re-mine)

So this list isn't misread as all-missing — Forge already ships: Session clip grid + scenes + scene launch +
stop-all; MIDI record into slots (W7); piano-roll (draw/move/resize/velocity lane/multi-select/copy-paste);
mixer (strips/inserts/bypass/reorder/master meter); plugin hosting + external scan + floating editors; aux A/B
sends + returns; WAV + per-track stem export (async); markers; volume/pan automation lanes; metronome + count-in;
MIDI-learn + focused-Edit CC routing; MIDI-clock out; offline LUFS; a Launchpad control-surface driver; global
undo/redo; transport LCD; channel tray; menu bar; tear-off popouts; snap + snap-division; app-wide logging.

---

# Part 6 — Suggested sequencing (weighted to the UX/nav/workflow driver)

Ordered to front-load the maintainer's stated priorities. Each is scoped to be headless-provable (a new selftest
gate) per Forge's no-manual-test constraint.

1. **Launcher expressiveness wave** *(§1.6)* — launch modes → follow actions → performance recording. Deepens the
   *primary surface*, all engine-native, and delivers the Session→Arrange bridge. **The single strongest pick —
   high UX payoff *and* identity-core.**
2. **The Actions panel** *(§1.1)* — generalize the channel tray into a selection-driven context panel. The
   biggest structural *workflow* win; unifies today's scattered inspectors and pre-shapes the controller
   soft-knob row.
3. **Navigation pass** *(Part 2)* — zoom-to-selection + marquee zoom, keyboard nudge (clips + notes), marked-
   region-over-selection (**A**), marker number-jump, configurable wheel/right-drag. Low-risk, high daily-feel.
4. **Layouts + Quick Actions bar + customizable shortcuts** *(§1.3, 1.2, 1.4)* — the "make it mine / mode-switch"
   cluster; strong for a performance tool.
5. **MIDI-editor ergonomics** *(§3.2)* — step entry, hear-while-editing, MIDI note names, note colours.
6. **Browser workflow** *(§1.7)* — drag-to-track + audition/preview + Consolidate.
7. **Retrospective record** *(§3.3)* — small, delightful, on-brand for live capture.
8. **Step Clips** *(Part 4)* — the top *capability* add for a beat DAW; a native clip type.

*Open design calls to resolve before building any of these: does the Actions panel replace or complement the
DetailView + tray? Does "performance recording" write into the existing Arrange view unchanged? Do saved layouts
belong per-project or app-wide (Waveform chose app-wide)? — surface to the maintainer at planning, not mid-wave.*
