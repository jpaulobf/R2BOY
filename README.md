# R2BOY

Emulador de Game Boy / Game Boy Color em C++17, desenvolvido do zero em paralelo ao R2NES.

O projeto está no início: a interface usa Dear ImGui sobre SDL2, com uma área
de vídeo provisória na proporção de 160×144 pixels. Ainda não carrega ROMs
nem emula o hardware. Use File > Exit ou feche a janela para sair; Esc também
fecha quando o ImGui não está capturando o teclado.

`src/Emulator.cpp` contém o launcher e chama `Window`, em `src/Window/`,
que gerencia a janela, os eventos e a interface ImGui.

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
