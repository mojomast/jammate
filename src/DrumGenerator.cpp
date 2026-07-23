#include "DrumGenerator.h"

namespace drum
{
namespace
{
    // RNG barato e determinístico (xorshift) — semeado por chamada
    struct Rng
    {
        juce::uint32 s;
        explicit Rng (juce::uint32 seed) : s (seed ? seed : 0x9E3779B9u) {}
        juce::uint32 next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        float f() { return (float) (next() >> 8) / 16777216.0f; }   // [0,1)
        bool chance (float p) { return f() < p; }
        int range (int lo, int hi) { return hi <= lo ? lo : lo + (int) (next() % (juce::uint32) (hi - lo + 1)); }
    };

    // agrupamento da métrica (compostos de 3 em 3) — igual à pauta
    void groupsFor (int num, int den, int groups[8], int& n)
    {
        n = 0;
        if (den == 8 && num % 3 == 0)      for (int i = 0; i < num / 3 && n < 8; ++i) groups[n++] = 6;
        else if (den == 8 && num == 7)     { int g[] = { 4, 4, 6 }; for (int x : g) groups[n++] = x; }
        else if (den == 8 && num == 5)     { int g[] = { 4, 6 };    for (int x : g) groups[n++] = x; }
        else if (den == 4)                 for (int i = 0; i < num && n < 8; ++i) groups[n++] = 4;
        else if (den == 2)                 for (int i = 0; i < num && n < 8; ++i) groups[n++] = 8;
        else { int steps = stepsForMeter (num, den), rem = steps;
               while (rem >= 4 && n < 8) { groups[n++] = 4; rem -= 4; }
               if (rem > 0 && n < 8) groups[n++] = rem; }
        if (n == 0) { groups[0] = stepsForMeter (num, den); n = 1; }
    }

