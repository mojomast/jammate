#include "DrumOverlay.h"

#include "LookAndFeel.h"
#include "PluginEditor.h"   // KnobComponent (guitar ribbon)

#include <BinaryData.h>

//==============================================================================
// Fixed layout inside the editor's 1100x700 content (v4: the staff is the track).
// Clean UI: the TRANSPORT band sits on top (mirroring the guitar top bar) and
// the guitar ribbon lives right below it, in the exact band where the drum
// ribbon sits on the guitar screen - "open guitar"/"open drums" swap in place.
namespace
{
constexpr int headerBandH = 60;              // transport band (== guitar top bar)
constexpr int gtrRibY = 62, gtrRibH = 160;   // larger controls with uniform hit targets
constexpr int margin = 26;
constexpr int headerY = 13, headerH = 34;
constexpr int tabsY = 230, tabsH = 26;
// clean UI: the bar options overlay the TOP of the staff area (no extra row)
constexpr int scoreY = 260, scoreH = 152;
constexpr int barHeadsY = scoreY + 2, barHeadsH = 26;
constexpr int libY = 420;                                 // top of the browser/grid
constexpr int gridY = 424, gridH = 216;                   // grid in place of the lib
constexpr int sourceY = 648, sourceH = 32;
// column browser: Genre | Grooves/Fills | Preview
constexpr int colGap = 8, genreColW = 150, listColW = 208, colRowH = 26;

// clean UI: small floating card holding the 3 humanize sliders (popover)
class HumanizePanel : public juce::Component
{
public:
    HumanizePanel (juce::Slider& v, juce::Slider& t, juce::Slider& r)
        : vel (v), tim (t), rr (r)
    {
        addAndMakeVisible (vel);
        addAndMakeVisible (tim);
        addAndMakeVisible (rr);
    }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (ui::cardTop);
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (ui::borderHover());
        g.drawRoundedRectangle (b, 6.0f, 1.0f);
        g.setFont (ui::monoFont (7.5f));
        g.setColour (ui::textFaint);
        g.drawText ("VELOCITY",    10, 12, 76, 12, juce::Justification::centredLeft);
        g.drawText ("TIMING",      10, 42, 76, 12, juce::Justification::centredLeft);
        g.drawText ("ROUND-ROBIN", 10, 72, 76, 12, juce::Justification::centredLeft);
    }
    void resized() override
    {
        vel.setBounds (88, 8, getWidth() - 98, 22);
        tim.setBounds (88, 38, getWidth() - 98, 22);
        rr.setBounds (88, 68, getWidth() - 98, 22);
    }
private:
    juce::Slider &vel, &tim, &rr;
};

// bar role (1..5) - label (UI) and key (generator)
juce::String roleLabel (int r)
{
    switch (r)
    {
        case 2: return "Chorus";
        case 3: return "Bridge";
        case 4: return "Breakdown";
        case 5: return "Fill";
        default: return "Verse";
    }
}
const char* roleKey (int r)
{
    switch (r) { case 2: return "chorus"; case 3: return "bridge";
                 case 4: return "breakdown"; case 5: return "fill"; default: return "verse"; }
}

// staff geometry: 4 bars x 16 steps across the usable width (~1048)
// 4 bars must fit in ~1038 usable px: 64*stepW + 12*beatPad +
// 3*barPad + scoreLeft + slack <= width, otherwise the 4th bar clips at the end
constexpr int scoreLeft = 64;
constexpr float stepW = 13.2f, beatPad = 5.0f, barPad = 20.0f;
constexpr float staffSP = 5.20f;
constexpr float staffTop = 65.0f;

float staffY (float pos) { return staffTop + 8.0f * staffSP - pos * staffSP; }

float timeSignatureWidth (int num, int den)
{
    return num >= 10 || den >= 10 ? 54.0f : 40.0f;
}

// MuseScore's Leland face is a SMuFL font.  Keeping the notes as semantic
// music glyphs (instead of hand-drawn ellipses and x marks) gives the main
// editor, ribbon and library preview one consistent engraving language while
// the existing DrumEngine remains responsible for timing and playback.
constexpr juce::juce_wchar smuflPercussionClef = 0xE069;
constexpr juce::juce_wchar smuflNoteheadBlack = 0xE0A4;
constexpr juce::juce_wchar smuflNoteheadXBlack = 0xE0A9;
constexpr juce::juce_wchar smuflNoteheadParenthesisLeft = 0xE0F5;
constexpr juce::juce_wchar smuflNoteheadParenthesisRight = 0xE0F6;
constexpr juce::juce_wchar smuflAccentAbove = 0xE4A0;
constexpr juce::juce_wchar smuflAccentBelow = 0xE4A1;
constexpr juce::juce_wchar smuflRestWhole = 0xE4E3;
constexpr juce::juce_wchar smuflTimeSig0 = 0xE080;

// Smoked Ivory: a low-glare notation palette that belongs to the dark UI
// without sacrificing the contrast expected from a professional score.
const juce::Colour notationFrame (0xff182125);
const juce::Colour notationPaperTop (0xffefeee8);
const juce::Colour notationPaperBottom (0xffe5e6e1);
const juce::Colour notationPaperEdge (0xff778185);
const juce::Colour notationInk (0xff172023);
const juce::Colour notationStaff (0xff657074);
const juce::Colour notationDim (0xff697477);
const juce::Colour notationCyan (0xff168f9d);
const juce::Colour notationAmber (0xffb8732f);

void fillNotationPaper (juce::Graphics& g, juce::Rectangle<float> area,
                        float radius, float shadowAlpha)
{
    g.setColour (juce::Colours::black.withAlpha (shadowAlpha));
    g.fillRoundedRectangle (area.translated (0.0f, 1.5f), radius);

    juce::ColourGradient paperGradient (notationPaperTop,
                                        area.getX(), area.getY(),
                                        notationPaperBottom,
                                        area.getX(), area.getBottom(), false);
    g.setGradientFill (paperGradient);
    g.fillRoundedRectangle (area, radius);
    g.setColour (notationPaperEdge.withAlpha (0.82f));
    g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
}

juce::Typeface::Ptr lelandTypeface()
{
    static juce::Typeface::Ptr face = juce::Typeface::createSystemTypefaceFor (
        BinaryData::Leland_otf, BinaryData::Leland_otfSize);
    return face;
}

juce::Font musicFont (float height)
{
    return juce::Font (juce::FontOptions (lelandTypeface()).withHeight (height));
}

juce::String musicNumber (int value)
{
    const auto digits = juce::String (juce::jmax (0, value));
    juce::String result;
    for (int i = 0; i < digits.length(); ++i)
    {
        const int digit = (int) digits[i] - (int) '0';
        if (juce::isPositiveAndBelow (digit, 10))
            result += juce::String::charToString ((juce::juce_wchar) (smuflTimeSig0 + digit));
    }
    return result;
}

void drawMusicTextCentred (juce::Graphics& g, const juce::String& text,
                           juce::Point<float> centre, float height,
                           juce::Colour colour)
{
    if (text.isEmpty())
        return;

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (musicFont (height), text, 0.0f, 0.0f);
    const auto bounds = glyphs.getBoundingBox (0, glyphs.getNumGlyphs(), false);
    glyphs.moveRangeOfGlyphs (0, -1, centre.x - bounds.getCentreX(),
                              centre.y - bounds.getCentreY());
    g.setColour (colour);
    glyphs.draw (g);
}

void drawMusicGlyph (juce::Graphics& g, juce::juce_wchar glyph,
                     juce::Point<float> centre, float height, juce::Colour colour)
{
    drawMusicTextCentred (g, juce::String::charToString (glyph), centre, height, colour);
}

void drawMusicTimeSignature (juce::Graphics& g, float x, float space,
                             const std::function<float(float)>& yOf,
                             int num, int den, juce::Colour colour)
{
    // Leland deliberately has very tall line metrics. A 32-half-space JUCE
    // font height maps its SMuFL digit outline to the expected two staff
    // spaces occupied by each half of a time signature.
    drawMusicTextCentred (g, musicNumber (num), { x, yOf (6.0f) }, space * 32.0f, colour);
    drawMusicTextCentred (g, musicNumber (den), { x, yOf (2.0f) }, space * 32.0f, colour);
}

// number of steps + beam grouping of a meter (compound meters in threes)
void meterGroups (int num, int den, int& steps, int groups[8], int& nGroups)
{
    steps = drum::stepsForMeter (num, den);
    nGroups = 0;
    if (den == 8 && num % 3 == 0)
        for (int i = 0; i < num / 3 && nGroups < 8; ++i) groups[nGroups++] = 6;
    else if (den == 8 && num == 7) { int g[] = { 4,4,6 }; for (int x : g) groups[nGroups++] = x; }
    else if (den == 8 && num == 5) { int g[] = { 4,6 };   for (int x : g) groups[nGroups++] = x; }
    else if (den == 4)
        for (int i = 0; i < num && nGroups < 8; ++i) groups[nGroups++] = 4;
    else if (den == 2)
        for (int i = 0; i < num && nGroups < 8; ++i) groups[nGroups++] = 8;
    else
    {
        int rem = steps;
        while (rem >= 4 && nGroups < 8) { groups[nGroups++] = 4; rem -= 4; }
        if (rem > 0 && nGroups < 8) groups[nGroups++] = rem;
    }
    if (nGroups == 0) { groups[0] = steps; nGroups = 1; }
}

// staff position / x-head / hand-or-foot per voice - drum::Voice indices
constexpr float staffPos[drum::numVoices] = { 1, 5, 9, -1, 8, 10, 7, 6, 3 };
constexpr bool staffXHead[drum::numVoices] = { false, false, true, true, true, true,
                                               false, false, false };
constexpr bool staffIsHand[drum::numVoices] = { false, true, true, false, true, true,
                                                true, true, true };

// Draws one engraved bar for both the drum ribbon and the library preview.
// Notes/time signatures are Leland SMuFL glyphs; stems, beams and staff lines
// remain vectors so the score stays crisp at every UI scale.
void drawMiniBar (juce::Graphics& g, juce::Rectangle<float> area,
                  const juce::uint8 pat[drum::numVoices][drum::maxStepsPerBar],
                  int num = 4, int den = 4, bool showClef = true,
                  bool showMeter = true, bool drawPaper = true, int playStep = -1)
{
    if (area.getWidth() < 30.0f || area.getHeight() < 24.0f)
        return;

    int steps = 16, groups[8], nGroups = 1;
    meterGroups (num, den, steps, groups, nGroups);
    int gStart[8] = {};
    for (int i = 1; i < nGroups; ++i)
        gStart[i] = gStart[i - 1] + groups[i - 1];

    if (drawPaper)
        fillNotationPaper (g, area, 3.5f, 0.22f);

    auto content = area.reduced (drawPaper ? 7.0f : 0.0f,
                                 drawPaper ? 5.0f : 3.0f);
    const float sp = juce::jlimit (1.8f, 6.6f, (content.getHeight() - 4.0f) / 18.0f);
    const float centreY = content.getCentreY();
    auto yOf = [&] (float pos) { return centreY + (4.0f - pos) * sp; };

    const float staffL = content.getX() + 1.0f;
    const float staffR = content.getRight() - 1.0f;
    float cursor = staffL;
    if (showClef)
        cursor += 2.8f * sp;
    const float meterReserve = (num >= 10 || den >= 10 ? 7.5f : 4.4f) * sp;
    if (showMeter)
        cursor += meterReserve;
    const float x0 = cursor + 0.35f * sp;
    const float sw = juce::jmax (0.8f, (staffR - x0) / (float) steps);
    auto xOf = [&] (int s) { return x0 + (s + 0.5f) * sw; };
    const float headHalfW = sp * 0.90f;
    const float lineW = juce::jmax (0.65f, sp * 0.12f);
    const float beamH = juce::jmax (0.9f, sp * 0.34f);

    // 5 staff lines (positions 0,2,4,6,8)
    g.setColour (notationStaff.withAlpha (0.86f));
    for (int i = 0; i <= 4; ++i)
        g.drawLine (staffL, yOf ((float) (i * 2)), staffR,
                    yOf ((float) (i * 2)), lineW);

    float symbolX = staffL;
    if (showClef)
    {
        drawMusicGlyph (g, smuflPercussionClef,
                        { symbolX + 1.3f * sp, yOf (4.0f) },
                        32.0f * sp, notationInk);
        symbolX += 2.8f * sp;
    }
    if (showMeter)
    {
        drawMusicTimeSignature (g, symbolX + meterReserve * 0.5f, sp, yOf,
                                num, den, notationInk);
    }

    // beat separators (meter group boundaries)
    g.setColour (notationInk.withAlpha (0.07f));
    for (int gi = 1; gi < nGroups; ++gi)
        g.drawLine (x0 + gStart[gi] * sw, yOf (9.0f),
                    x0 + gStart[gi] * sw, yOf (-2.0f), lineW);

    if (playStep >= 0)
    {
        const float px = xOf (juce::jlimit (0, steps - 1, playStep));
        g.setColour (ui::accent.withAlpha (0.10f));
        g.fillRect (px - sw * 0.45f, yOf (12.5f), sw * 0.9f,
                    yOf (-4.5f) - yOf (12.5f));
        g.setColour (ui::accent);
        g.fillRect (px - 0.7f, yOf (12.5f), 1.4f, yOf (-4.5f) - yOf (12.5f));
    }

    auto drawHead = [&] (float x, float y, bool cross, int val)
    {
        const auto colour = val == 3 ? notationDim.withAlpha (0.72f) : notationInk;
        drawMusicGlyph (g, cross ? smuflNoteheadXBlack : smuflNoteheadBlack,
                        { x, y }, 22.0f * sp, colour);
        if (val == 3)
        {
            drawMusicGlyph (g, smuflNoteheadParenthesisLeft,
                            { x - 1.12f * sp, y }, 24.0f * sp,
                            notationDim.withAlpha (0.82f));
            drawMusicGlyph (g, smuflNoteheadParenthesisRight,
                            { x + 1.12f * sp, y }, 24.0f * sp,
                            notationDim.withAlpha (0.82f));
        }
    };

    const float beamYH = yOf (12.0f), beamYF = yOf (-4.0f);
    bool anyNote = false;
    for (int v = 0; v < drum::numVoices && ! anyNote; ++v)
        for (int s = 0; s < steps; ++s)
            if (pat[v][s] != 0) { anyNote = true; break; }

    if (! anyNote)
    {
        drawMusicGlyph (g, smuflRestWhole,
                        { (x0 + staffR) * 0.5f, yOf (4.0f) },
                        30.0f * sp, notationDim);
    }

    for (int beat = 0; beat < nGroups; ++beat)
        for (int limb = 0; limb < 2; ++limb)
        {
            const bool up = (limb == 0);
            const int gLen = groups[beat];
            struct Col { int s; float noteY; bool accent; };
            Col cols[8];
            int nc = 0;
            for (int i = 0; i < gLen; ++i)
            {
                const int s = gStart[beat] + i;
                float ext = up ? -1.0e9f : 1.0e9f;
                bool any = false, accent = false;
                for (int v = 0; v < drum::numVoices; ++v)
                {
                    if (staffIsHand[v] != up)
                        continue;
                    const int val = pat[v][s];
                    if (val == 0)
                        continue;
                    any = true;
                    accent = accent || val == 2;
                    const float y = yOf (staffPos[v]);
                    drawHead (xOf (s), y, staffXHead[v], val);
                    ext = up ? juce::jmax (ext, y) : juce::jmin (ext, y);
                }
                if (any)
                    cols[nc++] = { s, ext, accent };
            }
            if (nc == 0)
                continue;

            const float beamY = up ? beamYH : beamYF;
            auto stemX = [&] (int s) { return up ? xOf (s) + headHalfW
                                                  : xOf (s) - headHalfW; };
            g.setColour (notationInk);
            for (int c = 0; c < nc; ++c)
            {
                g.setColour (notationInk);
                g.drawLine (stemX (cols[c].s), cols[c].noteY + (up ? -1.5f : 1.5f),
                            stemX (cols[c].s), beamY, lineW);
                if (cols[c].accent)
                    drawMusicGlyph (g, up ? smuflAccentAbove : smuflAccentBelow,
                                    { xOf (cols[c].s), beamY + (up ? -1.45f : 1.45f) * sp },
                                    28.0f * sp, ui::glowOrange);
            }
            g.setColour (notationInk);

            if (nc > 1)
            {
                const float primaryY = up ? beamY : beamY - beamH;
                g.fillRect (stemX (cols[0].s), primaryY,
                            stemX (cols[nc - 1].s) - stemX (cols[0].s), beamH);
                const float secondaryY = primaryY + (up ? 1.65f : -1.65f) * beamH;
                for (int c = 0; c < nc - 1; ++c)
                    if (cols[c + 1].s - cols[c].s == 1)
                        g.fillRect (stemX (cols[c].s), secondaryY,
                                    stemX (cols[c + 1].s) - stemX (cols[c].s), beamH);

                // Isolated sixteenths inside a beamed group receive a short
                // secondary beam, rather than looking like eighth notes.
                for (int c = 0; c < nc; ++c)
                {
                    const bool joinsLeft = c > 0 && cols[c].s - cols[c - 1].s == 1;
                    const bool joinsRight = c + 1 < nc && cols[c + 1].s - cols[c].s == 1;
                    if (! joinsLeft && ! joinsRight)
                    {
                        const float len = sp * (c == nc - 1 ? -1.1f : 1.1f);
                        g.fillRect (juce::jmin (stemX (cols[c].s), stemX (cols[c].s) + len),
                                    secondaryY, std::abs (len), beamH);
                    }
                }
            }
            else
            {
                const float x = stemX (cols[0].s);
                for (int flagIndex = 0; flagIndex < 2; ++flagIndex)
                {
                    const float dir = up ? 1.0f : -1.0f;
                    const float fy = beamY + dir * flagIndex * sp * 0.72f;
                    juce::Path flag;
                    flag.startNewSubPath (x, fy);
                    flag.quadraticTo (x + dir * sp, fy + dir * sp * 0.55f,
                                      x + dir * sp * 0.45f, fy + dir * sp * 1.6f);
                    g.strokePath (flag, juce::PathStrokeType (lineW));
                }
            }
        }

    g.setColour (notationInk.withAlpha (0.9f));
    g.drawLine (staffR, yOf (8.0f), staffR, yOf (0.0f), lineW);
}

