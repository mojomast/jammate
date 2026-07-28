#include "PluginEditor.h"

#include "DrumOverlay.h"
#include "SongOverlay.h"
#include "AudioOverlay.h"
#include "PluginCatalog.h"

#include <BinaryData.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace ui
{
juce::Typeface::Ptr uiTypeface (bool bold)
{
    // Archivo (OFL) — heading/label typeface of the modernist redesign
    static juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (
        BinaryData::ArchivoRegular_ttf, BinaryData::ArchivoRegular_ttfSize);
    static juce::Typeface::Ptr boldTf = juce::Typeface::createSystemTypefaceFor (
        BinaryData::ArchivoExtraBold_ttf, BinaryData::ArchivoExtraBold_ttfSize);
    return bold ? boldTf : regular;
}

juce::Typeface::Ptr monoTypeface (bool bold)
{
    static juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (
        BinaryData::JetBrainsMonoRegular_ttf, BinaryData::JetBrainsMonoRegular_ttfSize);
    static juce::Typeface::Ptr boldTf = juce::Typeface::createSystemTypefaceFor (
        BinaryData::JetBrainsMonoBold_ttf, BinaryData::JetBrainsMonoBold_ttfSize);
    return bold ? boldTf : regular;
}
} // namespace ui

namespace
{
// ---------- tuner: pitch detection (simplified NSDF/MPM) ----------
double detectPitchHz (const float* x, int n, double sr)
{
    double energy = 0.0;
    for (int i = 0; i < n; ++i)
        energy += (double) x[i] * x[i];
    if (energy / n < 1.0e-5) // silence
        return -1.0;

    const int minLag = juce::jmax (2, (int) (sr / 500.0)); // up to 500 Hz
    const int maxLag = juce::jmin (n / 2, (int) (sr / 55.0)); // down to 55 Hz
    if (maxLag <= minLag + 2)
        return -1.0;

    std::vector<double> nsdf ((size_t) maxLag + 1, 0.0);
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double ac = 0.0, norm = 0.0;
        const int m = n - maxLag; // fixed window for all lags
        for (int i = 0; i < m; ++i)
        {
            ac += (double) x[i] * x[i + lag];
            norm += (double) x[i] * x[i] + (double) x[i + lag] * x[i + lag];
        }
        nsdf[(size_t) lag] = norm > 0.0 ? 2.0 * ac / norm : 0.0;
    }

    double maxV = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag)
        maxV = juce::jmax (maxV, nsdf[(size_t) lag]);
    if (maxV < 0.6)
        return -1.0;

    const double thr = 0.9 * maxV;
    for (int lag = minLag + 1; lag < maxLag; ++lag)
    {
        const double v = nsdf[(size_t) lag];
        if (v >= thr && v >= nsdf[(size_t) lag - 1] && v >= nsdf[(size_t) lag + 1])
        {
            const double denom = 2.0 * (2.0 * v - nsdf[(size_t) lag - 1] - nsdf[(size_t) lag + 1]);
            const double d = denom != 0.0 ? (nsdf[(size_t) lag + 1] - nsdf[(size_t) lag - 1]) / denom : 0.0;
            return sr / ((double) lag + d);
        }
    }
    return -1.0;
}

const char* kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const double kStringFreqs[] = { 82.407, 110.0, 146.83, 196.0, 246.94, 329.63 };
} // namespace

//==============================================================================
KnobComponent::KnobComponent (juce::AudioProcessorValueTreeState& apvts,
                              const juce::String& paramId, const juce::String& labelText,
                              std::function<juce::String (float)> formatter)
    : format (std::move (formatter)),
      attachment (apvts, paramId, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    slider.onValueChange = [this] { updateValueText(); };

    // detent at the default value + refined interactions
    if (auto* param = apvts.getParameter (paramId))
    {
        const auto& range = param->getNormalisableRange();
        slider.snapTarget = range.convertFrom0to1 (param->getDefaultValue());
        slider.snapRadius = (range.end - range.start) * 0.04;
        slider.setDoubleClickReturnValue (true, slider.snapTarget); // double-click resets
    }
    slider.setScrollWheelEnabled (true);                            // wheel adjusts
    slider.setVelocityModeParameters (1.0, 1, 0.05, true,           // Ctrl = fine adjust
                                      juce::ModifierKeys::ctrlModifier);
    slider.setMouseClickGrabsKeyboardFocus (false); // shortcuts stay with RigContent
    addAndMakeVisible (slider);

    nameLabel.setText (labelText, juce::dontSendNotification);
    nameLabel.setFont (ui::monoFont (8.0f));
    nameLabel.setColour (juce::Label::textColourId, ui::textFaint);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setMinimumHorizontalScale (0.55f);
    addAndMakeVisible (nameLabel);

    valueLabel.setFont (ui::monoFont (10.5f, true));
    valueLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe8ecf1));
    valueLabel.setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour (0xff14181d));
    valueLabel.setColour (juce::TextEditor::highlightColourId, ui::accent.withAlpha (0.4f));
    valueLabel.setJustificationType (juce::Justification::centred);
    // click the value -> type the number
    valueLabel.setEditable (true, false, true);
    valueLabel.setTooltip ("Click to type the value");
    valueLabel.onTextChange = [this]
    {
        const auto text = valueLabel.getText().retainCharacters ("0123456789.,-");
        if (text.isEmpty())
        {
            updateValueText();
            return;
        }
        slider.setValue (text.replaceCharacter (',', '.').getDoubleValue(),
                         juce::sendNotificationSync);
        updateValueText();
    };
    addAndMakeVisible (valueLabel);

    // clean UI: the value is on-demand - visible while hovering/dragging/typing
    valueLabel.setVisible (false);
    valueLabel.onEditorHide = [this] { refreshValueVisibility(); };
    addMouseListener (this, true);   // enter/exit of the slider reach us too

    updateValueText();
}

void KnobComponent::setKnobTooltip (const juce::String& tip)
{
    slider.setTooltip (tip);
}

void KnobComponent::setCompactLayout (bool shouldBeCompact)
{
    if (compactLayout == shouldBeCompact)
        return;

    compactLayout = shouldBeCompact;
    nameLabel.setFont (ui::monoFont (compactLayout ? 6.5f : 8.0f));
    valueLabel.setFont (ui::monoFont (compactLayout ? 8.0f : 10.5f, true));
    valueLabel.setColour (juce::Label::backgroundColourId,
                          compactLayout ? juce::Colour (0xd914181d)
                                        : juce::Colours::transparentBlack);
    resized();
}

void KnobComponent::mouseEnter (const juce::MouseEvent&) { refreshValueVisibility(); }
void KnobComponent::mouseExit (const juce::MouseEvent&)  { refreshValueVisibility(); }
void KnobComponent::mouseUp (const juce::MouseEvent&)    { refreshValueVisibility(); }

void KnobComponent::refreshValueVisibility()
{
    valueLabel.setVisible (isMouseOver (true) || slider.isMouseOverOrDragging()
                           || valueLabel.isBeingEdited());
}

void KnobComponent::updateValueText()
{
    valueLabel.setText (format ((float) slider.getValue()), juce::dontSendNotification);
}

void KnobComponent::resized()
{
    auto area = getLocalBounds();
    slider.setBounds (area.removeFromTop (getWidth()));
    nameLabel.setBounds (area.removeFromTop (12));
    if (compactLayout)
        valueLabel.setBounds (0, juce::jmax (0, getWidth() - 13), getWidth(), 13);
    else
        valueLabel.setBounds (area.removeFromTop (14));
}

//==============================================================================
// vNext: searchable effect browser (drawer)
namespace
{
struct FxCatalogEntry { const char* id; const char* sub; };
struct FxCatalogCat { const char* title; std::initializer_list<FxCatalogEntry> fx; };
const FxCatalogCat kFxCatalog[] = {
    { "DYNAMICS", { { "gate", "Smart gate \xc2\xb7 hysteresis + hold" },
                    { "comp", "Dyna / Optical / Studio + presets" },
                    { "slowgear", "Automatic volume swell" },
                    { "limiter", "Brickwall \xc2\xb7 end of the chain" } } },
    { "DRIVE & FILTER", { { "wah", "Auto / manual wah" },
                          { "od", "6 drive voicings" },
                          { "octaver", "Analog sub-octave" },
                          { "ringmod", "Sine carrier" },
                          { "bitcrush", "Lo-fi \xc2\xb7 bits + rate" },
                          { "preeq", "3-band pre EQ" } } },
    { "PITCH", { { "pitch", "Granular shifter" },
                 { "harm", "Diatonic harmonizer" } } },
    { "MODULATION & COLOR", { { "mod", "Chorus \xc2\xb7 flanger \xc2\xb7 phaser \xc2\xb7 rotary" },
                              { "exciter", "Harmonic brightness" },
                              { "deesser", "Tames the harsh band" },
                              { "tape", "Saturation \xc2\xb7 bump \xc2\xb7 rolloff" },
                              { "console", "Analog buss glue" } } },
    { "AMBIENCE", { { "delay", "Tap tempo \xc2\xb7 subdivisions \xc2\xb7 trails" },
                    { "reverb", "Room / hall / plate / spring / shimmer" } } },
    { "EXTRAS", { { "ext", "Hosted VST3 slot 1" },  { "ext2", "Hosted VST3 slot 2" },
                  { "ext3", "Hosted VST3 slot 3" }, { "ext4", "Hosted VST3 slot 4" },
                  { "ext5", "Hosted VST3 slot 5" }, { "ext6", "Hosted VST3 slot 6" },
                  { "ext7", "Hosted VST3 slot 7" }, { "ext8", "Hosted VST3 slot 8" },
                  { "looper", "60 s looper \xc2\xb7 WAV export" },
                  { "analyzer", "Spectrum analyzer" } } },
};

// one clickable effect row (name + short description + category glyph)
class FxRow : public juce::Component
{
public:
    FxRow (const juce::String& fxId, const juce::String& fxName, const juce::String& fxSub,
           std::function<void (const juce::String&)> pick)
        : id (fxId), name (fxName), sub (fxSub), onPick (std::move (pick))
    {
        setRepaintsOnMouseActivity (true);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()) && onPick)
            onPick (id);
    }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        const bool hover = isMouseOver();
        g.setColour (hover ? ui::glassHover() : ui::glass());
        g.fillRoundedRectangle (b, 5.0f);
        g.setColour (hover ? ui::accent.withAlpha (0.6f) : ui::border());
        g.drawRoundedRectangle (b, 5.0f, 1.0f);

        g.setColour (ui::accent.withAlpha (0.14f));
        g.fillRoundedRectangle (8.0f, 9.0f, 28.0f, 28.0f, 5.0f);
        g.setColour (ui::accent);
        g.setFont (ui::uiFont (12.0f, true));
        g.drawText (name.substring (0, 1), 8, 9, 28, 28, juce::Justification::centred);

        g.setColour (ui::textBright);
        g.setFont (ui::uiFont (12.0f, true));
        g.drawText (name, 46, 6, getWidth() - 100, 16, juce::Justification::centredLeft);
        g.setColour (ui::textFaint);
        g.setFont (ui::monoFont (8.0f));
        g.drawText (sub, 46, 24, getWidth() - 100, 12, juce::Justification::centredLeft);

        g.setColour (hover ? ui::accent : ui::textFaint);
        g.setFont (ui::monoFont (8.5f, true));
        g.drawText ("ADD", getWidth() - 46, 0, 38, getHeight(), juce::Justification::centred);
    }
private:
    juce::String id, name, sub;
    std::function<void (const juce::String&)> onPick;
};

// non-interactive section label between rows
class FxSectionLabel : public juce::Component
{
public:
    explicit FxSectionLabel (const juce::String& t) : text (t)
    {
        setInterceptsMouseClicks (false, false);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (ui::textFaint);
        g.setFont (ui::monoFont (8.0f, true));
        g.drawText (text, 4, 0, getWidth() - 8, getHeight(), juce::Justification::bottomLeft);
    }
private:
    juce::String text;
};
} // namespace

FxDrawer::FxDrawer (GuitarRigNAMProcessor& p) : processor (p)
{
    search.setFont (ui::uiFont (12.5f));
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff10161a));
    search.setColour (juce::TextEditor::outlineColourId, ui::border());
    search.setColour (juce::TextEditor::focusedOutlineColourId, ui::accentDark);
    search.setColour (juce::TextEditor::textColourId, ui::text);
    search.setTextToShowWhenEmpty (
        juce::String (juce::CharPointer_UTF8 ("Search effects\xe2\x80\xa6")), ui::textMuted);
    search.onTextChange = [this] { rebuild(); };
    search.setEscapeAndReturnKeysConsumed (false);
    addAndMakeVisible (search);

    closeBtn.getProperties().set ("ghost", true);
    closeBtn.setMouseClickGrabsKeyboardFocus (false);
    closeBtn.onClick = [this] { close(); };
    addAndMakeVisible (closeBtn);

    vp.setViewedComponent (&content, false);
    vp.setScrollBarsShown (true, false);
    vp.setScrollBarThickness (8);
    addAndMakeVisible (vp);
}

void FxDrawer::open (int index)
{
    insertIndex = index;
    search.setText ({}, juce::dontSendNotification);
    rebuild();
    setVisible (true);
    toFront (true);
    search.grabKeyboardFocus();
}

void FxDrawer::rebuild()
{
    rows.clear();
    recentBtns.clear();
    content.removeAllChildren();

    const auto order = processor.getChainOrder();
    const auto q = search.getText().trim().toLowerCase();
    const int W = juce::jmax (100, getWidth() - 24 - 10);
    int y = 0;

    // session "recent" chips (only those not currently in the chain)
    juce::StringArray recentFree;
    for (const auto& id : recents)
        if (! order.contains (id))
            recentFree.add (id);
    if (q.isEmpty() && ! recentFree.isEmpty())
    {
        auto* lbl = new FxSectionLabel ("RECENT");
        rows.add (lbl);
        content.addAndMakeVisible (lbl);
        lbl->setBounds (0, y, W, 20);
        y += 24;
        int x = 0;
        for (const auto& id : recentFree)
        {
            auto* b = recentBtns.add (new juce::TextButton (
                ChainView::fxDisplayNamePublic (id)));
            b->getProperties().set ("chip", true);
            b->setMouseClickGrabsKeyboardFocus (false);
            b->onClick = [this, id]
            {
                if (onInsert)
                    onInsert (id, insertIndex);
            };
            content.addAndMakeVisible (b);
            const int bw = 26 + 7 * b->getButtonText().length();
            b->setBounds (x, y, bw, 26);
            x += bw + 6;
        }
        y += 34;
    }

    for (const auto& cat : kFxCatalog)
    {
        bool headerAdded = false;
        for (const auto& fx : cat.fx)
        {
            const juce::String id (fx.id);
            if (order.contains (id))
                continue;
            const auto name = ChainView::fxDisplayNamePublic (id);
            const juce::String sub = juce::String (juce::CharPointer_UTF8 (fx.sub));
            if (q.isNotEmpty() && ! (name.toLowerCase().contains (q)
                                     || sub.toLowerCase().contains (q)
                                     || juce::String (cat.title).toLowerCase().contains (q)))
                continue;
            if (! headerAdded)
            {
                auto* lbl = new FxSectionLabel (juce::String (juce::CharPointer_UTF8 (cat.title)));
                rows.add (lbl);
                content.addAndMakeVisible (lbl);
                lbl->setBounds (0, y, W, 20);
                y += 24;
                headerAdded = true;
            }
            auto* row = new FxRow (id, name, sub, [this] (const juce::String& picked)
            {
                recents.removeString (picked);
                recents.insert (0, picked);
                while (recents.size() > 3)
                    recents.remove (recents.size() - 1);
                if (onInsert)
                    onInsert (picked, insertIndex);
            });
            rows.add (row);
            content.addAndMakeVisible (row);
            row->setBounds (0, y, W, 46);
            y += 51;
        }
    }

    if (y == 0)
    {
        auto* lbl = new FxSectionLabel (q.isNotEmpty() ? "NO EFFECT MATCHES THE SEARCH"
                                                       : "ALL EFFECTS ARE IN THE CHAIN");
        rows.add (lbl);
        content.addAndMakeVisible (lbl);
        lbl->setBounds (0, 0, W, 20);
        y = 28;
    }

    content.setSize (W, y + 8);
    repaint();
}

void FxDrawer::resized()
{
    search.setBounds (14, 52, getWidth() - 14 - 48, 34);
    closeBtn.setBounds (getWidth() - 44, 52, 32, 34);
    vp.setBounds (14, 98, getWidth() - 24, getHeight() - 98 - 12);
    rebuild();
}

void FxDrawer::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    // slide-over card with a strong left edge (reads as a drawer)
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (b.removeFromLeft (6.0f));
    g.setGradientFill ({ ui::cardTop, 0.0f, 0.0f, ui::cardBottom, 0.0f, (float) getHeight(), false });
    g.fillRect (b);
    g.setColour (ui::borderHover());
    g.drawLine (6.0f, 0.0f, 6.0f, (float) getHeight(), 1.0f);

    g.setColour (ui::textBright);
    g.setFont (ui::uiFont (16.0f, true));
    g.drawText ("Add effect", 16, 14, 200, 20, juce::Justification::centredLeft);
    g.setColour (ui::textFaint);
    g.setFont (ui::monoFont (8.5f));
    g.drawText (insertIndex >= 0 ? "inserts at the clicked position"
                                 : "inserts at the canonical position",
                16, 34, getWidth() - 30, 12, juce::Justification::centredLeft);
}

//==============================================================================
void LedButton::paintButton (juce::Graphics& g, bool, bool)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    constexpr float d = 14.0f;   // bypass-led dot per tokens.json

    if (getToggleState())        // on: accent fill + glow
    {
        g.setColour (ui::accent.withAlpha (0.40f));
        g.fillEllipse (c.x - d * 0.78f, c.y - d * 0.78f, d * 1.56f, d * 1.56f);
        g.setColour (ui::accent);
        g.fillEllipse (c.x - d * 0.5f, c.y - d * 0.5f, d, d);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillEllipse (c.x - d * 0.24f, c.y - d * 0.34f, d * 0.34f, d * 0.28f); // specular
    }
    else                         // off: 1.5px divider ring (hollow)
    {
        g.setColour (ui::text.withAlpha (0.24f));
        g.drawEllipse (c.x - d * 0.5f + 0.75f, c.y - d * 0.5f + 0.75f, d - 1.5f, d - 1.5f, 1.5f);
    }
}

//==============================================================================
void LevelMeter::setLevel (float newLevelDb)
{
    solid = false;
    const float f = juce::jlimit (0.0f, 1.0f, (newLevelDb + 60.0f) / 60.0f);

    // peak-hold: holds the marker ~1.5 s then lets it slide
    const float oldPeak = peakFrac;
    if (f >= peakFrac)
    {
        peakFrac = f;
        peakHoldTicks = 45;
    }
    else if (peakHoldTicks > 0)
    {
        --peakHoldTicks;
    }
    else
    {
        peakFrac = juce::jmax (f, peakFrac - 0.012f);
    }

    if (std::abs (f - fraction) > 0.004f || std::abs (peakFrac - oldPeak) > 0.003f)
    {
        fraction = f;
        repaint();
    }
}

