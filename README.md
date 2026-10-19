# R2BOY

<div align="center">
  <img src="https://github.com/jpaulobf/R2BOY/blob/main/assets/r2boy2.png" height="450"/>
</div>

---

A Game Boy and Game Boy Color emulator written from scratch in C++17, developed
alongside R2NES.

R2BOY is taking its first steps toward bringing the Game Boy library to the desktop,
with an interface built on SDL3 and Dear ImGui.

**Early development:** you can browse ROMs and explore cartridge details, but games
are not fully playable yet. The CPU executes instructions and a FIFO PPU now
generates video; controls, audio and hardware timing accuracy are still under development.

## Getting started

Use **File > Load ROM** or **Ctrl+O** to browse your ROM collection. Click a
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

The SM83 CPU, memory subsystem, cartridge bank switching and IF/IE interrupt
registers, timer/divider, standalone serial transfers, OAM DMA and a FIFO PPU
are implemented. APU and joypad remain scaffolding. The emulation core stays
independent of the desktop interface.

The PPU emits pixels through separate background/window and object FIFOs, with
variable mode 3 duration, VBlank/STAT interrupts and CPU video-memory locks.
It supports DMG graphics and basic CGB attributes and palettes. Completed frames
are presented by SDL3; see [PPU implementation and accuracy limits](docs/PPU.md).

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
logo. Boot ROM, other peripheral registers and PPU/DMA access restrictions
are still pending. RAM starts with deterministic values for development, not
hardware-accurate power-on contents. ROM size codes outside 0x00–0x08 are rejected
for mapping even though the header parser can describe legacy sizes.

## CPU

The SM83 interpreter implements the defined base and CB-prefixed opcodes, flags,
stack operations and conditional timing. `Board::step()` runs one instruction
or interrupt entry and returns T-cycles (four per machine cycle). Each CPU bus
access and internal wait advances the Board by one M-cycle; bus accesses occur
at the end of that cycle. The Board advances peripherals in four one-T-cycle
steps, including during HALT. IF/IE, interrupt
priority, delayed EI, DI, RETI, HALT and the HALT bug are connected through the bus.
Invalid opcodes lock the CPU until reset. STOP currently waits for an explicit
wake-up; its API wait units do not advance the hardware clock. `clockCycles()`
reports the Board's actual hardware time, excluding those STOP waits. CGB speed
switching and detailed STOP behavior are not implemented.

Loading a ROM resets the CPU without executing a boot ROM: PC starts at `0x0100`,
SP at `0xFFFE`, and CPU registers follow DMG/CGB profiles, including the title-based
CGB compatibility values. This does not yet reproduce the entire machine's
post-boot state: video uses a deterministic initial phase, and audio and joypad
still need their implementations.
The desktop loop advances the CPU at the nominal normal-speed rate, with bounded
catch-up after pauses. Instruction totals come from the individual M-cycles,
without adding missing cycles at the end; Debug builds also check them against
the expected instruction totals. This is M-cycle synchronization, not a model
of the electrical phases within each T-cycle or all interrupt/DMA hardware quirks.
`CPU::setCycleObserver()` optionally traces reads, writes and internal cycles.