// optional grid
constexpr int gridLabelW = 96;
constexpr int gCellMaxW = 48, gCellGap = 3, gBeatGap = 10;
constexpr int gRowH = 20, gRowGap = 1;
} // namespace

const int DrumOverlay::gridRowVoice[DrumOverlay::gridRows] = {
    drum::crash, drum::hat, drum::ride, drum::tom1, drum::tom2,
    drum::snare, drum::floorTom, drum::kick, drum::hatPedal
};

//==============================================================================
// DrumRibbon - drum strip at the top of the guitar screen
DrumRibbon::DrumRibbon (DrumEngine& e) : engine (e)
{
    playBtn.setMouseClickGrabsKeyboardFocus (false);
    playBtn.getProperties().set ("accent", true);
    playBtn.setButtonText (juce::CharPointer_UTF8 ("\xe2\x96\xb6"));
    playBtn.onClick = [this] { engine.playing.store (! engine.playing.load()); repaint(); };
    addAndMakeVisible (playBtn);

    chevBtn.setMouseClickGrabsKeyboardFocus (false);
    chevBtn.getProperties().set ("ghost", true);
    chevBtn.setButtonText (juce::CharPointer_UTF8 ("\xe2\x8c\x83"));
    chevBtn.setTooltip ("Collapse/expand the drum strip");
    chevBtn.onClick = [this]
    {
        setMinimal (! minimal);
        if (onToggleMin)
            onToggleMin (minimal);
    };
    addAndMakeVisible (chevBtn);

    startTimerHz (15);
}

void DrumRibbon::setMinimal (bool m)
{
    minimal = m;
    chevBtn.setButtonText (juce::CharPointer_UTF8 (minimal ? "\xe2\x8c\x84" : "\xe2\x8c\x83"));
    resized();
    repaint();
}

void DrumRibbon::resized()
{
    if (minimal)
        playBtn.setBounds (8, (getHeight() - 20) / 2, 28, 20);
    else
        playBtn.setBounds (8, (getHeight() - 34) / 2, 40, 34);
    chevBtn.setBounds (getWidth() - 128, (getHeight() - 22) / 2, 22, 22);
}

void DrumRibbon::timerCallback()
{
    const bool playing = engine.playing.load();
    sectionShown = playing ? juce::jlimit (0, drum::maxSections - 1,
                                           engine.uiBar.load() / drum::barsPerSection)
                           : 0;
    playBtn.setButtonText (playing ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9d\x9a\xe2\x9d\x9a"))
                                   : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6")));
    repaint();
}

void DrumRibbon::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (ui::cardBottom);
    g.fillRoundedRectangle (b, 2.0f);
    g.setColour (ui::accentDark.withAlpha (0.45f));
    g.drawRoundedRectangle (b, 2.0f, 1.0f);

    const bool playing = engine.playing.load();
    const int base = sectionShown * drum::barsPerSection;

    if (minimal)
    {
        // collapsed: one thin line - play, name/state, BPM, open hint
        g.setColour (ui::textBright);
        g.setFont (ui::uiFont (10.5f, true));
        g.drawText ("DRUMS", 44, 0, 60, getHeight(), juce::Justification::centredLeft);
        g.setColour (ui::textFaint);
        g.setFont (ui::uiFont (8.5f));
        g.drawText (playing ? juce::String (juce::CharPointer_UTF8 ("playing"))
                            : juce::String ("stopped"),
                    104, 0, 60, getHeight(), juce::Justification::centredLeft);
        g.setColour (ui::textBright);
        g.setFont (ui::monoFont (10.5f, true));
        g.drawText (juce::String ((int) engine.bpm.load()) + " BPM",
                    168, 0, 70, getHeight(), juce::Justification::centredLeft);
        g.setColour (ui::textFaint);
        g.setFont (ui::uiFont (10.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("open drums \xe2\xa4\xa2")),
                    getWidth() - 104, 0, 98, getHeight(), juce::Justification::centredRight);
        return;
    }

    g.setColour (ui::textBright);
    g.setFont (ui::uiFont (12.0f, true));
    g.drawText ("DRUMS", 58, 7, 130, 14, juce::Justification::centredLeft);
    g.setColour (ui::textFaint);
    g.setFont (ui::uiFont (9.0f));
    g.drawText (playing ? juce::String (juce::CharPointer_UTF8 ("playing \xc2\xb7 follow along"))
                        : juce::String ("stopped"),
                58, 22, 150, 12, juce::Justification::centredLeft);

    g.setColour (ui::textBright);
    g.setFont (ui::monoFont (15.0f, true));
    g.drawText (juce::String ((int) engine.bpm.load()), 196, 6, 46, 18, juce::Justification::centred);
    g.setColour (ui::textFaint);
    g.setFont (ui::uiFont (8.0f, true));
    g.drawText ("BPM", 196, 25, 46, 10, juce::Justification::centred);

    const float staffX = 258.0f, staffR = (float) getWidth() - 108.0f;
    const float bw = (staffR - staffX) / (float) drum::barsPerSection;
    const int ub = engine.uiBar.load();

    const juce::Rectangle<float> paper (staffX - 4.0f, 3.0f,
                                        staffR - staffX + 4.0f,
                                        (float) getHeight() - 6.0f);
    fillNotationPaper (g, paper, 3.5f, 0.24f);

    for (int i = 0; i < drum::barsPerSection; ++i)
    {
        const int bar = base + i;
        juce::uint8 pat[drum::numVoices][drum::maxStepsPerBar];
        for (int v = 0; v < drum::numVoices; ++v)
            for (int s = 0; s < drum::maxStepsPerBar; ++s)
                pat[v][s] = engine.pattern[bar][v][s].load();
        const bool meterChanged = i == 0
                               || engine.meterNum (bar) != engine.meterNum (bar - 1)
                               || engine.meterDen (bar) != engine.meterDen (bar - 1);
        const int activeStep = playing && ub == bar ? engine.uiStep.load() : -1;
        drawMiniBar (g, { staffX + i * bw, 5.0f, bw, (float) getHeight() - 10.0f },
                     pat, engine.meterNum (bar), engine.meterDen (bar),
                     i == 0, meterChanged, false, activeStep);
    }

    g.setColour (ui::textFaint);
    g.setFont (ui::uiFont (10.0f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("open drums \xe2\xa4\xa2")),
                getWidth() - 104, 0, 98, getHeight(), juce::Justification::centredRight);
}

void DrumRibbon::mouseUp (const juce::MouseEvent&)
{
    if (onOpen) onOpen();
}

