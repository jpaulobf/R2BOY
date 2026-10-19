# R2BOY

<div align="center">
  <img src="https://github.com/jpaulobf/R2BOY/blob/main/assets/r2boy2.png" height="450"/>
</div>

A Game Boy and Game Boy Color emulator written from scratch in C++, developed
alongside R2NES.

**Work in progress:** games can run with video, keyboard and gamepad controls,
and automatic battery saves. Compatibility is still improving, and audio is
not available yet.

## Playing

1. Launch R2BOY.
2. Open **File > Load ROM...** or press **Ctrl+O**.
3. Choose a `.gb`, `.gbc`, or `.zip` file. If a ZIP contains multiple games,
   select the one you want to play.

The window keeps the Game Boy's screen proportions when resized. You can enable
smoother scaling with **Display > Bilinear Filter**. The title shows FPS while
a game is loaded, with presentation targeting 60 FPS.

To quit, close the window or choose **File > Exit**.

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

## Saves

Games with battery saves are saved automatically every five seconds, when
switching games, and when exiting normally. Save files are stored in the
`saves/` folder beside the executable.

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
