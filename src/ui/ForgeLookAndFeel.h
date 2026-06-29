/*
    ForgeLookAndFeel — Forge's dark, amber-accented theme.

    All app colours centralize here (via LookAndFeel colour IDs / colour scheme) so that
    re-theming, high-contrast, and per-feature styling are a single pass and every new
    component inherits the look for free. Palette is from docs/INTERFACE.md.
*/

#pragma once

#include <JuceHeader.h>

class ForgeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    enum Palette : juce::uint32
    {
        shellBg   = 0xff1a1c1e,   // window / shell
        panelBg   = 0xff232629,   // docked panels (control bar, headers)
        raisedBg  = 0xff2d3135,   // raised controls (buttons)
        textPrim  = 0xffd6d9dc,   // primary text
        textSec   = 0xff8a9095,   // secondary text / labels
        hairline  = 0xff34383c,   // separators
        accent    = 0xffe0902f,   // warm amber: playhead, arm, selection, focus
        onAccent  = 0xff241600,   // text on the accent fill
        recordRed = 0xffe24b4a    // reserved: active recording / clipping
    };

    ForgeLookAndFeel()
    {
        auto scheme = getDarkColourScheme();
        scheme.setUIColour (ColourScheme::windowBackground, juce::Colour (shellBg));
        scheme.setUIColour (ColourScheme::widgetBackground, juce::Colour (panelBg));
        scheme.setUIColour (ColourScheme::menuBackground,   juce::Colour (panelBg));
        scheme.setUIColour (ColourScheme::outline,          juce::Colour (hairline));
        scheme.setUIColour (ColourScheme::defaultText,      juce::Colour (textPrim));
        scheme.setUIColour (ColourScheme::menuText,         juce::Colour (textPrim));
        scheme.setUIColour (ColourScheme::defaultFill,      juce::Colour (accent));
        scheme.setUIColour (ColourScheme::highlightedFill,  juce::Colour (accent));
        scheme.setUIColour (ColourScheme::highlightedText,  juce::Colour (onAccent));
        setColourScheme (scheme);

        setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (shellBg));
        setColour (juce::DocumentWindow::textColourId,        juce::Colour (textPrim));

        setColour (juce::TextButton::buttonColourId,   juce::Colour (raisedBg));
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (accent));
        setColour (juce::TextButton::textColourOffId,  juce::Colour (textPrim));
        setColour (juce::TextButton::textColourOnId,   juce::Colour (onAccent));

        setColour (juce::Label::textColourId,          juce::Colour (textPrim));

        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (raisedBg));
        setColour (juce::TooltipWindow::textColourId,       juce::Colour (textPrim));
        setColour (juce::TooltipWindow::outlineColourId,    juce::Colour (hairline));

        setColour (juce::ComboBox::backgroundColourId, juce::Colour (raisedBg));
        setColour (juce::ComboBox::textColourId,       juce::Colour (textPrim));
        setColour (juce::ComboBox::outlineColourId,    juce::Colour (hairline));
    }
};
