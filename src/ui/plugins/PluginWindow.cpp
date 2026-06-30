#include "ui/plugins/PluginWindow.h"
#include "ui/ForgeLookAndFeel.h"

#include <map>
#include <memory>

using namespace juce;

//==============================================================================
/*  Why this file builds its own window instead of using the engine's mechanism
    ---------------------------------------------------------------------------
    Tracktion can open plugin windows through Plugin::showWindowExplicitly(), but that routes
    into Engine::getUIBehaviour().createPluginWindow(), whose default implementation returns
    nullptr. Wiring that up means installing a custom UIBehaviour at engine-construction time,
    which lives outside this module. To stay self-contained and own the windows here (as the
    contract requires), we build a juce::DocumentWindow directly and keep them in a registry.

    The engine's BUILT-IN effects don't supply an editor component — Plugin::createEditor()
    returns nullptr for them (only ExternalPlugin overrides it). So:
      - external plugins  -> host plugin.createEditor()
      - built-in effects  -> generic panel of sliders driven by getAutomatableParameters()      */
//==============================================================================

namespace PluginWindow
{
namespace
{
    //==============================================================================
    // One labelled, two-way-bound slider for a single AutomatableParameter.
    class ParameterRow  : public Component,
                          private Timer
    {
    public:
        explicit ParameterRow (te::AutomatableParameter& p)
            : param (p)
        {
            nameLabel.setText (param.getParameterName(), dontSendNotification);
            nameLabel.setColour (Label::textColourId, Colour (ForgeLookAndFeel::textPrim));
            nameLabel.setJustificationType (Justification::centredLeft);
            nameLabel.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (nameLabel);

            valueLabel.setColour (Label::textColourId, Colour (ForgeLookAndFeel::textSec));
            valueLabel.setJustificationType (Justification::centredRight);
            valueLabel.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (valueLabel);

            const auto range = param.getValueRange();
            slider.setSliderStyle (Slider::LinearHorizontal);
            slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
            slider.setRange ((double) range.getStart(), (double) range.getEnd());
            slider.setValue ((double) param.getCurrentValue(), dontSendNotification);
            slider.setColour (Slider::trackColourId,      Colour (ForgeLookAndFeel::accent));
            slider.setColour (Slider::thumbColourId,      Colour (ForgeLookAndFeel::accent));
            slider.setColour (Slider::backgroundColourId, Colour (ForgeLookAndFeel::raisedBg));

            slider.onDragStart = [this] { param.parameterChangeGestureBegin(); };
            slider.onDragEnd   = [this] { param.parameterChangeGestureEnd(); };
            slider.onValueChange = [this]
            {
                // Guard against feedback when WE move the slider from the timer.
                if (updatingFromEngine)
                    return;

                param.setParameter ((float) slider.getValue(), sendNotification);
                refreshValueText();
            };

            addAndMakeVisible (slider);

            refreshValueText();
            startTimerHz (15);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto top = r.removeFromTop (18);
            valueLabel.setBounds (top.removeFromRight (110));
            nameLabel.setBounds (top);
            slider.setBounds (r.reduced (0, 1));
        }

    private:
        void timerCallback() override
        {
            // Pull the authoritative value from the engine (it may have moved via automation or
            // another view) and reflect it without re-triggering onValueChange.
            const auto engineValue = (double) param.getCurrentValue();

            if (! approximatelyEqual (engineValue, slider.getValue()))
            {
                const ScopedValueSetter<bool> svs (updatingFromEngine, true);
                slider.setValue (engineValue, dontSendNotification);
                refreshValueText();
            }
        }

        void refreshValueText()
        {
            valueLabel.setText (param.getCurrentValueAsStringWithLabel(), dontSendNotification);
        }

        te::AutomatableParameter& param;
        Label nameLabel, valueLabel;
        Slider slider;
        bool updatingFromEngine = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterRow)
    };

    //==============================================================================
    // Generic editor: a scrollable column of ParameterRows for a plugin with no native UI.
    class GenericPluginEditor  : public Component
    {
    public:
        explicit GenericPluginEditor (te::Plugin& plugin)
        {
            for (auto* param : plugin.getAutomatableParameters())
                if (param != nullptr)
                    rows.add (new ParameterRow (*param));

            for (auto* row : rows)
                content.addAndMakeVisible (row);

            if (rows.isEmpty())
            {
                emptyLabel.setText ("This plugin has no editable parameters.", dontSendNotification);
                emptyLabel.setJustificationType (Justification::centred);
                emptyLabel.setColour (Label::textColourId, Colour (ForgeLookAndFeel::textSec));
                content.addAndMakeVisible (emptyLabel);
            }

            viewport.setViewedComponent (&content, false);
            viewport.setScrollBarsShown (true, false);
            addAndMakeVisible (viewport);

            const int wanted = jmax (1, rows.size());
            setSize (380, jlimit (120, 560, 16 + wanted * kRowHeight));
        }

