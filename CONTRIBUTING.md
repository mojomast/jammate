# Contribuindo com o PedalForge NAM

Obrigado pelo interesse! Este guia cobre o essencial para compilar, mudar e enviar melhorias.

## 🔨 Ambiente e build

- **Windows 10/11 x64** · Visual Studio 2022 (Build Tools ou Community) com "Desktop development with C++" · CMake ≥ 3.22 · Git
- Clone e build:

```powershell
git clone http://192.168.15.49:3000/raphael/GuitarRigNAM.git
cd GuitarRigNAM
git submodule update --init --recursive third_party   # NÃO precisa de references/
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

- ASIO é opcional (ver README). O Standalone roda com WASAPI sem nada extra.
- Rode o Standalone: `build\GuitarRigNAM_artefacts\Release\Standalone\PedalForge NAM.exe`

## 🗂️ Mapa do código

| Arquivo | Responsabilidade |
|---|---|
| `src/PluginProcessor.*` | Áudio: cadeia de DSP, parâmetros (APVTS), carregamento de modelos NAM/IRs, presets, estado |
| `src/PluginEditor.*` | UI: top bar, cadeia (`ChainView`, drag-and-drop), afinador, knobs |
| `src/LookAndFeel.h` | Tema (paleta `ui::`), desenho de knobs/botões/chips |
| `src/StoreOverlay.*` | Tone Store (UI de busca/downloads/biblioteca) |
| `src/Tone3000Client.*` | API TONE3000: OAuth PKCE, busca, downloads, imagens |

## ⚡ Regras de ouro

1. **Real-time safety é inegociável**: dentro de `processBlock` (e de qualquer função chamada por ele) é **proibido** alocar memória, usar locks, fazer I/O, logar ou usar rede. Troca de dados com outras threads = atomics ou mecanismos RT-safe (veja o protocolo pending/retired de troca de modelos).
2. **Strings com acento**: `juce::String("texto")` interpreta `char*` como **Latin-1**. Todo literal com acento/símbolo deve usar `juce::String (juce::CharPointer_UTF8 ("..."))` ou `juce::String::fromUTF8`. O target compila com `/utf-8`.
3. **MSVC + lambdas**: `this` em init-capture de lambda aninhada resolve errado no MSVC — use `auto* self = this;` antes. `Component::SafePointer` precisa do argumento de template explícito.
4. **Segredos**: `tone3000.json` (chave/token do usuário) vive em `Documentos\PedalForge NAM\` e **jamais** entra no repositório. Nunca commite chaves, tokens ou senhas.
5. **Estilo**: siga o código ao redor (estilo JUCE: 4 espaços, chaves de Allman, `camelCase`). Comentários em PT-BR explicando o *porquê*, não o *o quê*.

## ✅ Antes de abrir um Pull Request

- [ ] Compila em Release sem erros novos (`cmake --build build --config Release`)
- [ ] Standalone abre e o áudio passa (teste com um capture de `third_party/NeuralAmpModelerCore/example_models/`)
- [ ] Mudou DSP? Descreva como testou o som (ideal: antes/depois)
- [ ] Mudou UI? Anexe screenshot no PR
- [ ] Presets antigos continuam carregando (compatibilidade de estado)
- [ ] Commits pequenos com mensagens descritivas

## 🧭 Por onde começar

Veja o **Roadmap** no README — itens não marcados são bem-vindos. Ideias novas: abra uma issue antes para alinhar o escopo. Os projetos em `references/` servem de referência de algoritmos (atenção às licenças descritas em `references/README.md` — só código MIT pode ser portado diretamente).

## 📜 Licença

Ao contribuir, você concorda que sua contribuição será licenciada sob a **AGPLv3** (mesma licença do projeto, exigida pelo uso do JUCE no tier open source).
