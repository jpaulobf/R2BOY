# R2BOY

<div align="center">
  <img src="https://github.com/jpaulobf/R2BOY/blob/main/assets/r2boy2.png" height="450"/>
</div>

A Game Boy and Game Boy Color emulator written from scratch in C++, developed
alongside R2NES.

**Version 0.0.2** — see the [changelog](CHANGELOG.md) for release notes.

**Work in progress:** games can run with video, keyboard and gamepad controls,
automatic battery saves, save states, pause and fast-forward. Compatibility is
still improving, and audio is not available yet.

## Playing

1. Launch R2BOY.
2. Open **File > Open ROM...** or press **Ctrl+O**.
3. Choose a `.gb`, `.gbc`, or `.zip` file. If a ZIP contains multiple games,
   select the one you want to play.

Use **File > Reset** or **F12** to restart the current game, keeping its battery
save. **File > Unload** removes the ROM and returns to the black idle screen.
Both options are disabled when no game is loaded.

To quit, close the window or choose **File > Exit**. Escape never closes the emulator.

Press **Pause/Break** to pause or resume. While paused, the last completed image
stays visible for screenshots and the menus remain usable. Opening a ROM,
Reset, Unload and loading a save state return emulation to the running state.

## Display

The emulator starts at **4X**. Choose **1X to 5X** in the **Display** menu to resize
and center the window. Resizing it manually also keeps the Game Boy's proportions.

- **Borderless Fullscreen Stretched** fills the screen.
- **Borderless Fullscreen** preserves the original proportions, adding black bars
  where needed.

The menu is hidden in both fullscreen modes. Press **Esc** to return to the last
selected window scale and show the menu again.

Enable **Display > Bilinear Filter** for smoother scaling. **Scanlines** adds
alternating dark lines, with **Scanlines Level** adjustable from 5% to 25%.
Both effects start disabled.

**Display > VSync** starts enabled and follows the monitor refresh rate. This
also controls emulation speed: a high-refresh monitor can make games run faster.
Disable VSync to use the 60-FPS presentation limit and elapsed-time emulation.
The title shows presentation FPS while a game is loaded.

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
| F5 | Save to the selected state slot |
| F6 | Load the selected state slot |
| F12 | Reset the current game |
| Esc | Leave fullscreen |
| Pause/Break | Pause or resume emulation |
| Tab (hold) | Fast-forward, when enabled in Hacks |

## Hacks

**Hacks > Enable Fast Forward** starts enabled. Hold **Tab** to run without the
FPS limit or VSync; release it to restore the selected display timing.

**Hacks > Enable CPU Overclock** starts disabled. It gives the CPU twice as many
cycles relative to the normal hardware timing, including on top of CGB double
speed, while preserving video cadence. It can reduce game slowdowns but may
cause visual glitches or crashes. Extra CPU cycles are suspended during OAM
DMA and CGB speed-switch stabilization to protect cycle-counted waits.

While overclock is enabled, the title appends this warning after the FPS:
` | CPU OVERCLOCK ENABLED! GAMES CAN CRASH!`

These options last for the current application session.

## Saves

Games with battery saves are saved automatically every five seconds, when
switching games, before Reset or Unload, and when exiting normally. Save files
are stored in the `saves/` folder beside the executable.

Opening the same ROM directly or from a ZIP uses the same battery save and
state slots, identified by the ROM contents.

Use **File > Save State** (**F5**) and **File > Load State** (**F6**) to save or
restore the current emulation state. Choose one of three slots in
**File > Save State Slot**. State files are stored beside battery saves as
`<ROM hash>.slot1.state`, `.slot2.state` or `.slot3.state`.

Version 0.0.2 uses save state format 2, including native CGB speed-switch state.
Development builds that wrote format 1 states are incompatible; recreate those
states. Battery `.sav` files remain compatible. Loading a state keeps the current
CPU overclock preference.

## Compatibility

CGB games can request native double speed through KEY1/STOP. CPU, timers, serial
and OAM DMA follow that speed, while the video clock stays at its normal rate.
This happens automatically and does not require the overclock hack.

Video accuracy is still in development on both DMG and CGB. Some games may show
flicker, missing graphics or other artifacts even with overclock disabled.
CGB VRAM DMA (GDMA/HDMA) is not implemented yet. Audio and cartridge RTC support
are also pending.

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

## License

Released under the GNU General Public License v3.0. See [LICENSE](LICENSE).
