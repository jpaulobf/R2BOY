# Changelog

Todas as mudanças notáveis do R2BOY são documentadas aqui.

## [Unreleased]

- Corrigida a tela branca após iniciar Animaniacs (USA), CRC32 673C815D:
  NR50/NR51 agora preservam volume e roteamento, com alimentação por NR52.
  A ROM dependia da leitura de NR50 para concluir o fade de áudio. Canais e
  geração de som continuam pendentes. Savestates passam ao formato 10 para
  guardar esses controles; estados anteriores precisam ser recriados.
- Regressões de leitura/escrita do mixer, alimentação, reset, savestates,
  acesso durante DMA e conclusão de um fade pela CPU em DMG/CGB.
- Fetcher compartilhado: BG avança até o estágio de liberação de OBJ; os seis
  dots de busca de sprite ficam separados da espera, sem recalcular a fase
  por SCX/WX nem manter um identificador artificial do último tile.
- Push do BG aguarda o FIFO vazio; endereço em T1 e dado em T2, com LCDC.4 por
  plano e SCY por plano no DMG. Sprites à esquerda são buscados antes do primeiro
  pixel visível. CGB mantém o perfil de Y compartilhado de revisões D ou posteriores.
- Testes de avanço do BG durante espera de OBJ, SCX alterado entre sprites
  sobrepostos e restauração nas fases de endereço, espera e leitura de planos.
  Comparações Mealybug têm resultados mistos: quatro imagens melhoram e três
  pioram, incluindo SCY e dois casos de janela. Dados em `tests/PPUAlignmentValidation.md`.
- Revisão de bloqueios e interrupções de vídeo: o pulso espúrio de escrita em
  STAT no DMG mantém as seleções internas ativas por um M-cycle, sem pedidos
  repetidos enquanto a linha permanece alta. O pulso também observa mudanças
  de modo e é cancelado ao desligar o LCD; não é aplicado ao CGB físico.
- Regressões de acesso aos limites de VRAM/OAM, paletas CGB em normal/double
  speed, bloqueio por DMA com LCD off e entrega de VBlank/STAT após HALT/DMA.
  Resultados e falhas de fase ainda abertas em `tests/PPUAccessValidation.md`.
- Revisão de sprites: tile/atributos buscados em modo 3 e tamanho LCDC.2
  amostrado separadamente para cada plano de cor. No DMG, desabilitar OBJ
  durante a busca cancela a transferência pendente; no CGB físico a busca
  continua, inclusive no modo de compatibilidade DMG.
- Regressões de seleção OAM, prioridade antes da máscara do BG, transparência,
  alinhamento de SCX e restauração entre planos do sprite. Dois testes Mealybug
  de tamanho melhoram parcialmente, sem piorar as 19 imagens selecionadas.
  Resultados e limites em `tests/PPUSpriteValidation.md`.
- Revisão de tiles, scroll e janela: no DMG, desligar a janela durante modo 3
  termina o tile corrente e retoma o BG sem novo descarte fino de SCX. Um novo
  disparo de WX na mesma linha avança a linha interna da janela.
- Comparador horizontal da janela exige encontro com WX; habilitar a janela
  depois de passar dessa posição não a faz aparecer imediatamente.
- Executor de ROMs com captura Mealybug em PPM e comparação de imagens por pixel.
  O teste `m3_lcdc_win_en_change_multiple` passou de 8.316 diferenças para zero;
  resultados completos em `tests/PPUFetchValidation.md`.
- Primeira revisão de temporização da PPU: coincidência LY/LYC congelada com
  LCD desligado, partida sem modo 2 normal na primeira linha e pedido de STAT
  por modo 2 na entrada em VBlank do DMG, respeitando o bloqueio pela linha STAT.
- Regressões por dot para dois frames completos e todos os valores de SCX fino,
  transições LCD off/on e restauração dos estados de coincidência e partida.
  Resultados e limites em `tests/PPUTimingValidation.md`.
- Clock serial derivado do contador de sistema compartilhado com DIV/TIMA,
  preservando a fase entre transferências e observando escritas em DIV e STOP.
  Corrige o teste Mooneye de alinhamento serial pós-boot do DMG.
- Serial suspensa junto ao contador durante a estabilização de KEY1/STOP.
- Regressões para Timer/Serial/IF/IE durante DMA, prioridade das IRQs, fase do
  clock normal/rápido e restauração durante um bit parcialmente transmitido.
- Save states usam exclusivamente o formato 9 para preservar a fase serial,
  a coincidência LY/LYC congelada, a partida do LCD, a posição dentro do tile
  da janela, o pulso pendente de escrita em STAT e a arbitragem/endereço do fetcher.
  Estados anteriores precisam ser recriados; saves de bateria continuam compatíveis.
- Revisão de Timer, Interrupts e Serial registrada em `tests/IOValidation.md`,
  incluindo a falha ainda presente em `timer/rapid_toggle`.

## [0.0.6] - 2026-10-24

- Pausa automática enquanto menus, submenus ou o seletor de ROMs estão abertos.
  Ao fechar a interface, retoma sem acumular tempo de emulação, preservando a
  pausa manual de Pause/Break.
- Revisão de BUS/DMA: VRAM separada do barramento principal no DMG, acesso a
  I/O/IE durante a transferência e leituras DMG em conflito retornando o byte
  transferido. Corrige nove timeouts de testes de CPU e o teste de leitura de FF46.
- Regressões de execução de instruções durante DMA, troca de barramento no
  reinício e restauração do dado de DMA nos estados. Validação em
  `tests/BusDMAValidation.md`.
- Revisão da CPU: seleção de IRQ e reconhecimento de IF durante o empilhamento
  do PC, incluindo cancelamento e mudança de vetor por escritas em IE/IF.