        void paint (Graphics& g) override
        {
            g.fillAll (Colour (ForgeLookAndFeel::shellBg));
        }

        void resized() override
        {
            viewport.setBounds (getLocalBounds());

            const int w = viewport.getMaximumVisibleWidth();
            const int totalH = jmax (viewport.getHeight(), rows.size() * kRowHeight);
            content.setBounds (0, 0, w, totalH);

            int y = 8;
            for (auto* row : rows)
            {
                row->setBounds (8, y, w - 16, kRowHeight - 8);
                y += kRowHeight;
            }

            if (rows.isEmpty())
                emptyLabel.setBounds (content.getLocalBounds());
        }

    private:
        static constexpr int kRowHeight = 46;

        Viewport viewport;
        Component content;
        OwnedArray<ParameterRow> rows;
        Label emptyLabel;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenericPluginEditor)
    };

    //==============================================================================
    // Forward declaration: the window's callbacks need to erase themselves from the registry,
    // which is defined below the Window class (it stores unique_ptr<Window>).
    void scheduleEraseWindow (te::Plugin*);

    //==============================================================================
    // The floating window. Owns its content, listens for plugin deletion, and removes itself
    // from the registry when the user closes it.
    class Window  : public DocumentWindow,
                    private te::SelectableListener
    {
    public:
        explicit Window (te::Plugin& p)
            : DocumentWindow (p.getName(),
                              Colour (ForgeLookAndFeel::shellBg),
                              DocumentWindow::closeButton,
                              true),
              plugin (p)
        {
            setUsingNativeTitleBar (true);

            std::unique_ptr<Component> contentComp;

            // 1. Prefer the plugin's own editor (external VST3/AU).
            if (auto editor = plugin.createEditor())
            {
                hasNativeEditor = true;
                contentComp = std::move (editor);
            }
            else
            {
                // 2. Fall back to the generic parameter panel for built-ins.
                contentComp = std::make_unique<GenericPluginEditor> (plugin);
            }

            setContentOwned (contentComp.release(), true);
            setResizable (hasNativeEditor, false); // generic panel is fixed-size + scrolls

            centreWithSize (jmax (200, getWidth()), jmax (120, getHeight()));
            setVisible (true);
            setAlwaysOnTop (true);

            plugin.addSelectableListener (this);
        }

        ~Window() override
        {
            plugin.removeSelectableListener (this);
        }

        te::Plugin& getPlugin() const noexcept   { return plugin; }

        void closeButtonPressed() override
        {
            // Defer: this call comes from inside the window, so erasing (and destroying) it here
            // would delete `this` mid-method. Schedule the erase on the message loop instead.
            scheduleEraseWindow (&plugin);
        }

    private:
        void selectableObjectChanged (te::Selectable*) override {}

        void selectableObjectAboutToBeDeleted (te::Selectable* s) override
        {
            if (s == &plugin)
                scheduleEraseWindow (&plugin);
        }

        te::Plugin& plugin;
        bool hasNativeEditor = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Window)
    };

    //==============================================================================
    // Registry of open windows, keyed by the plugin they edit. One window per plugin.
    std::map<te::Plugin*, std::unique_ptr<Window>>& getRegistry()
    {
        static std::map<te::Plugin*, std::unique_ptr<Window>> registry;
        return registry;
    }

    void eraseWindow (te::Plugin* p)
    {
        auto& reg = getRegistry();
        auto it = reg.find (p);
        if (it != reg.end())
            reg.erase (it);
    }

    void scheduleEraseWindow (te::Plugin* p)
    {
        MessageManager::callAsync ([p] { eraseWindow (p); });
    }
} // anonymous namespace

    //==============================================================================
    void show (te::Plugin& plugin)
    {
        auto& reg = getRegistry();

        if (auto it = reg.find (&plugin); it != reg.end())
        {
            // Already open — just bring it forward.
            it->second->setVisible (true);
            it->second->toFront (true);
            return;
        }

        reg[&plugin] = std::make_unique<Window> (plugin);
    }

    void closeFor (te::Plugin& plugin)
    {
        eraseWindow (&plugin);
    }

    void closeAll()
    {
        getRegistry().clear();
    }
}
