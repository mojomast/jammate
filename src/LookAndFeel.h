#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Paleta do design GuitarRig.dc.html v2 (claude.ai/design) — tema escuro
// "glassy" com accent ciano.
namespace ui
{
inline const juce::Colour bg { 0xff0a0c0f };
inline const juce::Colour bgTop { 0xff14181d };        // topo do radial da janela
inline const juce::Colour chainTop { 0xff161b21 };
inline const juce::Colour chainBottom { 0xff0a0c0f };
inline const juce::Colour barTop { 0xff1c2127 };       // top bar / tuner bar
inline const juce::Colour barBottom { 0xff101317 };
inline const juce::Colour cardTop { 0xff1f242b };
inline const juce::Colour cardBottom { 0xff111419 };
inline const juce::Colour ampTop { 0xff222830 };
inline const juce::Colour ampBottom { 0xff101318 };
inline const juce::Colour accent { 0xff33c9d6 };
inline const juce::Colour accentDark { 0xff1c8f9a };
inline const juce::Colour accentTextDark { 0xff08211f };
inline const juce::Colour glowOrange { 0xffff963c };   // barra de brilho do amp
inline const juce::Colour text { 0xffeef2f6 };
inline const juce::Colour textBright { 0xfff4f7fa };
inline const juce::Colour textDim { 0xff99a1ab };
inline const juce::Colour textFaint { 0xff7c8590 };
inline const juce::Colour textMuted { 0xff616b76 };
inline const juce::Colour green { 0xff46e0a0 };
inline const juce::Colour yellow { 0xffe5c24a };
inline const juce::Colour red { 0xffe0533a };
inline const juce::Colour meterBg { 0xff0c0e11 };

// bordas/preenchimentos translúcidos do tema glassy
inline juce::Colour border()      { return juce::Colours::white.withAlpha (0.07f); }
inline juce::Colour borderHover() { return juce::Colours::white.withAlpha (0.18f); }
inline juce::Colour glass()       { return juce::Colours::white.withAlpha (0.03f); }
inline juce::Colour glassHover()  { return juce::Colours::white.withAlpha (0.06f); }

// Typefaces do design embutidos no binário (OFL): Space Grotesk (UI) e
// JetBrains Mono (valores/labels técnicos).
juce::Typeface::Ptr uiTypeface (bool bold);
juce::Typeface::Ptr monoTypeface (bool bold);

inline juce::Font monoFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (monoTypeface (bold)).withHeight (size));
}

inline juce::Font uiFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (uiTypeface (bold)).withHeight (size));
}
} // namespace ui

