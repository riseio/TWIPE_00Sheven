# I COULD HAVE GIVEN YOU THE WORLD

A native PC and Steam Deck port of **007: The World Is Not Enough**, with widescreen support,
modern controls, and a range of QOL enhancements. This is a hobby project originally built
for my own playthrough, now shared because sharing is caring.

[https://www.youtube.com/watch?v=Bh7wvModqQY](https://youtu.be/Bh7wvModqQY)

## Download and play

Builds are available from [Releases](https://github.com/riseio/TWIPE_00Sheven/releases).
This project does not contain any game assets or code derived from the **The World Is Not Enough (USA), revision 0** ROM. 

**The first launch compiles the game locally from your ROM. Later launches reuse the
completed native code. No development tools or internet connection are needed to play.**

Saves, settings, and generated content stay in the adjacent `data` folder when you
replace the executable. Keep that folder when updating or moving the game.

## Enhancements

- Widescreen and ultrawide support.
- Adjustable vertical FOV from 40 to 100 degrees, with a reset to the original 60-degree view.
- Independent horizontal and vertical inversion for mouse and right-stick aiming.
- Mouse-wheel weapon cycling when not using scope zoom.
- Interpolated rendering for high refresh rate displays.
- Mouse aiming or modern twin-stick controls.
- Larger magazines: E.g., Meyer TMP **25 → 60**, Deutsche M45 **25 → 40**,
  Deutsche M9S **30 → 50**, Ingalls Type 20 **30 → 45** etc..
- Doubled ammunition pickups and reserve limits for the shared pistol/SMG ammo pools:
  P2K, Meyer TMP and Deutsche M9S pickups **20 → 40**, reserves **80 → 160**;
  Deutsche M45 and Ingalls Type 20 pickups **25 → 50**, reserves **100 → 200**.
  Meyer TMP and Ingalls weapon pickups also grant twice their original ammunition.
- **1.5× faster reloads** for the Delta 900 Mag and Mustang .44.
- **Double damage** for the Seamaster Speargun and Watch Laser. The laser uses
  **one quarter of its original ammunition**, and spears retain straight flight beyond their old drop range.
- Removed firing spread from the Raptor Magnum's laser mode and halved the Frinesi Special 12's projectile drop beyond its original range.
- More responsive jumping and improved ladder jump-off and climb-off handling.
- Improved scope zoom using keyboard, mouse wheel/buttons, D-pad or left stick while aiming.
- Quick watch-gadget and vision-mode selection, with Night Vision and X-ray available in the gadget wheel.
- Clearer vision overlays.
- Crosshair colours for visible enemies and friendly NPCs, plus grapple-target feedback.
- Live objectives while playing, persistent quick saves/loads and automatic campaign-progress saving.
- Sharper game fonts and controller/keyboard prompts that follow your bindings.
- Audio recovery when devices reconnect.
- Steam Deck rear-button shortcuts and suspend/resume recovery.
- Portable saves and settings, plus a campaign-reset option that preserves your controls and display settings.

## QOL Toggles

- **Grapple:** Classic or the Nightfire-style.
- **Weapon Selection / Gadget Selection:** choose Classic cycling or Radial Menus.
  With Radial enabled, tap to cycle and hold to select from the wheel.
- **Remember Weapon Modes:** retain suppressors, firing modes and manual scope choices
  across weapon/gadget swaps until you change them. On by default.
- **Hold to Sprint:** run with Shift or held Interact while moving forward.
- **Civilian Health:** **2× starting health** for civilians.
- **Invincible Friendly NPCs:** protect allies and civilians from damage and player-hit mission failures.
- **Double Watch Darts:** double dart grants, pickups and capacity on the next mission or restart.
- **Regenerating Health:** Like Halo style if you want it.
- **Impact Effects:** enhanced hit effects like lil blood puffs, enabled by default.

## Other optional settings:

- **Graphics:** enhanced textures generated locally.
- **General:** auto aim, mouse acceleration, mouse/right-stick sensitivity and rumble strength.
- **Cheats:** invulnerability, infinite oxygen, no fall damage, all weapons, all gadgets,
  and all missions/difficulties.

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
| E / mouse wheel | Cycle weapon; hold E for weapon radial |
| Q | Cycle gadget / hold for gadget radial |
| V | Change weapon or gadget mode |
| Z / X, mouse side buttons, or mouse wheel while aiming | Zoom in / out |
| T | Toggle night vision / X-ray |
| Tab | Toggle objectives |
| F5 / F8 | Quick save / quick load |
| Tilde (~) | Toggle enhanced textures |
| Enter | Pause |
| Escape | Settings |

On Cold Reception's ski sections, W accelerates and S brakes. The mouse wheel
continues to control zoom while aiming with a scoped weapon.


## Steam Deck controls

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

## What is being worked on for the next patch
- Auto-aim restoration
- Stop mission briefing overlay turning off after mid mission cutscenes
- Restore scrolling at briefing screens
- Masquerade Bomb timer increase QOL toggle
- Steamdeck Rumble support
- Steamdeck Gyro-aim support
- Android and Mac builds

## And Beyond
- Online P2P multiplayer

## Disclaimer

I Could Have Given You The World is an unofficial fan project. It is not affiliated with, endorsed by,
or sponsored by the original game's developers, publishers, or rights holders.

**We do not provide the original game.** This repository and its downloads do not
include a game ROM, translated game code, music, sounds, textures, models, levels,
or other extracted original game assets. We do not provide ROM downloads or links to them.

The original game's characters, names, artwork, music, trademarks, and other content
belong to their respective copyright and trademark holders. We claim no ownership
of that content. This project grants no rights to redistribute the original game or its assets.
[Report a bug](https://github.com/riseio/TWIPE_00Sheven/issues)
