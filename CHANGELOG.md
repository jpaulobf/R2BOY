# Changelog

Todas as mudanças notáveis neste projeto serão documentadas neste arquivo.

## [Unreleased]

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
