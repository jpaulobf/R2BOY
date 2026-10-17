# R2BOY

A Game Boy and Game Boy Color emulator written from scratch in C++17, developed
alongside R2NES.

R2BOY is taking its first steps toward bringing the Game Boy library to the desktop,
with an interface built on SDL2 and Dear ImGui.

**Early development:** you can browse ROMs and explore cartridge details, but games
are not playable yet. Hardware emulation is the next major milestone.

## Getting started

Use **File > Open** or **Ctrl+O** to browse your ROM collection. Double-click a
folder to navigate or a ROM to open it. Use **Up** to go back; on Windows, the
**Drive** selector lets you switch drives.

- Open `.gb` and `.gbc` files, including ROMs inside `.zip` archives.
- Choose a ROM when an archive contains more than one game.
- Explore the title, GB/GBC compatibility, cartridge type, ROM/RAM sizes, and more
  in **Cartridge details**.

ZIP archives are opened without extracting files to disk. If a file cannot be
opened, the browser shows an error and keeps the previous cartridge loaded.
ROMs up to 8 MiB and ZIP archives up to 128 MiB are supported.

Close the window or use **File > Exit** to quit.

## Project structure

```text
src/
├── Common/            Shared types and constants
├── Core/              Game Boy hardware: CPU, video, audio, memory, and cartridges
├── System/            Main loop and platform input/audio
├── Util/              Utilities
├── Window/            Desktop interface and ROM browser
└── Emulator.h / .cpp  Application entry point
```

The hardware components are currently scaffolding. Game Boy and Game Boy Color
will share an emulation core, kept independent of the desktop interface.

## Building

You will need **CMake 3.14+**, a **C++17 compiler**, and **Git**. CMake downloads
SDL2, Dear ImGui, and miniz during the first configuration, so an internet
connection is required for that step.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The executable is generated in `bin/` (or `bin/Debug/` with multi-configuration
generators). On Windows with MinGW and Ninja, add `-G Ninja` to the configuration
command.

On Linux, you will also need build tools and the X11 or Wayland development
packages used by SDL2. Linux builds have not been validated yet.

## What's next

- Cartridge bank controllers, cartridge RAM, and the original Game Boy memory map.
- SM83 CPU implementation and instruction testing.
- Timers, interrupts, video, controls, and audio.
- Game Boy Color support.

## License

Released under the GNU General Public License v3.0. See [LICENSE](LICENSE).
