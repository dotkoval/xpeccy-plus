# Changelog

Notable changes in Xpeccy+, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versions use CalVer
(`YEAR.NUMBER[.PATCH]`).

Xpeccy+ starts from [Xpeccy](https://github.com/samstyle/Xpeccy) build `20260807`. Anything
before that point is upstream's history and is not repeated here.

## Unreleased

Entries marked **(Volutar)** are the work of [Volutar](https://github.com/Volutar), taken from
[his fork](https://github.com/Volutar/Xpeccy) with his authorship kept on every commit.

### Added

- **DejaVu Sans Mono travels with the emulator.** (Volutar) The debugger asked the system for
  it and silently took whatever it got where the font is not installed - macOS has none of it
  at all. It ships in the resources now, so the disassembler and the dumps line up the same
  way everywhere. The font keeps its own license, see `LICENSE_DEJAVU`.
- **Kempston joystick (port `#1F`) on ATM Turbo 2+, ZXM-Phoenix, ZX Spectrum +2 and +3.**
  (Volutar)
- **Disk images with more than 80 tracks**, in both `.trd` and `.scl`. (Volutar)
- **The build works on macOS again, and stays that way.** Every change is built on an Apple
  silicon runner, which puts the app bundle together, starts it and packs a dmg. Nothing is
  published from it yet - a released binary needs somebody who can run it, see the roadmap.
- **Eight interface styles travel with the emulator**, in `config/styles`. Pick one in Setup -
  deBUGa - Style Sheet; `System` still means whatever the desktop says, light on Windows and
  usually dark on Linux. `Light` keeps to the greys, the button size and the pale selection
  Windows itself draws, so it is the same picture as `System` there and a light interface on a
  dark desktop. `Dark` is its neutral counterpart, `ZX Spectrum` a near-black one with the
  machine's own magenta on top, and five come from the editors people already use: `Sublime`
  (Monokai), `Gruvbox` (Pavel Pertsev), `Solarized Dark` and `Solarized Light` (Ethan
  Schoonover) and `Dracula` (Zeno Rocha and contributors) - their palettes, under the same MIT
  license.
- **More fits on the debugger's screen**: tighter margins, spacing and row heights across 23
  panels, and nine stack entries where there were six. No panel moved. (Volutar)
- **The on-screen keyboard is drawn the classic skewed way**, with a key map to match.
  (Volutar)
- **Less separated AY stereo.** (Volutar) Three sixteenths of each side channel bleed into the
  other, the way real channels are never fully apart. The weights still add up to 16, so the
  volume is unchanged and mono sounds exactly as before.
- **Windows: `Shift+Alt+Space` sets a read breakpoint** on the memory cell under the cursor.
  (Volutar) Windows keeps plain `Alt+Space` for the window's system menu, so that breakpoint
  could not be reached from the keyboard there at all. Other platforms are unchanged.

### Fixed

- **A style sheet took the debugger's font away.** Qt gives every widget a font of its own as
  soon as the application gets a style sheet, which cut the debugger's panels off from the
  font the window hands down: the disassembler and the dumps fell back to the interface font,
  proportional and a size smaller, and the columns shrank with it. The font from Setup now
  reaches every panel by name, and the style sheet is applied before the debugger re-reads
  it, not after.
- **The emulator crashed on its way out when the window had never been shown** - `--help`
  does exactly that. The shaders are created when the window first appears, but the
  destructor deleted them either way, so it deleted a pointer that was never set: a
  segmentation fault on Linux, a bus error on macOS. A normal run never reached it.
- **macOS drew a black screen.** The emulator asked for an OpenGL 2.1 compatibility context,
  the way it does everywhere else, and macOS has no compatibility profile above 2.1 - its
  GLSL stops at 1.20 there, so every shader failed to compile and nothing was ever drawn,
  while the menus, the emulation and the debugger's own screen widget carried on working. It
  asks for a 3.3 core profile on macOS now, which Apple answers with 4.1.
- **The first frame bound a texture that had never been generated**, because the window is
  painted before the emulation thread hands over anything to show. Undefined behaviour that
  macOS reports and other drivers quietly swallow.
- **ZXM-Phoenix paged the wrong memory.** (Volutar) Bits 4, 6 and 7 of port `#1FFD` were
  masked and shifted as one, which put bits 6 and 7 in the wrong place in the bank number.
- **Profi did not initialise port `#DFFD` on reset.** (Volutar)
- **The debugger's disk dump stopped at 83 tracks**, a number hardcoded in two places and
  derived from nothing. It reads the drive's own geometry now, which is what made the images
  above usable in the dump.

### From upstream

Taken from [Xpeccy](https://github.com/samstyle/Xpeccy) builds `20260809` and `20260810`, by
SAM style. The base was build `20260807`.

- ZX Evo (TSConf) reads the PC keyboard as well as the ZX matrix, the way PentEvo already did.
- PentEvo: virtual DOS and NMI memory banks, and write protection per memory page.
- YM2203: an envelope rate of 0 now holds forever instead of creeping, and the fastest attack
  rates reach full volume at once. The rates are worked out when a register is written rather
  than on every tick.
- The build works again with `-DUSEOPENGL=0`.

## 2026.0 - 2026-08-08

### Added

- **Much lower input lag** - about 20 ms where it used to be about 60 ms, which is the tier
  the other ZX emulators are in. Emulation used to run in bursts the size of the audio
  buffer and the display used to show the oldest of three queued frames; it is paced by a
  wall-clock timer now and shows the newest frame. The new "Low latency" switch in
  Options - Video is on by default; turn it off if the motion looks uneven on a plain 60 Hz
  display.
- **Memory heat map**: per-cell read, write and exec counters for RAM and ROM, shown in the
  debugger and exported as CSV (also written next to a saved `.sna`). `tools/heatmap_png.py`
  renders the CSV as a PNG.
- **AF and AF' are back in the debugger.** The accumulator is shown as a register pair, the
  way ZX debuggers have always shown it, instead of `A` alone with the flags only as
  checkboxes. `A` and `A'` still resolve by name in watcher expressions and breakpoint
  conditions. Widening the register panel puts each register next to its alternative in a
  second column.
- **A release runs right after unpacking.** `config/` carries 29 rom images covering every
  ZX-compatible machine the emulator supports, with a romset and a ready profile for each -
  video geometry, contention, sound chips and the disk interface the machine shipped with -
  plus 20 CRT shaders, nine palettes and a default keyboard and gamepad mapping. Everything
  is named after the machine it belongs to: profile `ZX Spectrum +3`, romset
  `ZX Spectrum +2A/+3 (v4.0)`, geometry `ZX +2A/+3`. The defaults are a Spectrum's rather
  than the code's: an AY-3-8912 at 1.7734 MHz in ACB stereo on the Sinclair line, a YM2149
  at 1.75 MHz in ABC with SounDrive on the clones, the real 3.5469 MHz on the 128K family,
  a 3x picture with a 65% border and a CRT filter, Kempston mouse on everywhere. The rom
  images and the shaders keep their own licenses, separate from the MIT license of the
  project - see `config/roms/LICENSE`, `config/roms/PROVENANCE.md` and
  `config/shaders/README.md`.
- **Ready-to-run builds for both platforms.** On Windows `packaging\make-dist.ps1` stages
  the binary with the Qt and SDL runtime, `config\` and the docs, and can zip it; presets
  cover Qt 5 on x86 and x64 and Qt 6 on x64. On Linux `packaging/make-appimage.sh` packs an
  AppImage, so one file covers Ubuntu, Debian, Fedora, Arch and SteamOS. Both use CMake's
  own install layout, which a `.deb` or `.rpm` built with CPack carries too, and `config/`
  is part of it - a Linux package now starts with the same 16 machine profiles a Windows
  release has instead of the single built-in 48K rom. Each build also carries the metadata
  its platform expects and neither had before: product name, version, description and
  copyright on the Windows binary, description, license and documentation in a Linux
  package.
- **A boot loader for TR-DOS images.** "Add boot" is on by default and now has something to
  add: a `.trd` or `.scl` with no `boot` file of its own gets `config/boot.$B` appended when
  it is opened, so the disk can be started without typing a command. Dimon boot 2024 by
  Dmitry Yurinov - see `config/roms/PROVENANCE.md`.
- **Version scheme**: the tracked `VERSION` file is the only source, and the window title
  carries the whole thing - `Xpeccy+ (2026.0-dev+20260807)` for a development build,
  `Xpeccy+ (2026.0)` for a release.

### Changed

- Renamed to **Xpeccy+**. The binary, the desktop entry and the package are `xpeccy-plus`;
  window titles and the exported file headers carry the new name. The configuration files
  themselves are deliberately untouched, so existing settings keep working.
- **Own configuration directory on Linux and macOS**: `~/.config/xpeccy-plus` instead of
  upstream's `~/.config/samstyle/xpeccy`, which the fork still used - installed side by side,
  the two shared one `config.conf`, one set of profiles and one rom directory, and overwrote
  each other's settings. `$XDG_CONFIG_HOME` is honoured now where it used to be ignored.
  Nothing is migrated: the first run starts from the defaults, and the old directory is left
  alone - point `--confdir` at it to keep using the old settings. Windows is unaffected, its
  configuration has always lived next to the binary.
- **"Preset" in the romset editor** now fills in the names of the bundled images instead of
  names from the author's own machine.
- **The sources mirror the runtime layout**: `conf/` became `config/`, the same shape the
  emulator expects next to the binary. The built-in emergency defaults, used when no
  configuration is found at all, moved to `res/fallback/`.

### Fixed

- **Tape did not start for loaders that bypass the ROM routine**, so they had to be started
  by hand.
- **Register fields in the debugger sized themselves wrong**: one fixed width for every
  machine, too wide for a byte register and too narrow for a 32-bit one, and a 32-bit field
  (`PSW` on BK0010) took only a single digit of input. They follow the value and the number
  base now.
- **ZX Evo (TSConf) drew nothing but black.** The window into the FPGA at the address in
  `#15AF` was still tested as `flag & 0x10` after that flag became a `bool`, so it never
  opened: nothing could reach the palette or the sprite file. Loading screens, sprites and
  backgrounds were all missing. An upstream regression from build `20260418b`.
- **TSConf stalled wherever software drove the line interrupt.** The CPU only accepted an
  interrupt while a frame interrupt was being held, so the line and DMA sources were taken
  roughly once a frame instead of once a scanline, and demos slowed to a stop.
- **TSConf did not get through its boot rom.** `#21AF` bit 0 is the same rom select as
  `#7FFD` bit 4, but only the latter was recorded, so the wrong rom was paged in and the
  `#3Dxx` entry into TR-DOS never fired - the boot rom looped forever with the stack
  pointing into rom.
- **TurboSound had its two chips the wrong way round**: `#FFFD` `#FF` selects the first AY
  and `#FE` the second.
- **The AY could not be detected through port `#FFFD` any more.** Upstream's TSFM work made
  "read the status register" the state every machine powers up in, so a program that probes
  the chip by writing a register and reading it back got a status byte instead: a plain 128K
  reported no sound chip at all. Reading registers is the power-up state again, and the
  status register is only readable where the hardware has one.
- **Crash when switching profiles.** A profile's machine is created on first use, but the
  emulation thread could pick up the profile before that machine existed, and use it while
  it was still being built. Present upstream as well.
- **`--confdir` set up the configuration twice**, which leaked the gamepad controller, added
  a second `default` layout and profile, and left the windows holding values from the first
  configuration. The option is read before anything is initialised now.
- **Windows with no icon of their own** - options, tape, rzx, watcher - had none at all on
  X11, where there is no exe resource to fall back to. The application now carries an icon,
  which Qt hands to every window that does not set one. The Linux desktop entry also points
  at a path the icon theme spec knows, `share/icons/hicolor/128x128/apps`, instead of a flat
  `share/icons`.
- **The x86 trace log wrote one garbage character in place of the `CS` register**, since the
  value was appended as a character rather than as a hex word. It also broke the Qt 6 build.
- **Link error in MinSizeRel and Debug builds** (`lr_swaph` declared C99 `inline` with no
  external definition).
