# 📚 Referências (submódulos opcionais)

Projetos open source usados como **referência de algoritmos** para os efeitos do GuitarRig NAM. São submódulos **pinados** e **opcionais** — o build NÃO precisa deles. Para estudar um deles:

```powershell
git submodule update --init references/airwindows   # exemplo
```

| Pasta | Projeto | Licença | Uso previsto |
|---|---|---|---|
| `airwindows/` | [Airwindows](https://github.com/airwindows/airwindows) | MIT | **Portar código diretamente**: saturação, tape, delays e coloração analógica |
| `BYOD/` | [BYOD](https://github.com/Chowdhury-DSP/BYOD) | GPLv3 | Referência de circuitos de drive/waveshaping modelados (JUCE) |
| `dragonfly-reverb/` | [Dragonfly Reverb](https://github.com/michaelwillis/dragonfly-reverb) | GPLv3 | Referência para reverbs Plate/Room/Hall |
| `guitarix/` | [Guitarix](https://github.com/brummer10/guitarix) | GPLv2 | Referência de pedais com personalidade (drives, fuzz, wah) |
| `GxPlugins.lv2/` | [GxPlugins.lv2](https://github.com/brummer10/GxPlugins.lv2) | GPLv3 | Idem — coleção LV2 do Guitarix |
| `lsp-plugins/` | [LSP Plugins](https://github.com/lsp-plugins/lsp-plugins) | LGPLv3 | Referência do lado "studio": compressor, limiter, analisadores |
| `rkrlv2/` | [rkrlv2](https://github.com/ssj71/rkrlv2) | GPLv2 | Referência seletiva: harmonizer, pitch, tremolo óptico |
| `ToobAmp/` | [ToobAmp](https://github.com/rerdavies/ToobAmp) | MIT (verificar componentes) | Referência de gate, IR loader, EQ e modulações |

## ⚖️ Nota de licenças

- **Airwindows (MIT)** e partes MIT do ToobAmp: código pode ser incorporado ao app (mantendo o aviso de copyright).
- **GPL/LGPL** (BYOD, Dragonfly, Guitarix, Gx, LSP, rkrlv2): usados como **leitura/estudo de algoritmo** neste projeto pessoal, não distribuído. Se o GuitarRig NAM um dia for distribuído, revisar a compatibilidade de licenças antes.
- ToobAmp, Guitarix, GxPlugins e rkrlv2 são **LV2/Linux** — não compilam para Windows; por isso servem só como referência, nunca como dependência de build.
