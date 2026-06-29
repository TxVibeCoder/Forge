/*
    ControlBar — the single persistent top strip (Ableton-style). Merges the old title strip,
    toolbar, and transport into one bar: left = region toggle + file commands; center = the
    embedded TransportBar; right = the Arrange|Mix view switch + the Detail-drawer toggle.

    A dumb view: it owns no project logic, only forwards intent via std::function callbacks.
*/

#pragma once

#include <JuceHeader.h>
#include "ui/transport/TransportBar.h"

namespace te = tracktion;

class ControlBar : public juce::Component
{
public:
    ControlBar();

    void setEdit (te::Edit*);
    void setViewMode (int mode);          // 0 = Arrange, 1 = Mixer

    void resized() override;
    void paint (juce::Graphics&) override;

    // Intent callbacks, wired by the shell.
    std::function<void()> onNew, onOpen, onSave, onSaveAs, onImport, onAudioSettings;
    std::function<void()> onToggleBrowser, onToggleDrawer;
    std::function<void (int)> onViewMode;

    TransportBar&       getTransportBar()       { return transportBar; }
    const TransportBar& getTransportBar() const { return transportBar; }

private:
    void updateViewButtons();

    juce::TextButton browserBtn { "Browser" };
    juce::TextButton newBtn { "New" }, openBtn { "Open" }, saveBtn { "Save" },
                     saveAsBtn { "Save As" }, importBtn { "Import" }, audioBtn { "Audio" };
    TransportBar transportBar;
    juce::TextButton arrangeBtn { "Arrange" }, mixBtn { "Mix" };
    juce::TextButton drawerBtn { "Editor" };

    int viewMode = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlBar)
};
