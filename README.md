# 00Sheven

A native PC and Steam Deck port of **007: The World Is Not Enough**, with widescreen support,
modern controls, and a range of QOL enhancements. This is a hobby project originally built
for my own playthrough, now shared because sharing is caring.
While feedback and bug reports are welcome, please keep your virtue signalling and code
quality objections to Reddit.

## Download and play

Builds will be available from [Releases](https://github.com/riseio/TWIPE_00Sheven/releases).
You need your own **The World Is Not Enough (USA), revision 0** ROM. Place it beside the
executable or select it when prompted on first launch. No ROM or game assets are included.

- **Windows:** Run the portable EXE. No installer or separate DLLs needed.
- **Steam Deck:** Make the AppImage executable, add it to Steam as a non-Steam game,
  and use the Gamepad controller layout.
- **Linux:** The AppImage is the simplest option.

The first launch compiles the game locally from your ROM. Later launches reuse the
completed native code. No development tools or internet connection are needed to play.

Saves, settings, and generated content stay in the adjacent `data` folder when you
replace the executable. Keep that folder when updating or moving the game.

## Enhancements

- Widescreen and ultrawide support, with an adapted HUD and menus.
- Interpolated rendering for high refresh rate displays.
- Mouse aiming and dual-stick movement, with remappable controls.
- Weapon and gadget radial menus, with firing modes remembered between swaps.
- Hold Shift to sprint at 1.75 times walking speed.
- Improved sniper zoom controls and an optional crosshair.
- Live objectives, quick saves, and quick loads.
- Optional enhanced textures generated locally from your ROM.
- Audio output selection and an output test in the Sound menu.
- Local saves and settings, with no account or online connection needed to play.

## PC controls

| Input | Action |
|---|---|
| WASD / mouse | Move / aim |
| Left click | Fire / use gadget |
| Right click | Aim |
| Space | Stand / jump |
| Ctrl | Crouch |
| Shift | Sprint |
| F | Interact |
| R | Reload |
| E | Cycle weapon / hold for weapon radial |
| Q | Cycle gadget / hold for gadget radial |
| V | Change weapon or gadget mode |
| Z / X, mouse side buttons, or mouse wheel while aiming | Zoom in / out |
| T | Toggle night vision / X-ray |
| Tab | Toggle objectives |
| F5 / F8 | Quick save / quick load |
| Backtick (~) | Toggle enhanced textures |
| Enter | Pause |
| Escape | Settings |

Controls can be changed in the settings menu. Generate the optional texture pack
in Graphics before enabling enhanced textures.

## Steam Deck controls

Use Steam's **Gamepad** layout. These are the default in-game bindings:

| Input | Action |
|---|---|
| Left stick | Move |
| Right stick | Aim |
| R2 | Fire / use gadget |
| L2 | Aim |
| A | Stand / jump |
| B | Crouch |
| X | Interact / reload; hold while moving forward to sprint |
| Y | Change weapon or gadget mode |
| R1 | Cycle weapon / hold for weapon radial |
| L1 | Cycle gadget / hold for gadget radial |
| D-pad up / down while aiming | Zoom in / out |
| Left stick forward / back while aiming | Zoom in / out |
| D-pad up | Toggle night vision / X-ray |
| D-pad left / down / right | Watch laser / grapple / dart |
| Menu | Pause |
| View | Toggle objectives |
| R4 | Settings |
| L4 | Toggle enhanced textures |
| L5 / R5 | Quick save / quick load |
| A / B in menus | Confirm / back |

Custom bindings override these defaults. Zoom inputs apply to equipment that supports zoom.

## Disclaimer

00Sheven is an unofficial fan project. It is not affiliated with, endorsed by,
or sponsored by the original game's developers, publishers, or rights holders.

**We do not provide the original game.** This repository and its downloads do not
include a game ROM, translated game code, music, sounds, textures, models, levels,
or other extracted original game assets. We do not provide ROM downloads or links to them.
Game code and assets are prepared locally from the ROM you supply.

**You must own a legally purchased copy of The World Is Not Enough and provide your
own lawfully obtained ROM dump** of the supported US revision-0 release to use this
project. The port is not a substitute for owning the original game. Do not upload
or share ROMs or extracted game assets in this repository or its issues.

The original game's characters, names, artwork, music, trademarks, and other content
belong to their respective copyright and trademark holders. We claim no ownership
of that content. This project grants no rights to redistribute the original game or its assets.

## Project

Built with N64Recomp, N64ModernRuntime, RecompFrontend, and RT64.

Source builds require CMake, Python 3.11+, and the submodules below. Windows needs
Visual Studio 2022 with C++ support. Linux needs GCC or Clang, Ninja, SDL2, FreeType,
Vulkan headers, and the D-Bus development library. The pinned project dependency
branches require read access to `riseio/TWIPE`.

```sh
git -c core.longpaths=true submodule update --init --recursive
```

Windows:

```powershell
.\tools\build_windows.ps1 -Rom 'C:\path\to\game.z64'
```

Linux:

```sh
sh tools/build_linux.sh /path/to/game.z64
```

These commands build the native executable and its supporting runtime files in
`build/windows/Release` or `build/linux`. Keep those files together when running a source build.

[Report a bug](https://github.com/riseio/TWIPE_00Sheven/issues)
