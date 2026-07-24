# Auditoria geral e proposta de melhorias — PedalForge NAM

**Data:** 24 de julho de 2026  
**Escopo:** programa completo, interface, experiência de uso, arquitetura e riscos técnicos.

O programa está muito acima de um protótipo: há identidade visual consistente, recursos profundos e uma boa integração entre guitarra, bateria e Tone Store. Porém, ele chegou ao ponto em que uma versão focada em consolidação traria mais valor que novos efeitos.

O principal problema geral é a hierarquia: controles críticos ficam pequenos e discretos, enquanto cards e áreas vazias ocupam bastante espaço.

## Prioridades recomendadas

| Prioridade | Melhoria | Impacto |
|---|---|---|
| P0 | Remover desalocação do `processBlock` | Evita possíveis glitches no áudio |
| P0 | Corrigir responsividade e contraste | Torna o programa utilizável em diferentes tamanhos/DPI |
| P0 | Adicionar testes automatizados | Evita regressões em presets, bateria e DSP |
| P0 | Proteger instalação de VST3 | Reduz risco de pacote adulterado ou extração insegura |
| P1 | Navegador compacto da cadeia | Reduz rolagem horizontal e facilita encontrar efeitos |
| P1 | Undo/Redo global | Protege edições de cadeia, bateria e geração |
| P1 | Sincronização com DAW | Faz bateria acompanhar BPM, play/stop e posição do host |
| P1 | Redesenhar o modo Stage | Torna-o realmente útil ao tocar |
| P2 | Song/Scene Mode | Integra bateria, presets e automação por seção |
| P2 | MIDI Learn e footswitch | Essencial para uso sem tirar as mãos da guitarra |

## Interface da guitarra

A cadeia funciona bem conceitualmente, mas fica extensa muito rápido. Input e Mixer podem sair da área visível, enquanto alguns cards usam muita altura sem informação proporcional.

Sugestões:

- Criar um “modo compacto”: todos os efeitos aparecem como pedais menores e somente o selecionado expande seus knobs.
- Manter Input e Output/Mixer fixos nas laterais.
- Adicionar uma minimapa da cadeia com indicação da região visível.
- Fazer duplo clique num card centralizá-lo.
- Mostrar valores dos knobs com contraste maior.
- Em bypass, manter o card com aproximadamente 65% de visibilidade e acrescentar explicitamente “BYPASS”; atualmente o conteúdo fica escuro demais.
- Transformar o menu de efeitos em um navegador pesquisável com categorias, favoritos e “usados recentemente”. A lista atual ultrapassa a altura da janela.
- Dar nomes mais legíveis às capturas, separando “nome amigável” do nome completo do arquivo.

A janela aceita escala até 50%, mas isso reduz textos e alvos para tamanhos praticamente inutilizáveis. Recomenda-se limitar a redução ou implementar layout adaptativo, em vez de escalar o canvas inteiro.

## Modo Stage

É a tela que mais merece um redesenho. Hoje ela fica majoritariamente vazia, enquanto transporte, preset e afinador poderiam ocupar áreas grandes e operáveis à distância.

Divisão proposta:

- Preset e captura no topo.
- Afinador grande no centro.
- Estado do AMP, MUTE, bateria e gravação em blocos grandes.
- BPM, play/stop e próxima seção na parte inferior.
- Avisos grandes de CLIP, CPU e Auto-ECO.
- Opção para ocultar completamente o ribbon de bateria.
- Ações configuráveis para teclado/MIDI.

## Bateria

A partitura como timeline é uma das melhores ideias do programa. Os ganhos agora seriam de edição e legibilidade:

- Garantir margem depois da última nota do quarto compasso; ela ainda fica muito próxima da borda.
- Na Grade, calcular a largura das células dinamicamente. A largura fixa atual faz com que os 16 passos não caibam claramente.
- Diferenciar toque, acento e ghost não apenas por cor: usar ponto, `>`, círculo vazado ou símbolos.
- Adicionar Undo/Redo para notas, fórmulas, geração e movimentação de compassos.
- Oferecer copiar/colar, duplicar e arrastar compasso com modificadores.
- Adicionar busca, favoritos e recentes à biblioteca de aproximadamente 460 grooves.
- Incluir “Tap BPM” e entrada numérica direta.
- No gerador, permitir travar parâmetros e regenerar apenas partes do groove.
- Adicionar o mini-mixer por peça já previsto no roadmap.
- Em larguras menores, mostrar dois compassos por linha em vez de reduzir toda a tela.

