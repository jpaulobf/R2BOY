# Changelog

Todas as mudanças notáveis do R2BOY são documentadas aqui.

## [Unreleased]

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
