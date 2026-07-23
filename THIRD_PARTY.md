# Créditos de terceiros — PedalForge NAM

Este programa inclui material de terceiros. Abaixo, cada item com sua licença
e a atribuição exigida. (O código do PedalForge NAM é AGPLv3; estes materiais
mantêm as suas próprias licenças.)

## Grooves e viradas da bateria — Groove MIDI Dataset

Parte da biblioteca de grooves e viradas do **módulo Bateria** foi derivada do
**Groove MIDI Dataset**, quantizada e adaptada para o formato interno de 1
compasso.

- **Fonte:** Groove MIDI Dataset — Google Magenta
  <https://magenta.tensorflow.org/datasets/groove>
- **Licença:** Creative Commons Attribution 4.0 International (**CC BY 4.0**)
  <https://creativecommons.org/licenses/by/4.0/>
- **Atribuição:** "Groove MIDI Dataset" by Google LLC (Magenta), usado sob
  CC BY 4.0. As patterns foram quantizadas para uma grade de semicolcheia e
  remapeadas para 9 vozes; são obras derivadas.

## Grooves e viradas de metal — midi-drums

Parte dos grooves e viradas de **metal** foi portada (posições e velocities)
das definições de padrão do projeto **midi-drums**.

- **Fonte:** midi-drums — fsecada01
  <https://github.com/fsecada01/midi-drums>
- **Licença:** MIT (declarada em `pyproject.toml` do projeto:
  `license = { text = "MIT" }`)
- **Atribuição / aviso MIT:**

  ```
  MIT License

  Copyright (c) fsecada01 (midi-drums)

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
  ```

## Samples do kit interno da bateria — GMRockKit

O sampler interno da bateria usa os samples do **GMRockKit** (ver também
`assets/drums/ORIGEM.txt`).

- **Kit:** GMRockKit — "A Sampled 5pc Pearl DX Series Drumkit"
- **Autores:** Glen MacArthur / Sebastian Moors
- **Licença:** GPL (compatível com o AGPLv3 deste projeto)
- **Fonte:** repositório do Hydrogen
  <https://github.com/hydrogen-music/hydrogen/tree/main/data/drumkits/GMRockKit>

## Tipografia

- **Space Grotesk** e **JetBrains Mono** — SIL Open Font License 1.1 (OFL).
  Ver `assets/fonts/*-OFL.txt`.

## Plugins VST3 do catálogo embutido

O catálogo (Tone Store → aba Plugins) apenas **baixa dos releases oficiais** e
extrai os `.vst3`; cada plugin mantém a sua licença (GPLv3, MIT, GPLv2+). Ver
`plugins/README.md` para a lista, versões e fontes oficiais.

## Ideias de interface

Conceitos de interface do módulo Bateria (navegador, humanização) foram
inspirados no **DrumGroovePro** (InToEtherion, GPLv3) —
<https://github.com/InToEtherion/DrumGroovePro>. Nenhum código foi copiado;
apenas ideias, que não são cobertas por copyright.
