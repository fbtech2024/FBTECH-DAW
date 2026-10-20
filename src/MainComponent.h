// FBTECH DAW - finestra principale (versione 0.1: banco di prova per ASIO e plugin VST3)
#pragma once
#include <JuceHeader.h>
#include "AudioEngine.h"

// misuratore orizzontale: verde, giallo, rosso; picco tenuto; avviso di sovraccarico
class LevelMeter : public juce::Component
{
public:
    void push (float peak, double dt) { meter.push (peak, dt); repaint(); }
    void paint (juce::Graphics& g) override;
private:
    fbd::Meter meter;
};

// finestra di un plugin (la sua interfaccia, oppure una generica)
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioPluginInstance& plugin, std::function<void()> onClose);
    void closeButtonPressed() override { if (closeCallback) closeCallback(); }
private:
    std::function<void()> closeCallback;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

// ricerca dei plugin VST3 in un thread a parte (se un plugin si blocca, viene ricordato e saltato la volta dopo)
class PluginScanner : public juce::Thread
{
public:
    PluginScanner (juce::AudioPluginFormatManager& fm, juce::KnownPluginList& list, const juce::File& deadMansFile,
                   std::function<void (const juce::String&)> onProgress, std::function<void()> onDone);
    ~PluginScanner() override { stopThread (8000); }
    void run() override;
private:
    juce::AudioPluginFormatManager& formats;
    juce::KnownPluginList& knownList;
    juce::File deadFile;
    std::function<void (const juce::String&)> progress;
    std::function<void()> finished;
};

class MainComponent : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void openAudioSettings();
    void refreshInputChoices();
    void startScan();
    void scanFinished();
    void refreshPluginLists();
    void choosePlugin (int slot);
    void openEditor (int slot);
    void saveSlots();
    void restoreSlots();
    void showError (const juce::String& message);
    void setStatus (const juce::String& text);
    juce::File recordingsFolder() const;

    juce::ApplicationProperties props;
    AudioEngine engine;

    juce::TextButton btnAudio { TXT("Impostazioni audio e ASIO…") }, btnScan { "Cerca plugin VST3" };
    juce::Label lblDevice, lblScan, lblStatus;

    juce::GroupComponent grpInput { {}, "Ingresso e ascolto" }, grpPlugins { {}, "Plugin VST3 (in serie, dall'alto in basso)" }, grpRec { {}, "Registrazione e riproduzione" };
    juce::Label lblInput { {}, "Ingresso" }, lblInGain { {}, "Guadagno" }, lblOut { {}, "Volume uscita" };
    juce::ComboBox cbInput;
    juce::Slider slInGain, slOut;
    juce::ToggleButton tbMonitor { "Ascolto (senti l'ingresso in uscita: usa le CUFFIE, con le casse si innesca il fischio)" };
    LevelMeter meterInL, meterInR, meterOutL, meterOutR;
    juce::Label lblMeterIn { {}, "Ingresso L / R" }, lblMeterOut { {}, "Uscita L / R" };

    juce::ComboBox cbPlug[AudioEngine::kSlots];
    juce::TextButton btnEdit[AudioEngine::kSlots], btnRemove[AudioEngine::kSlots];
    juce::ToggleButton tbBypass[AudioEngine::kSlots];
    juce::Label lblSlot[AudioEngine::kSlots];
    juce::PluginDescription slotDesc[AudioEngine::kSlots];
    std::unique_ptr<PluginWindow> editors[AudioEngine::kSlots];
    juce::Array<juce::PluginDescription> descList;

    juce::TextButton btnRec { TXT("● Registra") }, btnStopRec { TXT("■ Ferma") }, btnRevealRec { "Mostra il file" };
    juce::TextButton btnLoad { TXT("Apri un file audio…") }, btnPlay { TXT("▶ Riproduci") }, btnStopPlay { TXT("■ Ferma") };
    juce::Label lblRec, lblPlay;

    std::vector<AudioEngine::InputChoice> inputChoices;
    std::unique_ptr<PluginScanner> scanner;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String lastDeviceText;
    int tick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
