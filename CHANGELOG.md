# Changelog

Todas as mudanças notáveis do R2BOY são documentadas aqui.

## [Unreleased]

- **Hacks > Enable Unlimited Sprites**, habilitado por padrão, permite até 40
  sprites por linha, mantendo as prioridades DMG/CGB e a temporização do pipeline
  original. Desligado, restaura o limite de dez sprites por linha.
- Save states usam exclusivamente o formato 3 para preservar os sprites extras
  selecionados na linha. Estados de formatos anteriores precisam ser recriados;
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
