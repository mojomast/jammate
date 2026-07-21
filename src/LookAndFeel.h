#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Paleta extraída do design GuitarRig.dc.html (claude.ai/design).
namespace ui
{
inline const juce::Colour bg { 0xff141517 };
inline const juce::Colour bgBorder { 0xff2a2c30 };
inline const juce::Colour topBarTop { 0xff2c2e33 };
inline const juce::Colour topBarBottom { 0xff232529 };
inline const juce::Colour chainTop { 0xff212327 };
inline const juce::Colour chainBottom { 0xff131416 };
inline const juce::Colour panel { 0xff1b1d20 };
inline const juce::Colour panelBorder { 0xff34373c };
inline const juce::Colour cardTop { 0xff2a2c31 };
inline const juce::Colour cardBottom { 0xff1e2023 };
inline const juce::Colour cardBorder { 0xff35383d };
inline const juce::Colour ampTop { 0xff342719 };
inline const juce::Colour ampMid { 0xff241c14 };
inline const juce::Colour ampBottom { 0xff1b1611 };
inline const juce::Colour ampBorder { 0xff5a4326 };
inline const juce::Colour accent { 0xffff9d2e };
inline const juce::Colour accentLight { 0xffffb14a };
inline const juce::Colour text { 0xffe5e6e8 };
inline const juce::Colour textBright { 0xfff0f1f2 };
inline const juce::Colour textDim { 0xff9a9da2 };
inline const juce::Colour textFaint { 0xff7f8288 };
inline const juce::Colour textMuted { 0xff6f7278 };
inline const juce::Colour green { 0xff37d67a };
inline const juce::Colour yellow { 0xffe2b53a };
inline const juce::Colour red { 0xffe0533a };
inline const juce::Colour meterBg { 0xff111214 };

inline juce::Font monoFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font uiFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions ("Segoe UI", size,
                                          bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace ui

// Knob conforme Knob.dc.html: corpo escuro com tampa interna, ponteiro laranja
// com brilho e ponto luminoso; curso de -135° a +135°.
class RigLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RigLookAndFeel()
    {
        setColour (juce::TextButton::buttonColourId, ui::panel);
        setColour (juce::TextButton::textColourOffId, ui::text);
        setColour (juce::ComboBox::outlineColourId, ui::panelBorder);
        setColour (juce::Label::textColourId, ui::text);
        setColour (juce::TooltipWindow::backgroundColourId, ui::panel);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override
    {
        const float d = (float) juce::jmin (width, height);
        const auto area = juce::Rectangle<float> ((float) x + ((float) width - d) * 0.5f,
                                                  (float) y, d, d);
        const auto centre = area.getCentre();

        // corpo externo
        {
            juce::ColourGradient grad (juce::Colour (0xff3c3f45),
                                       centre.x, area.getY() + d * 0.28f,
                                       juce::Colour (0xff1f2124),
                                       centre.x, area.getBottom(), true);
            g.setGradientFill (grad);
            g.fillEllipse (area);
            g.setColour (juce::Colour (0xff0f1012));
            g.drawEllipse (area, 1.0f);
        }

        // tampa interna (inset 15%)
        {
            const auto inner = area.reduced (d * 0.15f);
            juce::ColourGradient grad (juce::Colour (0xff4d5158),
                                       centre.x, inner.getY() + inner.getHeight() * 0.34f,
                                       juce::Colour (0xff292b30),
                                       centre.x, inner.getBottom(), true);
            g.setGradientFill (grad);
            g.fillEllipse (inner);
            g.setColour (juce::Colour (0xff15171a));
            g.drawEllipse (inner, 1.0f);
        }

        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const auto transform = juce::AffineTransform::rotation (angle, centre.x, centre.y);

        // ponteiro
        {
            juce::Path p;
            p.addRoundedRectangle (centre.x - 1.5f, area.getY() + d * 0.10f, 3.0f, d * 0.24f, 1.5f);
            p.applyTransform (transform);
            juce::ColourGradient grad (juce::Colour (0xffffd39a), centre.x, area.getY(),
                                       ui::accent, centre.x, area.getY() + d * 0.36f, false);
            g.setGradientFill (grad);
            g.fillPath (p);
        }

        // ponto luminoso com glow
        {
            auto dot = juce::Point<float> (centre.x, area.getY() + d * 0.095f)
                           .transformedBy (transform);
            g.setColour (ui::accent.withAlpha (0.35f));
            g.fillEllipse (dot.x - 6.0f, dot.y - 6.0f, 12.0f, 12.0f);
            g.setColour (ui::accentLight);
            g.fillEllipse (dot.x - 3.2f, dot.y - 3.2f, 6.4f, 6.4f);
        }
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour&, bool isHighlighted, bool isDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const bool accentButton = button.getProperties()["accent"];

        if (accentButton)
        {
            auto c = ui::accent;
            if (isDown) c = c.darker (0.15f);
            else if (isHighlighted) c = c.brighter (0.08f);
            g.setColour (button.isEnabled() ? c : c.withAlpha (0.35f));
            g.fillRoundedRectangle (bounds, 8.0f);
        }
        else
        {
            g.setColour (isDown ? ui::panel.brighter (0.08f) : ui::panel);
            g.fillRoundedRectangle (bounds, 8.0f);
            g.setColour (isHighlighted ? juce::Colour (0xff4a4d54) : ui::panelBorder);
            g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton& button, int) override
    {
        const bool accentButton = button.getProperties()["accent"];
        return ui::uiFont (accentButton ? 13.0f : 12.5f, true);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        const bool accentButton = button.getProperties()["accent"];
        g.setFont (getTextButtonFont (button, button.getHeight()));
        auto c = accentButton ? juce::Colour (0xff161719) : ui::text;
        if (! button.isEnabled())
            c = c.withAlpha (0.5f);
        g.setColour (c);
        g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
    }
};
