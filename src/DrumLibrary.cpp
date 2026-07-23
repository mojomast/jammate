#include "DrumEngine.h"

//==============================================================================
// Biblioteca de fábrica do módulo Bateria: grooves por gênero + viradas.
//
// spec: vozes separadas por '|' — K bumbo · S caixa · H chimbal · P pedal ·
// R ride · C crash · T tom1 · U tom2 · F surdo. Sufixos: '!' acento,
// '.' ghost. Faixa "0-14/2" = steps 0..14 pulando 2. TUDO é 1 compasso
// (16 steps) — grooves e viradas se arrastam igualmente para qualquer
// compasso da timeline (bpm 0 = mantém o andamento corrente).
//
// Grafias sem acento nos nomes usam \xNN (UTF-8) — exibir com
// CharPointer_UTF8 (armadilha MSVC/Latin-1).

namespace drum
{

const std::vector<Groove>& library()
{
    static const std::vector<Groove> lib = {
        // ================= ROCK =================
        { "ROCK", "B\xc3\xa1sico", 104, 0,
          "H:0-14/2,0!,8!|S:4!,12!|K:0,8,10" },
        { "ROCK", "B\xc3\xa1sico c/ crash", 104, 0,
          "H:2-14/2|C:0!|S:4!,12!,7.|K:0,8,10" },
        { "ROCK", "Meio-tempo", 88, 0,
          "H:0-14/2,0!|S:8!|K:0,10" },
        { "ROCK", "Meio-tempo 16", 92, 0,
          "H:0-15,0!,4!,8!,12!|S:8!|K:0,10,13" },
        { "ROCK", "Rock de ride", 118, 0,
          "R:0-14/2,0!,8!|C:0|S:4!,12!|K:0,8,10|P:0-12/4" },
        { "ROCK", "Anos 80", 112, 0,
          "H:0-14/2|S:4!,12!|K:0,6,8" },
        { "ROCK", "Colcheias no bumbo", 128, 0,
          "H:0-14/2,0!,8!|S:4!,12!|K:0,2,8,10" },
        { "ROCK", "Tribal (tons)", 116, 0,
          "F:0-14/2|T:2,10|K:0,4,8,12|S:4!,12!" },
        { "ROCK", "Power balada", 72, 0,
          "H:0-14/2|S:8!|K:0,6,10" },
        { "ROCK", "Shuffle rock", 132, 45,
          "H:0-14/2,0!,4!,8!,12!|S:4!,12!|K:0,8,11" },

        // ================= POP =================
        { "POP", "Pop colcheias", 100, 0,
          "H:0-14/2,0!,8!|S:4!,12!|K:0,6,8" },
        { "POP", "Disco", 118, 0,
          "H:0-12/4,2!,6!,10!,14!|S:4,12|K:0,4,8,12" },
        { "POP", "Dance-pop 16", 122, 0,
          "H:0-15,0!,8!|S:4!,12!|K:0,4,8,12" },
        { "POP", "Balada pop", 68, 0,
          "H:0-14/2|S:8!|K:0,10" },
        { "POP", "Motown (4 na caixa)", 118, 0,
          "S:0!,4!,8!,12!|H:0-14/2|K:0,8" },
        { "POP", "Pop punk", 160, 0,
          "H:0-14/2,0!,4!,8!,12!|S:4!,12!|K:0,2,6,8,10,14" },
        { "POP", "S\xc3\xadncope pop", 104, 0,
          "H:0-14/2|S:4!,12!|K:0,7,10" },

        // ================= PUNK =================
        { "PUNK", "Punk b\xc3\xa1sico", 172, 0,
          "H:0-14/2,0!,4!,8!,12!|S:4!,12!|K:0,2,8,10" },
        { "PUNK", "D-beat", 190, 0,
          "H:0-14/2|S:4!,12!|K:0,3,8,11" },
        { "PUNK", "Skate punk", 200, 0,
          "H:0-12/4|S:2!,6!,10!,14!|K:0,4,8,12" },
        { "PUNK", "Street/Oi", 140, 0,
          "H:0-14/2,0!|S:4!,12!|K:0,8" },

        // ================= METAL =================
        { "METAL", "Double kick", 150, 0,
          "R:0-14/2,0!,4!,8!,12!|C:0!|S:4!,12!|K:0-15" },
        { "METAL", "Thrash", 184, 0,
          "H:0-14/2|S:2!,6!,10!,14!|K:0,4,8,12" },
        { "METAL", "Half-time pesado", 96, 0,
          "H:0-14/2,0!|S:8!|K:0,3,6,10,13" },
        { "METAL", "Blast beat", 200, 0,
          "S:0-14/2!|K:0-14/2|R:0-14/2" },
        { "METAL", "Breakdown", 92, 0,
          "C:0!,8!|S:4!,12!|K:0,2,3,8,11" },
        { "METAL", "Groove metal", 108, 0,
          "H:0-14/2,0!,4!,8!,12!|S:4!,12!|K:0,1,10,11" },
        { "METAL", "Galope duplo", 140, 0,
          "K:0,3,4,7,8,11,12,15|R:0-12/4!|S:4!,12!" },

        // ================= FUNK =================
        { "FUNK", "Funk 16", 96, 0,
          "H:0-15,0!,4!,8!,12!|S:4!,12!,7.,10.,15.|K:0,3,10" },
        { "FUNK", "Ghost groove", 92, 8,
          "H:0-15,0!,8!|S:4!,12!,2.,7.,11.|K:0,5,10" },
        { "FUNK", "The One (JB)", 108, 0,
          "H:0-14/2,0!|S:4!,12!,10.|K:0,2,11" },
        { "FUNK", "Chimbal aberto", 102, 0,
          "H:0-15,2!,6!,10!,14!|S:4!,12!|K:0,3,8,11" },
        { "FUNK", "New Orleans", 98, 20,
          "H:0-14/2|S:0.,3,4!,7.,11,12!,14.|K:0,6,10" },
        { "FUNK", "Boogaloo", 112, 15,
          "R:0-14/2|S:4!,7.,12!|K:0,3,8,10" },
        { "FUNK", "Funk rock", 106, 0,
          "H:0-15,0!,4!,8!,12!|S:4!,12!,15.|K:0,2,7,10" },

        // ================= BLUES =================
        { "BLUES", "Shuffle", 120, 55,
          "H:0-14/2,0!,4!,8!,12!|S:4!,12!|K:0,8" },
        { "BLUES", "Slow blues", 60, 60,
          "R:0-14/2,0!,8!|S:4!,12!|K:0,10|P:4,12" },
        { "BLUES", "Texas shuffle", 126, 55,
          "R:0-14/2,0!,4!,8!,12!|S:4!,12!,2.,6.,10.,14.|K:0,4,8,12" },
        { "BLUES", "Rumba blues", 104, 30,
          "K:0,3,8,11|S:4!,12!|H:0-14/2" },
        { "BLUES", "Vassourinha", 72, 40,
          "S:0.,2.,4.,6.,8.,10.,12.,14.|R:0,8|K:0." },

        // ================= JAZZ =================
        { "JAZZ", "Swing (ride)", 140, 60,
          "R:0!,4,6,8!,12,14|P:4,12|K:0.,8.|S:6.,10." },
        { "JAZZ", "Swing m\xc3\xa9""dio", 120, 55,
          "R:0!,4,6,8!,12,14|P:4,12|S:3.,7.,11.|K:0." },
        { "JAZZ", "Bebop", 220, 60,
          "R:0,4,6,8,12,14|P:4,12|S:2.,9.,13.|K:7." },
        { "JAZZ", "Two-feel", 110, 55,
          "R:0!,4,8!,12|P:4,12|K:0.,8." },
        { "JAZZ", "Balada (brushes)", 70, 40,
          "S:0.,2.,4.,6.,8.,10.,12.,14.|R:0,8|K:0." },

        // ================= COUNTRY =================
        { "COUNTRY", "Train beat", 132, 0,
          "S:0,2,4!,6,8,10,12!,14|K:0,8|H:0-12/4" },
        { "COUNTRY", "Two-step", 108, 0,
          "H:0-14/2|S:4!,12!|K:0,8" },
        { "COUNTRY", "Country shuffle", 100, 50,
          "H:0-14/2|S:4!,12!|K:0,8,10" },

        // ================= BRASIL =================
        { "BRASIL", "Samba (kit)", 100, 0,
          "H:0-15,0!,3!,6!,10!,13!|K:0,4!,8,12!|S:2.,5.,10.,15." },
        { "BRASIL", "Samba cruzado", 104, 0,
          "R:0,1,3,4,6,8,9,11,12,14|K:0,4!,8,12!|S:2.,7.,13." },
        { "BRASIL", "Bossa nova (1\xc2\xba)", 126, 0,
          "S:0,6,12|K:0,6,8,14|P:4,12|H:0-14/2." },
        { "BRASIL", "Bossa nova (2\xc2\xba)", 126, 0,
          "S:4,10|K:0,6,8,14|P:4,12|H:0-14/2." },
        { "BRASIL", "Bai\xc3\xa3o", 110, 0,
          "H:0-14/2,0!,8!|K:0!,7,8!,15|S:4,12" },
        { "BRASIL", "Xote", 92, 25,
          "H:0-14/2|K:0,6,8,14|S:4!,12!" },
        { "BRASIL", "Frevo", 176, 0,
          "H:0-12/4|S:2,4!,7,10,12!,15|K:0,8" },
        { "BRASIL", "Marcha/ax\xc3\xa9", 138, 0,
          "K:0,4,8,12|S:2,6!,10,14!|H:0-14/2" },
        { "BRASIL", "Samba-funk", 104, 0,
          "H:0-15,0!,4!,8!,12!|S:3.,4!,7.,10.,12!|K:0,6,8,14" },

        // ================= LATINO =================
        { "LATINO", "Bolero", 92, 0,
          "R:0-14/2|S:4,8.,12|K:0,8|U:6,14" },
        { "LATINO", "Cha-cha", 116, 0,
          "R:0-14/2|S:4,12|K:0,8|U:7,15" },
        { "LATINO", "Songo", 106, 0,
          "R:0,3,6,8,11,14|S:2.,5,7.,10.,13|K:4,12|T:9" },
        { "LATINO", "Reggaeton", 95, 0,
          "K:0,4,8,12|S:3!,6!,11!,14!|H:0-14/2" },
        { "LATINO", "Cumbia", 100, 0,
          "K:0,4,8,12|S:4,12|H:2,6,10,14|R:0-12/4" },

        // ================= REGGAE/SKA =================
        { "REGGAE/SKA", "One drop", 76, 12,
          "H:0-14/2,4!,12!|K:8!|S:8" },
        { "REGGAE/SKA", "Steppers", 80, 0,
          "H:0-12/4,2!,6!,10!,14!|K:0,4,8,12|S:8!" },
        { "REGGAE/SKA", "Rockers", 74, 10,
          "H:0-14/2,0!|K:0,8!|S:8" },
        { "REGGAE/SKA", "Ska", 152, 0,
          "H:2,6,10,14|S:4!,12!|K:0,8" },

        // ================= HIP-HOP =================
        { "HIP-HOP", "Boom bap", 90, 15,
          "H:0-14/2,0!|S:4!,12!|K:0,7,10" },
        { "HIP-HOP", "Trap (half-time)", 70, 0,
          "H:0-15|S:8!|K:0,3,11" },
        { "HIP-HOP", "Lo-fi", 82, 20,
          "H:0-14/2.|S:4!,12!|K:0,7,9" },
        { "HIP-HOP", "West coast", 94, 0,
          "H:0-14/2,0!,8!|S:4!,12!|K:0,3,8,10" },
        { "HIP-HOP", "Drill", 140, 10,
          "H:0,2,4,5,8,10,12,13|S:6!,14!|K:0,4,11" },

        // ================= ELETRÔNICO =================
        { "ELETR\xc3\x94NICO", "House", 124, 0,
          "K:0,4,8,12|H:2!,6!,10!,14!|S:4,12|P:0-12/4" },
        { "ELETR\xc3\x94NICO", "Techno", 132, 0,
          "K:0,4,8,12|H:2,6,10,14|R:0-12/4.|S:4" },
        { "ELETR\xc3\x94NICO", "Drum'n'bass", 174, 0,
          "K:0,10|S:4!,12!|H:0-15,0!,4!,8!,12!" },
        { "ELETR\xc3\x94NICO", "Breakbeat", 130, 0,
          "K:0,10|S:4!,12!,7.,14|H:0-14/2,0!" },
        { "ELETR\xc3\x94NICO", "Synthwave", 108, 0,
          "K:0,4,8,12|S:4!,12!|H:0-15,2!,6!,10!,14!" },

        // ================= WORLD =================
        { "WORLD", "Afrobeat", 112, 0,
          "H:0-15,0!,3!,6!,9!,12!|K:0,6,10|S:2.,4,7.,11,13." },
        { "WORLD", "Motorik", 120, 0,
          "H:0-14/2|K:0,4,6,8,12,14|S:4!,12!" },
        { "WORLD", "Surf", 160, 0,
          "S:0,2,4!,6,8,10,12!,14|K:0,8|H:0-12/4" },
        { "WORLD", "Bo Diddley", 106, 0,
          "F:0!,3,6,10!,12|S:2.,8.,14.|K:0,8" },

        // ================= VIRADAS (1 compasso; entram no 2º) =================
        { "VIRADA", "S\xc3\xb3 no 4\xc2\xba tempo", 0, 0,
          "S:12,13,14,15!" },
        { "VIRADA", "Dois \xc3\xbaltimos tempos", 0, 0,
          "S:8,10,11|T:12,13|F:14,15!" },
        { "VIRADA", "Semicolcheias na caixa", 0, 0,
          "S:0-15,0!,4!,8!,12!" },
        { "VIRADA", "Meio compasso", 0, 0,
          "S:8,9,10,11|T:12,13|F:14,15!" },
        { "VIRADA", "Caixa e tons", 0, 0,
          "S:0,1,2,3,4,5,6,7|T:8,9,10,11|F:12,13,14,15!" },
        { "VIRADA", "Descida de tons", 0, 0,
          "S:0,2,4,6|T:8,10|U:12,13|F:14,15!" },
        { "VIRADA", "Subida (pra cima)", 0, 0,
          "F:0,1,2,3|U:4,5,6,7|T:8,9,10,11|S:12,13,14,15!" },
        { "VIRADA", "Virada do 3\xc2\xba tempo", 0, 0,
          "H:0-6/2,0!|K:0|S:4!,8,10,11|T:12,13|F:14,15!" },
        { "VIRADA", "Sexteto", 0, 40,
          "S:0,1,2,6,7,8,12,13,14" },
        { "VIRADA", "Bumbo e caixa", 0, 0,
          "S:0,3,6,9,12!|K:1,2,4,5,7,8,10,11,13,14,15" },
        { "VIRADA", "Rufo (press)", 0, 0,
          "S:0.,1.,2.,3.,4.,5.,6.,7.,8.,9.,10.,11.,12!,14!" },
        { "VIRADA", "Tom groove", 0, 0,
          "T:0,1|S:2,3|U:4,5|S:6,7|F:8,9|S:10,11|K:12|C:14!" },
        { "VIRADA", "Herta", 0, 0,
          "S:0,1,2,4,5,6,8,9,10,12!,13,14" },
        { "VIRADA", "Linear", 0, 0,
          "K:0,3,6|S:1,4,7,10,13|H:2,5,8,11,14|F:15!" },
        { "VIRADA", "Paradiddle", 0, 0,
          "S:0!,2,3,5,7|T:4!,6|U:8!,10,11|F:12!,14,15" },
        { "VIRADA", "Blast final", 0, 0,
          "S:8-14/2!|K:9-15/2" },
        { "VIRADA", "Shuffle fill", 0, 50,
          "S:0,2,4!,8,10,12!|F:14,15" },
        { "VIRADA", "Quebra e retoma", 0, 0,
          "C:0!|K:0|S:12,13,14,15!" },
        { "VIRADA", "Tumbao de tons", 0, 0,
          "T:0,4|U:2,6|F:8,12,14|S:10,15!" },
        { "VIRADA", "Samba virada", 0, 0,
          "S:0,1,3,4,5,7,8,9,11,12!,13,15|K:0,4,8,12" },
        { "VIRADA", "Funk chop", 0, 0,
          "S:0!,3,6.,7,9,10.,12!,14|K:1,8" },
        { "VIRADA", "Powerfill", 0, 0,
          "S:0!,1,2,3|T:4!,5,6,7|U:8!,9,10,11|F:12!,13,14,15" },
        { "VIRADA", "Contratempos", 0, 0,
          "S:2,6,10,14|K:0,4,8,12" },
        { "VIRADA", "Crescendo", 0, 0,
          "S:0.,2.,4.,6.,8,10,12!,13!,14!,15!" },
    };
    return lib;
}

juce::StringArray genres()
{
    juce::StringArray out;
    for (const auto& g : library())
        if (juce::String (g.genre) != "VIRADA")
            out.addIfNotAlreadyThere (juce::String (juce::CharPointer_UTF8 (g.genre)));
    return out;
}

} // namespace drum