- Regressões para entrada de interrupções e HALT bug com prefixo CB e RST;
  executor de ROMs com protocolo Mooneye e hacks desabilitados para validação.
- Resultados da revisão e limites conhecidos registrados em `tests/CPUValidation.md`.

- **Hacks > Enable Unlimited Sprites**, habilitado por padrão, permite até 40
  sprites por linha, mantendo as prioridades DMG/CGB e a temporização do pipeline
  original. Desligado, restaura o limite de dez sprites por linha.
- Save states usam exclusivamente o formato 4 para preservar o dado de DMA e
  os sprites extras selecionados na linha. Estados anteriores precisam ser recriados;
  saves de bateria `.sav` continuam compatíveis.
- Submenu **Display > Palette Preset** com **Default**, **Classic Green**,
  **Pocket**, **Amber**, **Ocean** e **Neon** para jogos DMG, inclusive em compatibilidade
  no CGB. A seleção funciona durante a pausa e em conjunto com scanlines,
  preservando as paletas dos jogos Color.
- Preset **Neon** com tons saturados de rosa, violeta e azul profundo.

## [0.0.2] - 2026-10-22

### Jogos e controles

- Save states com três slots por ROM, pelo menu File ou pelos atalhos **F5**
  (salvar) e **F6** (carregar). Os arquivos ficam na pasta `saves/`.
- Tecla **Pause/Break** para pausar e retomar a emulação, mantendo o último quadro
  visível e os menus disponíveis para facilitar capturas de tela.
- Menu **Hacks** com **Enable Fast Forward**, ativado por padrão. Segurar **Tab**
  executa sem limite de FPS e suspende temporariamente o VSync.
- **Enable CPU Overclock**, desligado por padrão: dobra os ciclos disponíveis à
  CPU sem acelerar o relógio de vídeo. Funciona em DMG e CGB, inclusive em conjunto
  com double speed, e exibe ` | CPU OVERCLOCK ENABLED! GAMES CAN CRASH!` após os FPS
  no título enquanto estiver ativo.

### Janela e imagem

- **VSync** no menu Display, ativado por padrão e conectado ao loop da Engine.
  A emulação acompanha a frequência do monitor e pode acelerar em telas com
  refrescagem alta. Desligado, mantém o limite de apresentação de 60 FPS.
- **Scanlines** opcionais, com intensidade ajustável entre **5% e 25%**.
- Escala inicial alterada de **3X** para **4X**.
- Ícone próprio para a aplicação no Windows.

### Emulação e estabilidade

- Double speed nativo do CGB via **KEY1/STOP**, substituindo o contorno que mantinha
  a CPU na velocidade normal. Inclui troca nos dois sentidos, pausa de
  estabilização e preservação do estado da troca nos save states.
- Separação entre os relógios da CPU e do vídeo para manter a cadência da PPU
  durante double speed e overclock.
- Overclock suspenso durante a partida e execução do DMA de OAM e durante a troca
  de velocidade CGB. Protege rotinas de espera por ciclos contra retornos de pilha
  antes do término do DMA.
- Limpeza do índice de sprites ao iniciar uma linha, corrigindo a rejeição de
  save states capturados nessa transição.
- Testes ampliados para save states, menus, VSync, pausa, fast-forward, overclock,
  relógios CGB e rotinas de DMA executadas pela CPU.

### Compatibilidade e limitações

- Save states usam o **formato 2**. Estados de builds de desenvolvimento no formato
  1 precisam ser recriados; saves de bateria `.sav` continuam compatíveis.
- O overclock é experimental e pode causar artefatos visuais ou travamentos.
- A PPU ainda apresenta limitações de precisão em DMG e CGB; o DMA de VRAM
  **GDMA/HDMA** do CGB permanece pendente.
- Áudio e relógio de cartuchos ainda não estão disponíveis.

## [0.0.1] - 2026-10-20

Primeira versão do R2BOY, ainda em desenvolvimento.

### Jogos e controles

- Emulação de Game Boy e Game Boy Color com vídeo.
- Abertura de ROMs `.gb`, `.gbc` e `.zip`, incluindo seleção de jogos em arquivos ZIP.
- Controles por teclado e gamepad, com botões turbo e opção de inverter o layout.
- Saves de bateria automáticos ao trocar, reiniciar ou descarregar jogos, a cada
  cinco segundos e ao sair normalmente.
- Opções **Unload** e **Reset** no menu File, com **F12** para reiniciar o jogo.

### Janela e imagem

- Escalas de **1X a 5X**, com **3X** como padrão e centralização ao aplicar a escala.
- Fullscreen sem bordas, nos modos esticado e proporcional.
- Menu oculto em fullscreen; **ESC** retorna à última escala selecionada sem
  fechar o emulador.
- Filtro bilinear opcional e preservação da proporção ao redimensionar a janela.
- Tela preta sem ROM e FPS no título somente com jogo carregado.
- Apresentação com alvo de 60 FPS e espera ajustada ao tempo de processamento.

### Base do emulador

- CPU, memória, interrupções, timer, vídeo, entrada e saída serial implementados.
- Suporte a cartuchos ROM simples, MBC1, MBC2, MBC3 sem relógio e MBC5.
- Interface com SDL3 e Dear ImGui, compilação com CMake e testes automatizados.
- Executor de ROMs de teste sem janela; testes individuais de CPU Blargg
  validados nos modelos DMG e CGB.

### Limitações conhecidas

- Áudio, relógio de cartuchos e suporte completo aos recursos do Game Boy Color
  ainda não estão disponíveis.
- Compatibilidade com jogos e precisão da emulação continuam em desenvolvimento.
- A troca de velocidade do Game Boy Color usa um contorno temporário no aplicativo
  e continua executando em velocidade normal.