//==============================================================================
DrumOverlay::DrumOverlay (GuitarRigNAMProcessor& p)
    : processor (p), engine (p.drumEngine)
{
    setWantsKeyboardFocus (true);

    closeButton.onClick = [this] { closeAnimated(); };
    addAndMakeVisible (closeButton);

    playButton.getProperties().set ("accent", true);
    playButton.onClick = [this]
    {
        engine.playing.store (! engine.playing.load());
        syncTransportUi();
    };
    addAndMakeVisible (playButton);

    bpmDown.onClick = [this]
    {
        engine.bpm.store (juce::jlimit (40.0f, 260.0f, engine.bpm.load() - 2.0f));
        repaint();
    };
    bpmUp.onClick = [this]
    {
        engine.bpm.store (juce::jlimit (40.0f, 260.0f, engine.bpm.load() + 2.0f));
        repaint();
    };
    addAndMakeVisible (bpmDown);
    addAndMakeVisible (bpmUp);

    swingSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    swingSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    swingSlider.setRange (0.0, 60.0, 1.0);
    swingSlider.onValueChange = [this]
    {
        engine.swingPct.store ((float) swingSlider.getValue());
        repaint();
    };
    addAndMakeVisible (swingSlider);

    levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    levelSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    levelSlider.setRange (0.0, 1.5, 0.01);
    levelSlider.onValueChange = [this]
    { engine.level.store ((float) levelSlider.getValue()); };
    addAndMakeVisible (levelSlider);

    for (auto* c : { &metroChip, &followChip, &gridChip, &genChip, &editChip, &saveChip, &humChip })
    {
        c->getProperties().set ("chip", true);
        c->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*c);
    }
    editChip.getProperties().set ("chipActive", editMode);
    editChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "EDIT: clicking the staff edits the notes. "
        "ASSEMBLE (off): drag the whole bar to reposition/copy it")));
    editChip.onClick = [this]
    {
        editMode = ! editMode;
        editChip.getProperties().set ("chipActive", editMode);
        editChip.repaint();
        scoreView.setMouseCursor (editMode ? juce::MouseCursor::NormalCursor
                                           : juce::MouseCursor::DraggingHandCursor);
        scoreView.repaint();
    };
    // vNext: DAW sync toggle - in the VST3 the drums follow the host BPM
    syncChip.getProperties().set ("chip", true);
    syncChip.setMouseClickGrabsKeyboardFocus (false);
    syncChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Follow the DAW tempo (VST3). The standalone keeps its own BPM.")));
    syncChip.onClick = [this]
    {
        processor.drumHostSync.store (! processor.drumHostSync.load());
        syncTransportUi();
    };
    addAndMakeVisible (syncChip);

    // clean UI: click & count-in live in the metronome menu (one chip)
    metroChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Metronome: click track and 1-bar count-in")));
    metroChip.onClick = [this]
    {
        juce::PopupMenu m;
        m.setLookAndFeel (&getLookAndFeel());
        m.addItem (1, "Click", true, engine.clickOn.load());
        m.addItem (2, "Count-in (1 bar)", true, engine.countInOn.load());
        auto* self = this; // MSVC: 'this' in a nested lambda init-capture resolves wrong
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&metroChip),
            [safe = juce::Component::SafePointer<DrumOverlay> (self)] (int r)
            {
                if (safe == nullptr) return;
                if (r == 1) safe->engine.clickOn.store (! safe->engine.clickOn.load());
                else if (r == 2) safe->engine.countInOn.store (! safe->engine.countInOn.load());
                if (r > 0)
                {
                    safe->metroChip.getProperties().set ("chipActive",
                        safe->engine.clickOn.load() || safe->engine.countInOn.load());
                    safe->metroChip.repaint();
                }
            });
    };
    followChip.getProperties().set ("chipActive", true);
    followChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "The staff turns the page by itself when the music enters the "
        "next section")));
    followChip.onClick = [this]
    {
        followOn = ! followOn;
        followChip.getProperties().set ("chipActive", followOn);
        followChip.repaint();
    };
    gridChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "16-step grid of the selected bar (in place of the library)")));
    gridChip.onClick = [this]
    {
        gridOn = ! gridOn;
        if (gridOn) { genOn = false; genChip.getProperties().set ("chipActive", false); genChip.repaint(); }
        gridChip.getProperties().set ("chipActive", gridOn);
        gridChip.repaint();
        refreshAll();
    };
    genChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Groove generator: genre/style/drummer + parameters; "
        "fills 1 or all 4 bars (in place of the library)")));
    genChip.onClick = [this]
    {
        genOn = ! genOn;
        if (genOn) { gridOn = false; gridChip.getProperties().set ("chipActive", false); gridChip.repaint(); }
        genChip.getProperties().set ("chipActive", genOn);
        genChip.repaint();
        refreshAll();
    };
    setupGenerator();
    setupGuitarRibbon();

    // column browser: GROOVES / FILLS tabs (middle column)
    for (auto* c : { &tabGrooves, &tabViradas })
    {
        c->getProperties().set ("chip", true);
        c->setMouseClickGrabsKeyboardFocus (false);
        addChildComponent (*c);
    }
    tabGrooves.onClick = [this] { currentKind = 1; rebuildList(); };
    tabViradas.onClick = [this] { currentKind = 2; rebuildList(); };
    saveChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Save the selected bar as a reusable groove in \"My bars\" - then drag "
        "it onto any bar (Documents\\PedalForge NAM\\compassos)")));
    saveChip.onClick = [this] { promptSaveBar(); };

    addSectionBtn.onClick = [this]
    {
        const int n = engine.numSections.load();
        if (n >= drum::maxSections)
            return;
        engine.numSections.store (n + 1);
        curSection = n;
        selBar = 0;
        refreshAll();
    };
    addAndMakeVisible (addSectionBtn);

    delSectionBtn.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Removes the shown section (bars shift back one slot)")));
    delSectionBtn.onClick = [this]
    {
        const int n = engine.numSections.load();
        if (n <= 1)
            return;
        // pulls the bars of the following sections back by one section
        for (int g = curSection * drum::barsPerSection;
             g < (n - 1) * drum::barsPerSection; ++g)
        {
            const int src = g + drum::barsPerSection;
            engine.setMeter (g, engine.meterNum (src), engine.meterDen (src));
            engine.barFromString (engine.barToString (src), g); // "" clears; honors the meter
            engine.barNames[g] = engine.barNames[src];
            engine.barRole[g] = engine.barRole[src];
        }
        for (int g = (n - 1) * drum::barsPerSection; g < n * drum::barsPerSection; ++g)
        {
            engine.clearBar (g);
            engine.barNames[g].clear();
            engine.barRole[g] = 0;
        }
        engine.numSections.store (n - 1);
        curSection = juce::jmin (curSection, n - 2);
        selBar = 0;
        refreshAll();
    };
    addAndMakeVisible (delSectionBtn);

    addAndMakeVisible (scoreView);

    // browser columns
    genreVp.setViewedComponent (&genreContent, false);
    genreVp.setScrollBarsShown (true, false);
    genreVp.setScrollBarThickness (7);
    addChildComponent (genreVp);
    listVp.setViewedComponent (&listContent, false);
    listVp.setScrollBarsShown (true, false);
    listVp.setScrollBarThickness (7);
    addChildComponent (listVp);
    addChildComponent (previewPane);

    applyBtn.getProperties().set ("outlineAccent", true);
    applyBtn.setMouseClickGrabsKeyboardFocus (false);
    applyBtn.onClick = [this]
    {
        if (selValid)
            applyGrooveToBar (selDragId, selectedBar());
    };
    addChildComponent (applyBtn);

    // humanize (internal kit): velocity, micro-timing, round-robin
    struct HS { juce::Slider* s; std::atomic<float>* p; };
    for (auto hs : { HS { &humVelSlider, &engine.humanVel },
                     HS { &humTimeSlider, &engine.humanTime },
                     HS { &humRRSlider, &engine.humanRR } })
    {
        hs.s->setSliderStyle (juce::Slider::LinearHorizontal);
        hs.s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        hs.s->setRange (0.0, 1.0, 0.01);
        hs.s->setValue (hs.p->load(), juce::dontSendNotification);
        hs.s->setColour (juce::Slider::trackColourId, ui::glowOrange.withAlpha (0.7f));
        auto* p = hs.p;
        auto* sl = hs.s;
        hs.s->onValueChange = [p, sl] { p->store ((float) sl->getValue()); };
        hs.s->setMouseClickGrabsKeyboardFocus (false);
        addChildComponent (*hs.s);
    }

    // (naming is now a one-shot popup from SAVE BAR; no inline editor)

    addChildComponent (gridView);

    // clean UI: the whole sound-source row collapses into ONE kit chip whose
    // menu holds source toggle / panel / load / remove
    kitChip.getProperties().set ("chip", true);
    kitChip.setMouseClickGrabsKeyboardFocus (false);
    kitChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Drum sound source: internal kit or a hosted VST3 (panel, load, remove)")));
    kitChip.onClick = [this]
    {
        const bool hasVst = processor.hasDrumPlugin();
        const bool vstOn = engine.useVst.load() && hasVst;
        juce::PopupMenu m;
        m.setLookAndFeel (&getLookAndFeel());
        m.addItem (1, "Source: VST3 plugin", hasVst, vstOn);
        m.addItem (2, "Source: internal kit", true, ! vstOn);
        m.addSeparator();
        m.addItem (3, "Open plugin panel", hasVst);
        m.addItem (4, juce::String (juce::CharPointer_UTF8 ("Load drum VST3\xe2\x80\xa6")));
        m.addItem (5, "Remove plugin", hasVst);
        auto* self = this; // MSVC: 'this' in a nested lambda init-capture resolves wrong
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&kitChip),
            [safe = juce::Component::SafePointer<DrumOverlay> (self)] (int r)
            {
                if (safe == nullptr) return;
                switch (r)
                {
                    case 1: safe->engine.useVst.store (true); break;
                    case 2: safe->engine.useVst.store (false); break;
                    case 3: if (safe->onOpenVstPanel) safe->onOpenVstPanel(); break;
                    case 4: if (safe->onChooseVst) safe->onChooseVst(); break;
                    case 5: safe->processor.clearDrumPlugin(); break;
                    default: break;
                }
                safe->refreshSourceRow();
            });
    };
    addAndMakeVisible (kitChip);

    // humanize: popover panel above the chip (sliders keep their bindings)
    humPanel = std::make_unique<HumanizePanel> (humVelSlider, humTimeSlider, humRRSlider);
    addChildComponent (*humPanel);
    humChip.setTooltip ("Velocity / timing / round-robin humanization");
    humChip.onClick = [this]
    {
        const bool show = ! humPanel->isVisible();
        humPanel->setVisible (show);
        humChip.getProperties().set ("chipActive", show);
        humChip.repaint();
        if (show)
            humPanel->toFront (false);
    };

    // notation legend: the permanent hint line became this "?" popover
    helpChip.getProperties().set ("chip", true);
    helpChip.setMouseClickGrabsKeyboardFocus (false);
    helpChip.setTooltip ("Notation & staff shortcuts");
    helpChip.onClick = [this]
    {
        juce::PopupMenu m;
        m.setLookAndFeel (&getLookAndFeel());
        m.addSectionHeader ("NOTATION");
        m.addItem (100, juce::String (juce::CharPointer_UTF8 ("\xc3\x97 cymbals \xc2\xb7 heads = drums")), false);
        m.addItem (101, juce::String (juce::CharPointer_UTF8 ("> accent \xc2\xb7 ( ) ghost note")), false);
        m.addSectionHeader ("STAFF");
        m.addItem (102, "drag a groove onto a bar", false);
        m.addItem (103, "click the staff to edit (EDIT on)", false);
        m.addItem (104, "time signature: click it on the staff", false);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&helpChip), nullptr);
    };
    addAndMakeVisible (helpChip);

    for (auto* b : std::initializer_list<juce::Button*> {
             &closeButton, &playButton, &bpmDown, &bpmUp,
             &addSectionBtn, &delSectionBtn, &kitChip, &helpChip })
        b->setMouseClickGrabsKeyboardFocus (false);

    // first time (empty timeline): builds a demo section with the default groove
    bool empty = true;
    for (int b = 0; b < drum::maxBars && empty; ++b)
        empty = ! engine.barUsed[b].load();
    if (empty && drum::library().size() > 1)
    {
        juce::uint8 pat[drum::numVoices][drum::maxStepsPerBar];
        const auto& g0 = drum::library()[0];
        drum::parseSpec (g0, pat);
        for (int b = 0; b < 3; ++b)
        {
            engine.setMeter (b, g0.num, g0.den);
            engine.setBarPattern (pat, b);
            engine.barNames[b] = juce::String (juce::CharPointer_UTF8 (g0.name));
        }
    }

    refreshAll();
    refreshSourceRow();
    syncTransportUi();
    startTimerHz (30);
}

DrumOverlay::~DrumOverlay() = default;

void DrumOverlay::open()
{
    syncTransportUi();
    refreshSourceRow();
    buildGuitarRibbon();   // mirror the current guitar chain
    refreshAll();
    morphT = 0.0f; morphTarget = 1.0f; morphing = true;  // grows from the top strip
    applyMorph();
    setVisible (true);
    toFront (true);
}

void DrumOverlay::closeAnimated()
{
    morphTarget = 0.0f; morphing = true;   // shrinks back to the strip
}

void DrumOverlay::applyMorph()
{
    const float W = (float) getWidth(), H = (float) getHeight();
    if (W < 1.0f || H < 1.0f) return;
    // rectangle of the drum strip on the guitar screen (== DrumRibbon)
    const float rx = 18.0f, ry = 62.0f, rw = W - 36.0f, rh = (float) ribbonSourceH;
    auto L = [] (float a, float b, float t) { return a + (b - a) * t; };
    const float sx = L (rw / W, 1.0f, morphT), sy = L (rh / H, 1.0f, morphT);
    const float tx = L (rx, 0.0f, morphT),     ty = L (ry, 0.0f, morphT);
    setTransform (juce::AffineTransform::scale (sx, sy).translated (tx, ty));
    setAlpha (L (0.25f, 1.0f, morphT));
}

void DrumOverlay::timerCallback()
{
    if (! isVisible())
        return;

    if (morphing)
    {
        morphT += (morphTarget - morphT) * 0.30f;
        if (std::abs (morphT - morphTarget) < 0.012f)
        {
            morphT = morphTarget;
            morphing = false;
            if (morphTarget < 0.5f)   // finished closing
            {
                setTransform ({});
                setAlpha (1.0f);
                setVisible (false);
                return;
            }
            setTransform ({});
            setAlpha (1.0f);
        }
        else
            applyMorph();
    }

    const int uiBar = engine.uiBar.load();
    if (uiBar != lastUiBar || (uiBar >= 0 && engine.uiStep.load() >= 0))
    {
        if (uiBar != lastUiBar)
        {
            lastUiBar = uiBar;
            // FOLLOW: turns the page when the music enters another section
            if (followOn && uiBar >= 0)
            {
                const int sec = uiBar / drum::barsPerSection;
                if (sec != curSection)
                {
                    curSection = sec;
                    selBar = uiBar % drum::barsPerSection;
                    refreshAll();
                }
            }
            rebuildSectionTabs();
        }
        scoreView.repaint();
        if (gridOn)
            gridView.repaint();
        repaint (getWidth() - 220, headerY, 200, headerH); // position on the label
    }

    const bool playing = engine.playing.load();
    const auto want = playing ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0 STOP"))
                              : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6 PLAY"));
    if (playButton.getButtonText() != want)
        playButton.setButtonText (want);

    const int nSec = engine.numSections.load();
    if (nSec != lastNumSections)
    {
        lastNumSections = nSec;
        curSection = juce::jmin (curSection, nSec - 1);
        refreshAll();
        syncTransportUi();
    }

    const bool hasVst = processor.hasDrumPlugin();
    if (hasVst != lastHasVst)
    {
        lastHasVst = hasVst;
        refreshSourceRow();
    }
}

