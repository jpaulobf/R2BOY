# R2BOY

Emulador de Game Boy / Game Boy Color em C++17, desenvolvido do zero em paralelo ao R2NES.

O projeto está no início: a interface usa Dear ImGui sobre SDL2, com uma área
de vídeo provisória na proporção de 160×144 pixels. Carrega ROMs e exibe os
dados do cartucho, mas ainda não emula o hardware. Use File > Exit ou feche a janela para sair; Esc também
fecha quando o ImGui não está capturando o teclado.

`src/Emulator.cpp` contém o launcher e inicia `System/Engine`, que controla
o gameloop. `Window` gerencia a janela, os eventos e a interface ImGui.
`Window/FileManager` concentra a navegação e a seleção de ROMs, incluindo membros
de ZIP e mensagens de erro; `System/RomFile` lê os arquivos e extrai os bytes.

## Abrir uma ROM

Use **File > Open** ou **Ctrl+O** para abrir o navegador de arquivos ImGui.
Use duplo clique na lista para entrar em pastas ou abrir uma ROM. O caminho atual
aparece acima dos botões **Up** e **Cancel**; use **Up** para voltar à pasta anterior.
São aceitos `.gb`, `.gbc` e `.zip`, inclusive com extensões em maiúsculas.
No Windows, o seletor **Drive**, acima do caminho atual, permite navegar para a raiz
de outra unidade.

ZIPs são lidos em memória, sem extrair arquivos no disco. Quando há uma única
ROM, ela é aberta diretamente; quando há várias, a interface pede a seleção.
Nesse caso, **Up** retorna à pasta que contém o ZIP.
O limite é de 8 MiB por ROM e 128 MiB por ZIP. Arquivos corrompidos, membros
criptografados e ROMs truncadas produzem uma mensagem no seletor; o cartucho
anterior permanece carregado se a abertura falhar.

**Cartridge details** mostra título, compatibilidade GB/GBC, tipo de cartucho,
tamanhos de ROM/RAM, versão, região, fabricante e licença. Logo e checksums
divergentes são avisos para permitir ROMs de desenvolvimento. A extensão não
determina o modelo: a compatibilidade vem do header. Os offsets e checksums
seguem [Pan Docs](https://gbdev.io/pandocs/The_Cartridge_Header.html).
Reconhecer o tipo de cartucho ainda não implementa seu controlador MBC.

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
Os namespaces acompanham as pastas: `R2BOY::Common`, `R2BOY::System`,
`R2BOY::Window` e `R2BOY::Core` para a placa e os modelos. Cada subsistema
do núcleo possui seu namespace, como `R2BOY::Core::APU`,
`R2BOY::Core::Cartridge` e `R2BOY::Core::IO`; os controladores ficam em
`R2BOY::Core::Cartridge::MBC`. Os testes usam `R2BOY::Test`.

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
a SDL2, o Dear ImGui e o miniz (leitura de ZIP) na primeira configuração;
essa etapa precisa de acesso à internet.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

O executável é gerado em `bin/` (ou `bin/Debug/` com geradores de múltiplas
configurações). No Windows com MinGW e Ninja, acrescente `-G Ninja` à configuração.

No Linux, instale também as ferramentas de compilação e os pacotes de
desenvolvimento de X11 ou Wayland usados pela SDL2. O build Linux ainda não foi validado.

## Próximos passos

- Controladores MBC, RAM de cartucho e mapa de memória do Game Boy clássico (DMG).
- CPU SM83 e validação das instruções.
- Temporizadores, interrupções, vídeo, controles e áudio.
- Suporte a Game Boy Color (GBC).

## Licença

Distribuído sob a GNU General Public License v3.0. Consulte [LICENSE](LICENSE).
