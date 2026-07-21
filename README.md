# GuitarRig NAM

Amp sim pessoal de guitarra (Standalone + VST3) baseado em [Neural Amp Modeler](https://github.com/sdatkinson/NeuralAmpModelerCore) e JUCE 8.

**Estado atual: Fase 1** — o NeuralAmpModelerCore (v0.5.4) compila e linka no plugin como static lib (`nam_core`, C++20, com fast-path A2 e `NAM_SAMPLE_FLOAT`), verificado por um smoke test no construtor do processor. O áudio ainda é passthrough; carregamento de modelos `.nam` e DSP real são a Fase 2.

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
