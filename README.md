# R2BOY

Emulador de Game Boy / Game Boy Color em C++17, desenvolvido do zero em paralelo ao R2NES.

O projeto está no início: a interface usa Dear ImGui sobre SDL2, com uma área
de vídeo provisória na proporção de 160×144 pixels. Ainda não carrega ROMs
nem emula o hardware. Use File > Exit ou feche a janela para sair; Esc também
fecha quando o ImGui não está capturando o teclado.

`src/Emulator.cpp` contém o launcher e inicia `System/Engine`, que controla
o gameloop. `Window` gerencia a janela, os eventos e a interface ImGui.

## Organização do código

```text
src/
├── Common/                 Tipos e constantes compartilhados
├── Core/
│   ├── Board.h / .cpp      Composição do hardware emulado
│   ├── Model.h             Modelo físico (DMG/CGB) e modo de execução
│   ├── APU/                Áudio emulado
│   ├── BUS/                Mapa de endereços e roteamento de acessos
│   ├── CPU/                CPU SM83
│   ├── PPU/                Vídeo emulado
│   ├── Cartridge/
│   │   ├── Cartridge.h / .cpp
│   │   ├── Header.h / .cpp
│   │   └── MBC/            Controladores de bancos do cartucho
│   ├── Memory/             WRAM, HRAM, VRAM, OAM e boot ROM
│   └── IO/                 Joypad, Timer, Serial, Interrupts e DMA
├── System/                 Engine, entrada e saída de áudio na plataforma
├── Util/                   Utilitários criados conforme a necessidade
├── Window/                 Janela, eventos e apresentação com SDL/ImGui
└── Emulator.h / .cpp       Entrada da aplicação
```

Os componentes de hardware são esqueletos compiláveis, ainda sem emulação.
`Board` possui os componentes; suas conexões, reset e sincronização por ciclos
serão implementados junto com o hardware. O `Core` deve permanecer independente
de SDL e ImGui. GB e GBC compartilham esse núcleo; o modelo físico e o modo de
execução são conceitos separados para permitir jogos de GB no GBC.

`IO` reúne os periféricos menores. Os registradores de áudio e vídeo pertencem
a `APU` e `PPU`, respectivamente, com os acessos encaminhados pelo `BUS`.
`System/Input` fará o mapeamento de controles físicos para `Core/IO/Joypad`;
`System/Audio` enviará à plataforma as amostras produzidas pela `APU`.

## Compilar

Requisitos: CMake 3.14 ou superior, compilador C++17 e Git. O CMake baixa
a SDL2 e o Dear ImGui na primeira configuração; essa etapa precisa de acesso à internet.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

O executável é gerado em `bin/` (ou `bin/Debug/` com geradores de múltiplas
configurações). No Windows com MinGW e Ninja, acrescente `-G Ninja` à configuração.

No Linux, instale também as ferramentas de compilação e os pacotes de
desenvolvimento de X11 ou Wayland usados pela SDL2. O build Linux ainda não foi validado.

## Próximos passos

- Carregamento de cartuchos e mapa de memória do Game Boy clássico (DMG).
- CPU SM83 e validação das instruções.
- Temporizadores, interrupções, vídeo, controles e áudio.
- Suporte a Game Boy Color (GBC).

## Licença

Distribuído sob a GNU General Public License v3.0. Consulte [LICENSE](LICENSE).
