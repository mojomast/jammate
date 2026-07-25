#include "AudioOverlay.h"

#include "LookAndFeel.h"
#include "PluginProcessor.h"

//==============================================================================
namespace
{
constexpr int headerH = 70;      // mockup .audio-head
constexpr int actionsH = 58;     // mockup .audio-actions
constexpr int healthColW = 340;  // mockup .audio-layout right column
constexpr int cardPad = 16;
constexpr int comboH = 34, comboRowStride = 64, comboGap = 14;

const char* const comboLabels[6] =
{
    "DRIVER TYPE", "AUDIO DEVICE", "SAMPLE RATE",
    "BUFFER SIZE", "GUITAR INPUT", "MAIN OUTPUT"
};

juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

// " · " separator used throughout the mockup's mono captions
juce::String dot() { return utf8 (" \xc2\xb7 "); }
} // namespace

//==============================================================================
AudioOverlay::AudioOverlay (GuitarRigNAMProcessor& p, juce::AudioDeviceManager* adm)
    : processor (p), deviceManager (adm), hostMode (adm == nullptr)
{
    setOpaque (false);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (closeButton);
    closeButton.getProperties().set ("ghost", true);
    closeButton.onClick = [this] { close(); };

    for (auto* box : comboGrid())
    {
        styleCombo (*box);
        addAndMakeVisible (*box);
        box->setVisible (! hostMode);
    }

    driverBox.onChange = [this]
    {
        if (rebuilding) return;
        pending.type = driverBox.getText();
        pending.device.clear();          // resolved by rebuildDeviceBox below
        pending.inputChannel = -1;
        pending.outputPair = -1;
        rebuildDeviceBox();
        rebuildRateAndBufferBoxes();
        rebuildChannelBoxes();
        updateActionButtons();
    };
    deviceBox.onChange = [this]
    {
        if (rebuilding) return;
        pending.device = deviceBox.getText();
        pending.inputChannel = -1;
        pending.outputPair = -1;
        rebuildRateAndBufferBoxes();
        rebuildChannelBoxes();
        updateActionButtons();
    };
    rateBox.onChange = [this]
    {
        if (rebuilding) return;
        const int idx = rateBox.getSelectedId() - 1;
        if (juce::isPositiveAndBelow (idx, rateValues.size()))
            pending.sampleRate = rateValues[idx];
        rebuildRateAndBufferBoxes();     // buffer captions show ms at this rate
        updateActionButtons();
    };
    bufferBox.onChange = [this]
    {
        if (rebuilding) return;
        const int idx = bufferBox.getSelectedId() - 1;
        if (juce::isPositiveAndBelow (idx, bufferValues.size()))
            pending.bufferSize = bufferValues[idx];
        updateActionButtons();
    };
    inputChanBox.onChange = [this]
    {
        if (rebuilding) return;
        pending.inputChannel = inputChanBox.getSelectedId() - 2;   // id 1 = default
        updateActionButtons();
    };
    outputChanBox.onChange = [this]
    {
        if (rebuilding) return;
        pending.outputPair = outputChanBox.getSelectedId() - 2;    // id 1 = default
        updateActionButtons();
    };

    addAndMakeVisible (cancelBtn);
    cancelBtn.setVisible (! hostMode);
    cancelBtn.onClick = [this] { cancelPending(); };

    addAndMakeVisible (applyBtn);
    applyBtn.getProperties().set ("accent", true);
    applyBtn.setVisible (! hostMode);
    applyBtn.onClick = [this] { applyPending(); };

    addAndMakeVisible (panelBtn);
    panelBtn.setVisible (false);         // shown when the device has a panel
    panelBtn.onClick = [this]
    {
        if (deviceManager == nullptr) return;
        if (auto* dev = deviceManager->getCurrentAudioDevice())
            if (dev->hasControlPanel() && dev->showControlPanel())
                deviceManager->restartLastAudioDevice();   // panel changed things
    };

    if (! hostMode)
    {
        captureCurrent();
        rebuildAll();
    }
    updateActionButtons();
}

AudioOverlay::~AudioOverlay() = default;

