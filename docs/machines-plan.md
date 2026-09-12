# Machines instead of profiles: plan

Status: design, nothing implemented yet. Written 2026-09-08, rewritten 2026-09-09 after the
decision to drop profiles altogether.

This file is the handover document for the rework of the configuration model. It records
what the code does today, what the target model is, why, and in what order to build it.
Another session should be able to pick it up cold. When the work is done this file goes
away and one line lands in `CLAUDE.md` -> "Where things live"; the reasoning moves to the
wiki.

---

## 1. Goal

Make the settings easy to live with:

- picking the emulated machine is one control, and it does not throw away what the user
  mounted or tuned;
- an update can fix a machine's specification without touching anything the user set;
- the UI says which setting belongs to the machine and which to the whole application, so
  nothing changes behind the user's back;
- the machine list matches real hardware;
- the settings the user is shown by default are the ones they might actually want to
  change.

## 2. Decisions already taken

Settled in discussion, so do not re-open them without a reason:

1. **Profiles go away.** One machine per process. See 3.1 for the evidence.
2. **Two configuration layers**, not three: built-in defaults compiled into the binary, and
   the user's own file. See 4.
3. **The global romset table goes away.** ROMs are named per machine, per 16K bank. The
   offset machinery stays in the format and in the engine - the ordinary view is just the
   machine's slots with a file each, and offsets live behind "Advanced". See 6.
4. **A missing ROM file gets a visible message plus an `xlog` line**, not a silent warning.
5. **"Machine" is the word** - in the UI, in the config file, and on the command line
   (`-m` / `--machine`, with `-p` / `--profile` kept as a deprecated alias). The names and
   ids themselves are section 7, and this is the moment the core names get corrected too.
6. **The machine-defining settings move behind an "Advanced" button**, except `psg.frq`,
   which stays on the Sound page - a sound card can be an add-on independent of the
   machine, whatever its clock is derived from.
   **Reversed on 2026-09-12 for where the value is kept**: `psg.frq` and `psg.stereo` are
   machine keys, because a 128K's AY runs at 1.773447 MHz and a clone's at 1.75, and as a
   global preference the clock stopped following the machine. Only `psg.separation` stayed
   global. Where the controls sit on the page is unchanged.
7. **No "keep or reset" dialog on a machine switch.** Overrides are remembered per machine
   instead, which removes the question. See 5.3.
8. **Play / developer / demo are runtime modes, not settings sets** - a hotkey toggle with
   its own overlay per mode. A separate feature, out of scope here. See 9.1.
9. **Mounted media stays global.** Offering the right formats per machine is a later
   feature. See 9.2.
10. **CMOS and NVRAM are per machine and live in the user directory**; where a machine
    needs starting contents, those ship in the resources. See 6.6.

---

## 3. What exists today

### 3.1 Profiles hold a whole machine each, and their valuable half is never saved

`xProfile` (`src/xcore/xcore.h:216`) holds the machine name, romset, video layout, palette,
keymap, two gamepad maps, the last directory, breakpoints, labels, comments - and a whole
`Computer*`, created lazily on the first switch to that profile (`prfSetCurrent`,
`profiles.cpp:146`). So a session can have several live machines, of which exactly one runs.

What the code actually asks a profile for - 390 uses across 43 files:

| what | uses | is it persisted? |
|---|---|---|
| `->zx` (just "the current machine") | 263 | - |
| `->brk` (breakpoints) | 47 | **only through a file dialog**, and `--brk` |
| `->labsets` / `->curlabset` / `->labmap` | 36 | **only by importing an SJASM file by hand** |
| `->commap` (comments) | 12 | **never** |
| palette, keymap, gamepad map, lastDir | ~17 | in the profile's `.conf` |

`brk_load_list()` / `brk_save_list()` are called from exactly two places, the load and save
buttons in `dbg_brkpoints.cpp:697,711`. Labels come back through `conf.labpath` - a
*single global* path, not a per-profile one. So the "developer workspace" that makes
profiles look worthwhile does not survive exit. A profile keeps it only between switches
inside one session.

The rest of the cost is real: every non-current profile's `Computer` sits frozen mid-frame
with its floppy motor, sound chips, tape position and open IDE file handles, which is why
`prfSetCurrent` has to do `prfClose()` plus `ide_remount()` and `sdc_remount()` on the way
back. Meanwhile the pacer, `conf.snd.need`, run-ahead and the autostart logic are all
single-machine globals - the rest of the design has already committed to one live machine.

And two machines at once is what a second process is for, with `--confdir` if they need
separate settings.

### 3.2 Fifteen shipped profiles are really machine definitions

`config/profiles/` ships 16 profiles (15 machines + `default`). They exist only to define
machines - and they are inconsistent, because a shipped profile is also a file the user
edits:

- `ZX Spectrum 128K + TR-DOS`, the one `config.conf` starts on, ships with `psg.count = 3`,
  `psg.type = 2`, `psg.stereo = 1` and `soundrive_type = 1`: a three-chip TurboSound and a
  SounDrive on a stock 128K, somebody's personal setup baked into the machine list.
- `Pentagon 128` and `Pentagon 512` differ in exactly one line (`memory`).
- `ZX Spectrum 128K` and `ZX Spectrum 128K + TR-DOS` differ in the romset, the disk
  interface **and** four sound/input settings that have nothing to do with TR-DOS.

### 3.3 The machine list does not match reality

`tabHwPtr` (`src/libxpeccy/hardware/hardware.c:40`), the ZX half: `ZX48K`, `Pentagon`,
`Pentagon1024SL`, `Scorpion`, `ATM2`, `Profi`, `Phoenix`, `PentEvo` (+ the 2021 variant),
`TSLab`, `Spectrum +2`, `Spectrum +3`.

Two concrete defects:

1. **There is no 128K core.** The `ZX Spectrum 128K` profile runs on `Pentagon`
   (`hardware/pentagon.c`). Pentagon decodes port 0x7FFD with mask `0x8002`, so a write to
   0x3FFD pages too, where a real 128K uses `0xC002`; and Pentagon reads bits 6-7 of 0x7FFD
   as the 512K extension, which a 128K does not have. The frame timing comes out right only
   because the profile picks the `ZX 128K` layout and `contPattern`.
2. **`HW_PLUS2` is a +2A.** `pl2MapMem()` (`hardware/plus2.c:12`) implements the +2A/+3
   special paging on port 0x1FFD. A real +2 (grey) is a 128K with a tape deck and no 0x1FFD
   at all. The shipped `ZX Spectrum +2` and `ZX Spectrum +2A` profiles therefore run the
   *same* core and differ only in romset, layout, `contPattern` and `contio`.

