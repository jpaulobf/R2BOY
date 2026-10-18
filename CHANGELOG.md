# Changelog

Todas as mudanças notáveis neste projeto serão documentadas neste arquivo.

## [Unreleased]

- Timer DIV/TIMA/TMA/TAC por T-cycle, bordas de DIV/TAC e diferenças DMG/CGB.
- Overflow com recarga atrasada, conflitos de escrita e interrupção do timer durante HALT.
- Blargg: 11/11 cpu_instrs individuais aprovados em DMG e CGB; ROM combinada e instr_timing aprovados em DMG.

- Clock intercalado por M-cycle em leituras, escritas e ciclos internos da CPU.
- Board avança a serial em T-cycles durante a instrução, sem repetir ciclos no fim.
- Rastreamento opcional de acessos e testes de ordem da pilha, desvios e interrupções.

- Porta serial SB/SC com clock interno, captura de saída e interrupção ao concluir.
- Executor rom_runner sem janela para ROMs Blargg, com resultado e limite de ciclos.

- CPU SM83 com instruções básicas/CB, flags, pilha e contagem de T-cycles por instrução.
- Registradores IF/IE, prioridade de interrupções, EI/DI/RETI, HALT e HALT bug.
- Perfis de CPU pós-boot DMG/CGB e execução de instruções no ciclo da aplicação.
- Testes de opcodes, aritmética, DAA, desvios, pilha, interrupções e ROM sintética.

- Mapa de memória com BUS, WRAM/echo, VRAM, OAM, HRAM e seleção de bancos CGB.
- Bancos de ROM/RAM para ROM simples, MBC1, MBC2, MBC3 sem RTC e MBC5.
- Restauração e gravação de RAM com bateria em saves/<CRC32>.sav junto ao executável.
- Testes de bancos, memória, persistência e preservação de estado após falhas.

- Abertura de ROMs .gb, .gbc e .zip em File > Open e Ctrl+O.
- Leitura do header, metadados e avisos de logo e checksums.
- Seleção de ROM em ZIPs com múltiplos jogos e extração em memória com miniz.
- Testes de leitura, arquivos inválidos e preservação do cartucho após falhas.


## [0.0.1] - 2026-10-17

- Launcher em Emulator.cpp e interface Dear ImGui/SDL2 em Window/.
- Esqueleto inicial em C++17 e SDL2, com janela e ciclo de eventos.
- Configuração de build e documentação inicial do R2BOY.
