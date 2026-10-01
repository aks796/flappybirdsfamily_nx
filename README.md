<div align="center">

<img src="launcher/icon.jpg" alt="Flappy Birds Family" width="160">

# flappybirdsfamily_nx

**Flappy Birds Family on Nintendo Switch**

An unofficial Nintendo Switch wrapper for the Android version of
**Flappy Birds Family**.

[![Switch](https://img.shields.io/badge/Nintendo_Switch-Homebrew-E60012?style=for-the-badge&logo=nintendoswitch&logoColor=white)](#)
[![Version](https://img.shields.io/badge/Version-0.1.0-4C8BF5?style=for-the-badge)](#)
[![ARM](https://img.shields.io/badge/32--bit-armeabi--v7a-3DDC84?style=for-the-badge&logo=android&logoColor=white)](#)

</div>

---

## About

`flappybirdsfamily_nx` is a native wrapper that runs the 32-bit (armeabi-v7a)
Android build of **Flappy Birds Family** on Nintendo Switch as a 32-bit
process. It loads the game's own engine library, `libflapfire.so`, and
recreates what the game's Java side did: the OpenGL ES context, the texture
atlas, the SoundPool, the saved high score, controller and touch input.

This release targets **Flappy Birds Family 1.0.4** for Android
(`com.dotgears.flapfire`, armeabi-v7a), such as the Amazon Appstore / Fire TV
build. Version 1.0 also runs. The APK is not included: you need your own copy
of the game.

The game's leaderboard button, which used Amazon GameCircle on Android, shows
a local top five with three-letter names. It is drawn from the game's own art
at run time.

See [NOTES.md](NOTES.md) for how the port works and what the 32-bit libraries
needed.

---

## Controls

Up to two players. The first controller to press a button on the main menu is
player 1, the next one player 2. Other controllers stay connected but do not
control the game.

| Input | Action |
| --- | --- |
| **A / B / X / Y / L / R / ZL / ZR / SL / SR** | Flap, confirm |
| **D-Pad / Left Stick** | Move through menus |
| **Up / Down** (main menu) | Change your bird |
| **A** on the mode button (main menu) | Ask for 2P |
| **+** | Pause |
| **-** | Back to the main menu; on the main menu, close the game |
| **Touchscreen** | Flap, menus |

A pair of Joy-Cons is one controller. A single Joy-Con is one controller,
held sideways: its stick and buttons turn with it, and the button on the
right is A.

**Leaderboard name entry** (1P, a score of at least 10 that makes the top
five): **Up / Down** change the letter, **Left / Right** move, **A** next
letter or done, **B** back, **+** done.

---

## Build

### Requirements

* Docker
* The [android32](https://github.com/aks796/android32) runtime at `runtime/`
  (a submodule; a symlink while developing)
* The vita2hos container (`ghcr.io/vita2hos/devcontainer/vita2hos`), for
  devkitARM and libnx32
* [libnx32](https://github.com/aks796/libnx32) 4.12.0 or newer, the 32-bit
  libnx. `build.sh` mounts its `prefix/` from `../libnx32/prefix` (libnx32
  cloned next to this folder), or from the path in `DCR_LIBNX32`.
* [mesa32](https://github.com/aks796/mesa32), Mesa and libdrm_nouveau for
  AArch32: its `lib/` and `include/` copied into `portlibs32/`.
* The devkitA64 container (`devkitpro/devkita64`), for the launcher NRO
* Python 3 with pyelftools, capstone and Pillow, for the tools

libnx32 and mesa32 both have prebuilt releases, which work as well as
building them.

Copy mesa32 into `portlibs32/` (from `../mesa32/prefix` by default; `MESA32`
names another folder holding its `lib/` and `include/`):

```bash
MESA32=/path/to/mesa32/prefix tools/get_portlibs.sh
```

Compile the 32-bit wrapper (`fbf_nx.nsp`):

```bash
./build.sh
```

Compile the launcher (`launcher/flappybirdsfamily_nx.nro`), which carries the
wrapper:

```bash
launcher/build.sh
```


Build everything, run the host tests and lay out `SD_CARD/`:

```bash
tools/package_sd.sh /path/to/FlappyBirdsFamily.apk
```

`SD_CARD.zip` never contains the APK. The host tests need only Python 3 and a C
compiler; given an APK, `tools/check_engine.py` also checks every engine
offset the port uses against that APK's code.

---

## Running

Requires Atmosphère and sphaira.

Create this folder on the SD card and put both files in it:

```text
sd:/switch/flappybirdsfamily_nx/
├── flappybirdsfamily_nx.nro
└── Flappy Birds Family.apk
```

The APK's file name does not matter as long as it ends in `.apk`.

1. In sphaira, open **Homebrew**, select **Flappy Birds Family** and choose
   **Install Forwarder**.
2. Launch the new **Flappy Birds Family** icon on the HOME menu. The launcher
   checks the APK, installs the 32-bit game program for that icon and
   restarts it.
3. The first start unpacks the game's engine from the APK.

Afterwards the folder looks like this:

```text
sd:/switch/flappybirdsfamily_nx/
├── flappybirdsfamily_nx.nro
├── Flappy Birds Family.apk
├── libflapfire.so
├── classes.txt
├── config.ini
├── leaderboard.txt
├── data/
└── debug.log
```

The high score is in `data/shared_prefs/`, the leaderboard in
`leaderboard.txt`. Settings live in `config.ini`, which is written on the
first launch and explains each option in place.

To update, replace `flappybirdsfamily_nx.nro` and launch the icon. The game
installs the newer build itself.

Coming from an older build (`sd:/switch/flappybirdsfamily/`,
`FlappyBirdsFamily.nro`): put `flappybirdsfamily_nx.nro` in the old folder and
launch the icon. The game updates, then moves the APK, settings, high score and
leaderboard to `sd:/switch/flappybirdsfamily_nx/`. The old NRO stays behind and
can be deleted.

---

## Status

Tested on hardware: 1P and 2P, 60 fps, sound, pause, the high score, touch,
docked and handheld.

Added since the last hardware test: sideways single Joy-Cons, the leaderboard,
the main-menu fix for Up / Down and 2P, the new folder name and any APK name.

The wrapper is built for **Flappy Birds Family 1.0.4** for Android,
armeabi-v7a. Version 1.0 loads as well. Other versions have not been tested.

---

## Credits

**Flappy Birds Family Nintendo Switch port**: aks796

**Flappy Birds Family**: .GEARS (dotGears)

The loader derives from the open-source Android `.so` loader work by Andy
Nguyen (TheOfficialFloW) and fgsfds. The 32-bit runtime was forked from the
Disney Crossy Road and Plants vs. Zombies ports. The sideways Joy-Con mapping
follows the BombSquad Switch port.

Images and sounds are decoded with stb_image and stb_vorbis by Sean Barrett
(public domain), zip files read with miniz.

Built with devkitPro, devkitARM, libnx and vita2hos' AArch32 libnx, and Mesa.
The wrapper code is MIT-licensed. See `LICENSE`.

---

## Contributing

Bug reports and tested improvements are welcome. Include the build number (shown
by the launcher), steps to reproduce, and `debug.log`, `config.ini` and, if the
game closed by itself, `crash.log` from the game folder. For controller
problems, set `log_input = true` in `config.ini` first.

---

## Disclaimer

This is an unofficial fan project and is not affiliated with, sponsored by or
endorsed by Nintendo or .GEARS. Flappy Birds Family and its art and sounds
belong to .GEARS.

No part of the game is included. The wrapper reads the APK of a copy you own.