Worth noting for phase 1: the correct +2 ROMs (`plus2-0.rom`, `plus2-1.rom`) are already
bundled and already referenced by the `ZX Spectrum +2` romset. Once a real 128K core
exists, the grey +2 is exactly "128K core + those two ROMs" - a small delta, not a new
machine.

### 3.4 An update cannot fix any of this

`seedConfigDir()` (`src/xcore/config.cpp:374`) copies from the installed `config/` into the
user's config directory **only files that are not there yet**. That is right for a user's
own files - but the shipped profiles live in the same tree, so once the emulator has run
once, no fix to a shipped machine ever reaches that user. And because the file is both the
shipped definition and the user's edits, there is no way to tell a fix that should be
applied from a change that must be kept.

This is the fact that shapes the whole design: **a shipped definition must never be copied
into the user's directory.**

### 3.5 The global / per-machine split is arbitrary and invisible

| setting | where it is | where it belongs |
|---|---|---|
| tape `speed`, `frq.mul` (turbo) | profile | global preference |
| tape `autoplay`, `fast`, `rewind` | global | global |
| `psg.count`, `psg.type` | profile | **machine** |
| `psg.frq`, `psg.stereo` | profile | **machine** (decision 6, as revised) |
| `psg.separation` | profile | global preference |
| sound volumes | global | global |
| video `border`, `shader`, `scale` | global | global |
| `geometry`, `contPattern`, `earlyTiming`, `4t-border` | profile | **machine** |
| `palette` | profile | global preference |
| `memory`, `cpu.frq`, `contio`, `contmem`, `scrp.wait` | profile | **machine** |
| mounted tape / disks / HDD / SD | profile | session state |

Nothing in the UI says which is which. The Options window is one `QTabWidget` tree
(`ui/setupwin.ui`) and a page mixes both kinds freely - the Sound page carries the
machine's chips and the host's output device side by side.

### 3.6 Something already called a preset

`setupwin.cpp:1476` has a hard-coded `xRomPreset presets[]`: per machine id, a default set
of ROM files, applied only by a button in the romset editor. The right idea in the wrong
place - compiled in, ROM-only, and manual. The new machine definitions absorb it.

### 3.7 Defects found while reading (report only, not part of this work)

- `profiles.cpp:777` writes `gs.reset` from `comp->gs->stereo` instead of
  `comp->gs->reset`. The value does not round-trip.
- `prf_load_conf()` drops every key it does not know, so loading and saving a config
  written by a newer build silently deletes the newer settings.

---

## 4. Target model

Two layers, plus the machine definition in between - but the middle layer is read-only and
compiled in, so there are only two places anything is *stored*:

```
built-in (in the binary)  ->  user's xpeccy.conf  ->  command line
```

### 4.1 Built-in: machine definitions and the stock content

Machine definitions ship **compiled into the binary** as Qt resources, next to what is
already there (`:/res/fallback/config.conf`, `:/res/fallback/xpeccy.conf`,
`:/config/roms/48.rom`). So this extends a mechanism that exists rather than adding one.

Why in the binary rather than in the install directory: `sysConfigDir()` has three
platform branches today and returns an empty string on Windows, which is the day-to-day
platform. Resources have no path problem, work the same in an AppImage and a bundle, and
make it structurally impossible for a stale user file to shadow a fixed definition.

The same rule covers palettes, styles, shaders, keymaps, gamepad maps and video layouts:
the list the user picks from is *built-in entries plus whatever is found in the user
directory*, and a user file with the same name shadows the built-in one. That keeps the
escape hatch - a machine or palette we do not ship is still a file you drop in a folder.

Starting contents for a machine's CMOS or NVRAM ship the same way (6.6) - they are ours,
they are tiny, and a user must not be able to end up without them.

ROM images stay on disk in `roms/`, not in the binary: they are not ours to merge into the
executable, `config/roms/PROVENANCE.md` tracks them per file with an MD5 each, and users
swap individual dumps.

### 4.2 User: one file

Everything the user changed goes into `xpeccy.conf` in the config directory: global
preferences, session state, and per-machine overrides. Nothing else is written.

```
[general]
machine = zx128            # the machine in use

[video]
scale = 3
shader = tvline-nocurve-light.txt

[sound]
volume.master = 100

[media]                    # session state, restored on start
tape   = D:\zx\games\manic.tzx
disk.A = 80DW:D:\zx\trd\demos.trd

[machine.pent128]          # overrides, only what differs from the built-in definition
memory = 512

[machine.zx128]
cpu.frq = 3550000
```

An override block is written only when the user changes something, and holds only the keys
that differ. Everything else follows the built-in definition, so a fix shipped in an update
reaches the user for free - that is the whole answer to "an update must not break my
settings".

### 4.3 A machine definition

One resource file per machine, read-only:

```
[machine]
id        = zx128
name      = ZX Spectrum 128K
family    = sinclair            # grouping in the machine menu
inherit   =                     # another machine's id; this one is a delta on it. What the
                                # TR-DOS pairs are: `zx128-trdos` is `zx128` plus the Beta
                                # Disk interface and the ROM that drives it
hw        = ZX128               # HardWare core name (findHardware)
memory    = 128
cpu       = Z80
cpu.frq   = 3546900
contio    = yes
contmem   = yes
scrp.wait = no

[video]
geometry    = ZX 128K
contPattern = 1
earlyTiming = yes
4t-border   = yes

[sound]
psg.count = 1
psg.type  = AY

[storage]
disk = none                     # none / trdos / plus3
ide  = none
sdc  = no

[input]
mouse = no

[rom]                           # see section 6
rom0 = 128-0.rom
rom1 = 128-1.rom
gs   = gs105b.rom
```

---

## 5. What replaces what

### 5.1 The machine list

The list the user picks from is the machine list, and the disk interface is part of the
machine - a 128K with a Beta Disk really is a different machine from a bare 128K, and it
already is one in the shipped data. So the shipped 15 profiles become roughly the same 15
machines, cleaned up per 3.2 and 3.3.

### 5.2 Session state

Mounted media, the window position, the last directory: written to `xpeccy.conf` and
restored on start, as they are now. Switching the machine keeps them - only what the new
machine cannot take is unmounted, and the user is told which. That is the answer to "a game
needs a 48K, and after switching I have to mount it again".

### 5.3 Overrides and why there is no dialog

