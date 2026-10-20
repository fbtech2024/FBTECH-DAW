#include "MainComponent.h"

// ====================================================================== misuratore
void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0b1620));
    g.fillRoundedRectangle (r, 3.0f);
    auto bar = r.reduced (1.0f);

    juce::ColourGradient grad (juce::Colour (0xff2bd67b), bar.getX(), 0.0f, juce::Colour (0xffff4f6d), bar.getRight(), 0.0f, false);
    grad.addColour (0.70, juce::Colour (0xff2bd67b));
    grad.addColour (0.85, juce::Colour (0xffffd24d));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * fbd::dbToFraction (meter.db)), 2.0f);

    g.setColour (juce::Colours::white);
    g.fillRect (bar.getX() + bar.getWidth() * fbd::dbToFraction (meter.holdDb) - 1.0f, bar.getY(), 2.0f, bar.getHeight());

    g.setColour (juce::Colours::white.withAlpha (0.35f));   // segno dello 0 dB
    g.fillRect (bar.getX() + bar.getWidth() * fbd::dbToFraction (0.0f), bar.getY(), 1.0f, bar.getHeight());

    if (meter.clipping())
    {
        g.setColour (juce::Colour (0xffff4f6d));
        g.fillRoundedRectangle (r.removeFromRight (12.0f).reduced (1.0f), 2.0f);
    }
}

