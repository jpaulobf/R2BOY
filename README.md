# R2BOY

<div align="center">
  <img src="https://github.com/jpaulobf/R2BOY/blob/main/assets/r2boy2.png" height="450"/>
</div>

---

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

The memory subsystem and cartridge bank switching are implemented. CPU, PPU,
APU, DMA and the other peripherals remain scaffolding. The emulation core stays
independent of the desktop interface.

## Memory and battery saves

ROM reads and MBC register writes are routed through the bus, alongside cartridge
RAM, WRAM (including its echo), VRAM, OAM and HRAM. CGB mode supports the VRAM and
WRAM bank registers. Loading a cartridge clears volatile memory and resets bank
selection; resetting memory preserves cartridge RAM.

Supported controllers are ROM-only (including ROM+RAM), standard MBC1, MBC2,
MBC3 without RTC, and MBC5. Bank selection and RAM enable behavior follow
[Pan Docs](https://gbdev.io/pandocs/Memory_Map.html). MBC2 stores 512 four-bit cells;
MBC5 rumble state is tracked but is not sent to a physical controller.

Battery-backed RAM is restored from `saves/<CRC32>.sav` beside the executable.
Modified RAM is saved when switching games, every five seconds, and on normal
exit. The same ROM shares its save whether opened directly or from a ZIP.
Writes use a temporary file before replacing the previous save. A save with an
incompatible size produces an error and prevents replacing the current cartridge.
Saves contain raw RAM; MBC2 uses one byte per four-bit cell (512 bytes total).

RTC cartridges, MBC1M compilations and other controllers are not implemented;
unsupported types are rejected, as are MBC1M images detected by their secondary
logo. Boot ROM, peripheral registers, CPU execution and PPU/DMA access restrictions
are still pending. RAM starts with deterministic values for development, not
hardware-accurate power-on contents. ROM size codes outside 0x00–0x08 are rejected
for mapping even though the header parser can describe legacy sizes.

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

- MBC3 RTC, additional controllers, boot ROM and memory-mapped peripheral registers.
- SM83 CPU implementation and instruction testing.
- Timers, interrupts, video, controls, and audio.
- Game Boy Color support.

## License

Released under the GNU General Public License v3.0. See [LICENSE](LICENSE).