Overrides are stored **per machine** (`[machine.<id>]`), so they are remembered. Set 512K
on a Pentagon, switch to a 48K, come back - the 512K is still there, and nothing was asked.

That is why decision 7 drops the "use preset defaults / keep mine" prompt. The prompt would
be answering a question the storage model already answers, and it would fire on every
switch, since two machines always differ. The case it was meant for - "I tuned this and
want it on the new machine too" - is nearly always a setting that should have been *global*
in the first place (turbo multiplier, tape speed, palette), and decision 6 plus the table
in 3.5 move those out of the machine scope. If a real need for the prompt shows up later,
the storage model does not stand in its way.

What replaces it in the UI is a plain **"Reset this machine to defaults"** button, plus a
marker on any field that currently overrides the definition.

### 5.4 The four task setups (Default / Game / Development / Demo)

These were the reason to want profiles, so they need an answer. What actually differs
between "Game" and "Development" is not configuration: it is which windows are open,
whether the debugger is up, fullscreen, the LED indicators. That is a *mode*, not a
settings set - and settled as decision 8, it is a **separate feature** that this work
neither needs nor blocks: a hotkey-toggled runtime mode, each with an overlay of its own
(timings in the developer mode, something else in the game one, a demo playlist in the demo
one).

What matters here is only that this plan must not make it harder. It does not: a mode is
one global enum plus what each overlay draws, orthogonal to which machine is running.
Nothing about it wants a profile.

### 5.5 In the code

`conf.prof.cur->zx` (263 sites) becomes one global machine pointer - a big but mechanical
change that removes an indirection from the hottest GUI and debugger code. The session-only
workspace fields move to `conf` directly: `conf.brk`, `conf.labsets`, `conf.commap`. Since
none of them is persisted today (3.1), nothing about their lifetime changes.

---

## 6. ROMs

### 6.1 What the offsets are actually worth

A romset entry is `file:foffset:fsize:roffset` today. Across all 33 `rom =` lines in the
shipped `config.conf`:

- `foffset` is **0 in every one of the 33**. Reading from the middle of a source file has
  never been used.
- `fsize` is `16` in 26 and `0` (auto) in 7. Every file those 26 name is *exactly* 16384
  bytes, so `16` and auto mean the same thing in every case. The 7 auto entries are the
  combined dumps (`atm2.rom` 64K, `phoenix.rom` 64K, `plus3-41.rom` 64K, `profi.rom` 64K,
  `tsconf.rom` 64K, `prof39f.rom` 128K, `zxevo-fe.rom` 512K).
- `roffset` is 0 / 16 / 32 / 48 KB - that is, a 16K bank index, and the only field carrying
  information.

So on every shipped machine the quadruple carries exactly one piece of information: which
bank a file goes to. That does **not** mean the capability should go - splitting a dump
that someone hands you is a real thing to need, and it costs nothing to keep, since the
engine already does it (`prfSetRomset`, `profiles.cpp:279`). It means the *ordinary* view
must not ask about it: the short form is the only one anybody writes, and the long form
stays available for the case that needs it.

No bundling either, and it would not have helped - bundled or not, the copyright status of
a dump is the same, and `config/roms/PROVENANCE.md` tracks each image with its own MD5 and
rights tier, which a merged blob would destroy.

### 6.2 The format

ROMs are named inside the machine definition, by bank. The key is the bank, so `roffset`
disappears into it; `foffset` and `fsize` are an optional tail, defaulted to "the whole
file":

```
[rom]
rom0 = 128-0.rom
rom1 = 128-1.rom
gs   = gs105b.rom       # General Sound, its own 32K space
font = sgen.rom         # character generator, its own space
```

A machine whose ROM comes as one combined dump names one file at bank 0
(`rom0 = tsconf.rom`) and it fills as far as it reaches. The long form appends the part of the
file to take, in KB - `rom2 = big-dump.rom:32:16` is "16K starting 32K into the file, at bank
2" - and is what the shipped definitions never use.

### 6.3 The two views

**Ordinary**: the machine's ROM slots, one row each, a file picker per row. For the classic
ZX machines that is four 16K banks; a machine that ships a combined dump shows the one row
it has. Nothing about offsets is on screen, and the row is complete without them.

**Advanced**, behind a button: the same rows with the offset and size columns exposed, plus
add and remove, which is the current romset editor minus the global list it edits.

### 6.4 Variants

The shipped data already has ROM variants of the same machine - `Pentagon 128` vs
`Pentagon 128 (TR-DOS 5.03)`, `ZX Spectrum +2A/+3 (v4.0)` vs `ZX Spectrum +3 (v4.1)`,
`Scorpion ZS 256` vs `Scorpion ZS 256 (ProfROM 3.9f)`. Those stay, as a named set inside
the machine:

```
[rom]
rom0 = 128p-0.rom
rom1 = 128p-1.rom
rom2 = gluck.rom
rom3 = trdos504t.rom

[rom.trdos503]
name = TR-DOS 5.03
rom3 = trdos.rom
```

A variant block lists only the banks it changes. In the UI this is one "ROM set" combo
above the slot rows, holding the machine's variants; picking one refills the rows, and a
row the user then changes by hand lands in their override block as `rom3 = something.rom`.
The global `[ROMSETS]` section goes away, and its editor becomes the Advanced view of 6.3.

### 6.5 Missing files

A ROM file that cannot be opened gets a message box naming the file and the machine, and an
`xlog` line at `XLL_ERROR` (today: an `XLL_WARN` line and a machine that boots to garbage).

### 6.6 CMOS and NVRAM

Per machine, keyed by machine id, in the user directory: `nvram/<id>.cmos` and
`nvram/<id>.nvram`. They are the user's own settings that the emulated machine keeps, so
they are written there and nowhere else.

A machine that needs starting contents ships them in the resources
(`:/res/nvram/<id>.cmos`), and the user's file is seeded from that the first time the
machine is used. Today exactly one machine has such data - the 256-byte
`config/profiles/ZX Evo (TSConf)/ZX Evo (TSConf).cmos` - and it is currently a file the
user can silently end up without. Note that the TSConf boot target comes out of that NVRAM
(see the "TSConf reset model" note), so losing it is not cosmetic.

---

## 7. Machine names and identifiers

### 7.1 Two name fields, and a third that is about to stop being persisted

`HardWare` (`hardware/hardware.h:90`) carries both, and its own comments say what they are:

```
const char* name;       // name used in conf file
const char* optName;    // name used in options window
```

`optName` is display only - `SetupWin` puts it in the combo box with `name` as the item's
data - so it has always been free.

