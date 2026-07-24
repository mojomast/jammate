#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Palette from the "modernist" PedalForge NAM redesign (claude.ai/design,
// tokens.json). Runtime-switchable dark/light theme: teal accent, amber warn,
// square panels/slots, LED-ring knobs, JetBrains Mono telemetry.
namespace ui
{
inline bool lightTheme = false;

// backgrounds / surfaces
inline juce::Colour bg { 0xff0f1315 };
inline juce::Colour bgTop { 0xff12181c };
inline juce::Colour chainTop { 0xff12181c };
inline juce::Colour chainBottom { 0xff0c1013 };
inline juce::Colour barTop { 0xff1d2327 };
inline juce::Colour barBottom { 0xff161b1e };
inline juce::Colour cardTop { 0xff1d2327 };
inline juce::Colour cardBottom { 0xff161b1e };
inline juce::Colour ampTop { 0xff262d32 };
inline juce::Colour ampBottom { 0xff161b1e };
inline juce::Colour meterBg { 0xff0c1013 };
// accent (cyan) / warn (amber)
inline juce::Colour accent { 0xff57becb };
inline juce::Colour accentBright { 0xff83d8e4 };
inline juce::Colour accentDark { 0xff46a9b5 };
inline juce::Colour accentTextDark { 0xff08191c };   // on-accent
inline juce::Colour glowOrange { 0xffe0a35c };       // warn (amber)
inline juce::Colour warnBright { 0xfff0b878 };
// text
inline juce::Colour text { 0xffe9eef1 };
inline juce::Colour textBright { 0xfff2f6f8 };
inline juce::Colour textDim { 0x94e9eef1 };
inline juce::Colour textFaint { 0x57e9eef1 };
inline juce::Colour textMuted { 0x3de9eef1 };
// semantic
inline juce::Colour green { 0xff57becb };
inline juce::Colour yellow { 0xffe0a35c };
inline juce::Colour red { 0xffe0533a };
// knob (dimensional cap + bezel + specular)
inline juce::Colour knobCapHi { 0xff39424a };
inline juce::Colour knobCapLo { 0xff12171b };
inline juce::Colour knobBezelHi { 0xff2a323a };
inline juce::Colour knobBezelLo { 0xff0d1013 };
inline juce::Colour knobEdge { 0x1fffffff };          // rgba(255,255,255,.12)
inline juce::Colour knobSpec { 0x4dffffff };          // rgba(255,255,255,.30)

// translucent borders/fills (divider ramp — theme aware)
inline juce::Colour dividerBase() { return juce::Colour (lightTheme ? 0xff172023 : 0xffe9eef1); }
inline juce::Colour border()      { return dividerBase().withAlpha (lightTheme ? 0.16f : 0.12f); }
inline juce::Colour borderHover() { return dividerBase().withAlpha (lightTheme ? 0.42f : 0.24f); }
inline juce::Colour glass()       { return dividerBase().withAlpha (0.04f); }
inline juce::Colour glassHover()  { return dividerBase().withAlpha (0.07f); }

/// Reassign the whole palette to the dark or light token set. Call from the
/// editor and then re-apply the LookAndFeel colours + repaint.
inline void applyTheme (bool light)
{
    lightTheme = light;
    auto C = [] (juce::uint32 v) { return juce::Colour (v); };
    if (! light)
    {
        bg = C(0xff0f1315);   bgTop = C(0xff12181c);   chainTop = C(0xff12181c);   chainBottom = C(0xff0c1013);
        barTop = C(0xff1d2327); barBottom = C(0xff161b1e); cardTop = C(0xff1d2327); cardBottom = C(0xff161b1e);
        ampTop = C(0xff262d32); ampBottom = C(0xff161b1e); meterBg = C(0xff0c1013);
        accent = C(0xff57becb); accentBright = C(0xff83d8e4); accentDark = C(0xff46a9b5); accentTextDark = C(0xff08191c);
        glowOrange = C(0xffe0a35c); warnBright = C(0xfff0b878);
        text = C(0xffe9eef1); textBright = C(0xfff2f6f8);
        textDim = C(0x94e9eef1); textFaint = C(0x57e9eef1); textMuted = C(0x3de9eef1);
        green = C(0xff57becb); yellow = C(0xffe0a35c); red = C(0xffe0533a);
        knobCapHi = C(0xff39424a); knobCapLo = C(0xff12171b); knobBezelHi = C(0xff2a323a); knobBezelLo = C(0xff0d1013);
        knobEdge = C(0x1fffffff); knobSpec = C(0x4dffffff);
    }
    else
    {
        bg = C(0xffeef1f2);   bgTop = C(0xfff5f7f8);   chainTop = C(0xffe8ecee);   chainBottom = C(0xffe8ecee);
        barTop = C(0xffffffff); barBottom = C(0xffe4e8ea); cardTop = C(0xffffffff); cardBottom = C(0xffe4e8ea);
        ampTop = C(0xffffffff); ampBottom = C(0xffdbe0e2); meterBg = C(0xffdbe0e2);
        accent = C(0xff2f9aa8); accentBright = C(0xff2f9aa8); accentDark = C(0xff268490); accentTextDark = C(0xffffffff);
        glowOrange = C(0xffb7772e); warnBright = C(0xffa56a26);
        text = C(0xff172023); textBright = C(0xff0e1518);
        textDim = C(0x9e172023); textFaint = C(0x66172023); textMuted = C(0x40172023);
        green = C(0xff2f9aa8); yellow = C(0xffb7772e); red = C(0xffc0442d);
        knobCapHi = C(0xffffffff); knobCapLo = C(0xffd0d7db); knobBezelHi = C(0xfff2f5f6); knobBezelLo = C(0xffc2cace);
        knobEdge = C(0x1a000000); knobSpec = C(0xd9ffffff);
    }
}

// Design typefaces embedded in the binary (OFL): Archivo (UI/headings) and
// JetBrains Mono (technical values/labels).
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

// Knob per Knob.dc.html (modernist redesign): a ring of 21 LED dots lighting up
// to the value, over a dimensional metallic cap (radial gradient + specular)
// with an accent indicator notch. Sweep 270 deg from 225 deg.
class RigLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RigLookAndFeel() { applyColours(); }

