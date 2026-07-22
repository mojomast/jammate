# GuitarRig NAM

Amp sim pessoal de guitarra (Standalone + VST3) baseado em [Neural Amp Modeler](https://github.com/sdatkinson/NeuralAmpModelerCore) e JUCE 8.

**Estado atual: Fase 3** — cadeia completa de amp sim:

- **Resampler automático** (Lanczos, via `ResamplingContainer` do AudioDSPTools): captures rodam no sample rate que esperam, em qualquer sample rate da interface (~0,6 ms de latência extra quando ativo, reportada ao host e exibida na barra de status).
- **Noise Gate** (pré-amp): THRESH (−90..−20 dB) e RELEASE (10..500 ms), expander 10:1.
- **Cab IR**: carrega WAV/AIFF/FLAC via `juce::dsp::Convolution` (troca RT-safe, IR resampleado automaticamente), knob LEVEL. Um IR de teste transparente é criado em `Documentos\GuitarRig NAM\IRs\`.
- **Presets**: XML em `Documentos\GuitarRig NAM\Presets\` — salvar (SALVAR), navegar (◂ ▸), escolher pelo menu do pill central. O preset inclui parâmetros + caminhos do capture e do IR.

Base da Fase 2:

- Botão **CARREGAR CAPTURE NAM** no cartão do amp abre um seletor de `.nam`; o carregamento (incl. prewarm) roda numa thread de fundo e o modelo é trocado na thread de áudio sem glitch (troca lock-free por atomics; nunca há alocação/`delete` no `processBlock`).
- Cadeia mono: canal 0 → input gain → NAM → output level → duplicado para stereo.
- Knobs **GAIN** (entrada, ±24 dB) e **LEVEL** (saída, −40..+12 dB), LED de bypass do amp, medidores IN/OUT, barra de status com sample rate/buffer.
- O caminho do capture e os parâmetros são salvos no estado do plugin (o capture recarrega ao reabrir).
- UI conforme o design do projeto (claude.ai/design), canvas lógico 1100×700 escalado — janela redimensionável com aspecto travado.
- Sem resampling ainda: se o capture espera 48 kHz, rode a interface em 48 kHz (a barra de status avisa). Tone Store, afinador e cadeia de pedais/efeitos são fases futuras.

Modelos de exemplo para teste: `third_party/NeuralAmpModelerCore/example_models/*.nam`.

## Requisitos

- Windows 10/11 x64
- Visual Studio 2022 com workload "Desktop development with C++"
- CMake >= 3.22 e Git no PATH

## Build

```powershell
git clone <url> GuitarRigNAM
cd GuitarRigNAM
git submodule update --init --recursive

cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Artefatos:

- Standalone: `build\GuitarRigNAM_artefacts\Release\Standalone\GuitarRig NAM.exe`
- VST3: `build\GuitarRigNAM_artefacts\Release\VST3\GuitarRig NAM.vst3`

O VST3 não é copiado automaticamente para a pasta do sistema (`COPY_PLUGIN_AFTER_BUILD FALSE`). Para usar em um DAW, copie a pasta `.vst3` para `C:\Program Files\Common Files\VST3\`.

## Habilitar ASIO (recomendado para latência baixa)

O ASIO SDK da Steinberg não pode ser redistribuído, então o download é manual:

1. Baixe o SDK em <https://www.steinberg.net/asiosdk> (aceite a licença).
2. Extraia para `third_party/asiosdk/` de modo que exista `third_party/asiosdk/common/iasiodrv.h`.
3. Reconfigure e rebuilde:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DASIOSDK_DIR="$PWD\third_party\asiosdk"
cmake --build build --config Release
```

Sem o SDK, o build funciona normalmente usando WASAPI/DirectSound.

A pasta `third_party/asiosdk/` está no `.gitignore` e nunca deve ser commitada (licença Steinberg).

## Versões pinadas

- JUCE `8.0.15` (série 8.x)
- NeuralAmpModelerCore `v0.5.4` (>= v0.5.2 exigido para arquitetura A2)

## Regra de real-time safety

Dentro de `processBlock` é proibido alocar memória, usar locks, fazer I/O, logar ou chamar rede. Vale para todo o projeto.
