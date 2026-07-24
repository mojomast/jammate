#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Palette from the "modernist" PedalForge NAM redesign (claude.ai/design,
// tokens.json). Dark theme: teal accent (#57becb), amber warn (#e0a35c),
// square panels/slots, LED-ring knobs, JetBrains Mono telemetry.
namespace ui
{
// backgrounds / surfaces
inline const juce::Colour bg { 0xff0f1315 };
inline const juce::Colour bgTop { 0xff12181c };        // app-bg-2 (window radial top)
inline const juce::Colour chainTop { 0xff12181c };
inline const juce::Colour chainBottom { 0xff0c1013 };  // app-bg-1
inline const juce::Colour barTop { 0xff1d2327 };       // surface-2 (top bar / tuner bar)
inline const juce::Colour barBottom { 0xff161b1e };    // surface
inline const juce::Colour cardTop { 0xff1d2327 };      // surface-2
inline const juce::Colour cardBottom { 0xff161b1e };   // surface
inline const juce::Colour ampTop { 0xff262d32 };       // surface-3
inline const juce::Colour ampBottom { 0xff161b1e };
inline const juce::Colour meterBg { 0xff0c1013 };
// accent (cyan) / warn (amber)
inline const juce::Colour accent { 0xff57becb };
inline const juce::Colour accentBright { 0xff83d8e4 };
inline const juce::Colour accentDark { 0xff46a9b5 };   // accent-hover
inline const juce::Colour accentTextDark { 0xff08191c }; // on-accent
inline const juce::Colour glowOrange { 0xffe0a35c };   // warn (amber) — clip/record/caution
inline const juce::Colour warnBright { 0xfff0b878 };
// text (alpha over surfaces = muted/faint ramp)
inline const juce::Colour text { 0xffe9eef1 };
inline const juce::Colour textBright { 0xfff2f6f8 };
inline const juce::Colour textDim { 0x94e9eef1 };      // muted .58
inline const juce::Colour textFaint { 0x57e9eef1 };    // faint .34
inline const juce::Colour textMuted { 0x3de9eef1 };    // ~ .24
// semantic
inline const juce::Colour green { 0xff57becb };        // in-tune lock = accent
inline const juce::Colour yellow { 0xffe0a35c };       // caution = warn
inline const juce::Colour red { 0xffe0533a };

// translucent borders/fills (divider ramp of the design)
inline juce::Colour border()      { return juce::Colour (0xffe9eef1).withAlpha (0.12f); }
inline juce::Colour borderHover() { return juce::Colour (0xffe9eef1).withAlpha (0.24f); }
inline juce::Colour glass()       { return juce::Colour (0xffe9eef1).withAlpha (0.04f); }
inline juce::Colour glassHover()  { return juce::Colour (0xffe9eef1).withAlpha (0.07f); }

// Design typefaces embedded in the binary (OFL): Space Grotesk (UI) and
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

// Knob per Knob.dc.html (modernist redesign): a ring of 21 LED dots that light
// up to the value, over a dimensional metallic cap (radial gradient + specular)
// with an accent indicator notch. Sweep 270 deg from 225 deg (= JUCE rotary
// start pi*1.25 .. end pi*2.75).
class RigLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RigLookAndFeel()
    {
        setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1d2327));
        setColour (juce::TextButton::textColourOffId, ui::text);
        setColour (juce::ComboBox::outlineColourId, ui::border());
        setColour (juce::Label::textColourId, ui::text);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1d2327));
        setColour (juce::PopupMenu::textColourId, ui::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, ui::accent.withAlpha (0.20f));
        setColour (juce::PopupMenu::highlightedTextColourId, ui::textBright);
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff2e363c));
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff1d2327));
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

        // bezel (dark ring, radial gradient)
        {
            auto bz = centred (bezelD, bezelD, c);
            juce::ColourGradient grad (juce::Colour (0xff2a323a),
                                       bz.getX() + bezelD * 0.34f, bz.getY() + bezelD * 0.22f,
                                       juce::Colour (0xff0d1013), bz.getRight(), bz.getBottom(), true);
            g.setGradientFill (grad);
            g.fillEllipse (bz);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.drawEllipse (bz.reduced (0.5f), 1.0f);
        }
        // cap (dimensional) + specular
        {
            auto cp = centred (capD, capD, c);
            juce::ColourGradient grad (juce::Colour (0xff39424a),
                                       cp.getX() + capD * 0.38f, cp.getY() + capD * 0.26f,
                                       juce::Colour (0xff12171b), cp.getRight(), cp.getBottom(), true);
            g.setGradientFill (grad);
            g.fillEllipse (cp);
            auto sp = centred (capD * 0.68f, capD * 0.42f, { c.x, c.y - capD * 0.15f });
            juce::ColourGradient sg (juce::Colours::white.withAlpha (0.30f), sp.getCentreX(), sp.getY(),
                                     juce::Colours::transparentWhite, sp.getCentreX(), sp.getBottom(), false);
            g.setGradientFill (sg);
            g.fillEllipse (sp);
        }
        // LED ring (21 dots)
        {
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
        // indicator notch (rides the cap edge, accent + glow)
        {
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

        g.setColour (ui::text.withAlpha (0.14f));      // track bg (divider)
        g.fillRoundedRectangle (track, r);

        const float fillW = sliderPos - (float) x;
        if (fillW > 1.0f)
        {
            auto fill = track.withWidth (fillW);
            g.setColour (ui::accent.withAlpha (0.22f)); // glow
            g.fillRoundedRectangle (fill.expanded (0.0f, 2.0f), r + 2.0f);
            juce::ColourGradient grad (ui::accentDark, fill.getX(), 0.0f,
                                       ui::accent, fill.getRight(), 0.0f, false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (fill, r);
        }

        // round dimensional handle (mini knob cap)
        const float hd = juce::jlimit (14.0f, 22.0f, (float) height * 0.8f);
        auto handle = juce::Rectangle<float> (hd, hd).withCentre ({ sliderPos, cy });
        juce::ColourGradient hg (juce::Colour (0xff39424a),
                                 handle.getX() + hd * 0.38f, handle.getY() + hd * 0.26f,
                                 juce::Colour (0xff12171b), handle.getRight(), handle.getBottom(), true);
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
                g.setColour (ui::accent.withAlpha (0.15f));  // accent-tint
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
            g.fillRoundedRectangle (bounds.expanded (2.0f), rBtn + 2.0f); // glow
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
            return ui::uiFont (10.0f, true);   // Archivo is wider — fit the fixed chips
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