void DrumOverlay::syncTransportUi()
{
    swingSlider.setValue (engine.swingPct.load(), juce::dontSendNotification);
    levelSlider.setValue (engine.level.load(), juce::dontSendNotification);
    metroChip.getProperties().set ("chipActive",
                                   engine.clickOn.load() || engine.countInOn.load());
    syncChip.getProperties().set ("chipActive", processor.drumHostSync.load());
    syncChip.repaint();
    playButton.setButtonText (engine.playing.load()
                                  ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0 STOP"))
                                  : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6 PLAY")));
    repaint();
}

//==============================================================================
void DrumOverlay::paint (juce::Graphics& g)
{
    g.fillAll (ui::bg);

    // ---- transport band on TOP (mirrors the guitar top bar) ----
    {
        g.setGradientFill ({ ui::barTop, 0.0f, 0.0f, ui::barBottom, 0.0f, (float) headerBandH, false });
        g.fillRect (0, 0, getWidth(), headerBandH);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (0, headerBandH, getWidth(), 1);
    }

    // ---- guitar ribbon (band below, aligned with the guitar screen's drum ribbon) ----
    {
        juce::Rectangle<float> rib (0.0f, (float) gtrRibY, (float) getWidth(), (float) gtrRibH);
        g.setColour (ui::cardBottom);
        g.fillRect (rib);
        g.setColour (ui::accentDark.withAlpha (0.35f));
        g.drawLine (0.0f, (float) (gtrRibY + gtrRibH), (float) getWidth(),
                    (float) (gtrRibY + gtrRibH), 1.0f);

        g.setColour (ui::textBright);
        g.setFont (ui::uiFont (12.0f, true));
        g.drawText ("GUITAR", margin, gtrRibY + 10, 84, 14, juce::Justification::centredLeft);
        g.setColour (ui::textFaint);
        g.setFont (ui::uiFont (8.5f));
        g.drawText ("signal chain", margin, gtrRibY + 26, 84, 12, juce::Justification::centredLeft);

        auto drawPower = [&g] (float cx, float cy, bool on)
        {
            g.setColour (on ? ui::green : ui::textFaint);
            g.drawEllipse (cx - 3.5f, cy - 2.5f, 7.0f, 7.0f, 1.2f);
            g.drawLine (cx, cy - 5.0f, cx, cy + 0.5f, 1.4f);
        };

        // Functional miniature of the real chain: every group is a card and
        // its status lamp reflects the actual bypass parameter.
        for (const auto& grp : gtrGroups)
        {
            const bool amp = grp.rigLane >= 0;
            bool enabled = true;
            if (auto* p = processor.apvts.getRawParameterValue (grp.onParam); p != nullptr)
                enabled = p->load() > 0.5f;

            if (amp)
            {
                const int rigs = processor.getRigCount();
                const int cardH = rigs == 1 ? 140 : (rigs == 2 ? 76 : 50);
                const int totalH = rigs * cardH + 4 * juce::jmax (0, rigs - 1);
                const int y = gtrRibY + (gtrRibH - totalH) / 2
                              + grp.rigLane * (cardH + 4);
                const int ampW = juce::roundToInt (grp.w * 0.68f);
                const juce::Rectangle<int> ampCard (grp.x, y, ampW, cardH);
                const juce::Rectangle<int> irCard (grp.x + ampW + 3, y,
                                                   grp.w - ampW - 3, cardH);
                for (auto card : { ampCard, irCard })
                {
                    g.setColour (ui::cardTop); g.fillRoundedRectangle (card.toFloat(), 3.0f);
                    g.setColour (juce::Colour (0xffe0b072).withAlpha (0.5f));
                    g.drawRoundedRectangle (card.toFloat(), 3.0f, 1.0f);
                }
                drawPower (ampCard.getX() + 7.0f, ampCard.getY() + 7.0f, enabled);
                const bool cabOn = processor.apvts.getRawParameterValue ("cabOn")->load() > 0.5f;
                drawPower (irCard.getX() + 7.0f, irCard.getY() + 7.0f, cabOn);
                g.setColour (juce::Colour (0xffe0b072));
                g.setFont (ui::monoFont (6.5f, true));
                g.drawText ("AMP" + juce::String (grp.rigLane + 1),
                            ampCard.getX() + 13, ampCard.getY() + 2,
                            ampCard.getWidth() - 17, 10,
                            juce::Justification::centredLeft);
                g.drawText ("IR" + juce::String (grp.rigLane + 1),
                            irCard.getX() + 13, irCard.getY() + 2,
                            irCard.getWidth() - 17, 10,
                            juce::Justification::centredLeft);
            }
            else
            {
                juce::Rectangle<float> card ((float) grp.x, (float) gtrRibY + 10.0f,
                                             (float) grp.w, (float) gtrRibH - 20.0f);
                g.setColour (ui::cardTop); g.fillRoundedRectangle (card, 4.0f);
                g.setColour ((enabled ? ui::accent : ui::textFaint).withAlpha (0.55f));
                g.drawRoundedRectangle (card, 4.0f, 1.0f);
                drawPower (card.getRight() - 7.0f, card.getY() + 7.0f, enabled);
                g.setColour (enabled ? ui::accent : ui::textMuted);
                g.setFont (ui::monoFont (7.0f, true));
                g.drawText (grp.name, card.getX() + 3.0f, card.getY() + 2.0f,
                            card.getWidth() - 14.0f, 9.0f, juce::Justification::centredLeft);
            }
        }
        for (size_t i = 1; i < gtrGroups.size(); ++i)
        {
            const auto& a = gtrGroups[i - 1];
            const auto& b = gtrGroups[i];
            if (a.rigLane >= 0 && b.rigLane >= 0) continue;
            const float x1 = (float) a.x + a.w + 3.0f, x2 = (float) b.x - 3.0f;
            if (x2 > x1)
            {
                g.setColour (ui::textMuted);
                g.drawLine (x1, gtrRibY + gtrRibH * 0.57f,
                            x2, gtrRibY + gtrRibH * 0.57f, 1.2f);
            }
        }
    }

    auto area = getLocalBounds().reduced (margin, 0);

    g.setColour (ui::green);
    g.fillEllipse ((float) area.getX(), headerY + 13.0f, 8.0f, 8.0f);
    g.setFont (ui::uiFont (15.0f, true));
    g.setColour (ui::textBright);
    g.drawText ("DRUMS", area.getX() + 16, headerY, 100, headerH,
                juce::Justification::centredLeft);

    // BPM + transport labels
    g.setFont (ui::monoFont (16.0f, true));
    g.setColour (ui::textBright);
    g.drawText (juce::String ((int) engine.bpm.load()),
                area.getX() + 244, headerY, 50, headerH, juce::Justification::centred);
    g.setFont (ui::uiFont (9.0f, true));
    g.setColour (ui::textMuted);
    g.drawText ("BPM", area.getX() + 244, headerY - 6, 50, 10, juce::Justification::centred);
    g.drawText ("SWING " + juce::String ((int) engine.swingPct.load()) + "%",
                area.getX() + 362, headerY - 6, 110, 10, juce::Justification::centredLeft);

    // position (playing bar)
    {
        const int uiBar = engine.uiBar.load();
        g.setFont (ui::monoFont (10.0f));
        g.setColour (ui::textFaint);
        const auto txt = uiBar >= 0
                             ? "bar " + juce::String (uiBar + 1) + "/"
                                   + juce::String (engine.totalBars())
                             : juce::String (engine.totalBars()) + " bars";
        g.drawText (txt, getWidth() - margin - 44 - 180, headerY, 170, headerH,
                    juce::Justification::centredRight);
    }

    g.setFont (ui::uiFont (9.0f, true));
    g.setColour (ui::textMuted);
    g.drawText ("VOLUME", getWidth() - margin - 184, sourceY + 3, 50, 26,
                juce::Justification::centredRight);

    // humanize labels
    // (humanize labels now live inside the HumanizePanel popover)

    // generator labels
    if (genOn)
    {
        auto title = [&] (const juce::String& t, int x)
        {
            g.setFont (ui::uiFont (9.0f, true));
            g.setColour (ui::textFaint);
            g.drawText (t, x, libY + 4, 220, 12, juce::Justification::centredLeft);
        };
        title ("GENERATOR", genGenreBox.getX());

        auto fld = [&] (const juce::String& t, juce::Component& c)
        {
            g.setFont (ui::monoFont (7.5f));
            g.setColour (ui::textMuted);
            g.drawText (t, c.getX(), c.getY() - 11, c.getWidth(), 9, juce::Justification::centredLeft);
        };
        fld ("GENRE", genGenreBox);
        fld ("STYLE", genStyleBox);
        fld (juce::String (juce::CharPointer_UTF8 ("DRUMMER \xc2\xb7 optional")), genDrummerBox);

        g.setFont (ui::monoFont (8.5f));
        g.setColour (ui::textDim);
        const juce::String pn[] = { "Complexity", "Dynamics", "Humanize", "Fills", "Swing" };
        juce::Slider* ps[] = { &genComplex, &genDynamics, &genHuman, &genFill, &genSwing };
        for (int i = 0; i < 5; ++i)
            g.drawText (pn[i], ps[i]->getX() - 96, ps[i]->getY(), 92, ps[i]->getHeight(),
                        juce::Justification::centredLeft);

        g.setFont (ui::monoFont (8.5f));
        g.setColour (ui::textMuted);
        g.drawText (juce::CharPointer_UTF8 (
            "Reads each bar's time signature \xc2\xb7 writes on the staff above"),
            genGenreBox.getX(), genOneBtn.getY() + 10,
            genAllBtn.getX() - genGenreBox.getX() - 12, 14,
            juce::Justification::centredLeft);
    }
}

void DrumOverlay::resized()
{
    const int W = getWidth();
    const int x0 = margin;

    closeButton.setBounds (W - margin - 34, headerY + 1, 34, 30);

    playButton.setBounds (x0 + 96, headerY, 96, headerH);
    bpmDown.setBounds (x0 + 204, headerY + 4, 24, 26);
    bpmUp.setBounds (x0 + 310, headerY + 4, 24, 26);
    swingSlider.setBounds (x0 + 362, headerY + 5, 108, 24);
    metroChip.setBounds (x0 + 484, headerY + 3, 78, 28);
    // GRID and GENERATE live in the lower action row.
    followChip.setBounds (x0 + 578, headerY + 3, 70, 28);
    editChip.setBounds (x0 + 648, headerY + 3, 56, 28);
    syncChip.setBounds (x0 + 720, headerY + 3, 56, 28);   // DAW BPM follow

    // ---- guitar ribbon (band below the transport), in chain order
    {
        const int ky = gtrRibY + 24, kh = 72;
        const int rightLimit = W - margin - 130;   // leave room for "open guitar"
        int gx = margin + 96;
        const int rigCount = processor.getRigCount();
        const int rigIdealW = rigCount == 1 ? 430 : (rigCount == 2 ? 360 : 306);
        int ideal = 0;
        for (const auto& grp : gtrGroups)
            ideal += grp.rigLane >= 0 ? (grp.rigLane == 0 ? rigIdealW : 0)
                                      : juce::jmax (44, (grp.last - grp.first + 1) * 28) + 10;
        const int usable = juce::jmax (240, rightLimit - gx);
        const float scale = juce::jmin (1.0f, usable / (float) juce::jmax (1, ideal));
        const int kw = juce::jlimit (18, 26, juce::roundToInt (26.0f * scale));
        int rigX = -1;
        for (auto& grp : gtrGroups)
        {
            if (grp.rigLane >= 0)
            {
                if (grp.rigLane == 0)
                {
                    rigX = gx;
                    gx += juce::roundToInt ((float) rigIdealW * scale);
                }
                grp.x = rigX;
                grp.w = juce::roundToInt ((float) (rigIdealW - 10) * scale);
                const int idealKnob = rigCount == 1 ? 36 : (rigCount == 2 ? 30 : 22);
                const int ampKnobW = juce::jlimit (19, idealKnob,
                                                   juce::roundToInt (idealKnob * scale));
                const int cardH = rigCount == 1 ? 140 : (rigCount == 2 ? 76 : 50);
                const int cardY = gtrRibY + (gtrRibH - (cardH * rigCount
                                  + 4 * juce::jmax (0, rigCount - 1))) / 2
                                  + grp.rigLane * (cardH + 4);
                const bool compact = rigCount > 1;
                const int componentH = ampKnobW + (compact ? 12 : 26);
                const int contentTop = cardY + 12;     // dedicated AMP/IR title row
                const int contentBottom = cardY + cardH - 4;
                const int ay = contentTop + juce::jmax (0,
                    (contentBottom - contentTop - componentH) / 2);
                const int ampAreaW = juce::roundToInt (grp.w * 0.68f);
                const int ampSetW = 6 * ampKnobW + 5 * 2;
                int kx = grp.x + (ampAreaW - ampSetW) / 2;
                for (int i = grp.first; i <= grp.last && i < gtrKnobs.size(); ++i)
                {
                    if (i == grp.first + 6)
                    {
                        const int irX = grp.x + ampAreaW + 3;
                        const int irW = grp.w - ampAreaW - 3;
                        const int irSetW = 3 * ampKnobW + 2 * 2;
                        kx = irX + (irW - irSetW) / 2;
                    }
                    gtrKnobs[i]->setCompactLayout (compact);
                    gtrKnobs[i]->setBounds (kx, ay, ampKnobW, componentH);
                    kx += ampKnobW + 2;
                }
                continue;
            }
            grp.x = gx;
            const int nKnobs = grp.last - grp.first + 1;
            if (nKnobs <= 0)                        // name-only mini-slot
            {
                grp.w = juce::roundToInt (44.0f * scale);
            }
            else
            {
                const int knobsW = nKnobs * kw + juce::jmax (0, nKnobs - 1) * 2;
                grp.w = juce::jmax (juce::roundToInt (44.0f * scale), knobsW + 8);
                int knobX = grp.x + (grp.w - knobsW) / 2;
                for (int i = grp.first; i <= grp.last && i < gtrKnobs.size(); ++i)
                {
                    gtrKnobs[i]->setBounds (knobX, ky, kw, kh);
                    knobX += kw + 2;
                }
            }
            gx = grp.x + grp.w + juce::roundToInt (12.0f * scale);
        }
        // same spot as the guitar screen's "open drums" (aligned bands)
        gtrOpenBtn.setBounds (W - margin - 116, gtrRibY + (gtrRibH - 26) / 2, 116, 26);
    }

    // section tabs
    {
        int sx = x0;
        for (auto* t : sectionTabs)
        {
            t->setBounds (sx, tabsY, 120, tabsH);
            sx += 124;
        }
        addSectionBtn.setBounds (sx, tabsY, 84, tabsH);
        sx += 90;
        delSectionBtn.setBounds (sx, tabsY, 86, tabsH);
    }

    // bar headers + staff (widths follow the meter)
    scoreView.setBounds (margin, scoreY, W - 2 * margin, scoreH);
    computeBarLayout (scoreView.getWidth() - 8);
    for (auto* h : barHeads)
    {
        const auto& L = barLay[h->barInSec];
        const int x0 = margin + (int) (L.notesX - curStepW * 0.5f - 5.0f);
        h->setBounds (x0, barHeadsY, (int) (L.width + curStepW + 10.0f), barHeadsH - 2);
    }

    // library (column browser); GRID and the GENERATOR replace only the
    // preview column, so the genre + groove list never disappear (clean UI)
    const int libBottom = sourceY - 8;
    const int libH = libBottom - libY;
    const int listX = margin + genreColW + colGap;
    const int prevX = listX + listColW + colGap;
    const int prevW = (W - margin) - prevX;
    {
        genreVp.setBounds (margin, libY, genreColW, libH);
        const int tabW = (listColW - 4) / 2;
        tabGrooves.setBounds (listX, libY, tabW, 24);
        tabViradas.setBounds (listX + tabW + 4, libY, tabW, 24);
        listVp.setBounds (listX, libY + 28, listColW, libH - 28);
        previewPane.setBounds (prevX, libY, prevW, libH - 36);
        applyBtn.setBounds (prevX, libBottom - 30, 190, 30);
        // humanize: one chip; the 3 sliders live in the popover panel above it
        humChip.setBounds (W - margin - 112, libBottom - 28, 112, 26);
        if (humPanel != nullptr)
            humPanel->setBounds (W - margin - 214, libBottom - 28 - 106, 214, 100);
    }
    helpChip.setBounds (W - margin - 26, scoreY + 6, 22, 20);
    gridView.setBounds (prevX, libY, prevW, libH);

    // generator, inside the preview slot: selects row + 2-column sliders +
    // action buttons (no dead space)
    {
        genGenreBox.setBounds   (prevX + 4, libY + 26, 148, 30);
        genStyleBox.setBounds   (prevX + 160, libY + 26, 158, 30);
        genDrummerBox.setBounds (prevX + 326, libY + 26, 180, 30);

        juce::Slider* ps[] = { &genComplex, &genDynamics, &genHuman, &genFill, &genSwing };
        const int colW = (prevW - 16) / 2;
        for (int i = 0; i < 5; ++i)
        {
            const int col = i % 2, row = i / 2;
            ps[i]->setBounds (prevX + 4 + 92 + col * colW, libY + 78 + row * 34,
                              colW - 100, 22);
        }

        genOneBtn.setBounds (prevX + prevW - 168, libY + libH - 40, 168, 34);
        genAllBtn.setBounds (prevX + prevW - 168 - 8 - 140, libY + libH - 40, 140, 34);
    }

    kitChip.setBounds (margin, sourceY, 254, sourceH);
    saveChip.setBounds (margin + 262, sourceY + 2, 120, 28);
    genChip.setBounds (W - margin - 396, sourceY + 2, 92, 28);
    gridChip.setBounds (W - margin - 298, sourceY + 2, 62, 28);
    levelSlider.setBounds (W - margin - 130, sourceY + 3, 130, 26);
}

//==============================================================================
// Staff layout with variable meters (computes per-bar widths and, if it
// overflows the usable width, shrinks everything proportionally to fit).
void DrumOverlay::computeBarLayout (int availW)
{
    const int sec0 = curSection * drum::barsPerSection;
    auto build = [&] (float sw, float bp, float bpad) -> float
    {
        float x = (float) scoreLeft;
        int prevN = -1, prevD = -1;
        for (int b = 0; b < drum::barsPerSection; ++b)
        {
            auto& L = barLay[b];
            const int gb = sec0 + b;
            L.num = engine.meterNum (gb);
            L.den = engine.meterDen (gb);
            meterGroups (L.num, L.den, L.steps, L.groups, L.nGroups);
            L.showTS = (b == 0) || L.num != prevN || L.den != prevD;
            const float meterW = timeSignatureWidth (L.num, L.den);
            if (L.showTS) { L.tsX = x + meterW * 0.5f; x += meterW; }
            else L.tsX = -1.0f;
            L.notesX = x;
            L.width = L.steps * sw + (L.nGroups - 1) * bp;
            x += L.width + bpad;
            prevN = L.num; prevD = L.den;
        }
        return x - bpad + sw * 0.5f + 4.0f;
    };

    float total = build (stepW, beatPad, barPad);
    const float avail = (float) juce::jmax (200, availW);
    if (total > avail)
    {
        // The clef offset and engraved time signatures do not scale with note
        // spacing. Solve only for the scalable part; scaling the entire total
        // once left mixed/odd-meter final bars a few pixels outside the view.
        float fixed = (float) scoreLeft + 4.0f;
        for (const auto& L : barLay)
            if (L.showTS)
                fixed += timeSignatureWidth (L.num, L.den);
        const float k = juce::jlimit (0.35f, 1.0f,
                                     (avail - fixed) / juce::jmax (1.0f, total - fixed));
        curStepW = stepW * k; curBeatPad = beatPad * k; curBarPad = barPad * k;
        total = build (curStepW, curBeatPad, curBarPad);
    }
    else { curStepW = stepW; curBeatPad = beatPad; curBarPad = barPad; }
    scoreTotalW = total;
}

int DrumOverlay::groupIndexInBar (int b, int s) const
{
    const auto& L = barLay[b];
    int acc = 0;
    for (int i = 0; i < L.nGroups; ++i) { acc += L.groups[i]; if (s < acc) return i; }
    return juce::jmax (0, L.nGroups - 1);
}

float DrumOverlay::stepXInBar (int b, int s) const
{
    return barLay[b].notesX + s * curStepW + groupIndexInBar (b, s) * curBeatPad
           + curStepW * 0.5f;
}

int DrumOverlay::barAtXlocal (int x) const
{
    for (int b = 0; b < drum::barsPerSection; ++b)
        if ((float) x >= barLay[b].notesX - curStepW * 0.5f - 5.0f
            && (float) x <= barLay[b].notesX + barLay[b].width + 5.0f)
            return b;
    return -1;
}

//==============================================================================
void DrumOverlay::refreshAll()
{
    rebuildSectionTabs();
    rebuildBarHeads();
    rebuildGenreCol();
    rebuildList();

    // clean UI: the genre + groove list stay visible; GRID and the GENERATOR
    // replace only the preview column
    const bool prev = ! gridOn && ! genOn;
    const bool mine = currentGenre == "MINE";
    genreVp.setVisible (true);
    listVp.setVisible (true);
    previewPane.setVisible (prev);
    tabGrooves.setVisible (! mine);
    tabViradas.setVisible (! mine);
    applyBtn.setVisible (prev && selValid);
    const bool humShow = prev && ! mine;
    humChip.setVisible (humShow);
    if (! humShow && humPanel != nullptr && humPanel->isVisible())
    {
        humPanel->setVisible (false);
        humChip.getProperties().set ("chipActive", false);
    }
    gridView.setVisible (gridOn);

    juce::Component* genComps[] = { &genGenreBox, &genStyleBox, &genDrummerBox,
                                    &genComplex, &genDynamics, &genHuman, &genFill, &genSwing,
                                    &genOneBtn, &genAllBtn };
    for (auto* c : genComps)
        c->setVisible (genOn);

    gridView.repaint();
    scoreView.repaint();
    repaint();
}

void DrumOverlay::rebuildSectionTabs()
{
    sectionTabs.clear();
    const int nSec = juce::jlimit (1, drum::maxSections, engine.numSections.load());
    curSection = juce::jlimit (0, nSec - 1, curSection);
    const int playSec = engine.uiBar.load() >= 0
                            ? engine.uiBar.load() / drum::barsPerSection : -1;

    for (int i = 0; i < nSec; ++i)
    {
        const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + i));
        auto* t = sectionTabs.add (new juce::TextButton (
            juce::String ("SECTION ") + letter
            + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
            + juce::String (i * 4 + 1) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"))
            + juce::String (i * 4 + 4)));
        t->getProperties().set ("chip", true);
        t->getProperties().set ("chipActive", i == curSection);
        if (i == playSec && i != curSection)
            t->setColour (juce::TextButton::textColourOffId, ui::glowOrange);
        t->setMouseClickGrabsKeyboardFocus (false);
        t->onClick = [this, i]
        {
            curSection = i;
            selBar = 0;
            refreshAll();
        };
        addAndMakeVisible (*t);
    }
    addSectionBtn.setEnabled (nSec < drum::maxSections);
    delSectionBtn.setEnabled (nSec > 1);
    resized();
}