std::array<juce::ComboBox*, 6> AudioOverlay::comboGrid()
{
    return { &driverBox, &deviceBox, &rateBox, &bufferBox, &inputChanBox, &outputChanBox };
}

void AudioOverlay::styleCombo (juce::ComboBox& box)
{
    box.setColour (juce::ComboBox::backgroundColourId, ui::chainBottom);
    box.setColour (juce::ComboBox::outlineColourId, ui::border());
    box.setColour (juce::ComboBox::textColourId, ui::text);
    box.setColour (juce::ComboBox::arrowColourId, ui::textDim);
}

//==============================================================================
void AudioOverlay::open()
{
    if (! hostMode)
    {
        bannerError.clear();
        captureCurrent();                // discard any stale pending edits
        rebuildAll();
        updateActionButtons();
    }
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
    repaint();
}

void AudioOverlay::close()
{
    setVisible (false);
    if (onClose != nullptr)
        onClose();
}

bool AudioOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    return false;
}

void AudioOverlay::visibilityChanged()
{
    if (isVisible())
    {
        refreshHealth();
        startTimerHz (10);
    }
    else
    {
        stopTimer();
    }
}

//==============================================================================
// Staging model
//==============================================================================
AudioOverlay::PendingSetup AudioOverlay::snapshotFromManager() const
{
    PendingSetup s;
    if (deviceManager == nullptr)
        return s;

    s.type = deviceManager->getCurrentAudioDeviceType();
    const auto setup = deviceManager->getAudioDeviceSetup();
    auto* dev = deviceManager->getCurrentAudioDevice();

    s.device = setup.outputDeviceName.isNotEmpty() ? setup.outputDeviceName
             : (dev != nullptr ? dev->getName() : setup.inputDeviceName);
    s.sampleRate = dev != nullptr ? dev->getCurrentSampleRate() : setup.sampleRate;
    s.bufferSize = dev != nullptr ? dev->getCurrentBufferSizeSamples() : setup.bufferSize;

    if (! setup.useDefaultInputChannels)
        s.inputChannel = setup.inputChannels.findNextSetBit (0);   // -1 if none
    if (! setup.useDefaultOutputChannels)
    {
        const int firstOut = setup.outputChannels.findNextSetBit (0);
        s.outputPair = firstOut >= 0 ? firstOut / 2 : -1;
    }
    return s;
}

void AudioOverlay::captureCurrent()
{
    pending = applied = snapshotFromManager();
}

bool AudioOverlay::pendingMatchesOpenDevice() const
{
    if (deviceManager == nullptr)
        return false;
    auto* dev = deviceManager->getCurrentAudioDevice();
    return dev != nullptr
        && pending.type == deviceManager->getCurrentAudioDeviceType()
        && pending.device == dev->getName();
}

juce::AudioIODeviceType* AudioOverlay::findTypeObject (const juce::String& name) const
{
    if (deviceManager == nullptr)
        return nullptr;
    for (auto* t : deviceManager->getAvailableDeviceTypes())
        if (t->getTypeName() == name)
            return t;
    return nullptr;
}

void AudioOverlay::rebuildAll()
{
    rebuildDriverBox();
    rebuildDeviceBox();
    rebuildRateAndBufferBoxes();
    rebuildChannelBoxes();
    refreshHealth();
    repaint();
}

void AudioOverlay::rebuildDriverBox()
{
    if (deviceManager == nullptr) return;
    const juce::ScopedValueSetter<bool> svs (rebuilding, true);

    driverBox.clear (juce::dontSendNotification);
    int id = 1, selected = 0;
    for (auto* t : deviceManager->getAvailableDeviceTypes())
    {
        driverBox.addItem (t->getTypeName(), id);
        if (t->getTypeName() == pending.type)
            selected = id;
        ++id;
    }
    if (selected == 0 && driverBox.getNumItems() > 0)
    {
        selected = 1;
        pending.type = driverBox.getItemText (0);
    }
    driverBox.setSelectedId (selected, juce::dontSendNotification);
    driverBox.setEnabled (driverBox.getNumItems() > 1);
}

