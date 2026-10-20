# R2BOY

<div align="center">
  <img src="https://github.com/jpaulobf/R2BOY/blob/main/assets/r2boy2.png" height="450"/>
</div>

A Game Boy and Game Boy Color emulator written from scratch in C++, developed
alongside R2NES.

**Version 0.0.1** — see the [changelog](CHANGELOG.md) for release notes.

**Work in progress:** games can run with video, keyboard and gamepad controls,
and automatic battery saves. Compatibility is still improving, and audio is
not available yet.

## Playing

1. Launch R2BOY.
2. Open **File > Load ROM...** or press **Ctrl+O**.
3. Choose a `.gb`, `.gbc`, or `.zip` file. If a ZIP contains multiple games,
   select the one you want to play.

Use **File > Reset** or **F12** to restart the current game, keeping its battery
save. **File > Unload** removes the ROM and returns to the black idle screen.
Both options are disabled when no game is loaded.

To quit, close the window or choose **File > Exit**. Escape never closes the emulator.

## Display

The emulator starts at **3X**. Choose **1X to 5X** in the **Display** menu to resize
and center the window. Resizing it manually also keeps the Game Boy's proportions.

- **Borderless Fullscreen Stretched** fills the screen.
- **Borderless Fullscreen** preserves the original proportions, adding black bars
  where needed.

The menu is hidden in both fullscreen modes. Press **Esc** to return to the last
selected window scale and show the menu again.

Enable **Display > Bilinear Filter** for smoother scaling. Presentation targets
60 FPS, and the title shows FPS while a game is loaded.

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

Gamepads are supported too, using the D-pad or left stick to move. With Xbox
button labels, A / B map to Game Boy B / A, and X / Y provide Turbo B / Turbo A.

Use **Input > Invert BA/YB Buttons** to switch the action-button layout for both
keyboard and gamepad.

| Shortcut | Action |
| --- | --- |
| Ctrl+O | Open a ROM |
| F12 | Reset the current game |
| Esc | Leave fullscreen |

## Saves

Games with battery saves are saved automatically every five seconds, when
switching games, before Reset or Unload, and when exiting normally. Save files
are stored in the `saves/` folder beside the executable.

Opening the same ROM directly or from a ZIP uses the same save. Save states
are not available yet.

## Building

You need **CMake 3.16+**, a **C++17 compiler**, and **Git**. CMake downloads the
required libraries during the first setup, so that step needs internet access.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

The executable is generated in `bin/` or `bin/Debug/`, depending on your build
setup. On Windows with MinGW and Ninja, add `-G Ninja` to the first command.
On Linux, SDL3 also needs X11 or Wayland development packages; Linux builds
have not been validated yet.

To run the automated tests:

```sh
ctest --test-dir build -C Debug --output-on-failure
```

## What's next

- Audio.
- Better game compatibility and more accurate emulation.
- Support for cartridge clocks and more cartridge types.
- More complete Game Boy Color support.

For technical details about video emulation, see [PPU documentation](docs/PPU.md).

## License

Released under the GNU General Public License v3.0. See [LICENSE](LICENSE).