// ====================================================================== finestra del plugin
PluginWindow::PluginWindow (juce::AudioPluginInstance& plugin, std::function<void()> onClose)
    : juce::DocumentWindow (plugin.getName(), juce::Colours::darkgrey, juce::DocumentWindow::closeButton), closeCallback (std::move (onClose))
{
    setUsingNativeTitleBar (true);
    juce::AudioProcessorEditor* editor = plugin.hasEditor() ? plugin.createEditorIfNeeded() : nullptr;
    if (editor == nullptr) editor = new juce::GenericAudioProcessorEditor (plugin);
    setContentOwned (editor, true);
    setResizable (editor->isResizable(), false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

// ====================================================================== ricerca plugin
PluginScanner::PluginScanner (juce::AudioPluginFormatManager& fm, juce::KnownPluginList& list, const juce::File& deadMansFile,
                              std::function<void (const juce::String&)> onProgress, std::function<void()> onDone)
    : juce::Thread ("FBTECH ricerca plugin"), formats (fm), knownList (list), deadFile (deadMansFile),
      progress (std::move (onProgress)), finished (std::move (onDone)) {}

void PluginScanner::run()
{
    for (int i = 0; i < formats.getNumFormats() && ! threadShouldExit(); ++i)
    {
        auto* format = formats.getFormat (i);
        if (format == nullptr || format->getName() != "VST3") continue;
        juce::PluginDirectoryScanner scanner (knownList, *format, format->getDefaultLocationsToSearch(), true, deadFile);
        juce::String name;
        while (! threadShouldExit() && scanner.scanNextFile (true, name))
        {
            auto cb = progress; const juce::String n = name;
            juce::MessageManager::callAsync ([cb, n] { if (cb) cb (n); });
        }
    }
    auto done = finished;
    juce::MessageManager::callAsync ([done] { if (done) done(); });
}

// ====================================================================== finestra principale
MainComponent::MainComponent() : engine (props)
{
    juce::PropertiesFile::Options options;
    options.applicationName = "FBTECH DAW";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName = "FBTECH DAW";
    props.setStorageParameters (options);

    for (auto* c : std::initializer_list<juce::Component*> { &btnAudio, &btnScan, &lblDevice, &lblScan, &lblStatus, &grpInput, &grpPlugins, &grpRec,
                                                              &lblInput, &lblInGain, &lblOut, &cbInput, &slInGain, &slOut, &tbMonitor,
                                                              &meterInL, &meterInR, &meterOutL, &meterOutR, &lblMeterIn, &lblMeterOut,
                                                              &btnRec, &btnStopRec, &btnRevealRec, &btnLoad, &btnPlay, &btnStopPlay, &lblRec, &lblPlay })
        addAndMakeVisible (c);

    btnAudio.setTooltip ("Scegli il driver (ASIO, WASAPI...), la scheda, la frequenza, la dimensione del buffer e i canali");
    btnAudio.onClick = [this] { openAudioSettings(); };
    btnScan.onClick = [this] { startScan(); };

    lblDevice.setJustificationType (juce::Justification::topLeft);
    lblDevice.setColour (juce::Label::textColourId, juce::Colour (0xff9fe9ff));
    lblScan.setColour (juce::Label::textColourId, juce::Colour (0xff8fb4c9));
    lblStatus.setColour (juce::Label::textColourId, juce::Colour (0xffffd24d));
    lblStatus.setJustificationType (juce::Justification::centredLeft);

    slInGain.setSliderStyle (juce::Slider::LinearHorizontal);
    slInGain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
    slInGain.setRange (-24.0, 24.0, 0.5);
    slInGain.setTextValueSuffix (" dB");
    slInGain.setValue (0.0, juce::dontSendNotification);
    slInGain.onValueChange = [this] { engine.setInputGainDb ((float) slInGain.getValue()); };
    slInGain.setTitle ("Guadagno d'ingresso");
    slOut.setSliderStyle (juce::Slider::LinearHorizontal);
    slOut.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
    slOut.setRange (-60.0, 6.0, 0.5);
    slOut.setTextValueSuffix (" dB");
    slOut.setValue (0.0, juce::dontSendNotification);
    slOut.onValueChange = [this] { engine.setOutputGainDb ((float) slOut.getValue()); };
    slOut.setTitle ("Volume di uscita");

    cbInput.setTextWhenNothingSelected ("(nessun ingresso attivo)");
    cbInput.onChange = [this]
    {
        const int i = cbInput.getSelectedId() - 1;
        if (i >= 0 && i < (int) inputChoices.size()) engine.setInputChoice (inputChoices[(size_t) i]);
    };
    tbMonitor.onClick = [this] { engine.setMonitor (tbMonitor.getToggleState()); };

    for (int s = 0; s < AudioEngine::kSlots; ++s)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &cbPlug[s], &btnEdit[s], &btnRemove[s], &tbBypass[s], &lblSlot[s] }) addAndMakeVisible (c);
        lblSlot[s].setText ("Posto " + juce::String (s + 1), juce::dontSendNotification);
        cbPlug[s].setTextWhenNothingSelected ("(vuoto)");
        cbPlug[s].onChange = [this, s] { choosePlugin (s); };
        btnEdit[s].setButtonText ("Apri");
        btnEdit[s].onClick = [this, s] { openEditor (s); };
        btnRemove[s].setButtonText ("Togli");
        btnRemove[s].onClick = [this, s] { cbPlug[s].setSelectedId (1, juce::sendNotificationSync); };
        tbBypass[s].setButtonText ("Escludi");
        tbBypass[s].onClick = [this, s] { engine.setBypass (s, tbBypass[s].getToggleState()); saveSlots(); };
    }

    btnRec.onClick = [this]
    {
        if (engine.isRecording()) return;
        const auto t = juce::Time::getCurrentTime();
        const auto file = recordingsFolder().getChildFile (juce::String (fbd::recordingName (t.getYear(), t.getMonth() + 1, t.getDayOfMonth(), t.getHours(), t.getMinutes(), t.getSeconds())));
        juce::String error;
        if (! engine.startRecording (file, error)) { showError (error); return; }
        setStatus (TXT("Registrazione in corso…"));
    };
    btnStopRec.onClick = [this]
    {
        if (! engine.isRecording()) return;
        engine.stopRecording();
        setStatus ("Registrazione salvata.");
    };
    btnRevealRec.onClick = [this]
    {
        if (engine.lastRecording.existsAsFile()) { engine.lastRecording.revealToUser(); return; }
        recordingsFolder().createDirectory();
        recordingsFolder().revealToUser();
    };
    btnLoad.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Scegli un file audio", engine.lastRecording.exists() ? engine.lastRecording.getParentDirectory() : recordingsFolder(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            juce::String error;
            if (! engine.loadFile (f, error)) { showError (error); return; }
            setStatus ("Caricato: " + f.getFileName());
        });
    };
    btnPlay.onClick = [this]
    {
        if (engine.loadedFile == juce::File() && engine.lastRecording.existsAsFile())
        {
            juce::String error;
            if (! engine.loadFile (engine.lastRecording, error)) { showError (error); return; }
        }
        if (engine.loadedFile == juce::File()) { showError ("Prima registra qualcosa oppure apri un file audio."); return; }
        engine.play();
    };
    btnStopPlay.onClick = [this] { engine.stopPlayback(); };
    lblRec.setColour (juce::Label::textColourId, juce::Colour (0xff9fe9ff));
    lblPlay.setColour (juce::Label::textColourId, juce::Colour (0xff9fe9ff));

    setSize (1040, 760);
    setStatus ("");

    const juce::String error = engine.start();
    if (error.isNotEmpty()) setStatus ("Scheda audio: " + error);
    engine.deviceManager.addChangeListener (this);
    refreshInputChoices();
    refreshPluginLists();
    restoreSlots();
    lblScan.setText ("Plugin VST3 conosciuti: " + juce::String (descList.size()) + (descList.isEmpty() ? TXT("  (premi «Cerca plugin VST3»)") : ""), juce::dontSendNotification);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.deviceManager.removeChangeListener (this);
    scanner.reset();
    saveSlots();
    for (auto& e : editors) e.reset();
    engine.shutdown();
}