void LevelMeter::setFraction (float f, juce::Colour c)
{
    solid = true;
    solidColour = c;
    f = juce::jlimit (0.0f, 1.0f, f);
    if (std::abs (f - fraction) > 0.004f)
    {
        fraction = f;
        repaint();
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (ui::meterBg);
    g.fillRect (b);
    g.setColour (ui::text.withAlpha (0.10f));
    g.drawRect (b, 1.0f);

    auto inner = b.reduced (1.5f);
    constexpr int N = 14;                 // segments per tokens.json
    constexpr int clipZone = N - 2;       // last 2 = warn (amber)
    const float gap = 1.4f;
    const float segW = (inner.getWidth() - gap * (float) (N - 1)) / (float) N;
    const float off = 0.08f;

    for (int i = 0; i < N; ++i)
    {
        auto seg = juce::Rectangle<float> (inner.getX() + (float) i * (segW + gap),
                                           inner.getY(), segW, inner.getHeight());
        const bool lit = fraction >= (float) i / (float) N + 0.001f;
        juce::Colour c;
        if (solid)
            c = lit ? solidColour : ui::text.withAlpha (off);
        else if (i >= clipZone)
            c = lit ? (peakFrac >= 0.98f ? ui::red : ui::glowOrange)
                    : ui::glowOrange.withAlpha (0.12f);
        else
            c = lit ? ui::accent : ui::text.withAlpha (off);
        g.setColour (c);
        g.fillRect (seg);
    }

    // peak-hold marker (bright segment)
    if (! solid && peakFrac > 0.02f)
    {
        const int pi = juce::jlimit (0, N - 1, (int) (peakFrac * (float) N));
        auto seg = juce::Rectangle<float> (inner.getX() + (float) pi * (segW + gap),
                                           inner.getY(), segW, inner.getHeight());
        g.setColour (peakFrac >= 0.98f ? ui::red : ui::textBright.withAlpha (0.9f));
        g.fillRect (seg);
    }
}

//==============================================================================
void PillButton::paintButton (juce::Graphics& g, bool isHighlighted, bool)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (ui::glass());
    g.fillRoundedRectangle (b, 2.0f);
    g.setColour (isHighlighted ? ui::borderHover() : juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (b, 2.0f, 1.0f);

    g.setColour (dotLit ? ui::accent : ui::textMuted);
    g.fillEllipse (b.getX() + 13.0f, b.getCentreY() - 3.0f, 6.0f, 6.0f);

    g.setFont (ui::uiFont (13.0f, true));
    g.setColour (ui::text);
    g.drawText (getButtonText(), getLocalBounds().reduced (26, 0), juce::Justification::centred);

    g.setFont (ui::uiFont (9.0f));
    g.setColour (ui::textMuted);
    g.drawText ("v", getLocalBounds().removeFromRight (20), juce::Justification::centredLeft);
}

//==============================================================================
ChainView::ChainView (GuitarRigNAMProcessor& p, std::function<void (int)> onLoadModel,
                      std::function<void (int)> onLoadIr,
                      std::function<void (int)> onLoadExtPlugin,
                      std::function<void (int)> onOpenExtPluginUi)
    : processor (p)
{
    auto& apvts = processor.apvts;

    // TONE3000 mark for store-loaded signal blocks (design requirement 5)
    t3kMark = juce::ImageFileFormat::loadFrom (BinaryData::t3kmark_png,
                                               (size_t) BinaryData::t3kmark_pngSize);

    auto formatDb = [] (float v) { return juce::String (v, 1) + " dB"; };
    auto formatDbInt = [] (float v) { return juce::String ((int) v) + " dB"; };
    auto formatMs = [] (float v) { return juce::String ((int) v) + " ms"; };
    auto formatTen = [] (float v) { return juce::String (v, 1); };
    auto formatPct = [] (float v) { return juce::String ((int) v) + "%"; };

    auto makeKnob = [&] (std::unique_ptr<KnobComponent>& dest, const char* id,
                         const char* label, std::function<juce::String (float)> fmt)
    {
        dest = std::make_unique<KnobComponent> (apvts, id, label, std::move (fmt));
        addAndMakeVisible (*dest);
    };

    makeKnob (gateThreshKnob, "gateThresh", "THRESH", formatDbInt);
    makeKnob (gateHoldKnob, "gateHold", "HOLD", formatMs);
    makeKnob (gateReleaseKnob, "gateRelease", "RELEASE", formatMs);
    makeKnob (compSustainKnob, "compSustain", "SUSTAIN", formatTen);
    makeKnob (compAttackKnob, "compAttack", "ATTACK", formatMs);
    makeKnob (compBlendKnob, "compBlend", "BLEND", formatPct);
    makeKnob (compLevelKnob, "compLevel", "LEVEL", formatDb);
    makeKnob (preEqLowKnob, "preEqLow", "LOW", formatDbInt);
    makeKnob (preEqMidKnob, "preEqMid", "MID", formatDbInt);
    makeKnob (preEqHighKnob, "preEqHigh", "HIGH", formatDbInt);
    auto formatHzMod = [] (float v) { return juce::String (v, 1) + " Hz"; };
    makeKnob (modRateKnob, "modRate", "RATE", formatHzMod);
    makeKnob (modDepthKnob, "modDepth", "DEPTH", formatPct);
    makeKnob (modMixKnob, "modMix", "MIX", formatPct);
    makeKnob (odDriveKnob, "odDrive", "DRIVE", formatTen);
    makeKnob (odToneKnob, "odTone", "TONE", formatTen);
    makeKnob (odLevelKnob, "odLevel", "LEVEL", formatTen);

    auto formatHz = [] (float v)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + "k" : juce::String ((int) v);
    };
    for (int r = 0; r < maxRigs; ++r)
    {
        const auto n = juce::String (r + 1);
        const auto prefix = r == 0 ? juce::String ("amp") : "amp" + n;
        makeKnob (ampGainKnob[r], (prefix + "Gain").toRawUTF8(), "GAIN", formatDb);
        makeKnob (ampBassKnob[r], (prefix + "Bass").toRawUTF8(), "BASS", formatTen);
        makeKnob (ampMidKnob[r], (prefix + "Mid").toRawUTF8(), "MID", formatTen);
        makeKnob (ampTrebleKnob[r], (prefix + "Treble").toRawUTF8(), "TREBLE", formatTen);
        makeKnob (ampPresKnob[r], (prefix + "Presence").toRawUTF8(), "PRES", formatTen);
        makeKnob (ampMasterKnob[r], (prefix + "Master").toRawUTF8(), "MASTER", formatDb);

        loadButtons[r].setButtonText ("LOAD NAM CAPTURE");
        loadButtons[r].setTooltip ("Add a capture from the TONE3000 store or a local .nam file");
        loadButtons[r].setMouseClickGrabsKeyboardFocus (false);
        loadButtons[r].onClick = [onLoadModel, r] { onLoadModel (r); };
        addChildComponent (loadButtons[r]);

        // variation selector: swaps the loaded capture for another model of the
        // same TONE3000 tone (inline picker). Shown whenever a model is loaded;
        // enabled only for store captures (those carry a tone_id in the .meta).
        ampVarButtons[r].setButtonText (juce::String (juce::CharPointer_UTF8 ("VARIANTS \xe2\x96\xbe")));
        ampVarButtons[r].getProperties().set ("outlineAccent", true);
        ampVarButtons[r].setMouseClickGrabsKeyboardFocus (false);
        ampVarButtons[r].onClick = [this, r]
        {
            const int tid = toneIdForLane (r);
            if (tid > 0 && onShowVariations != nullptr)
                onShowVariations (r, tid, &ampVarButtons[r]);
        };
        addChildComponent (ampVarButtons[r]);

        // the lane's blend/level lives in the OUTPUT card (RigContent)
        makeKnob (cabLcKnob[r], ("cab" + n + "LowCut").toRawUTF8(), "LO CUT", formatHz);
        makeKnob (cabHcKnob[r], ("cab" + n + "HighCut").toRawUTF8(), "HI CUT", formatHz);

        cabPhaseChips[r].setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x98")));
        cabPhaseChips[r].getProperties().set ("chip", true);
        cabPhaseChips[r].setClickingTogglesState (true);
        cabPhaseChips[r].setTooltip (juce::String (juce::CharPointer_UTF8 (
            "Inverts this cab's phase (avoids cancellation in parallel)")));
        cabPhaseChips[r].setMouseClickGrabsKeyboardFocus (false);
        cabPhaseAtt[r] = std::make_unique<Attachment> (apvts, "cab" + n + "Phase",
                                                       cabPhaseChips[r]);
        addChildComponent (cabPhaseChips[r]);

        cabIrButtons[r].setButtonText ("CHANGE");   // narrow card: no room for the caret
        cabIrButtons[r].setTooltip ("Add an IR from the TONE3000 store or a local file");
        cabIrButtons[r].setMouseClickGrabsKeyboardFocus (false);
        cabIrButtons[r].onClick = [onLoadIr, r] { onLoadIr (r); };
        addChildComponent (cabIrButtons[r]);

        // cab variation selector: other IRs of the same TONE3000 cab tone
        // (short label - the cab card is narrower than the amp's)
        cabVarButtons[r].setButtonText (juce::String (juce::CharPointer_UTF8 ("VARS \xe2\x96\xbe")));
        cabVarButtons[r].getProperties().set ("outlineAccent", true);
        cabVarButtons[r].setMouseClickGrabsKeyboardFocus (false);
        cabVarButtons[r].onClick = [this, r]
        {
            const int tid = toneIdForCab (r);
            if (tid > 0 && onShowVariations != nullptr)
                onShowVariations (r, tid, &cabVarButtons[r]);
        };
        addChildComponent (cabVarButtons[r]);
    }

    // variation selectors on the cards (menu in the footer) - the tooltips cite
    // the study sources of each family; details in docs/EFEITOS.md
    setupTypeButton (odTypeButton, "odType",
                     juce::String (juce::CharPointer_UTF8 (
                         "Choose the drive model \xc2\xb7 refs: BYOD, Guitarix, Airwindows (docs/EFEITOS.md)")));
    setupTypeButton (compTypeButton, "compType",
                     juce::String (juce::CharPointer_UTF8 (
                         "Choose the compressor model \xc2\xb7 refs: LSP Plugins, rkrlv2 (docs/EFEITOS.md)")));
    setupTypeButton (delayTypeButton, "delayType",
                     juce::String (juce::CharPointer_UTF8 (
                         "Choose the delay model \xc2\xb7 refs: Airwindows, Guitarix (docs/EFEITOS.md)")));
    setupTypeButton (revTypeButton, "revType",
                     juce::String (juce::CharPointer_UTF8 (
                         "Choose the reverb model \xc2\xb7 refs: Dragonfly, GxPlugins (docs/EFEITOS.md)")));
    setupTypeButton (modTypeButton, "modType",
                     juce::String (juce::CharPointer_UTF8 (
                         "Choose the modulation type \xc2\xb7 refs: ToobAmp, GxPlugins, Airwindows (docs/EFEITOS.md)")));
    setupTypeButton (delayDivButton, "delayDiv",
                     juce::String (juce::CharPointer_UTF8 (
                         "Subdivision applied to TAP (1/8. = dotted eighth)")));
    setupTypeButton (pitchTypeButton, "pitchType",
                     juce::String (juce::CharPointer_UTF8 (
                         "Choose the pitch interval \xc2\xb7 ref: rkrlv2/rakarrack (docs/EFEITOS.md)")));
    setupTypeButton (wahModeButton, "wahMode",
                     juce::String (juce::CharPointer_UTF8 (
                         "Auto = envelope \xc2\xb7 Manual = FREQ knob \xc2\xb7 LFO = sweep \xc2\xb7 ref: Guitarix")));
    setupTypeButton (harmKeyButton, "harmKey",
                     juce::String (juce::CharPointer_UTF8 ("Song key")));
    setupTypeButton (harmScaleButton, "harmScale",
                     juce::String (juce::CharPointer_UTF8 ("Major or minor scale")));
    setupTypeButton (harmIntervalButton, "harmInterval",
                     juce::String (juce::CharPointer_UTF8 (
                         "Diatonic interval of the second voice \xc2\xb7 ref: rkrlv2/rakarrack")));

    makeKnob (eqLowKnob, "eqLow", "LOW", formatDbInt);
    makeKnob (eqMidKnob, "eqMid", "MID", formatDbInt);
    makeKnob (eqHighKnob, "eqHigh", "HIGH", formatDbInt);
    makeKnob (delayTimeKnob, "delayTime", "TIME", formatMs);
    makeKnob (delayFbKnob, "delayFb", "FB", formatPct);
    makeKnob (delayMixKnob, "delayMix", "MIX", formatPct);
    makeKnob (revDecayKnob, "revDecay", "DECAY", formatTen);
    makeKnob (revMixKnob, "revMix", "MIX", formatPct);
    makeKnob (revPreKnob, "revPre", "PRE", formatMs);
    makeKnob (pitchMixKnob, "pitchMix", "MIX", formatPct);
    makeKnob (pitchLevelKnob, "pitchLevel", "LEVEL", formatDb);
    makeKnob (looperLevelKnob, "looperLevel", "LOOP", formatDb);
    makeKnob (limCeilKnob, "limCeiling", "CEIL", formatDb);
    makeKnob (limRelKnob, "limRelease", "REL", formatMs);
    // external VST3 plugin slots
    for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
    {
        const auto prefix = s == 0 ? juce::String ("ext") : "ext" + juce::String (s + 1);
        makeKnob (extMixKnob[s], (prefix + "Mix").toRawUTF8(), "MIX", formatPct);

        extLoadButton[s].setButtonText ("LOAD VST3");
        extLoadButton[s].setTooltip (juce::String (juce::CharPointer_UTF8 (
            "Choose a .vst3 plugin by category (Dragonfly, Airwindows, Zam...)")));
        extLoadButton[s].onClick = [onLoadExtPlugin, s] { onLoadExtPlugin (s); };
        extUiButton[s].setButtonText ("PANEL");
        extUiButton[s].setTooltip ("Open the hosted plugin's interface");
        extUiButton[s].onClick = [onOpenExtPluginUi, s] { onOpenExtPluginUi (s); };
        extRemoveButton[s].setButtonText ("REMOVE");
        extRemoveButton[s].setTooltip ("Empty the slot");
        extRemoveButton[s].onClick = [this, s] { processor.clearExternalPlugin (s); };
        for (auto* b : { &extLoadButton[s], &extUiButton[s], &extRemoveButton[s] })
        {
            b->setMouseClickGrabsKeyboardFocus (false);
            addAndMakeVisible (*b);
        }
    }

    // P4 cards - one effect per card
    makeKnob (wahFreqKnob, "wahFreq", "FREQ", formatHz);
    makeKnob (wahRangeKnob, "wahRange", "RANGE", formatPct);
    makeKnob (wahResKnob, "wahRes", "RES", formatTen);
    makeKnob (sgSensKnob, "sgSens", "SENS", formatTen);
    makeKnob (sgRiseKnob, "sgRise", "RISE", formatMs);
    makeKnob (octSubKnob, "octSub", "SUB", formatPct);
    makeKnob (octDirectKnob, "octDirect", "DIRECT", formatPct);
    makeKnob (octToneKnob, "octTone", "TONE", formatHz);
    makeKnob (rmFreqKnob, "rmFreq", "FREQ", formatHz);
    makeKnob (rmMixKnob, "rmMix", "MIX", formatPct);
    makeKnob (bcBitsKnob, "bcBits", "BITS", [] (float v) { return juce::String ((int) v); });
    makeKnob (bcRateKnob, "bcRate", "RATE", formatHz);
    makeKnob (bcMixKnob, "bcMix", "MIX", formatPct);
    makeKnob (harmMixKnob, "harmMix", "MIX", formatPct);
    makeKnob (harmLevelKnob, "harmLevel", "LEVEL", formatDb);
    makeKnob (excFreqKnob, "excFreq", "FREQ", formatHz);
    makeKnob (excAmtKnob, "excAmt", "AMT", formatPct);
    makeKnob (dsFreqKnob, "dsFreq", "FREQ", formatHz);
    makeKnob (dsSensKnob, "dsSens", "SENS", formatTen);
    makeKnob (dsAmtKnob, "dsAmt", "AMT", formatPct);
    makeKnob (tapeDriveKnob, "tapeDrive", "DRIVE", formatTen);
    makeKnob (tapeBumpKnob, "tapeBump", "BUMP", formatDb);
    makeKnob (tapeRollKnob, "tapeRoll", "ROLL", formatHz);
    makeKnob (cnsAmtKnob, "cnsAmt", "GLUE", formatTen);


    // looper buttons (dynamic text in refreshDynamicText)
    {
        auto setupLooperButton = [this] (juce::TextButton& b, int cmd, const char* tipUtf8)
        {
            b.setTooltip (juce::String (juce::CharPointer_UTF8 (tipUtf8)));
            b.setMouseClickGrabsKeyboardFocus (false);
            if (cmd > 0)
                b.onClick = [this, cmd] { processor.requestLooperCommand (cmd); };
            addAndMakeVisible (b);
        };
        setupLooperButton (looperRecButton, 1,
                           "Records the loop; again closes and plays; then toggles overdub");
        setupLooperButton (looperPlayButton, 2, "Plays/stops the recorded loop");
        setupLooperButton (looperClearButton, 3, "Erases the loop");
        looperClearButton.setButtonText ("CLEAR");
        setupLooperButton (looperExportButton, 0,
                           "Saves the loop as WAV (Documents\\PedalForge NAM\\Loops)");
        looperExportButton.setButtonText ("WAV");
        looperExportButton.onClick = [this]
        {
            const auto file = processor.exportLoopToWav();
            looperExportButton.setButtonText (file != juce::File() ? "SAVED" : "EMPTY");
            auto* self = this; // MSVC: 'this' in a nested init-capture resolves incorrectly
            juce::Timer::callAfterDelay (1200,
                [safe = juce::Component::SafePointer<ChainView> (self)]
                {
                    if (safe != nullptr)
                        safe->looperExportButton.setButtonText ("WAV");
                });
        };
    }

    auto makeLed = [&] (LedButton& led, const char* id, std::unique_ptr<Attachment>& att)
    {
        att = std::make_unique<Attachment> (apvts, id, led);
        led.setTooltip (juce::String (juce::CharPointer_UTF8 ("Enable/disable the module")));
        led.setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (led);
    };
    makeLed (gateLed, "gateOn", gateAtt);
    makeLed (odLed, "odOn", odAtt);
    makeLed (ampLed, "ampOn", ampAtt);
    ampLed.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Enable/disable the amp section (space)")));
    makeLed (cabLed, "cabOn", cabAtt);
    makeLed (eqLed, "eqOn", eqAtt);
    makeLed (delayLed, "delayOn", delayAtt);
    makeLed (revLed, "revOn", revAtt);
    makeLed (compLed, "compOn", compAtt);
    makeLed (preEqLed, "preEqOn", preEqAtt);
    makeLed (pitchLed, "pitchOn", pitchAtt);
    makeLed (looperLed, "looperOn", looperAtt);
    looperLed.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Enable/disable loop monitoring (recording continues)")));
    makeLed (limLed, "limOn", limAtt);
    for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
    {
        const auto prefix = s == 0 ? juce::String ("ext") : "ext" + juce::String (s + 1);
        extAtt[s] = std::make_unique<Attachment> (apvts, prefix + "On", extLed[s]);
        extLed[s].setTooltip (juce::String (juce::CharPointer_UTF8 ("Enable/disable the module")));
        extLed[s].setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (extLed[s]);
    }
    makeLed (wahLed, "wahOn", wahAtt);
    makeLed (harmLed, "harmOn", harmAtt);
    makeLed (octLed, "octOn", octAtt);
    makeLed (rmLed, "rmOn", rmAtt);
    makeLed (bcLed, "bcOn", bcAtt);
    makeLed (sgLed, "sgOn", sgAtt);
    makeLed (excLed, "excOn", excAtt);
    makeLed (dsLed, "dsOn", dsAtt);
    makeLed (tapeLed, "tapeOn", tapeAtt);
    makeLed (cnsLed, "cnsOn", cnsAtt);
    makeLed (anLed, "anOn", anAtt);
    modAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, "modOn", modLed);
    modLed.setTooltip (juce::String (juce::CharPointer_UTF8 ("Enable/disable the module")));
    modLed.setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (modLed);

    // delay TAP tempo
    tapButton.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Tap twice in time with the song to set the delay time")));
    tapButton.setMouseClickGrabsKeyboardFocus (false);
    tapButton.onClick = [this] { applyTapTempo(); };
    addAndMakeVisible (tapButton);

    // compressor presets: set the 4 knobs at once
    {
        struct CompPreset { const char* label; float sustain, attack, blend, level; };
        const CompPreset presets[3] = { { "CLN", 2.5f, 30.0f, 70.0f, 0.0f },
                                        { "CTY", 6.0f, 10.0f, 100.0f, 1.0f },
                                        { "LEAD", 8.0f, 25.0f, 100.0f, 2.0f } };
        for (int i = 0; i < 3; ++i)
        {
            auto& chip = compPresetChips[i];
            chip.setButtonText (presets[i].label);
            chip.getProperties().set ("chip", true);
            chip.setMouseClickGrabsKeyboardFocus (false);
            const CompPreset pr = presets[i];
            chip.onClick = [this, pr]
            {
                auto set = [this] (const char* id, float value)
                {
                    if (auto* param = processor.apvts.getParameter (id))
                        param->setValueNotifyingHost (
                            param->getNormalisableRange().convertTo0to1 (value));
                };
                set ("compSustain", pr.sustain);
                set ("compAttack", pr.attack);
                set ("compBlend", pr.blend);
                set ("compLevel", pr.level);
                set ("compOn", 1.0f);
            };
            addAndMakeVisible (chip);
        }
        compPresetChips[0].setTooltip (juce::String (juce::CharPointer_UTF8 (
            "Clean: light, transparent compression")));
        compPresetChips[1].setTooltip (juce::String (juce::CharPointer_UTF8 (
            "Country: fast Dyna Comp-style squish")));
        compPresetChips[2].setTooltip (juce::String (juce::CharPointer_UTF8 (
            "Lead: maximum sustain for solos")));
    }

    // ECO chip: switches to the light capture version (when it exists)
    ecoChip.getProperties().set ("chip", true);
    ecoChip.setClickingTogglesState (true);
    ecoChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Uses the light capture version (less CPU). Downloaded alongside when the tone offers it.")));
    ecoChip.setMouseClickGrabsKeyboardFocus (false);
    ecoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, "ampEco", ecoChip);
    addAndMakeVisible (ecoChip);

    // knob tooltips
    auto tip = [] (std::unique_ptr<KnobComponent>& k, const char* utf8)
    { k->setKnobTooltip (juce::String (juce::CharPointer_UTF8 (utf8))); };
    tip (gateThreshKnob, "Opens at this level; only closes 6 dB below (preserves sustain)");
    tip (gateHoldKnob, "Holds the gate open after the signal drops");
    tip (gateReleaseKnob, "Time for the gate to close");
    tip (compSustainKnob, "More sustain = more compression (threshold+ratio+makeup)");
    tip (compAttackKnob, "Attack: high lets the pick attack through before compressing");
    tip (compBlendKnob, "Parallel compression: blend with the dry signal");
    tip (compLevelKnob, "Compressor output volume");
    tip (preEqLowKnob, "Bass BEFORE the amp (100 Hz) - changes the saturation");
    tip (preEqMidKnob, "Mids BEFORE the amp (500 Hz)");
    tip (preEqHighKnob, "Treble BEFORE the amp (2.2 kHz)");
    tip (modRateKnob, "Modulation speed");
    tip (modDepthKnob, "Modulation depth");
    tip (modMixKnob, "Effect blend into the signal");
    tip (odDriveKnob, "Amount of pedal saturation");
    tip (odToneKnob, "Overdrive brightness");
    tip (odLevelKnob, "Overdrive volume");
    for (int r = 0; r < maxRigs; ++r)
    {
        tip (ampGainKnob[r], "Pushes the signal into the capture - acts like the real amp's gain");
        tip (ampBassKnob[r], "Bass (150 Hz)");
        tip (ampMidKnob[r], "Mids (500 Hz)");
        tip (ampTrebleKnob[r], "Treble (1.8 kHz)");
        tip (ampPresKnob[r], "Presence (4.5 kHz)");
        tip (ampMasterKnob[r], "Amp section volume");
        tip (cabLcKnob[r], "Cuts this cab's bass (20 Hz = off)");
        tip (cabHcKnob[r], "Cuts this cab's treble (20 kHz = off)");
    }
    tip (eqLowKnob, "Bass after the cab (120 Hz)");
    tip (eqMidKnob, "Mids after the cab (800 Hz)");
    tip (eqHighKnob, "Treble after the cab (4 kHz)");
    tip (delayTimeKnob, "Time between repeats");
    tip (delayFbKnob, "How many repeats (feedback)");
    tip (delayMixKnob, "Delay blend into the signal");
    tip (revDecayKnob, "Reverb size/decay");
    tip (revMixKnob, "Reverb blend into the signal");
    tip (revPreKnob, "Delay before the reverb starts");
    tip (pitchMixKnob, "Blend of the pitched voice with the dry signal");
    tip (pitchLevelKnob, "Pitched voice volume");
    tip (looperLevelKnob, "Loop volume in the mix");
    tip (limCeilKnob, "Limiter ceiling - nothing passes this level");
    tip (limRelKnob, "Recovery time after limiting");
    for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
        tip (extMixKnob[s], "Blend of the hosted plugin with the dry signal");
    tip (wahFreqKnob, "Wah base frequency (pedal position in Manual mode)");
    tip (wahRangeKnob, "How far the envelope/LFO sweeps from FREQ");
    tip (wahResKnob, "Filter resonance (the \"quack\")");
    tip (sgSensKnob, "Sensitivity to picking (when the swell restarts)");
    tip (sgRiseKnob, "Time for the volume to rise after each note");
    tip (octSubKnob, "Volume of the synthetic sub-octave");
    tip (octDirectKnob, "Direct signal volume");
    tip (octToneKnob, "Sub-octave damping");
    tip (rmFreqKnob, "Carrier frequency (low = tremor; high = bells)");
    tip (rmMixKnob, "Effect blend");
    tip (bcBitsKnob, "Bit resolution (less = dirtier)");
    tip (bcRateKnob, "Reduced sample rate (lo-fi aliasing)");
    tip (bcMixKnob, "Effect blend");
    tip (harmMixKnob, "Blend of the second voice");
    tip (harmLevelKnob, "Second voice volume");
    tip (excFreqKnob, "Where the harmonics start being generated");
    tip (excAmtKnob, "How much brightness is added back");
    tip (dsFreqKnob, "Center of the harsh band to tame");
    tip (dsSensKnob, "Detection sensitivity");
    tip (dsAmtKnob, "Maximum depth of the dynamic cut");
    tip (tapeDriveKnob, "Tape saturation");
    tip (tapeBumpKnob, "Head bump: bass boost at 90 Hz");
    tip (tapeRollKnob, "Tape treble rolloff");
    tip (cnsAmtKnob, "Amount of the \"glue\" (subtle sine waveshaping)");

    updateLayout();
}

// amp-focus / cab-focus metrics (mockup FINAL PASS)
namespace
{
// Chain zoom: tightened so more of the chain fits without scrolling. Only the
// slack was taken - kCabFocusW (the CHANGE + VARS row needs 150) and kMiniW
// (the 2-column knob grid needs 110) are at their minimum already.
constexpr int kAmpFocusW = 290;
constexpr int kCabFocusW = 150;
constexpr int kMiniW = 110;      // fx-mini card width
constexpr int kMiniH = 178;      // fx-mini card height
constexpr int kCardGap = 26;     // connector length (the 22 px "+" ring sits in it)
constexpr int kChainPad = 34;    // chain's left/right margin
constexpr int kRigBusW = 14;     // split/sum bus around a rig block
constexpr int kAmpCabGap = 18;   // amp -> cab inside one rig lane
}

// width of the rig block (stacked lanes, constant width):
// split bus + amp + gap + cab + sum bus
int ChainView::rigBlockWidth() const
{
    return kRigBusW + kAmpFocusW + kAmpCabGap + kCabFocusW + kRigBusW;
}

void ChainView::updateLayout()
{
    // fx-mini metrics + connectors; the rig block is dynamic
    int x = kChainPad; // left margin (room for the first connector "+")
    for (const auto& id : processor.getChainOrder())
        x += (id == "amp" ? rigBlockWidth() : effectCardWidth (id)) + kCardGap;
    setSize (x + kChainPad, chainHeight);
}

void ChainView::setChainHeight (int newHeight)
{
    newHeight = juce::jmax (260, newHeight);
    if (chainHeight == newHeight)
        return;
    chainHeight = newHeight;
    applyChainRelayout();
}

void ChainView::setAmpImage (int lane, juce::Image img)
{
    if (lane < 0 || lane >= maxRigs)
        return;
    ampImages[lane] = std::move (img);
    resized();
    repaint();
}

void ChainView::setCabImage (int lane, juce::Image img)
{
    if (lane < 0 || lane >= maxRigs)
        return;
    cabImages[lane] = std::move (img);
    resized();
    repaint();
}

void ChainView::setupTypeButton (juce::TextButton& button, const char* paramId,
                                 const juce::String& tooltip)
{
    button.setTooltip (tooltip);
    button.setMouseClickGrabsKeyboardFocus (false);
    button.onClick = [this, &button, paramId]
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (
            processor.apvts.getParameter (paramId));
        if (param == nullptr)
            return;

        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());
        for (int i = 0; i < param->choices.size(); ++i)
            menu.addItem (i + 1, param->choices[i], true, i == param->getIndex());

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&button),
                            [param] (int result)
                            {
                                if (result > 0)
                                    param->setValueNotifyingHost (
                                        param->convertTo0to1 ((float) (result - 1)));
                            });
    };
    addAndMakeVisible (button);
}

void ChainView::refreshTypeButtons()
{
    auto update = [this] (juce::TextButton& b, const char* id)
    {
        if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (
                processor.apvts.getParameter (id)))
        {
            const auto text = param->getCurrentChoiceName()
                              + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xbe"));
            if (b.getButtonText() != text)
                b.setButtonText (text);
        }
    };
    update (odTypeButton, "odType");
    update (compTypeButton, "compType");
    update (delayTypeButton, "delayType");
    update (revTypeButton, "revType");
    update (modTypeButton, "modType");
    update (delayDivButton, "delayDiv");
    update (pitchTypeButton, "pitchType");
    update (wahModeButton, "wahMode");
    update (harmKeyButton, "harmKey");
    update (harmScaleButton, "harmScale");
    update (harmIntervalButton, "harmInterval");
}

void ChainView::applyTapTempo()
{
    const auto now = juce::Time::currentTimeMillis();
    const auto interval = now - lastTapMs;
    lastTapMs = now;

    if (interval < 120 || interval > 2000)
        return; // first tap (or out of useful range): just arms the next one

    const float factors[] = { 1.0f, 0.5f, 0.75f, 0.25f }; // 1/4, 1/8, 1/8., 1/16
    const int div = juce::jlimit (0, 3, (int) processor.apvts.getRawParameterValue ("delayDiv")->load());
    const float timeMs = juce::jlimit (60.0f, 1000.0f, (float) interval * factors[div]);

    if (auto* param = processor.apvts.getParameter ("delayTime"))
        param->setValueNotifyingHost (param->getNormalisableRange().convertTo0to1 (timeMs));
}

juce::String ChainView::archBadgeForIr (int slot)
{
    const auto path = processor.getIrPath (slot);
    if (path != cabArchPathSeen[slot])
    {
        cabArchPathSeen[slot] = path;
        cabArchCache[slot].clear();
        if (path.isNotEmpty())
        {
            const auto meta = juce::JSON::parse (juce::File (path + ".meta").loadFileAsString());
            const auto arch = meta.getProperty ("arch", "").toString();
            if (arch == "2") cabArchCache[slot] = "A2";
            else if (arch == "1") cabArchCache[slot] = "A1";
        }
    }
    return cabArchCache[slot];
}

int ChainView::toneIdForLane (int lane) const
{
    // cached by path: this is polled by the 30 Hz UI timer and by paint(),
    // so it must not hit the filesystem on every call (vNext P0)
    const auto path = processor.getModelPathNormal (lane);
    if (path == toneIdCachePath[lane])
        return toneIdCacheVal[lane];
    toneIdCachePath[lane] = path;
    toneIdCacheVal[lane] = 0;
    if (path.isNotEmpty())
    {
        const juce::File meta (path + ".meta");
        if (meta.existsAsFile())
            toneIdCacheVal[lane] = (int) juce::JSON::parse (meta.loadFileAsString())
                                             .getProperty ("tone_id", 0);
    }
    return toneIdCacheVal[lane];
}

int ChainView::toneIdForCab (int slot) const
{
    const auto path = processor.getIrPath (slot);
    if (path == cabToneCachePath[slot])
        return cabToneCacheVal[slot];
    cabToneCachePath[slot] = path;
    cabToneCacheVal[slot] = 0;
    if (path.isNotEmpty())
    {
        const juce::File meta (path + ".meta");
        if (meta.existsAsFile())
            cabToneCacheVal[slot] = (int) juce::JSON::parse (meta.loadFileAsString())
                                              .getProperty ("tone_id", 0);
    }
    return cabToneCacheVal[slot];
}

void ChainView::refreshDynamicText()
{
    bool varLayoutChanged = false;
    const int rigCount = processor.getRigCount();
    for (int r = 0; r < maxRigs; ++r)
    {
        const bool loaded = processor.hasModelLoaded (r);
        loadButtons[r].setButtonText (juce::String (juce::CharPointer_UTF8 (
            loaded ? "CHANGE \xe2\x96\xbe" : "LOAD CAPTURE \xe2\x96\xbe")));
        // the variations selector shows whenever a model is loaded on an active
        // lane; it only works for store captures (a tone_id in the .meta), so
        // disable it for disk/old captures and explain via the tooltip.
        const bool showVar = loaded && r < rigCount;
        const bool hasVariations = loaded && toneIdForLane (r) > 0;
        ampVarButtons[r].setVisible (showVar);
        ampVarButtons[r].setEnabled (hasVariations);
        ampVarButtons[r].setTooltip (hasVariations
            ? "Switch to another capture of this TONE3000 tone"
            : "Add this capture from the TONE3000 store to switch between its variations");

        // the CHANGE row reserves space for the button only when it shows, so a
        // load/unload needs a relayout for the button to actually get bounds.
        if (showVar != lastVarLoaded[r])
        {
            lastVarLoaded[r] = showVar;
            varLayoutChanged = true;
        }

        // same for the cab: variations of the loaded IR/cab tone
        const bool cabLoaded = processor.getIrPath (r).isNotEmpty();
        const bool showCabVar = cabLoaded && r < rigCount;
        const bool cabHasVar = cabLoaded && toneIdForCab (r) > 0;
        cabVarButtons[r].setVisible (showCabVar);
        cabVarButtons[r].setEnabled (cabHasVar);
        cabVarButtons[r].setTooltip (cabHasVar
            ? "Switch to another IR of this TONE3000 cab tone"
            : "Add this IR from the TONE3000 store to switch between its variations");
        if (showCabVar != lastCabVarLoaded[r])
        {
            lastCabVarLoaded[r] = showCabVar;
            varLayoutChanged = true;
        }
    }
    if (varLayoutChanged)
        resized();

    ecoChip.setEnabled (processor.hasEcoVariant());
    refreshTypeButtons();

    // looper buttons track the state
    {
        using LS = GuitarRigNAMProcessor::LooperState;
        const auto st = processor.getLooperState();
        const auto rec = st == LS::empty ? juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f REC"))
                         : st == LS::recording ? juce::String ("CLOSE")
                         : st == LS::overdub ? juce::String ("END DUB")
                                             : juce::String ("OVERDUB");
        if (looperRecButton.getButtonText() != rec)
            looperRecButton.setButtonText (rec);
        const auto play = st == LS::playing || st == LS::overdub
                              ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0 STOP"))
                              : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6 PLAY"));
        if (looperPlayButton.getButtonText() != play)
            looperPlayButton.setButtonText (play);
        const bool hasLoop = st != LS::empty;
        looperPlayButton.setEnabled (hasLoop);
        looperClearButton.setEnabled (hasLoop);
        looperExportButton.setEnabled (hasLoop && st != LS::recording);
    }

    // slots VST3
    for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
    {
        const bool hasExt = processor.hasExternalPlugin (s);
        const auto loadText = hasExt ? juce::String ("CHANGE VST3")
                                     : juce::String ("LOAD VST3");
        if (extLoadButton[s].getButtonText() != loadText)
            extLoadButton[s].setButtonText (loadText);
        extUiButton[s].setEnabled (hasExt);
        extRemoveButton[s].setEnabled (hasExt);
    }

    // disabled cards: dim knobs/buttons (the LED stays lit to re-enable)
    {
        static const char* dimIds[] = { "gate", "comp", "od", "preeq", "eq", "mod", "delay",
                                        "reverb", "pitch", "looper", "limiter", "ext", "ext2",
                                        "ext3", "ext4", "ext5", "ext6", "ext7", "ext8",
                                        "wah", "harm", "octaver", "ringmod", "bitcrush",
                                        "slowgear", "exciter", "deesser", "tape", "console",
                                        "analyzer" };
        const auto chain = processor.getChainOrder();
        for (auto* id : dimIds)
        {
            if (! chain.contains (id))
                continue;
            auto* p = processor.apvts.getRawParameterValue (onParamIdForFx (id));
            const float alpha = p != nullptr && p->load() > 0.5f ? 1.0f : 0.4f;
            auto comps = componentsForFx (id);
            for (int i = 1; i < comps.size(); ++i) // 0 = LED, always stays visible
                comps[i]->setAlpha (alpha);
        }
    }

    // rig count or chain order changed -> relayout. With the mouse button
    // pressed, DEFER: a reflow during an ongoing drag makes the slider
    // "jump" (the relative position changes without the mouse moving) and
    // swaps the target under the cursor. The timer retries each tick until release.
    const auto orderNow = processor.getChainOrder().joinIntoString (",");
    if ((processor.getRigCount() != lastRigCount || orderNow != lastOrderSeen)
        && ! juce::Component::isMouseButtonDownAnywhere())
    {
        applyChainRelayout();
    }
    repaint();
}