    inline bool eq (const juce::String& a, const char* b) { return a.equalsIgnoreCase (b); }
}

//==============================================================================
int generateBar (const GenParams& p, juce::uint8 out[numVoices][maxStepsPerBar])
{
    const int steps = stepsForMeter (p.num, p.den);
    for (int v = 0; v < numVoices; ++v)
        for (int s = 0; s < maxStepsPerBar; ++s)
            out[v][s] = 0;

    Rng rng (p.seed);
    auto set = [&] (int v, int s, juce::uint8 val) { if (s >= 0 && s < steps) out[v][s] = val; };
    auto has = [&] (int v, int s) { return s >= 0 && s < steps && out[v][s] != 0; };

    int groups[8], ng; groupsFor (p.num, p.den, groups, ng);
    int gStart[8] = {}; for (int i = 1; i < ng; ++i) gStart[i] = gStart[i - 1] + groups[i - 1];

    // pulsos ("beats"): em compostos são os inícios de grupo; senão semínimas
    int beats[32], nb = 0;
    if (p.den == 8) { for (int i = 0; i < ng; ++i) beats[nb++] = gStart[i]; }
    else { const int per = (p.den == 2 ? 8 : 4); for (int s = 0; s < steps; s += per) beats[nb++] = s; }

    const bool mMetal = eq (p.genre, "METAL"), mRock = eq (p.genre, "ROCK");
    const bool mJazz  = eq (p.genre, "JAZZ"),  mFunk = eq (p.genre, "FUNK");
    const bool rChorus = eq (p.role, "chorus"), rBreak = eq (p.role, "breakdown");
    const bool rBridge = eq (p.role, "bridge"), rFill = eq (p.role, "fill");

    const float cx = juce::jlimit (0.0f, 1.0f, p.complexity);
    const float dy = juce::jlimit (0.0f, 1.0f, p.dynamics);

    //========================= VIRADA (papel = fill) =========================
    if (rFill)
    {
        set (crash, 0, 2); set (kick, 0, 1);
        const int sp = (cx > 0.6f ? 1 : 2);
        for (int s = 0; s < steps; s += sp)
        {
            const float t = (float) s / (float) juce::jmax (1, steps - 1);
            if      (t < 0.30f) set (tom1, s, 1);
            else if (t < 0.55f) set (tom2, s, 1);
            else if (t < 0.80f) set (floorTom, s, 1);
            else                set (snare, s, s == steps - 1 ? 2 : 1);
        }
        // Hoglan/Dee: rulo de bumbo no fim
        if (eq (p.drummer, "hoglan") || eq (p.drummer, "dee"))
            for (int s = juce::jmax (0, steps - 4); s < steps; ++s) set (kick, s, 1);
        return steps;
    }

    //========================= TIMEKEEPING (prato) ==========================
    int cym = hat; int sub = 2; bool halfTime = false;
    if (rChorus) cym = ride;
    if (mJazz) cym = ride;
    if (mMetal && eq (p.style, "doom"))  { cym = ride; halfTime = true; }
    if (mMetal && rBreak)                 halfTime = true;
    if (rBreak && !mMetal)                halfTime = true;
    if (mFunk) sub = (cx > 0.5f ? 1 : 2);
    if (mRock && !rChorus && cx > 0.62f) sub = 1;   // hats em 16 às vezes

    if (mJazz)
    {
        // swing: ride "spang-a-lang" — semínima + a "e" dos tempos pares
        for (int i = 0; i < nb; ++i)
        {
            set (ride, beats[i], i == 0 ? 2 : 1);
            if (i % 2 == 1) set (ride, beats[i] + 3, 1);          // o "a"
            set (hatPedal, beats[i], (i % 2 == 1) ? 1 : 0);       // chimbal em 2 e 4
        }
    }
    else
    {
        for (int s = 0; s < steps; s += sub) set (cym, s, 1);
        for (int i = 0; i < ng; ++i) if (has (cym, gStart[i])) set (cym, gStart[i], 2);
    }

    //============================= CAIXA (backbeat) =========================
    if (!mJazz)
    {
        if (halfTime)            set (snare, beats[juce::jmax (1, nb / 2)], 2);
        else if (p.den == 8)     for (int i = 1; i < ng; ++i) set (snare, gStart[i], 2);
        else                     for (int i = 1; i < nb; i += 2) set (snare, beats[i], 2);
    }

    //============================== BUMBO ===================================
    auto kickBeats = [&] { for (int i = 0; i < nb; ++i) set (kick, beats[i], 1); };
    auto kickGallop = [&] { for (int i = 0; i < nb; ++i) { set (kick, beats[i], 1); set (kick, beats[i] + 3, 1); } };
    auto kickDouble = [&] (int step) { for (int s = 0; s < steps; s += step) set (kick, s, 1); };

    if (mMetal)
    {
        if      (eq (p.style, "death"))   { kickDouble (cx > 0.72f ? 1 : 2);        // double bass / blast
                                            for (int s = 2; s < steps; s += 4) set (snare, s, 1); } // caixa offbeat (blast)
        else if (eq (p.style, "thrash"))  kickGallop();
        else if (eq (p.style, "power"))   { kickBeats(); if (cx > 0.5f) for (int i = 0; i < nb; ++i) set (kick, beats[i] + 2, 1); }
        else if (eq (p.style, "doom"))    { set (kick, 0, 2); if (nb > 2) set (kick, beats[nb / 2], 2); }
        else if (eq (p.style, "progressive")) { for (int i = 0; i < ng; ++i) { set (kick, gStart[i], 1);
                                              if (rng.chance (0.5f + cx * 0.4f)) set (kick, gStart[i] + 1, 1);
                                              if (rng.chance (cx * 0.6f)) set (kick, gStart[i] + 3, 1); } }
        else if (eq (p.style, "breakdown")) { set (kick, 0, 2); set (kick, 6, 1); if (steps > 10) set (kick, 10, 1); }
        else /* heavy */                  { for (int i = 0; i < nb; ++i) { set (kick, beats[i], 1);
                                              if (i + 1 < nb && rng.chance (cx * 0.5f)) set (kick, beats[i] + 3, 1); } }
    }
    else if (mRock)
    {
        if      (eq (p.style, "punk"))    kickBeats();
        else if (eq (p.style, "hard"))    { set (kick, 0, 1); for (int i = 0; i < nb; ++i) if (i % 2 == 0) { set (kick, beats[i], 1); set (kick, beats[i] + 3, 1); } }
        else /* classic/pop/alt/prog */   { for (int i = 0; i < nb; i += 2) { set (kick, beats[i], 1);
                                              if (rng.chance (0.4f + cx * 0.4f)) set (kick, beats[i] + 3, 1); } }
    }
    else if (mFunk)
    {
        set (kick, 0, 2);                                    // "the one"
        for (int i = 0; i < nb; ++i) if (rng.chance (0.35f + cx * 0.4f)) set (kick, beats[i] + (rng.chance (0.5f) ? 2 : 3), 1);
    }
    else if (mJazz)
    {
        for (int i = 0; i < nb; ++i) set (kick, beats[i], 1);   // feathered
        if (cx > 0.6f) set (snare, beats[juce::jmin (nb - 1, 1)] + 2, 3); // comping ghost
    }
    else kickBeats();

    //====================== GHOSTS + ACENTOS (complexidade/dinâmica) =======
    if (!rFill && !mJazz && cx > 0.35f)
        for (int i = 0; i < nb; ++i)
        {
            const int a = beats[i] + 1, b = beats[i] + 3;
            if (!has (snare, a) && !has (kick, a) && rng.chance ((cx - 0.2f) * 0.5f)) set (snare, a, 3);
            if (!has (snare, b) && !has (kick, b) && rng.chance ((cx - 0.2f) * 0.5f)) set (snare, b, 3);
        }
    if (dy > 0.4f) { set (kick, 0, has (kick, 0) ? 2 : out[kick][0]); }

    //============================ REFRÃO: prato de ataque ==================
    if (rChorus) { set (crash, 0, 2); if (has (hat, 0)) set (hat, 0, 0); }
    if (rBridge && has (hat, 0)) for (int s = 1; s < steps; s += 2) set (hat, s, 0); // ponte mais seca

    //=================== mini-virada eventual (fillFreq) ===================
    if (!rFill && nb >= 2 && rng.chance (juce::jlimit (0.0f, 1.0f, p.fillFreq)))
    {
        const int b0 = beats[nb - 1];
        for (int s = b0; s < steps; ++s) { set (hat, s, 0); set (snare, s, 0); }
        set (tom1, b0, 1); set (tom2, b0 + 1, 1);
        set (floorTom, b0 + 2, 1); set (snare, steps - 1, 2);
    }

    //============================ MODS de baterista ========================
    const auto& dr = p.drummer;
    if (eq (dr, "hoglan"))            // double bass mecânico + flurry no fim
    {
        for (int s = 0; s < steps; s += 2) if (!has (kick, s)) if (rng.chance (0.6f)) set (kick, s, 1);
        for (int s = juce::jmax (0, steps - 3); s < steps; ++s) set (kick, s, 1);
    }
    else if (eq (dr, "bonham"))       // tercinas/fantasmas antes do backbeat
    {
        for (int i = 1; i < nb; i += 2) { const int g = beats[i] - 1; if (!has (snare, g) && !has (kick, g)) set (snare, g, 3); }
        if (cx > 0.5f) for (int i = 0; i < nb; ++i) set (kick, beats[i] + 2, out[kick][juce::jlimit(0,steps-1,beats[i]+2)] ? out[kick][beats[i]+2] : (juce::uint8)1);
    }
    else if (eq (dr, "weckl"))        // linear: nada simultâneo (mãos vs pés)
    {
        for (int s = 0; s < steps; ++s) if (has (kick, s) && has (snare, s)) set (snare, s, 0);
        for (int s = 0; s < steps; ++s) if (has (kick, s) && has (cym, s) && rng.chance (0.6f)) set (cym, s, 0);
    }
    else if (eq (dr, "chambers") || eq (dr, "porcaro"))  // ghosts de 16 no bolso
    {
        for (int i = 0; i < nb; ++i) { const int a = beats[i] + 3; if (!has (snare, a) && !has (kick, a)) set (snare, a, 3); }
    }
    else if (eq (dr, "roeder"))       // minimalista: rareia o prato
    {
        for (int s = 0; s < steps; s += 2) if (has (cym, s) && rng.chance (0.4f)) set (cym, s, 0);
    }
    else if (eq (dr, "dee"))          // velocidade: bumbo extra + acento torto
    {
        for (int i = 0; i < nb; ++i) if (rng.chance (0.5f)) set (kick, beats[i] + 2, 1);
    }

    return steps;
}

//==============================================================================
juce::StringArray genGenres() { return { "METAL", "ROCK", "JAZZ", "FUNK" }; }

juce::StringArray genStyles (const juce::String& genre)
{
    if (genre.equalsIgnoreCase ("METAL")) return { "heavy", "death", "power", "progressive", "thrash", "doom", "breakdown" };
    if (genre.equalsIgnoreCase ("ROCK"))  return { "classic", "blues", "alternative", "progressive", "punk", "hard", "pop" };
    if (genre.equalsIgnoreCase ("JAZZ"))  return { "swing", "bebop", "fusion", "latin", "ballad", "hard_bop", "contemporary" };
    if (genre.equalsIgnoreCase ("FUNK"))  return { "classic", "pfunk", "shuffle", "new_orleans", "fusion", "minimal", "heavy" };
    return { "default" };
}

const std::vector<GenDrummer>& genDrummers()
{
    static const std::vector<GenDrummer> d = {
        { "bonham",   "John Bonham",     "Tercinas \xc2\xb7 atr\xc3\xa1s do tempo" },
        { "porcaro",  "Jeff Porcaro",    "Half-time shuffle \xc2\xb7 est\xc3\xba" "dio" },
        { "weckl",    "Dave Weckl",      "Linear \xc2\xb7 fusion" },
        { "chambers", "Dennis Chambers", "Pocket de funk \xc2\xb7 chops" },
        { "roeder",   "Jason Roeder",    "Sludge atmosf\xc3\xa9rico \xc2\xb7 minimal" },
        { "dee",      "Mikkey Dee",      "Velocidade \xc2\xb7 pot\xc3\xaancia" },
        { "hoglan",   "Gene Hoglan",     "Precis\xc3\xa3o mec\xc3\xa2nica \xc2\xb7 blast" },
    };
    return d;
}

bool drummerFitsGenre (const juce::String& id, const juce::String& genre)
{
    const juce::String g = genre.toLowerCase();
    auto in = [&] (std::initializer_list<const char*> gs) { for (auto* x : gs) if (g == x) return true; return false; };
    if (id == "bonham")   return in ({ "rock", "metal", "blues" });
    if (id == "hoglan")   return in ({ "metal" });
    if (id == "weckl")    return in ({ "jazz", "funk", "rock" });
    if (id == "chambers") return in ({ "funk", "jazz", "rock" });
    if (id == "porcaro")  return in ({ "rock", "pop", "funk", "jazz" });
    if (id == "roeder")   return in ({ "metal" });
    if (id == "dee")      return in ({ "metal" });
    return true;
}

} // namespace drum