void DrumOverlay::rebuildBarHeads()
{
    barHeads.clear();
    for (int b = 0; b < drum::barsPerSection; ++b)
    {
        const int g = curSection * drum::barsPerSection + b;
        auto* h = barHeads.add (new BarHead());
        h->barInSec = b;
        h->empty = ! engine.barUsed[g].load();
        h->title = juce::String (g + 1) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
                   + (h->empty ? juce::String ("empty")
                               : (engine.barNames[g].isNotEmpty() ? engine.barNames[g]
                                                           : juce::String ("edited")));
        h->selected = b == selBar;
        h->meterText = juce::String (engine.meterNum (g)) + "/" + juce::String (engine.meterDen (g));
        h->roleId = resolveRole (g);
        h->roleAuto = engine.barRole[g] == 0;
        h->roleText = roleLabel (h->roleId);
        h->onSelect = [this, b]
        {
            selBar = b;
            refreshAll();
        };
        h->onClear = [this, g]
        {
            engine.clearBar (g);
            engine.barNames[g].clear();
            refreshAll();
        };
        h->onMeter = [this, b, h] { selBar = b; openMeterMenu (b, h); };
        h->onRole  = [this, b, h] { selBar = b; openRoleMenu (b, h); };
        h->onSave  = [this, b]    { selBar = b; promptSaveBar(); };
        h->setRepaintsOnMouseActivity (true);   // save/clear appear on hover
        addAndMakeVisible (*h);
    }
    resized();
}

void DrumOverlay::BarHead::paint (juce::Graphics& g)
{
    // Slim controls above each bar. The meter remains engraved in the staff,
    // while this compact pill is its explicit selector.
    const int H = getHeight(), W = getWidth();
    const int py = (H - 18) / 2;
    const bool hover = isMouseOver (true);
    const juce::juce_wchar caret = juce::CharPointer_UTF8 ("\xe2\x96\xbe")[0];

    auto pill = [&] (juce::Rectangle<int> r, const juce::String& txt, juce::Colour c,
                     bool strong)
    {
        g.setColour (juce::Colour (0xff0c0e11).withAlpha (0.85f));
        g.fillRoundedRectangle (r.toFloat(), 5.0f);
        g.setColour (c.withAlpha (0.11f)); g.fillRoundedRectangle (r.toFloat(), 5.0f);
        g.setColour (c.withAlpha (strong ? 0.6f : 0.32f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 5.0f, 1.0f);
        g.setColour (strong ? c : c.withAlpha (0.85f));
        g.setFont (ui::uiFont (9.5f, true));
        g.drawText (txt, r.getX() + 7, r.getY(), r.getWidth() - 22, 18, juce::Justification::centredLeft);
        g.setFont (ui::monoFont (7.0f));
        g.drawText (juce::String::charToString (caret), r.getRight() - 13, r.getY(), 10, 18,
                    juce::Justification::centred);
    };

    // METER selector first, immediately followed by the ROLE selector.
    const int mtw = juce::GlyphArrangement::getStringWidthInt (ui::monoFont (9.5f, true), meterText);
    meterRect = { 6, py, mtw + 24, 18 };
    pill (meterRect, meterText, ui::textDim, selected);

    // ROLE pill - color by role; dimmer when "auto"
    const juce::Colour rc = roleId == 3 ? ui::textDim : (roleId >= 4 ? ui::glowOrange : ui::accent);
    const int rtw = juce::GlyphArrangement::getStringWidthInt (ui::uiFont (9.5f, true), roleText);
    roleRect = { meterRect.getRight() + 5, py, rtw + 24, 18 };
    pill (roleRect, roleText, rc, ! roleAuto);

    // clear + save (right, on hover/selection so the staff stays clean)
    const bool showX = ! empty && (hover || selected);
    clearRect = showX ? juce::Rectangle<int> (W - 22, py, 16, 18) : juce::Rectangle<int>();
    if (showX)
    {
        g.setFont (ui::monoFont (11.0f));
        g.setColour (ui::textMuted);
        g.drawText (juce::CharPointer_UTF8 ("\xc3\x97"), clearRect, juce::Justification::centred);
    }
    const bool showSave = ! empty && (hover || selected);
    saveRect = showSave ? juce::Rectangle<int> ((showX ? clearRect.getX() : W - 6) - 20, py, 18, 18)
                        : juce::Rectangle<int>();
    if (showSave)
    {
        g.setFont (ui::monoFont (10.0f, true));
        g.setColour (saveRect.contains (getMouseXYRelative()) ? ui::accent : ui::textFaint);
        g.drawText (juce::CharPointer_UTF8 ("\xe2\xa4\x93"), saveRect,
                    juce::Justification::centred);   // save bar to "My bars"
    }

    // title (number, groove) in the middle, if it fits
    const int tx = roleRect.getRight() + 8;
    const int tw = (saveRect.isEmpty() ? W - 6 : saveRect.getX() - 4) - tx;
    if (tw > 24)
    {
        g.setFont (ui::uiFont (10.5f, false));
        g.setColour (empty ? ui::textMuted : (selected ? ui::accent : ui::textDim));
        g.drawText (title, tx, 0, tw, H, juce::Justification::centredLeft, true);
    }
}

void DrumOverlay::BarHead::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (! empty && clearRect.contains (p)) { if (onClear)  onClear();  return; }
    if (! empty && saveRect.contains (p))  { if (onSave)   onSave();   return; }
    if (meterRect.contains (p))            { if (onMeter)  onMeter();  return; }
    if (roleRect.contains (p))             { if (onRole)   onRole();   return; }
    if (onSelect) onSelect();
}

