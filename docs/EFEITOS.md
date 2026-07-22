# 🎛️ Efeitos — fontes e referências

Cada efeito do GuitarRig NAM tem três "camadas" de origem, documentadas aqui para
nunca perder a rastreabilidade:

1. **Inspiração clássica** — o pedal/equipamento cujo caráter a variação persegue;
2. **Fonte de estudo** — o projeto open source (em [`references/`](../references/README.md))
   usado para estudar topologia, vozeamento e faixas de parâmetros;
3. **Implementação** — o que roda de fato no código (motor `juce::dsp`, biquads
   RBJ próprios ou DSP escrito para este projeto).

> **Honestidade de engenharia**: com exceção do que está marcado como "adaptado",
> nenhum código foi copiado dos projetos de referência — as implementações são
> próprias, escritas sobre `juce::dsp` e biquads RBJ ([Audio EQ Cookbook de
> Robert Bristow-Johnson](https://www.w3.org/TR/audio-eq-cookbook/)). Os projetos
> GPL/LGPL foram usados **somente como leitura** (ver nota de licenças no
> [`references/README.md`](../references/README.md)).

## Noise Gate

| | |
|---|---|
| Inspiração | gates de pedalboard com hold (ISP Decimator-like) |
| Estudo | `references/ToobAmp` (GxNoiseGate/gate do ToobAmp) |
| Implementação | própria: follower de envelope + histerese de 6 dB + hold + release (`SmartGate`) |

## Compressor (variações no cartão)

Motor: `juce::dsp::Compressor` + compressão paralela (BLEND) própria.
Estudo: `references/lsp-plugins` (dinâmica "studio") e `references/rkrlv2`.

| Variação | Inspiração clássica |
|---|---|
| **Dyna** | MXR Dyna Comp (OTA, squish agressivo) |
| **Optical** | Teletronix LA-2A (opto, lento e musical) |
| **Studio** | VCA de rack (dbx 160/SSL bus, transparente) |
| **Squeezer** | Dan Armstrong Orange Squeezer (ataque rápido vintage) |

## Drive (variações no cartão)

Topologia própria: HP → waveshaper → tone LP → pós-filtro.
Estudo: `references/BYOD` (circuitos modelados), `references/guitarix` e
`references/GxPlugins.lv2` (vozeamentos); **Valve** adaptado da ideia do
Airwindows *Tube* (`references/airwindows`, MIT).

| Variação | Inspiração clássica | Assinatura no código |
|---|---|---|
| **Screamer** | Ibanez Tube Screamer | HP 300 Hz + corcova de médios 700 Hz, `tanh` |
| **Blues** | Marshall Blues Breaker | quase flat, clip `v/(1+abs v)` |
| **Distortion** | ProCo RAT / Boss DS-1 | scoop leve 800 Hz, clip duro |
| **Fuzz** | Fuzz Face | HP 80 Hz, clip assimétrico (bias) |
| **Boost** | Xotic EP Booster | quase linear, satura só no extremo |
| **Heavy Fuzz** | Electro-Harmonix Big Muff | scoop 1 kHz, clip limitado, sustain |
| **Valve** | Airwindows Tube (MIT) | assimetria leve → harmônicos pares, presença 1.2 kHz |
| **Metal** | Boss Metal Zone (via Guitarix) | ganho 4×, scoop profundo −6 dB @ 650 Hz |

## Pré-EQ

Vozeamento pré-amp (muda como o capture satura). Estudo: `references/ToobAmp`
(tone shaping pré-amp). Implementação: shelves/peak RBJ próprios.

## Pitch (variações no cartão)

Shifter granular de delay-line com 2 cabeças e crossfade sen/cos (técnica
clássica descrita em DAFX/Zölzer). Estudo de uso musical: `references/rkrlv2`
(harmonizer herdado do rakarrack/ZynAddSubFX). Implementação própria, validada
headless (440 Hz → 220/660/880 Hz).

| Variação | Intervalo (razão) |
|---|---|
| **Oitava ↓** | 0.5 |
| **Oitava ↑** | 2.0 |
| **Quinta** | 1.5 |
| **Quarta** | 4/3 |
| **Detune** | 1.007 (~12 cents, engrossa tipo doubler) |

## Modulação (variações no cartão)

Estudo: `references/ToobAmp` e `references/GxPlugins.lv2` (modulações),
`references/airwindows` (rotary/vibrato).

| Variação | Inspiração clássica | Implementação |
|---|---|---|
| **Chorus** | Boss CE-2 | `juce::dsp::Chorus` (delay 7 ms) |
| **Phaser** | MXR Phase 90 | `juce::dsp::Phaser` |
| **Flanger** | Electric Mistress | `juce::dsp::Chorus` com delay 1.8 ms + feedback 0.7 |
| **Tremolo H.** | Fender brownface (tremolo harmônico) | próprio: bandas LP/HP em anti-fase |
| **Vibrato** | Boss VB-2 | chorus 100% wet (só a afinação ondula) |
| **Rotary** | Leslie 122 | próprio: doppler (chorus) + AM por bandas, corneta 2.7× o tambor |

## Delay (variações no cartão)

Motor: `juce::dsp::DelayLine` + TAP tempo/subdivisões e trails próprios.
Estudo: `references/airwindows` (tape/coloração analógica) e
`references/guitarix` (echo BBD).

| Variação | Inspiração clássica | Assinatura no código |
|---|---|---|
| **Digital** | linha digital limpa | feedback puro |
| **Analog** | EHX Memory Man (BBD) | LP 3 kHz + HP 150 Hz + `tanh` no feedback |
| **Tape** | Echoplex/Space Echo | LP 4.5 kHz + wobble 0.9 Hz no tempo |
| **Ping-Pong** | delays estéreo modernos | repetições alternam L/R (via `stereoExtra`) |
| **Ducking** | TC 2290 dynamic delay | follower abaixa as repetições enquanto você toca |

## Reverb (variações no cartão)

Motor: `juce::Reverb` (Freeverb/Schroeder). Estudo de vozeamentos:
`references/dragonfly-reverb` (Hall/Room/Plate) e `references/GxPlugins.lv2`
(spring).

| Variação | Inspiração clássica | Assinatura no código |
|---|---|---|
| **Hall** | halls de estúdio | room grande, damping médio |
| **Room** | ambiência curta | room pequeno, damping alto, width 0.7 |
| **Plate** | EMT 140 | denso, damping baixíssimo |
| **Spring** | tanque de molas Fender | bandpass 400 Hz–5 kHz no wet |
| **Shimmer** | Valhalla Shimmer / Eno-Lanois | oitava acima (60%) na entrada do reverb, usa o mesmo shifter do cartão Pitch |

## EQ / Tone stack do amp

Biquads RBJ próprios (Audio EQ Cookbook, S=1 nos shelves). Tone stack B/M/T/
Presence pós-NAM com curso de ±12 dB (±9 dB presence).

## Looper

Conceito de pedal looper padrão (TC Ditto-like): REC → fecha e toca → overdub,
export WAV. Implementação própria (buffer pré-alocado, estados via atomics).

## Limiter

Motor: `juce::dsp::Limiter` (brickwall). Papel de limiter de saída + medidor de
gain reduction estudado em `references/lsp-plugins`.

## Cards P4 — um efeito por card, controles próprios

| Card | Inspiração clássica | Fonte de estudo | Implementação |
|---|---|---|---|
| **Wah** (Auto/Manual/LFO) | Cry Baby / Mu-Tron envelope filter | `references/guitarix` (GxWahwah) | bandpass RBJ ressonante varrido por envelope, knob ou LFO |
| **Slow Gear** | BOSS SG-1 (swell de violino) | `references/guitarix` (GxSlowGear) | detector de palhetada + rampa de volume quadrática |
| **Octaver** | BOSS OC-2 (sub analógica) | `references/GxPlugins.lv2` (GxOctaver) | flip-flop nos cruzamentos de zero × envelope + LP |
| **Ring Mod** | ring modulators clássicos | `references/airwindows` | portadora senoidal 20–2000 Hz |
| **Bitcrusher** | lo-fi 8-bit / samplers antigos | `references/airwindows` | sample & hold + quantização 4–16 bits |
| **Harmonizer** | harmonizers inteligentes (estilo HM-2/Whammy harmony) | `references/rkrlv2` (rakarrack) | autocorrelação em sinal decimado detecta a nota; intervalo diatônico (3ª/5ª/6ª/oitava no tom/escala escolhidos) via shifter granular |
| **Exciter** | Aphex Aural Exciter | `references/airwindows` (Energy) | harmônicos dos agudos saturados somados de volta |
| **De-esser** | de-essers/dynamic EQ de estúdio | `references/lsp-plugins` | subtração dinâmica da banda áspera (bandpass + envelope) |
| **Tape** | Studer/ampex, ToTape | **adaptado do `references/airwindows` ToTape/IronOxide (MIT)** | HP 30 Hz → saturação assimétrica → head bump 90 Hz → rolloff |
| **Console** | consoles analógicos (glue de buss) | **adaptado do `references/airwindows` Console (MIT)** | waveshape seno sutil |

## Slot de plugin VST3 externo

Hosting nativo do JUCE (`AudioPluginFormatManager` + `VST3PluginFormat`,
`JUCE_PLUGINHOST_VST3`). Qualquer efeito VST3 de terceiros entra na cadeia sem
portar código; o botão CARREGAR abre um menu com os plugins instalados em
`C:\Program Files\Common Files\VST3` + "Procurar arquivo…".
Dev: `GUITARRIG_EXT_PLUGIN=<caminho>` carrega no slot ao iniciar.

Plugins **grátis** recomendados para o slot (builds Windows oficiais):

| Plugin | Licença | O que traz |
|---|---|---|
| [Dragonfly Reverb](https://michaelwillis.github.io/dragonfly-reverb/) | GPLv3 | os 4 reverbs completos (Hall/Room/Plate/Early) |
| [LSP Plugins](https://lsp-plug.in/) | LGPLv3 | compressor multibanda, EQ paramétrico 32 bandas, gate sidechain |
| [Airwindows Consolidated](https://github.com/baconpaul/airwin2rack) | MIT | ~400 efeitos num só VST3 com browser |
| [Zam Plugins](https://www.zamaudio.com/) | GPLv2+ | ZamTube, ZamComp, ZamEQ |
| [Ratatouille](https://github.com/brummer10/Ratatouille.lv2) | GPLv3 | loader NAM/RTNeural com blend de 2 modelos |
| Valhalla Supermassive | grátis (não open) | reverb/delay ambient |

## Gate / Cab / Resampler (infra)

- **Cab IR**: `juce::dsp::Convolution` (troca RT-safe interna do JUCE)
- **Resampler**: `ResamplingContainer` (Lanczos) do AudioDSPTools (Apache-2.0/MIT, mesmo dos plugins NAM oficiais)
- **Amp**: [NAM Core](https://github.com/sdatkinson/NeuralAmpModelerCore) (MIT)
