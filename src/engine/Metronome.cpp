#include "engine/Metronome.h"
#include "core/Log.h"

namespace Metronome
{
    void enableClick (te::Edit& edit, bool shouldBeEnabled)
    {
        // clickTrackEnabled is a CachedValue<bool>; assignment writes through to the CLICKTRACK
        // ValueTree. Setting it cannot fail, so there is nothing fallible to log at ERROR/WARN;
        // a DEBUG line documents the toggle without touching a hot path (this is a user action,
        // never a per-tick poll).
        edit.clickTrackEnabled = shouldBeEnabled;
        FORGE_LOG_DEBUG (juce::String ("Metronome click ") + (shouldBeEnabled ? "enabled" : "disabled"));
    }

    bool isClickEnabled (te::Edit& edit)
    {
        return edit.clickTrackEnabled.get();
    }

    void setCountInBars (te::Edit& edit, int bars)
    {
        te::Edit::CountIn mode = te::Edit::CountIn::none;

        if (bars <= 0)
        {
            mode = te::Edit::CountIn::none;
        }
        else if (bars == 1)
        {
            mode = te::Edit::CountIn::oneBar;
        }
        else
        {
            mode = te::Edit::CountIn::twoBar;   // native whole-bar maximum

            if (bars > 2)
                FORGE_LOG_WARN ("Count-in clamped to 2 bars (Tracktion maximum); requested "
                                + juce::String (bars));
        }

        edit.setCountInMode (mode);
    }

    int getCountInBars (te::Edit& edit)
    {
        // Map the native mode back to whole bars. Sub-bar modes (oneBeat/twoBeat) are never set by
        // Forge; bucket them to the nearest whole bar so the UI selector still resolves to a value.
        switch (edit.getCountInMode())
        {
            case te::Edit::CountIn::none:    return 0;
            case te::Edit::CountIn::oneBeat: return 1;
            case te::Edit::CountIn::oneBar:  return 1;
            case te::Edit::CountIn::twoBeat: return 2;
            case te::Edit::CountIn::twoBar:  return 2;
        }

        return 0;
    }
}
