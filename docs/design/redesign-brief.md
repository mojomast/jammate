# Brief de redesign — PedalForge NAM

> Cole este texto inteiro numa sessão do Claude focada em design.
> Descreve **função e estrutura**; a **estética é livre** — quero uma identidade nova.

## Seu papel
Você é o design lead. Vou te dar TODAS as features e o comportamento de um plugin/app
de guitarra chamado **PedalForge NAM**. Quero um **redesign visual completo e original**:
NÃO reproduza nenhum design anterior — crie uma nova identidade e um sistema de design
coeso. Eu descrevo só o que cada coisa faz e onde fica; a aparência é sua.

## O produto
- Simulador pessoal de amplificador de guitarra + multiefeitos, com **Neural Amp Modeler
  (NAM)** e **IRs de cabinet**.
- Roda como **app Standalone e plugin VST3** (uso ao vivo em casa e dentro de uma DAW).
- Público: guitarrista/músico tocando em casa (o dono é baterista/metaleiro). Ambiente
  típico: pouca luz, uso enquanto toca — legibilidade em movimento e alvos de clique
  generosos importam.
- Canvas lógico atual: ~**1100×700**, escalado. Você pode propor outra proporção, mas
  lembre que é uma **janela de plugin dentro de uma DAW**: precisa funcionar de ~1000px
  até 4K por escala, sem exigir tela cheia.

## Restrições que afetam o design
- **Densidade alta** de controles (knobs, sliders, LEDs, seletores) — hierarquia e
  agrupamento visual claros são essenciais.
- **Knobs rotativos** são o controle primário: arraste vertical ajusta; duplo-clique
  reseta; scroll ajusta; Ctrl = ajuste fino. Cada knob tem rótulo + valor legível.
- **Tempo real de áudio**: estados precisam ler bem em movimento — medidores IN/OUT/CPU,
  playhead da bateria, afinador.
- Proponha **tema claro e escuro** (plugins de áudio costumam ser escuros, mas fique livre).
- Acessibilidade: contraste bom, foco de teclado visível, alvos de clique ≥ ~28px.
- Desktop, **mouse + teclado** (nada de gestos de touch/tela cheia).

## Entregáveis (formato pensado para implementação em JUCE/C++)
1. **Sistema de design (tokens):** paleta nomeada com papéis semânticos (fundo, superfícies,
   texto/《muted》, acento, ok/aviso/erro); tipografia (2–3 famílias + escala); espaçamento;
   raios; sombras; e o estilo dos **componentes-base**: knob, slider, botão, LED de bypass,
   chip/toggle, dropdown, aba, card/slot, conector da cadeia, medidor, pill de preset.
2. **Mockups em alta fidelidade** de cada tela e dos estados-chave listados abaixo.
3. **Inventário de componentes** com estados (normal / hover / ativo / desabilitado) e tamanhos.
4. Em cada mockup, **rotule cada região/controle com o nome funcional** (pra eu mapear no código).
5. Se possível, entregue os mockups como **HTML/CSS autocontido** (eu extraio os tokens e
   traduzo para JUCE, e uso o mockup como referência 1:1).

---

## Telas e features

### 1. Tela principal — "Cadeia" (guitarra)
**Barra superior:** logo/nome do produto; selo do formato (NAM); navegação de preset
(◀ [nome do preset] ▶); botão Salvar; comparação **A/B**; medidores **IN/OUT** (dB) e
**CPU** (%); chip **REC** (grava a saída); botões que abrem: **Bateria**, **Áudio** (config
de driver — só standalone), **Tone Store**.

**Corpo — a cadeia de sinal horizontal, EDITÁVEL e REORDENÁVEL** (é o coração do programa):
uma sequência de "slots" ligados por conectores, com um "**+**" entre eles para inserir um
efeito. Cada slot tem um **LED de bypass** (ligado/desligado), título, um seletor de
tipo/variação quando aplicável, e seus knobs. Slots:
- **Input** — knob Gain; indicador de sinal presente.
- **Noise Gate** — Threshold, Hold, Release; modo histerese.
- **Compressor** — Sustain, Attack, Blend, Level; presets rápidos (Clean / Country / Lead).
- **Pre-EQ / EQ** — Low, Mid, High.
- **Overdrive / Distortion** — Drive, Tone, Level; seletor de tipo/modelo.
- **Modulation** — Rate, Depth, Mix; tipo (chorus/flanger/phaser…).
- **Delay** — Time, Feedback, Mix; tipo; divisão rítmica + **TAP tempo**.
- **Reverb** — Decay, Mix, Pre-delay; tipo.
- **Pitch/Harmonizer** — Mix, Level; tipo; (harmonizer: tonalidade, escala, intervalo);
  também Octaver, Wah (modo), Ring mod.
- **Amp Head (NAM)** — nome do modelo capturado; Gain, Bass, Mid, Treble, Presence, Master;
  modo **ECO**; slot de variação; botão **"trocar captura NAM"** (carrega arquivo .nam).
  Pode haver **vários amps em paralelo** (lanes/rigs).
- **Cab IR** — nome do IR; Low Cut, Hi Cut, Air; inverter fase; botão trocar IR.
- **Mixer** — soma as lanes paralelas; nível por lane + nível global; **+/-** adiciona/remove
  uma lane inteira (par Amp+Cab).
- **+ Efeito** — slot vazio para adicionar.
- **Looper** (gravar/tocar/limpar/exportar) e **Limiter** (ceiling/release) — efeitos extras.