//==============================================================================
// The central staff (time signature per bar, variable widths)
void DrumOverlay::ScoreView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (notationFrame);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (notationCyan.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

    const juce::Rectangle<float> paper (4.0f, 30.0f,
                                        (float) getWidth() - 8.0f,
                                        (float) getHeight() - 34.0f);
    fillNotationPaper (g, paper, 3.5f, 0.28f);

    owner.computeBarLayout (getWidth() - 8);
    auto& engine = owner.engine;
    const int sec0 = owner.curSection * drum::barsPerSection;
    auto sx = [&] (int b, int s) { return owner.stepXInBar (b, s); };
    auto yOf = [] (float pos) { return staffY (pos); };

    // staff lines + percussion clef
    g.setColour (notationStaff.withAlpha (0.86f));
    for (int i = 0; i <= 4; ++i)
        g.drawLine (paper.getX() + 8.0f, staffY ((float) (i * 2)),
                    paper.getRight() - 8.0f, staffY ((float) (i * 2)), 0.9f);
    drawMusicGlyph (g, smuflPercussionClef, { 29.0f, staffY (4.0f) },
                    32.0f * staffSP, notationInk);

    // background of the selected bar
    {
        const auto& L = owner.barLay[owner.selBar];
        const float x0 = L.notesX - owner.curStepW * 0.5f - 5.0f, x1 = L.notesX + L.width + 5.0f;
        g.setColour (notationCyan.withAlpha (0.065f));
        g.fillRoundedRectangle (x0, staffY (13.0f), x1 - x0, staffY (-5.0f) - staffY (13.0f), 2.0f);
        g.setColour (notationCyan.withAlpha (0.42f));
        const float dash[] = { 3.0f, 3.0f };
        juce::Path outline, dashed;
        outline.addRoundedRectangle (x0, staffY (13.0f), x1 - x0,
                                     staffY (-5.0f) - staffY (13.0f), 2.0f);
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dash, 2);
        g.fillPath (dashed);
    }
    if (dragOverBar >= 0)
    {
        const auto& L = owner.barLay[dragOverBar];
        const float x0 = L.notesX - owner.curStepW * 0.5f - 5.0f, x1 = L.notesX + L.width + 5.0f;
        g.setColour (notationAmber.withAlpha (0.10f));
        g.fillRoundedRectangle (x0, staffY (13.0f), x1 - x0, staffY (-5.0f) - staffY (13.0f), 2.0f);
        g.setColour (notationAmber);
        g.drawRoundedRectangle (x0, staffY (13.0f), x1 - x0, staffY (-5.0f) - staffY (13.0f), 2.0f, 1.4f);
    }

    // playhead
    const int uiBar = engine.uiBar.load();
    if (uiBar >= 0 && uiBar / drum::barsPerSection == owner.curSection)
    {
        const int b = uiBar % drum::barsPerSection;
        const int s = juce::jlimit (0, owner.barLay[b].steps - 1, engine.uiStep.load());
        const float px = sx (b, s);
        g.setColour (notationCyan.withAlpha (0.11f));
        g.fillRoundedRectangle (px - owner.curStepW * 0.45f, staffY (13.0f),
                                owner.curStepW * 0.9f,
                                staffY (-5.0f) - staffY (13.0f), 2.0f);
        g.setColour (notationCyan);
        g.fillRect (px - 0.8f, staffY (13.0f), 1.6f,
                    staffY (-5.0f) - staffY (13.0f));
        juce::Path marker;
        marker.addTriangle (px - 4.0f, staffY (13.0f) - 1.0f,
                            px + 4.0f, staffY (13.0f) - 1.0f,
                            px, staffY (13.0f) + 5.0f);
        g.fillPath (marker);
    }

    auto drawHead = [&] (float x, float y, bool cross, bool ghost)
    {
        drawMusicGlyph (g, cross ? smuflNoteheadXBlack : smuflNoteheadBlack,
                        { x, y }, 22.0f * staffSP,
                        ghost ? notationDim.withAlpha (0.74f) : notationInk);
        if (ghost)
        {
            drawMusicGlyph (g, smuflNoteheadParenthesisLeft,
                            { x - 1.12f * staffSP, y }, 24.0f * staffSP,
                            notationDim.withAlpha (0.86f));
            drawMusicGlyph (g, smuflNoteheadParenthesisRight,
                            { x + 1.12f * staffSP, y }, 24.0f * staffSP,
                            notationDim.withAlpha (0.86f));
        }
    };

    const float beamYH = staffY (12.0f), beamYF = staffY (-4.0f);
    const float headHalfW = staffSP * 0.90f;
    const float stemW = 1.15f;
    const float beamH = 3.0f;

    for (int b = 0; b < drum::barsPerSection; ++b)
    {
        const auto& L = owner.barLay[b];
        const int bar = sec0 + b;

        // Engraved time signature appears only at the start of the system or
        // when the bar meter changes. Its selector remains in the bar header.
        if (L.showTS)
            drawMusicTimeSignature (g, L.tsX, staffSP, yOf,
                                    L.num, L.den, notationInk);

        // bar line
        const float bx = L.notesX + L.width + owner.curBarPad * 0.5f - 2.0f;
        g.setColour (notationInk);
        if (b == drum::barsPerSection - 1)
        {
            g.drawLine (bx - 3.2f, staffY (8), bx - 3.2f, staffY (0), 1.0f);
            g.drawLine (bx, staffY (8), bx, staffY (0), 2.2f);
        }
        else
            g.drawLine (bx, staffY (8), bx, staffY (0), 1.0f);

        // beat numbers (1 per group)
        g.setFont (ui::monoFont (9.0f));
        g.setColour (notationDim.withAlpha (0.9f));
        for (int gi = 0, gs = 0; gi < L.nGroups; gs += L.groups[gi], ++gi)
            g.drawText (juce::String (gi + 1), (int) sx (b, gs) - 8,
                        getHeight() - 18, 16, 12, juce::Justification::centred);

        if (! engine.barUsed[bar].load())
        {
            drawMusicGlyph (g, smuflRestWhole,
                            { L.notesX + L.width * 0.5f, staffY (4.0f) },
                            30.0f * staffSP, notationDim);
            continue;
        }

        // notes + stems + beams, per meter beat group
        for (int gi = 0, gStart = 0; gi < L.nGroups; gStart += L.groups[gi], ++gi)
        {
            const int gLen = L.groups[gi];
            for (int limb = 0; limb < 2; ++limb)
            {
                const bool up = limb == 0;
                struct Col { int s; float noteY; bool accent; };
                Col cols[8];
                int numCols = 0;

                for (int k = 0; k < gLen; ++k)
                {
                    const int s = gStart + k;
                    float extremeY = up ? -1.0e9f : 1.0e9f;
                    bool any = false, acc = false;
                    for (int v = 0; v < drum::numVoices; ++v)
                    {
                        if (staffIsHand[v] != up)
                            continue;
                        const int val = engine.pattern[bar][v][s].load();
                        if (val == 0)
                            continue;
                        any = true;
                        if (val == 2)
                            acc = true;
                        const float y = staffY (staffPos[v]);
                        drawHead (sx (b, s), y, staffXHead[v], val == 3);
                        extremeY = up ? juce::jmax (extremeY, y) : juce::jmin (extremeY, y);
                    }
                    if (any && numCols < 8)
                        cols[numCols++] = { s, extremeY, acc };
                }
                if (numCols == 0)
                    continue;

                const float beamY = up ? beamYH : beamYF;
                auto stemX = [&] (int s) { return up ? sx (b, s) + headHalfW
                                                      : sx (b, s) - headHalfW; };

                g.setColour (notationInk);
                for (int c = 0; c < numCols; ++c)
                {
                    g.setColour (notationInk);
                    g.drawLine (stemX (cols[c].s), cols[c].noteY + (up ? -2.0f : 2.0f),
                                stemX (cols[c].s), beamY, stemW);
                    if (cols[c].accent)
                        drawMusicGlyph (g, up ? smuflAccentAbove : smuflAccentBelow,
                                        { sx (b, cols[c].s),
                                          beamY + (up ? -1.45f : 1.45f) * staffSP },
                                        28.0f * staffSP, notationAmber);
                }
                g.setColour (notationInk);

                if (numCols > 1)
                {
                    const float primaryY = up ? beamY : beamY - beamH;
                    g.fillRect (stemX (cols[0].s), primaryY,
                                stemX (cols[numCols - 1].s) - stemX (cols[0].s), beamH);
                    const float secondaryY = primaryY + (up ? 1.65f : -1.65f) * beamH;
                    for (int c = 0; c < numCols - 1; ++c)
                        if (cols[c + 1].s - cols[c].s == 1)
                            g.fillRect (stemX (cols[c].s), secondaryY,
                                        stemX (cols[c + 1].s) - stemX (cols[c].s), beamH);

                    for (int c = 0; c < numCols; ++c)
                    {
                        const bool joinsLeft = c > 0 && cols[c].s - cols[c - 1].s == 1;
                        const bool joinsRight = c + 1 < numCols
                                             && cols[c + 1].s - cols[c].s == 1;
                        if (! joinsLeft && ! joinsRight)
                        {
                            const float len = staffSP * (c == numCols - 1 ? -1.1f : 1.1f);
                            g.fillRect (juce::jmin (stemX (cols[c].s), stemX (cols[c].s) + len),
                                        secondaryY, std::abs (len), beamH);
                        }
                    }
                }
                else
                {
                    const float x = stemX (cols[0].s);
                    for (int flagIndex = 0; flagIndex < 2; ++flagIndex)
                    {
                        const float dir = up ? 1.0f : -1.0f;
                        const float fy = beamY + dir * flagIndex * staffSP * 0.72f;
                        juce::Path flag;
                        flag.startNewSubPath (x, fy);
                        flag.quadraticTo (x + dir * staffSP, fy + dir * staffSP * 0.55f,
                                          x + dir * staffSP * 0.45f,
                                          fy + dir * staffSP * 1.6f);
                        g.strokePath (flag, juce::PathStrokeType (stemW));
                    }
                }
            }
        }
    }

    // (the permanent legend line moved into the "?" popover - clean UI)
}

int DrumOverlay::ScoreView::barAtX (int x) const { return owner.barAtXlocal (x); }

void DrumOverlay::ScoreView::mouseDown (const juce::MouseEvent& e)
{
    owner.computeBarLayout (getWidth() - 8);

    downBar = owner.barAtXlocal (e.x);

    // ASSEMBLE: only selects; the bar drag starts in mouseDrag
    if (! owner.editMode)
    {
        if (downBar >= 0) { owner.selBar = downBar; owner.refreshAll(); }
        return;
    }

    // EDIT: find step/voice and edit the note
    int bb = -1, ss = -1;
    for (int b = 0; b < drum::barsPerSection && bb < 0; ++b)
        for (int s = 0; s < owner.barLay[b].steps; ++s)
            if (std::abs ((float) e.x - owner.stepXInBar (b, s)) <= owner.curStepW * 0.5f + 0.5f)
            {
                bb = b; ss = s; break;
            }
    if (bb < 0)
        return;

    int voice = -1;
    float best = 8.0f;
    for (int v = 0; v < drum::numVoices; ++v)
    {
        const float d = std::abs ((float) e.y - staffY (staffPos[v]));
        if (d < best) { best = d; voice = v; }
    }
    if (voice < 0)
        return;

    const int bar = owner.curSection * drum::barsPerSection + bb;
    owner.selBar = bb;
    if (! owner.engine.barUsed[bar].load())
    {
        juce::uint8 zero[drum::numVoices][drum::maxStepsPerBar] = {};
        owner.engine.setBarPattern (zero, bar);
        owner.engine.barNames[bar] = "new";
    }
    auto& cell = owner.engine.pattern[bar][voice][ss];
    cell.store ((juce::uint8) ((cell.load() + 1) % 4));
    owner.rebuildBarHeads();
    repaint();
    if (owner.gridOn)
        owner.gridView.repaint();
}

void DrumOverlay::ScoreView::mouseDrag (const juce::MouseEvent& e)
{
    if (owner.editMode || downBar < 0 || e.getDistanceFromDragStart() < 6)
        return;
    const int srcG = owner.curSection * drum::barsPerSection + downBar;
    if (! owner.engine.barUsed[srcG].load())
        return;   // empty bar: nothing to drag
    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (this))
        dnd->startDragging ("bar:" + juce::String (srcG), this);
}

void DrumOverlay::ScoreView::itemDragMove (const SourceDetails& d)
{
    const int b = owner.barAtXlocal (d.localPosition.getX());
    if (b != dragOverBar) { dragOverBar = b; repaint(); }
}

void DrumOverlay::ScoreView::itemDragExit (const SourceDetails&)
{
    dragOverBar = -1;
    repaint();
}

void DrumOverlay::ScoreView::itemDropped (const SourceDetails& d)
{
    const int b = owner.barAtXlocal (d.localPosition.getX());
    dragOverBar = -1;
    repaint();
    if (b < 0)
        return;
    const int dstG = owner.curSection * drum::barsPerSection + b;
    const auto desc = d.description.toString();

    // dragging a whole bar (assemble): copies pattern + meter
    if (desc.startsWith ("bar:"))
    {
        const int srcG = desc.substring (4).getIntValue();
        if (srcG != dstG)
        {
            owner.engine.setMeter (dstG, owner.engine.meterNum (srcG),
                                   owner.engine.meterDen (srcG));
            owner.engine.barFromString (owner.engine.barToString (srcG), dstG);
            owner.engine.barNames[dstG] = owner.engine.barNames[srcG];
            owner.selBar = b;
            owner.refreshAll();
        }
        return;
    }
    owner.applyGrooveToBar (desc, dstG);
}