## Melhor evolução de produto: Song/Scene Mode

Esta seria a principal aposta para transformar o aplicativo em um verdadeiro companion:

- Cada seção de bateria recebe um snapshot do rig.
- Verse pode usar clean; Chorus, drive; Bridge, delay; Fill, boost.
- Mudança automática de preset quando o playhead entra na seção.
- BPM, fórmula, preset, estados de pedais e automações salvos como uma música.
- Setlist com próxima música e contagem.
- Controle por footswitch/MIDI.

Também falta sincronização com a DAW. Atualmente a bateria usa apenas seu BPM interno, sem integração com o playhead/PPQ do host. O plugin também declara que não recebe MIDI.

## Tone Store e Plugins

A correção recente resolveu as sobreposições. Próximas melhorias:

- Tornar a barra de filtros responsiva, quebrando linha somente quando necessário.
- Substituir “Add” por ações específicas: “Load in AMP 1”, “Replace IR” ou “Add parallel rig”.
- Permitir audição temporária/A-B antes de substituir a captura atual.
- Normalizar nomes enormes da biblioteca e deixar o nome do arquivo num tooltip.
- Trocar “offline ok” por “Available offline” ou “Downloaded”.
- Mostrar tamanho, arquitetura, sample rate e licença antes do download.
- Compactar as linhas do catálogo de plugins; hoje há bastante espaço vazio.
- Remover o caminho técnico de instalação da área principal e colocá-lo em “Detalhes”.

A instalação de VST3 baixa e extrai ZIPs sem verificação SHA-256 explícita. Recomenda-se checksum fixado, limite de tamanho, extração em diretório temporário, validação de caminho contra `../` e promoção atômica após validação.

O refresh token do TONE3000 é salvo em JSON. No Windows, seria melhor protegê-lo com DPAPI ou Credential Manager.

## Áudio e acessibilidade

O diálogo de áudio é o componente que mais destoa visualmente, por usar a aparência padrão do JUCE/Windows. Recomenda-se um painel próprio com:

- Driver, dispositivo, entrada, saída, sample rate e buffer.
- Latência estimada em destaque.
- Medidor de entrada e teste de saída.
- Apply/Cancel e indicação de reinicialização do dispositivo.
- Mensagem clara quando o dispositivo desconectar.

Os textos secundários usam opacidade baixa, o que explica a legibilidade insuficiente, especialmente no tema claro. Além disso, quase todos os botões recusam foco por teclado. Devem ser adicionados foco visível, navegação por Tab e estados que não dependam exclusivamente de cor.

## Riscos técnicos encontrados

- Há um `delete` executado dentro de `processBlock`, contrariando a regra RT-safe documentada no próprio código. Deve ir para uma fila de objetos aposentados e ser coletado fora da thread de áudio.
- O timer de UI roda a 30 Hz e consulta arquivos `.meta`, interpreta JSON e verifica sidecars repetidamente. Isso deveria ser cacheado e invalidado somente quando o arquivo/caminho mudar.
- Não foram encontrados testes próprios nem pipeline CI do aplicativo. Há testes apenas nas dependências.
- Os quatro arquivos principais somam mais de 12 mil linhas. Vale separar cards, navegador de efeitos, transporte, persistência e telas em componentes menores.
- Presets e manifestos precisam de escrita atômica, backup e recuperação da última sessão.
- VST3s externos podem derrubar o processo inteiro; seria útil um “Safe Start” que desativa o último plugin carregado após uma saída anormal.

## Ordem sugerida de execução

1. Segurança de áudio.
2. Responsividade e contraste.
3. Grade da bateria e navegador de efeitos.
4. Undo/Redo.
5. Sincronização com DAW.
6. Modo Stage.
7. Song/Scene Mode e MIDI Learn.

