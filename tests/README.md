# Testes do GlyphShift

Guia para testar as fixtures, os ajustes visuais e o SuperTux pelo Proton.
O [README principal](../README.md) continua sendo a referência de escopo,
dependências e resultados de validação.

Execute os comandos abaixo **na raiz do repositório**, em uma sessão X11,
como usuário normal e **sem sudo**.

## Preparar o build

```bash
cmake --preset dev
cmake --build --preset dev
```

Os exemplos usam o preset editável `presets/readable.conf`. Seu conteúdo pode
ter sido alterado durante testes anteriores; os valores atuais desse arquivo
serão usados. Para experimentar sem modificar um arquivo versionado, copie-o
para `/tmp/glyphshift-manual.conf` e use esse caminho em `GLYPHSHIFT_CONFIG`.

## Fixture OpenGL/GLX: editor e comparação visual

```bash
setpriv --inh-caps=-all --ambient-caps=-all -- \
  env GLYPHSHIFT_CONFIG="$PWD/presets/readable.conf" \
  ./build/dev/glyphshift -- ./build/dev/glyphshift-glx-fixture
```

A janela mostra um gradiente animado transformado em glifos.

| Controle | Resultado esperado |
| --- | --- |
| Insert | Abre ou fecha o editor. |
| Glyph strength | Ajusta a intensidade entre 0 e 1, ao vivo. Ctrl-clique permite digitar um valor. |
| Cell width / Cell height | Ajustam o tamanho das células independentemente. |
| Monochrome | Alterna entre cor da cena e monocromático. |
| F10 | Desativa/ativa o efeito para comparar com o original. |
| Escape com editor aberto | Fecha apenas o editor e devolve o input à fixture. |
| Escape com editor fechado | Encerra a fixture. |

Teste intensidades `0`, `0.25`, `0.45`, `0.75` e `1`. Redimensione a janela e
confirme que o efeito e o editor continuam funcionando. No i3, coloque a janela
em modo flutuante antes de testar tamanhos específicos.

Para testar persistência, ajuste os controles, dê um nome ao preset e clique
em **Save preset**. Feche a fixture e reabra **sem o arquivo externo**, para
conferir os valores persistidos:

```bash
setpriv --inh-caps=-all --ambient-caps=-all -- \
  env -u GLYPHSHIFT_CONFIG \
  ./build/dev/glyphshift -- ./build/dev/glyphshift-glx-fixture
```

O preset selecionado e os valores salvos devem voltar. Um arquivo indicado em
`GLYPHSHIFT_CONFIG` tem precedência na inicialização e quando seu conteúdo muda;
por isso ele deve ser removido do ambiente nessa conferência de persistência.

## Fixture Vulkan

O mesmo procedimento de controles e comparação vale para Vulkan:

```bash
setpriv --inh-caps=-all --ambient-caps=-all -- \
  env GLYPHSHIFT_CONFIG="$PWD/presets/readable.conf" \
  ./build/dev/glyphshift -- ./build/dev/glyphshift-vulkan-fixture
```

Com a camada Khronos instalada e disponível ao loader, habilite validação:

```bash
setpriv --inh-caps=-all --ambient-caps=-all -- \
  env GLYPHSHIFT_CONFIG="$PWD/presets/readable.conf" \
  VK_LAYER_VALIDATE_SYNC=1 \
  ./build/dev/glyphshift -- ./build/dev/glyphshift-vulkan-fixture --validation
```

Se a camada estiver em um diretório externo, acrescente `VK_LAYER_PATH` ao
`env`. Nesta sessão, foi extraída em
`/tmp/glyphshift-m4-validation/root/usr/share/vulkan/explicit_layer.d`.
`--validation` falha explicitamente se a camada não estiver disponível.

## SuperTux Windows pelo GE-Proton, sem Steam

Este comando usa os arquivos portáteis **já preparados nesta máquina**:

- GE-Proton11-9 em `/tmp/glyphshift-m5-apps/GE-Proton11-9-x86_64`;
- SuperTux 0.7.0 Windows em `/tmp/glyphshift-m5-apps/supertux/SuperTux-portable`;
- prefixo Proton em `/tmp/glyphshift-m5-proton-prefix`;
- diretório Steam vazio em `/tmp/glyphshift-m5-steam-empty`;
- dados de teste do jogo em `/tmp/glyphshift-m5-supertux-user`;
- wrapper de bloqueio de rede em `/tmp/glyphshift-offline`.

Eles não fazem parte do checkout e podem desaparecer após reiniciar ou limpar
`/tmp`. O comando não baixa nem instala Steam, Proton ou o jogo. A configuração
de teste do SuperTux foi preparada com rede, checagem de atualizações e
integração com Discord desabilitadas. Use somente essa execução offline, sem
anti-cheat ou serviços online ativos.