juce::File MainComponent::recordingsFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("FBTECH DAW").getChildFile ("Registrazioni");
}

void MainComponent::showError (const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "FBTECH DAW", message);
}

void MainComponent::setStatus (const juce::String& text) { lblStatus.setText (text, juce::dontSendNotification); }

void MainComponent::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff0b1620)); }

void MainComponent::resized()
{
    auto a = getLocalBounds().reduced (14);
    auto top = a.removeFromTop (34);
    btnAudio.setBounds (top.removeFromLeft (250));
    top.removeFromLeft (10);
    btnScan.setBounds (top.removeFromLeft (170));
    top.removeFromLeft (12);
    lblScan.setBounds (top);
    a.removeFromTop (6);
    lblDevice.setBounds (a.removeFromTop (44));
    lblStatus.setBounds (a.removeFromTop (22));
    a.removeFromTop (4);

    auto gi = a.removeFromTop (190);
    grpInput.setBounds (gi);
    auto in = gi.reduced (14, 22);
    {
        auto row = in.removeFromTop (28); lblInput.setBounds (row.removeFromLeft (90)); cbInput.setBounds (row);
        in.removeFromTop (6);
        row = in.removeFromTop (28); lblInGain.setBounds (row.removeFromLeft (90)); slInGain.setBounds (row);
        in.removeFromTop (6);
        row = in.removeFromTop (28); lblOut.setBounds (row.removeFromLeft (90)); slOut.setBounds (row);
        in.removeFromTop (6);
        tbMonitor.setBounds (in.removeFromTop (26));
        in.removeFromTop (6);
        auto m = in.removeFromTop (18);
        lblMeterIn.setBounds (m.removeFromLeft (110)); meterInL.setBounds (m.removeFromLeft (m.getWidth() / 2 - 8).reduced (0, 2)); m.removeFromLeft (8); meterInR.setBounds (m.reduced (0, 2));
    }
    a.removeFromTop (8);

    auto gp = a.removeFromTop (AudioEngine::kSlots * 36 + 56);
    grpPlugins.setBounds (gp);
    auto pl = gp.reduced (14, 22);
    for (int s = 0; s < AudioEngine::kSlots; ++s)
    {
        auto row = pl.removeFromTop (32); pl.removeFromTop (4);
        lblSlot[s].setBounds (row.removeFromLeft (64));
        btnRemove[s].setBounds (row.removeFromRight (70)); row.removeFromRight (6);
        tbBypass[s].setBounds (row.removeFromRight (90)); row.removeFromRight (6);
        btnEdit[s].setBounds (row.removeFromRight (70)); row.removeFromRight (6);
        cbPlug[s].setBounds (row);
    }
    {
        auto m = pl.removeFromTop (18);
        lblMeterOut.setBounds (m.removeFromLeft (110)); meterOutL.setBounds (m.removeFromLeft (m.getWidth() / 2 - 8).reduced (0, 2)); m.removeFromLeft (8); meterOutR.setBounds (m.reduced (0, 2));
    }
    a.removeFromTop (8);

    auto gr = a.removeFromTop (130);
    grpRec.setBounds (gr);
    auto rc = gr.reduced (14, 22);
    {
        auto row = rc.removeFromTop (32);
        btnRec.setBounds (row.removeFromLeft (130)); row.removeFromLeft (8); btnStopRec.setBounds (row.removeFromLeft (110)); row.removeFromLeft (8); btnRevealRec.setBounds (row.removeFromLeft (150)); row.removeFromLeft (12); lblRec.setBounds (row);
        rc.removeFromTop (10);
        row = rc.removeFromTop (32);
        btnLoad.setBounds (row.removeFromLeft (190)); row.removeFromLeft (8); btnPlay.setBounds (row.removeFromLeft (130)); row.removeFromLeft (8); btnStopPlay.setBounds (row.removeFromLeft (110)); row.removeFromLeft (12); lblPlay.setBounds (row);
    }
}