//==============================================================================
// Time signature menu: 5 common ones + Custom (numerator/denominator)
void DrumOverlay::openMeterMenu (int barInSec, juce::Component* anchor)
{
    const int gb = curSection * drum::barsPerSection + barInSec;
    auto apply = [safe = juce::Component::SafePointer<DrumOverlay> (this)] (int g, int num, int den)
    {
        if (safe == nullptr)
            return;
        safe->engine.setMeter (g, num, den);
        safe->refreshAll();
    };

    juce::PopupMenu m;
    static const int MAIN[5][2] = { { 4, 4 }, { 3, 4 }, { 2, 4 }, { 6, 8 }, { 12, 8 } };
    for (auto& mm : MAIN)
    {
        const bool on = engine.meterNum (gb) == mm[0] && engine.meterDen (gb) == mm[1];
        m.addItem (juce::String (mm[0]) + "/" + juce::String (mm[1]), true, on,
                   [apply, gb, mm] { apply (gb, mm[0], mm[1]); });
    }
    m.addSeparator();
    m.addItem (juce::String (juce::CharPointer_UTF8 ("Custom\xe2\x80\xa6")),
               [this, gb, apply]
    {
        auto* w = new juce::AlertWindow (
            juce::String ("Time signature"),
            juce::String ("Numerator (1 to 16) and denominator (2, 4, 8 or 16)"),
            juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("num", juce::String (engine.meterNum (gb)), "Numerator");
        w->addTextEditor ("den", juce::String (engine.meterDen (gb)), "Denominator");
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (juce::String ("Cancel"), 0,
                      juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create (
            [w, gb, apply] (int r)
            {
                if (r == 1)
                {
                    const int num = w->getTextEditorContents ("num").getIntValue();
                    int den = w->getTextEditorContents ("den").getIntValue();
                    if (den != 2 && den != 4 && den != 8 && den != 16)
                        den = 4;
                    apply (gb, juce::jlimit (1, 16, num), den);
                }
                delete w;
            }), false);
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor));
}

int DrumOverlay::resolveRole (int globalBar) const
{
    const int r = engine.barRole[globalBar];
    if (r > 0) return r;
    static const int arc[drum::barsPerSection] = { 1, 2, 3, 5 }; // verse/chorus/bridge/fill
    return arc[globalBar % drum::barsPerSection];
}

void DrumOverlay::openRoleMenu (int barInSec, juce::Component* anchor)
{
    const int gb = curSection * drum::barsPerSection + barInSec;
    auto set = [safe = juce::Component::SafePointer<DrumOverlay> (this)] (int g, int role)
    {
        if (safe == nullptr) return;
        safe->engine.barRole[g] = role;
        safe->refreshAll();
    };

    juce::PopupMenu m;
    const int cur = engine.barRole[gb];
    m.addItem (juce::String ("Automatic (by arc)"), true,
               cur == 0, [set, gb] { set (gb, 0); });
    m.addSeparator();
    for (int r = 1; r <= 5; ++r)
        m.addItem (roleLabel (r), true, cur == r, [set, gb, r] { set (gb, r); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor));
}

//==============================================================================
// Column browser: Genre | Grooves/Fills | Preview
void DrumOverlay::rebuildGenreCol()
{
    genreRows.clear();
    auto names = drum::genres();
    names.add (juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85 MINE")));
    const auto star = juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85"));
    int y = 0;
    for (const auto& n : names)
    {
        const auto key = n.startsWith (star) ? juce::String ("MINE") : n;

        // clean UI: no per-genre counters (visual noise without navigation value)
        auto* b = genreRows.add (new juce::TextButton());
        b->setButtonText (n);
        b->setColour (juce::TextButton::buttonColourId,
                      key == currentGenre ? ui::accentDark.withAlpha (0.22f) : juce::Colour (0));
        b->setColour (juce::TextButton::buttonOnColourId, ui::accentDark.withAlpha (0.22f));
        b->setColour (juce::TextButton::textColourOffId,
                      key == currentGenre ? ui::accent : ui::textDim);
        b->setMouseClickGrabsKeyboardFocus (false);
        b->onClick = [this, key] { currentGenre = key; selValid = false; refreshAll(); };
        genreContent.addAndMakeVisible (b);
        b->setBounds (2, y, genreColW - 14, colRowH);
        y += colRowH + 2;
    }
    genreContent.setSize (genreColW, juce::jmax (y, 1));
    genreVp.setViewPosition (0, 0);
}

void DrumOverlay::rebuildList()
{
    libRows.clear();
    tabGrooves.getProperties().set ("chipActive", currentKind == 1);
    tabViradas.getProperties().set ("chipActive", currentKind == 2);
    tabGrooves.repaint();
    tabViradas.repaint();

    const bool mine = currentGenre == "MINE";
    int y = 0;
    auto add = [&] (const juce::String& name, const juce::String& dragId, bool fill, bool del,
                    std::function<void()> onDel)
    {
        auto* r = libRows.add (new LibRow());
        r->name = name;
        r->dragId = dragId;
        r->fill = fill;
        r->deletable = del;
        r->selected = (selValid && dragId == selDragId);
        r->onSelect = [this, dragId, name, fill] { selectEntry (dragId, name, fill); };
        r->onDelete = std::move (onDel);
        listContent.addAndMakeVisible (r);
        r->setBounds (0, y, listColW - 10, colRowH);
        y += colRowH + 2;
    };

    if (mine)
    {
        for (const auto& f : userGroovesDir().findChildFiles (juce::File::findFiles, false, "*.json"))
        {
            const auto parsed = juce::JSON::parse (f.loadFileAsString());
            const auto nm = parsed.getProperty ("name", f.getFileNameWithoutExtension()).toString();
            add (nm, "u:" + f.getFullPathName(), false, true,
                 [this, f] { f.deleteFile(); selValid = false; rebuildList(); updatePreview(); });
        }
    }
    else
    {
        const auto& lib = drum::library();
        for (int i = 0; i < (int) lib.size(); ++i)
        {
            const auto& g = lib[(size_t) i];
            if (juce::String (juce::CharPointer_UTF8 (g.genre)) != currentGenre)
                continue;
            if (currentKind == 1 && g.fill)  continue;
            if (currentKind == 2 && ! g.fill) continue;
            add (juce::String (juce::CharPointer_UTF8 (g.name)), "f:" + juce::String (i),
                 g.fill, false, nullptr);
        }
    }
    listContent.setSize (listColW - 10, juce::jmax (y, 1));
    listVp.setViewPosition (0, 0);

    if (! selValid && ! libRows.isEmpty())        // selects the first by default
    {
        auto* r = libRows[0];
        selectEntry (r->dragId, r->name, r->fill);
    }
    else
        updatePreview();
}

void DrumOverlay::selectEntry (const juce::String& dragId, const juce::String& name, bool fill)
{
    selDragId = dragId;
    selName = name;
    selFill = fill;
    selValid = true;
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < drum::maxStepsPerBar; ++s)
            selPat[v][s] = 0;
    selBpm = 0;
    selNum = 4; selDen = 4;

    if (dragId.startsWith ("f:"))
    {
        const int i = dragId.substring (2).getIntValue();
        const auto& lib = drum::library();
        if (i >= 0 && i < (int) lib.size())
        {
            drum::parseSpec (lib[(size_t) i], selPat);
            selBpm = lib[(size_t) i].bpm;
            selNum = lib[(size_t) i].num;
            selDen = lib[(size_t) i].den;
        }
    }
    else if (dragId.startsWith ("u:"))
    {
        const auto parsed = juce::JSON::parse (juce::File (dragId.substring (2)).loadFileAsString());
        selBpm = (int) parsed.getProperty ("bpm", 0);
        const auto str = parsed.getProperty ("pattern", "").toString();
        int k = 0;
        for (int v = 0; v < drum::numVoices; ++v)
            for (int s = 0; s < drum::stepsPerBar; ++s)
            {
                const juce::juce_wchar ch = k < str.length() ? str[k] : '0';
                selPat[v][s] = ch >= '0' && ch <= '3' ? (juce::uint8) (ch - '0') : 0;
                ++k;
            }
    }
    for (auto* r : libRows)
    {
        const bool on = r->dragId == dragId;
        if (r->selected != on) { r->selected = on; r->repaint(); }
    }
    updatePreview();
}

void DrumOverlay::updatePreview()
{
    applyBtn.setButtonText (juce::String ("apply to bar ")
                            + juce::String (selectedBar() + 1));
    applyBtn.setVisible (! gridOn && selValid);
    previewPane.repaint();
}

void DrumOverlay::applyGrooveToBar (const juce::String& dragId, int globalBar)
{
    if (globalBar < 0 || globalBar >= engine.totalBars())
        return;

    juce::uint8 pat[drum::numVoices][drum::maxStepsPerBar] = {};
    juce::String name;
    int gNum = 4, gDen = 4;   // groove meter (library grooves may be odd)

    if (dragId.startsWith ("f:"))
    {
        const int idx = dragId.substring (2).getIntValue();
        const auto& lib = drum::library();
        if (idx < 0 || idx >= (int) lib.size())
            return;
        const auto& g = lib[(size_t) idx];
        drum::parseSpec (g, pat);
        name = juce::String (juce::CharPointer_UTF8 (g.name));
        gNum = g.num; gDen = g.den;
    }
    else if (dragId.startsWith ("u:"))
    {
        const juce::File f (dragId.substring (2));
        const auto parsed = juce::JSON::parse (f.loadFileAsString());
        const auto str = parsed.getProperty ("pattern", "").toString();
        if (str.isEmpty())
            return;
        int i = 0;
        for (int v = 0; v < drum::numVoices; ++v)
            for (int s = 0; s < drum::stepsPerBar; ++s)
            {
                const juce::juce_wchar c = i < str.length() ? str[i] : '0';
                pat[v][s] = c >= '0' && c <= '3' ? (juce::uint8) (c - '0') : 0;
                ++i;
            }
        name = parsed.getProperty ("name", f.getFileNameWithoutExtension()).toString();
        // user bars are saved in 4/4 (16 steps)
    }
    else
        return;

    // applies ONLY the notes - the song's BPM/swing does not change when you
    // drop a groove (the value on the card is just a suggestion; adjust the
    // tempo in the transport). The bar's meter becomes the groove's (odd
    // prog/djent grooves bring 7/8, 5/4, etc.). setMeter BEFORE setBarPattern
    // so barSteps() already reflects the new meter.
    engine.setMeter (globalBar, gNum, gDen);
    engine.setBarPattern (pat, globalBar);
    engine.barNames[globalBar] = name;

    curSection = globalBar / drum::barsPerSection;
    selBar = globalBar % drum::barsPerSection;
    refreshAll();
}

//==============================================================================
// ---- Groove generator (phase 19) ------------------------------------------
void DrumOverlay::setupGuitarRibbon()
{
    gtrOpenBtn.getProperties().set ("chip", true);
    gtrOpenBtn.setMouseClickGrabsKeyboardFocus (false);
    gtrOpenBtn.onClick = [this] { if (onClose) onClose(); closeAnimated(); };
    addAndMakeVisible (gtrOpenBtn);
    buildGuitarRibbon();
}

// The guitar ribbon mirrors the real signal chain: one group per active effect
// in chain order (amp shows its 6 knobs; pedals show their primary knob; the
// rest show a name-only mini-slot), so it reads as a miniature of the chain.
void DrumOverlay::buildGuitarRibbon()
{
    gtrKnobs.clear();     // OwnedArray deletes the child components
    gtrGroups.clear();
    auto& apvts = processor.apvts;

    auto add = [&] (const juce::String& id, const juce::String& lbl)
    {
        if (apvts.getParameter (id) == nullptr) return;
        auto* kn = new KnobComponent (apvts, id, lbl, [id] (float v)
        {
            if (id.endsWithIgnoreCase ("LowCut") || id.endsWithIgnoreCase ("HighCut"))
            {
                if (v >= 1000.0f)
                    return juce::String (v / 1000.0f, v < 10000.0f ? 1 : 0) + "k";
                return juce::String (juce::roundToInt (v));
            }
            return juce::String (v, 1);
        });
        gtrKnobs.add (kn);
        addAndMakeVisible (*kn);
    };

    struct KP { const char* id; const char* lbl; };
    auto defFor = [] (const juce::String& id, juce::String& name) -> std::vector<KP>
    {
        if (id == "amp")      { name = "AMP";     return {}; }
        if (id == "gate")     { name = "GATE";    return { {"gateThresh","THRESH"} }; }
        if (id == "comp")     { name = "COMP";    return { {"compLevel","LEVEL"} }; }
        if (id == "od")       { name = "OD";      return { {"odDrive","DRIVE"},{"odLevel","LEVEL"} }; }
        if (id == "preeq")    { name = "PRE-EQ";  return { {"preEqMid","MID"} }; }
        if (id == "eq")       { name = "EQ";      return { {"eqMid","MID"} }; }
        if (id == "mod")      { name = "MOD";     return { {"modMix","MIX"} }; }
        if (id == "delay")    { name = "DELAY";   return { {"delayMix","MIX"} }; }
        if (id == "reverb")   { name = "REVERB";  return { {"revMix","MIX"} }; }
        if (id == "pitch")    { name = "PITCH";   return { {"pitchMix","MIX"} }; }
        if (id == "limiter")  { name = "LIMIT";   return { {"limCeiling","CEILING"} }; }
        if (id == "wah")      { name = "WAH";     return { {"wahFreq","FREQ"} }; }
        if (id == "harm")     { name = "HARM";    return { {"harmMix","MIX"} }; }
        if (id == "octaver")  { name = "OCT";     return { {"octSub","SUB"} }; }
        if (id == "ringmod")  { name = "RING";    return { {"rmMix","MIX"} }; }
        if (id == "bitcrush") { name = "CRUSH";   return { {"bcMix","MIX"} }; }
        if (id == "slowgear") { name = "SLOW";    return { {"sgRise","RISE"} }; }
        if (id == "exciter")  { name = "EXCITE";  return { {"excAmt","AMOUNT"} }; }
        if (id == "deesser")  { name = "DE-ESS";  return { {"dsAmt","AMOUNT"} }; }
        if (id == "tape")     { name = "TAPE";    return { {"tapeDrive","DRIVE"} }; }
        if (id == "console")  { name = "CONSOLE"; return { {"cnsAmt","AMOUNT"} }; }
        if (id.startsWith ("ext")) { name = "VST"; return {}; }
        name = {};            // looper / analyzer / cab / mixer -> skipped
        return {};
    };

    auto onFor = [] (const juce::String& id)
    {
        if (id == "amp") return juce::String ("ampOn");
        if (id == "gate") return juce::String ("gateOn");
        if (id == "comp") return juce::String ("compOn");
        if (id == "od") return juce::String ("odOn");
        if (id == "preeq") return juce::String ("preEqOn");
        if (id == "eq") return juce::String ("eqOn");
        if (id == "mod") return juce::String ("modOn");
        if (id == "delay") return juce::String ("delayOn");
        if (id == "reverb") return juce::String ("revOn");
        if (id == "pitch") return juce::String ("pitchOn");
        if (id == "limiter") return juce::String ("limOn");
        if (id == "looper") return juce::String ("looperOn");
        if (id == "wah") return juce::String ("wahOn");
        if (id == "harm") return juce::String ("harmOn");
        if (id == "octaver") return juce::String ("octOn");
        if (id == "ringmod") return juce::String ("rmOn");
        if (id == "bitcrush") return juce::String ("bcOn");
        if (id == "slowgear") return juce::String ("sgOn");
        if (id == "exciter") return juce::String ("excOn");
        if (id == "deesser") return juce::String ("dsOn");
        if (id == "tape") return juce::String ("tapeOn");
        if (id == "console") return juce::String ("cnsOn");
        if (id == "analyzer") return juce::String ("anOn");
        if (id.startsWith ("ext")) return id + "On";
        return juce::String();
    };

    for (const auto& id : processor.getChainOrder())
    {
        if (id == "amp")
        {
            for (int r = 0; r < processor.getRigCount(); ++r)
            {
                GtrGroup grp;
                grp.name = "AMP" + juce::String (r + 1) + "  >  IR" + juce::String (r + 1);
                grp.onParam = "ampOn";
                grp.first = gtrKnobs.size();
                const auto prefix = r == 0 ? juce::String ("amp") : "amp" + juce::String (r + 1);
                add (prefix + "Gain", "GAIN"); add (prefix + "Bass", "BASS");
                add (prefix + "Mid", "MID"); add (prefix + "Treble", "TREBLE");
                add (prefix + "Presence", "PRES"); add (prefix + "Master", "MASTER");
                const auto cabPrefix = "cab" + juce::String (r + 1);
                add (cabPrefix + "Blend", "MIX");
                add (cabPrefix + "LowCut", "LO CUT");
                add (cabPrefix + "HighCut", "HI CUT");
                grp.last = gtrKnobs.size() - 1;
                grp.rigLane = r;
                gtrGroups.push_back (grp);
            }
            continue;
        }
        juce::String name;
        const auto kps = defFor (id, name);
        if (name.isEmpty()) continue;
        GtrGroup grp; grp.name = name; grp.onParam = onFor (id); grp.first = gtrKnobs.size();
        for (const auto& kp : kps) add (kp.id, kp.lbl);
        grp.last = gtrKnobs.size() - 1;   // < first when name-only
        gtrGroups.push_back (grp);
    }
    resized();
    repaint();
}

void DrumOverlay::setupGenerator()
{
    for (auto* b : { &genGenreBox, &genStyleBox, &genDrummerBox })
    {
        b->setMouseClickGrabsKeyboardFocus (false);
        addChildComponent (*b);
    }
    for (const auto& g : drum::genGenres())
        genGenreBox.addItem (g, genGenreBox.getNumItems() + 1);
    genGenreBox.setSelectedItemIndex (0, juce::dontSendNotification);
    genGenreBox.onChange = [this] { rebuildGenStyles(); rebuildGenDrummers(); };

    // 5 parameters 0..1
    struct SP { juce::Slider* s; double def; } sps[] = {
        { &genComplex, 0.65 }, { &genDynamics, 0.60 }, { &genHuman, 0.35 },
        { &genFill, 0.20 },    { &genSwing, 0.00 } };
    for (auto& sp : sps)
    {
        sp.s->setSliderStyle (juce::Slider::LinearHorizontal);
        sp.s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
        sp.s->setRange (0.0, 1.0, 0.01);
        sp.s->setValue (sp.def, juce::dontSendNotification);
        sp.s->setColour (juce::Slider::trackColourId, ui::accent.withAlpha (0.7f));
        sp.s->setMouseClickGrabsKeyboardFocus (false);
        addChildComponent (*sp.s);
    }
    genFill.setColour (juce::Slider::trackColourId, ui::glowOrange.withAlpha (0.7f));

    genOneBtn.setButtonText (juce::String ("GENERATE THIS BAR"));
    genAllBtn.setButtonText (juce::String ("FILL THE 4 BARS"));
    genOneBtn.getProperties().set ("chip", true);
    genAllBtn.getProperties().set ("accent", true);
    genOneBtn.setMouseClickGrabsKeyboardFocus (false);
    genAllBtn.setMouseClickGrabsKeyboardFocus (false);
    genOneBtn.onClick = [this] { generateOne(); };
    genAllBtn.onClick = [this] { generateAll(); };
    addChildComponent (genOneBtn);
    addChildComponent (genAllBtn);

    rebuildGenStyles();
    rebuildGenDrummers();
}

void DrumOverlay::rebuildGenStyles()
{
    genStyleBox.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& s : drum::genStyles (genGenreBox.getText()))
        genStyleBox.addItem (s, id++);
    genStyleBox.setSelectedItemIndex (0, juce::dontSendNotification);
}

