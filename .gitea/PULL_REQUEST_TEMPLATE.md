**O que este PR faz**


**Como foi testado** (build Release ✓? Standalone abriu ✓? Som verificado com guitarra/capture de exemplo?)


**Screenshot** (obrigatório para mudanças de UI)


**Checklist**
- [ ] Compila em Release sem erros novos
- [ ] Regra de real-time safety respeitada (nada de alocação/locks/IO no caminho de áudio)
- [ ] Literais com acento usam `CharPointer_UTF8`
- [ ] Presets antigos continuam carregando
- [ ] Nenhum segredo/token no diff