//==============================================================================
// Effects drawer: mappings by id (components, On param, name)

int ChainView::extSlotForId (const juce::String& id)
{
    if (id == "ext")
        return 0;
    if (id.startsWith ("ext"))
    {
        const auto rest = id.substring (3);
        if (rest.isNotEmpty() && rest.containsOnly ("0123456789")) // excludes "exciter"
        {
            const int n = rest.getIntValue();
            if (n >= 2 && n <= GuitarRigNAMProcessor::maxExtSlots)
                return n - 1;
        }
    }
    return -1;
}

juce::Array<juce::Component*> ChainView::componentsForFx (const juce::String& id)
{
    if (const int s = extSlotForId (id); s >= 0)
        return { &extLed[s], extMixKnob[s].get(), &extLoadButton[s], &extUiButton[s],
                 &extRemoveButton[s] };
    // convention: the LED is always first (stays out of the dimming)
    if (id == "gate")   return { &gateLed, gateThreshKnob.get(), gateHoldKnob.get(), gateReleaseKnob.get() };
    if (id == "comp")   return { &compLed, compSustainKnob.get(), compAttackKnob.get(), compBlendKnob.get(),
                                 compLevelKnob.get(), &compTypeButton, &compPresetChips[0],
                                 &compPresetChips[1], &compPresetChips[2] };
    if (id == "od")     return { &odLed, odDriveKnob.get(), odToneKnob.get(), odLevelKnob.get(), &odTypeButton };
    if (id == "preeq")  return { &preEqLed, preEqLowKnob.get(), preEqMidKnob.get(), preEqHighKnob.get() };
    if (id == "eq")     return { &eqLed, eqLowKnob.get(), eqMidKnob.get(), eqHighKnob.get() };
    if (id == "mod")    return { &modLed, modRateKnob.get(), modDepthKnob.get(), modMixKnob.get(), &modTypeButton };
    if (id == "delay")  return { &delayLed, delayTimeKnob.get(), delayFbKnob.get(), delayMixKnob.get(),
                                 &delayTypeButton, &delayDivButton, &tapButton };
    if (id == "reverb") return { &revLed, revDecayKnob.get(), revMixKnob.get(), revPreKnob.get(), &revTypeButton };
    if (id == "pitch")  return { &pitchLed, pitchMixKnob.get(), pitchLevelKnob.get(), &pitchTypeButton };
    if (id == "looper") return { &looperLed, looperLevelKnob.get(), &looperRecButton, &looperPlayButton,
                                 &looperClearButton, &looperExportButton };
    if (id == "limiter") return { &limLed, limCeilKnob.get(), limRelKnob.get() };
    if (id == "wah")    return { &wahLed, wahFreqKnob.get(), wahRangeKnob.get(), wahResKnob.get(), &wahModeButton };
    if (id == "harm")   return { &harmLed, harmMixKnob.get(), harmLevelKnob.get(), &harmKeyButton,
                                 &harmScaleButton, &harmIntervalButton };
    if (id == "octaver") return { &octLed, octSubKnob.get(), octDirectKnob.get(), octToneKnob.get() };
    if (id == "ringmod") return { &rmLed, rmFreqKnob.get(), rmMixKnob.get() };
    if (id == "bitcrush") return { &bcLed, bcBitsKnob.get(), bcRateKnob.get(), bcMixKnob.get() };
    if (id == "slowgear") return { &sgLed, sgSensKnob.get(), sgRiseKnob.get() };
    if (id == "exciter") return { &excLed, excFreqKnob.get(), excAmtKnob.get() };
    if (id == "deesser") return { &dsLed, dsFreqKnob.get(), dsSensKnob.get(), dsAmtKnob.get() };
    if (id == "tape")   return { &tapeLed, tapeDriveKnob.get(), tapeBumpKnob.get(), tapeRollKnob.get() };
    if (id == "console") return { &cnsLed, cnsAmtKnob.get() };
    if (id == "analyzer") return { &anLed };
    return {};
}

juce::String ChainView::onParamIdForFx (const juce::String& id) const
{
    if (const int s = extSlotForId (id); s >= 0)
        return (s == 0 ? juce::String ("ext") : "ext" + juce::String (s + 1)) + "On";
    if (id == "gate") return "gateOn";
    if (id == "comp") return "compOn";
    if (id == "od") return "odOn";
    if (id == "preeq") return "preEqOn";
    if (id == "eq") return "eqOn";
    if (id == "mod") return "modOn";
    if (id == "delay") return "delayOn";
    if (id == "reverb") return "revOn";
    if (id == "pitch") return "pitchOn";
    if (id == "looper") return "looperOn";
    if (id == "limiter") return "limOn";
    if (id == "wah") return "wahOn";
    if (id == "harm") return "harmOn";
    if (id == "octaver") return "octOn";
    if (id == "ringmod") return "rmOn";
    if (id == "bitcrush") return "bcOn";
    if (id == "slowgear") return "sgOn";
    if (id == "exciter") return "excOn";
    if (id == "deesser") return "dsOn";
    if (id == "tape") return "tapeOn";
    if (id == "console") return "cnsOn";
    if (id == "analyzer") return "anOn";
    return {};
}

juce::String ChainView::fxDisplayName (const juce::String& id)
{
    if (const int s = extSlotForId (id); s >= 0)
        return "Plugin VST3 " + juce::String (s + 1);
    if (id == "gate") return "Noise Gate";
    if (id == "comp") return "Compressor";
    if (id == "od") return "Drive";
    if (id == "preeq") return juce::String (juce::CharPointer_UTF8 ("Pre-EQ"));
    if (id == "eq") return "EQ";
    if (id == "mod") return juce::String (juce::CharPointer_UTF8 ("Modulation"));
    if (id == "delay") return "Delay";
    if (id == "reverb") return "Reverb";
    if (id == "pitch") return "Pitch";
    if (id == "looper") return "Looper";
    if (id == "limiter") return "Limiter";
    if (id == "wah") return "Wah";
    if (id == "harm") return "Harmonizer";
    if (id == "octaver") return "Octaver";
    if (id == "ringmod") return "Ring Mod";
    if (id == "bitcrush") return "Bitcrusher";
    if (id == "slowgear") return "Slow Gear";
    if (id == "exciter") return "Exciter";
    if (id == "deesser") return "De-esser";
    if (id == "tape") return "Tape";
    if (id == "console") return "Console";
    if (id == "analyzer") return "Analyzer";
    return id;
}

std::vector<std::pair<juce::Rectangle<int>, int>> ChainView::insertSpots() const
{
    // One "+" CENTRED in every gap: insert BEFORE card i = index i, and a
    // trailing "+" that appends at the end. The gaps are measured against the
    // same virtual input/output nodes paint() runs its connectors between, so
    // the "+" is always half-way and never sits on a connector's endpoint dot
    // (the trailing one used to be pinned 15 px off the last card, which left
    // the 7 px start node poking out of the ring like a stray period).
    std::vector<std::pair<juce::Rectangle<int>, int>> spots;
    auto centredIn = [this] (int gapLeft, int gapRight, int idx)
    {
        const int midX = (gapLeft + gapRight) / 2;
        return std::make_pair (
            juce::Rectangle<int> (midX - 11, chainHeight / 2 - 11, 22, 22), idx);
    };

    juce::Rectangle<int> prev (0, chainHeight / 2, 8, 1);   // virtual input node
    const auto entries = orderedEntries();
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& box = entries[(size_t) i].box;
        if (box.isEmpty())
            continue;
        spots.push_back (centredIn (prev.getRight(), box.getX(), i));
        prev = box;
    }
    if (! entries.empty() && ! prev.isEmpty() && prev.getRight() > 8)
        spots.push_back (centredIn (prev.getRight(), getWidth() - 10,   // virtual output node
                                    (int) entries.size()));
    return spots;
}

juce::String ChainView::fxDisplayNamePublic (const juce::String& id)
{
    return fxDisplayName (id);
}

void ChainView::insertFxAt (const juce::String& id, int insertIndex)
{
    auto order = processor.getChainOrder();
    if (order.contains (id))
        return;
    int pos;
    if (insertIndex >= 0)
    {
        pos = juce::jlimit (0, order.size(), insertIndex);
    }
    else
    {
        const int rank = GuitarRigNAMProcessor::canonicalRank (id);
        pos = order.size();
        for (int i = 0; i < order.size(); ++i)
            if (GuitarRigNAMProcessor::canonicalRank (order[i]) > rank)
            {
                pos = i;
                break;
            }
    }
    order.insert (pos, id);
    processor.setChainOrder (order);
    applyChainRelayout();
}

void ChainView::showAddFxMenu (int insertIndex, juce::Rectangle<int> targetArea)
{
    // vNext: prefer the searchable drawer when the host wired it
    if (onOpenFxBrowser != nullptr)
    {
        onOpenFxBrowser (insertIndex);
        return;
    }
    struct Category { const char* title; std::initializer_list<const char*> ids; };
    static const Category categories[] = {
        { "Dynamics",              { "gate", "comp", "slowgear", "limiter" } },
        { "Drive & Filter",        { "wah", "od", "octaver", "ringmod", "bitcrush", "preeq" } },
        { "Pitch",                 { "pitch", "harm" } },
        { "Modulation & Color",    { "mod", "exciter", "deesser", "tape", "console" } },
        { "Ambience",              { "delay", "reverb" } },
        { "Extras",                { "ext", "ext2", "ext3", "ext4", "ext5", "ext6",
                                     "ext7", "ext8", "looper", "analyzer" } },
    };

    const auto order = processor.getChainOrder();
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    bool any = false;

    for (const auto& cat : categories)
    {
        bool catAny = false;
        for (auto* id : cat.ids)
            if (! order.contains (id))
                catAny = true;
        if (! catAny)
            continue;

        menu.addSectionHeader (juce::String (juce::CharPointer_UTF8 (cat.title)));
        for (auto* id : cat.ids)
            if (! order.contains (id))
                menu.addItem (GuitarRigNAMProcessor::fxFromString (id) + 1,
                              fxDisplayName (id));
        any = true;
    }

    if (! any)
        menu.addItem (99999, juce::String (juce::CharPointer_UTF8 (
                          "All effects are already in the chain")), false);

    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetScreenArea (
            juce::Rectangle<int> (targetArea.getWidth(), 1)
                .withPosition (localPointToGlobal (targetArea.getPosition()))),
        [safe = juce::Component::SafePointer<ChainView> (this), insertIndex] (int result)
        {
            if (safe == nullptr || result <= 0 || result >= 99999)
                return;
            const auto id = GuitarRigNAMProcessor::fxToString (
                (GuitarRigNAMProcessor::ChainFx) (result - 1));

            auto order = safe->processor.getChainOrder();
            int pos;
            if (insertIndex >= 0)
            {
                // connector "+": lands exactly where it was clicked
                pos = juce::jlimit (0, order.size(), insertIndex);
            }
            else
            {
                // end button: canonical position (can be dragged afterward)
                const int rank = GuitarRigNAMProcessor::canonicalRank (id);
                pos = order.size();
                for (int i = 0; i < order.size(); ++i)
                    if (GuitarRigNAMProcessor::canonicalRank (order[i]) > rank)
                    {
                        pos = i;
                        break;
                    }
            }
            order.insert (pos, id);
            safe->processor.setChainOrder (order);
            safe->applyChainRelayout();
        });
}

void ChainView::removeFxFromChain (const juce::String& id)
{
    auto order = processor.getChainOrder();
    order.removeString (id);
    processor.setChainOrder (order);
    if (selectedFxId == id)
        selectedFxId.clear();
    if (expandedFxId == id)
        expandedFxId.clear();
    applyChainRelayout(); // layout updates immediately, not on the next tick
}

void ChainView::applyChainRelayout()
{
    lastRigCount = processor.getRigCount();
    lastOrderSeen = processor.getChainOrder().joinIntoString (",");
    updateLayout();
    resized();
    repaint();
}

//==============================================================================
// Reordering drag-and-drop

void ChainView::mouseDown (const juce::MouseEvent& e)
{
    draggingId.clear();
    panning = false;

    // connector "+": adds an effect AT THAT position
    for (const auto& [rect, idx] : insertSpots())
        if (rect.contains (e.getPosition()))
        {
            showAddFxMenu (idx, rect);
            return;
        }

    // "x" removes the effect from the chain (back to the drawer, settings preserved).
    // Synchronous + immediate relayout: fast successive clicks never land
    // on a stale layout (wrong knob/x sliding under the mouse).
    for (const auto& entry : orderedEntries())
        if (entry.id != "amp" && removeHotspot (entry.box).contains (e.getPosition()))
        {
            removeFxFromChain (entry.id);
            return;
        }

    // Clicks on knobs/buttons go to the children; only the card background
    // reaches here. Amp+cabs are an anchor and cannot be dragged.
    for (const auto& entry : orderedEntries())
        if (entry.id != "amp" && entry.box.contains (e.getPosition()))
        {
            draggingId = entry.id;
            dragGrabDx = e.x - entry.box.getX();
            dragMouseX = (float) e.x;
            dropIndex = -1;
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            break;
        }

    // empty background (or amp block): dragging pans the chain
    if (draggingId.isEmpty())
        if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        {
            panning = true;
            panStartMouse = e.getScreenPosition();
            panStartView = vp->getViewPosition();
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        }
}

void ChainView::mouseDrag (const juce::MouseEvent& e)
{
    if (panning)
    {
        if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        {
            const int dx = e.getScreenPosition().x - panStartMouse.x;
            vp->setViewPosition (juce::jmax (0, panStartView.x - dx), panStartView.y);
        }
        return;
    }

    if (draggingId.isEmpty())
        return;

    dragMouseX = (float) e.x;

    // insertion index: before the first entry whose center is to the
    // right of the mouse
    const auto entries = orderedEntries();
    dropIndex = (int) entries.size();
    for (int i = 0; i < (int) entries.size(); ++i)
        if (e.x < entries[(size_t) i].box.getCentreX())
        {
            dropIndex = i;
            break;
        }
    repaint();
}

void ChainView::mouseMove (const juce::MouseEvent& e)
{
    // microinteraction: highlights the "+"/"x" under the mouse
    juce::Rectangle<int> hot;
    for (const auto& [rect, idx] : insertSpots())
        if (rect.contains (e.getPosition()))
        {
            hot = rect;
            break;
        }
    if (hot.isEmpty())
        for (const auto& entry : orderedEntries())
            if (entry.id != "amp" && removeHotspot (entry.box).contains (e.getPosition()))
            {
                hot = removeHotspot (entry.box);
                break;
            }
    if (hot != hoverHotspot)
    {
        hoverHotspot = hot;
        setMouseCursor (hot.isEmpty() ? juce::MouseCursor::NormalCursor
                                      : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void ChainView::mouseExit (const juce::MouseEvent&)
{
    if (! hoverHotspot.isEmpty())
    {
        hoverHotspot = {};
        repaint();
    }
}

//==============================================================================
// File drag-and-drop: .nam -> amp, IR -> cab, .vst3 -> external slot

static bool isNamFile (const juce::String& f) { return f.endsWithIgnoreCase (".nam"); }
static bool isIrFile (const juce::String& f)
{
    return f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif")
           || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".flac");
}
static bool isVst3File (const juce::String& f) { return f.endsWithIgnoreCase (".vst3"); }

bool ChainView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isNamFile (f) || isIrFile (f) || isVst3File (f))
            return true;
    return false;
}

std::pair<juce::Rectangle<int>, juce::String> ChainView::dropTargetAt (const juce::String& file,
                                                                       int x, int y) const
{
    const auto pos = juce::Point<int> (x, y);
    const int count = processor.getRigCount();

    if (isNamFile (file))
    {
        for (int r = 0; r < count; ++r)
            if (ampLaneB[r].contains (pos))
                return { ampLaneB[r], "nam:" + juce::String (r) };
        // outside an amp: first free lane (or the 1st)
        const int lane = juce::jmax (0, processor.firstFreeModelLane());
        return { ampLaneB[juce::jlimit (0, count - 1, lane)], "nam:" + juce::String (lane) };
    }
    if (isIrFile (file))
    {
        for (int r = 0; r < count; ++r)
            if (cabLaneB[r].contains (pos))
                return { cabLaneB[r], "ir:" + juce::String (r) };
        const int slot = juce::jmax (0, processor.firstFreeIrSlot());
        return { cabLaneB[juce::jlimit (0, count - 1, slot)], "ir:" + juce::String (slot) };
    }
    if (isVst3File (file))
    {
        // slot under the cursor; else the first visible empty slot; else the 1st
        for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
            if (! extB[s].isEmpty() && extB[s].contains (pos))
                return { extB[s], "vst3:" + juce::String (s) };
        for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
            if (! extB[s].isEmpty() && ! processor.hasExternalPlugin (s))
                return { extB[s], "vst3:" + juce::String (s) };
        return { extB[0], "vst3:0" };
    }
    return { {}, {} };
}

void ChainView::fileDragMove (const juce::StringArray& files, int x, int y)
{
    juce::Rectangle<int> target;
    if (! files.isEmpty())
        target = dropTargetAt (files[0], x, y).first;
    if (target != dropHighlight)
    {
        dropHighlight = target;
        repaint();
    }
}

void ChainView::fileDragExit (const juce::StringArray&)
{
    if (! dropHighlight.isEmpty())
    {
        dropHighlight = {};
        repaint();
    }
}

void ChainView::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropHighlight = {};
    for (const auto& f : files)
    {
        const auto [rect, action] = dropTargetAt (f, x, y);
        if (action.startsWith ("nam:"))
            processor.setModelPair (action.fromFirstOccurrenceOf (":", false, false).getIntValue(),
                                    juce::File (f), {});
        else if (action.startsWith ("ir:"))
            processor.loadIrAsync (action.fromFirstOccurrenceOf (":", false, false).getIntValue(),
                                   juce::File (f));
        else if (action.startsWith ("vst3:"))
            processor.loadExternalPluginAsync (
                action.fromFirstOccurrenceOf (":", false, false).getIntValue(), juce::File (f));
    }
    repaint();
}

void ChainView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    // mouse wheel scrolls the chain (there is no vertical scroll here)
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
    {
        const int dx = juce::roundToInt ((wheel.deltaY + wheel.deltaX) * 480.0f);
        vp->setViewPosition (juce::jmax (0, vp->getViewPositionX() - dx),
                             vp->getViewPositionY());
    }
}

void ChainView::mouseUp (const juce::MouseEvent& e)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
    panning = false;

    // vNext fx-mini: a plain click (no drag) on a card selects it; a click on
    // the already-selected card (or a double-click) expands it inline with the
    // full knob layout. Clicking an expanded card's background collapses it.
    if (e.mouseWasClicked() && dropIndex < 0)
    {
        for (const auto& en : orderedEntries())
        {
            if (en.id == "amp" || ! en.box.contains (e.getPosition()))
                continue;
            if (removeHotspot (en.box).contains (e.getPosition()))
                break;   // the "x" already handled it - don't also select
            const bool wasSelected = selectedFxId == en.id;
            const bool wasExpanded = expandedFxId == en.id;
            if (wasExpanded)
                expandedFxId.clear();
            else if (wasSelected || e.getNumberOfClicks() >= 2)
                expandedFxId = en.id;
            selectedFxId = en.id;
            draggingId.clear();
            dropIndex = -1;
            dragMouseX = -1.0f;
            applyChainRelayout();
            if (expandedFxId == en.id)
                centreFxInViewport (en.id);  // double-click centers (mockup hint)
            repaint();
            return;
        }
    }

    if (draggingId.isNotEmpty() && dropIndex >= 0)
    {
        auto order = processor.getChainOrder();
        const int from = order.indexOf (draggingId);
        if (from >= 0)
        {
            int to = dropIndex;
            order.remove (from);
            if (to > from)
                --to;
            order.insert (juce::jlimit (0, order.size(), to), draggingId);
            processor.setChainOrder (order);
        }
    }

    draggingId.clear();
    dropIndex = -1;
    dragMouseX = -1.0f;
    repaint();
}

int ChainView::effectCardWidth (const juce::String& id) const
{
    // fx-mini default; the expanded card keeps the full (classic) width
    if (id != expandedFxId)
        return kMiniW;
    if (id == "eq" || id == "preeq" || id == "looper" || id == "harm" || id == "analyzer"
        || extSlotForId (id) >= 0)
        return 176;
    return 132;
}

void ChainView::centreFxInViewport (const juce::String& id)
{
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
    {
        const auto b = boxForFx (id);
        if (! b.isEmpty())
            vp->setViewPosition (juce::jmax (0, b.getCentreX() - vp->getWidth() / 2),
                                 vp->getViewPositionY());
    }
}

std::vector<std::pair<juce::Rectangle<int>, bool>> ChainView::minimapBlocks() const
{
    std::vector<std::pair<juce::Rectangle<int>, bool>> out;
    for (const auto& en : orderedEntries())
        if (! en.box.isEmpty())
            out.push_back ({ en.box, en.id == "amp" });
    return out;
}

juce::TextButton* ChainView::typeButtonForFx (const juce::String& id)
{
    if (id == "od")     return &odTypeButton;
    if (id == "comp")   return &compTypeButton;
    if (id == "delay")  return &delayTypeButton;
    if (id == "reverb") return &revTypeButton;
    if (id == "mod")    return &modTypeButton;
    if (id == "pitch")  return &pitchTypeButton;
    if (id == "wah")    return &wahModeButton;
    if (id == "harm")   return &harmIntervalButton;
    return nullptr;
}

juce::String ChainView::miniFooterFor (const juce::String& id) const
{
    if (const int s = extSlotForId (id); s >= 0)
    {
        const auto n = processor.getExternalPluginName (s);
        return n.isNotEmpty() ? n.toUpperCase() : juce::String ("VST3 SLOT");
    }
    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    if (id == "gate")     return "SMART" + dot + "6 dB HYST";
    if (id == "preeq")    return "3-BAND PRE";
    if (id == "eq")       return "3-BAND POST";
    if (id == "limiter")  return "BRICKWALL";
    if (id == "octaver")  return "ANALOG SUB";
    if (id == "ringmod")  return "SINE CARRIER";
    if (id == "bitcrush") return "LO-FI";
    if (id == "slowgear") return "AUTO SWELL";
    if (id == "exciter")  return "HARMONICS";
    if (id == "deesser")  return "DYNAMIC CUT";
    if (id == "tape")     return "SATURATION";
    if (id == "console")  return "BUSS GLUE";
    if (id == "analyzer") return "SPECTRUM";
    if (id == "looper")   return "60 s" + dot + "WAV";
    return {};
}

juce::Rectangle<int> ChainView::boxForFx (const juce::String& id) const
{
    if (id == "gate") return gateB;
    if (id == "od") return odB;
    if (id == "eq") return eqB;
    if (id == "delay") return delayB;
    if (id == "reverb") return revB;
    if (id == "comp") return compB;
    if (id == "preeq") return preEqB;
    if (id == "mod") return modB;
    if (id == "pitch") return pitchB;
    if (id == "looper") return looperB;
    if (id == "limiter") return limB;
    if (const int s = extSlotForId (id); s >= 0) return extB[s];
    if (id == "wah") return wahB;
    if (id == "harm") return harmB;
    if (id == "octaver") return octB;
    if (id == "ringmod") return rmB;
    if (id == "bitcrush") return bcB;
    if (id == "slowgear") return sgB;
    if (id == "exciter") return excB;
    if (id == "deesser") return dsB;
    if (id == "tape") return tapeB;
    if (id == "console") return cnsB;
    if (id == "analyzer") return anB;
    // "amp" = whole rigs block (all stacked amp+cab lanes)
    auto block = ampLaneB[0];
    for (int r = 0; r < maxRigs; ++r)
    {
        if (! ampLaneB[r].isEmpty()) block = block.getUnion (ampLaneB[r]);
        if (! cabLaneB[r].isEmpty()) block = block.getUnion (cabLaneB[r]);
    }
    return block;
}

std::vector<ChainView::ChainEntry> ChainView::orderedEntries() const
{
    std::vector<ChainEntry> out;
    for (const auto& id : processor.getChainOrder())
        out.push_back ({ id, boxForFx (id) });
    return out;
}

