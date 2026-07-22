# 🎸 GuitarRig NAM

**Amp sim pessoal para guitarra** — Standalone + VST3, baseado em [Neural Amp Modeler](https://github.com/sdatkinson/NeuralAmpModelerCore) (captures neurais de amplificadores reais) e JUCE 8, com loja integrada ao [TONE3000](https://www.tone3000.com).

![Windows](https://img.shields.io/badge/Windows-10%2F11%20x64-0078d4) ![JUCE](https://img.shields.io/badge/JUCE-8.0.15-8bc34a) ![NAM](https://img.shields.io/badge/NAM%20Core-v0.5.4%20(A2)-33c9d6) ![Status](https://img.shields.io/badge/status-funcional%20%C2%B7%20em%20evolu%C3%A7%C3%A3o-33c9d6)

![Tela principal](docs/screenshots/rig.png)

---

## 📊 Estado do projeto

| Fase | Entrega | Status |
|------|---------|--------|
| 0 | Casco JUCE (Standalone + VST3), passthrough, ASIO | ✅ |
| 1 | NAM Core integrado ao build (WHOLE_ARCHIVE, C++20) | ✅ |
| 2 | Carregamento de captures `.nam` + DSP real, troca RT-safe | ✅ |
| 3 | Resampler automático (Lanczos), Noise Gate, Cab IR, presets | ✅ |
| 4 | Tone Store: OAuth PKCE + busca + downloads do TONE3000 | ✅ |
| 5 | Design v2, cadeia de pedais completa, afinador, fotos, UX | ✅ |

**Validação**: cada fase foi testada com guitarra real (Focusrite ASIO, 48 kHz, 128 samples) e testes headless do DSP (carregamento das arquiteturas A2/WaveNet/LSTM, resampling 48→44.1 kHz).

## ⚡ Funcionalidades

- **Cadeia de sinal completa** (rolável): `Input → Noise Gate → Overdrive → Amp NAM → Cab IR → EQ → Delay → Reverb → Output`
- **Amp por capture neural**: qualquer `.nam` (arquiteturas A1/A2), com GAIN que satura o modelo como o amp real, tone stack B/M/T/Presence e Master
- **Resampler automático**: captures rodam no sample rate que esperam, em qualquer sample rate da interface (~0,6 ms de latência, reportada ao host)
- **Rigs paralelos**: até 3 pares **AMP+CAB** completos (capture + knobs próprios + IR por lane), sempre em dupla, somados no card **Mixer** (blend por rig + AIR global)
- **Cab IR** por convolução (wav/aiff/flac, troca sem glitch), low/high cut e fase por lane
- **Tone Store (TONE3000)**: login OAuth, busca com fotos, filtros por tipo/tags/arquitetura A2, escolha de variação (mics/canais), downloads com progresso, biblioteca offline, sem re-downloads
- **Afinador** real (detecção de pitch NSDF) com liga/desliga
- **Presets**: salvar em 1 clique, "Salvar como", indicador de modificado (•), presets de fábrica, navegação ◂ ▸
- **Fotos** do amp/cabinete carregados nos cartões do rig
- **UX**: knobs com trava no default, duplo-clique reseta, roda ajusta, Ctrl = fino, valor digitável; tooltips em tudo; atalhos (espaço, T, ←/→, Esc); medidores IN/OUT + CPU real
- **Pitch/octaver, Looper (60 s, overdub, export WAV) e Limiter** com indicador de clip
- **Real-time safety**: zero alocação/locks/IO no caminho de áudio (regra inegociável do projeto)

## 🖼️ Telas

| Tone Store | Biblioteca offline |
|---|---|
| ![Tone Store](docs/screenshots/tone-store.png) | ![Biblioteca](docs/screenshots/biblioteca.png) |

## 🔧 Build (Windows)

Requisitos: VS 2022 (Build Tools ou Community) com C++, CMake ≥ 3.22, Git.

```powershell
git clone <url-do-repo> GuitarRigNAM
cd GuitarRigNAM
# só o necessário para o build (references/ é opcional e pesado):
git submodule update --init --recursive third_party

cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Artefatos em `build\GuitarRigNAM_artefacts\Release\` (`Standalone\GuitarRig NAM.exe` e `VST3\GuitarRig NAM.vst3`).

### ASIO (recomendado)

O SDK da Steinberg não pode ser redistribuído. Baixe em <https://www.steinberg.net/asiosdk>, extraia para `third_party/asiosdk/` (deve existir `common/iasiodrv.h`) e reconfigure com:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DASIOSDK_DIR="$PWD\third_party\asiosdk"
```

`third_party/asiosdk/` está no `.gitignore` e **nunca** entra no Git.

## 🔑 Configuração do TONE3000

A API exige uma chave própria (grátis):

1. Crie conta em [tone3000.com](https://www.tone3000.com) → Settings → API Keys
2. Registre o redirect `http://localhost:53682/callback`
3. Cole a chave (`t3k_pub_…`) em `Documentos\GuitarRig NAM\tone3000.json` (o app cria o template)
4. No app: Tone Store → **Conectar TONE3000**

> ⚠️ **Segurança**: `tone3000.json` guarda sua chave e o refresh token da sua conta. Ele vive em `Documentos\GuitarRig NAM\` — **fora deste repositório** — e nunca deve ser commitado em lugar nenhum.

## 📁 Estrutura

```
src/                  código do plugin (processor, editor, store, cliente TONE3000)
assets/fonts/         Space Grotesk + JetBrains Mono (OFL, embutidas no binário)
docs/screenshots/     telas do projeto
references/           submódulos OPCIONAIS: projetos de referência p/ efeitos (ver references/README.md)
third_party/JUCE            submódulo pinado em 8.0.15 (necessário p/ build)
third_party/NeuralAmpModelerCore  submódulo pinado em v0.5.4, suporte A2 (necessário p/ build)
```

Dados do usuário (fora do repo): `Documentos\GuitarRig NAM\` — `Captures/`, `IRs/`, `Presets/`, `tone3000.json`.

## 🗺️ Roadmap

**Fase 6 — concluída:**
- [x] Cabs paralelos: 1–3 slots de IR com blend, low/high cut e phase por cab + AIR global *(evoluiu para rigs AMP+CAB na fase 8)*
- [x] Cadeia reordenável por **drag-and-drop** (arraste os cartões de efeito; amp+cabs são âncora fixa)
- [x] Modo **ECO** (capture leve baixado junto) com **auto-ECO** em CPU > 90% + aviso ⚠ no medidor
- [x] Badges **V1/V2** da arquitetura em amps e IRs
- [x] Efeitos P1: compressor de pedal (presets Clean/Country/Lead), gate com hold+histerese de 6 dB, pré-EQ antes do NAM

**Fase 7 — efeitos P2 (concluída):**
- [x] Variações por efeito no cartão: Drive ×6 (Boost/Screamer/Blues/Distortion/Fuzz/Heavy), Comp ×3, Delay ×4, Reverb ×4
- [x] Delay: **TAP tempo** com subdivisões (1/4, 1/8, 1/8., 1/16), **Ping-Pong** estéreo e **trails**
- [x] Reverbs Hall/Room/Plate/**Spring**, estéreo real e trails
- [x] Cartão **Modulação**: Chorus, Phaser, Flanger e Tremolo harmônico

**Fase 8 — rigs paralelos AMP+CAB (concluída):**
- [x] Arquitetura corrigida: cada lane paralela é um par **AMP+CAB** completo (capture NAM com knobs próprios + IR), não só IRs em paralelo
- [x] Card **Mixer** dedicado: soma das lanes com blend por rig, AIR global e botões +/− que adicionam/removem o par inteiro (mín. 1, máx. 3)
- [x] Visual em paralelo de verdade: lanes **empilhadas** com bus de divisão na entrada e bus de soma no Mixer (com 1 rig, mantém o card grande clássico)

![Rigs paralelos](docs/screenshots/rigs-paralelos.png)

**Fase 9 — efeitos P3 (concluída):**
- [x] Cartão **Pitch**: octaver granular de 2 cabeças (Oitava ↓/↑, Quinta, Detune) com MIX/LEVEL — validado headless (440 Hz → 220/660/880 Hz)
- [x] Cartão **Looper**: até 60 s, REC → fecha e toca → overdub, PLAY/STOP, LIMPAR e **export WAV** (`Documentos\GuitarRig NAM\Loops`)
- [x] Cartão **Limiter** brickwall no fim da cadeia com barra de gain reduction + aviso **CLIP** no medidor OUT

![Efeitos P3](docs/screenshots/efeitos-p3.png)

**Próximos:**
- [ ] Analisador de espectro
- [ ] Slot de plugin **VST3 externo** (hosting JUCE — Dragonfly, LSP etc. sem portar código)
- [ ] Animações e microinterações · medidores com peak-hold/clip
- [ ] Drag-and-drop de arquivos · afinador com mute · delay/reverb estéreo
- [ ] Favoritos do TONE3000 · A/B de rigs · gravador rápido · modo performance

## 🤝 Contribuindo

Contribuições são bem-vindas! Leia o **[CONTRIBUTING.md](CONTRIBUTING.md)** (build, mapa do código, regras de real-time safety e armadilhas conhecidas do MSVC/JUCE) e use os templates de issue/PR. Itens não marcados do roadmap são um ótimo ponto de partida.

*Contributions welcome — see [CONTRIBUTING.md](CONTRIBUTING.md) (Portuguese; feel free to open issues in English).*

## 📜 Licenças

Este projeto é licenciado sob a **[AGPLv3](LICENSE)** — exigência do uso do JUCE 8 no tier open source. Dependências:

- **JUCE 8** — AGPLv3 (uso pessoal/open-source) · **NAM Core** — MIT · **AudioDSPTools** — Apache-2.0/MIT (ver repositório)
- **Fontes** — SIL Open Font License (textos em `assets/fonts/`)
- **ASIO SDK** — licença Steinberg (download manual, não redistribuído)
- Captures/IRs baixados do TONE3000 têm licenças próprias por tone (CC/T3K) — respeite-as ao redistribuir timbres.