`name` looks like a compatibility surface, and today it is one: it is written into every
profile as `[MACHINE] current = ...`. But that surface disappears with the profiles. It is
read from a file in exactly one place, `prfSetHardware()` (`profiles.cpp:216`) handing
`prf->hwName` to `findHardware()`, and the only other uses in the whole tree are three log
lines (`filer.cpp:409`, `spectrum.c:624`, `profiles.cpp:175`) and the combo box's in-memory
item data (`setupwin.cpp:460,836`). No save state, snapshot or RZX file records it.

So after the migration reads the old configs once, `name` is a purely internal identifier
that appears only in definitions we ship. **This is therefore the moment to correct it**,
and the cost is a handful of extra rows in the alias table the migration already needs
(7.6). Leaving a core called `Spectrum +2` that implements a +2A because a file format we
are deleting used to contain that string would be preserving somebody else's mistake for no
one's benefit.

That gives three layers, and all three are in play:

| layer | where it lives | changeable |
|---|---|---|
| core name | `HardWare.name` | yes, once - via the migration alias table |
| display name | `HardWare.optName` and the definition's `name` | yes, always |
| machine id | the definition's `id` (new) | yes - a new namespace |

### 7.2 Machine ids

The id is new, so nothing existing depends on it. It is used for `machine = <id>` in
`xpeccy.conf`, the `[machine.<id>]` override blocks, `nvram/<id>.*`, `--machine <id>` and
the definition's own resource name.

Rules: lowercase ASCII, digits and `-`, no spaces, no `+`. That is not cosmetic - an id
becomes a file name, and today's profile directories (`ZX Spectrum +2A`) already mix spaces,
`+` and case across Windows and Linux filesystems.

Prefer the short name people actually use over the full designation: `pent`, `scorp`. The
model number goes in only where it distinguishes machines we ship separately (`pent1024`),
not to be complete - `scorp` covers the ZS Scorpion whatever the board revision, and we make
no difference between them. The full name lives in the display column, which is where a user
reads it.

An id is not the core name: there are more machines than cores (one Pentagon core serves
the 128 and the 512; one 48K core serves the bare machine and the Beta Disk one). The
definition names its core with `hw = <HardWare.name>`.

### 7.3 The list

Display name on the left, id in the middle, core on the right. `*` marks what phase 1
changes.

| display name | id | core (`HardWare.name`) | was |
|---|---|---|---|
| ZX Spectrum 48K | `zx48` | `ZX48` | `ZX48K` |
| ZX Spectrum 48K + TR-DOS | `zx48-trdos` | `ZX48` | |
| ZX Spectrum 128K / +2 | `zx128` | `ZX128` | **new** - one machine, two ROM sets (7.4) |
| ZX Spectrum 128K + TR-DOS | `zx128-trdos` | `ZX128` | |
| ZX Spectrum +2A | `zxplus2a` | `Plus2A` | `Spectrum +2` - **this core is the +2A** |
| ZX Spectrum +3 | `zxplus3` | `Plus3` | `Spectrum +3` |
| Pentagon | `pent` | `Pentagon` | 128K or 512K is a setting (7.4) |
| Pentagon 1024 SL | `pent1024` | `Pentagon1024SL` | |
| ZS Scorpion 256 | `scorp` | `Scorpion` | |
| Profi | `profi` | `Profi` | |
| ATM Turbo 2+ | `atm2` | `ATM2` | |
| ZXM-Phoenix | `phoenix` | `Phoenix` | |
| ZX Evolution (BaseConf) | `evo-baseconf` | `Baseconf` | `PentEvo` |
| ZX Evolution (BaseConf 2021) | `evo-baseconf21` | `Baseconf21` | `PentEvo21` |
| ZX Evolution (TSConf) | `evo-tsconf` | `TSConf` | `TSLab` - TS-Labs is the group |

Six cores are renamed. `Spectrum +2` -> `Plus2A` is the one that matters, because the name
was actively lying about what the code does; the rest are consistency (`ZX48` beside
`ZX128`, `Plus3` beside `Plus2A`) or accuracy (`TSLab` is the group that made TSConf, not a
machine; the board is ZX Evolution and `Baseconf` is the configuration on it). Old spellings
are resolved by the migration table in 7.6 and then never referenced again.

### 7.4 When something is a machine, a ROM set, or a setting

Three of the shipped entries turn out not to be machines of their own, and the project's own
data says so.

**ZX Spectrum 128K and +2 are one machine.** Diffing the two shipped profiles gives exactly
two lines:

```
-current = Pentagon              +current = Spectrum +2      # the core
-current = ZX Spectrum 128K      +current = ZX Spectrum +2   # the romset
```

Everything else - `memory`, `cpu.frq`, `contio`, `geometry`, `contPattern`, `earlyTiming` -
is byte-identical, which is the emulator agreeing that the grey +2 is a 128K in a different
case with a tape deck bolted on. After phase 1 puts both on the `ZX128` core, the first of
those two lines goes too and the whole difference is **the ROM**. So they are one entry,
`ZX Spectrum 128K / +2`, with two ROM sets in the combo - the way most emulators list them.

The +2A is where the machine really changes: `contio = no`, `geometry = ZX +2A/+3`,
`contPattern = 2`, and the 0x1FFD paging. It and the +3 are separate machines, not ROM sets
on this one.

**Pentagon 512 is a RAM size.** `Pentagon 128` and `Pentagon 512` differ in one line,
`memory`. And the RAM control is already on screen and already correct: `setmszbox()`
(`setupwin.cpp:1706`) fills it from the core's `mask`, so picking Pentagon offers exactly
128K and 512K and nothing else. A second list entry says nothing the dropdown does not.

So the three tiers:

| the difference is | it becomes |
|---|---|
| the core, or the timing | its own machine |
| the ROM only | a ROM set inside one machine, named in that machine's name |
| a value that already has its own control | a setting |

Nothing is lost for an existing user: an old `Pentagon 512` profile migrates to `pent` with
`[machine.pent] memory = 512` in the override block, the same machine by another route -
and a good check that the override model does what it claims.

### 7.5 What comes out of the names

Two version tails in today's `optName`s do not belong in a machine name:

- **`ATM Turbo 2+ (v7.10)`** - a firmware version, so it is a ROM variant (6.4), not part of
  the machine.
- **`Evo Baseconf (before 2021)` / `(after 2021)`** - a real hardware revision, so these
  stay two machines; only the wording changes.

