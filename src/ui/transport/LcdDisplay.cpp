#include "ui/transport/LcdDisplay.h"
#include "ui/transport/TempoPopup.h"
#include "ui/transport/TimeSigPopup.h"
#include "ui/ForgeLookAndFeel.h"

using namespace juce;

LcdDisplay::LcdDisplay()
{
    // Only the tempo zone is clickable (hitTest gates it to tempoZoneBounds); every other part
    // of the face stays click-through to the crowded control bar underneath. wantsKeyboardFocus
    // stays false — the popup owns its own focus.
    setInterceptsMouseClicks (true, false);

    current = forge::lcd::computeLcdState ({});   // static default face ("1|1", "120.0")

    startTimerHz (25);   // same cadence as TransportBar's transport poll
}

LcdDisplay::~LcdDisplay()
{
    stopTimer();
}

void LcdDisplay::setEdit (te::Edit* e)
{
    // SAME-edit resync (the shell routes generic toggle resyncs through controlBar.setEdit):
    // exit demo mode and refresh, but do NOT touch the edge-tracking or the count-in latch —
    // resetting them mid-pre-roll would kill the count-in face and surface the raw negative
    // bars the face exists to hide (QC: any Transport menu command during a count-in did
    // exactly that). The live timer has been maintaining prev*/latched* all along.
    if (e == edit)
    {
        demoMode = false;
        refreshNow();   // synchronous: a frozen demo face must not leak into the next snapshot
        return;
    }

    edit = e;
    transport = (e != nullptr) ? &e->getTransport() : nullptr;

    // Genuine edit swap: seed the edge-tracking from live state so a transport already in
    // motion doesn't read as a fresh record trigger on the first tick (a phantom rising edge
    // would latch a phantom count-in — the exact failure skeptic guard 1 exists to prevent).
    prevPlaying   = transport != nullptr && transport->isPlaying();
    prevRecording = transport != nullptr && transport->isRecording();
    latchedCountInTotal = 0;
    latchedFromStopped  = false;
    fedByLivePoll = false;   // the probe means "fed from THIS edit"

    demoMode = false;   // live polling resumes
    refreshNow();
}

void LcdDisplay::setDemoState (const forge::lcd::LcdState& s)
{
    demoMode = true;
    current  = s;
    repaint();
}

bool LcdDisplay::hitTest (int x, int y)
{
    // Clickable ONLY over the tempo or signature readout, and only once the shell has wired that
    // zone's seams — otherwise that part of the LCD stays click-through exactly as before (each
    // zone is also empty at narrow widths / during count-in, which sheds its clickability).
    const bool overTempo = queryBpm != nullptr && onBpmChanged != nullptr && tempoZoneBounds.contains (x, y);
    const bool overSig   = querySig != nullptr && onSigChanged  != nullptr && sigZoneBounds.contains (x, y);
    return overTempo || overSig;
}

void LcdDisplay::mouseUp (const MouseEvent& e)
{
    // Tempo zone -> tempo popup. Both seams must be wired and the click must land in the last-
    // painted tempo readout (empty at narrow widths / during count-in, which sheds clickability).
    // The popup owns the tap estimator and pushes edits back through our seams; the box anchors to
    // the tempo zone and manages its own lifetime.
    if (queryBpm != nullptr && onBpmChanged != nullptr && tempoZoneBounds.contains (e.getPosition()))
    {
        CallOutBox::launchAsynchronously (std::make_unique<TempoPopup> (queryBpm, onBpmChanged),
                                          localAreaToGlobal (tempoZoneBounds),
                                          nullptr);
        return;
    }

    // Signature zone -> time-signature popup (a second branch beside the tempo one; same gating).
    if (querySig != nullptr && onSigChanged != nullptr && sigZoneBounds.contains (e.getPosition()))
    {
        CallOutBox::launchAsynchronously (std::make_unique<TimeSigPopup> (querySig, onSigChanged),
                                          localAreaToGlobal (sigZoneBounds),
                                          nullptr);
    }
}

void LcdDisplay::timerCallback()
{
    if (demoMode)
        return;

    refreshNow();
}