    /// (Re)apply theme-dependent JUCE colour IDs. Call after ui::applyTheme.
    void applyColours()
    {
        setColour (juce::TextButton::buttonColourId, ui::cardTop);
        setColour (juce::TextButton::textColourOffId, ui::text);
        setColour (juce::ComboBox::outlineColourId, ui::border());
        setColour (juce::Label::textColourId, ui::text);
        setColour (juce::PopupMenu::backgroundColourId, ui::cardTop);
        setColour (juce::PopupMenu::textColourId, ui::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, ui::accent.withAlpha (0.20f));
        setColour (juce::PopupMenu::highlightedTextColourId, ui::textBright);
        setColour (juce::ScrollBar::thumbColourId, ui::dividerBase().withAlpha (0.28f));
        setColour (juce::TooltipWindow::backgroundColourId, ui::cardTop);
        setColour (juce::TooltipWindow::textColourId, ui::text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& s) override
    {
        const float D = (float) juce::jmin (width, height);
        const auto area = juce::Rectangle<float> ((float) x + ((float) width - D) * 0.5f,
                                                  (float) y, D, D);
        const auto c = area.getCentre();
        const float capD = D * 0.58f, bezelD = D * 0.80f, ringR = D * 0.44f;
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const bool active = s.isMouseOverOrDragging();
        const juce::Colour lit = active ? ui::accentBright : ui::accent;
        const juce::Colour off = ui::text.withAlpha (0.24f);

        auto centred = [] (float w, float h, juce::Point<float> ctr)
        { return juce::Rectangle<float> (w, h).withCentre (ctr); };

        {   // bezel
            auto bz = centred (bezelD, bezelD, c);
            juce::ColourGradient grad (ui::knobBezelHi,
                                       bz.getX() + bezelD * 0.34f, bz.getY() + bezelD * 0.22f,
                                       ui::knobBezelLo, bz.getRight(), bz.getBottom(), true);
            g.setGradientFill (grad);
            g.fillEllipse (bz);
            g.setColour (ui::knobEdge);
            g.drawEllipse (bz.reduced (0.5f), 1.0f);
        }
        {   // cap + specular
            auto cp = centred (capD, capD, c);
            juce::ColourGradient grad (ui::knobCapHi,
                                       cp.getX() + capD * 0.38f, cp.getY() + capD * 0.26f,
                                       ui::knobCapLo, cp.getRight(), cp.getBottom(), true);
            g.setGradientFill (grad);
            g.fillEllipse (cp);
            auto sp = centred (capD * 0.68f, capD * 0.42f, { c.x, c.y - capD * 0.15f });
            juce::ColourGradient sg (ui::knobSpec, sp.getCentreX(), sp.getY(),
                                     juce::Colours::transparentWhite, sp.getCentreX(), sp.getBottom(), false);
            g.setGradientFill (sg);
            g.fillEllipse (sp);
        }
        {   // LED ring (21 dots)
            const int N = 21;
            for (int i = 0; i < N; ++i)
            {
                const float frac = (float) i / (float) (N - 1);
                const float t = rotaryStartAngle + (rotaryEndAngle - rotaryStartAngle) * frac;
                const float px = c.x + ringR * std::sin (t);
                const float py = c.y - ringR * std::cos (t);
                const bool on = frac <= sliderPos + 1.0e-4f;
                const float sz = on ? D * 0.052f : D * 0.032f;
                auto dot = centred (sz, sz, { px, py });
                if (on)
                {
                    g.setColour (lit.withAlpha (0.35f));
                    g.fillEllipse (dot.expanded (D * 0.035f));
                }
                g.setColour (on ? lit : off);
                g.fillEllipse (dot);
            }
        }
        {   // indicator notch (rides the cap edge)
            auto at = [&] (float r) { return juce::Point<float> (c.x + r * std::sin (angle),
                                                                 c.y - r * std::cos (angle)); };
            const auto p1 = at (capD * 0.16f), p2 = at (capD * 0.52f);
            g.setColour (lit.withAlpha (0.35f));
            g.drawLine ({ p1, p2 }, D * 0.10f);
            g.setColour (lit);
            g.drawLine ({ p1, p2 }, juce::jmax (2.0f, D * 0.05f));
        }
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float, float,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                                    0.0f, 0.0f, style, s);
            return;
        }