References: [Pan Docs instruction set](https://gbdev.io/pandocs/CPU_Instruction_Set.html),
[interrupts](https://gbdev.io/pandocs/Interrupts.html) and
[post-boot state](https://gbdev.io/pandocs/Power_Up_Sequence.html).
The CPU test suite covers all opcode slots and CB instructions, exhaustive 8-bit
ALU inputs, decimal arithmetic, signed SP offsets, branches, stack boundaries,
interrupts, serial transfers and execution of a synthetic ROM. Timing tests also
verify bus-access ordering, internal waits around stack operations and branches,
interrupt entry, HALT/STOP, and peripheral changes visible during an instruction.

## Timer and divider

`FF04`–`FF07` map DIV, TIMA, TMA and TAC. The 16-bit system divider advances every
T-cycle; TIMA increments on falling edges of divider bit 9, 3, 5 or 7 according
to TAC, giving periods of 1024, 16, 64 or 256 T-cycles. Writing DIV resets the
whole divider and can trigger an early TIMA increment. TAC changes also model
falling-edge increments, including the DMG/CGB difference when disabling it.

TIMA overflow exposes zero for four T-cycles before reloading TMA and requesting
the timer interrupt. Writes to TIMA during that delay cancel the reload/IRQ;
during the reload M-cycle, TIMA writes are ignored and TMA writes also update
TIMA. The timer keeps running during HALT; STOP resets DIV and suspends the clock.
Tests cover the frequency boundaries, write conflicts, IRQ entry and CPU reads
across an overflow/reload boundary. Behavior follows
[Pan Docs](https://gbdev.io/pandocs/Timer_Obscure_Behaviour.html).

Without a boot ROM, reset uses deterministic divider phases: `ABCC` on DMG,
`1EA0` on CGB native mode and `267C` on CGB compatibility mode. These do not
reproduce variable boot duration or user input. The revision-dependent race on
CGB timer enabling is modeled deterministically without an extra pulse. CGB
double-speed switching and the APU's divider-driven events remain pending.

## Running Blargg tests

To run a test through the desktop application, start `R2BOY.exe` from a terminal,
choose **File > Load ROM**, and click a test ROM such as
`cpu_instrs/individual/01-special.gb`.
The ROM's serial output appears live in the same console, including `Passed`,
`Failed`, or `Passed all tests`. Video output is not required. Keep the emulator
window open and unminimized while the test runs.

The desktop currently uses CGB hardware. As a temporary workaround, it resumes
STOP when the ROM has written a speed-switch request to KEY1, and logs a warning
once per ROM. Execution continues at normal speed: KEY1 and double-speed timing
are not emulated. Ordinary STOP still waits for wake-up. This allows the combined
`cpu_instrs` ROM to run, but does not validate CGB speed-switch or timing behavior.
The headless `rom_runner` retains the Core's STOP behavior without this workaround.

```powershell
.\bin\R2BOY.exe
```

With `BUILD_TESTING` enabled, `rom_runner` runs a Blargg ROM without
SDL or save files and prints the serial output:

```powershell
.\bin\rom_runner.exe "path\to\cpu_instrs\individual\01-special.gb" 500000000
```

The optional second argument limits T-cycles (default: one billion); a third
argument selects `dmg` (default) or `cgb`. Exit codes are 0 for a serial `Passed`
result, 1 for failure/error/CPU lock, and 2 when no result arrives before the limit.
The test ROMs are downloaded separately from
[Blargg's test ROM collection](https://github.com/retrio/gb-test-roms).

Validated: **all 11 individual `cpu_instrs` tests pass on both DMG and CGB models**.
The combined `cpu_instrs.gb` reports **"Passed all tests"** in DMG mode, and
`instr_timing.gb` also passes in DMG mode. The combined ROM in CGB mode times out
at its speed-switch STOP without the desktop workaround. With that workaround,
the combined suite reports **"Passed all tests"** (225,274,856 T-cycles), verified
with a headless harness using the desktop's observer and resume logic.
Mooneye has not yet been validated.
These results do not validate all peripheral timing details or full game compatibility.

Serial SB/SC transfers advance during each CPU M-cycle, not in a batch after an
instruction. A write to SC starts the transfer at that write boundary. With no cable attached,
internal-clock transfers receive `0xFF` and request the serial interrupt on
completion; external-clock transfers wait indefinitely. The transmitted bytes
are also printed in the desktop console. Physical link communication and exact
serial clock phase are not implemented.

## Building

You will need **CMake 3.16+**, a **C++17 compiler**, and **Git**. CMake downloads
SDL 3.4.18, Dear ImGui 1.92.9b, and miniz during the first configuration, so an internet
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
packages used by SDL3. Linux builds have not been validated yet.

When migrating an existing SDL2 checkout, configure a fresh build directory
(for example, `-B build-sdl3`) to avoid cached dependency paths. The interface
currently uses SDL_Renderer with the ImGui SDL3 backends; SDL_GPU presentation
and post-processing shaders are planned for the video implementation.

## What's next

- MBC3 RTC, additional controllers, boot ROM and memory-mapped peripheral registers.
- More hardware timing validation, including Mooneye and memory/interrupt edge cases.
- Controls, audio, PPU hardware-test validation and further T-cycle timing refinement.
- CGB double speed, VRAM DMA/HDMA and further Game Boy Color compatibility.

## License

Released under the GNU General Public License v3.0. See [LICENSE](LICENSE).