void AudioOverlay::rebuildDeviceBox()
{
    if (deviceManager == nullptr) return;
    const juce::ScopedValueSetter<bool> svs (rebuilding, true);

    deviceBox.clear (juce::dontSendNotification);
    auto* type = findTypeObject (pending.type);
    if (type == nullptr)
    {
        deviceBox.setEnabled (false);
        return;
    }
    type->scanForDevices();
    const auto names = type->getDeviceNames();   // output side (= device for ASIO)

    if (pending.device.isEmpty() || ! names.contains (pending.device))
    {
        auto* dev = deviceManager->getCurrentAudioDevice();
        if (dev != nullptr && deviceManager->getCurrentAudioDeviceType() == pending.type)
            pending.device = dev->getName();
        if (! names.contains (pending.device))
            pending.device = names.isEmpty() ? juce::String() : names[0];
    }

    for (int i = 0; i < names.size(); ++i)
        deviceBox.addItem (names[i], i + 1);
    deviceBox.setSelectedId (names.indexOf (pending.device) + 1, juce::dontSendNotification);
    deviceBox.setEnabled (names.size() > 0);
}

void AudioOverlay::rebuildRateAndBufferBoxes()
{
    if (deviceManager == nullptr) return;
    const juce::ScopedValueSetter<bool> svs (rebuilding, true);

    rateValues.clear();
    bufferValues.clear();
    rateBox.clear (juce::dontSendNotification);
    bufferBox.clear (juce::dontSendNotification);

    if (pendingMatchesOpenDevice())
    {
        auto* dev = deviceManager->getCurrentAudioDevice();
        rateValues = dev->getAvailableSampleRates();
        bufferValues = dev->getAvailableBufferSizes();
    }
    if (rateValues.isEmpty())
    {
        // pending device is not the open one: offer the common rates and let
        // setAudioDeviceSetup snap to the closest supported value on Apply
        for (double r : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
            rateValues.add (r);
    }
    if (bufferValues.isEmpty())
        for (int b : { 32, 64, 128, 256, 512, 1024, 2048 })
            bufferValues.add (b);

    if (pending.sampleRate <= 0.0)
        pending.sampleRate = rateValues.contains (48000.0) ? 48000.0 : rateValues[0];
    if (pending.bufferSize <= 0)
        pending.bufferSize = bufferValues.contains (128) ? 128 : bufferValues[0];

    int bestRate = 0;
    for (int i = 1; i < rateValues.size(); ++i)
        if (std::abs (rateValues[i] - pending.sampleRate)
                < std::abs (rateValues[bestRate] - pending.sampleRate))
            bestRate = i;
    pending.sampleRate = rateValues[bestRate];

    int bestBuf = 0;
    for (int i = 1; i < bufferValues.size(); ++i)
        if (std::abs (bufferValues[i] - pending.bufferSize)
                < std::abs (bufferValues[bestBuf] - pending.bufferSize))
            bestBuf = i;
    pending.bufferSize = bufferValues[bestBuf];

    for (int i = 0; i < rateValues.size(); ++i)
        rateBox.addItem (formatSampleRate (rateValues[i]), i + 1);
    for (int i = 0; i < bufferValues.size(); ++i)
        bufferBox.addItem (formatBuffer (bufferValues[i]), i + 1);

    rateBox.setSelectedId (bestRate + 1, juce::dontSendNotification);
    bufferBox.setSelectedId (bestBuf + 1, juce::dontSendNotification);
    rateBox.setEnabled (rateValues.size() > 1);
    bufferBox.setEnabled (bufferValues.size() > 1);
}

void AudioOverlay::rebuildChannelBoxes()
{
    if (deviceManager == nullptr) return;
    const juce::ScopedValueSetter<bool> svs (rebuilding, true);

    inputChanBox.clear (juce::dontSendNotification);
    outputChanBox.clear (juce::dontSendNotification);
    inputChanNames.clear();
    numOutputPairs = 0;

    if (! pendingMatchesOpenDevice())
    {
        // channel names come from the open device; a staged device switch
        // keeps the manager untouched, so they only appear after Apply
        inputChanBox.addItem ("Available after apply", 1);
        outputChanBox.addItem ("Available after apply", 1);
        inputChanBox.setSelectedId (1, juce::dontSendNotification);
        outputChanBox.setSelectedId (1, juce::dontSendNotification);
        inputChanBox.setEnabled (false);
        outputChanBox.setEnabled (false);
        return;
    }

    auto* dev = deviceManager->getCurrentAudioDevice();
    inputChanNames = dev->getInputChannelNames();
    const auto outNames = dev->getOutputChannelNames();
    numOutputPairs = outNames.size() / 2;

    inputChanBox.addItem ("Device default", 1);
    for (int i = 0; i < inputChanNames.size(); ++i)
        inputChanBox.addItem ("Input " + juce::String (i + 1) + dot() + inputChanNames[i], i + 2);
    if (! juce::isPositiveAndBelow (pending.inputChannel, inputChanNames.size()))
        pending.inputChannel = -1;
    inputChanBox.setSelectedId (pending.inputChannel + 2, juce::dontSendNotification);
    inputChanBox.setEnabled (inputChanNames.size() > 0);

    outputChanBox.addItem ("Device default", 1);
    for (int p = 0; p < numOutputPairs; ++p)
        outputChanBox.addItem ("Output " + juce::String (p * 2 + 1) + " + "
                                   + juce::String (p * 2 + 2) + dot() + outNames[p * 2],
                               p + 2);
    if (! juce::isPositiveAndBelow (pending.outputPair, numOutputPairs))
        pending.outputPair = -1;
    outputChanBox.setSelectedId (pending.outputPair + 2, juce::dontSendNotification);
    outputChanBox.setEnabled (numOutputPairs > 0);
}

void AudioOverlay::cancelPending()
{
    bannerError.clear();
    pending = applied;
    rebuildAll();
    updateActionButtons();
}

void AudioOverlay::applyPending()
{
    if (deviceManager == nullptr)
        return;

    bannerError.clear();

    // Order matters: switching the type opens that type's default device, then
    // one setAudioDeviceSetup call restarts it with the staged configuration.
    if (pending.type.isNotEmpty()
        && pending.type != deviceManager->getCurrentAudioDeviceType())
        deviceManager->setCurrentAudioDeviceType (pending.type, true);

    auto setup = deviceManager->getAudioDeviceSetup();
    setup.outputDeviceName = pending.device;

    auto* type = findTypeObject (pending.type);
    if (type != nullptr && type->hasSeparateInputsAndOutputs())
    {
        // single AUDIO DEVICE select (mockup): keep the current input device,
        // falling back to the type's first input so the guitar stays wired
        if (setup.inputDeviceName.isEmpty())
        {
            type->scanForDevices();
            const auto ins = type->getDeviceNames (true);
            if (! ins.isEmpty())
                setup.inputDeviceName = ins[0];
        }
    }
    else
    {
        setup.inputDeviceName = pending.device;
    }

    setup.sampleRate = pending.sampleRate;
    setup.bufferSize = pending.bufferSize;

    if (pending.inputChannel >= 0)
    {
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setBit (pending.inputChannel);
    }
    else
    {
        setup.useDefaultInputChannels = true;
    }

    if (pending.outputPair >= 0)
    {
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.clear();
        setup.outputChannels.setBit (pending.outputPair * 2);
        setup.outputChannels.setBit (pending.outputPair * 2 + 1);
    }
    else
    {
        setup.useDefaultOutputChannels = true;
    }

    const auto error = deviceManager->setAudioDeviceSetup (setup, true);
    if (error.isNotEmpty())
        bannerError = error;

    captureCurrent();
    rebuildAll();
    updateActionButtons();
}

void AudioOverlay::updateActionButtons()
{
    const bool dirty = ! hostMode && isDirty();
    cancelBtn.setEnabled (dirty);
    applyBtn.setEnabled (dirty);
    repaint (actionsRect);
}

//==============================================================================
// Live telemetry
//==============================================================================
void AudioOverlay::timerCallback()
{
    auto toDb = [] (float v) { return juce::Decibels::gainToDecibels (v, -80.0f); };
    const float inPeak = processor.inputPeak.load();
    inMeterDb = juce::jmax (toDb (inPeak), inMeterDb - 2.2f);
    outMeterDb = juce::jmax (toDb (processor.outputPeak.load()), outMeterDb - 2.2f);

    if (inPeak > 0.003f)                 // ~ -50 dBFS: signal present
        signalHoldTicks = 30;            // holds HEALTHY for ~3 s
    else if (signalHoldTicks > 0)
        --signalHoldTicks;

    refreshHealth();

    // A device change made elsewhere (host, control panel, another window)
    // refreshes the selects as long as no edit is staged here.
    if (! hostMode && ! isDirty())
    {
        const auto now = snapshotFromManager();
        if (now != applied)
        {
            pending = applied = now;
            rebuildAll();
        }
    }

    repaint (tagRect);
    repaint (channelRect);
    repaint (healthRect);
}

void AudioOverlay::refreshHealth()
{
    deviceOnline = false;
    xrunCount = -1;
    latencyOutMs = latencyRoundMs = 0.0;
    bool showPanelBtn = false;

    if (hostMode)
    {
        const double sr = processor.getSampleRate();
        const int block = processor.getBlockSize();
        liveTypeName = "Host audio";
        deviceOnline = sr > 0.0;
        if (sr > 0.0 && block > 0)
        {
            latencyOutMs = block * 1000.0 / sr;
            latencyRoundMs = latencyOutMs * 2.0;
        }
    }
    else if (auto* dev = deviceManager->getCurrentAudioDevice())
    {
        liveTypeName = deviceManager->getCurrentAudioDeviceType();
        deviceOnline = dev->isOpen();
        xrunCount = dev->getXRunCount();   // -1 when the backend can't report

        const double sr = dev->getCurrentSampleRate();
        if (sr > 0.0)
        {
            const int outSamples = dev->getOutputLatencyInSamples() > 0
                                       ? dev->getOutputLatencyInSamples()
                                       : dev->getCurrentBufferSizeSamples();
            const int inSamples = dev->getInputLatencyInSamples() > 0
                                      ? dev->getInputLatencyInSamples()
                                      : dev->getCurrentBufferSizeSamples();
            latencyOutMs = outSamples * 1000.0 / sr;
            latencyRoundMs = (inSamples + outSamples) * 1000.0 / sr;
        }
        showPanelBtn = dev->hasControlPanel();
    }
    else
    {
        liveTypeName = deviceManager->getCurrentAudioDeviceType();
    }

    if (panelBtn.isVisible() != showPanelBtn)
        panelBtn.setVisible (showPanelBtn);
}

//==============================================================================
// Formatting helpers
//==============================================================================
juce::String AudioOverlay::formatSampleRate (double sr)
{
    const juce::String digits ((juce::int64) (sr + 0.5));
    juce::String out;
    const int len = digits.length();
    for (int i = 0; i < len; ++i)
    {
        if (i > 0 && (len - i) % 3 == 0)
            out << ",";
        out << digits.substring (i, i + 1);
    }
    return out + " Hz";
}

juce::String AudioOverlay::formatBuffer (int numSamples) const
{
    const double sr = pending.sampleRate > 0.0 ? pending.sampleRate : 48000.0;
    return juce::String (numSamples) + " samples" + dot()
         + juce::String (numSamples * 1000.0 / sr, 1) + " ms";
}

juce::String AudioOverlay::formatDb (float db) const
{
    if (db <= -79.0f)
        return utf8 ("-\xe2\x88\x9e dB");
    return juce::String (db, 1) + " dB";
}

//==============================================================================
// Layout / painting
//==============================================================================
void AudioOverlay::resized()
{
    const int W = getWidth(), H = getHeight();
    if (W <= 0 || H <= 0)
        return;

    headerRect = { 0, 0, W, headerH };
    closeButton.setBounds (W - 48, (headerH - 34) / 2, 34, 34);
    tagRect = { closeButton.getX() - 138, (headerH - 20) / 2, 128, 20 };

    actionsRect = { 0, H - actionsH, W, actionsH };
    applyBtn.setBounds (W - 186, actionsRect.getY() + (actionsH - 34) / 2, 166, 34);
    cancelBtn.setBounds (applyBtn.getX() - 120, applyBtn.getY(), 110, 34);

    auto content = juce::Rectangle<int> (16, headerRect.getBottom() + 14,
                                         W - 32, actionsRect.getY() - headerRect.getBottom() - 28);
    healthRect = content.removeFromRight (healthColW);
    content.removeFromRight (12);
    settingsRect = content;

    // Device configuration selects: 2-column grid (labels painted above)
    const auto inner = settingsRect.reduced (cardPad);
    const int colW = (inner.getWidth() - comboGap) / 2;
    const int gridTop = inner.getY() + 28;
    auto boxes = comboGrid();
    for (int i = 0; i < 6; ++i)
    {
        const int col = i % 2, row = i / 2;
        boxes[(size_t) i]->setBounds (inner.getX() + col * (colW + comboGap),
                                      gridTop + row * comboRowStride + 16, colW, comboH);
    }

    // Live channel health card (meters painted): under the selects, or right
    // below the host notice when there is nothing to select (VST3)
    const int chanTop = hostMode ? inner.getY() + 92
                                 : gridTop + 3 * comboRowStride + 22;
    const int chanH = juce::jmin (118, inner.getBottom() - chanTop);
    channelRect = { inner.getX(), chanTop, inner.getWidth(), juce::jmax (0, chanH) };

    panelBtn.setBounds (healthRect.getX() + cardPad, healthRect.getBottom() - 48,
                        healthRect.getWidth() - cardPad * 2, 32);
}

void AudioOverlay::paint (juce::Graphics& g)
{
    g.fillAll (ui::bg.withAlpha (0.985f));

    auto drawCard = [&g] (juce::Rectangle<int> r)
    {
        const auto rf = r.toFloat();
        juce::ColourGradient grad (ui::cardTop, rf.getX(), rf.getY(),
                                   ui::cardBottom, rf.getX(), rf.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (rf, 9.0f);
        g.setColour (ui::border());
        g.drawRoundedRectangle (rf.reduced (0.5f), 9.0f, 1.0f);
    };
    auto drawTag = [&g] (juce::Rectangle<int> r, const juce::String& text, juce::Colour c)
    {
        g.setColour (c.withAlpha (0.55f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 5.0f, 1.0f);
        g.setColour (c);
        g.setFont (ui::monoFont (8.0f, true));
        g.drawText (text, r, juce::Justification::centred);
    };
    auto drawLed = [&g] (int x, int cy, juce::Colour c, bool on)
    {
        const juce::Rectangle<float> dot ((float) x, (float) cy - 5.0f, 10.0f, 10.0f);
        if (on)
        {
            g.setColour (c.withAlpha (0.30f));
            g.fillEllipse (dot.expanded (3.0f));
            g.setColour (c);
        }
        else
        {
            g.setColour (ui::textMuted);
        }
        g.fillEllipse (dot);
    };

    //---- header (mockup .audio-head)
    g.setColour (ui::border());
    g.fillRect (0, headerRect.getBottom() - 1, getWidth(), 1);
    g.setFont (ui::uiFont (19.0f, true));
    g.setColour (ui::textBright);
    g.drawText ("Audio & MIDI", 22, 0, 170, headerH, juce::Justification::centredLeft);
    g.setFont (ui::monoFont (8.0f));
    g.setColour (ui::textFaint);
    g.drawText (utf8 ("changes are staged until Apply \xc2\xb7 safe device restart"),
                196, 0, 400, headerH, juce::Justification::centredLeft);

    if (hostMode)
        drawTag (tagRect, "HOST MANAGED", ui::accent);
    else if (deviceOnline)
        drawTag (tagRect, "DEVICE ONLINE", ui::green);
    else
        drawTag (tagRect, "DEVICE OFFLINE", ui::red);

    //---- Device configuration card
    drawCard (settingsRect);
    g.setFont (ui::uiFont (14.0f, true));
    g.setColour (ui::textBright);
    g.drawText ("Device configuration",
                settingsRect.getX() + cardPad, settingsRect.getY() + 12,
                settingsRect.getWidth() - cardPad * 2, 18, juce::Justification::centredLeft);

    if (hostMode)
    {
        const auto inner = settingsRect.reduced (cardPad);
        g.setFont (ui::uiFont (13.0f, true));
        g.setColour (ui::text);
        g.drawText ("Audio device is managed by the host",
                    inner.getX(), inner.getY() + 34, inner.getWidth(), 20,
                    juce::Justification::centredLeft);
        g.setFont (ui::monoFont (9.0f));
        g.setColour (ui::textDim);
        juce::String info = "Sample rate " + formatSampleRate (processor.getSampleRate())
                          + dot() + juce::String (processor.getBlockSize()) + " samples per block";
        if (processor.getSampleRate() <= 0.0)
            info = "Waiting for the host to start the audio engine";
        g.drawText (info, inner.getX(), inner.getY() + 58, inner.getWidth(), 14,
                    juce::Justification::centredLeft);
    }
    else
    {
        // labels above each select
        g.setFont (ui::monoFont (8.0f, true));
        g.setColour (ui::textFaint);
        auto boxes = comboGrid();
        for (int i = 0; i < 6; ++i)
        {
            const auto b = boxes[(size_t) i]->getBounds();
            g.drawText (comboLabels[i], b.getX() + 1, b.getY() - 15, b.getWidth(), 12,
                        juce::Justification::centredLeft);
        }
    }

    //---- Live channel health card (inside the settings card)
    if (! channelRect.isEmpty())
    {
        g.setColour (ui::glass());
        g.fillRoundedRectangle (channelRect.toFloat(), 7.0f);
        g.setColour (ui::border());
        g.drawRoundedRectangle (channelRect.toFloat().reduced (0.5f), 7.0f, 1.0f);

        const int cx = channelRect.getX() + 12;
        const int cw = channelRect.getWidth() - 24;
        g.setFont (ui::uiFont (10.0f, true));
        g.setColour (ui::text);
        g.drawText ("Live channel health", cx, channelRect.getY() + 8, cw, 14,
                    juce::Justification::centredLeft);

        auto drawLevelRow = [&] (int rowY, const juce::String& name, float db,
                                 juce::Colour tagColour)
        {
            g.setFont (ui::uiFont (10.0f, true));
            g.setColour (ui::text);
            g.drawText (name, cx, rowY, 104, 38, juce::Justification::centredLeft);

            const juce::Rectangle<float> bar ((float) (cx + 112), (float) rowY + 15.0f,
                                              (float) (cw - 112 - 82), 8.0f);
            g.setColour (ui::meterBg);
            g.fillRoundedRectangle (bar, 4.0f);
            const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            if (frac > 0.01f)
            {
                juce::ColourGradient grad (ui::accent, bar.getX(), 0.0f,
                                           ui::glowOrange, bar.getRight(), 0.0f, false);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * frac), 4.0f);
            }
            drawTag ({ channelRect.getRight() - 12 - 74, rowY + 10, 74, 18 },
                     formatDb (db), tagColour);
        };

        const int row1 = channelRect.getY() + 26;
        g.setColour (ui::border());
        g.fillRect (cx, row1 + 38, cw, 1);
        drawLevelRow (row1, "INPUT", inMeterDb,
                      signalHoldTicks > 0 ? ui::green : ui::textDim);
        drawLevelRow (row1 + 39, "OUTPUT", outMeterDb,
                      outMeterDb >= -0.1f ? ui::red : ui::textDim);
    }

    //---- System health column
    drawCard (healthRect);
    const int hx = healthRect.getX() + cardPad;
    const int hw = healthRect.getWidth() - cardPad * 2;
    g.setFont (ui::uiFont (14.0f, true));
    g.setColour (ui::textBright);
    g.drawText ("System health", hx, healthRect.getY() + 12, hw, 18,
                juce::Justification::centredLeft);

    {   // latency hero card
        const juce::Rectangle<float> lat ((float) hx, (float) healthRect.getY() + 40.0f,
                                          (float) hw, 84.0f);
        g.setColour (ui::accent.withAlpha (0.07f));
        g.fillRoundedRectangle (lat, 8.0f);
        g.setColour (ui::accent.withAlpha (0.50f));
        g.drawRoundedRectangle (lat.reduced (0.5f), 8.0f, 1.0f);

        g.setFont (ui::monoFont (24.0f, true));
        g.setColour (ui::accentBright);
        const juce::String big = latencyOutMs > 0.0
                                     ? juce::String (latencyOutMs, 1) + " ms"
                                     : utf8 ("\xe2\x80\x94");
        g.drawText (big, lat.toNearestInt().withTrimmedBottom (26),
                    juce::Justification::centred);
        g.setFont (ui::monoFont (8.0f));
        g.setColour (ui::textDim);
        juce::String sub = "output latency";
        if (latencyRoundMs > 0.0)
            sub << dot() << juce::String (latencyRoundMs, 1) << " ms estimated roundtrip";
        g.drawText (sub, lat.toNearestInt().withTrimmedTop (52), juce::Justification::centred);
    }

    {   // LED status rows
        struct Item { juce::String label, value; juce::Colour c; bool on; };
        juce::Array<Item> items;
        items.add ({ liveTypeName.isNotEmpty() ? liveTypeName + " device" : "Audio device",
                     deviceOnline ? "ONLINE" : "OFFLINE",
                     deviceOnline ? ui::green : ui::red, deviceOnline });
        items.add ({ "Input signal",
                     signalHoldTicks > 0 ? "HEALTHY" : "NO SIGNAL",
                     signalHoldTicks > 0 ? ui::green : ui::textDim,
                     signalHoldTicks > 0 });
        if (xrunCount >= 0)   // omitted when the backend can't report xruns
            items.add ({ "Buffer stability", juce::String (xrunCount) + " XRUNS",
                         xrunCount == 0 ? ui::green : ui::yellow, xrunCount == 0 });
        items.add ({ "Host sync", utf8 ("\xe2\x80\x94"), ui::textDim, false });

        int y = healthRect.getY() + 136;
        for (const auto& it : items)
        {
            drawLed (hx, y + 20, it.c, it.on);
            g.setFont (ui::uiFont (10.0f, true));
            g.setColour (ui::text);
            g.drawText (it.label, hx + 18, y, hw - 90, 40, juce::Justification::centredLeft);
            g.setFont (ui::monoFont (8.0f, true));
            g.setColour (it.c);
            g.drawText (it.value, hx, y, hw, 40, juce::Justification::centredRight);
            g.setColour (ui::border());
            g.fillRect (hx, y + 40, hw, 1);
            y += 41;
        }
    }

    //---- actions bar (staging status / error banner)
    g.setColour (ui::border());
    g.fillRect (0, actionsRect.getY(), getWidth(), 1);
    g.setFont (ui::monoFont (8.5f));
    const int statusW = (hostMode ? getWidth() - 40
                                  : cancelBtn.getX() - 36);
    if (bannerError.isNotEmpty())
    {
        g.setColour (ui::red);
        g.drawText ("! Device error" + dot() + bannerError,
                    20, actionsRect.getY(), statusW, actionsH,
                    juce::Justification::centredLeft);
    }
    else if (hostMode)
    {
        g.setColour (ui::textDim);
        g.drawText ("Audio device is managed by the host",
                    20, actionsRect.getY(), statusW, actionsH,
                    juce::Justification::centredLeft);
    }
    else if (isDirty())
    {
        g.setColour (ui::glowOrange);
        g.drawText (utf8 ("Pending changes \xc2\xb7 APPLY CHANGES restarts the device safely"),
                    20, actionsRect.getY(), statusW, actionsH,
                    juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (ui::textDim);
        g.drawText (utf8 ("No changes applied \xc2\xb7 current audio continues uninterrupted"),
                    20, actionsRect.getY(), statusW, actionsH,
                    juce::Justification::centredLeft);
    }
}