void ChainView::resized()
{
    const int H = chainHeight;
    auto cardY = [H] (int cardH) { return (H - cardH) / 2; };

    // drawer: clear the boxes and hide the effect components outside the
    // chain; the present ones reappear when positioned below
    static const char* allFxIds[] = { "gate", "comp", "od", "preeq", "eq", "mod", "delay",
                                      "reverb", "pitch", "looper", "limiter", "ext", "ext2",
                                      "ext3", "ext4", "ext5", "ext6", "ext7", "ext8",
                                      "wah", "harm", "octaver", "ringmod", "bitcrush",
                                      "slowgear", "exciter", "deesser", "tape", "console",
                                      "analyzer" };
    const auto chain = processor.getChainOrder();
    for (auto* idc : allFxIds)
    {
        const juce::String id (idc);
        const bool present = chain.contains (id);
        // fx-mini: collapsed cards keep the LED, the first 2 knobs and the
        // type selector; the expanded card shows everything
        const bool mini = present && id != expandedFxId;
        auto comps = componentsForFx (id);
        auto* typeBtn = typeButtonForFx (id);
        int knobsShown = 0;
        for (int i = 0; i < comps.size(); ++i)
        {
            auto* c = comps.getUnchecked (i);
            bool vis = present;
            if (mini && i > 0)
            {
                if (dynamic_cast<KnobComponent*> (c) != nullptr)
                    vis = ++knobsShown <= 2;
                else
                    vis = c == typeBtn;
            }
            c->setVisible (vis);
        }
    }
    gateB = odB = eqB = delayB = revB = compB = preEqB = pitchB = looperB = limB = {};
    for (auto& b : extB)
        b = {};
    wahB = harmB = octB = rmB = bcB = sgB = excB = dsB = tapeB = cnsB = anB = {};

    // position the cards following the chain's dynamic order
    const int bigH = juce::jmin (330, H - 16);   // expanded effect card height
    const int miniH = juce::jmin (kMiniH, H - 16);
    int x = kChainPad;

    for (const auto& id : processor.getChainOrder())
    {
        if (id == "amp")
        {
            // STACKED AMP+CAB lanes (true parallel): one row per rig, split
            // bus on the left and sum bus on the right (mix in the OUTPUT card)
            const int count = processor.getRigCount();
            const int rowGap = 12, busW = kRigBusW;
            const int availH = H - 20;
            const int rowH = juce::jmin (390, (availH - (count - 1) * rowGap) / count);
            const int totalH = count * rowH + (count - 1) * rowGap;
            const int topY = (H - totalH) / 2;
            const int pairX = x + busW;

            for (int r = 0; r < GuitarRigNAMProcessor::maxRigs; ++r)
            {
                if (r >= count)
                {
                    ampLaneB[r] = cabLaneB[r] = {};
                    continue;
                }
                const int ry = topY + r * (rowH + rowGap);
                const int cabH = juce::jmin (rowH, 290);
                ampLaneB[r] = { pairX, ry, kAmpFocusW, rowH };
                cabLaneB[r] = { pairX + kAmpFocusW + kAmpCabGap, ry + (rowH - cabH) / 2,
                                kCabFocusW, cabH };
            }
            x = cabLaneB[0].getRight() + busW + kCardGap;
        }
        else
        {
            const int w = effectCardWidth (id);
            const int cardH = id == expandedFxId ? bigH : miniH;
            auto box = juce::Rectangle<int> { x, cardY (cardH), w, cardH };
            if (id == "gate") gateB = box;
            else if (id == "od") odB = box;
            else if (id == "eq") eqB = box;
            else if (id == "delay") delayB = box;
            else if (id == "reverb") revB = box;
            else if (id == "comp") compB = box;
            else if (id == "preeq") preEqB = box;
            else if (id == "mod") modB = box;
            else if (id == "pitch") pitchB = box;
            else if (id == "looper") looperB = box;
            else if (id == "limiter") limB = box;
            else if (const int es = extSlotForId (id); es >= 0) extB[es] = box;
            else if (id == "wah") wahB = box;
            else if (id == "harm") harmB = box;
            else if (id == "octaver") octB = box;
            else if (id == "ringmod") rmB = box;
            else if (id == "bitcrush") bcB = box;
            else if (id == "slowgear") sgB = box;
            else if (id == "exciter") excB = box;
            else if (id == "deesser") dsB = box;
            else if (id == "tape") tapeB = box;
            else if (id == "console") cnsB = box;
            else if (id == "analyzer") anB = box;
            x += w + kCardGap;
        }
    }

    // ---- generic pedal: knobs wrapped in 2 columns (46 px)
    auto layoutPedal = [] (juce::Rectangle<int> b, LedButton& led,
                           std::initializer_list<KnobComponent*> knobs)
    {
        led.setBounds (b.getRight() - 12 - 18, b.getY() + 10, 18, 18);
        const int kw = 46, kh = kw + 26, gapX = 11, gapY = 12;
        const int n = (int) knobs.size();
        const int rows = (n + 1) / 2;
        const int blockH = rows * kh + (rows - 1) * gapY;
        int i = 0;
        for (auto* k : knobs)
        {
            const int row = i / 2;
            const int inRow = juce::jmin (2, n - row * 2);
            const int rowW = inRow * kw + (inRow - 1) * gapX;
            const int rx = b.getCentreX() - rowW / 2 + (i % 2) * (kw + gapX);
            const int ry = b.getY() + 52 + (b.getHeight() - 52 - 88 - blockH) / 2 + row * (kh + gapY);
            k->setBounds (rx, ry, kw, kh);
            ++i;
        }
    };

    layoutPedal (gateB, gateLed, { gateThreshKnob.get(), gateHoldKnob.get(),
                                   gateReleaseKnob.get() });
    layoutPedal (odB, odLed, { odDriveKnob.get(), odToneKnob.get(), odLevelKnob.get() });
    layoutPedal (delayB, delayLed, { delayTimeKnob.get(), delayFbKnob.get(), delayMixKnob.get() });
    layoutPedal (revB, revLed, { revDecayKnob.get(), revMixKnob.get(), revPreKnob.get() });
    layoutPedal (compB, compLed, { compSustainKnob.get(), compAttackKnob.get(),
                                   compBlendKnob.get(), compLevelKnob.get() });
    layoutPedal (modB, modLed, { modRateKnob.get(), modDepthKnob.get(), modMixKnob.get() });
    layoutPedal (pitchB, pitchLed, { pitchMixKnob.get(), pitchLevelKnob.get() });
    layoutPedal (limB, limLed, { limCeilKnob.get(), limRelKnob.get() });
    layoutPedal (wahB, wahLed, { wahFreqKnob.get(), wahRangeKnob.get(), wahResKnob.get() });
    layoutPedal (sgB, sgLed, { sgSensKnob.get(), sgRiseKnob.get() });
    layoutPedal (octB, octLed, { octSubKnob.get(), octDirectKnob.get(), octToneKnob.get() });
    layoutPedal (rmB, rmLed, { rmFreqKnob.get(), rmMixKnob.get() });
    layoutPedal (bcB, bcLed, { bcBitsKnob.get(), bcRateKnob.get(), bcMixKnob.get() });
    layoutPedal (excB, excLed, { excFreqKnob.get(), excAmtKnob.get() });
    layoutPedal (dsB, dsLed, { dsFreqKnob.get(), dsSensKnob.get(), dsAmtKnob.get() });
    layoutPedal (tapeB, tapeLed, { tapeDriveKnob.get(), tapeBumpKnob.get(), tapeRollKnob.get() });
    layoutPedal (cnsB, cnsLed, { cnsAmtKnob.get() });
    anLed.setBounds (anB.getRight() - 12 - 18, anB.getY() + 10, 18, 18);

    // harmonizer: 3 selectors (KEY/SCALE/INTERVAL) + MIX/LEVEL
    {
        harmLed.setBounds (harmB.getRight() - 12 - 18, harmB.getY() + 10, 18, 18);
        const int bx = harmB.getX() + 12, bw = harmB.getWidth() - 24;
        harmKeyButton.setBounds (bx, harmB.getY() + 36, bw / 2 - 3, 24);
        harmScaleButton.setBounds (bx + bw / 2 + 3, harmB.getY() + 36, bw / 2 - 3, 24);
        harmIntervalButton.setBounds (bx, harmB.getY() + 66, bw, 24);
        harmMixKnob->setBounds (harmB.getCentreX() - 52, harmB.getY() + 130, 46, 46 + 26);
        harmLevelKnob->setBounds (harmB.getCentreX() + 6, harmB.getY() + 130, 46, 46 + 26);
    }

    // VST3 slots: MIX + stacked LOAD/PANEL/REMOVE buttons
    for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
    {
        const auto& b = extB[s];
        extLed[s].setBounds (b.getRight() - 12 - 18, b.getY() + 10, 18, 18);
        extMixKnob[s]->setBounds (b.getCentreX() - 23, b.getY() + 92, 46, 46 + 26);
        const int bx = b.getX() + 12, bw = b.getWidth() - 24;
        extLoadButton[s].setBounds (bx, b.getBottom() - 12 - 24 - 60, bw, 24);
        extUiButton[s].setBounds (bx, b.getBottom() - 12 - 24 - 30, bw, 24);
        extRemoveButton[s].setBounds (bx, b.getBottom() - 12 - 24, bw, 24);
    }

    // looper: LOOP (level) + 2x2 button grid
    {
        looperLed.setBounds (looperB.getRight() - 12 - 18, looperB.getY() + 10, 18, 18);
        looperLevelKnob->setBounds (looperB.getCentreX() - 23, looperB.getY() + 64, 46, 46 + 26);
        const int bw = (looperB.getWidth() - 24 - 8) / 2, bh = 30;
        const int bx = looperB.getX() + 12;
        const int by = looperB.getY() + 168;
        looperRecButton.setBounds (bx, by, bw, bh);
        looperPlayButton.setBounds (bx + bw + 8, by, bw, bh);
        looperClearButton.setBounds (bx, by + bh + 8, bw, bh);
        looperExportButton.setBounds (bx + bw + 8, by + bh + 8, bw, bh);
    }

    // TAP + subdivision on the delay card (row above the model selector)
    tapButton.setBounds (delayB.getX() + 12, delayB.getBottom() - 96, 50, 24);
    delayDivButton.setBounds (delayB.getX() + 12 + 54, delayB.getBottom() - 96,
                              delayB.getWidth() - 24 - 54, 24);

    // compressor preset chips (row under the title)
    {
        const int cw = 36;
        int px = compB.getX() + (compB.getWidth() - (3 * cw + 2 * 4)) / 2;
        for (auto& chip : compPresetChips)
        {
            chip.setBounds (px, compB.getY() + 32, cw, 18);
            px += cw + 4;
        }
    }

    // variation selectors (card footer, in place of the text)
    auto placeTypeButton = [] (juce::TextButton& b, juce::Rectangle<int> card)
    {
        b.setBounds (card.getX() + 12, card.getBottom() - 66, card.getWidth() - 24, 24);
    };
    placeTypeButton (odTypeButton, odB);
    placeTypeButton (compTypeButton, compB);
    placeTypeButton (delayTypeButton, delayB);
    placeTypeButton (revTypeButton, revB);
    placeTypeButton (modTypeButton, modB);
    placeTypeButton (pitchTypeButton, pitchB);
    placeTypeButton (wahModeButton, wahB);

    // pre-EQ: same layout as the EQ
    {
        preEqLed.setBounds (preEqB.getRight() - 13 - 18, preEqB.getY() + 10, 18, 18);
        const int kw = 44, kh = kw + 26, gap = 13;
        const int gx = preEqB.getCentreX() - (3 * kw + 2 * gap) / 2;
        const int ky = preEqB.getY() + 130;
        preEqLowKnob->setBounds (gx, ky, kw, kh);
        preEqMidKnob->setBounds (gx + kw + gap, ky, kw, kh);
        preEqHighKnob->setBounds (gx + 2 * (kw + gap), ky, kw, kh);
    }

    // ---- parallel rigs: AMP+CAB pair per lane (mix lives in the OUTPUT card)
    {
        const int count = processor.getRigCount();

        for (int r = 0; r < GuitarRigNAMProcessor::maxRigs; ++r)
        {
            const bool active = r < count;
            const auto ampB = ampLaneB[r];
            const auto cabB = cabLaneB[r];
            const bool compact = ampB.getHeight() < 300; // 2-3 rigs stacked

            for (auto* k : { ampGainKnob[r].get(), ampBassKnob[r].get(), ampMidKnob[r].get(),
                             ampTrebleKnob[r].get(), ampPresKnob[r].get(), ampMasterKnob[r].get() })
                k->setVisible (active);
            loadButtons[r].setVisible (active);
            cabLcKnob[r]->setVisible (active);
            cabHcKnob[r]->setVisible (active);
            cabPhaseChips[r].setVisible (active);
            cabIrButtons[r].setVisible (active);
            if (! active)
                continue;

            // lane's amp
            if (r == 0)
            {
                ampLed.setBounds (ampB.getRight() - 18 - 18, ampB.getY() + (compact ? 10 : 19), 18, 18);
                ecoChip.setBounds (ampLed.getX() - 8 - 52, ampB.getY() + (compact ? 8 : 17), 52, 22);
            }

            KnobComponent* grid[6] = { ampGainKnob[r].get(), ampBassKnob[r].get(),
                                       ampMidKnob[r].get(), ampTrebleKnob[r].get(),
                                       ampPresKnob[r].get(), ampMasterKnob[r].get() };
            if (! compact)
            {
                // classic 3x2 grid (optional photo between header and knobs)
                const bool photo = ampImages[r].isValid();
                const int kw = 42, kh = kw + 26, gapX = 26, gapY = 4;
                const int gx = ampB.getX() + (ampB.getWidth() - (3 * kw + 2 * gapX)) / 2;
                const int gy = ampB.getY() + (photo ? 152 : 118);
                for (int i = 0; i < 6; ++i)
                    grid[i]->setBounds (gx + (i % 3) * (kw + gapX), gy + (i / 3) * (kh + gapY), kw, kh);

                {
                    auto lb = juce::Rectangle<int> (ampB.getX() + 18, ampB.getBottom() - 15 - 32,
                                                    ampB.getWidth() - 36, 32);
                    if (processor.hasModelLoaded (r))
                    {
                        ampVarButtons[r].setBounds (lb.removeFromRight (86));
                        lb.removeFromRight (6);
                    }
                    loadButtons[r].setBounds (lb);
                }
            }
            else
            {
                // single row of 6 smaller knobs
                const int kw = 32, kh = kw + 26, gapX = 6;
                const int gx = ampB.getX() + (ampB.getWidth() - (6 * kw + 5 * gapX)) / 2;
                const int gy = ampB.getY() + 46 + (ampB.getHeight() - 46 - 32 - kh) / 2;
                for (int i = 0; i < 6; ++i)
                    grid[i]->setBounds (gx + i * (kw + gapX), gy, kw, kh);

                {
                    auto lb = juce::Rectangle<int> (ampB.getX() + 14, ampB.getBottom() - 28,
                                                    ampB.getWidth() - 28, 22);
                    if (processor.hasModelLoaded (r))
                    {
                        ampVarButtons[r].setBounds (lb.removeFromRight (80));
                        lb.removeFromRight (5);
                    }
                    loadButtons[r].setBounds (lb);
                }
            }

            // lane's cab
            if (r == 0)
                cabLed.setBounds (cabB.getRight() - 10 - 18, cabB.getY() + 10, 18, 18);
            if (! compact)
            {
                cabPhaseChips[r].setBounds (cabB.getRight() - 12 - 26, cabB.getY() + 36, 26, 20);
                cabLcKnob[r]->setBounds (cabB.getX() + 25, cabB.getY() + 156, 40, 40 + 26);
                cabHcKnob[r]->setBounds (cabB.getX() + 85, cabB.getY() + 156, 40, 40 + 26);
                {
                    auto cb = juce::Rectangle<int> (cabB.getX() + 10, cabB.getBottom() - 12 - 24,
                                                    cabB.getWidth() - 20, 24);
                    if (processor.getIrPath (r).isNotEmpty())
                    {
                        cabVarButtons[r].setBounds (cb.removeFromRight (62));
                        cb.removeFromRight (6);
                    }
                    cabIrButtons[r].setBounds (cb);
                }
            }
            else
            {
                cabPhaseChips[r].setBounds (cabB.getRight() - 10 - 26, cabB.getY() + 32, 26, 20);
                const int kh2 = 36 + 26;
                const int ky = cabB.getY() + 34 + (cabB.getHeight() - 34 - 30 - kh2) / 2;
                cabLcKnob[r]->setBounds (cabB.getX() + 27, ky, 36, kh2);
                cabHcKnob[r]->setBounds (cabB.getX() + 87, ky, 36, kh2);
                {
                    auto cb = juce::Rectangle<int> (cabB.getX() + 10, cabB.getBottom() - 28,
                                                    cabB.getWidth() - 20, 22);
                    if (processor.getIrPath (r).isNotEmpty())
                    {
                        cabVarButtons[r].setBounds (cb.removeFromRight (58));
                        cb.removeFromRight (5);
                    }
                    cabIrButtons[r].setBounds (cb);
                }
            }
        }
    }

    // ---- EQ
    {
        eqLed.setBounds (eqB.getRight() - 13 - 18, eqB.getY() + 10, 18, 18);
        const int kw = 44, kh = kw + 26, gap = 13;
        const int gx = eqB.getCentreX() - (3 * kw + 2 * gap) / 2;
        const int ky = eqB.getY() + 130;
        eqLowKnob->setBounds (gx, ky, kw, kh);
        eqMidKnob->setBounds (gx + kw + gap, ky, kw, kh);
        eqHighKnob->setBounds (gx + 2 * (kw + gap), ky, kw, kh);
    }

    // ---- fx-mini pass: overrides the legacy positions for collapsed cards
    // (LED top-right, up to 2 main knobs centered, type box in the footer)
    for (auto* idc : allFxIds)
    {
        const juce::String id (idc);
        if (! chain.contains (id))
            continue;
        auto comps = componentsForFx (id);
        const bool mini = id != expandedFxId;
        if (! mini)
        {
            for (int i = 1; i < comps.size(); ++i)
                if (auto* k = dynamic_cast<KnobComponent*> (comps.getUnchecked (i)))
                    k->setCompactLayout (false);
            continue;
        }

        const auto box = boxForFx (id);
        if (box.isEmpty())
            continue;
        comps.getUnchecked (0)->setBounds (box.getRight() - 10 - 16, box.getY() + 8, 16, 16);

        juce::Array<KnobComponent*> knobs;
        for (int i = 1; i < comps.size() && knobs.size() < 2; ++i)
            if (auto* k = dynamic_cast<KnobComponent*> (comps.getUnchecked (i)))
                knobs.add (k);
        const int kw = 40, kh = kw + 16, gap = 10;
        const int rowW = knobs.size() * kw + juce::jmax (0, knobs.size() - 1) * gap;
        int kx = box.getCentreX() - rowW / 2;
        for (auto* k : knobs)
        {
            k->setCompactLayout (true);
            k->setBounds (kx, box.getY() + 54, kw, kh);
            kx += kw + gap;
        }

        if (auto* typeBtn = typeButtonForFx (id))
            typeBtn->setBounds (box.getX() + 8, box.getBottom() - 8 - 24,
                                box.getWidth() - 16, 24);
    }
}

