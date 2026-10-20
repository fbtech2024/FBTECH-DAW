// FBTECH DAW - motore audio (scheda audio, ASIO, ascolto, registrazione, riproduzione, plugin VST3).
// Tutto cio' che gira nel "callback" audio e' scritto per non bloccarsi: niente allocazioni, niente attese lunghe.
#pragma once
#include <JuceHeader.h>
#include "core/Levels.h"

// Testo con accenti e simboli: in JUCE i letterali "..." devono essere ASCII, per l'UTF-8 si usa fromUTF8 (e il compilatore legge i file come UTF-8: vedi CMakeLists.txt)
#define TXT(x) juce::String::fromUTF8 (x)

class AudioEngine : public juce::AudioIODeviceCallback, private juce::ChangeListener
{
public:
    static constexpr int kSlots = 4;
    static constexpr bool kHasAsio = (FBD_HAS_ASIO != 0);

    explicit AudioEngine (juce::ApplicationProperties& properties);
    ~AudioEngine() override;

    juce::String start();      // apre la scheda audio (come l'ultima volta, o ASIO se c'e'); restituisce un errore o ""
    void shutdown();

    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    // ---- ingresso, ascolto, uscita
    struct InputChoice { juce::String label; int left; int right; };
    std::vector<InputChoice> inputChoices();            // coppie stereo e canali mono tra quelli attivi
    void setInputChoice (const InputChoice& c)          { inL.store (c.left); inR.store (c.right); }
    void setMonitor (bool on)                           { monitor.store (on); }
    void setInputGainDb (float db)                      { inputGain.store (fbd::dbToGain (db)); }
    void setOutputGainDb (float db)                     { outputGain.store (fbd::dbToGain (db)); }
    juce::String describeDevice();

    // ---- registrazione (WAV 24 bit stereo, il segnale in ingresso "a secco")
    bool startRecording (const juce::File& file, juce::String& error);
    void stopRecording();
    bool isRecording() const                            { return activeWriter.load() != nullptr; }
    double recordedSeconds() const                      { return sampleRate > 0 ? (double) samplesRecorded.load() / sampleRate : 0.0; }
    juce::File lastRecording;

    // ---- riproduzione di un file audio
    bool loadFile (const juce::File& file, juce::String& error);
    void play();
    void stopPlayback();
    bool isPlaying()                                    { return transport.isPlaying(); }
    double playPosition()                               { return transport.getCurrentPosition(); }
    double playLength()                                 { return transport.getLengthInSeconds(); }
    juce::File loadedFile;

    // ---- plugin VST3 (4 posti in serie)
    void instantiate (int slot, const juce::PluginDescription& description, std::function<void (juce::String)> done);
    void removePlugin (int slot);
    juce::AudioPluginInstance* pluginAt (int slot)      { return slots[slot].plugin.get(); }
    void setBypass (int slot, bool bypassed)            { slots[slot].bypass.store (bypassed); }

    // ---- livelli (la finestra li legge e li azzera)
    std::atomic<float> inPeakL { 0.f }, inPeakR { 0.f }, outPeakL { 0.f }, outPeakR { 0.f };

    // ---- AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void preferAsio();
    void prepareInstance (juce::AudioPluginInstance* p);

    struct Slot { std::unique_ptr<juce::AudioPluginInstance> plugin; std::atomic<bool> bypass { false }; };

    juce::ApplicationProperties& props;
    std::atomic<bool> monitor { false };
    std::atomic<float> inputGain { 1.f }, outputGain { 1.f };
    std::atomic<int> inL { 0 }, inR { 0 };
    double sampleRate = 0.0;
    int blockSize = 0, blockCapacity = 16384, fileChannels = 2;

    juce::AudioBuffer<float> work, dry, playBuf;
    juce::MidiMessageCollector midiCollector;
    juce::MidiBuffer midi, midiTmp;
    juce::CriticalSection chainLock;
    Slot slots[kSlots];

    juce::TimeSliceThread writerThread { "FBTECH scrittura audio" };
    juce::CriticalSection writerLock;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter { nullptr };
    std::atomic<juce::int64> samplesRecorded { 0 };

    juce::AudioFormatManager formats;
    juce::TimeSliceThread readAhead { "FBTECH lettura audio" };
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