**Barra inferior:** **Afinador** (com display de nota/frequência e as cordas E A D G B e);
Mute; Auto-ECO; **Palco** (modo performance com controles grandes e poucos); display de
forma de onda/afinação; info técnica (sample rate · buffer · A=440 Hz).

**Estados a desenhar:** cadeia normal; um slot em bypass; **arrastando** um slot para
reordenar; inserindo efeito no "+"; modo **Palco**; **A/B** ativo; medidores em pico/clip;
afinador afinando (dentro = ok / fora = alerta).

### 2. Módulo Bateria (abre por cima da tela principal)
Conceito central: **"a partitura é a track"** — os compassos são editados em **pentagrama de
bateria de verdade**.

**Transporte:** Tocar/Pausar; **BPM** (com ±); **Swing**; **Metrônomo** (Click); **Contagem**
(count-in); **Seguir** (vira a página sozinho ao mudar de seção); **Grade**; **Gerador**;
**Edição** (alterna entre editar notas × montar/arrastar compassos inteiros).

**Timeline de seções:** "Seção 1 · compassos 1–4", adicionar/remover seção (até 8 seções ×
4 compassos = 32 compassos).

**Cabeçalho de cada compasso** (4 por seção): um chip de **PAPEL** (Verso / Refrão / Ponte /
Breakdown / Virada, ou Automático) **e** um chip de **FÓRMULA DE COMPASSO** (4/4, 3/4, 6/8,
7/8, 5/4, custom) — cada um abre o seu próprio menu; + o nome do groove aplicado; + botão limpar.

**Pauta central:** os 4 compassos como **pentagrama de bateria corrido** — 5 linhas, cabeças
"×" (pratos/chimbal) e ovais (tambores), hastes para cima (mãos) e para baixo (pés),
ligaduras agrupadas pela métrica, **largura variável conforme a fórmula** do compasso, e um
**playhead** que anda quando toca.

**Área inferior — alterna entre 3 modos:**
- **Biblioteca:** coluna de **Gêneros com contadores** (Rock, Pop, Punk, Metal, Funk,
  Soul/Gospel, Blues, Jazz, Country, Brasil, Latino, Reggae/Ska, Hip-Hop, Eletrônico, World,
  Geral, Meus); abas **Grooves / Viradas**; **lista de grooves arrastáveis**; painel de
  **Preview** com o groove em mini-pauta grande + "aplicar no compasso N" + salvar; e 3
  sliders de **Humanização** (Velocity, Timing, Round-Robin).
- **Grade:** editor de **16 passos** do compasso selecionado — uma linha por peça (bumbo,
  caixa, chimbal, pedal, ride, crash, tom 1, tom 2, surdo); célula cicla nada→toque→acento→ghost.
- **Gerador:** **Gênero** (Metal/Rock/Jazz/Funk) → **Estilo** contextual (ex. metal:
  heavy/death/power/progressive/thrash/doom/breakdown) → **Baterista** (Bonham, Porcaro,
  Weckl, Chambers, Roeder, Dee, Hoglan — os incompatíveis com o gênero aparecem
  desabilitados) + 5 parâmetros (Complexidade, Dinâmica, Humanização, Viradas, Swing) + 2
  ações ("gerar este compasso" e "preencher os 4 compassos").

**Barra inferior:** fonte do som (**kit interno** de samples ou **VST3 de bateria** —
carregar/painel/remover); salvar compasso; volume.

**Interações:** arrastar groove da biblioteca para o compasso; clicar no compasso edita a
nota; em "montar", arrastar o compasso **inteiro** para copiar/reposicionar.

**Estados a desenhar:** biblioteca; grade; gerador; um compasso em 7/8 (largura diferente);
tocando com playhead; menu de papel aberto; menu de fórmula aberto.

### 3. Ribbons + transição (integração guitarra ⇄ bateria)
- No topo da **tela da guitarra**: uma **faixa fina da bateria** — play/pausa, BPM, e os 4
  compassos em mini-pauta com playhead (acompanhar sem abrir o módulo).
- No topo da **tela da bateria**: uma **faixa fina da guitarra** — os knobs do **amp + pedais
  ativos** (com o nome do efeito/amp), para ajustar o tom sem sair.
- **Transição morph:** clicar na faixa **expande** ela até virar a tela cheia do outro
  módulo; o inverso encolhe de volta. **Desenhe os quadros-chave** da transição (faixa →
  meio → tela cheia).

### 4. Tone Store (abre por cima)
Buscar e baixar **capturas NAM** e **IRs** de um serviço online (login OAuth). Abas
**Explorar / Minha biblioteca**; **cards de tone** (nome, autor, tags, botão baixar/instalar);
**catálogo de plugins VST3** (baixa direto). Estados: tela de login; grid de resultados;
item baixando; biblioteca local.

### 5. Áudio (config, standalone) e diálogos
Seleção de **driver / dispositivo / sample rate / buffer**; diálogos de **carregar arquivo**
(.nam, IR), **fórmula de compasso custom** (numerador/denominador), **salvar preset/compasso**.

---

## O que NÃO fazer
- Não reproduzir o design atual (cores, fontes, formas) — quero identidade nova.
- Não remover nenhuma feature; só repensar a apresentação.
- Não exigir tela cheia nem gestos de touch (é desktop, roda dentro de DAW).

## Resumo dos entregáveis
Sistema de design (tokens) + folha de componentes → mockup de **cada tela e estado** listado
→ tudo **rotulado com nomes funcionais** → de preferência em **HTML/CSS autocontido**.