void ChainView::drawPedalFrame (juce::Graphics& g, juce::Rectangle<int> b,
                                const juce::String& title, const juce::String& footer,
                                const juce::String& id)
{
    if (b.isEmpty()) // effect in the drawer (outside the current chain)
        return;

    const bool selected = id.isNotEmpty() && id == selectedFxId;
    auto bf = b.toFloat();
    g.setGradientFill ({ ui::cardTop, 0.0f, bf.getY(), ui::cardBottom, 0.0f, bf.getBottom(), false });
    g.fillRoundedRectangle (bf, 2.0f);
    g.setColour (selected ? ui::accent.withAlpha (0.85f) : ui::border());
    g.drawRoundedRectangle (bf.reduced (0.5f), 2.0f, selected ? 1.4f : 1.0f);

    // vNext fx-mini card: title + status + footer type box (knobs/LED are
    // children placed by resized(); effects with a variation selector show
    // their type button in the footer instead of the static caption)
    if (id.isNotEmpty() && b.getWidth() <= kMiniW + 6)
    {
        g.setFont (ui::uiFont (11.0f, true));
        g.setColour (ui::text);
        // width stops short of the remove "x" (right-50) so the title never
        // runs underneath it - the "x" and the LED own the top-right corner
        g.drawFittedText (title, b.getX() + 10, b.getY() + 9, b.getWidth() - 66, 14,
                          juce::Justification::centredLeft, 1);

        auto* onParam = processor.apvts.getRawParameterValue (onParamIdForFx (id));
        const bool on = onParam == nullptr || onParam->load() > 0.5f;
        g.setFont (ui::monoFont (7.5f, on));
        g.setColour (on ? ui::accent : ui::textFaint);
        g.drawText (on ? "ACTIVE" : "BYPASS", b.getX() + 10, b.getY() + 25,
                    b.getWidth() - 20, 10, juce::Justification::centredLeft);

        if (typeButtonForFx (id) == nullptr)
        {
            auto typeBox = juce::Rectangle<int> (b.getX() + 8, b.getBottom() - 8 - 24,
                                                 b.getWidth() - 16, 24).toFloat();
            g.setColour (ui::border());
            g.drawRoundedRectangle (typeBox.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (ui::textFaint);
            g.setFont (ui::monoFont (7.5f, true));
            g.drawFittedText (miniFooterFor (id), typeBox.toNearestInt().reduced (3, 0),
                              juce::Justification::centred, 1);
        }

        tipZones.push_back ({ { b.getX() + 8, b.getY() + 6, b.getWidth() - 36, 30 },
                              footer.trim().isNotEmpty()
                                  ? title + " - " + footer
                                  : title + " (click again to expand)" });
        return;
    }

    g.setFont (ui::uiFont (12.0f, true));
    g.setColour (ui::text);
    // -70 = 12 left margin + the top-right corner (12 + LED 18 + 6 + "x" 14 + 8
    // of breathing room); with -46 the title ran under the remove "x"
    g.drawText (title, b.getX() + 12, b.getY() + 12, b.getWidth() - 70, 15,
                juce::Justification::centredLeft);

    // clean UI: the footer note is no longer painted on the card - it becomes
    // a hover tooltip on the title (metadata on demand)
    if (footer.trim().isNotEmpty())
        tipZones.push_back ({ { b.getX() + 12, b.getY() + 8, b.getWidth() - 46, 22 },
                              footer });
}

juce::String ChainView::getTooltip()
{
    const auto p = getMouseXYRelative();
    for (const auto& [zone, tip] : tipZones)
        if (zone.contains (p))
            return tip;
    return {};
}

void ChainView::drawPhoto (juce::Graphics& g, const juce::Image& img, juce::Rectangle<int> spot)
{
    if (! img.isValid())
        return;

    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle (spot.toFloat(), 2.0f);
    g.reduceClipRegion (clip);
    const float scale = juce::jmax ((float) spot.getWidth() / img.getWidth(),
                                    (float) spot.getHeight() / img.getHeight());
    const float dw = img.getWidth() * scale, dh = img.getHeight() * scale;
    g.drawImage (img, juce::Rectangle<float> (spot.getX() + (spot.getWidth() - dw) / 2.0f,
                                              spot.getY() + (spot.getHeight() - dh) / 2.0f, dw, dh),
                 juce::RectanglePlacement::stretchToFit);
    g.restoreState();
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (spot.toFloat(), 2.0f, 1.0f);
}

void ChainView::paint (juce::Graphics& g)
{
    tipZones.clear();     // hover metadata zones are rebuilt every paint
    g.fillAll (ui::bg);   // theme background behind the chain
    // subtle striped background
    g.setColour (ui::dividerBase().withAlpha (0.018f));
    for (int gx = 0; gx < getWidth(); gx += 44)
        g.fillRect (gx, 0, 1, getHeight());

    // ---- directional connectors ("SIGNAL FLOW" title lives in the chain header)
    auto connector = [&g] (juce::Rectangle<int> a, juce::Rectangle<int> b)
    {
        const float y = (float) a.getCentreY();
        const float xa = (float) a.getRight() + 3.0f;
        const float xb = (float) b.getX() - 3.0f;
        g.setColour (ui::accent);
        g.fillEllipse (xa, y - 3.5f, 7.0f, 7.0f);
        g.setGradientFill ({ ui::accent.withAlpha (0.7f), xa, 0.0f,
                             ui::accent.withAlpha (0.15f), xb, 0.0f, false });
        g.fillRoundedRectangle (xa + 7.0f, y - 1.0f, xb - xa - 12.0f, 2.0f, 1.0f);
        g.setColour (ui::accent.withAlpha (0.4f));
        g.fillEllipse (xb - 5.0f, y - 2.5f, 5.0f, 5.0f);
    };
    // connectors follow the dynamic order (amp -> cabs is internal to the block)
    {
        const auto entries = orderedEntries();
        juce::Rectangle<int> prev (0, chainHeight / 2, 8, 1);   // virtual input node
        for (const auto& e : entries)
        {
            if (e.id == "amp")
            {
                const int count = processor.getRigCount();

                if (count == 1)
                {
                    connector (prev, ampLaneB[0]);
                    connector (ampLaneB[0], cabLaneB[0]);
                    prev = cabLaneB[0];
                }
                else
                {
                    // PARALLEL topology: split node -> one branch per lane
                    // (amp -> cab) -> sum bus (mix lives in the OUTPUT card)
                    auto hLine = [&g] (float x1, float x2, float y)
                    {
                        g.setGradientFill ({ ui::accent.withAlpha (0.7f), x1, 0.0f,
                                             ui::accent.withAlpha (0.25f), x2, 0.0f, false });
                        g.fillRoundedRectangle (x1, y - 1.0f, x2 - x1, 2.0f, 1.0f);
                    };
                    auto vBar = [&g] (float x, float y1, float y2)
                    {
                        g.setColour (ui::accent.withAlpha (0.55f));
                        g.fillRoundedRectangle (x - 1.5f, y1 - 1.5f, 3.0f, y2 - y1 + 3.0f, 1.5f);
                    };

                    // centred in the split/sum bus reserved by the layout
                    constexpr float busMid = (float) kRigBusW / 2.0f;
                    const float busInX = (float) ampLaneB[0].getX() - busMid;
                    const float busOutX = (float) cabLaneB[0].getRight() + busMid;
                    const float yPrev = (float) prev.getCentreY();
                    const float yOut = (float) chainHeight / 2.0f;
                    const float yTop = (float) ampLaneB[0].getCentreY();
                    const float yBot = (float) ampLaneB[count - 1].getCentreY();

                    // dry signal split
                    g.setColour (ui::accent);
                    g.fillEllipse ((float) prev.getRight() + 3.0f, yPrev - 3.5f, 7.0f, 7.0f);
                    hLine ((float) prev.getRight() + 10.0f, busInX, yPrev);
                    vBar (busInX, juce::jmin (yTop, yPrev), juce::jmax (yBot, yPrev));

                    for (int r = 0; r < count; ++r)
                    {
                        const float ry = (float) ampLaneB[r].getCentreY();
                        hLine (busInX, (float) ampLaneB[r].getX() - 3.0f, ry);
                        g.setColour (ui::accent.withAlpha (0.4f));
                        g.fillEllipse ((float) ampLaneB[r].getX() - 8.0f, ry - 2.5f, 5.0f, 5.0f);

                        connector (ampLaneB[r], cabLaneB[r]);

                        // lane output -> sum bus
                        g.setColour (ui::accent);
                        g.fillEllipse ((float) cabLaneB[r].getRight() + 3.0f, ry - 3.5f, 7.0f, 7.0f);
                        hLine ((float) cabLaneB[r].getRight() + 10.0f, busOutX, ry);
                    }

                    vBar (busOutX, juce::jmin (yTop, yOut), juce::jmax (yBot, yOut));
                    prev = juce::Rectangle<int> ((int) busOutX - 4, (int) yOut - 2, 4, 4);
                }
            }
            else
            {
                connector (prev, e.box);
                prev = e.box;
            }
        }
        // tail: the chain leaves toward the OUTPUT card on the right
        connector (prev, juce::Rectangle<int> (getWidth() - 10, chainHeight / 2, 8, 1));
    }

    // ---- pedals (those with variations have a selector in the footer instead of text)
    drawPedalFrame (g, gateB, "Noise Gate", juce::String (juce::CharPointer_UTF8 ("Hysteresis 6 dB \xc2\xb7 hold")), "gate");
    drawPedalFrame (g, odB, "Drive", " ", "od");
    drawPedalFrame (g, delayB, "Delay", " ", "delay");
    drawPedalFrame (g, revB, "Reverb", " ", "reverb");
    drawPedalFrame (g, compB, "Compressor", " ", "comp");
    drawPedalFrame (g, modB, "Modulation", " ", "mod");
    drawPedalFrame (g, pitchB, "Pitch", " ", "pitch");
    drawPedalFrame (g, wahB, "Wah", " ", "wah");
    drawPedalFrame (g, sgB, "Slow Gear",
                    juce::String (juce::CharPointer_UTF8 ("automatic swell")), "slowgear");
    drawPedalFrame (g, octB, "Octaver",
                    juce::String (juce::CharPointer_UTF8 ("analog sub-octave")), "octaver");
    drawPedalFrame (g, rmB, "Ring Mod",
                    juce::String (juce::CharPointer_UTF8 ("sine carrier")), "ringmod");
    drawPedalFrame (g, bcB, "Bitcrusher",
                    juce::String (juce::CharPointer_UTF8 ("lo-fi \xc2\xb7 bits + rate")), "bitcrush");
    drawPedalFrame (g, harmB, "Harmonizer", " ", "harm");
    drawPedalFrame (g, excB, "Exciter",
                    juce::String (juce::CharPointer_UTF8 ("harmonic brightness")), "exciter");
    drawPedalFrame (g, dsB, "De-esser",
                    juce::String (juce::CharPointer_UTF8 ("tames the harsh band")), "deesser");
    drawPedalFrame (g, tapeB, "Tape",
                    juce::String (juce::CharPointer_UTF8 ("saturation \xc2\xb7 bump \xc2\xb7 rolloff")), "tape");
    drawPedalFrame (g, cnsB, "Console",
                    juce::String (juce::CharPointer_UTF8 ("analog buss glue")), "console");

    // ---- looper (state + time drawn live; mini shows only knob + footer)
    if (! looperB.isEmpty())
    {
        drawPedalFrame (g, looperB, "Looper", {}, "looper");
    }
    if (! looperB.isEmpty() && expandedFxId == "looper")
    {
        const auto st = processor.getLooperState();
        juce::String status;
        juce::Colour c = ui::textFaint;
        switch (st)
        {
            case GuitarRigNAMProcessor::LooperState::empty:
                status = juce::String (juce::CharPointer_UTF8 ("empty \xc2\xb7 REC to record"));
                break;
            case GuitarRigNAMProcessor::LooperState::recording:
                status = "recording " + juce::String (processor.getLooperPosSeconds(), 1) + " s";
                c = ui::red;
                break;
            case GuitarRigNAMProcessor::LooperState::playing:
                status = "playing " + juce::String (processor.getLooperPosSeconds(), 1) + " / "
                         + juce::String (processor.getLooperSeconds(), 1) + " s";
                c = ui::accent;
                break;
            case GuitarRigNAMProcessor::LooperState::overdub:
                status = "overdub " + juce::String (processor.getLooperPosSeconds(), 1) + " / "
                         + juce::String (processor.getLooperSeconds(), 1) + " s";
                c = ui::glowOrange;
                break;
            case GuitarRigNAMProcessor::LooperState::stopped:
                status = juce::String (juce::CharPointer_UTF8 ("stopped \xc2\xb7 "))
                         + juce::String (processor.getLooperSeconds(), 1) + " s";
                break;
        }
        g.setFont (ui::monoFont (9.0f));
        g.setColour (c);
        g.drawText (status, looperB.getX() + 12, looperB.getY() + 34, looperB.getWidth() - 24, 12,
                    juce::Justification::centredLeft);

        // loop progress bar
        if (st != GuitarRigNAMProcessor::LooperState::empty)
        {
            auto bar = juce::Rectangle<float> ((float) looperB.getX() + 12.0f,
                                               (float) looperB.getY() + 52.0f,
                                               (float) looperB.getWidth() - 24.0f, 4.0f);
            g.setColour (ui::meterBg);
            g.fillRoundedRectangle (bar, 2.0f);
            const double total = st == GuitarRigNAMProcessor::LooperState::recording
                                     ? (double) GuitarRigNAMProcessor::looperMaxSeconds
                                     : processor.getLooperSeconds();
            const double frac = total > 0 ? processor.getLooperPosSeconds() / total : 0.0;
            g.setColour (c);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, frac)), 2.0f);
        }
    }

    // ---- external VST3 plugin slots (mini keeps the name in the footer box)
    for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
    {
        const auto& b = extB[s];
        if (b.isEmpty())
            continue;
        const auto extId = s == 0 ? juce::String ("ext") : "ext" + juce::String (s + 1);
        drawPedalFrame (g, b, "Plugin VST3 " + juce::String (s + 1), {}, extId);
        if (expandedFxId != extId)
            continue;

        const auto extName = processor.getExternalPluginName (s);
        g.setFont (ui::monoFont (8.0f));
        g.setColour (ui::accent);
        g.drawText (juce::CharPointer_UTF8 ("HOSTING \xc2\xb7 VST3"),
                    b.getX() + 12, b.getY() + 32, 140, 11,
                    juce::Justification::centredLeft);
        g.setFont (ui::uiFont (13.0f, true));
        g.setColour (extName.isNotEmpty() ? ui::textBright : ui::textMuted);
        g.drawFittedText (extName.isNotEmpty()
                              ? extName
                              : juce::String ("- empty slot -"),
                          b.getX() + 12, b.getY() + 48, b.getWidth() - 24, 34,
                          juce::Justification::topLeft, 2);
    }

    // ---- spectrum analyzer (live FFT 2048; expand the card to see it)
    if (! anB.isEmpty())
        drawPedalFrame (g, anB, "Analyzer",
                        juce::String (juce::CharPointer_UTF8 ("spectrum \xc2\xb7 40 Hz-16 kHz")),
                        "analyzer");
    if (! anB.isEmpty() && expandedFxId == "analyzer")
    {
        auto viz = juce::Rectangle<float> ((float) anB.getX() + 13.0f, (float) anB.getY() + 40.0f,
                                           (float) anB.getWidth() - 26.0f,
                                           (float) anB.getHeight() - 40.0f - 84.0f);
        g.setColour (ui::meterBg);
        g.fillRoundedRectangle (viz, 2.0f);

        if (processor.apvts.getRawParameterValue ("anOn")->load() > 0.5f)
        {
            // FFT of the most recent chunk + log bands with smooth decay
            constexpr int fftSize = 2048;
            static float sample[fftSize];
            processor.readAnalyzerBlock (sample, fftSize);
            for (int i = 0; i < fftSize; ++i)
                anFftBuf[(size_t) i] = sample[i]
                    * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (fftSize - 1)));
            std::fill (anFftBuf.begin() + fftSize, anFftBuf.end(), 0.0f);
            anFft.performFrequencyOnlyForwardTransform (anFftBuf.data());

            const double sr = 48000.0; // display; the log ratio is what matters
            for (int b = 0; b < anNumBands; ++b)
            {
                const double f0 = 40.0 * std::pow (400.0, (double) b / anNumBands);
                const double f1 = 40.0 * std::pow (400.0, (double) (b + 1) / anNumBands);
                const int k0 = juce::jlimit (1, fftSize / 2 - 1, (int) (f0 * fftSize / sr));
                const int k1 = juce::jlimit (k0 + 1, fftSize / 2, (int) (f1 * fftSize / sr) + 1);
                float mag = 0.0f;
                for (int k = k0; k < k1; ++k)
                    mag = juce::jmax (mag, anFftBuf[(size_t) k]);
                const float db = juce::Decibels::gainToDecibels (mag / (fftSize * 0.25f), -80.0f);
                const float norm = juce::jlimit (0.0f, 1.0f, (db + 70.0f) / 70.0f);
                anBands[b] = norm > anBands[b] ? norm : anBands[b] * 0.85f; // smooth decay
            }

            const float bw = (viz.getWidth() - 12.0f) / anNumBands;
            for (int b = 0; b < anNumBands; ++b)
            {
                const float hgt = juce::jmax (2.0f, anBands[b] * (viz.getHeight() - 12.0f));
                const float bx = viz.getX() + 6.0f + b * bw;
                juce::ColourGradient grad (ui::accent, 0.0f, viz.getBottom() - 6.0f - hgt,
                                           ui::accent.withAlpha (0.15f), 0.0f,
                                           viz.getBottom() - 6.0f, false);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (bx + 1.0f, viz.getBottom() - 6.0f - hgt,
                                        bw - 2.0f, hgt, 2.0f);
            }
        }
    }

    // ---- limiter (with a little gain reduction bar when expanded)
    if (! limB.isEmpty())
        drawPedalFrame (g, limB, "Limiter",
                        juce::String (juce::CharPointer_UTF8 ("brickwall \xc2\xb7 end of the chain")),
                        "limiter");
    if (! limB.isEmpty() && expandedFxId == "limiter")
    {
        const float gr = processor.getLimiterGrDb();
        auto bar = juce::Rectangle<float> ((float) limB.getX() + 13.0f, (float) limB.getY() + 40.0f,
                                           (float) limB.getWidth() - 26.0f, 6.0f);
        g.setColour (ui::meterBg);
        g.fillRoundedRectangle (bar, 3.0f);
        if (gr > 0.05f)
        {
            g.setColour (gr > 6.0f ? ui::red : ui::accent);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth()
                                                   * juce::jlimit (0.0f, 1.0f, gr / 12.0f)), 3.0f);
        }
        g.setFont (ui::monoFont (8.0f));
        g.setColour (ui::textFaint);
        g.drawText ("GR " + juce::String (gr, 1) + " dB",
                    limB.getX() + 13, limB.getY() + 50, limB.getWidth() - 26, 11,
                    juce::Justification::centredLeft);
    }

    // ---- pre-EQ (with live bars, like the post EQ)
    if (! preEqB.isEmpty())
        drawPedalFrame (g, preEqB, juce::String (juce::CharPointer_UTF8 ("Pre-EQ")),
                        juce::String (juce::CharPointer_UTF8 ("shapes the saturation \xc2\xb7 pre-amp")),
                        "preeq");
    if (! preEqB.isEmpty() && expandedFxId == "preeq")
    {
        auto viz = juce::Rectangle<float> ((float) preEqB.getX() + 13.0f, (float) preEqB.getY() + 38.0f,
                                           (float) preEqB.getWidth() - 26.0f, 62.0f);
        g.setColour (ui::meterBg);
        g.fillRoundedRectangle (viz, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (viz, 2.0f, 1.0f);

        const float lo = processor.apvts.getRawParameterValue ("preEqLow")->load();
        const float mi = processor.apvts.getRawParameterValue ("preEqMid")->load();
        const float hi = processor.apvts.getRawParameterValue ("preEqHigh")->load();
        const float gains[7] = { lo, lo, (lo + mi) / 2.0f, mi, (mi + hi) / 2.0f, hi, hi };
        const float bw = (viz.getWidth() - 2 * 11.0f - 6 * 5.0f) / 7.0f;
        for (int i = 0; i < 7; ++i)
        {
            const float hgt = juce::jlimit (0.12f, 0.95f, 0.5f + gains[i] / 30.0f)
                              * (viz.getHeight() - 18.0f);
            const float bx = viz.getX() + 11.0f + i * (bw + 5.0f);
            juce::ColourGradient grad (ui::glowOrange, 0.0f, viz.getBottom() - 9.0f - hgt,
                                       ui::glowOrange.withAlpha (0.15f), 0.0f, viz.getBottom() - 9.0f, false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bx, viz.getBottom() - 9.0f - hgt, bw, hgt, 2.0f);
        }
    }

    // ---- cabs (one card per rig lane)
    {
        const int count = processor.getRigCount();
        for (int s = 0; s < count; ++s)
        {
            const auto cabB = cabLaneB[s];
            const bool compact = cabB.getHeight() < 260;
            drawPedalFrame (g, cabB, count > 1 ? "Cab " + juce::String (s + 1)
                                               : juce::String ("Cab IR"), {});

            // IR's V1/V2 badge (when TONE3000 reports it via .meta)
            if (const auto irArch = archBadgeForIr (s); irArch.isNotEmpty())
            {
                auto badge = juce::Rectangle<float> ((float) cabB.getX() + 12.0f,
                                                     (float) cabB.getY() + (compact ? 32.0f : 38.0f),
                                                     26.0f, 15.0f);
                g.setColour (ui::accent.withAlpha (irArch == "A2" ? 0.9f : 0.45f));
                g.drawRoundedRectangle (badge, 4.0f, 1.0f);
                g.setFont (ui::monoFont (8.0f, true));
                g.drawText (irArch, badge, juce::Justification::centred);
            }

            // TONE3000 mark on store IRs (design req 5) - top-right, left of the LED
            if (t3kMark.isValid() && toneIdForCab (s) > 0)
            {
                const float mh = 13.0f;
                const float mw = mh * t3kMark.getWidth() / (float) t3kMark.getHeight();
                const float mx = (float) cabB.getRight() - (s == 0 ? 34.0f : 12.0f) - mw;
                g.drawImage (t3kMark, juce::Rectangle<float> (mx, (float) cabB.getY() + 11.0f, mw, mh),
                             juce::RectanglePlacement::centred);
            }

            // photo (only on the large card) or IR name
            const auto irName = processor.getIrName (s);
            if (! compact && cabImages[s].isValid())
            {
                drawPhoto (g, cabImages[s], { cabB.getX() + 12, cabB.getY() + 60,
                                              cabB.getWidth() - 24, 46 });
            }
            else
            {
                g.setFont (ui::monoFont (8.5f));
                g.setColour (juce::Colour (0xffb4bbc4));
                g.drawFittedText (irName.isNotEmpty()
                                      ? irName
                                      : juce::String ("- no IR -"),
                                  cabB.getX() + (compact ? 46 : 12), cabB.getY() + (compact ? 32 : 60),
                                  cabB.getWidth() - (compact ? 84 : 24), compact ? 22 : 34,
                                  juce::Justification::topLeft, compact ? 2 : 3);
            }
        }
    }

    // ---- EQ (with live bars reflecting LOW/MID/HIGH)
    if (! eqB.isEmpty())
        drawPedalFrame (g, eqB, "EQ",
                        juce::String (juce::CharPointer_UTF8 ("3 bands \xc2\xb7 post-cab")), "eq");
    if (! eqB.isEmpty() && expandedFxId == "eq")
    {
        auto viz = juce::Rectangle<float> ((float) eqB.getX() + 13.0f, (float) eqB.getY() + 38.0f,
                                           (float) eqB.getWidth() - 26.0f, 62.0f);
        g.setColour (ui::meterBg);
        g.fillRoundedRectangle (viz, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (viz, 2.0f, 1.0f);

        const float lo = processor.apvts.getRawParameterValue ("eqLow")->load();
        const float mi = processor.apvts.getRawParameterValue ("eqMid")->load();
        const float hi = processor.apvts.getRawParameterValue ("eqHigh")->load();
        const float gains[7] = { lo, lo, (lo + mi) / 2.0f, mi, (mi + hi) / 2.0f, hi, hi };
        const float bw = (viz.getWidth() - 2 * 11.0f - 6 * 5.0f) / 7.0f;
        for (int i = 0; i < 7; ++i)
        {
            const float h = juce::jlimit (0.12f, 0.95f, 0.5f + gains[i] / 30.0f)
                            * (viz.getHeight() - 18.0f);
            const float bx = viz.getX() + 11.0f + i * (bw + 5.0f);
            juce::ColourGradient grad (ui::accent, 0.0f, viz.getBottom() - 9.0f - h,
                                       ui::accent.withAlpha (0.15f), 0.0f, viz.getBottom() - 9.0f, false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bx, viz.getBottom() - 9.0f - h, bw, h, 2.0f);
        }
    }

    // ---- amps (one head per rig lane)
    for (int lane = 0; lane < processor.getRigCount(); ++lane)
    {
        const auto ampB = ampLaneB[lane];
        const bool compact = ampB.getHeight() < 300;
        auto bf = ampB.toFloat();
        g.setGradientFill ({ ui::ampTop, 0.0f, bf.getY(), ui::ampBottom, 0.0f, bf.getBottom(), false });
        g.fillRoundedRectangle (bf, 2.0f);
        g.setColour (ui::accent.withAlpha (0.28f));
        g.drawRoundedRectangle (bf.reduced (0.5f), 2.0f, 1.0f);

        // accent stripe at the top
        {
            g.saveState();
            juce::Path clip;
            clip.addRoundedRectangle (bf, 2.0f);
            g.reduceClipRegion (clip);
            juce::ColourGradient grad (ui::accent.withAlpha (0.0f), bf.getX(), 0.0f,
                                       ui::accent.withAlpha (0.0f), bf.getRight(), 0.0f, false);
            grad.addColour (0.5, ui::accent);
            g.setGradientFill (grad);
            g.fillRect (bf.getX(), bf.getY(), bf.getWidth(), 4.0f);
            g.restoreState();
        }

        const auto modelName = processor.getModelName (lane);

        g.setFont (ui::uiFont (12.0f, true));
        g.setColour (ui::textBright);
        g.drawText (processor.getRigCount() > 1 ? "AMP " + juce::String (lane + 1)
                                                : juce::String ("AMP HEAD"),
                    ampB.getX() + 18, ampB.getY() + (compact ? 10 : 19), 170, 14,
                    juce::Justification::centredLeft);

        // the top-right corner holds the arch badge and, for store captures, the
        // TONE3000 mark - reserve room so the name never runs under them.
        const bool hasT3k = t3kMark.isValid() && modelName.isNotEmpty()
                            && toneIdForLane (lane) > 0;
        const float t3kAspect = t3kMark.isValid()
            ? t3kMark.getWidth() / (float) t3kMark.getHeight() : 2.0f;

        if (compact)
        {
            const int extra = hasT3k ? (int) (16.0f * t3kAspect) + 10 : 0;
            // capture name on the line below the title (no subtitle/info)
            g.setFont (ui::uiFont (13.0f, true));
            g.setColour (modelName.isNotEmpty() ? ui::textBright : ui::textMuted);
            g.drawText (modelName.isNotEmpty()
                            ? modelName
                            : juce::String ("- no capture -"),
                        ampB.getX() + 18, ampB.getY() + 26, ampB.getWidth() - 36 - 34 - extra, 16,
                        juce::Justification::centredLeft);
        }
        else
        {
            // clean UI: no eyebrow line - the name sits right under the title;
            // tech info (kHz/mono/NAM) moved to the name's hover tooltip
            const int extra = hasT3k ? (int) (18.0f * t3kAspect) + 10 : 0;
            g.setFont (ui::uiFont (18.0f, true));
            g.setColour (modelName.isNotEmpty() ? ui::textBright : ui::textMuted);
            // reserve the right corner for the V1/V2 badge (+ TONE3000 mark)
            g.drawText (modelName.isNotEmpty() ? modelName
                                               : juce::String ("- no capture -"),
                        ampB.getX() + 18, ampB.getY() + 40, ampB.getWidth() - 36 - 40 - extra, 22,
                        juce::Justification::centredLeft);
        }

        // V1/V2 badge of the capture's architecture
        const auto archLabel = processor.getModelArchLabel (lane);
        if (archLabel.isNotEmpty() && modelName.isNotEmpty())
        {
            auto badge = compact
                             ? juce::Rectangle<float> ((float) ampB.getRight() - 18.0f - 28.0f,
                                                       (float) ampB.getY() + 26.0f, 28.0f, 16.0f)
                             : juce::Rectangle<float> ((float) ampB.getRight() - 18.0f - 30.0f,
                                                       (float) ampB.getY() + 40.0f, 30.0f, 18.0f);
            g.setColour (ui::accent.withAlpha (archLabel == "A2" ? 0.9f : 0.45f));
            g.drawRoundedRectangle (badge, 5.0f, 1.0f);
            g.setFont (ui::monoFont (9.0f, true));
            g.drawText (archLabel, badge, juce::Justification::centred);

            // TONE3000 mark: this capture came from the store (design req 5)
            if (t3kMark.isValid() && toneIdForLane (lane) > 0)
            {
                const float mh = badge.getHeight();
                const float mw = mh * t3kMark.getWidth() / (float) t3kMark.getHeight();
                g.drawImage (t3kMark,
                             juce::Rectangle<float> (badge.getX() - 6.0f - mw, badge.getY(), mw, mh),
                             juce::RectanglePlacement::centred);
            }
        }

        // tech line lives in the name's hover tooltip now (clean UI)
        {
            const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
            juce::String info;
            const double modelSr = processor.getModelExpectedSampleRate (lane);
            if (modelName.isNotEmpty())
            {
                info = (modelSr > 0 ? juce::String (modelSr / 1000.0, 1) + " kHz" + dot : juce::String())
                       + "mono" + dot + "NAM";
                if (processor.isResampling (lane))
                    info += dot + "resample";
                if (toneIdForLane (lane) > 0)
                    info += dot + "TONE3000";
            }
            else
            {
                info = "load a capture from the Tone 3000 Store";
            }
            tipZones.push_back ({ compact
                                      ? juce::Rectangle<int> (ampB.getX() + 18, ampB.getY() + 26,
                                                              ampB.getWidth() - 70, 16)
                                      : juce::Rectangle<int> (ampB.getX() + 18, ampB.getY() + 38,
                                                              ampB.getWidth() - 76, 24),
                                  info });
        }

        if (! compact && ampImages[lane].isValid())
            drawPhoto (g, ampImages[lane],
                       { ampB.getX() + 18, ampB.getY() + 70, ampB.getWidth() - 36, 74 });

        // glow bar (tube-style - orange, as in the design)
        {
            auto glow = compact
                            ? juce::Rectangle<float> (bf.getX() + 22.0f, bf.getBottom() - 36.0f,
                                                      bf.getWidth() - 44.0f, 5.0f)
                            : juce::Rectangle<float> (bf.getX() + 22.0f, bf.getBottom() - 66.0f,
                                                      bf.getWidth() - 44.0f, 7.0f);
            const float alpha = processor.hasModelLoaded (lane)
                                    && processor.apvts.getRawParameterValue ("ampOn")->load() > 0.5f
                                ? 0.85f : 0.15f;
            juce::ColourGradient grad (ui::glowOrange.withAlpha (0.0f), glow.getX(), 0.0f,
                                       ui::glowOrange.withAlpha (0.0f), glow.getRight(), 0.0f, false);
            grad.addColour (0.5, ui::glowOrange.withAlpha (alpha));
            g.setGradientFill (grad);
            g.fillRoundedRectangle (glow, 5.0f);
        }
    }

    // ---- dimmed disabled cards (the knobs get setAlpha separately)
    for (const auto& entry : orderedEntries())
    {
        if (entry.id == "amp" || entry.box.isEmpty())
            continue;
        if (auto* p = processor.apvts.getRawParameterValue (onParamIdForFx (entry.id));
            p != nullptr && p->load() <= 0.5f)
        {
            g.setColour (ui::bg.withAlpha (0.55f));
            g.fillRoundedRectangle (entry.box.toFloat(), 2.0f);
        }
    }

    // ---- remove "x" (returns the effect to the drawer; highlights on hover)
    for (const auto& entry : orderedEntries())
    {
        if (entry.id == "amp" || entry.box.isEmpty())
            continue;
        const auto hi = removeHotspot (entry.box);
        const bool hov = hi == hoverHotspot;
        const auto h = hi.toFloat();
        g.setColour (hov ? ui::red.withAlpha (0.95f) : ui::textFaint.withAlpha (0.55f));
        constexpr float in = 3.0f;   // 8 px cross - 6 px read as a stray speck
        g.drawLine (h.getX() + in, h.getY() + in, h.getRight() - in, h.getBottom() - in,
                    hov ? 1.8f : 1.4f);
        g.drawLine (h.getRight() - in, h.getY() + in, h.getX() + in, h.getBottom() - in,
                    hov ? 1.8f : 1.4f);
    }

    // ---- "+" on the connectors (insert an effect at that position; highlights on hover)
    for (const auto& [rect, idx] : insertSpots())
    {
        const bool hov = rect == hoverHotspot;
        auto rf = rect.toFloat().reduced (hov ? 0.0f : 2.0f);
        g.setColour (ui::cardTop);
        g.fillEllipse (rf);
        g.setColour (ui::accent.withAlpha (hov ? 0.95f : 0.4f));
        g.drawEllipse (rf, hov ? 1.6f : 1.2f);
        // drawn, not typed: drawText centres the text BOX, and the glyph's own
        // bearings then left the "+" visibly off-centre inside the ring
        const auto cc = rf.getCentre();
        const float arm = hov ? 5.0f : 4.5f;
        g.setColour (ui::accent.withAlpha (hov ? 1.0f : 0.85f));
        g.drawLine (cc.x - arm, cc.y, cc.x + arm, cc.y, hov ? 1.8f : 1.5f);
        g.drawLine (cc.x, cc.y - arm, cc.x, cc.y + arm, hov ? 1.8f : 1.5f);
    }

    // file drop target (capture/IR/vst3 dragged from Explorer)
    if (! dropHighlight.isEmpty())
    {
        g.setColour (ui::accent.withAlpha (0.9f));
        g.drawRoundedRectangle (dropHighlight.toFloat().reduced (1.5f), 2.0f, 2.5f);
        g.setColour (ui::accent.withAlpha (0.12f));
        g.fillRoundedRectangle (dropHighlight.toFloat(), 2.0f);
    }

    // ---- drag-and-drop feedback (ghost + insertion indicator)
    if (draggingId.isNotEmpty())
    {
        const auto source = boxForFx (draggingId);

        // dimmed source
        g.setColour (ui::bg.withAlpha (0.55f));
        g.fillRoundedRectangle (source.toFloat(), 2.0f);

        // insertion line
        const auto entries = orderedEntries();
        if (dropIndex >= 0)
        {
            const float ix = dropIndex < (int) entries.size()
                                 ? (float) entries[(size_t) dropIndex].box.getX() - 16.0f
                                 : (float) entries.back().box.getRight() + 16.0f;
            g.setColour (ui::accent);
            g.fillRoundedRectangle (ix - 2.0f, (float) source.getY() - 8.0f, 4.0f,
                                    (float) source.getHeight() + 16.0f, 2.0f);
        }

        // card ghost following the mouse
        auto ghost = source.toFloat().withX (dragMouseX - (float) dragGrabDx);
        g.setColour (ui::cardTop.withAlpha (0.85f));
        g.fillRoundedRectangle (ghost, 2.0f);
        g.setColour (ui::accent.withAlpha (0.8f));
        g.drawRoundedRectangle (ghost, 2.0f, 1.5f);
        g.setFont (ui::uiFont (13.0f, true));
        g.setColour (ui::textBright);
        g.drawText (fxDisplayName (draggingId), ghost.reduced (12.0f).removeFromTop (30.0f),
                    juce::Justification::centredLeft);
    }
}

//==============================================================================
// Chain minimap (vNext R1): proportional blocks per card - the amp block is
// larger and accent-tinted - plus a frame showing the viewport window.
// Clicking or dragging scrolls the chain.
class ChainMinimap : public juce::Component
{
public:
    ChainMinimap (ChainView& cv, juce::Viewport& vpIn) : chain (cv), vp (vpIn)
    {
        setRepaintsOnMouseActivity (true);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (ui::glass());
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (ui::border());
        g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);

        const float totalW = (float) juce::jmax (1, chain.getWidth());
        const auto inner = getLocalBounds().reduced (6, 7);
        auto toMap = [&] (int x) {
            return (float) inner.getX() + (float) x / totalW * (float) inner.getWidth();
        };

        for (const auto& [box, isAmp] : chain.minimapBlocks())
        {
            const float x1 = toMap (box.getX());
            const float x2 = toMap (box.getRight());
            g.setColour (isAmp ? ui::accent.withAlpha (0.45f)
                               : ui::text.withAlpha (0.18f));
            g.fillRoundedRectangle (x1, (float) inner.getY(),
                                    juce::jmax (2.0f, x2 - x1 - 2.0f),
                                    (float) inner.getHeight(), 2.0f);
        }

        // viewport window
        const float vx1 = toMap (vp.getViewPositionX());
        const float vx2 = toMap (vp.getViewPositionX() + vp.getWidth());
        g.setColour (ui::accent.withAlpha (0.9f));
        g.drawRoundedRectangle (vx1, (float) inner.getY() - 3.0f,
                                juce::jmax (8.0f, vx2 - vx1), (float) inner.getHeight() + 6.0f,
                                3.0f, 1.2f);
    }

    void mouseDown (const juce::MouseEvent& e) override { scrollTo (e.x); }
    void mouseDrag (const juce::MouseEvent& e) override { scrollTo (e.x); }

private:
    void scrollTo (int mx)
    {
        const auto inner = getLocalBounds().reduced (6, 7);
        if (inner.getWidth() <= 0)
            return;
        const float frac = juce::jlimit (0.0f, 1.0f,
                                         (float) (mx - inner.getX()) / (float) inner.getWidth());
        vp.setViewPosition (juce::jmax (0, (int) (frac * (float) chain.getWidth())
                                               - vp.getWidth() / 2),
                            vp.getViewPositionY());
        repaint();
    }

    ChainView& chain;
    juce::Viewport& vp;
};