Where I am not certain of the official styling, and it should be checked against the
hardware's own documentation rather than taken from here: `ZXM-Phoenix` (hyphen and case),
`ATM Turbo 2+` (hyphenation varies in the wild), the full designation of the Scorpion -
which the shipped profile writes as `Scorpion ZS 256` and the core's `optName` as
`ZS Scorpion` - and **TSConf**, where the wild has TSConf, TS-Conf and TS-Configuration.
This repository has settled on `TSConf` (26 uses in prose against no `TS-Conf` at all), and
consistency with what is already written and shipped is worth something, but that is a
convention, not a citation.

None of these blocks anything, because of an asymmetry worth stating plainly: **the id has
to be right now, the display name never does.** An id becomes a file name
(`nvram/evo-tsconf.cmos`) and a config key, so changing it later means another migration.
A display name is one string in one definition and can be corrected in any release at no
cost. So take the ids from 7.3 and treat every spelling above as pending.

One naming choice worth a decision rather than a silent pick: the shipped profiles say
**"+ TR-DOS"**, which is the operating system in the ROM; the hardware is the Beta Disk
interface. "TR-DOS" is what the community searches for, so the table keeps it - but it is a
choice, not a fact.

### 7.6 Not breaking what works

Four guarantees, all cheap:

1. `HardWare.name` is never edited, so an old profile always resolves to a core.
2. Migration maps old profile *directory names* to new machine ids through an explicit alias
   table, which is the only place the old names appear.
3. `-p` / `--profile <old name>` keeps working through that same table.
4. Machine ids are a new namespace, so no existing file, script or shortcut refers to one.

The alias table is written once, in phase 3, and is the whole compatibility story. It has
two halves - old profile name to machine id, and old core name to new core name:

```
ZX Spectrum 48K            -> zx48
ZX Spectrum 48K + TR-DOS   -> zx48-trdos
ZX Spectrum 128K           -> zx128
ZX Spectrum 128K + TR-DOS  -> zx128-trdos
ZX Spectrum +2             -> zx128, rom set "+2"   # see below
ZX Spectrum +2A            -> zxplus2a
ZX Spectrum +3             -> zxplus3
Pentagon 128               -> pent
Pentagon 512               -> pent + memory = 512
Pentagon 1024 SL           -> pent1024
...

ZX48K        -> ZX48          # old HardWare.name, seen only in [MACHINE] current =
Spectrum +2  -> Plus2A
Spectrum +3  -> Plus3
PentEvo      -> Baseconf
PentEvo21    -> Baseconf21
TSLab        -> TSConf
```

The core half is a safety net rather than the main road: a profile is normally matched by
its own name, and the core name only decides the outcome for a profile the user renamed or
made themselves.

The `+2` line is the one place migration deliberately changes behaviour, and it is worth
being clear about why. That profile is labelled `+2`, loads the real +2 ROMs, and carries
128K timing - but runs the +2A core, so its paging answers on 0x1FFD. It has never been the
machine it says it is. Migrating it to `zx128` with the `+2` ROM set gives the user what the
label promised; leaving it on `zxplus2a` would preserve a defect on the grounds that it is
old. Anyone who actually wanted +2A paging was using the `+2A` profile, which is untouched.

---

## 8. Work items

Each phase is meant to be shippable on its own.

### Phase 0 - reference data - DONE
Build the machine reference table: for each machine, the correct CPU frequency, RAM sizes,
video layout, contention pattern, port decode, sound chips, disk interface. Sources: the
layouts already in `config.conf`, the wiki, and the raster-geometry notes in memory.

It is `docs/machines-reference.md`, and it is what phase 2 generates the shipped definitions
from.

### Phase 1 - machine list cleanup (core, independent of everything else) - DONE

The point of this phase is to stop the machine list lying, once, so it never has to be
revisited. Names and structure both.

- **Add the 128K core** (`HW_ZX128`): 0x7FFD decoded with mask `0xC002`, no bits 6-7, the
  128K ROM pair.
- **Put it where it belongs in the lineage.** The 128K is the parent of the Pentagon, not a
  borrowing from it, and the code should read that way. Ten of Pentagon's thirteen
  `HardWare` callbacks are already the shared `zx_*` ones; the only real difference is
  paging, and `penMapMem()` (`pentagon.c:3`) is the 128K's mapping plus two extra bank bits:

  ```c
  pg = (comp->p7FFD & 7) | ((comp->p7FFD & 0xc0) >> 3);   // Pentagon
  pg = (comp->p7FFD & 7);                                  // 128K
  ```

  The ROM half (`flgDOS ? 2 : 0 | flgROM`) is identical, and a 128K with a Beta Disk needs
  the DOS bank too. So this is one shared helper taking the extension as a parameter, with
  both cores on it - not two near-identical functions.
- **Order `tabHwPtr` by lineage**: the Sinclair line first (48, 128, +2A, +3), then the
  clones. Today the 128K does not exist and the Amstrad machines sit past a separator at the
  bottom.
- **Rename the six cores** per 7.3. `Spectrum +2` -> `Plus2A` is the one that was actively
  wrong.
- Audit `cpu.frq` and the layout of every machine against the phase 0 table.
- **Verify Pentagon is unchanged.** It is the most used machine, the shared helper touches
  its paging, and there is no test suite - so check it against the current build on
  something that pages hard, using the A/B method in the "Tick/dot drift" and "Demo test
  loop" notes rather than by reading the diff.
- Test the `ZXONLY` toggle *in place*, both ways (`CLAUDE.md` -> "ZX-only build").

Beyond the list: the shipped profiles name the new cores, and `ZX Spectrum +2` moved onto the
128K core with its own ROM set - 7.4 in profile form, so phase 3 has nothing left to decide
there. `HW_PLUS2` is `HW_PLUS2A`. The core half of the alias table (7.6) is in
`prfSetHardware()` (`xcore/profiles.cpp`), the one place a name from a file reaches the core,
and where the profile-name half joins it in phase 3.

### Phase 2 - machine definitions - DONE
- The definition format and its loader (`src/xcore/machines.cpp`, `xMachine` in `xcore.h`),
  reading from Qt resources first and the user directory second.
- Generate the shipped definitions from today's 15 machine profiles, cleaned per 3.2.
- Move the ROM model to per-bank keys (6.2), keeping the offset tail, and fold `xRomPreset`
  (`setupwin.cpp:1476`) into the definitions.
- Seed data for CMOS/NVRAM into the resources (6.6), starting with the TSConf one.
- Nothing observable changes yet: profiles still work, still load, still save.