void LcdDisplay::refreshNow()
{
    if (edit == nullptr || transport == nullptr)
    {
        const auto def = forge::lcd::computeLcdState ({});
        if (def != current) { current = def; repaint(); }
        return;
    }

    const bool playing   = transport->isPlaying();
    const bool recording = transport->isRecording();

    // Record rising edge: latch the count-in beat total, and ONLY when the previous tick saw a
    // stopped transport (skeptic guard 1 — a mid-playback record() punches in with no pre-roll
    // and a stale start time; latching 0 keeps the count-in face off for that path). Reading
    // getNumCountInBeats() here rather than per tick keeps PropertyStorage off the poll.
    if (recording && ! prevRecording)
    {
        latchedFromStopped  = ! prevPlaying;
        latchedCountInTotal = latchedFromStopped ? edit->getNumCountInBeats() : 0;
    }
    else if (! recording && prevRecording)
    {
        latchedFromStopped  = false;
        latchedCountInTotal = 0;
    }

    prevPlaying   = playing;
    prevRecording = recording;

    auto& ts       = edit->tempoSequence;
    const auto pos = transport->getPosition();
    const auto bb  = ts.toBarsAndBeats (pos);

    forge::lcd::LcdInput in;
    in.bars           = bb.bars;
    in.beatInBar      = bb.getWholeBeats();
    in.fractionalBeat = bb.getFractionalBeats().inBeats();
    in.bpm            = ts.getBpmAt (pos);
    in.timeSigString  = ts.getTimeSigAt (pos).getStringTimeSig();

    // Key policy (W04a design decision): note name plus a lowercase "m" for minor AND aeolian
    // (identical intervals — both read as minor to a musician); every other scale shows the
    // bare note name.
    auto& pitchSetting = edit->pitchSequence.getPitchAt (pos);
    const auto scale   = pitchSetting.getScale();
    in.keyString       = pitchSetting.getName()
                       + ((scale == te::Scale::minor || scale == te::Scale::aeolian) ? "m" : "");

    in.recording          = recording;
    in.positionSeconds    = pos.inSeconds();
    in.startedFromStopped = latchedFromStopped;
    in.countInTotal       = latchedCountInTotal;

    if (recording && latchedFromStopped && latchedCountInTotal > 0)
    {
        // The digit derives from the CLICK GRID (whole timeline beats), never from distances
        // to the punch — the punch is not beat-snapped, and QC proved the distance form leads
        // the audible click by up to a full beat after a mid-beat stop.
        const auto punch    = transport->getTimeWhenStarted();   // the punch position
        in.punchTimeSeconds = punch.inSeconds();
        in.currentBeat      = ts.toBeats (pos).inBeats();
        in.punchBeat        = ts.toBeats (punch).inBeats();
    }

    const auto next = forge::lcd::computeLcdState (in);
    fedByLivePoll = true;   // a live tick computed a state from a bound edit (the report probe)

    if (next != current)   // edge-compare: a static face never repaints; motion animates
    {
        current = next;
        repaint();
    }
}