//==============================================================================
RigContent::RigContent (GuitarRigNAMProcessor& p)
    : processor (p)
{
    // restore the persisted theme (dark default) — press 'L' to toggle
    if (processor.apvts.state.getProperty ("uiTheme", "dark").toString() == "light")
    {
        ui::applyTheme (true);
        lookAndFeel.applyColours();
    }
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (cpuMeter);

    // vNext: dedicated Audio & MIDI screen (staged device changes); in the
    // plugin the device is host-managed but meters/health still show
    audioButton.onClick = [this]
    {
        if (audioOverlay == nullptr)
        {
            auto* holder = juce::StandalonePluginHolder::getInstance();
            audioOverlay = std::make_unique<AudioOverlay> (
                processor, holder != nullptr ? &holder->deviceManager : nullptr);
            addAndMakeVisible (*audioOverlay);
            audioOverlay->setBounds (getLocalBounds());
        }
        audioOverlay->open();
    };
    addAndMakeVisible (audioButton);

    storeButton.getProperties().set ("accent", true);
    storeButton.onClick = [this] { storeOverlay->open(); };
    addAndMakeVisible (storeButton);

    // clean UI: preset nav + SAVE are ghost (no boxes) - only the store button
    // keeps the accent, so a single primary action reads per screen
    prevButton.getProperties().set ("ghost", true);
    nextButton.getProperties().set ("ghost", true);
    saveButton.getProperties().set ("ghost", true);
    prevButton.onClick = [this] { processor.loadAdjacentPreset (-1); };
    nextButton.onClick = [this] { processor.loadAdjacentPreset (1); };
    saveButton.onClick = [this] { saveCurrentPreset(); };
    presetPill.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (presetPill);

    // inline preset name editor (appears over the pill)
    presetNameEditor.setFont (ui::uiFont (13.0f, true));
    presetNameEditor.setJustification (juce::Justification::centred);
    presetNameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff14181d));
    presetNameEditor.setColour (juce::TextEditor::outlineColourId, ui::accent);
    presetNameEditor.setColour (juce::TextEditor::focusedOutlineColourId, ui::accent);
    presetNameEditor.setColour (juce::TextEditor::textColourId, ui::text);
    presetNameEditor.onReturnKey = [this]
    {
        const auto name = juce::File::createLegalFileName (presetNameEditor.getText().trim());
        presetNameEditor.setVisible (false);
        if (name.isNotEmpty())
        {
            processor.savePreset (processor.getPresetsDirectory().getChildFile (name + ".xml"));
            saveFlashTicks = 27;
        }
        grabKeyboardFocus();
    };
    presetNameEditor.onEscapeKey = [this]
    {
        presetNameEditor.setVisible (false);
        grabKeyboardFocus();
    };
    presetNameEditor.onFocusLost = [this] { presetNameEditor.setVisible (false); };
    addChildComponent (presetNameEditor);

    // tooltips + shortcuts
    setWantsKeyboardFocus (true);
    for (auto* b : std::initializer_list<juce::Button*> { &prevButton, &nextButton, &saveButton,
                                                          &presetPill, &audioButton, &storeButton,
                                                          &tunerToggle })
        b->setMouseClickGrabsKeyboardFocus (false);

    prevButton.setTooltip (juce::String (juce::CharPointer_UTF8 ("Previous preset (\xe2\x86\x90)")));
    nextButton.setTooltip (juce::String (juce::CharPointer_UTF8 ("Next preset (\xe2\x86\x92)")));
    saveButton.setTooltip ("Saves the current preset (no name: asks for one)");
    presetPill.setTooltip ("Choose preset / Save as new");
    storeButton.setTooltip ("Search and download tones from TONE3000");
    audioButton.setTooltip ("Driver, device, sample rate and buffer (ASIO/WASAPI)");
    tunerToggle.setTooltip ("Enable/disable the tuner (T)");

    // dev: GUITARRIG_TUNER=off starts with the tuner disabled (UI test)
    if (juce::SystemStats::getEnvironmentVariable ("GUITARRIG_TUNER", "") == "off")
        processor.apvts.state.setProperty ("tunerOn", false, nullptr);

    tunerToggle.getProperties().set ("chip", true);
    tunerToggle.getProperties().set ("chipActive", isTunerOn());
    tunerToggle.onClick = [this]
    {
        const bool newState = ! isTunerOn();
        processor.apvts.state.setProperty ("tunerOn", newState, nullptr);
        tunerToggle.getProperties().set ("chipActive", newState);
        tunerToggle.repaint();
        if (! newState)
        {
            tunerFreq = -1.0;
            tunerNote.clear();
            tunerStringIndex = -1;
        }
    };
    addAndMakeVisible (tunerToggle);

    // tuner MUTE: silences the output while the tuner is on
    muteChip.getProperties().set ("chip", true);
    muteChip.getProperties().set ("chipActive", false);
    muteChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Silences the output while the tuner is on (tune in silence)")));
    muteChip.setMouseClickGrabsKeyboardFocus (false);
    muteChip.onClick = [this]
    {
        tunerMuteWanted = ! tunerMuteWanted;
        muteChip.getProperties().set ("chipActive", tunerMuteWanted);
        muteChip.repaint();
    };
    addAndMakeVisible (muteChip);

    // quick RECORDER: output WAV in Documents\PedalForge NAM\Recordings
    recChip.getProperties().set ("ghost", true);
    recChip.getProperties().set ("ghostHot", true);   // active = red (recording)
    recChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Records WAVs in Documents\\PedalForge NAM\\Recordings: the mix plus "
        "separate guitar and drum stems")));
    recChip.setMouseClickGrabsKeyboardFocus (false);
    recChip.onClick = [this]
    {
        if (processor.isRecording())
        {
            processor.stopRecording();
            recSavedTicks = 45;
        }
        else
        {
            if (processor.startRecording() != juce::File())
                recStartMs = juce::Time::currentTimeMillis();
        }
        recChip.getProperties().set ("chipActive", processor.isRecording());
        recChip.repaint();
    };
    addAndMakeVisible (recChip);

    // A/B: compares two complete settings
    abButton.getProperties().set ("ghost", true);
    abButton.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "A/B: toggles between two complete rig settings (the current one is saved in the active slot)")));
    abButton.setMouseClickGrabsKeyboardFocus (false);
    abButton.onClick = [this]
    {
        processor.toggleAB();
        abButton.setButtonText (processor.getABIndex() == 0 ? "A" : "B");
    };
    addAndMakeVisible (abButton);

    // STAGE: performance mode - only the essentials, huge (F key)
    perfChip.getProperties().set ("chip", true);
    perfChip.getProperties().set ("chipActive", false);
    perfChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Stage mode (F): hides the chain and shows preset, tuner and "
        "meters at large size. Esc returns.")));
    perfChip.setMouseClickGrabsKeyboardFocus (false);
    perfChip.onClick = [this] { setPerfMode (! perfMode); };
    addAndMakeVisible (perfChip);

    // AUTO-ECO: switches to the light capture by itself when CPU goes over 90%
    autoEcoChip.getProperties().set ("chip", true);
    autoEcoChip.setClickingTogglesState (true);
    autoEcoChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "With CPU above 90%%, automatically switches to the light "
        "capture version (when available)")));
    autoEcoChip.setMouseClickGrabsKeyboardFocus (false);
    autoEcoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "autoEco", autoEcoChip);
    addAndMakeVisible (autoEcoChip);

    chainView = std::make_unique<ChainView> (processor,
                                             [this] (int lane) { chooseModelSource (lane); },
                                             [this] (int slot) { chooseIrSource (slot); },
                                             [this] (int slot) { chooseExtPluginFile (slot); },
                                             [this] (int slot) { openExtPluginWindow (slot); });

    // close the hosted plugin's panel before any change/disposal
    processor.onExternalPluginWillChange =
        [safe = juce::Component::SafePointer<RigContent> (this)] (int slot)
        {
            if (safe != nullptr)
                safe->closeExtPluginWindow (slot);
        };

    chainViewport.setViewedComponent (chainView.get(), false);
    chainViewport.setScrollBarsShown (false, false);   // the minimap navigates
    addAndMakeVisible (chainViewport);

    // chain header "+ EFFECT" (opens the drawer at the canonical position)
    addFxHeaderButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xef\xbc\x8b EFFECT")));
    addFxHeaderButton.getProperties().set ("chip", true);
    addFxHeaderButton.setTooltip ("Add an effect to the chain");
    addFxHeaderButton.setMouseClickGrabsKeyboardFocus (false);
    addFxHeaderButton.onClick = [this]
    {
        if (fxDrawer != nullptr)
            fxDrawer->open (-1);
    };
    addAndMakeVisible (addFxHeaderButton);

    // footer minimap (proportional blocks + viewport window; click/drag scrolls)
    chainMinimap = std::make_unique<ChainMinimap> (*chainView, chainViewport);
    addAndMakeVisible (*chainMinimap);

    // ---- vNext R1: fixed side cards (INPUT left, OUTPUT/MIXER right) --------
    auto formatDb = [] (float v) { return juce::String (v, 1) + " dB"; };
    auto formatPct = [] (float v) { return juce::String ((int) v) + "%"; };
    auto formatTen = [] (float v) { return juce::String (v, 1); };

    inGainKnob = std::make_unique<KnobComponent> (processor.apvts, "inputGain", "GAIN", formatDb);
    inGainKnob->setKnobTooltip ("Input gain (before everything)");
    addAndMakeVisible (*inGainKnob);

    outLevelKnob = std::make_unique<KnobComponent> (processor.apvts, "outputGain", "LEVEL",
                                                    formatDb);
    outLevelKnob->setKnobTooltip ("Final output volume");
    addAndMakeVisible (*outLevelKnob);

    airKnob = std::make_unique<KnobComponent> (processor.apvts, "cabAir", "AIR", formatTen);
    airKnob->setKnobTooltip ("Air/brightness after the rig mix (8 kHz shelf)");
    addAndMakeVisible (*airKnob);

    for (int r = 0; r < GuitarRigNAMProcessor::maxRigs; ++r)
    {
        const auto n = juce::String (r + 1);
        rigLevelKnob[r] = std::make_unique<KnobComponent> (
            processor.apvts, "cab" + n + "Blend", "RIG " + n, formatPct);
        rigLevelKnob[r]->setKnobTooltip ("How much of rig " + n + " enters the output sum");
        addChildComponent (*rigLevelKnob[r]);

        rigSegButtons[r].setButtonText (n);
        rigSegButtons[r].getProperties().set ("chip", true);
        rigSegButtons[r].setTooltip (juce::String (juce::CharPointer_UTF8 (
            "Number of parallel AMP+IR rigs")));
        rigSegButtons[r].setMouseClickGrabsKeyboardFocus (false);
        rigSegButtons[r].onClick = [this, r] { setRigCountParam (r + 1); };
        addAndMakeVisible (rigSegButtons[r]);
    }

    // "rig" was misleading: what this adds in parallel is an AMP+CAB pair, not
    // a whole second chain
    addRigButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xef\xbc\x8b ADD AMP+CAB")));
    addRigButton.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Add a parallel AMP+CAB pair (up to 3)")));
    addRigButton.setMouseClickGrabsKeyboardFocus (false);
    addRigButton.onClick = [this] { setRigCountParam (processor.getRigCount() + 1); };
    addAndMakeVisible (addRigButton);

    // ---- vNext R2: chain-order undo/redo --------------------------------
    chainUndoButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\xb6")));
    chainRedoButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\xb7")));
    chainUndoButton.setTooltip ("Undo the last chain change (Ctrl+Z)");
    chainRedoButton.setTooltip ("Redo the undone chain change (Ctrl+Y)");
    for (auto* b : { &chainUndoButton, &chainRedoButton })
    {
        b->getProperties().set ("ghost", true);
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    chainUndoButton.onClick = [this] { undoChainOrder(); };
    chainRedoButton.onClick = [this] { redoChainOrder(); };
    chainOrderSeen = processor.getChainOrder();

    // vNext: the "+" spots open the searchable drawer instead of a popup menu
    fxDrawer = std::make_unique<FxDrawer> (processor);
    fxDrawer->onInsert = [this] (const juce::String& id, int idx)
    {
        chainView->insertFxAt (id, idx);
        fxDrawer->close();
    };
    addChildComponent (*fxDrawer);
    chainView->onOpenFxBrowser = [this] (int idx)
    {
        fxDrawer->open (idx);
    };

    storeOverlay = std::make_unique<StoreOverlay> (processor);
    addChildComponent (*storeOverlay);

    // amp-card "variations": open the Tone Store details for this tone, aimed at
    // the lane so the picked capture replaces the one playing there.
    chainView->onShowVariations =
        [safe = juce::Component::SafePointer<RigContent> (this)]
        (int lane, int toneId, juce::Component* anchor)
        {
            if (safe != nullptr)
                safe->storeOverlay->showVariationPicker (lane, toneId, anchor);
        };

    // Drums module: overlay + top bar button + drum VST window
    drumOverlay = std::make_unique<DrumOverlay> (processor);
    addChildComponent (*drumOverlay);
    drumOverlay->onChooseVst = [this] { chooseDrumVstFile(); };
    drumOverlay->onOpenVstPanel = [this] { openDrumVstWindow(); };
    drumOverlay->onOpenSongMap = [this]
    {
        if (songOverlay != nullptr)
            songOverlay->open();
    };
    drumButton.onClick = [this] { drumOverlay->open(); };
    drumButton.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Electronic drums: grooves by genre, score and grid")));
    drumButton.setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (drumButton);

    // vNext: SONG / SCENES screen (per-section rig snapshots + setlist)
    songOverlay = std::make_unique<SongOverlay> (processor);
    addChildComponent (*songOverlay);
    songOverlay->onSaveSong = [this] { saveCurrentPreset(); };
    songOverlay->onAddSong = [this]
    {
        // new song = new preset seeded from the current state, unique name
        auto dir = processor.getPresetsDirectory();
        juce::File f;
        for (int n = 1; n < 100; ++n)
        {
            f = dir.getChildFile ("New Song" + (n == 1 ? juce::String()
                                                       : " " + juce::String (n)) + ".xml");
            if (! f.existsAsFile())
                break;
        }
        processor.savePreset (f);
    };
    songButton.onClick = [this] { songOverlay->open(); };
    songButton.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Song/Scenes: one rig snapshot per drum section, auto-switch on play")));
    songButton.setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (songButton);

    // drum ribbon at the top (follow along without opening the module)
    drumRibbon = std::make_unique<DrumRibbon> (processor.drumEngine);
    drumRibbon->onOpen = [this] { drumOverlay->open(); };
    // collapsed state persists in the session state; the chain area and the
    // morph source rect follow the ribbon height
    drumRibbon->onToggleMin = [this] (bool m)
    {
        processor.apvts.state.setProperty ("drumRibMin", m, nullptr);
        if (drumOverlay != nullptr)
            drumOverlay->ribbonSourceH = m ? 26 : 54;
        resized();
    };
    if ((bool) processor.apvts.state.getProperty ("drumRibMin", false))
    {
        drumRibbon->setMinimal (true);
        drumOverlay->ribbonSourceH = 26;
    }
    drumRibbon->setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Follow along with the drums - click to open the module")));
    addAndMakeVisible (*drumRibbon);

    processor.onDrumPluginWillChange =
        [safe = juce::Component::SafePointer<RigContent> (this)]
        {
            if (safe != nullptr)
            {
                safe->closeDrumVstWindow();
                if (safe->drumOverlay != nullptr)
                    safe->drumOverlay->refreshSourceRow();
            }
        };

    // Dev: GUITARRIG_EXT_PLUGIN=<.vst3 path> loads into the slot at startup.
    {
        const auto extFlag = juce::SystemStats::getEnvironmentVariable ("GUITARRIG_EXT_PLUGIN", "");
        if (extFlag.isNotEmpty())
            juce::MessageManager::callAsync (
                [safe = juce::Component::SafePointer<RigContent> (this), extFlag]
                {
                    if (safe != nullptr)
                        safe->processor.loadExternalPluginAsync (0, juce::File (extFlag));
                });
    }

    // Dev flags: GUITARRIG_OPEN_DRUMS=1 opens the Drums module at startup;
    // =play also starts playback (transport test without synthetic clicks).
    {
        const auto flag = juce::SystemStats::getEnvironmentVariable ("GUITARRIG_OPEN_DRUMS", "");
        if (flag.isNotEmpty())
            juce::MessageManager::callAsync (
                [safe = juce::Component::SafePointer<RigContent> (this), flag]
                {
                    if (safe == nullptr)
                        return;
                    if (flag == "rig1" || flag == "rig2" || flag == "rig3")
                        if (auto* param = safe->processor.apvts.getParameter ("cabCount"))
                        {
                            const float rigs = (float) flag.getTrailingIntValue();
                            param->setValueNotifyingHost (
                                param->getNormalisableRange().convertTo0to1 (rigs));
                        }
                    safe->drumOverlay->open();
                    if (flag == "play")
                        safe->processor.drumEngine.playing.store (true);
                    if (flag == "meter")   // test: 4/4, 3/4, 6/8, 7/8
                    {
                        auto& e = safe->processor.drumEngine;
                        e.setMeter (0, 4, 4); e.setMeter (1, 3, 4);
                        e.setMeter (2, 6, 8); e.setMeter (3, 7, 8);
                    }
                    if (flag == "grid")
                        safe->drumOverlay->devOpenGrid();
                    if (flag == "gen" || flag == "genfill")  // generator test (3rd = 7/8)
                    {
                        safe->processor.drumEngine.setMeter (2, 7, 8);
                        safe->drumOverlay->devOpenGenerator();
                        if (flag == "genfill")
                            safe->drumOverlay->devGenerateAll();
                    }
                });
    }

    // Dev flag: GUITARRIG_OPEN_STORE=explore|library|plugins|browse opens the
    // store ("browse" goes straight into the embedded TONE3000 picker).
    {
        const auto flag = juce::SystemStats::getEnvironmentVariable ("GUITARRIG_OPEN_STORE", "");
        if (flag == "library" || flag == "explore" || flag == "plugins" || flag == "browse")
            juce::MessageManager::callAsync (
                [safe = juce::Component::SafePointer<RigContent> (this), flag]
                {
                    if (safe == nullptr)
                        return;
                    if (flag == "library")      safe->storeOverlay->openOnLibrary();
                    else if (flag == "plugins") safe->storeOverlay->openOnPlugins();
                    else if (flag == "browse")  safe->storeOverlay->openOnBrowser();
                    else                        safe->storeOverlay->open();
                });
    }

    // Dev flags for the remaining screens, so a UI sweep can open each one
    // deterministically instead of hunting for buttons by coordinate.
    // GUITARRIG_OPEN_SONG=1   -> Song / Scenes
    // GUITARRIG_OPEN_AUDIO=1  -> Audio & MIDI
    // GUITARRIG_STAGE=1       -> Stage (performance) mode
    if (juce::SystemStats::getEnvironmentVariable ("GUITARRIG_OPEN_SONG", "") == "1")
        juce::MessageManager::callAsync (
            [safe = juce::Component::SafePointer<RigContent> (this)]
            {
                if (safe != nullptr && safe->songOverlay != nullptr)
                    safe->songOverlay->open();
            });

    if (juce::SystemStats::getEnvironmentVariable ("GUITARRIG_OPEN_AUDIO", "") == "1")
        juce::MessageManager::callAsync (
            [safe = juce::Component::SafePointer<RigContent> (this)]
            {
                if (safe != nullptr)
                    safe->audioButton.triggerClick();   // builds the overlay lazily
            });

    if (juce::SystemStats::getEnvironmentVariable ("GUITARRIG_STAGE", "") == "1")
        juce::MessageManager::callAsync (
            [safe = juce::Component::SafePointer<RigContent> (this)]
            {
                if (safe != nullptr)
                    safe->setPerfMode (true);
            });

    setSize (designWidth, designHeight);
    startTimerHz (30);
}

RigContent::~RigContent()
{
    processor.onExternalPluginWillChange = nullptr;
    processor.onDrumPluginWillChange = nullptr;
    closeAllExtPluginWindows();
    closeDrumVstWindow();
    setLookAndFeel (nullptr);
}

void RigContent::setRigCountParam (int count)
{
    if (auto* param = processor.apvts.getParameter ("cabCount"))
    {
        const int c = juce::jlimit (1, (int) GuitarRigNAMProcessor::maxRigs, count);
        param->setValueNotifyingHost (param->getNormalisableRange().convertTo0to1 ((float) c));
    }
}

//==============================================================================
// vNext R2: chain-order history. Every change lands in the undo stack (the
// 30 Hz timer watches setChainOrder from any source: drag, drawer, "x", preset).
void RigContent::undoChainOrder()
{
    if (chainUndoStack.empty())
        return;
    chainRedoStack.push_back (processor.getChainOrder());
    const auto prev = chainUndoStack.back();
    chainUndoStack.pop_back();
    chainOrderSeen = prev;          // the watcher must not re-push this change
    processor.setChainOrder (prev);
    if (chainMinimap != nullptr)
        chainMinimap->repaint();
}

void RigContent::redoChainOrder()
{
    if (chainRedoStack.empty())
        return;
    chainUndoStack.push_back (processor.getChainOrder());
    const auto next = chainRedoStack.back();
    chainRedoStack.pop_back();
    chainOrderSeen = next;
    processor.setChainOrder (next);
    if (chainMinimap != nullptr)
        chainMinimap->repaint();
}

//==============================================================================
// vNext S1: next-scene helpers (stage view)
int RigContent::nextDrumSection() const
{
    const int uiBar = processor.drumEngine.uiBar.load();
    const int sec = uiBar >= 0 ? uiBar / drum::barsPerSection : 0;
    const int next = sec + 1;
    return next < processor.drumEngine.numSections.load() ? next : -1;
}

void RigContent::applyNextScene()
{
    const int next = nextDrumSection();
    if (next >= 0 && processor.hasScene (next))
        processor.applySceneForSection (next);
}

void RigContent::resized()
{
    const int W = getWidth();

    storeOverlay->setBounds (getLocalBounds());
    drumOverlay->setBounds (getLocalBounds());
    if (songOverlay != nullptr)
        songOverlay->setBounds (getLocalBounds());

    // ---- top bar (60 px) - clean UI: ghost cluster left, meters, actions right
    storeButton.setBounds (W - 18 - 134, 13, 134, 34);
    audioButton.setBounds (storeButton.getX() - 8 - 82, 13, 82, 34);
    drumButton.setBounds (audioButton.getX() - 8 - 78, 13, 78, 34);
    songButton.setBounds (drumButton.getX() - 8 - 66, 13, 66, 34);
    const int metersRight = songButton.getX() - 16;
    const int meterW = 58, cpuW = 48;
    cpuMeter.setBounds (metersRight - cpuW, 34, cpuW, 7);
    inMeter.setBounds (metersRight - cpuW - 14 - meterW, 17, meterW, 7);
    outMeter.setBounds (metersRight - cpuW - 14 - meterW, 32, meterW, 7);

    {
        const int pillW = 145, navW = 26, saveW = 52, gap = 4;
        const int groupW = navW + gap + pillW + gap + navW + gap + saveW
                           + gap + 32 + 4 + 62; // + A/B + REC
        // shifted left so it clears the meters (reserve ~40px for IN/OUT labels)
        int x = juce::jmin ((W - groupW) / 2, inMeter.getX() - 40 - groupW);
        x = juce::jmax (x, 186);
        prevButton.setBounds (x, 13, navW, 34);
        x += navW + gap;
        presetPill.setBounds (x, 13, pillW, 34);
        x += pillW + gap;
        nextButton.setBounds (x, 13, navW, 34);
        x += navW + gap;
        saveButton.setBounds (x, 13, saveW, 34);
        x += saveW + gap;
        abButton.setBounds (x, 16, 32, 28);
        x += 32 + 4;
        recChip.setBounds (x, 16, 62, 28);
    }

    // ---- drum ribbon (top, collapsible) + 3-column workspace + bottom bar
    const int ribH = (drumRibbon != nullptr && drumRibbon->isMinimal()) ? 26 : 54;
    if (drumRibbon != nullptr)
        drumRibbon->setBounds (18, 62, W - 36, ribH);
    const int chainTop = 62 + ribH + 4;
    const int wsBottom = getHeight() - 60 - 8;   // above the bottom bar

    // rig-work grid (mockup): INPUT 132 | chain 1fr | OUTPUT 176, gap 10
    inputCardB = { 18, chainTop, 132, wsBottom - chainTop };
    outputCardB = { W - 18 - 176, chainTop, 176, wsBottom - chainTop };
    chainWrapB = { inputCardB.getRight() + 10, chainTop,
                   outputCardB.getX() - 10 - (inputCardB.getRight() + 10),
                   wsBottom - chainTop };

    // INPUT card children
    if (inGainKnob != nullptr)
        inGainKnob->setBounds (inputCardB.getCentreX() - 25, inputCardB.getY() + 128, 50, 76);
    inputMeterB = { inputCardB.getCentreX() - 5, inputCardB.getY() + 218,
                    10, inputCardB.getBottom() - 54 - (inputCardB.getY() + 218) };

    // OUTPUT / MIXER card children
    {
        const int cx = outputCardB.getCentreX();
        const int segW = 3 * 28 + 2 * 4;
        for (int r = 0; r < GuitarRigNAMProcessor::maxRigs; ++r)
            rigSegButtons[r].setBounds (cx - segW / 2 + r * 32, outputCardB.getY() + 48, 28, 22);

        const int count = processor.getRigCount();
        const int kw = 40, kh = kw + 26;
        const int rowW = count * kw + (count - 1) * 8;
        for (int r = 0; r < GuitarRigNAMProcessor::maxRigs; ++r)
        {
            if (rigLevelKnob[r] == nullptr)
                continue;
            rigLevelKnob[r]->setVisible (! perfMode && r < count);
            if (r < count)
                rigLevelKnob[r]->setBounds (cx - rowW / 2 + r * (kw + 8),
                                            outputCardB.getY() + 82, kw, kh);
        }
        if (airKnob != nullptr)
            airKnob->setBounds (cx - 46, outputCardB.getY() + 158, 42, 42 + 26);
        if (outLevelKnob != nullptr)
            outLevelKnob->setBounds (cx + 4, outputCardB.getY() + 158, 42, 42 + 26);

        outputMeterB = { cx - 5, outputCardB.getY() + 240,
                         10, outputCardB.getBottom() - 88 - (outputCardB.getY() + 240) };
        addRigButton.setBounds (outputCardB.getX() + 12, outputCardB.getBottom() - 38,
                                outputCardB.getWidth() - 24, 26);
    }

    // chain container: 38 px header, viewport, 32 px minimap footer
    addFxHeaderButton.setBounds (chainWrapB.getRight() - 8 - 86, chainWrapB.getY() + 5, 86, 28);
    const int vpY = chainWrapB.getY() + 39;
    const int vpH = chainWrapB.getBottom() - 48 - vpY;
    chainViewport.setBounds (chainWrapB.getX() + 1, vpY, chainWrapB.getWidth() - 2, vpH);
    if (chainView != nullptr)
        chainView->setChainHeight (vpH);
    if (chainMinimap != nullptr)
        chainMinimap->setBounds (chainWrapB.getX() + 10, chainWrapB.getBottom() - 42,
                                 chainWrapB.getWidth() - 20, 32);

    if (fxDrawer != nullptr)
        fxDrawer->setBounds (W - 352, chainTop, 352, getHeight() - chainTop - 60);

    // ---- bottom bar (mockup .rig-bottom): undo/redo, pills, tuneline, tags
    const int by = getHeight() - 60 + 16;
    chainUndoButton.setBounds (18, by, 30, 28);
    chainRedoButton.setBounds (50, by, 30, 28);
    tunerToggle.setBounds (92, by, 72, 28);
    muteChip.setBounds (168, by, 48, 28);
    autoEcoChip.setBounds (220, by, 78, 28);
    perfChip.setBounds (302, by, 58, 28);
}

