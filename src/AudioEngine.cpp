#include "AudioEngine.h"

AudioEngine::AudioEngine (juce::ApplicationProperties& properties) : props (properties)
{
    formatManager.addDefaultFormats();      // aggiunge il formato VST3 (JUCE_PLUGINHOST_VST3=1)
    formats.registerBasicFormats();         // WAV, AIFF, FLAC...
    work.setSize (8, blockCapacity);
    dry.setSize (2, blockCapacity);
    playBuf.setSize (2, blockCapacity);
    midi.ensureSize (8192);
    midiTmp.ensureSize (8192);
    midiCollector.reset (44100.0);
    writerThread.startThread();
    readAhead.startThread();
}

AudioEngine::~AudioEngine() { shutdown(); }

void AudioEngine::shutdown()
{
    deviceManager.removeChangeListener (this);
    deviceManager.removeMidiInputDeviceCallback ({}, &midiCollector);
    deviceManager.removeAudioCallback (this);
    stopRecording();
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();
    for (auto& s : slots)
    {
        std::unique_ptr<juce::AudioPluginInstance> old;
        { const juce::ScopedLock sl (chainLock); old = std::move (s.plugin); }
        if (old != nullptr) old->releaseResources();
    }
    deviceManager.closeAudioDevice();
    writerThread.stopThread (2000);
    readAhead.stopThread (2000);
}

juce::String AudioEngine::start()
{
    auto saved = props.getUserSettings()->getXmlValue ("audioDevice");
    juce::String error = deviceManager.initialise (2, 2, saved.get(), true);
    if (saved == nullptr) preferAsio();

    deviceManager.addAudioCallback (this);
    deviceManager.addMidiInputDeviceCallback ({}, &midiCollector);   // tutte le tastiere MIDI attive
    deviceManager.addChangeListener (this);

    if (auto xml = props.getUserSettings()->getXmlValue ("knownPlugins"))
        knownPlugins.recreateFromXml (*xml);
    return error;
}

// al primo avvio, se esiste un driver ASIO (come quello della Yamaha) si parte con quello
void AudioEngine::preferAsio()
{
#if FBD_HAS_ASIO
    juce::String previousType;
    if (auto* current = deviceManager.getCurrentDeviceTypeObject()) previousType = current->getTypeName();
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        if (type->getTypeName() != "ASIO") continue;
        type->scanForDevices();
        auto names = type->getDeviceNames();
        if (names.isEmpty()) break;
        deviceManager.setCurrentAudioDeviceType ("ASIO", true);
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.inputDeviceName = names[0];
        setup.outputDeviceName = names[0];
        setup.useDefaultInputChannels = true;
        setup.useDefaultOutputChannels = true;
        const juce::String e = deviceManager.setAudioDeviceSetup (setup, true);
        if (e.isNotEmpty() && previousType.isNotEmpty()) deviceManager.setCurrentAudioDeviceType (previousType, true);
        break;
    }
#endif
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto xml = deviceManager.createStateXml())
    {
        props.getUserSettings()->setValue ("audioDevice", xml.get());
        props.saveIfNeeded();
    }
}

std::vector<AudioEngine::InputChoice> AudioEngine::inputChoices()
{
    std::vector<InputChoice> v;
    auto* d = deviceManager.getCurrentAudioDevice();
    if (d == nullptr) return v;
    const auto names = d->getInputChannelNames();
    const auto active = d->getActiveInputChannels();
    juce::StringArray an;
    for (int i = 0; i < names.size(); ++i) if (active[i]) an.add (names[i]);
    for (int i = 0; i + 1 < an.size(); i += 2) v.push_back (InputChoice { "Stereo: " + an[i] + " + " + an[i + 1], i, i + 1 });
    for (int i = 0; i < an.size(); ++i)         v.push_back (InputChoice { "Mono: " + an[i], i, i });
    return v;
}

juce::String AudioEngine::describeDevice()
{
    auto* d = deviceManager.getCurrentAudioDevice();
    if (d == nullptr) return TXT("Nessuna scheda audio attiva: apri «Impostazioni audio e ASIO».");
    const double sr = d->getCurrentSampleRate();
    const int bs = d->getCurrentBufferSizeSamples(), il = d->getInputLatencyInSamples(), ol = d->getOutputLatencyInSamples();
    auto ms = [sr] (int samples) { return juce::String (fbd::samplesToMs (samples, sr), 1) + " ms"; };
    return d->getTypeName() + TXT("  ·  ") + d->getName() + TXT("  ·  ") + juce::String ((int) sr) + TXT(" Hz  ·  buffer ") + juce::String (bs) + " campioni (" + ms (bs) + ")"
           + "\nLatenza dichiarata dal driver: ingresso " + ms (il) + " + uscita " + ms (ol) + " = " + ms (il + ol) + (kHasAsio ? "" : "   [ASIO non incluso in questa compilazione]");
}