// ====================================================================== scheda audio e ASIO
void MainComponent::openAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.deviceManager, 0, 256, 0, 256, true, false, true, false);
    selector->setSize (580, 540);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Impostazioni audio e ASIO";
    o.componentToCentreAround = this;
    o.dialogBackgroundColour = juce::Colour (0xff0b1620);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::refreshInputChoices()
{
    inputChoices = engine.inputChoices();
    cbInput.clear (juce::dontSendNotification);
    int firstMono = 0;
    for (size_t i = 0; i < inputChoices.size(); ++i)
    {
        cbInput.addItem (inputChoices[i].label, (int) i + 1);
        if (firstMono == 0 && inputChoices[i].left == inputChoices[i].right) firstMono = (int) i;
    }
    if (! inputChoices.empty())
    {
        cbInput.setSelectedId (firstMono + 1, juce::dontSendNotification);   // di partenza: il primo ingresso in mono
        engine.setInputChoice (inputChoices[(size_t) firstMono]);
    }
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshInputChoices();
    lastDeviceText = {};
}

// ====================================================================== plugin
void MainComponent::refreshPluginLists()
{
    descList.clear();
    for (const auto& d : engine.knownPlugins.getTypes())
        if (d.pluginFormatName == "VST3") descList.add (d);
    for (int s = 0; s < AudioEngine::kSlots; ++s)
    {
        const int keep = cbPlug[s].getSelectedId();
        cbPlug[s].clear (juce::dontSendNotification);
        cbPlug[s].addItem ("(vuoto)", 1);
        for (int i = 0; i < descList.size(); ++i)
            cbPlug[s].addItem (descList[i].name + TXT("  —  ") + descList[i].manufacturerName + (descList[i].isInstrument ? "  [strumento]" : ""), i + 2);
        cbPlug[s].setSelectedId (keep > 0 ? keep : 1, juce::dontSendNotification);
    }
}

void MainComponent::startScan()
{
    if (scanner != nullptr && scanner->isThreadRunning()) return;
    const auto dead = props.getUserSettings()->getFile().getSiblingFile ("scansione-in-corso.txt");
    juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (engine.knownPlugins, dead);
    btnScan.setEnabled (false);
    lblScan.setText (TXT("Cerco i plugin VST3…"), juce::dontSendNotification);
    juce::Component::SafePointer<MainComponent> safe (this);
    scanner = std::make_unique<PluginScanner> (engine.formatManager, engine.knownPlugins, dead,
        [safe] (const juce::String& name) { if (safe != nullptr) safe->lblScan.setText ("Controllo: " + juce::File (name).getFileName(), juce::dontSendNotification); },
        [safe] { if (safe != nullptr) safe->scanFinished(); });
    scanner->startThread();
}

void MainComponent::scanFinished()
{
    engine.knownPlugins.sort (juce::KnownPluginList::sortAlphabetically, true);
    if (auto xml = engine.knownPlugins.createXml())
    {
        props.getUserSettings()->setValue ("knownPlugins", xml.get());
        props.saveIfNeeded();
    }
    refreshPluginLists();
    btnScan.setEnabled (true);
    lblScan.setText ("Plugin VST3 trovati: " + juce::String (descList.size()), juce::dontSendNotification);
}

void MainComponent::choosePlugin (int s)
{
    const int id = cbPlug[s].getSelectedId();
    editors[s].reset();
    if (id <= 1)
    {
        engine.removePlugin (s);
        slotDesc[s] = juce::PluginDescription();
        tbBypass[s].setToggleState (false, juce::dontSendNotification);
        saveSlots();
        return;
    }
    const int idx = id - 2;
    if (idx < 0 || idx >= descList.size()) return;
    const juce::PluginDescription desc = descList[idx];
    setStatus (TXT("Carico «") + desc.name + TXT("»…"));
    juce::Component::SafePointer<MainComponent> safe (this);
    engine.instantiate (s, desc, [safe, s, desc] (juce::String error)
    {
        if (safe == nullptr) return;
        if (error.isNotEmpty())
        {
            safe->cbPlug[s].setSelectedId (1, juce::dontSendNotification);
            safe->setStatus ("");
            safe->showError (TXT("Non riesco a caricare «") + desc.name + TXT("»:\n") + error);
            return;
        }
        safe->slotDesc[s] = desc;
        safe->tbBypass[s].setToggleState (false, juce::dontSendNotification);
        safe->setStatus (TXT("«") + desc.name + TXT("» caricato nel posto ") + juce::String (s + 1) + ".");
        safe->saveSlots();
    });
}

