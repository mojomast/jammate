# Relatório — Mockup final PedalForge vNext

**Data:** 24/07/2026
**Arquivo:** `docs/design/pedalforge-vnext-complete.html`
**Método:** renderização headless (Chrome) de cada tela/estado → inspeção visual → correção → re-inspeção, até o checklist fechar. Console verificado em dois estados compostos (zero erros).

## 1. Resultado

O mockup aprovado foi consolidado em versão final de referência: todas as telas revisadas, os requisitos do escopo implementados e os bugs visuais encontrados no processo corrigidos. Nenhuma tela foi recriada — o trabalho foi incremental sobre o layout aprovado.

## 2. Arquivos alterados

| Arquivo | Mudança |
|---|---|
| `docs/design/pedalforge-vnext-complete.html` | Única fonte editada (CSS + HTML + JS no mesmo arquivo) |
| `docs/design/RELATORIO_MOCKUP_FINAL.md` | Este relatório |
| `build/mockup-final-*.png` | 11 capturas finais |

## 3. Componentes criados ou reorganizados

- **Rigs paralelos reais (1/2/3)** — estado global `rigCount` com três renderizadores sincronizados:
  - `renderChainRig()` — na tela Rig, 1 rig usa os cards focados originais (AMP HEAD + CAB IR); 2–3 rigs viram uma pilha `.rig-stack` de pares AMP+IR alinhados por linha, cada um com nome, A2, LED, 6 knobs, CHANGE/VARIATIONS e REPLACE IR — nada sai da tela.
  - `renderRibbonRig()` — no ribbon da Bateria, pares AMP+IR em linhas paralelas; altura do ribbon adapta (114/142/182); knobs com nome sempre visível e tamanho por densidade (32/24/19 px).
  - `updateMixer()` — OUTPUT/MIXER mostra “N parallel rigs”, knobs RIG 1..N, segmentado 1·2·3 e o botão “＋ PARALLEL RIG” (desativa em 3).
- **Seções A/B na partitura** — renderizador único `engravedSection(config)` (pauta, clave, fórmulas só quando mudam, notas, ghosts, acentos, **pausas**, beams, contagem, barra final, playhead). Seção A: 4/4 · 4/4(oculta) · 7/8 · 7/4. Seção B: 3/4 · 6/8 · 6/8(oculta por repetição) · 4/4. Cabeçalhos de compasso e status trocam junto.
- **Valores de knob do ribbon em hover** — overlay dentro do card (nunca vaza); `?hover=1` força um valor visível para captura.
- **GENERATOR e GRID na barra inferior** — toggles à esquerda do volume (segundo clique volta à Library); as abas do painel ficaram só LIBRARY · KIT MIXER.
- **Grade** — separação visual de tempos (borda a cada 4 passos) e **coluna de playhead** (contorno ciano no beat 2), mantendo estados hit/acento/ghost e o clique que cicla estados.
- **Tone Store** — filtros de tipo em linha própria; VIEW (collection) e SORT em outra linha; **My Library real** (4 tones locais com badges IN RIG/DOWNLOADED e ações próprias); contador por aba; preview por delegação (sobrevive à troca de aba).
- **Traços de cadeia no ribbon** (`.ribdash`) entre todos os cards, incluindo dentro do par AMP–IR.
- `bindKnobs(root)` reutilizável para conteúdo dinâmico; LEDs de conteúdo dinâmico por delegação.
- Parâmetros de URL para captura/teste: `view`, `panel`, `store`, `section=b`, `rigs=1..3`, `hover=1`, `playing`, `drawer`, `theme`.

## 4. Bugs encontrados e corrigidos

1. **Cadeia do Rig cortada no viewport padrão** (CAB pela metade, Delay invisível) — larguras compactadas (fx-mini 126→110, amp 366→310, cab 178→150, conector 25→16, colunas laterais 142/184→132/176). Com 1 rig a cadeia inteira cabe sem rolagem; com 2–3 rigs a rolagem + minimapa assumem.
2. **Label do afinador atravessada pela agulha** — o texto “E2 · −1.4 cents” ganhou fundo/z-index.
3. **Conector “+” duplicado** entre a pilha de rigs e o Delay (o renderizador anexava um conector além do estático) — removido.
4. **Terceira linha do ribbon 3-rig cortada** pela borda — alturas r2/r3 ajustadas (142/182).
5. **Badges ilegíveis sobre imagens claras** na Store (“FULL RIG” sobre o gradiente creme) — fundo escuro nos badges.
6. **Falta de pausas na partitura** — pausa de colcheia (SMuFL `restStemlessNote`… `E4E6`) adicionada no compasso 7/8 da seção A e no 6/8 variado da seção B (com chimbal omitido no passo correspondente).
7. **Biblioteca sob ribbon alto** podia clipar gêneros — colunas Genre/Grooves agora rolam internamente.

## 5. Decisões de layout

- **Canvas**: mantido o palco 1440×900 do mockup aprovado. Ele representa a **janela física** real (~1379×911 na tela do usuário a 125% de DPI), sobre a qual o canvas lógico 1100×700 do aplicativo é mapeado (~1.31×). Recriar o mockup em 1100 unidades violaria a diretriz de não refazer do zero; a fidelidade dimensional foi validada pelos testes de escala.
- Fórmula de compasso: gravada na pauta (Leland/SMuFL, tinta da partitura) e **seletor no cabeçalho do compasso, à esquerda do papel musical** (`4/4⌄` antes de `VERSE⌄`), em tags pequenas.
- Rigs 2–3 no ribbon: nomes encurtam por densidade (“MESA RECTIFIER” → “MESA”) para manter todos os knobs nomeados e legíveis.
- 1 rig continua com os cards focados grandes (edição confortável), como no mockup aprovado.

## 6. Estados testados (todos renderizados e inspecionados)

Rig 1 rig · Rig 3 rigs · Bateria seção A (4/4, 7/8, 7/4) · Bateria seção B (3/4, 6/8, 6/8 repetido, 4/4) · Grid · Generator · ribbon 1/2(hover)/3 rigs · Store Explore · Store My Library · Plugins (aba) · tema claro (spot) · escala 125% e 150% (`--force-device-scale-factor`) — proporcional, sem quebra · console limpo (2 execuções, incluindo estado composto `rigs=3&section=b&panel=grid`).

## 7. Capturas geradas (`build/`)

`mockup-final-rig-1rig.png` · `mockup-final-rig-3rigs.png` · `mockup-final-drums.png` · `mockup-final-drums-mixed-meters.png` · `mockup-final-grid.png` · `mockup-final-generator.png` · `mockup-final-ribbon-rig1.png` · `mockup-final-ribbon-rig2-hover.png` · `mockup-final-ribbon-rig3.png` · `mockup-final-store-explore.png` · `mockup-final-store-library.png`

## 8. Limitações restantes

- A contagem sob os compassos compostos (6/8) usa o padrão “1 & 2 &…” do mockup; o aplicativo real conta pelos agrupamentos do DrumEngine.
- Playhead/“PLAYING BAR 2” são ilustrativos (sem transporte real).
- O arrasto de groove para a pauta é indicado (drag handle ⠿) mas não implementado como drag-and-drop real no mockup.
- STAGE, SONG/SCENES e ÁUDIO foram mantidos como aprovados (coerência visual conferida; fora do checklist detalhado).
- Com 2–3 rigs a cadeia do Rig rola horizontalmente por concepção (minimapa indica a região visível).