The definitions are `res/machines/<id>.conf`, the seed is `res/nvram/evo-tsconf.cmos`, and
the one thing reading them so far is the ROM preset button in Options. Five points where the
format came out different from 4.3:

- **The file name is the id**, so there is no `id` key to keep in step with it.
- **A bank is `rom<N>`, not a bare number.** A number would have to be the "everything else"
  branch of the parser, which quietly turns a mistyped key into bank 0; a named key can be
  told apart from a mistake, and one gets a warning.
- **No `sdc` key**: nothing in `Computer` says whether a card reader is fitted.
- Added, because today's profiles vary them and they are machine traits: `reset` in
  `[machine]`, `soundrive` in `[sound]`, `joy.buttons` in `[input]`.
- **A child's `[rom]` block names the banks it changes**, on top of the set it inherits; the
  parent's ROM *variants* are not inherited, since a variant belongs to the machine listing it.
- **A `[rom.<variant>]` block is an overlay applied after the base set**, so a variant that is
  one combined dump (`rom0 = plus3-41.rom`) covers the base banks by itself and needs no syntax
  for clearing them.

The machines with no definition - the non-ZX ones - keep a small ROM preset table of their
own in `setupwin.cpp`, under `#ifndef XZXONLY`.

Two notes for later phases. Phase 4 retires `xRomset`'s global list, and with it the overlap
between `xRomset` and `xMachineRoms`. And phase 5 deletes the `xm_find_by_core()` call in
`SetupWin::romPreset()` rather than extending it: a core does not say which machine is running,
so the preset now offers the plain machine of that core - `ZX48` gives the 48K's one ROM, not
the 48K + TR-DOS pair it used to.

### Phase 3 - drop profiles - DONE
- One `Computer` in the process. `conf.prof.cur->zx` -> the global; `brk`, `labsets`,
  `commap` -> `conf`.
- Migration on first run: resolve each profile through the alias table (7.5), then write
  `machine = <id>` plus an override block for whatever differs from that definition. Media, palette, keymap and gamepad map become the global session state.
  Every old profile directory is left on disk untouched, and a log line says so.
- `-p` / `--profile` keeps working as an alias that resolves an old profile name to a
  machine id; `-m` / `--machine` is the name from here on (decision 5).
- CMOS and NVRAM move to `nvram/<id>.*` in the user directory (6.6).
- `schema = 2` in `xpeccy.conf`, so a later change can migrate again.

Where it came out differently from 4.2:

- **The one user file is `config.conf`**, the name it already has, not `xpeccy.conf`. Renaming
  it would have been a migration of its own for no gain, and `xpeccy.conf` was the *profile*
  file name - reusing it right after dropping profiles is the confusing choice, not the tidy
  one. The sections it grew are `[MACHINE.<id>]` and `[MEDIA]`.
- **Overrides land in phase 3, not phase 4.** Migration has to keep what a user tuned, and
  that only means anything if loading and saving already work in terms of "what differs from
  the definition". So `xm_save()` diffs the live machine against its definition and writes
  only the difference; a machine that matches its definition writes no block at all. What is
  left for phase 4 is the `[ROMSETS]` and layout tables.
- **A named romset that is the machine's own set is dropped on migration**, so the shipped
  machines are not pinned to the old romset table and take a ROM fix from an update. An empty
  `romset` means "the machine's own ROMs"; a name still points into `[ROMSETS]`.
- The settings that sit on the `Computer` but belong to the user - what is mounted, the PSG
  clock and stereo, mouse and keyboard preferences, the turbo multiplier, the watched ports -
  are collected while the file is read and applied once the machine is up (`xm_defer()`), so
  building the machine cannot wipe them.
- The Machine page's own combo is the machine list, in the lineage order the cores are in,
  and picking another machine loads it - definition, overrides, reset - and shows its
  settings instead of writing the page's values over it. That also retires
  `xm_find_by_core()`'s use in the ROM preset, which now knows the machine exactly, and the
  small non-ZX preset table with it: a machine with no definition cannot be picked, so a
  non-ZX build needs definitions before its machines come back (see 9.3).
- The Options "Profiles" page is a second machine list for now, and its add / copy / delete
  buttons are hidden. Phase 5 replaces the page.
- `res/fallback/config.conf`, what a config directory is bootstrapped from, is schema 2 and
  starts on `zx48` - the one machine whose ROM a bootstrapped directory has, since `48.rom`
  is copied out of the resources beside it. `res/fallback/xpeccy.conf` was the profile half
  of that pair and is gone.
- `-p` resolves a name through `xm_id_for_name()`, which takes a machine id, the name an old
  profile went by, or a machine's display name.

### Phase 4 - the file split
- Machine keys move out of the user's file except as overrides. **Done in phase 3** - the
  overrides had to exist for the migration to keep anything.
- The layout table moves out of the user's file. **Done.**
- The `[ROMSETS]` table moves into the machine definitions. **Done**, together with the
  editor over it (6.3, the two views apart).
- Palettes, styles, shaders and keymaps become built-in plus user-directory entries (4.1).
  **Half done.** They ship inside the binary, and `xres_path()` / `xres_list()`
  (`xcore/common.cpp`) read and list the config directory first and the binary second, so a
  config directory with none of them - a fresh install, or `--confdir` - now has the whole
  list instead of empty combo boxes. What is *not* done is stopping the install from writing
  them into the config directory as files: while a copy sits there it shadows the built-in
  one, so a shipped fix still does not reach a user whose directory was seeded (3.4). That
  half is a packaging decision - the files are also how a user copies one to tweak it - and
  is left for whoever takes it.

**Layouts are named and shared, not inlined into each definition.** 4.1 and this phase's own
bullet disagreed; 4.1 wins. Four machines use the Pentagon geometry, so inlining copies the
same eleven numbers four times and there is nothing left for the layout editor to edit. They
ship in the binary as `res/layouts.conf` and a `layouts.conf` in the config directory adds to
that list, an entry of the same name replacing the shipped one. Only what the user added or
changed is written back, so a `layout =` line in an old `config.conf` migrates itself on the
first save and a shipped fix reaches everyone who never touched that layout.

**The romsets and their editor went together**, since dropping `conf.rsList` leaves the
editor with nothing behind it. What a machine loads is now `conf.roms`: its own set out of the
definition, the chosen variant over that, and the user's own files over that again. The combo
lists the machine's variants ("Its own" plus each `[rom.<id>]`), the table is the machine's
slots, and a row the user changes is written as `rom<N>` in the machine's own block. Adding or
deleting a whole romset is gone - the sets are the machine's.

Three things fell out of it:

- **An empty value empties a bank.** `rom2 =` says "nothing here" over a set that has
  something, which the editor's delete needs and a migrated romset needs too.
- **A line with nothing after the `=` is a value, not a section.** The config parser treated
  it as a section header, so any cleared setting was silently dropped - a cleared shortcut
  could not be saved either.
- **Migration reads `[ROMSETS]` once and never writes it.** A named set that is the machine's
  own becomes nothing, one that matches a variant becomes that variant, and anything else
  becomes the files themselves as `rom<N>` keys.

Still phase 6: the ordinary/Advanced split of the ROM rows (6.3) - offsets and sizes are on
screen as they always were.

### Phase 5 - the UI - PART DONE
- A machine selector in the main window: always visible, one control.
- Options regrouped into two groups, each page headed with what it applies to:
  **This machine** (machine, ROM set, storage, peripherals - with the phase-6 settings
  behind an "Advanced" button) and **Application** (video output, audio output, input and
  hotkeys, interface, debugger, log, paths).
- Overridden fields marked; "Reset this machine to defaults" on the machine page.
- A definition declares what applies to it, so the UI greys what a machine does not have
  instead of showing dead controls.
- The Profiles page goes away.

Decided while building it, both by the user:

- **The machine selector stays in the menu.** The emulator window is the picture and nothing
  else; a strip under it would cost the picture height for a control that is one click away
  in the menu anyway. That menu is called Machine now, lists the machines in lineage order
  with a separator between the Sinclair ones and the clones, and ticks the one running.
- **Options is not regrouped into "this machine" and "application" yet.** Video, Sound and
  Input each mix the two, so a real regroup means moving widgets between pages - a large
  edit of `setupwin.ui` with a real chance of breaking a layout, for a gain that is
  presentational. Left as a decision of its own.

Done: the Profiles page is gone and the Machine page is what a machine *is* - the machine, its
CPU, its memory, its default reset and its ROMs. Restore machine defaults drops everything the
user changed on it (`xm_reset_over()`); the ULA settings and the raster layout are behind
Advanced settings, a window built in code out of `ui.advBox`.

**The bold marking of changed settings was dropped.** `xm_over_keys()` gave the same diff
`xm_save()` writes, so the marks could not disagree with the file - but they only caught up
when the dialog was reopened, which reads as the marks being wrong. What replaced it is
plainer: names in normal weight, what a setting does in italics beside it, on the Emulation
page as well.

**The ROM slots got their two views** (6.3, phase 6's item, taken here since the group was
being rebuilt anyway). The page shows every slot the machine has - `ROM 0`..`ROM 3`, `GS`,
`Font` - with a combo of the ROM files found under `roms/` and a button beside it for one
from anywhere else. An empty slot reads `(empty)`; one the machine cannot have is greyed and
reads `(not fitted)`. A file outside the rom directory is stored as its own absolute path;
`xm_rom_path()` is where that is decided, so the loader and the size column agree. Advanced,
in the same group, opens the old table - offsets, sizes and positions - in a window of its
own.

How many slots a machine has is `banks` in its `[rom]` section, the 16K pages its core can
address. Four is the default and only the 48K says otherwise: two, BASIC and the interface
ROM the DOS flag pages in. A file bigger than one bank covers the ones after it, and they
say so - `(from ROM 0)` - which is how the machines that load a single image over the whole
ROM space read.

**Romset variants are gone.** `[rom.<id>]` sections, `conf.romSet`, `xm_set_romset()`,
`xm_rom_variants()` and the combo over them: a slot takes any file, so "TR-DOS 5.03 instead
of 5.04" is a file in ROM 3, kept as an override like everything else, and the combo was one
concept too many for what it bought. The one variant that was not a file swap was the 128K's
`plus2` - a different model wearing different ROMs - and that is now `zxplus2`, a machine of
its own inheriting `zx128`. An old config that named a romset still migrates: unless the set
holds what the machine ships with anyway, its files come across as the user's own.

**Machines of the user's own** (7.x, not in the plan as written - asked for while phase 5
was being built). Save as a machine writes the running machine to
`<confdir>/machines/<id>.conf` as `inherit = <the machine it came from>` plus the same diff
`xm_save()` writes to the override block, in the sections a definition uses - so it carries
only what was changed and follows a shipped fix in everything else. The machine it came from
goes back to how it ships: the settings did not disappear, they moved. Delete this machine
removes the file; a definition that shadowed a shipped one leaves that one behind, and the
list is rebuilt either way.

Saving under a name that is already a machine of the user's own writes over it, which is how
a machine is updated - one button, and the name says what happens. Two things fall out of
that. The file has to inherit what the machine it replaces inherited, or it would inherit
itself; and the diff has to be measured against *that* parent, not against the machine being
written, or a machine saved twice with nothing changed would come back empty - the first
save's keys are part of its own definition by then. `mac_put_all()` takes the baseline it
measures against for that reason. A name that belongs to a machine that ships is refused:
a file of that id would shadow the machine it inherits.

Saving applies the page first. Every setting there stages until Apply, so without that the
button would keep what the machine is rather than what the page shows.

The keys of a definition and the keys of an override block are the same words in a different
shape, which is what makes this a mapping and not a translation - `macSectTab` in
`machines.cpp` is that mapping. The one collision it turned up: `gs` is the General Sound
chip in `[sound]` and its ROM file in `[rom]`, and in the flat override block the two could
not be told apart - the ROM file was read as a boolean and switched the chip off. The rom
keys are `rom.gs` and `rom.font` there now.

**The turbo multiplier had two owners.** A machine that switches its own turbo on - Scorpion,
ATM, Pentagon 1024, ZX Evolution - wrote the same `comp->frqMul` the user's Mult box and the
turbo hotkey write, so the multiplier survived into the machine you switched to next and it
ran at the wrong speed. Resetting it on a switch would have papered over it and dropped the
user's own setting with it; instead the machine's turbo is `comp->hwMul`, set through
`compSetHwTurbo()`, and the timings divide by the product. `compReset()` puts it back, which
is where "what the machine is doing" already goes - so a switch and a plain reset are covered
by the same line. Measured: a Scorpion runs 139776 T a frame, and the 48K after it 69888.

**The two extended machines went** once machines of your own worked: `zx48-trdos` and
`zx128-trdos` were a Spectrum with a disk interface and a sound card added, which is now a
thing anyone can make and keep. Their old profile names still migrate, onto `zx48` and
`zx128`, so what the profile added arrives as that machine's own settings.

