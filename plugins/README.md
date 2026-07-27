# 🔌 Plugins VST3 recomendados

Pacote de efeitos open source prontos para os **slots Plugin VST3** da
cadeia. Depois de instalados, eles aparecem automaticamente no menu
**CARREGAR VST3**, organizados por categoria.

**Regra do catálogo: só entra plugin com download DIRETO** — o app baixa o
zip do release oficial, extrai o `.vst3` e pronto; para desinstalar, apaga
o arquivo. **Nada de instalador (.exe), nada de admin, nada de "vá ao
site".** Projetos que só publicam instalador (BYOD, ChowDSP, GuitarML,
Valentine…) ou download por site (OJD, Temper, Swanky Amp, Valhalla) ficam
de fora por isso.

## Instalar/desinstalar — pelo próprio app

O catálogo é **embutido no programa** (`src/PluginCatalog.cpp`) e tem um
gerenciador próprio: **Tone Store → aba "Plugins"** (ou menu CARREGAR VST3 →
"Gerenciar plugins…"). Cada plugin tem status (● instalado + versão) e um
botão que alterna **INSTALAR ⇄ DESINSTALAR**:

- **Instalar**: baixa o zip do release oficial com progresso e extrai em
  `%LOCALAPPDATA%\Programs\Common\VST3` (pasta VST3 de usuário da spec —
  **sem administrador**)
- **Desinstalar**: solta o plugin dos slots da cadeia, espera o módulo
  descarregar e apaga exatamente os bundles registrados no manifesto
  (`Documentos\PedalForge NAM\plugins.json`)

## Instalar — pelo script

Clique-direito em **`instalar-plugins.ps1`** → *Executar com o PowerShell*.
Copia os bundles para a **mesma pasta VST3 de usuário** (sem admin), baixando
do **release oficial no GitHub** de cada projeto.

Se você quiser instalar sem internet, crie uma pasta `offline/` ao lado do
script e ponha nela os zips dos releases oficiais — o script os usa quando
existem. Essa pasta **não faz parte do repositório** (está no `.gitignore`).

## O catálogo (8 plugins, todos download direto)

| Plugin | Versão | O que traz | Licença | Fonte oficial |
|---|---|---|---|---|
| **Dragonfly Reverb** | 3.2.10 | Hall, Room, Plate e Early Reflections | GPLv3 | [github.com/michaelwillis/dragonfly-reverb](https://github.com/michaelwillis/dragonfly-reverb) |
| **Airwindows Consolidated** | 2026-07-19 | ~400 efeitos num só plugin (tape, console, saturação…) | MIT | [github.com/baconpaul/airwin2rack](https://github.com/baconpaul/airwin2rack) |
| **Zam Plugins** | 4.5 | ZamTube, ZamComp(X2), ZamEQ2, ZaMaximX2, ZamGate… | GPLv2+ | [github.com/zamaudio/zam-plugins](https://github.com/zamaudio/zam-plugins) |
| **AIDA-X** | 1.1.0 | player neural (pedais/amps) | GPLv3 | [github.com/AidaDSP/AIDA-X](https://github.com/AidaDSP/AIDA-X) |
| **Fire** | 1.5.0 | distorção multibanda | GPLv3 | [github.com/jerryuhoo/Fire](https://github.com/jerryuhoo/Fire) |
| **Wolf Shaper** | 1.0.2 | waveshaper com editor de curva | GPLv3 | [github.com/wolf-plugins/wolf-shaper](https://github.com/wolf-plugins/wolf-shaper) |
| **PeakEater** | 0.8.2 | clipper | GPLv3 | [github.com/vvvar/PeakEater](https://github.com/vvvar/PeakEater) |
| **Surge XT Effects** | 1.3.4 | multi-fx: reverbs, delays, rotary, phaser, distorção… | GPLv3 | [surge-synthesizer.github.io](https://surge-synthesizer.github.io/) |

> Do zip do Surge o app extrai **só** o bundle "Surge XT Effects.vst3" (o
> sintetizador que vem junto é ignorado).

### Por que estes ficaram de fora

| Plugin | Motivo |
|---|---|
| BYOD, ChowCentaur, ChowTapeModel, ChowPhaser, TS-M1N3, Proteus, Valentine | o release Windows só tem instalador `.exe` (confirmado nos assets do GitHub) |
| Schrammel OJD, Temper, Swanky Amp | sem release direto — download só pelo site |
| LSP Plugins | releases atuais não têm mais binário Windows |
| Valhalla Supermassive | freeware com EULA que proíbe redistribuir; só instalador do site |

Se algum deles um dia publicar zip portátil com `.vst3`, é só adicionar a
entrada em `src/PluginCatalog.cpp`.

## Nota de licenças

Este repositório **não redistribui binário de plugin nenhum**. Cada plugin é
baixado do release oficial do próprio projeto, no momento da instalação, e
mantém a licença dele (GPLv3, MIT, GPLv2+) — as fontes estão nos repositórios
linkados na tabela acima.

A pasta `offline/` foi removida do repositório justamente por isso: distribuir
binário GPL cria a obrigação de fornecer a fonte correspondente a quem recebe,
e não faz sentido assumir essa obrigação quando o instalador já busca direto na
origem.