// ---------------------------------------------------------------- registrazione
bool AudioEngine::startRecording (const juce::File& file, juce::String& error)
{
    if (deviceManager.getCurrentAudioDevice() == nullptr || sampleRate <= 0) { error = "Nessuna scheda audio attiva."; return false; }
    stopRecording();
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr) { error = "Non riesco a creare il file: " + file.getFullPathName(); return false; }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sampleRate, 2, 24, {}, 0));
    if (writer == nullptr) { error = "Non riesco a preparare il file WAV."; return false; }
    stream.release();   // ora lo gestisce lo scrittore
    threadedWriter.reset (new juce::AudioFormatWriter::ThreadedWriter (writer.release(), writerThread, 65536));
    samplesRecorded.store (0);
    lastRecording = file;
    const juce::ScopedLock sl (writerLock);
    activeWriter.store (threadedWriter.get());
    return true;
}

void AudioEngine::stopRecording()
{
    {
        const juce::ScopedLock sl (writerLock);
        activeWriter.store (nullptr);
    }
    threadedWriter.reset();     // svuota la coda e chiude il file
}

// ---------------------------------------------------------------- riproduzione
bool AudioEngine::loadFile (const juce::File& file, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr) { error = "Formato non riconosciuto (usa WAV, AIFF o FLAC): " + file.getFileName(); return false; }
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();
    fileChannels = (int) reader->numChannels;
    const double fileRate = reader->sampleRate;
    readerSource.reset (new juce::AudioFormatReaderSource (reader.release(), true));
    transport.setSource (readerSource.get(), 32768, &readAhead, fileRate);
    loadedFile = file;
    return true;
}

void AudioEngine::play()          { if (readerSource != nullptr) { transport.setPosition (0.0); transport.start(); } }
void AudioEngine::stopPlayback()  { transport.stop(); }

// ---------------------------------------------------------------- plugin
void AudioEngine::prepareInstance (juce::AudioPluginInstance* p)
{
    if (p == nullptr) return;
    const double sr = sampleRate > 0 ? sampleRate : 44100.0;
    const int bs = blockSize > 0 ? blockSize : 512;
    auto layout = p->getBusesLayout();
    if (p->getBusCount (true) > 0)  layout.getMainInputChannelSet()  = juce::AudioChannelSet::stereo();
    if (p->getBusCount (false) > 0) layout.getMainOutputChannelSet() = juce::AudioChannelSet::stereo();
    if (p->checkBusesLayoutSupported (layout)) p->setBusesLayout (layout);
    p->setPlayConfigDetails (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels(), sr, bs);
    p->prepareToPlay (sr, bs);
}

void AudioEngine::instantiate (int slot, const juce::PluginDescription& description, std::function<void (juce::String)> done)
{
    const double sr = sampleRate > 0 ? sampleRate : 44100.0;
    const int bs = blockSize > 0 ? blockSize : 512;
    formatManager.createPluginInstanceAsync (description, sr, bs,
        [this, slot, done] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (instance == nullptr) { done (error.isNotEmpty() ? error : juce::String ("Impossibile caricare il plugin.")); return; }
            prepareInstance (instance.get());
            std::unique_ptr<juce::AudioPluginInstance> old;
            {
                const juce::ScopedLock sl (chainLock);
                old = std::move (slots[slot].plugin);
                slots[slot].plugin = std::move (instance);
                slots[slot].bypass.store (false);
            }
            if (old != nullptr) old->releaseResources();
            done ({});
        });
}

void AudioEngine::removePlugin (int slot)
{
    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (chainLock);
        old = std::move (slots[slot].plugin);
        slots[slot].bypass.store (false);
    }
    if (old != nullptr) old->releaseResources();
}

// ---------------------------------------------------------------- scheda audio
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate = device->getCurrentSampleRate();
    blockSize = device->getCurrentBufferSizeSamples();
    midiCollector.reset (sampleRate);
    transport.prepareToPlay (blockSize, sampleRate);
    const juce::ScopedLock sl (chainLock);
    for (auto& s : slots) prepareInstance (s.plugin.get());
}