void LcdDisplay::paint (Graphics& g)
{
    const auto face = getLocalBounds().toFloat();

    if (face.isEmpty())
        return;

    // The inset screen: face fill + 1 px recessed bezel, 3 px corner radius.
    g.setColour (Colour (ForgeLookAndFeel::lcdBg));
    g.fillRoundedRectangle (face, 3.0f);
    g.setColour (Colour (ForgeLookAndFeel::lcdFrame));
    g.drawRoundedRectangle (face.reduced (0.5f), 3.0f, 1.0f);

    if (current.countInActive)
    {
        // The count-in face has no tempo/signature readout — clear both clickable zones so a
        // mid-record click can't launch a popup over the count-in digit.
        tempoZoneBounds = {};
        sigZoneBounds   = {};

        // Count-in face: one large centred digit over a recordRed underline whose alpha peaks
        // on the click (pulsePhase 0) and decays across the beat. The raw bars|beats never
        // render here — during the pre-roll they run negative.
        const auto digitArea = getLocalBounds().withTrimmedBottom (5);

        g.setFont (Font (FontOptions (22.0f, Font::bold)));

        if (current.countInDigit > 0)
        {
            g.setColour (Colour (ForgeLookAndFeel::textPrim));
            g.drawText (String (current.countInDigit), digitArea, Justification::centred);
        }
        else
        {
            // The lead-in half-beat: a dimmed placeholder, never a zero.
            g.setColour (Colour (ForgeLookAndFeel::textPrim).withAlpha (0.35f));
            g.drawText ("·", digitArea, Justification::centred);
        }

        const float pulse = 1.0f - (float) current.pulsePhase;         // 1 on the click, decays
        const auto underline = Rectangle<int> (0, 0, 28, 3)
                                   .withCentre ({ getWidth() / 2, getHeight() - 4 });
        g.setColour (Colour (ForgeLookAndFeel::recordRed).withAlpha (0.2f + 0.8f * pulse));
        g.fillRect (underline);
        return;
    }

    // Idle/playing face, four zones. Narrow widths drop the timecode zone first (absolute
    // time is the most expendable readout), then the key zone (at the width where the tempo
    // strip would otherwise ellipsize beside it), then the tempo zone at the published floor;
    // the position readout always survives. Thresholds are the published header constants so
    // code and contract cannot drift (QC-corrected).
    auto inner = getLocalBounds().reduced (10, 2);
    const bool showTimecode = getWidth() >= timecodeMinWidth;
    const bool showKey      = getWidth() >= keyZoneMinWidth;
    const bool showTempo    = getWidth() >= minimumWidth;

    // LEFT — bars|beats, the large transport-clock readout.
    g.setColour (Colour (ForgeLookAndFeel::timeTempo));
    g.setFont (Font (FontOptions (Font::getDefaultMonospacedFontName(), 16.0f, Font::plain)));
    g.drawText (current.positionText, inner.removeFromLeft (jmin (64, inner.getWidth())),
                Justification::centredLeft);

    // LEFT-CENTRE — absolute time, immediately right of the position (W04b). textSec, not
    // timeTempo: absolute time is secondary to musical time in the design hierarchy.
    if (showTimecode)
    {
        g.setColour (Colour (ForgeLookAndFeel::textSec));
        g.setFont (Font (FontOptions (Font::getDefaultMonospacedFontName(), 12.0f, Font::plain)));
        g.drawText (current.timecodeText, inner.removeFromLeft (jmin (80, inner.getWidth())),
                    Justification::centredLeft);
    }

    // RIGHT — key + time signature. The whole "C · 4/4" readout is the clickable signature zone:
    // latch it into sigZoneBounds for hitTest / mouseUp (mirrors the tempo strip below). When the
    // key zone is shed (too narrow, or the count-in face above), the bounds are cleared so that
    // part of the LCD becomes click-through again.
    if (showKey)
    {
        const auto keyZone = inner.removeFromRight (jmin (56, inner.getWidth()));
        sigZoneBounds = keyZone;

        g.setColour (Colour (ForgeLookAndFeel::textSec));
        g.setFont (Font (FontOptions (11.0f)));
        g.drawText (current.keySigText, keyZone, Justification::centredRight);
    }
    else
    {
        sigZoneBounds = {};   // no key/sig readout painted -> nothing clickable there
    }

    // CENTRE — tempo, with a small BPM tag. The whole strip (readout + tag) is the clickable
    // tempo zone: latch it into tempoZoneBounds for hitTest / mouseUp. When the tempo zone is
    // shed (too narrow, or the count-in face above), the bounds are cleared so the whole LCD
    // becomes click-through again.
    if (showTempo)
    {
        const auto tempoStrip = inner.withSizeKeepingCentre (jmin (70, inner.getWidth()), inner.getHeight());
        tempoZoneBounds = tempoStrip;

        auto tempoArea = tempoStrip;
        const auto bpmTag = tempoArea.removeFromRight (24);

        g.setColour (Colour (ForgeLookAndFeel::timeTempo));
        g.setFont (Font (FontOptions (Font::getDefaultMonospacedFontName(), 14.0f, Font::plain)));
        g.drawText (current.tempoText, tempoArea, Justification::centredRight);

        g.setColour (Colour (ForgeLookAndFeel::textSec));
        g.setFont (Font (FontOptions (9.0f)));
        g.drawText ("BPM", bpmTag.translated (3, 2), Justification::centredLeft);
    }
    else
    {
        tempoZoneBounds = {};   // no tempo readout painted -> nothing clickable
    }
}
