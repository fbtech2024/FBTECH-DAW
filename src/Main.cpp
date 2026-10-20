// FBTECH DAW - avvio del programma
#include <JuceHeader.h>
#include "MainComponent.h"

class FBTechDawApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "FBTECH DAW"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String&) override
    {
        laf.setColourScheme (juce::LookAndFeel_V4::getMidnightColourScheme());
        juce::LookAndFeel::setDefaultLookAndFeel (&laf);
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override { if (mainWindow != nullptr) mainWindow->toFront (true); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : juce::DocumentWindow (name, juce::Colour (0xff0b1620), juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (900, 640, 4000, 3000);
            centreWithSize (1040, 760);
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    juce::LookAndFeel_V4 laf;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (FBTechDawApplication)
