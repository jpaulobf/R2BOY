# R2BOY

<div align="center">
  <img src="https://github.com/jpaulobf/R2BOY/blob/main/assets/r2boy2.png" height="450"/>
</div>

A Game Boy and Game Boy Color emulator written from scratch in C++, developed
alongside R2NES.

R2BOY is a work in progress. Game compatibility and graphics accuracy are still
improving, and audio is not available yet.

## Features

- Open Game Boy and Game Boy Color games from `.gb`, `.gbc`, or `.zip` files.
- Play with a keyboard or gamepad, with turbo buttons and fast-forward.
- Automatic battery saves and three save state slots.
- Windowed and fullscreen display, color palettes, scanlines and bilinear filtering.
- Optional CPU overclock and unlimited sprites.

## Getting started

Launch R2BOY, choose **File > Open ROM...** (or press **Ctrl+O**) and select a
game. If a ZIP contains multiple games, choose one from the list.

The **Display** menu provides window scales, fullscreen modes and visual options.
Press **Esc** to leave fullscreen.

## Controls

| Game Boy | Keyboard |
| --- | --- |
| D-pad | W / A / S / D |
| A | K |
| B | J |
| Turbo A | I |
| Turbo B | U |
| Start | Enter |
| Select | Backspace |

Gamepads can use the D-pad or left stick to move. With Xbox button labels,
A / B map to Game Boy B / A, and X / Y provide Turbo B / Turbo A.
Use **Input > Invert BA/YB Buttons** to switch the action-button layout.

| Shortcut | Action |
| --- | --- |
| Ctrl+O | Open a ROM |
| Ctrl+1 to Ctrl+5 | Set window size to 1X through 5X |
| F5 | Save state |
| F6 | Load state |
| F12 | Reset the game |
| Pause/Break | Pause or resume |
| Tab (hold) | Fast-forward, when enabled in Hacks |
| Esc | Leave fullscreen |

## Saves

Battery saves are stored automatically in the `saves/` folder beside the
executable. Save states use the same folder; choose one of three slots in
**File > Save State Slot**, then press **F5** to save or **F6** to load.

Save states may become incompatible after an update. Battery saves remain
compatible.

## Known limitations

Some games may show graphical glitches or fail to run correctly. Audio,
cartridge clocks (RTC) and parts of Game Boy Color support are still pending.
Optional hacks can affect compatibility and can be toggled in the **Hacks** menu.

## Building

You need **CMake 3.16+**, a **C++17 compiler** and **Git**. The first setup
downloads the required libraries and needs internet access.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

The executable is generated in `bin/` or `bin/Debug/`, depending on your build
setup. For MinGW with Ninja on Windows, add `-G Ninja` to the first command.
Linux also requires X11 or Wayland development packages; Linux builds have not
been validated yet.

## License

Released under the GNU General Public License v3.0. See [LICENSE](LICENSE).