**Layouts are named after the ULA**: `ULA.48`, `ULA.128`, `ULA.Plus3`, `ULA.Pentagon`,
`ULA.Scorpion`, `ULA.Profi`, `ULA.ATM2`, `ULA.TSConf`. The `default` entry is gone - it was
built in code, held Pentagon geometry, sorted itself to the top and could not be deleted,
and nothing about the name said any of that. `LAY_DEFAULT` (`ULA.48`) is what a machine gets
when the layout it names is missing.

**`config/` ships the roms and little else.** `config.conf` and the seventeen profiles under
it are gone from the install: the settings a fresh start uses are `res/fallback/config.conf`
in the binary, which is the shipped default rather than the bare minimum it used to be, and
the roms its machine needs are resources too. The palettes, shaders, styles and keymaps moved
to `res/`, which is what the binary is built from, so nothing shadows them any more - that
closes 3.4 and the half of 4.1 phase 4 left open. Each of those folders keeps its README, so
the config directory still says what to put there. The empty `plugins` directory is not made
either: it was for cpu cores as shared libraries, which nothing ships and the ZX-only build
cannot even pick.

**One reader for a machine's keys.** The settings kept for a machine were applied a second
way: `mac_apply()` read them from a definition into an `xMachine`, and `mac_set_key()` poked
each one into the running `Computer` after it was already built. The two had drifted - one
clamped `cpu.frq` and warned about an unknown key, the other did neither, and `psg.count` and
`psg.type` had to read the live machine back to avoid clobbering each other. The block holds
the definition's own keys in a flatter shape, so `mac_with_over()` now turns each pair back
into a definition line (`mac_key_place()` says which section it belongs to) and lays it over
the definition; the machine is built once, from the result. `mac_set_key()` is gone, and so is
the `macRomOver` scaffolding that existed only because rom keys were read before the machine
was up - the merged definition carries them like any other key.

**The ROM editors stage like everything else.** They wrote straight into the running machine,
so a ROM picked and then Cancelled was still what the config file got. `SetupWin` keeps its own
`xRomset`, seeded in `start()`, and Apply is what loads it - the same shape as every other
setting on the page.

**`saveConfig()` writes the settings file and nothing else.** It had grown `layouts_save()` and
`xm_save_nvram()`, and it is called from the zoom and fullscreen hotkeys - so changing the
window size rewrote the machine's CMOS. Layouts are saved on Apply and on exit, NVRAM on a
machine switch (where it already was) and on exit.

### Phase 6 - the settings audit
- Behind "Advanced": `4t-border`, `earlyTiming`, `contPattern`, `contio`, `contmem`,
  `scrp.wait`, `DDpal`. **Not** `psg.frq` - decision 6.
- The ROM slots get their two views (6.3): files only by default, offsets and sizes behind
  the same button. **Done in phase 5.**
- One media panel showing everything mounted at once (tape, A-D, HDD master/slave, SD),
  instead of five sibling tabs under Storage. **Left as it is**, on its own evidence: this
  line is the only place the idea is written down, nothing in the plan says why, and the
  four media have genuinely different controls - a block map and a speed for the tape,
  geometry for the hdd, image-or-folder for the sd - so one panel grows a details area and
  ends up being the tabs again. What the idea is really after is "what is in the machine
  right now", which is a question about state, not about settings. If it comes back, it
  comes back as that: an overview, in its own task.
- Export / import of the whole configuration as one file, for sharing a setup and for bug
  reports. **Done.** One text file with `[[<name>]]` markers, holding `config.conf`,
  `layouts.conf` and the machines of the user's own - plain text so it can be read, diffed
  and pasted into a report. Roms, palettes, shaders, styles and keymaps are not in it: those
  are files a person put there, not settings. Import and Reset to defaults both end in
  `reloadConfig()`, which is `loadConfig()` with the throw caught - it was already written
  to be able to run twice, since it clears every list it fills and keeps `conf.zx`.

---

## 9. Out of scope: separate features

Nothing here blocks a start. Each is a feature of its own that this plan has to leave room
for and otherwise stay out of.

### 9.1 Play modes
A hotkey-toggled runtime mode with an overlay per mode - timings for developing, a playlist
rotation for demos, something else for playing (decision 8, and 5.4). Independent of the
machine model; needs a global enum and the overlays, nothing from this work.

### 9.2 Machine-aware media
Media stays global (decision 9). What is wanted later is the file dialog offering what the
current machine can actually take - `.dsk` on a +3, `.spg` on a TSConf. Both formats are
already implemented (`filetypes/dsk.c`, `filetypes/spg.c`, registered at `filer.cpp:52,43`),
so this is a filter over the existing format table keyed by the machine, not new loaders.
The machine definition is the natural place for that list, which is one more reason the
definitions should be easy to extend with a key.

### 9.3 Bringing ALF TV-Game back
The ALF core is still in the tree, but it is in `NONZX_SOURCES`, so the default build does not
have it, and there is no profile for it either. Wanted back later, not now. Nothing here blocks
it: the core exists, so it is a machine definition plus taking `alf.c` out of that list.

### 9.4 Per-machine media
If it turns out people want "the 48K remembers its tape and the TSConf remembers its
image", that is media keyed by machine id in the override block - a small addition, to be
made on evidence rather than upfront.

---

## 10. Risks and traps

- **Config files in the wild.** Migration must never delete or rewrite in place. Leave the
  old profile directories alone and say so in the log.
- **`--confdir` round trip.** Test that definitions are found (resources) while everything
  written lands in the throwaway directory. The staging step of `make-dist.ps1` re-installs
  `config/` on every build, so a config patched by hand for a test is silently back to the
  shipped one after a rebuild.
- **Mixed line endings.** `spectrum.c/.h`, `debuger.cpp`, the `.ui` files and `CHANGELOG.md`
  are CRLF; `profiles.cpp` and `common.cpp` are LF. Do not rewrite a file wholesale
  (`sed -i` flips it) - patch and check `git diff --stat`.
- **`MOCFILES`.** Any new Q_OBJECT header has to be listed by hand in `CMakeLists.txt`.
- **`ZXONLY`.** The definition loader must not name a non-ZX machine outside
  `#ifndef XZXONLY`, and the option has to be toggled in place to test it.
- **Phase 3 is the big one.** 263 call sites and the lifetime of every debugger panel that
  holds a pointer into the current machine. It is mechanical, but it is not small, and it
  is the phase where a regression will not show up in a build - only in running the thing.
- **No test suite.** Every phase is validated by building and running the staged folder in
  `build\dist\`, not the raw link output.
