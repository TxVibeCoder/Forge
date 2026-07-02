/*
    StripWidgets — Forge's shared channel-strip control styling (W05). The mixer strips
    (MixerView.cpp: ChannelStrip / MasterStrip / ReturnStrip) and the Arrange channel tray
    (ChannelTray.cpp) build the SAME dB fader, pan knob, aux-send knob and M/S toggles; before
    this header each surface carried its own byte-for-byte copy of the range constants, the
    setColour/setRange/setStyle calls and the busLetter helper. Extracting them here (the
    shared-utilities principle, the PeakMeter.h precedent) makes "how a Forge fader/knob/toggle
    looks" a single definition so the two surfaces cannot drift apart.

    SCOPE — style only. Each helper does exactly the range/snap/double-click/colour/text-style
    setup that is IDENTICAL across every consuming surface. It deliberately does NOT:
      - call addAndMakeVisible — visibility stays with the caller. ReturnStrip::refresh() manages
        its M/S buttons' visibility independently of construction (placeholder vs. live), so a
        helper that added-and-showed would fight that logic; the tray and mixer strips likewise
        own their own add/show ordering.
      - set the fader's text-box style — its width differs per surface (stripW-8 / masterW-8 /
        returnW-8 / preferredWidth-32), so the caller applies setTextBoxStyle afterwards.
      - set a tooltip — the send/pan tooltips are per-instance (they name the bus letter), so the
        caller sets them.
      - wire setValue / onValueChange / onDragStart / onDragEnd — all value + drag wiring is the
        caller's, since it binds to that surface's engine seam.

    The range constants are inline constexpr so every including TU shares one definition, and are
    named in the forge::strip namespace (like forge::meter in PeakMeter.h) so an including TU's own
    kMin/kMax locals can't collide. Callers reference them for their own jlimit clamps in the
    live-sync paths (MixerView SendControls, ChannelTray::syncControls).

    Header-only — not listed in CMake target_sources (only .cpp files are), matching PeakMeter.h.
    Message-thread only (JUCE widget setup); no engine calls here.
*/

#pragma once

#include <JuceHeader.h>
#include "ui/ForgeLookAndFeel.h"

namespace forge::strip
{
    // --- Fader travel (dB) -----------------------------------------------------------------------
    // -60 reads as -inf-ish silence at the bottom; +6 of headroom up top. Matches the range mapped
    // by EngineHelpers::set/getTrackVolumeDb (which clamps the underlying 0..1 volume parameter into
    // dB), so the slider position round-trips cleanly.
    inline constexpr double kMinDb = -60.0;
    inline constexpr double kMaxDb =   6.0;

    // --- Pan travel ------------------------------------------------------------------------------
    // hard-left (-1) .. centre (0) .. hard-right (+1).
    inline constexpr double kMinPan = -1.0;
    inline constexpr double kMaxPan =  1.0;

    // --- Aux-send knob travel (dB) ---------------------------------------------------------------
    // Bottom of the knob (kMinSendDb) reads as "no send"; the aux seam reports <= kMinSendDb (engine
    // silence) when a track has no send plugin for that bus, so the knob sits at the bottom until the
    // user dials one in. +6 of headroom up top, matching the fader.
    inline constexpr double kMinSendDb = -60.0;
    inline constexpr double kMaxSendDb =   6.0;

    /** The bus letter ("A", "B", …) for aux-return index `b`. Consumed by the send-knob tooltips and
        bus labels on both the mixer (SendControls) and the tray. */
    inline juce::String busLetter (int b)
    {
        return juce::String::charToString ((juce_wchar) ('A' + b));
    }

    /** Applies Forge's shared vertical dB-fader treatment (style, range, readout, double-click
        unity, colours) to a slider. The caller sets the text-box style afterwards (its width differs
        per surface) and wires setValue/onValueChange/drag brackets.
        Consumed by: MixerView ChannelStrip / MasterStrip / ReturnStrip faders; ChannelTray fader. */
    inline void styleDbFader (juce::Slider& f)
    {
        f.setSliderStyle (juce::Slider::LinearVertical);
        f.setRange (kMinDb, kMaxDb, 0.1);
        f.setNumDecimalPlacesToDisplay (1);
        f.setTextValueSuffix (" dB");
        f.setDoubleClickReturnValue (true, 0.0);   // double-click -> unity (0 dB)
        f.setColour (juce::Slider::thumbColourId,          juce::Colour (ForgeLookAndFeel::accent));
        f.setColour (juce::Slider::trackColourId,          juce::Colour (ForgeLookAndFeel::accent).withAlpha (0.5f));
        f.setColour (juce::Slider::backgroundColourId,     juce::Colour (ForgeLookAndFeel::raisedBg));
        f.setColour (juce::Slider::textBoxTextColourId,    juce::Colour (ForgeLookAndFeel::textSec));
        f.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (ForgeLookAndFeel::hairline));
    }

    /** Applies Forge's shared pan-knob treatment (rotary drag, no text box, -1..+1 range, double-
        click centre, accent fill + hairline outline) to a slider. The caller sets any tooltip and
        wires the value/drag callbacks.
        Consumed by: MixerView ChannelStrip pan; ChannelTray pan. */
    inline void stylePanKnob (juce::Slider& p)
    {
        p.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        p.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        p.setRange (kMinPan, kMaxPan, 0.01);
        p.setDoubleClickReturnValue (true, 0.0);     // double-click -> centre
        p.setColour (juce::Slider::thumbColourId,             juce::Colour (ForgeLookAndFeel::accent));
        p.setColour (juce::Slider::rotarySliderFillColourId,  juce::Colour (ForgeLookAndFeel::accent).withAlpha (0.5f));
        p.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (ForgeLookAndFeel::hairline));
    }

    /** Applies Forge's shared aux-send-knob treatment (rotary drag, no text box, send-dB range,
        double-click -> no send, accent fill + hairline outline) to a slider. The caller sets the
        per-bus tooltip and wires the value/drag callbacks.
        Consumed by: MixerView SendControls knobs; ChannelTray send knobs. */
    inline void styleSendKnob (juce::Slider& k)
    {
        k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.setRange (kMinSendDb, kMaxSendDb, 0.1);
        k.setDoubleClickReturnValue (true, kMinSendDb);   // double-click -> no send
        k.setColour (juce::Slider::thumbColourId,             juce::Colour (ForgeLookAndFeel::accent));
        k.setColour (juce::Slider::rotarySliderFillColourId,  juce::Colour (ForgeLookAndFeel::accent).withAlpha (0.5f));
        k.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (ForgeLookAndFeel::hairline));
    }

    /** Applies Forge's shared M/S-toggle treatment (click-toggles-state + the raised/accent colour
        set) to a text button. Style ONLY — the caller keeps addAndMakeVisible (ReturnStrip manages
        its toggle visibility separately from construction) and wires onClick.
        Consumed by: MixerView ChannelStrip / ReturnStrip M/S; ChannelTray M/S. */
    inline void styleStripToggle (juce::TextButton& b)
    {
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonColourId,   juce::Colour (ForgeLookAndFeel::raisedBg));
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (ForgeLookAndFeel::accent));
        b.setColour (juce::TextButton::textColourOffId,  juce::Colour (ForgeLookAndFeel::textSec));
        b.setColour (juce::TextButton::textColourOnId,   juce::Colour (ForgeLookAndFeel::onAccent));
    }
}