void RigContent::setPerfMode (bool shouldBeOn)
{
    perfMode = shouldBeOn;
    chainViewport.setVisible (! perfMode);
    // hide the whole rig workspace (side cards, chain header, minimap, ribbon)
    const bool ws = ! perfMode;
    if (chainMinimap != nullptr) chainMinimap->setVisible (ws);
    addFxHeaderButton.setVisible (ws);
    if (inGainKnob != nullptr)  inGainKnob->setVisible (ws);
    if (outLevelKnob != nullptr) outLevelKnob->setVisible (ws);
    if (airKnob != nullptr)      airKnob->setVisible (ws);
    for (int r = 0; r < GuitarRigNAMProcessor::maxRigs; ++r)
    {
        if (rigLevelKnob[r] != nullptr)
            rigLevelKnob[r]->setVisible (ws && r < processor.getRigCount());
        rigSegButtons[r].setVisible (ws);
    }
    addRigButton.setVisible (ws);
    if (drumRibbon != nullptr)
        drumRibbon->setVisible (ws);
    perfChip.getProperties().set ("chipActive", perfMode);
    perfChip.repaint();
    repaint();
    grabKeyboardFocus();
}

bool RigContent::isTunerOn() const
{
    return (bool) processor.apvts.state.getProperty ("tunerOn", true);
}

void RigContent::paint (juce::Graphics& g)
{
    const int W = getWidth(), H = getHeight();

    // overall background (radial at the top)
    {
        juce::ColourGradient grad (ui::bgTop, W * 0.5f, -H * 0.1f, ui::bg, W * 0.5f, H * 0.7f, true);
        g.setGradientFill (grad);
        g.fillAll();
    }

    // ---- top bar
    {
        g.setGradientFill ({ ui::barTop, 0.0f, 0.0f, ui::barBottom, 0.0f, 60.0f, false });
        g.fillRect (0, 0, W, 60);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (0, 59, W, 1);

        // logo with glow
        auto logo = juce::Rectangle<float> (18.0f, 15.0f, 30.0f, 30.0f);
        g.setColour (ui::accent.withAlpha (0.35f));
        g.fillRoundedRectangle (logo.expanded (3.0f), 2.0f);
        g.setGradientFill ({ ui::accent, logo.getX(), logo.getY(),
                             ui::accentDark, logo.getRight(), logo.getBottom(), false });
        g.fillRoundedRectangle (logo, 2.0f);
        {
            juce::Path diamond;
            diamond.addRoundedRectangle (-5.0f, -5.0f, 10.0f, 10.0f, 2.0f);
            diamond.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi / 4.0f)
                                        .translated (logo.getCentre()));
            g.setColour (ui::bg);
            g.fillPath (diamond);
        }

        g.setFont (ui::uiFont (16.0f, true));
        g.setColour (ui::textBright);
        g.drawText ("PedalForge", 56, 17, 110, 26, juce::Justification::centredLeft);

        auto badge = juce::Rectangle<float> (146.0f, 22.0f, 40.0f, 17.0f);
        g.setColour (ui::accent.withAlpha (0.35f));
        g.drawRoundedRectangle (badge, 5.0f, 1.0f);
        g.setFont (ui::monoFont (9.0f, true));
        g.setColour (ui::accent);
        g.drawText ("NAM", badge, juce::Justification::centred);

        // meter labels
        g.setFont (ui::monoFont (8.0f));
        g.setColour (ui::textFaint);
        g.drawText ("IN", inMeter.getX() - 28, inMeter.getY() - 4, 24, 12, juce::Justification::centredRight);
        if (clipTicks > 0)
        {
            g.setColour (ui::red);
            g.setFont (ui::monoFont (8.0f, true));
            g.drawText ("CLIP", outMeter.getX() - 32, outMeter.getY() - 4, 28, 12,
                        juce::Justification::centredRight);
            g.setColour (ui::textFaint);
            g.setFont (ui::monoFont (8.0f));
        }
        else
        {
            g.drawText ("OUT", outMeter.getX() - 28, outMeter.getY() - 4, 24, 12,
                        juce::Justification::centredRight);
        }
        {
            const float cpu = processor.cpuLoad.load();
            const bool overload = cpu >= 0.9f;
            g.setColour (overload ? ui::red : ui::textFaint);
            g.setFont (ui::monoFont (8.0f, overload));
            // left edge flush with the bar it labels: the old -14 pushed the
            // text onto the IN/OUT bars, which end exactly there
            g.drawText ((overload ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9a\xa0 CPU "))
                                  : juce::String ("CPU "))
                            + juce::String ((int) (cpu * 100.0f)) + "%",
                        cpuMeter.getX(), cpuMeter.getY() - 14,
                        songButton.getX() - 12 - cpuMeter.getX(), 12,
                        juce::Justification::centredLeft);
        }

        // divider between the meters/CPU group and the screen buttons. It has to
        // sit BEFORE the first of them (Song) - anchored to Drums it landed 4 px
        // inside the Song button and read as a stray border on it.
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (songButton.getX() - 8, 16, 1, 28);
    }

    // ---- rig workspace: fixed side cards + chain container frame (vNext R1)
    if (! perfMode)
    {
        auto cardFrame = [&g] (juce::Rectangle<int> b)
        {
            auto bf = b.toFloat();
            g.setGradientFill ({ ui::cardTop, 0.0f, bf.getY(),
                                 ui::cardBottom, 0.0f, bf.getBottom(), false });
            g.fillRoundedRectangle (bf, 3.0f);
            g.setColour (ui::border());
            g.drawRoundedRectangle (bf.reduced (0.5f), 3.0f, 1.0f);
        };
        auto vMeter = [&g] (juce::Rectangle<int> r, float db)
        {
            g.setColour (ui::meterBg);
            g.fillRoundedRectangle (r.toFloat(), 4.0f);
            const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            if (frac > 0.01f)
            {
                auto fill = r.toFloat();
                fill = fill.removeFromBottom (fill.getHeight() * frac);
                g.setGradientFill ({ ui::glowOrange, 0.0f, fill.getY(),
                                     ui::accent, 0.0f, fill.getBottom(), false });
                g.fillRoundedRectangle (fill, 4.0f);
            }
        };
        auto tag = [&g] (juce::Rectangle<int> r, const juce::String& text, juce::Colour c)
        {
            g.setColour (c.withAlpha (0.55f));
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setColour (c);
            g.setFont (ui::monoFont (7.5f, true));
            g.drawText (text, r, juce::Justification::centred);
        };

        // INPUT card (left)
        {
            const auto& b = inputCardB;
            cardFrame (b);
            g.setFont (ui::uiFont (12.0f, true));
            g.setColour (ui::textBright);
            g.drawText ("INPUT", b.getX(), b.getY() + 12, b.getWidth(), 14,
                        juce::Justification::centred);
            g.setFont (ui::monoFont (8.0f));
            g.setColour (ui::textFaint);
            g.drawFittedText (inputDeviceName.isNotEmpty() ? inputDeviceName
                                                           : juce::String ("audio in"),
                              b.getX() + 8, b.getY() + 30, b.getWidth() - 16, 12,
                              juce::Justification::centred, 1);

            // jack graphic (radial circle, as in the mockup)
            const float jx = (float) b.getCentreX(), jy = (float) b.getY() + 88.0f;
            g.setColour (juce::Colours::black);
            g.fillEllipse (jx - 24.0f, jy - 24.0f, 48.0f, 48.0f);
            g.setColour (juce::Colour (0xff363c45));
            g.drawEllipse (jx - 24.0f, jy - 24.0f, 48.0f, 48.0f, 3.0f);
            g.setColour (juce::Colour (0xff1e292e));
            g.drawEllipse (jx - 13.0f, jy - 13.0f, 26.0f, 26.0f, 5.0f);

            vMeter (inputMeterB, inMeterDb);
            const bool signalOk = inMeterDb > -50.0f;
            tag ({ b.getX() + 14, b.getBottom() - 40, b.getWidth() - 28, 20 },
                 signalOk ? "SIGNAL OK" : "NO SIGNAL",
                 signalOk ? ui::green : ui::textFaint);
        }

        // OUTPUT / MIXER card (right)
        {
            const auto& b = outputCardB;
            const int count = processor.getRigCount();
            cardFrame (b);
            g.setFont (ui::uiFont (12.0f, true));
            g.setColour (ui::textBright);
            g.drawText ("OUTPUT / MIXER", b.getX(), b.getY() + 12, b.getWidth(), 14,
                        juce::Justification::centred);
            g.setFont (ui::monoFont (8.0f));
            g.setColour (ui::textFaint);
            // same wording as the button below it: these are AMP+CAB pairs
            g.drawText (juce::String (count) + " AMP+CAB" + (count > 1 ? " pairs" : ""),
                        b.getX(), b.getY() + 30, b.getWidth(), 12,
                        juce::Justification::centred);

            vMeter (outputMeterB, outMeterDb);
            const auto peakText = clipTicks > 0
                ? juce::String ("CLIP")
                : "PEAK " + juce::String (juce::jmax (-60.0f, outMeterDb), 1) + " dB";
            tag ({ b.getX() + 14, b.getBottom() - 72, b.getWidth() - 28, 20 },
                 peakText, clipTicks > 0 ? ui::red : ui::textDim);
        }

        // chain container: frame + 38 px header strip ("SIGNAL FLOW" + hint;
        // the "+ EFFECT" button and the minimap are child components)
        {
            const auto& b = chainWrapB;
            auto bf = b.toFloat();
            g.setColour (ui::bg);
            g.fillRoundedRectangle (bf, 3.0f);
            g.setColour (ui::border());
            g.drawRoundedRectangle (bf.reduced (0.5f), 3.0f, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (b.getX() + 1, b.getY() + 38, b.getWidth() - 2, 1);

            g.setFont (ui::monoFont (9.0f, true));
            g.setColour (ui::text);
            g.drawText ("SIGNAL FLOW", b.getX() + 14, b.getY() + 13, 120, 12,
                        juce::Justification::centredLeft);
            g.setFont (ui::monoFont (8.0f));
            g.setColour (ui::textFaint);
            g.drawText (juce::String (juce::CharPointer_UTF8 (
                            "double-click centers \xc2\xb7 drag reorders \xc2\xb7 Ctrl+Z undo")),
                        b.getX() + 130, b.getY() + 13,
                        juce::jmax (60, b.getWidth() - 130 - 104), 12,
                        juce::Justification::centredRight);
        }
    }

    // ---- bottom bar (mockup .rig-bottom): tuneline + tech tags on the right
    {
        const int barY = H - 60;
        g.setGradientFill ({ ui::barTop, 0.0f, (float) barY, ui::barBottom, 0.0f, (float) H, false });
        g.fillRect (0, barY, W, 60);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (0, barY, W, 1);

        // right tags: real host rate/buffer + reference pitch/latency
        // (load errors and the auto-ECO notice take over the same corner)
        int tagsLeft = W - 22;
        {
            const double sr = processor.getSampleRate();
            const int bs = processor.getBlockSize();
            const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
            const auto err = processor.getLoadError();

            if (err.isNotEmpty() || ecoNoticeTicks > 0)
            {
                const bool isErr = err.isNotEmpty();
                g.setFont (ui::monoFont (9.5f));
                g.setColour (isErr ? ui::red : ui::accent);
                g.drawText (isErr ? "Error: " + err
                                  : juce::String (juce::CharPointer_UTF8 (
                                        "Auto ECO enabled (high CPU)")),
                            W - 22 - 360, barY, 360, 60, juce::Justification::centredRight);
                tagsLeft = W - 22 - 360;
            }
            else
            {
                const auto hostText = sr > 0
                    ? "HOST " + juce::String (sr / 1000.0, sr == 44100.0 ? 1 : 0) + " kHz"
                          + dot + juce::String (bs) + " smp"
                    : juce::String (juce::CharPointer_UTF8 ("HOST \xe2\x80\x94"));
                const auto latText = sr > 0
                    ? "A = 440 Hz" + dot + "latency "
                          + juce::String (bs * 1000.0 / sr, 1) + " ms"
                    : juce::String ("A = 440 Hz");

                g.setFont (ui::monoFont (8.5f));
                g.setColour (ui::textFaint);
                const int latW = 16 + 6 * latText.length();
                g.drawText (latText, W - 22 - latW, barY + 16, latW, 28,
                            juce::Justification::centredRight);

                const int hostW = 16 + 6 * hostText.length();
                auto hostR = juce::Rectangle<int> (W - 22 - latW - 8 - hostW, barY + 17,
                                                   hostW, 26);
                g.setColour (ui::green.withAlpha (0.5f));
                g.drawRoundedRectangle (hostR.toFloat().reduced (0.5f), 4.0f, 1.0f);
                g.setColour (ui::green);
                g.setFont (ui::monoFont (8.0f, true));
                g.drawText (hostText, hostR, juce::Justification::centred);
                tagsLeft = hostR.getX();
            }
        }

        // inline tuneline (vNext R2): baseline + center tick + cents needle
        // with the usual green/amber/red colours and a central note label.
        // Nothing is drawn while the tuner is off (real toggle).
        if (isTunerOn())
        {
            const int tlX = 380;
            const int tlRight = juce::jmin (tlX + 420, tagsLeft - 16);
            if (tlRight - tlX > 140)
            {
                const float cy = (float) barY + 42.0f;
                const float cx = (float) (tlX + tlRight) / 2.0f;

                g.setColour (juce::Colours::white.withAlpha (0.14f));
                g.fillRect ((float) tlX, cy, (float) (tlRight - tlX), 1.0f);
                g.setColour (ui::accent.withAlpha (0.9f));
                g.fillRect (cx - 1.0f, (float) barY + 14.0f, 2.0f, cy - (float) barY - 8.0f);

                const bool hasPitch = tunerFreq > 0.0 && tunerNote.isNotEmpty();
                const double absCents = std::abs (tunerCents);
                const juce::Colour needleC = absCents < 5.0 ? ui::green
                                             : absCents < 15.0 ? ui::yellow
                                                               : ui::red;
                if (hasPitch)
                {
                    const float half = (float) (tlRight - tlX) / 2.0f - 10.0f;
                    const float nx = cx + (float) juce::jlimit (-50.0, 50.0, tunerCents)
                                              / 50.0f * half;
                    g.setColour (needleC.withAlpha (0.35f));
                    g.fillRoundedRectangle (nx - 3.0f, cy - 16.0f, 6.0f, 20.0f, 3.0f);
                    g.setColour (needleC);
                    g.fillRoundedRectangle (nx - 1.5f, cy - 16.0f, 3.0f, 20.0f, 1.5f);

                    const auto label = tunerNote + juce::String (tunerOctave)
                                       + juce::String::fromUTF8 (" \xc2\xb7 ")
                                       + juce::String (tunerCents, 1) + " cents";
                    g.setFont (ui::monoFont (10.0f, true));
                    g.setColour (needleC);
                    g.drawText (label, (int) cx - 90, barY + 8, 180, 14,
                                juce::Justification::centred);
                }
                else
                {
                    g.setFont (ui::monoFont (9.0f));
                    g.setColour (ui::textFaint);
                    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc2\xb7 \xc2\xb7 \xc2\xb7")),
                                (int) cx - 40, barY + 8, 80, 14, juce::Justification::centred);
                }
            }
        }
    }

    if (perfMode)
        paintPerformanceView (g);
}

void RigContent::paintPerformanceView (juce::Graphics& g)
{
    const int W = getWidth(), H = getHeight();
    const auto area = juce::Rectangle<int> (0, 60, W, H - 120);

    // ---- vNext S1: status boxes (input/output/CPU/sync), mockup .stage-status
    {
        struct Box { juce::String text; juce::Colour c; };
        const float cpu = processor.cpuLoad.load();
        const bool sync = processor.drumHostSync.load() && processor.getHostBpm() > 0.0f;
        const float bpm = sync ? processor.getHostBpm()
                               : processor.drumEngine.bpm.load();
        const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        const Box boxes[4] = {
            { "INPUT " + juce::String ((int) juce::jmax (-60.0f, inMeterDb)) + " dB",
              inMeterDb > -50.0f ? ui::green : ui::textFaint },
            { "OUT " + juce::String ((int) juce::jmax (-60.0f, outMeterDb)) + " dB",
              clipTicks > 0 ? ui::red : ui::textDim },
            { "CPU " + juce::String ((int) (cpu * 100.0f)) + "%",
              cpu > 0.8f ? ui::red : ui::textDim },
            { (sync ? juce::String ("DAW SYNC") : juce::String ("INTERNAL"))
                  + dot + juce::String ((int) bpm) + " BPM",
              sync ? ui::green : ui::textFaint },
        };

        int widths[4], total = 0;
        for (int i = 0; i < 4; ++i)
        {
            widths[i] = 26 + 6 * boxes[i].text.length();
            total += widths[i] + (i > 0 ? 8 : 0);
        }
        int bx = (W - total) / 2;
        for (int i = 0; i < 4; ++i)
        {
            const juce::Rectangle<int> r (bx, area.getY() + 6, widths[i], 26);
            g.setColour (ui::cardBottom.withAlpha (0.7f));
            g.fillRoundedRectangle (r.toFloat(), 6.0f);
            g.setColour (ui::border());
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
            g.setColour (boxes[i].c);
            g.setFont (ui::monoFont (8.5f, true));
            g.drawText (boxes[i].text, r, juce::Justification::centred);
            bx += widths[i] + 8;
        }
    }

    // ---- huge preset (click: left = previous, right = next,
    //      center = menu)
    const auto presetName = processor.getCurrentPresetName();
    const bool dirty = presetDirtyCached;
    g.setFont (ui::uiFont (48.0f, true));
    g.setColour (ui::textBright);
    g.drawFittedText ((dirty ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2 ")) : juce::String())
                          + (presetName.isNotEmpty() ? presetName : juce::String ("(no preset)")),
                      area.getX() + 120, area.getY() + 40, area.getWidth() - 240, 60,
                      juce::Justification::centred, 1);

    // navigation arrows on the sides
    g.setFont (ui::uiFont (40.0f, true));
    g.setColour (ui::textFaint);
    g.drawText (juce::CharPointer_UTF8 ("\xe2\x97\x82"), area.getX() + 30, area.getY() + 40, 60, 60,
                juce::Justification::centred);
    g.drawText (juce::CharPointer_UTF8 ("\xe2\x96\xb8"), area.getRight() - 90, area.getY() + 40, 60, 60,
                juce::Justification::centred);

    // loaded capture + rigs
    {
        const auto model = processor.getModelName (0);
        juce::String info = model.isNotEmpty()
                                ? model
                                : juce::String ("- no capture -");
        if (processor.getRigCount() > 1)
            info += juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
                    + juce::String (processor.getRigCount()) + " rigs";
        // vNext S1: "· SCENE X" when the playing section has a scene
        {
            const int uiBar = processor.drumEngine.uiBar.load();
            const int sec = uiBar >= 0 ? uiBar / drum::barsPerSection : 0;
            if (processor.hasScene (sec))
            {
                auto sceneName = processor.getSceneName (sec);
                if (sceneName.isEmpty())
                    sceneName = juce::String::charToString ((juce::juce_wchar) ('A' + sec));
                info += juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 SCENE "))
                        + sceneName.toUpperCase();
            }
        }
        g.setFont (ui::monoFont (14.0f));
        g.setColour (ui::accent);
        g.drawText (info, area.getX(), area.getY() + 108, area.getWidth(), 20,
                    juce::Justification::centred);
    }

    // ---- large tuner
    {
        const int cy = area.getCentreY() + 60;
        const bool hasNote = tunerNote.isNotEmpty();
        const bool inTune = hasNote && std::abs (tunerCents) <= 5.0;

        g.setFont (ui::monoFont (84.0f, true));
        g.setColour (! hasNote ? ui::textMuted : inTune ? ui::green : ui::textBright);
        g.drawText (hasNote ? tunerNote : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")),
                    area.getX(), cy - 110, area.getWidth(), 100, juce::Justification::centred);

        // cents ruler: -50 .. +50, needle at the position
        const int barW = juce::jmin (560, W - 200);
        auto bar = juce::Rectangle<float> ((float) (W - barW) / 2.0f, (float) cy + 10.0f,
                                           (float) barW, 12.0f);
        g.setColour (ui::meterBg);
        g.fillRoundedRectangle (bar, 6.0f);

        g.setColour (juce::Colours::white.withAlpha (0.15f));
        for (int t = -40; t <= 40; t += 10)
        {
            const float tx = bar.getCentreX() + (float) t / 50.0f * bar.getWidth() / 2.0f;
            g.fillRect (tx - 0.5f, bar.getY() - 5.0f, 1.0f, bar.getHeight() + 10.0f);
        }
        g.setColour (ui::green.withAlpha (0.5f));
        g.fillRect (bar.getCentreX() - 1.0f, bar.getY() - 8.0f, 2.0f, bar.getHeight() + 16.0f);

        if (hasNote)
        {
            const float nx = bar.getCentreX()
                             + (float) juce::jlimit (-50.0, 50.0, tunerCents) / 50.0f
                                   * bar.getWidth() / 2.0f;
            g.setColour (inTune ? ui::green : ui::glowOrange);
            g.fillRoundedRectangle (nx - 3.0f, bar.getY() - 10.0f, 6.0f,
                                    bar.getHeight() + 20.0f, 3.0f);

            g.setFont (ui::monoFont (16.0f, true));
            g.drawText ((tunerCents >= 0 ? "+" : "") + juce::String (tunerCents, 1) + " cents",
                        area.getX(), (int) bar.getBottom() + 14, area.getWidth(), 20,
                        juce::Justification::centred);
        }
        else
        {
            g.setFont (ui::monoFont (12.0f));
            g.setColour (ui::textFaint);
            g.drawText ("play a string to tune", area.getX(), (int) bar.getBottom() + 14,
                        area.getWidth(), 18, juce::Justification::centred);
        }
    }

    // ---- vNext: side tiles (large, operable from a distance) ----
    {
        auto tile = [&] (int idx, juce::Rectangle<int> r, const juce::String& label,
                         const juce::String& big, const juce::String& sub, bool active)
        {
            stageTiles[idx] = r;
            g.setColour (active ? ui::accent.withAlpha (0.10f)
                                : ui::cardBottom.withAlpha (0.76f));
            g.fillRoundedRectangle (r.toFloat(), 10.0f);
            g.setColour (active ? ui::accent.withAlpha (0.7f) : ui::border());
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
            g.setColour (ui::textFaint);
            g.setFont (ui::monoFont (9.0f, true));
            g.drawText (label, r.getX(), r.getY() + 10, r.getWidth(), 12,
                        juce::Justification::centred);
            g.setColour (active ? ui::accentBright : ui::textDim);
            g.setFont (ui::uiFont (24.0f, true));
            g.drawText (big, r.getX(), r.getCentreY() - 16, r.getWidth(), 30,
                        juce::Justification::centred);
            g.setColour (ui::textFaint);
            g.setFont (ui::monoFont (8.5f));
            g.drawFittedText (sub, r.getX() + 8, r.getBottom() - 26, r.getWidth() - 16, 18,
                              juce::Justification::centred, 1);
        };

        const int tileW = 196, tileH = (area.getHeight() - 206 - 2 * 12) / 3;
        const int tileY0 = area.getY() + 76;
        auto boolParam = [&] (const char* id)
        {
            auto* p = processor.apvts.getRawParameterValue (id);
            return p != nullptr && p->load() > 0.5f;
        };
        auto choiceText = [&] (const char* id) -> juce::String
        {
            if (auto* p = processor.apvts.getParameter (id))
                return p->getCurrentValueAsText();
            return {};
        };

        // left: guitar blocks
        const auto model = processor.getModelName (0);
        tile (0, { 34, tileY0, tileW, tileH }, "AMP",
              boolParam ("ampOn") ? "ON" : "OFF",
              model.isNotEmpty() ? model : juce::String ("- no capture -"),
              boolParam ("ampOn"));
        tile (1, { 34, tileY0 + tileH + 12, tileW, tileH }, "DRIVE",
              boolParam ("odOn") ? choiceText ("odType").toUpperCase() : "OFF",
              "tap to bypass", boolParam ("odOn"));
        tile (2, { 34, tileY0 + 2 * (tileH + 12), tileW, tileH }, "DELAY",
              boolParam ("delayOn") ? "ON" : "OFF",
              "mix " + choiceText ("delayMix"), boolParam ("delayOn"));

        // right: drums / rec / input mute
        const bool playing = processor.drumEngine.playing.load();
        const int uiBar = processor.drumEngine.uiBar.load();
        tile (3, { W - 34 - tileW, tileY0, tileW, tileH }, "DRUMS",
              playing ? "PLAY" : "STOP",
              playing && uiBar >= 0
                  ? "bar " + juce::String (uiBar + 1) + "/"
                        + juce::String (processor.drumEngine.totalBars())
                  : juce::String ((int) processor.drumEngine.bpm.load()) + " BPM",
              playing);
        tile (4, { W - 34 - tileW, tileY0 + tileH + 12, tileW, tileH }, "RECORD",
              processor.isRecording() ? "REC" : "READY", "24-bit WAV",
              processor.isRecording());

        // vNext S1: NEXT SCENE tile (replaces INPUT) - upcoming section's scene
        {
            const int next = nextDrumSection();
            juce::String big (juce::CharPointer_UTF8 ("\xe2\x80\x94")), sub ("last section");
            bool active = false;
            if (next >= 0)
            {
                auto name = processor.getSceneName (next);
                if (name.isEmpty())
                    name = "Section " + juce::String::charToString (
                               (juce::juce_wchar) ('A' + next));
                big = name.toUpperCase();
                active = processor.hasScene (next);
                // On stage this tile must never promise something it cannot do:
                // it used to read "tap to apply" for a section with NO snapshot,
                // while the footswitch row below correctly showed nothing.
                if (! active)
                {
                    sub = "no rig saved here";
                }
                else if (playing && uiBar >= 0)
                {
                    const int barsLeft = drum::barsPerSection
                                         - (uiBar % drum::barsPerSection);
                    sub = "in " + juce::String (barsLeft)
                          + (barsLeft == 1 ? " bar" : " bars");
                }
                else
                {
                    sub = "tap to apply";
                }
            }
            tile (5, { W - 34 - tileW, tileY0 + 2 * (tileH + 12), tileW, tileH },
                  "NEXT SCENE", big, sub, active);
        }
    }

    // ---- vNext: footswitch-style action row ----
    {
        const char* labels[5] = { "\xe2\x80\xb9 PRESET", nullptr, "TAP TEMPO",
                                  nullptr, nullptr };
        const bool playing = processor.drumEngine.playing.load();
        const juce::String drumsLbl = playing
            ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0 STOP DRUMS"))
            : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6 PLAY DRUMS"));
        const juce::String tunerLbl = isTunerOn() ? "TUNER ON" : "TUNER OFF";
        const juce::String sceneLbl (juce::CharPointer_UTF8 ("NEXT SCENE \xe2\x80\xba"));
        // 5th action applies the next section's scene on the spot (vNext S1)
        juce::String sceneSub (juce::CharPointer_UTF8 ("\xe2\x80\x94"));
        if (const int next = nextDrumSection(); next >= 0 && processor.hasScene (next))
        {
            auto name = processor.getSceneName (next);
            if (name.isEmpty())
                name = "Section " + juce::String::charToString (
                           (juce::juce_wchar) ('A' + next));
            sceneSub = name;
        }
        const juce::String subs[5] = { "footswitch 1", "footswitch 2",
                                       juce::String ((int) processor.drumEngine.bpm.load()) + " BPM",
                                       "footswitch 4", sceneSub };

        const int rowY = area.getBottom() - 96, rowH = 74;
        const int btnW = (W - 2 * 34 - 4 * 10) / 5;
        for (int i = 0; i < 5; ++i)
        {
            const juce::Rectangle<int> r (34 + i * (btnW + 10), rowY, btnW, rowH);
            stageActions[i] = r;
            const bool hot = i == 1 && playing;
            g.setColour (hot ? ui::accent : ui::cardBottom.withAlpha (0.85f));
            g.fillRoundedRectangle (r.toFloat(), 9.0f);
            g.setColour (hot ? ui::accent : ui::border());
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 9.0f, 1.0f);
            g.setColour (hot ? ui::accentTextDark : ui::text);
            g.setFont (ui::uiFont (13.0f, true));
            const juce::String big = i == 1 ? drumsLbl
                                   : i == 3 ? tunerLbl
                                   : i == 4 ? sceneLbl
                                            : juce::String (juce::CharPointer_UTF8 (labels[i]));
            g.drawText (big, r.getX(), r.getY() + 16, r.getWidth(), 20,
                        juce::Justification::centred);
            g.setColour (hot ? ui::accentTextDark.withAlpha (0.75f) : ui::textFaint);
            g.setFont (ui::monoFont (8.5f));
            g.drawText (subs[i], r.getX(), r.getBottom() - 28, r.getWidth(), 14,
                        juce::Justification::centred);
        }
    }

    // ---- hints
    g.setFont (ui::monoFont (10.0f));
    g.setColour (ui::textFaint);
    g.drawText (juce::CharPointer_UTF8 ("\xe2\x86\x90/\xe2\x86\x92 presets \xc2\xb7 "
                                        "space toggles the amp \xc2\xb7 "
                                        "T tuner \xc2\xb7 F/Esc back to editing"),
                area.getX(), area.getBottom() - 110, area.getWidth(), 14,
                juce::Justification::centred);
}