```bash
setpriv --inh-caps=-all --ambient-caps=-all -- \
  env \
  GLYPHSHIFT_CONFIG="$PWD/presets/readable.conf" \
  STEAM_COMPAT_DATA_PATH=/tmp/glyphshift-m5-proton-prefix \
  STEAM_COMPAT_CLIENT_INSTALL_PATH=/tmp/glyphshift-m5-steam-empty \
  PROTONFIXES_DISABLE=1 \
  WINEDEBUG=-all \
  SteamGameId=0 \
  /tmp/glyphshift-offline \
  ./build/dev/glyphshift --profile proton-supertux -- \
  /usr/bin/python3 /tmp/glyphshift-m5-apps/GE-Proton11-9-x86_64/proton run \
  /tmp/glyphshift-m5-apps/supertux/SuperTux-portable/bin/supertux2.exe \
  --userdir 'Z:\tmp\glyphshift-m5-supertux-user' \
  --window --geometry 800x600 --disable-sound --disable-music
```

O wrapper bloqueia a criação de sockets IPv4/IPv6 no processo e seus filhos,
mantendo Unix/X11 disponíveis. Ele não isola o filesystem. Não remova o wrapper
para contornar sua ausência: restaure primeiro os arquivos do ambiente de teste.

**Insert e os atalhos do GlyphShift não foram validados no Wine; Insert não
abriu o editor no SuperTux testado.** Faça os ajustes pelo arquivo indicado em
`GLYPHSHIFT_CONFIG`. Com o jogo aberto, edite e salve esse arquivo; a mudança
aparece ao vivo, sem reabrir o jogo.

Por exemplo:

```ini
enabled=true
cell_width=6
cell_height=10
color_mode=source
glyph_strength=0.45
```

| Valor | O que observar |
| --- | --- |
| `glyph_strength=0` | Cena original em modo source; tons de cinza em modo mono. |
| `glyph_strength=0.25` ou `0.45` | Mais detalhe da cena preservado junto dos glifos. |
| `glyph_strength=0.75` | Maior presença dos glifos. |
| `glyph_strength=1` | Glifos puros; com mono, saída binária preta/branca. |
| `color_mode=source` / `mono` | Alterna cor da cena e monocromático. |
| `enabled=false` | Imagem original, independentemente do modo de cor. |

Os presets fornecidos são [readable](../presets/readable.conf),
[mono-readable](../presets/mono-readable.conf) e
[classic](../presets/classic.conf). Para iniciar com outro, altere o caminho de
`GLYPHSHIFT_CONFIG` antes de executar o comando.

O menu e a introdução de uma fase foram usados nos testes anteriores;
movimentação durante a partida não foi validada. Uma rodada posterior confirmou
a troca de estilos, mas o jogo recusou o redimensionamento solicitado. Essa
rodada não conta como aprovação completa de resize/encerramento. Não assuma
compatibilidade geral com jogos Proton a partir desse teste.

## Erros comuns

### `refusing to launch from a process with capabilities`

Use o prefixo `setpriv --inh-caps=-all --ambient-caps=-all --` presente nos
comandos deste guia. Ele remove capabilities herdáveis e ambientes apenas da
execução filha, sem alterar o terminal ou configurações do sistema. O launcher
continua recusando root e processos privilegiados; não use sudo para resolver.

### Display inacessível

Rode no terminal da sessão X11 com `DISPLAY` e a autorização X11 disponíveis.
Wayland nativo não é um backend de input suportado. Os smoke tests retornam
skip (77) quando não conseguem acessar o display; isso não significa que a
renderização passou.

### Jogo ou wrapper não encontrado

Confira os caminhos em `/tmp` da seção SuperTux. Eles são específicos do
ambiente preparado durante o desenvolvimento e não são instalados pelo CMake.
As fixtures podem ser testadas independentemente desses arquivos.

## Verificações automatizadas

Testes de CPU, configuração, presets, launcher e interposição:

```bash
ctest --preset dev
```

Smoke tests gráficos sequenciais, com X11/GPU e camada Khronos disponíveis:

```bash
setpriv --inh-caps=-all --ambient-caps=-all -- \
  cmake --build --preset dev --target smoke
```

Com a camada temporária desta máquina, o comando equivalente é:

```bash
setpriv --inh-caps=-all --ambient-caps=-all -- \
  env VK_LAYER_PATH=/tmp/glyphshift-m4-validation/root/usr/share/vulkan/explicit_layer.d \
  cmake --build --preset dev --target smoke
```

As capturas PPM e os logs ficam em `build/dev/smoke-evidence/`. Os testes cobrem
input/editor, intensidade 0/1/fracionária, preset após reinício, seis tamanhos,
múltiplos contextos/swapchains e apresentação original em falhas suportadas.
Mantenha as janelas de teste visíveis e desobstruídas durante as capturas.

Para incluir os smoke tests no CTest Release:

```bash
cmake --preset release -DGLYPHSHIFT_GPU_SMOKE_TESTS=ON
cmake --build --preset release
setpriv --inh-caps=-all --ambient-caps=-all -- ctest --preset release
```

Se a camada Khronos não estiver instalada no sistema, use o mesmo
`env VK_LAYER_PATH=...` mostrado acima antes de `ctest`.

Os resultados efetivamente observados (10 testes Debug, 12 Release, Mesa GLX e
limitações do Vulkan Mesa/Proton) estão no [README principal](../README.md).
Este guia registra como reproduzir os testes; sua criação não representa uma
nova execução dos testes gráficos ou do jogo.
