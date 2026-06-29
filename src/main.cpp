/*
    Forge — Phase 0: first sound.

    A deliberately tiny app whose only job is to prove the whole stack end-to-end:
      1. construct a Tracktion Engine,
      2. generate a short sine wave into a temporary .wav,
      3. load it as a looping clip on an audio track inside an Edit,
      4. play it through the default audio device (WASAPI on Windows).

    The sine is routed through the actual engine graph (clip -> track -> master ->
    device), not a bypass callback — so a working build proves the real DAW audio path.

    Run modes:
      Forge              -> opens a window; a 440 Hz sine loops audibly until you quit.
      Forge --selftest   -> plays for a few seconds, writes a status report to
                            %TEMP%/forge_phase0_selftest.log, then quits. Used for
                            automated Phase 0 verification (we can't "hear" a sine in CI,
                            but we can confirm the device opened and transport is playing).
*/

#include <JuceHeader.h>

namespace te = tracktion;
using namespace juce;

//==============================================================================
/** Writes `seconds` of a mono sine wave to a temp .wav file and returns it. */
static File createSineWaveFile (double sampleRate, double seconds, double frequencyHz, float gain)
{
    auto file = File::createTempFile (".wav");

    if (auto outStream = file.createOutputStream())
    {
        WavAudioFormat wavFormat;

        if (auto* writer = wavFormat.createWriterFor (outStream.get(), sampleRate, 1, 16, {}, 0))
        {
            outStream.release();                                  // writer now owns the stream
            std::unique_ptr<AudioFormatWriter> writerOwner (writer);

            const int numSamples = (int) (seconds * sampleRate);
            AudioBuffer<float> buffer (1, numSamples);
            auto* samples = buffer.getWritePointer (0);

            const double phaseDelta = MathConstants<double>::twoPi * frequencyHz / sampleRate;
            double phase = 0.0;

            for (int i = 0; i < numSamples; ++i)
            {
                samples[i] = gain * (float) std::sin (phase);
                phase += phaseDelta;
            }

            writerOwner->writeFromAudioSampleBuffer (buffer, 0, numSamples);
        }
    }

    return file;
}

//==============================================================================
class MainComponent : public Component,
                      private Timer
{
public:
    explicit MainComponent (bool runSelfTest)
        : selfTest (runSelfTest)
    {
        // 1. Generate a sine and load it as a looping clip in a fresh Edit.
        sineFile = createSineWaveFile (44100.0, 2.0, 440.0, 0.2f);

        edit = te::createEmptyEdit (engine, File{});
        edit->ensureNumberOfAudioTracks (1);

        if (auto* track = te::getAudioTracks (*edit)[0])
        {
            te::AudioFile audioFile (engine, sineFile);

            if (audioFile.isValid())
                clip = track->insertWaveClip (sineFile.getFileNameWithoutExtension(), sineFile,
                                              { { {}, te::TimeDuration::fromSeconds (audioFile.getLength()) }, {} },
                                              false);
        }

        // 2. Loop around the clip and start playing.
        if (clip != nullptr)
        {
            auto& transport = edit->getTransport();
            transport.setLoopRange (clip->getEditTimeRange());
            transport.looping = true;
            transport.setPosition (te::TimePosition());
            transport.play (false);
        }

        // 3. UI.
        statusLabel.setJustificationType (Justification::centred);
        addAndMakeVisible (statusLabel);

        playStopButton.onClick = [this] { togglePlay(); };
        addAndMakeVisible (playStopButton);

        setSize (520, 220);

        if (selfTest)
            startTimer (3000);          // play briefly, then report + quit
        else
            startTimerHz (5);           // live status updates
    }

    ~MainComponent() override
    {
        if (edit != nullptr)
            edit->getTransport().stop (false, false);

        sineFile.deleteFile();
    }

    void paint (Graphics& g) override
    {
        g.fillAll (getLookAndFeel().findColour (ResizableWindow::backgroundColourId));
        g.setColour (Colours::white);
        g.setFont (24.0f);
        g.drawText ("FORGE", getLocalBounds().removeFromTop (64), Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        r.removeFromTop (56);
        playStopButton.setBounds (r.removeFromBottom (40).withSizeKeepingCentre (160, 36));
        statusLabel.setBounds (r);
    }

private:
    te::Engine engine { "Forge" };
    std::unique_ptr<te::Edit> edit;
    te::WaveAudioClip::Ptr clip;
    File sineFile;
    bool selfTest = false;

    Label statusLabel { {}, "Starting audio engine..." };
    TextButton playStopButton { "Stop" };

    //==============================================================================
    void togglePlay()
    {
        if (edit == nullptr)
            return;

        auto& transport = edit->getTransport();

        if (transport.isPlaying())
            transport.stop (false, false);
        else
            transport.play (false);

        playStopButton.setButtonText (transport.isPlaying() ? "Stop" : "Play");
    }

    String describeAudioState() const
    {
        if (auto* device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
            return device->getName()
                 + " @ " + String (device->getCurrentSampleRate(), 0) + " Hz"
                 + ", buffer " + String (device->getCurrentBufferSizeSamples());

        return "No audio device open";
    }

    void timerCallback() override
    {
        const bool playing = edit != nullptr && edit->getTransport().isPlaying();
        const auto cpu = engine.getDeviceManager().getCpuUsage();

        statusLabel.setText (String (playing ? "Playing 440 Hz sine" : "Stopped")
                                 + "\n" + describeAudioState()
                                 + "\nCPU: " + String (roundToInt (cpu * 100.0)) + "%",
                             dontSendNotification);

        if (selfTest)
        {
            writeSelfTestReport (playing);
            JUCEApplication::getInstance()->systemRequestedQuit();
        }
    }

    void writeSelfTestReport (bool playing) const
    {
        auto* device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
        const bool pass = device != nullptr && playing && clip != nullptr;

        String report;
        report << "device="      << (device != nullptr ? device->getName() : String ("none")) << newLine
               << "sampleRate="  << (device != nullptr ? String (device->getCurrentSampleRate(), 0) : String ("0")) << newLine
               << "bufferSize="  << (device != nullptr ? String (device->getCurrentBufferSizeSamples()) : String ("0")) << newLine
               << "clipCreated=" << (clip != nullptr ? 1 : 0) << newLine
               << "playing="     << (playing ? 1 : 0) << newLine
               << "result="      << (pass ? "PASS" : "FAIL") << newLine;

        File::getSpecialLocation (File::tempDirectory)
            .getChildFile ("forge_phase0_selftest.log")
            .replaceWithText (report);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

//==============================================================================
class ForgeApplication : public JUCEApplication
{
public:
    const String getApplicationName() override     { return "Forge"; }
    const String getApplicationVersion() override  { return "0.0.1"; }
    bool moreThanOneInstanceAllowed() override      { return true; }

    void initialise (const String& commandLine) override
    {
        const bool selfTest = commandLine.contains ("--selftest");
        mainWindow.reset (new MainWindow ("Forge", new MainComponent (selfTest), *this));
    }

    void shutdown() override { mainWindow = nullptr; }

private:
    class MainWindow : public DocumentWindow
    {
    public:
        MainWindow (const String& name, Component* c, JUCEApplication& a)
            : DocumentWindow (name,
                              Desktop::getInstance().getDefaultLookAndFeel().findColour (ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons),
              app (a)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (c, true);
            setResizable (true, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { app.systemRequestedQuit(); }

    private:
        JUCEApplication& app;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION (ForgeApplication)