void RigContent::timerCallback()
{
    auto toDb = [] (float linear) { return juce::Decibels::gainToDecibels (linear, -80.0f); };

    inMeterDb = juce::jmax (toDb (processor.inputPeak.load()), inMeterDb - 2.2f);
    outMeterDb = juce::jmax (toDb (processor.outputPeak.load()), outMeterDb - 2.2f);
    inMeter.setLevel (inMeterDb);
    outMeter.setLevel (outMeterDb);

    if (processor.outputPeak.load() >= 0.999f)
        clipTicks = 60; // ~2 s warning
    else if (clipTicks > 0)
        --clipTicks;

    const float cpu = processor.cpuLoad.load();
    cpuMeter.setFraction (cpu, cpu > 0.8f ? ui::red : cpu > 0.5f ? ui::yellow : ui::accent);

    // the preset fingerprint is XML serialization - check at 2 Hz, not 30 Hz
    if (tunerTick % 15 == 0)
    {
        processor.settlePresetBaseline();
        presetDirtyCached = processor.isPresetDirty();
    }

    const auto presetName = processor.getCurrentPresetName();
    const bool dirty = presetDirtyCached;
    presetPill.setButtonText (processor.isLoadingModel()
                                  ? juce::String (juce::CharPointer_UTF8 ("Loading..."))
                                  : (presetName.isNotEmpty()
                                         ? (dirty ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2 ")) + presetName
                                                  : presetName)
                                         : juce::String ("(no preset)")));
    presetPill.dotLit = processor.anyModelLoaded();

    if (saveFlashTicks > 0)
    {
        --saveFlashTicks;
        saveButton.setButtonText (saveFlashTicks > 0 ? "Saved" : "SAVE");
    }

    if (! focusGrabbed && isShowing())
    {
        focusGrabbed = true;
        grabKeyboardFocus();
    }

    chainView->refreshDynamicText();
    refreshSidecarImages();

    // vNext R2: chain-order history watcher - captures changes from any
    // source (drag reorder, drawer insert, "x" remove, presets)
    {
        const auto orderNow = processor.getChainOrder();
        if (orderNow != chainOrderSeen)
        {
            chainUndoStack.push_back (chainOrderSeen);
            if (chainUndoStack.size() > 40)
                chainUndoStack.erase (chainUndoStack.begin());
            chainRedoStack.clear();
            chainOrderSeen = orderNow;
        }
        chainUndoButton.setEnabled (! chainUndoStack.empty());
        chainRedoButton.setEnabled (! chainRedoStack.empty());
    }

    // OUTPUT card: the 1/2/3 segment mirrors the real rig count; the knob
    // row relayouts when the count changes (params can move under presets)
    {
        const int count = processor.getRigCount();
        if (count != rigCountSeen)
        {
            rigCountSeen = count;
            resized();
            repaint();
        }
        for (int r = 0; r < GuitarRigNAMProcessor::maxRigs; ++r)
        {
            const bool on = count == r + 1;
            if ((bool) rigSegButtons[r].getProperties()["chipActive"] != on)
            {
                rigSegButtons[r].getProperties().set ("chipActive", on);
                rigSegButtons[r].repaint();
            }
        }
        addRigButton.setEnabled (count < GuitarRigNAMProcessor::maxRigs);
    }

    // INPUT card subtitle: audio device name, polled at 2 Hz (standalone)
    if (tunerTick % 15 == 0)
    {
        juce::String dev ("host audio");
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            if (auto* d = holder->deviceManager.getCurrentAudioDevice())
                dev = d->getName();
        if (dev != inputDeviceName)
        {
            inputDeviceName = dev;
            repaint (inputCardB);
        }
    }

    // retired VST3 instance is deleted here (message thread, outside audio)
    processor.collectExternalRetired();

    applyEcoSwitchIfNeeded();
    if (ecoNoticeTicks > 0)
        --ecoNoticeTicks;

    if (++tunerTick % 3 == 0 && (isTunerOn() || perfMode))
        analyseTuner();

    // the chip may have changed via state/preset load
    if ((bool) tunerToggle.getProperties()["chipActive"] != isTunerOn())
    {
        tunerToggle.getProperties().set ("chipActive", isTunerOn());
        tunerToggle.repaint();
    }

    // tuner mute only applies with the tuner active (or on stage)
    processor.setTunerMuted (tunerMuteWanted && (isTunerOn() || perfMode));

    // recorder: shows the elapsed time on the chip
    {
        juce::String recText;
        if (processor.isRecording())
        {
            const int secs = (int) ((juce::Time::currentTimeMillis() - recStartMs) / 1000);
            recText = juce::String::fromUTF8 ("\xe2\x96\xa0 ")
                      + juce::String (secs / 60) + ":"
                      + juce::String (secs % 60).paddedLeft ('0', 2);
        }
        else if (recSavedTicks > 0)
        {
            --recSavedTicks;
            recText = "SAVED";
        }
        else
        {
            recText = juce::String::fromUTF8 ("\xe2\x97\x8f REC");
        }
        if (recChip.getButtonText() != recText)
            recChip.setButtonText (recText);
        if ((bool) recChip.getProperties()["chipActive"] != processor.isRecording())
        {
            recChip.getProperties().set ("chipActive", processor.isRecording());
            recChip.repaint();
        }
    }

    if (perfMode)
        repaint(); // large tuner/meters live
    else
    {
        repaint (0, 0, getWidth(), 60);
        repaint (0, getHeight() - 60, getWidth(), 60);
        // live side-card meters/tags + minimap viewport window
        repaint (inputMeterB.expanded (4));
        repaint (inputCardB.getX(), inputCardB.getBottom() - 44, inputCardB.getWidth(), 28);
        repaint (outputMeterB.expanded (4));
        repaint (outputCardB.getX(), outputCardB.getBottom() - 78, outputCardB.getWidth(), 30);
        if (chainMinimap != nullptr)
            chainMinimap->repaint();
    }
}

void RigContent::applyEcoSwitchIfNeeded()
{
    auto& apvts = processor.apvts;
    const bool eco = apvts.getRawParameterValue ("ampEco")->load() > 0.5f;
    const bool autoEco = apvts.getRawParameterValue ("autoEco")->load() > 0.5f;

    // auto-ECO: CPU above 90% for ~2 s turns on the light mode (never turns
    // off by itself, to avoid constantly switching the tone)
    if (autoEco && ! eco && processor.hasEcoVariant()
        && processor.cpuLoad.load() >= 0.9f)
    {
        if (++cpuHighTicks >= 60)
        {
            cpuHighTicks = 0;
            if (auto* p = apvts.getParameter ("ampEco"))
                p->setValueNotifyingHost (1.0f);
            ecoNoticeTicks = 120; // notice for ~4 s in the bottom bar
        }
    }
    else
    {
        cpuHighTicks = 0;
    }

    // keeps the loaded file consistent with the mode (chip, preset or auto),
    // lane by lane
    const bool ecoNow = apvts.getRawParameterValue ("ampEco")->load() > 0.5f;
    for (int r = 0; r < GuitarRigNAMProcessor::maxRigs; ++r)
    {
        const auto ecoPath = processor.getModelPathEco (r);
        const auto target = ecoNow && ecoPath.isNotEmpty()
                                ? ecoPath
                                : processor.getModelPathNormal (r);
        if (target.isNotEmpty() && ! processor.isLoadingModel()
            && target != processor.getModelPath (r))
            processor.loadModelAsync (r, juce::File (target));
    }
}

void RigContent::analyseTuner()
{
    const double sr = processor.getSampleRate();
    if (sr <= 0)
        return;

    constexpr int N = 2048;
    float buf[N];
    processor.readTunerBlock (buf, N);

    const double freq = detectPitchHz (buf, N, sr);
    tunerFreq = freq;

    if (freq > 0.0)
    {
        const double midi = 69.0 + 12.0 * std::log2 (freq / 440.0);
        const int nearest = juce::roundToInt (midi);
        tunerCents = (midi - nearest) * 100.0;
        tunerNote = kNoteNames[((nearest % 12) + 12) % 12];
        tunerOctave = nearest / 12 - 1;   // scientific pitch ("E2" for 82.4 Hz)

        tunerStringIndex = -1;
        double bestDiff = 1.0e9;
        for (int i = 0; i < 6; ++i)
        {
            const double diff = std::abs (std::log2 (freq / kStringFreqs[i]));
            if (diff < bestDiff)
            {
                bestDiff = diff;
                tunerStringIndex = i;
            }
        }
        if (bestDiff > 0.12) // > ~1.4 semitones from any string
            tunerStringIndex = -1;
    }
    else
    {
        tunerNote.clear();
        tunerStringIndex = -1;
    }
}

void RigContent::refreshSidecarImages()
{
    // Reloads when the path changes OR when the sidecar appears later
    // (the photo write is asynchronous to the download).
    auto refresh = [] (const juce::String& path, juce::String& cachedPath, bool& hadImage,
                       auto&& apply)
    {
        const juce::File sidecar (path + ".img");
        const bool exists = path.isNotEmpty() && sidecar.existsAsFile();

        if (path == cachedPath && hadImage == exists)
            return;

        cachedPath = path;
        hadImage = exists;
        juce::Image img;
        if (exists)
            img = juce::ImageFileFormat::loadFrom (sidecar);
        apply (std::move (img));
    };

    for (int r = 0; r < GuitarRigNAMProcessor::maxRigs; ++r)
    {
        refresh (processor.getModelPath (r), loadedModelPaths[r], ampImagesLoaded[r],
                 [this, r] (juce::Image img) { chainView->setAmpImage (r, std::move (img)); });
        refresh (processor.getIrPath (r), loadedIrPaths[r], cabImagesLoaded[r],
                 [this, r] (juce::Image img) { chainView->setCabImage (r, std::move (img)); });
    }
}

void RigContent::chooseModelSource (int lane)
{
    // Entry point from the signal chain (TONE3000 design requirement): the amp
    // card's LOAD/CHANGE offers the Tone Store first, then a local .nam file.
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    menu.addSectionHeader (processor.hasModelLoaded (lane) ? "Change capture" : "Load capture");
    menu.addItem (1, "Browse TONE3000 Tone Store\xe2\x80\xa6");
    menu.addItem (2, "Load .nam file from disk\xe2\x80\xa6");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chainView->getLoadButton (lane)),
        [safe = juce::Component::SafePointer<RigContent> (this), lane] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1)      safe->storeOverlay->open();
            else if (r == 2) safe->chooseModelFile (lane);
        });
}

void RigContent::chooseModelFile (int lane)
{
    auto initialDir = juce::File (processor.getModelPath (lane)).getParentDirectory();
    if (! initialDir.isDirectory())
        initialDir = Tone3000Client::capturesDir();

    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose NAM capture (.nam) for AMP " + juce::String (lane + 1),
        initialDir, "*.nam");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this, lane] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file.existsAsFile())
                                      processor.setModelPair (lane, file, {}); // local: no eco pair
                              });
}

void RigContent::chooseExtPluginFile (int slot)
{
    // Menu by CATEGORY: lists the installed .vst3s (system + user folder
    // - the built-in installer uses the user's, no admin), grouped
    // by the built-in catalog (PluginCatalog) + install what's missing + disk.
    juce::Array<juce::File> found;
    for (const auto& dir : { plugcat::systemVst3Dir(), plugcat::userVst3Dir() })
        if (dir.isDirectory())
            for (const auto& f : dir.findChildFiles (juce::File::findFilesAndDirectories,
                                                     false, "*.vst3"))
                found.add (f);

    struct Category { const char* title; std::initializer_list<const char*> keys; };
    static const Category categories[] = {
        { "Reverb & Ambience", { "dragonfly", "reverb", "surge" } },
        { "Airwindows Collection", { "airwindows", "airwin" } },
        { "Pedals & Dynamics (Zam)", { "zam", "zamaudio" } },
        { "Drives & Pedals",            { "fire", "wolf-shaper", "peakeater" } },
        { "Neural / captures",          { "aida", "proteus", "neural" } },
        { "Amp sims",                   { "bias", "amplitube", "guitar rig", "th-u",
                                          "stormblade" } },
    };

    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);
    const auto current = processor.getExternalPluginPath (slot);
    juce::Array<bool> used;
    used.insertMultiple (0, false, found.size());

    auto matches = [] (const juce::String& lowerName,
                       std::initializer_list<const char*> keys)
    {
        for (auto* k : keys)
            if (lowerName.contains (k))
                return true;
        return false;
    };

    for (const auto& cat : categories)
    {
        bool any = false;
        for (int i = 0; i < found.size(); ++i)
            if (! used[i] && matches (found[i].getFileName().toLowerCase(), cat.keys))
                any = true;
        if (! any)
            continue;

        menu.addSectionHeader (juce::String (juce::CharPointer_UTF8 (cat.title)));
        for (int i = 0; i < found.size(); ++i)
            if (! used[i] && matches (found[i].getFileName().toLowerCase(), cat.keys))
            {
                menu.addItem (i + 1, found[i].getFileNameWithoutExtension(), true,
                              found[i].getFullPathName() == current);
                used.set (i, true);
            }
    }

    bool anyOther = false;
    for (int i = 0; i < found.size(); ++i)
        if (! used[i])
            anyOther = true;
    if (anyOther)
    {
        menu.addSectionHeader ("Other");
        for (int i = 0; i < found.size(); ++i)
            if (! used[i])
                menu.addItem (i + 1, found[i].getFileNameWithoutExtension(), true,
                              found[i].getFullPathName() == current);
    }

    menu.addSeparator();
    menu.addItem (9100, juce::String (juce::CharPointer_UTF8 (
                      "Manage plugins (install/uninstall)...")));
    menu.addItem (9000, juce::String (juce::CharPointer_UTF8 ("Browse file...")));

    menu.showMenuAsync (juce::PopupMenu::Options(),
        [safe = juce::Component::SafePointer<RigContent> (this), found, slot] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            auto* self = safe.getComponent();

            if (result >= 1 && result <= found.size())
            {
                self->processor.loadExternalPluginAsync (slot, found[result - 1]);
                return;
            }

            // plugin manager (store's Plugins tab)
            if (result == 9100)
            {
                self->storeOverlay->openOnPlugins();
                return;
            }

            if (result != 9000)
                return;

            // browse on disk
            auto initialDir = juce::File (self->processor.getExternalPluginPath (slot))
                                  .getParentDirectory();
            if (! initialDir.isDirectory())
                initialDir = juce::File ("C:\\Program Files\\Common Files\\VST3");
            if (! initialDir.isDirectory())
                initialDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

            self->fileChooser = std::make_unique<juce::FileChooser> (
                "Choose VST3 plugin (.vst3)", initialDir, "*.vst3");
            self->fileChooser->launchAsync (
                juce::FileBrowserComponent::openMode
                    | juce::FileBrowserComponent::canSelectFiles
                    | juce::FileBrowserComponent::canSelectDirectories,
                [safe, slot] (const juce::FileChooser& fc)
                {
                    const auto file = fc.getResult();
                    if (safe != nullptr && file.exists())
                        safe->processor.loadExternalPluginAsync (slot, file);
                });
        });
}

// Floating window with the hosted plugin's panel; closes by itself before
// any instance change (onExternalPluginWillChange).
class ExtPluginWindow : public juce::DocumentWindow
{
public:
    ExtPluginWindow (juce::AudioPluginInstance& inst, std::function<void()> onCloseIn)
        : juce::DocumentWindow (inst.getName(), juce::Colour (0xff14181d),
                                juce::DocumentWindow::closeButton),
          onClose (std::move (onCloseIn))
    {
        setUsingNativeTitleBar (true);
        juce::AudioProcessorEditor* ed = inst.createEditorIfNeeded();
        if (ed != nullptr)
            setContentOwned (ed, true);
        else
            setContentOwned (new juce::GenericAudioProcessorEditor (inst), true);
        setResizable (ed == nullptr || ed->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override
    {
        if (onClose)
            onClose();
    }

private:
    std::function<void()> onClose;
};

void RigContent::openExtPluginWindow (int slot)
{
    auto* inst = processor.getExternalInstance (slot);
    if (inst == nullptr || ! processor.hasExternalPlugin (slot))
        return;

    if (extWindow[slot] != nullptr)
    {
        extWindow[slot]->toFront (true);
        return;
    }

    extWindow[slot] = std::make_unique<ExtPluginWindow> (
        *inst, [safe = juce::Component::SafePointer<RigContent> (this), slot]
        {
            if (safe != nullptr)
                safe->closeExtPluginWindow (slot);
        });
}

void RigContent::closeExtPluginWindow (int slot)
{
    if (slot >= 0 && slot < GuitarRigNAMProcessor::maxExtSlots)
        extWindow[slot].reset();
}

void RigContent::closeAllExtPluginWindows()
{
    for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
        extWindow[s].reset();
}

void RigContent::chooseDrumVstFile()
{
    auto initialDir = juce::File (processor.getDrumPluginPath()).getParentDirectory();
    if (! initialDir.isDirectory())
        initialDir = plugcat::userVst3Dir().isDirectory() ? plugcat::userVst3Dir()
                                                          : plugcat::systemVst3Dir();

    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose the drum VST3", initialDir, "*.vst3");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file.exists())
                                      processor.loadDrumPluginAsync (file);
                              });
}

void RigContent::openDrumVstWindow()
{
    auto* inst = processor.getDrumInstance();
    if (inst == nullptr || ! processor.hasDrumPlugin())
        return;

    if (drumVstWindow != nullptr)
    {
        drumVstWindow->toFront (true);
        return;
    }

    drumVstWindow = std::make_unique<ExtPluginWindow> (
        *inst, [safe = juce::Component::SafePointer<RigContent> (this)]
        {
            if (safe != nullptr)
                safe->closeDrumVstWindow();
        });
}

void RigContent::closeDrumVstWindow()
{
    drumVstWindow.reset();
}

void RigContent::chooseIrSource (int slot)
{
    // Cab entry point (TONE3000 design requirement): the CAB card's CHANGE
    // offers the Tone Store first (IRs live there too), then a local file.
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    menu.addSectionHeader (processor.getIrPath (slot).isNotEmpty() ? "Change IR" : "Load IR");
    menu.addItem (1, "Browse TONE3000 Tone Store\xe2\x80\xa6");
    menu.addItem (2, "Load IR file from disk\xe2\x80\xa6");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chainView->getIrButton (slot)),
        [safe = juce::Component::SafePointer<RigContent> (this), slot] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1)      safe->storeOverlay->open();
            else if (r == 2) safe->chooseIrFile (slot);
        });
}

void RigContent::chooseIrFile (int slot)
{
    auto initialDir = juce::File (processor.getIrPath (slot)).getParentDirectory();
    if (! initialDir.isDirectory())
        initialDir = Tone3000Client::irsDir();

    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose impulse response for CAB " + juce::String (slot + 1),
        initialDir, "*.wav;*.aif;*.aiff;*.flac");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this, slot] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file.existsAsFile())
                                      processor.loadIrAsync (slot, file);
                              });
}

void RigContent::saveCurrentPreset()
{
    const auto name = processor.getCurrentPresetName();
    if (name.isEmpty())
    {
        beginPresetNameEdit();
        return;
    }
    processor.savePreset (processor.getPresetsDirectory().getChildFile (name + ".xml"));
    saveFlashTicks = 27; // ~0.9 s of "Saved"
}

void RigContent::beginPresetNameEdit()
{
    presetNameEditor.setBounds (presetPill.getBounds());
    presetNameEditor.setText (processor.getCurrentPresetName(), juce::dontSendNotification);
    presetNameEditor.setVisible (true);
    presetNameEditor.toFront (true);
    presetNameEditor.grabKeyboardFocus();
    presetNameEditor.selectAll();
}

void RigContent::showPresetMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);
    const auto current = processor.getCurrentPresetName();
    const auto files = processor.getPresetFiles();

    menu.addItem (1000, juce::String (juce::CharPointer_UTF8 ("Save as new...")));
    if (! files.isEmpty())
        menu.addSeparator();

    for (int i = 0; i < files.size(); ++i)
    {
        const auto name = files.getReference (i).getFileNameWithoutExtension();
        menu.addItem (i + 1, name, true, name == current);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetPill),
                        [this, files] (int result)
                        {
                            if (result == 1000)
                                beginPresetNameEdit();
                            else if (result > 0 && result <= files.size())
                                processor.loadPreset (files.getReference (result - 1));
                        });
}

void RigContent::toggleTuner()
{
    tunerToggle.onClick();
}

bool RigContent::keyPressed (const juce::KeyPress& key)
{
    if (storeOverlay->isVisible())
        return false; // the overlay has its own shortcuts

    // vNext: ESC closes the effect drawer first
    if (key == juce::KeyPress::escapeKey && fxDrawer != nullptr && fxDrawer->isVisible())
    {
        fxDrawer->close();
        return true;
    }

    // vNext R2: chain-order undo/redo (Ctrl+Z / Ctrl+Y)
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        undoChainOrder();
        return true;
    }
    if (key == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0))
    {
        redoChainOrder();
        return true;
    }

    if (key == juce::KeyPress::spaceKey)
    {
        if (auto* p = processor.apvts.getParameter ("ampOn"))
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        return true;
    }
    if (key.getTextCharacter() == 't' || key.getTextCharacter() == 'T')
    {
        toggleTuner();
        return true;
    }
    if (key.getTextCharacter() == 'f' || key.getTextCharacter() == 'F')
    {
        setPerfMode (! perfMode);
        return true;
    }
    if (key.getTextCharacter() == 'l' || key.getTextCharacter() == 'L')   // light/dark theme
    {
        ui::applyTheme (! ui::lightTheme);
        lookAndFeel.applyColours();
        processor.apvts.state.setProperty ("uiTheme", ui::lightTheme ? "light" : "dark", nullptr);
        if (auto* top = getTopLevelComponent())
            top->repaint();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && perfMode)
    {
        setPerfMode (false);
        return true;
    }
    if (key == juce::KeyPress::leftKey)
    {
        processor.loadAdjacentPreset (-1);
        return true;
    }
    if (key == juce::KeyPress::rightKey)
    {
        processor.loadAdjacentPreset (1);
        return true;
    }
    return false;
}

void RigContent::mouseDown (const juce::MouseEvent& e)
{
    if (perfMode && e.y > 60 && e.y < getHeight() - 60)
    {
        const auto p = e.getPosition();
        auto toggleParam = [this] (const char* id)
        {
            if (auto* prm = processor.apvts.getParameter (id))
                prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
        };

        // vNext stage: tiles (amp/drive/delay - drums/rec/input)
        if (stageTiles[0].contains (p)) { toggleParam ("ampOn"); repaint(); return; }
        if (stageTiles[1].contains (p)) { toggleParam ("odOn"); repaint(); return; }
        if (stageTiles[2].contains (p)) { toggleParam ("delayOn"); repaint(); return; }
        if (stageTiles[3].contains (p))
        {
            processor.drumEngine.playing.store (! processor.drumEngine.playing.load());
            repaint();
            return;
        }
        if (stageTiles[4].contains (p)) { recChip.triggerClick(); repaint(); return; }
        if (stageTiles[5].contains (p)) { applyNextScene(); repaint(); return; }

        // vNext stage: footswitch action row
        if (stageActions[0].contains (p)) { processor.loadAdjacentPreset (-1); return; }
        if (stageActions[4].contains (p)) { applyNextScene(); repaint(); return; }
        if (stageActions[1].contains (p))
        {
            processor.drumEngine.playing.store (! processor.drumEngine.playing.load());
            repaint();
            return;
        }
        if (stageActions[2].contains (p))
        {
            // tap tempo: interval between consecutive taps -> BPM
            const auto now = juce::Time::currentTimeMillis();
            const auto dt = now - lastStageTapMs;
            lastStageTapMs = now;
            if (dt > 250 && dt < 2000)
                processor.drumEngine.bpm.store (
                    juce::jlimit (40.0f, 260.0f, 60000.0f / (float) dt));
            repaint();
            return;
        }
        if (stageActions[3].contains (p)) { tunerToggle.triggerClick(); repaint(); return; }

        // remaining area: sides navigate presets, center opens the menu
        if (e.x < getWidth() / 4)
            processor.loadAdjacentPreset (-1);
        else if (e.x > getWidth() * 3 / 4)
            processor.loadAdjacentPreset (1);
        else
            showPresetMenu();
        return;
    }

    grabKeyboardFocus(); // click in empty area returns focus to the shortcuts
}

//==============================================================================
GuitarRigNAMEditor::GuitarRigNAMEditor (GuitarRigNAMProcessor& p)
    : AudioProcessorEditor (p), content (p)
{
    addAndMakeVisible (content);
    content.setBounds (0, 0, RigContent::designWidth, RigContent::designHeight);

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) RigContent::designWidth
                                           / RigContent::designHeight);
    setResizeLimits (RigContent::designWidth / 2, RigContent::designHeight / 2,
                     RigContent::designWidth * 2, RigContent::designHeight * 2);

    double scale = 1.0;
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userArea;
        scale = juce::jmin (1.0,
                            (area.getWidth() - 60) / (double) RigContent::designWidth,
                            (area.getHeight() - 110) / (double) RigContent::designHeight);
    }
    setSize (juce::roundToInt (RigContent::designWidth * scale),
             juce::roundToInt (RigContent::designHeight * scale));
}

void GuitarRigNAMEditor::resized()
{
    const float scale = (float) getWidth() / (float) RigContent::designWidth;
    content.setTransform (juce::AffineTransform::scale (scale));
}