        const float trackH = juce::jmin (9.0f, (float) height * 0.5f);
        const float cy = (float) y + (float) height * 0.5f;
        const float r = trackH * 0.5f;
        juce::Rectangle<float> track ((float) x, cy - r, (float) width, trackH);

        g.setColour (ui::text.withAlpha (0.14f));
        g.fillRoundedRectangle (track, r);

        const float fillW = sliderPos - (float) x;
        if (fillW > 1.0f)
        {
            auto fill = track.withWidth (fillW);
            g.setColour (ui::accent.withAlpha (0.22f));
            g.fillRoundedRectangle (fill.expanded (0.0f, 2.0f), r + 2.0f);
            juce::ColourGradient grad (ui::accentDark, fill.getX(), 0.0f,
                                       ui::accent, fill.getRight(), 0.0f, false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (fill, r);
        }

        const float hd = juce::jlimit (14.0f, 22.0f, (float) height * 0.8f);
        auto handle = juce::Rectangle<float> (hd, hd).withCentre ({ sliderPos, cy });
        juce::ColourGradient hg (ui::knobCapHi,
                                 handle.getX() + hd * 0.38f, handle.getY() + hd * 0.26f,
                                 ui::knobCapLo, handle.getRight(), handle.getBottom(), true);
        g.setGradientFill (hg);
        g.fillEllipse (handle);
        const bool act = s.isMouseOverOrDragging();
        g.setColour (act ? ui::accentBright : ui::accent.withAlpha (0.55f));
        g.drawEllipse (handle.reduced (0.6f), act ? 1.8f : 1.4f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour&, bool isHighlighted, bool isDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto& props = button.getProperties();
        constexpr float rBtn = 5.0f;

        if (props["tab"])
        {
            if (props["tabActive"])
            {
                g.setColour (ui::accent);
                g.fillRect (bounds.removeFromBottom (2.0f));
            }
            return;
        }

        if (props["ghost"])   // clean UI: no box at rest, subtle glass on hover
        {
            if (isDown || isHighlighted)
            {
                g.setColour (isDown ? ui::glassHover() : ui::glass());
                g.fillRoundedRectangle (bounds, rBtn);
            }
            return;
        }

        if (props["chip"])   // square toggle: solid accent on / ruled ghost off
        {
            if ((bool) props["chipActive"] || button.getToggleState())
            {
                g.setColour (ui::accent);
                g.fillRect (bounds);
            }
            else
            {
                g.setColour (isHighlighted ? ui::glassHover() : ui::glass());
                g.fillRect (bounds);
                g.setColour (isHighlighted ? ui::borderHover() : ui::border());
                g.drawRect (bounds, 1.0f);
            }
            return;
        }

        if (props["outlineAccent"])
        {
            if (isHighlighted || isDown)
            {
                g.setColour (ui::accent);
                g.fillRoundedRectangle (bounds, rBtn);
            }
            else
            {
                g.setColour (ui::accent.withAlpha (0.15f));
                g.fillRoundedRectangle (bounds, rBtn);
                g.setColour (ui::accent.withAlpha (0.55f));
                g.drawRoundedRectangle (bounds, rBtn, 1.0f);
            }
            return;
        }

        if (props["accent"])   // primary: solid accent + subtle glow
        {
            auto c = ui::accent;
            if (isDown) c = ui::accentDark;
            else if (isHighlighted) c = ui::accentBright;
            if (! button.isEnabled()) c = c.withAlpha (0.35f);

            g.setColour (c.withAlpha (0.28f));
            g.fillRoundedRectangle (bounds.expanded (2.0f), rBtn + 2.0f);
            g.setColour (c);
            g.fillRoundedRectangle (bounds, rBtn);
        }
        else   // secondary: ruled ghost
        {
            g.setColour (isDown ? ui::glassHover() : ui::glass());
            g.fillRoundedRectangle (bounds, rBtn);
            g.setColour (isHighlighted ? ui::borderHover() : ui::border());
            g.drawRoundedRectangle (bounds, rBtn, 1.0f);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton& button, int) override
    {
        const auto& props = button.getProperties();
        if (props["tab"])
            return ui::uiFont (12.0f, true);
        if (props["chip"])
            return ui::uiFont (10.0f, true);
        return ui::uiFont (props["accent"] ? 12.0f : 11.5f, true);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool isHighlighted, bool isDown) override
    {
        const auto& props = button.getProperties();
        g.setFont (getTextButtonFont (button, button.getHeight()));

        juce::Colour c;
        if (props["tab"])
            c = props["tabActive"] ? ui::textBright : ui::textFaint;
        else if (props["ghost"])
        {
            // active state (chipActive) tints the label: red for "hot" (REC),
            // accent otherwise; idle is dim text that brightens on hover
            const bool act = (bool) props["chipActive"] || button.getToggleState();
            c = act ? (props["ghostHot"] ? ui::red : ui::accent)
                    : (isHighlighted || isDown ? ui::text : ui::textDim);
        }
        else if (props["chip"])
            c = ((bool) props["chipActive"] || button.getToggleState())
                    ? ui::accentTextDark : ui::textDim;
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
