# 🔌 Plugins VST3 recomendados

Pacote de efeitos open source prontos para o **slot Plugin VST3** da cadeia
(cards "Plugin VST3 1–3"). Depois de instalados, eles aparecem automaticamente
no menu **CARREGAR VST3**, organizados por categoria.

## Instalar

Clique-direito em **`instalar-plugins.ps1`** → *Executar com o PowerShell*
(ele pede permissão de administrador sozinho — o destino é
`C:\Program Files\Common Files\VST3`).

O instalador usa a **cópia offline** de [`offline/`](offline/) quando ela
existe (vem junto no repositório — instala sem internet) e cai para o
**release oficial no GitHub** de cada projeto quando não.

## O pacote

| Plugin | Versão | O que traz | Licença | Fonte oficial |
|---|---|---|---|---|
| **Dragonfly Reverb** | 3.2.10 | Hall, Room, Plate e Early Reflections — reverbs muito acima do interno | GPLv3 | [github.com/michaelwillis/dragonfly-reverb](https://github.com/michaelwillis/dragonfly-reverb) |
| **Airwindows Consolidated** | 2026-07-19 | ~400 efeitos num só plugin com browser (tape, console, saturação…) | MIT | [github.com/baconpaul/airwin2rack](https://github.com/baconpaul/airwin2rack) |
| **Zam Plugins** | 4.5 | ZamTube, ZamComp(X2), ZamEQ2, ZaMaximX2, ZamGate… | GPLv2+ | [github.com/zamaudio/zam-plugins](https://github.com/zamaudio/zam-plugins) |

### Instalação manual (sem binário Windows no GitHub)

| Plugin | O que traz | Licença | Onde baixar |
|---|---|---|---|
| **LSP Plugins** | compressor multibanda, EQ paramétrico 32 bandas, gate sidechain | LGPLv3 | [lsp-plug.in](https://lsp-plug.in) |
| **Valhalla Supermassive** | reverb/delay ambient (grátis, EULA proíbe redistribuir) | freeware | [valhalladsp.com](https://valhalladsp.com/shop/reverb/valhalla-supermassive/) |

> **Ratatouille** (loader NAM do brummer) ficou de fora: os builds Windows são
> LV2/CLAP/VST2 — sem VST3, o slot não consegue hospedar.

## Nota de licenças (cópia offline)

Os zips em `offline/` contêm **apenas os bundles VST3 extraídos dos pacotes
oficiais, sem modificações**, com as licenças e um `ORIGEM.txt` apontando o
release de origem. Redistribuição permitida pelas licenças (GPLv3, MIT,
GPLv2+); o código-fonte de cada um está nos repositórios oficiais linkados
acima.