void DrumOverlay::rebuildGenDrummers()
{
    genDrummerBox.clear (juce::dontSendNotification);
    genDrummerBox.addItem (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 none \xe2\x80\x94")), 1);
    const auto& drs = drum::genDrummers();
    const juce::String genre = genGenreBox.getText();
    for (int i = 0; i < (int) drs.size(); ++i)
    {
        const bool fits = drum::drummerFitsGenre (drs[(size_t) i].id, genre);
        auto label = juce::String (drs[(size_t) i].name);
        if (! fits) label += juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7 (other genre)"));
        genDrummerBox.addItem (label, i + 2);
        genDrummerBox.setItemEnabled (i + 2, fits);
    }
    genDrummerBox.setSelectedId (1, juce::dontSendNotification);
}

void DrumOverlay::fillBarWithGen (int globalBar, const juce::String& role, juce::uint32 seed)
{
    if (globalBar < 0 || globalBar >= engine.totalBars())
        return;

    drum::GenParams gp;
    gp.genre = genGenreBox.getText();
    gp.style = genStyleBox.getText();
    const int di = genDrummerBox.getSelectedId();
    const auto& drs = drum::genDrummers();
    if (di >= 2 && di - 2 < (int) drs.size())
        gp.drummer = drs[(size_t) (di - 2)].id;
    gp.role       = role;
    gp.complexity = (float) genComplex.getValue();
    gp.dynamics   = (float) genDynamics.getValue();
    gp.fillFreq   = (float) genFill.getValue();
    gp.num = engine.meterNum (globalBar);   // HONORS the bar's time signature
    gp.den = engine.meterDen (globalBar);
    gp.seed = seed;

    juce::uint8 pat[drum::numVoices][drum::maxStepsPerBar];
    drum::generateBar (gp, pat);
    engine.setBarPattern (pat, globalBar);

    auto nm = gp.style.substring (0, 1).toUpperCase() + gp.style.substring (1);
    if (role == "fill") nm = "Fill " + nm;
    engine.barNames[globalBar] = nm;

    // the humanize/swing parameters go to playback (atomics)
    const float h = (float) genHuman.getValue();
    engine.humanVel.store (juce::jlimit (0.0f, 1.0f, 0.15f + h * 0.5f));
    engine.humanTime.store (juce::jlimit (0.0f, 1.0f, h * 0.45f));
    engine.humanRR.store  (juce::jlimit (0.0f, 1.0f, 0.2f + h * 0.6f));
    engine.swingPct.store ((float) genSwing.getValue() * 60.0f);
}

void DrumOverlay::generateOne()
{
    const int g = selectedBar();
    fillBarWithGen (g, roleKey (resolveRole (g)), genSeedCtr++);
    refreshAll();
}

void DrumOverlay::generateAll()
{
    const int base = curSection * drum::barsPerSection;
    for (int i = 0; i < drum::barsPerSection; ++i)
        fillBarWithGen (base + i, roleKey (resolveRole (base + i)), genSeedCtr++);
    refreshAll();
}

juce::File DrumOverlay::userGroovesDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("PedalForge NAM").getChildFile ("compassos");
    dir.createDirectory();
    return dir;
}

void DrumOverlay::promptSaveBar()
{
    const int g = selectedBar();
    if (! engine.barUsed[g].load())
    {
        // nothing on the selected bar to save
        auto* w = new juce::AlertWindow (
            juce::String ("Nothing to save"),
            juce::String ("Select a bar with notes first, then use save to "
                          "store it in your reusable \"My bars\" library."),
            juce::MessageBoxIconType::NoIcon);
        w->addButton ("OK", 0, juce::KeyPress (juce::KeyPress::returnKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create (
            [w] (int) { delete w; }));
        return;
    }

    auto* w = new juce::AlertWindow (
        juce::String ("Save bar to My bars"),
        juce::String ("Name this bar. It appears under the \"My bars\" genre so you "
                      "can drag it onto any bar later."),
        juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", engine.barNames[g], "Name");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (juce::String ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, w] (int r)
        {
            if (r == 1)
            {
                const auto name = w->getTextEditorContents ("name").trim();
                if (name.isNotEmpty())
                {
                    saveUserGroove (name);
                    currentGenre = "MINE";   // show the result in My bars
                    rebuildGenreCol();
                    refreshAll();
                }
            }
            delete w;
        }));
}

void DrumOverlay::saveUserGroove (const juce::String& name)
{
    const int g = selectedBar();
    if (name.isEmpty() || ! engine.barUsed[g].load())
        return;

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("name", name);
    obj->setProperty ("bpm", (int) engine.bpm.load());
    obj->setProperty ("swing", (int) engine.swingPct.load());
    obj->setProperty ("pattern", engine.barToString (g));
    userGroovesDir()
        .getChildFile (juce::File::createLegalFileName (name) + ".json")
        .replaceWithText (juce::JSON::toString (juce::var (obj), true));

    engine.barNames[g] = name;
    refreshAll();
}

//==============================================================================
void DrumOverlay::LibRow::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    if (selected)
    {
        g.setColour (ui::accent.withAlpha (0.14f));
        g.fillRoundedRectangle (b, 6.0f);
    }
    else if (isMouseOver())
    {
        g.setColour (ui::glass());
        g.fillRoundedRectangle (b, 6.0f);
    }
    // marker: dot (groove) / orange dash (fill)
    if (fill)
    {
        g.setColour (ui::glowOrange);
        g.fillRoundedRectangle (3.0f, 5.0f, 2.5f, b.getHeight() - 10.0f, 1.2f);
    }
    else
    {
        g.setColour (ui::accentDark);
        g.fillEllipse (4.0f, b.getCentreY() - 2.0f, 4.0f, 4.0f);
    }
    g.setFont (ui::uiFont (12.0f, selected));
    g.setColour (selected ? ui::accent : ui::textDim);
    g.drawText (name, 14, 0, getWidth() - (deletable ? 30 : 18), getHeight(),
                juce::Justification::centredLeft);
    if (deletable)
    {
        g.setFont (ui::monoFont (11.0f));
        g.setColour (ui::textMuted);
        g.drawText (juce::CharPointer_UTF8 ("\xc3\x97"), getWidth() - 16, 0, 12, getHeight(),
                    juce::Justification::centred);
    }
}

void DrumOverlay::LibRow::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    if (deletable && e.getPosition().x > getWidth() - 20)
    {
        if (onDelete) onDelete();
        return;
    }
    if (onSelect) onSelect();
}

void DrumOverlay::LibRow::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6)
        return;
    if (deletable && e.getMouseDownPosition().x > getWidth() - 20)
        return;
    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragging = true;
        dnd->startDragging (dragId, this);
    }
}

//==============================================================================
void DrumOverlay::PreviewPane::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff0c0e11));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 2.0f);
    g.setColour (ui::border());
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 2.0f, 1.0f);

    if (! owner.selValid)
    {
        g.setFont (ui::monoFont (11.0f));
        g.setColour (ui::textMuted);
        g.drawText (juce::String ("select a groove from the list"),
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    g.setFont (ui::uiFont (14.0f, true));
    g.setColour (ui::textBright);
    g.drawText (owner.selName, 14, 8, getWidth() - 150, 20, juce::Justification::centredLeft);
    g.setFont (ui::monoFont (9.0f));
    g.setColour (owner.selFill ? ui::glowOrange : ui::accent);
    auto tag = owner.selFill ? juce::String ("FILL")
                             : "GROOVE" + (owner.selBpm > 0 ? juce::String (" \xc2\xb7 ")
                                                                  + juce::String (owner.selBpm) + " bpm"
                                                            : juce::String());
    if (owner.selNum != 4 || owner.selDen != 4)
        tag += juce::String (" \xc2\xb7 ") + juce::String (owner.selNum) + "/" + juce::String (owner.selDen);
    g.drawText (juce::String (juce::CharPointer_UTF8 (tag.toRawUTF8())), getWidth() - 150, 9, 140, 16,
                juce::Justification::centredRight);

    drawMiniBar (g, { 10.0f, 34.0f, (float) getWidth() - 20.0f,
                      (float) getHeight() - 66.0f },
                 owner.selPat, owner.selNum, owner.selDen,
                 true, true, true);

    g.setFont (ui::monoFont (9.0f));
    g.setColour (ui::textFaint);
    g.drawText (juce::String (juce::CharPointer_UTF8 (
                    "\xe2\xa0\xbf drag onto the staff, or use \"apply\"")),
                14, getHeight() - 26, getWidth() - 28, 14, juce::Justification::centredLeft);
}

void DrumOverlay::PreviewPane::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! owner.selValid || e.getDistanceFromDragStart() < 6)
        return;
    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragging = true;
        dnd->startDragging (owner.selDragId, this);
    }
}

//==============================================================================
// Optional grid (16 steps of the selected bar)
int DrumOverlay::GridView::cellWidth() const
{
    const int steps = juce::jmax (1, owner.engine.barSteps (owner.selectedBar()));
    const int beatGaps = (steps - 1) / 4;
    const int fixed = gridLabelW + 4 + (steps - 1) * gCellGap + beatGaps * gBeatGap;
    return juce::jlimit (10, gCellMaxW,
                         juce::jmax (10, (getWidth() - fixed) / steps));
}

int DrumOverlay::GridView::stepX (int step) const
{
    return gridLabelW + step * (cellWidth() + gCellGap) + (step / 4) * gBeatGap;
}

juce::Rectangle<int> DrumOverlay::GridView::cellBounds (int row, int step) const
{
    return { stepX (step), 16 + row * (gRowH + gRowGap), cellWidth(), gRowH };
}

void DrumOverlay::GridView::paint (juce::Graphics& g)
{
    auto& engine = owner.engine;
    const int bar = owner.selectedBar();
    const int steps = engine.barSteps (bar);

    g.setFont (ui::monoFont (9.0f));
    g.setColour (ui::textFaint);
    g.drawText ("GRID " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7"))
                    + " bar " + juce::String (bar + 1)
                    + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
                    + juce::String (engine.meterNum (bar)) + "/" + juce::String (engine.meterDen (bar)),
                0, 0, 260, 14, juce::Justification::centredLeft);
    for (int s = 0; s < steps; ++s)
    {
        g.setColour (s % 4 == 0 ? ui::textDim : ui::textMuted);
        const juce::String lbl = s % 4 == 0 ? juce::String (s / 4 + 1)
                                            : (s % 2 == 0 ? juce::String ("&")
                                                          : juce::String::fromUTF8 ("\xc2\xb7"));
        const auto cell = cellBounds (0, s);
        g.drawText (lbl, cell.getX(), 0, cell.getWidth(), 14,
                    juce::Justification::centred);
    }

    const int uiBar = engine.uiBar.load();
    const int playStep = uiBar == bar ? engine.uiStep.load() : -1;

    for (int row = 0; row < gridRows; ++row)
    {
        const int v = gridRowVoice[row];
        g.setFont (ui::uiFont (11.0f, true));
        g.setColour (ui::textDim);
        g.drawText (juce::String (juce::CharPointer_UTF8 (drum::voiceNames[v])),
                    0, 16 + row * (gRowH + gRowGap), gridLabelW - 10, gRowH,
                    juce::Justification::centredRight);

        for (int s = 0; s < steps; ++s)
        {
            auto r = cellBounds (row, s).toFloat();
            const int val = engine.barUsed[bar].load()
                                ? engine.pattern[bar][v][s].load() : 0;

            juce::Colour c = s % 4 == 0 ? juce::Colours::white.withAlpha (0.085f)
                                        : juce::Colours::white.withAlpha (0.05f);
            if (val == 1) c = ui::accentDark;
            else if (val == 2) c = ui::glowOrange;
            else if (val == 3) c = ui::accent.withAlpha (0.3f);
            g.setColour (c);
            g.fillRoundedRectangle (r, 5.0f);

            if (s == playStep)
            {
                g.setColour (ui::accent);
                g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.4f);
            }
        }
    }
}

void DrumOverlay::GridView::mouseDown (const juce::MouseEvent& e)
{
    const int steps = owner.engine.barSteps (owner.selectedBar());
    for (int row = 0; row < gridRows; ++row)
        for (int s = 0; s < steps; ++s)
            if (cellBounds (row, s).contains (e.getPosition()))
            {
                const int bar = owner.selectedBar();
                if (! owner.engine.barUsed[bar].load())
                {
                    juce::uint8 zero[drum::numVoices][drum::maxStepsPerBar] = {};
                    owner.engine.setBarPattern (zero, bar);
                    owner.engine.barNames[bar] = "new";
                    owner.rebuildBarHeads();
                }
                auto& cell = owner.engine.pattern[bar][gridRowVoice[row]][s];
                cell.store ((juce::uint8) ((cell.load() + 1) % 4));
                repaint();
                owner.scoreView.repaint();
                return;
            }
}

//==============================================================================
void DrumOverlay::refreshSourceRow()
{
    const bool hasVst = processor.hasDrumPlugin();
    const bool vstOn = engine.useVst.load() && hasVst;

    // single kit chip: "<name> · VST3 ▾" or "INTERNAL KIT ▾"
    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    const auto caret = juce::String::fromUTF8 (" \xe2\x96\xbe");
    kitChip.setButtonText (vstOn
        ? juce::String (juce::CharPointer_UTF8 ("\xf0\x9f\xa5\x81 "))
              + processor.getDrumPluginName().substring (0, 18) + dot + "VST3" + caret
        : "INTERNAL KIT" + caret);
    kitChip.getProperties().set ("chipActive", vstOn);
    kitChip.repaint();
}