// Knob conforme Knob.dc.html v2: arco de valor (gauge) accent de -135° a
// +135°, tampa metálica interna e ponteiro accent com glow.
class RigLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RigLookAndFeel()
    {
        setColour (juce::TextButton::buttonColourId, juce::Colour (0xff14171b));
        setColour (juce::TextButton::textColourOffId, ui::text);
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff23272c));
        setColour (juce::Label::textColourId, ui::text);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff14181d));
        setColour (juce::PopupMenu::textColourId, ui::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, ui::accent.withAlpha (0.18f));
        setColour (juce::PopupMenu::highlightedTextColourId, ui::textBright);
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff2e343c));
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff14181d));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override
    {
        const float d = (float) juce::jmin (width, height);
        const auto area = juce::Rectangle<float> ((float) x + ((float) width - d) * 0.5f,
                                                  (float) y, d, d);
        const auto centre = area.getCentre();
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // gauge externo (270°)
        {
            const float stroke = juce::jmax (2.5f, d * 0.075f);
            const float r = d / 2.0f - stroke / 2.0f;

            juce::Path track;
            track.addCentredArc (centre.x, centre.y, r, r, 0.0f,
                                 rotaryStartAngle, rotaryEndAngle, true);
            g.setColour (juce::Colours::white.withAlpha (0.09f));
            g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

            if (sliderPos > 0.001f)
            {
                juce::Path value;
                value.addCentredArc (centre.x, centre.y, r, r, 0.0f,
                                     rotaryStartAngle, angle, true);
                g.setColour (ui::accent.withAlpha (0.3f));
                g.strokePath (value, juce::PathStrokeType (stroke + 3.0f,
                                                           juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
                g.setColour (ui::accent);
                g.strokePath (value, juce::PathStrokeType (stroke, juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
            }
        }

        // tampa interna
        {
            const auto inner = area.reduced (d * 0.15f);
            juce::ColourGradient grad (juce::Colour (0xff3d434c),
                                       centre.x, inner.getY() + inner.getHeight() * 0.28f,
                                       juce::Colour (0xff181b20),
                                       centre.x, inner.getBottom(), true);
            g.setGradientFill (grad);
            g.fillEllipse (inner);
            g.setColour (juce::Colour (0xff32383f));
            g.drawEllipse (inner, 1.0f);
        }

        // ponteiro
        {
            const auto transform = juce::AffineTransform::rotation (angle, centre.x, centre.y);
            juce::Path p;
            p.addRoundedRectangle (centre.x - 1.5f, area.getY() + d * 0.22f, 3.0f, d * 0.20f, 1.5f);
            p.applyTransform (transform);
            g.setColour (ui::accent.withAlpha (0.4f));
            g.strokePath (p, juce::PathStrokeType (3.0f));
            g.setColour (ui::accent);
            g.fillPath (p);
        }
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour&, bool isHighlighted, bool isDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto& props = button.getProperties();

        if (props["tab"])
        {
            if (props["tabActive"])
            {
                g.setColour (ui::accent);
                g.fillRect (bounds.removeFromBottom (2.0f));
            }
            return;
        }

        if (props["chip"])
        {
            const float r = bounds.getHeight() / 2.0f;
            if (props["chipActive"])
            {
                g.setColour (ui::accent);
                g.fillRoundedRectangle (bounds, r);
            }
            else
            {
                g.setColour (ui::glass());
                g.fillRoundedRectangle (bounds, r);
                g.setColour (isHighlighted ? ui::borderHover() : juce::Colours::white.withAlpha (0.1f));
                g.drawRoundedRectangle (bounds, r, 1.0f);
            }
            return;
        }

        if (props["outlineAccent"])
        {
            if (isHighlighted || isDown)
            {
                g.setColour (ui::accent);
                g.fillRoundedRectangle (bounds, 8.0f);
            }
            else
            {
                g.setColour (ui::accent.withAlpha (0.08f));
                g.fillRoundedRectangle (bounds, 8.0f);
                g.setColour (ui::accent.withAlpha (0.5f));
                g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
            }
            return;
        }

        if (props["accent"])
        {
            auto c = ui::accent;
            if (isDown) c = c.darker (0.15f);
            else if (isHighlighted) c = c.brighter (0.1f);
            if (! button.isEnabled()) c = c.withAlpha (0.35f);

            g.setColour (c.withAlpha (0.35f));
            g.fillRoundedRectangle (bounds.expanded (2.0f), 11.0f); // glow
            g.setColour (c);
            g.fillRoundedRectangle (bounds, 9.0f);
        }
        else
        {
            g.setColour (isDown ? ui::glassHover() : ui::glass());
            g.fillRoundedRectangle (bounds, 9.0f);
            g.setColour (isHighlighted ? ui::borderHover() : juce::Colours::white.withAlpha (0.08f));
            g.drawRoundedRectangle (bounds, 9.0f, 1.0f);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton& button, int) override
    {
        const auto& props = button.getProperties();
        if (props["tab"])
            return ui::uiFont (13.0f, true);
        if (props["chip"])
            return ui::uiFont (12.0f);
        return ui::uiFont (props["accent"] ? 13.0f : 12.5f, true);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool isHighlighted, bool isDown) override
    {
        const auto& props = button.getProperties();
        g.setFont (getTextButtonFont (button, button.getHeight()));

        juce::Colour c;
        if (props["tab"])
            c = props["tabActive"] ? ui::textBright : ui::textFaint;
        else if (props["chip"])
            c = props["chipActive"] ? ui::accentTextDark : juce::Colour (0xffb4bbc4);
        else if (props["outlineAccent"])
            c = (isHighlighted || isDown) ? ui::accentTextDark : ui::accent;
        else if (props["accent"])
            c = ui::accentTextDark;
        else
            c = ui::text;

        if (! button.isEnabled())
            c = c.withAlpha (0.5f);
        g.setColour (c);
        g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
    }
};