void MainComponent::openEditor (int s)
{
    auto* p = engine.pluginAt (s);
    if (p == nullptr) { showError ("Il posto " + juce::String (s + 1) + " e' vuoto: scegli prima un plugin."); return; }
    if (editors[s] != nullptr) { editors[s]->toFront (true); return; }
    juce::Component::SafePointer<MainComponent> safe (this);
    editors[s] = std::make_unique<PluginWindow> (*p, [safe, s]
    {
        juce::MessageManager::callAsync ([safe, s] { if (safe != nullptr) { safe->editors[s].reset(); safe->saveSlots(); } });
    });
}

void MainComponent::saveSlots()
{
    juce::XmlElement root ("SLOTS");
    for (int s = 0; s < AudioEngine::kSlots; ++s)
    {
        auto* p = engine.pluginAt (s);
        if (p == nullptr) continue;
        auto* e = root.createNewChildElement ("SLOT");
        e->setAttribute ("index", s);
        e->setAttribute ("bypass", tbBypass[s].getToggleState());
        juce::MemoryBlock state;
        p->getStateInformation (state);
        e->setAttribute ("state", state.toBase64Encoding());
        if (auto d = slotDesc[s].createXml()) e->addChildElement (d.release());
    }
    props.getUserSettings()->setValue ("slots", &root);
    props.saveIfNeeded();
}

void MainComponent::restoreSlots()
{
    auto xml = props.getUserSettings()->getXmlValue ("slots");
    if (xml == nullptr) return;
    juce::Component::SafePointer<MainComponent> safe (this);
    for (auto* e : xml->getChildIterator())
    {
        const int s = e->getIntAttribute ("index", -1);
        auto* descXml = e->getChildElement (0);
        if (s < 0 || s >= AudioEngine::kSlots || descXml == nullptr) continue;
        juce::PluginDescription desc;
        if (! desc.loadFromXml (*descXml)) continue;
        const juce::String state = e->getStringAttribute ("state");
        const bool bypass = e->getBoolAttribute ("bypass");
        engine.instantiate (s, desc, [safe, s, desc, state, bypass] (juce::String error)
        {
            if (safe == nullptr || error.isNotEmpty()) return;
            safe->slotDesc[s] = desc;
            if (auto* p = safe->engine.pluginAt (s))
            {
                juce::MemoryBlock mb;
                if (state.isNotEmpty() && mb.fromBase64Encoding (state)) p->setStateInformation (mb.getData(), (int) mb.getSize());
            }
            safe->tbBypass[s].setToggleState (bypass, juce::dontSendNotification);
            safe->engine.setBypass (s, bypass);
            for (int i = 0; i < safe->descList.size(); ++i)
                if (safe->descList[i].isDuplicateOf (desc)) { safe->cbPlug[s].setSelectedId (i + 2, juce::dontSendNotification); break; }
        });
    }
}

// ====================================================================== orologio (30 volte al secondo)
void MainComponent::timerCallback()
{
    const double dt = 1.0 / 30.0;
    meterInL.push (engine.inPeakL.exchange (0.0f), dt);
    meterInR.push (engine.inPeakR.exchange (0.0f), dt);
    meterOutL.push (engine.outPeakL.exchange (0.0f), dt);
    meterOutR.push (engine.outPeakR.exchange (0.0f), dt);

    const bool recording = engine.isRecording();
    btnRec.setEnabled (! recording);
    btnStopRec.setEnabled (recording);
    if (recording)
        lblRec.setText (TXT("● ") + juce::String (fbd::formatTime (engine.recordedSeconds())) + "   " + engine.lastRecording.getFileName(), juce::dontSendNotification);
    else if (engine.lastRecording != juce::File())
        lblRec.setText ("Ultima registrazione: " + engine.lastRecording.getFullPathName(), juce::dontSendNotification);
    else
        lblRec.setText ("Il segnale registrato e' quello in ingresso (a secco), in WAV 24 bit stereo, in Documenti/FBTECH DAW/Registrazioni.", juce::dontSendNotification);

    const bool playing = engine.isPlaying();
    btnStopPlay.setEnabled (playing);
    if (engine.loadedFile != juce::File())
        lblPlay.setText ((playing ? TXT("▶ ") : "") + engine.loadedFile.getFileName() + "   " + juce::String (fbd::formatTime (engine.playPosition())) + " / " + juce::String (fbd::formatTime (engine.playLength())), juce::dontSendNotification);
    else
        lblPlay.setText ("Nessun file caricato.", juce::dontSendNotification);

    if (++tick % 10 == 0)
    {
        const juce::String t = engine.describeDevice();
        if (t != lastDeviceText) { lastDeviceText = t; lblDevice.setText (t, juce::dontSendNotification); }
    }
}