void AudioEngine::audioDeviceStopped()
{
    transport.releaseResources();
    const juce::ScopedLock sl (chainLock);
    for (auto& s : slots) if (s.plugin != nullptr) s.plugin->releaseResources();
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* in, int nIn, float* const* out, int nOut,
                                                    int n, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = 0; c < nOut; ++c)
        if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);
    if (n <= 0 || n > blockCapacity) return;

    // 1) ingresso scelto, con il suo guadagno -> "dry" (il segnale a secco)
    const int li = inL.load(), ri = inR.load();
    const float g = inputGain.load();
    const float* l = (li >= 0 && li < nIn) ? in[li] : nullptr;
    const float* r = (ri >= 0 && ri < nIn) ? in[ri] : nullptr;
    if (r == nullptr) r = l;
    float* dl = dry.getWritePointer (0);
    float* dr = dry.getWritePointer (1);
    if (l != nullptr) juce::FloatVectorOperations::copyWithMultiply (dl, l, g, n); else juce::FloatVectorOperations::clear (dl, n);
    if (r != nullptr) juce::FloatVectorOperations::copyWithMultiply (dr, r, g, n); else juce::FloatVectorOperations::clear (dr, n);

    const auto rl = juce::FloatVectorOperations::findMinAndMax (dl, n);
    const auto rr = juce::FloatVectorOperations::findMinAndMax (dr, n);
    inPeakL.store (std::max (inPeakL.load(), std::max (std::abs (rl.getStart()), std::abs (rl.getEnd()))));
    inPeakR.store (std::max (inPeakR.load(), std::max (std::abs (rr.getStart()), std::abs (rr.getEnd()))));

    // 2) registrazione del segnale a secco
    {
        const juce::ScopedTryLock tl (writerLock);
        if (tl.isLocked())
            if (auto* w = activeWriter.load())
            {
                const float* channels[2] = { dl, dr };
                w->write (channels, n);
                samplesRecorded.fetch_add (n);
            }
    }

    // 3) ascolto: l'ingresso entra nella catena dei plugin solo se "Ascolto" e' acceso (gli strumenti suonano comunque)
    float* wl = work.getWritePointer (0);
    float* wr = work.getWritePointer (1);
    if (monitor.load()) { juce::FloatVectorOperations::copy (wl, dl, n); juce::FloatVectorOperations::copy (wr, dr, n); }
    else                { juce::FloatVectorOperations::clear (wl, n);    juce::FloatVectorOperations::clear (wr, n); }

    midi.clear();
    midiCollector.removeNextBlockOfMessages (midi, n);
    {
        const juce::ScopedTryLock tl (chainLock);
        if (tl.isLocked())
        {
            for (auto& s : slots)
            {
                auto* p = s.plugin.get();
                if (p == nullptr || s.bypass.load()) continue;
                const int nch = juce::jlimit (2, work.getNumChannels(), std::max (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels()));
                for (int c = 2; c < nch; ++c) juce::FloatVectorOperations::clear (work.getWritePointer (c), n);
                juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), nch, n);
                midiTmp.clear();
                if (p->acceptsMidi()) midiTmp.addEvents (midi, 0, n, 0);
                p->processBlock (view, midiTmp);
            }
        }
    }

    // 4) uscita: catena + riproduzione, con il volume d'uscita
    const float og = outputGain.load();
    if (nOut > 0 && out[0] != nullptr) juce::FloatVectorOperations::addWithMultiply (out[0], wl, og, n);
    if (nOut > 1 && out[1] != nullptr) juce::FloatVectorOperations::addWithMultiply (out[1], wr, og, n);

    if (transport.isPlaying())
    {
        juce::AudioSourceChannelInfo info (&playBuf, 0, n);
        transport.getNextAudioBlock (info);
        if (fileChannels == 1) juce::FloatVectorOperations::copy (playBuf.getWritePointer (1), playBuf.getReadPointer (0), n);
        if (nOut > 0 && out[0] != nullptr) juce::FloatVectorOperations::addWithMultiply (out[0], playBuf.getReadPointer (0), og, n);
        if (nOut > 1 && out[1] != nullptr) juce::FloatVectorOperations::addWithMultiply (out[1], playBuf.getReadPointer (1), og, n);
    }

    if (nOut > 0 && out[0] != nullptr)
    {
        const auto a = juce::FloatVectorOperations::findMinAndMax (out[0], n);
        outPeakL.store (std::max (outPeakL.load(), std::max (std::abs (a.getStart()), std::abs (a.getEnd()))));
    }
    if (nOut > 1 && out[1] != nullptr)
    {
        const auto b = juce::FloatVectorOperations::findMinAndMax (out[1], n);
        outPeakR.store (std::max (outPeakR.load(), std::max (std::abs (b.getStart()), std::abs (b.getEnd()))));
    }
}
