# Xpeccy+

A ZX Spectrum emulator with a built-in debugger.

Xpeccy+ is a fork of [Xpeccy](https://github.com/samstyle/Xpeccy) by SAM style. All the
emulation code comes from that project. This fork does not claim authorship of it.

Current version: **2026.0** (development). Forked from upstream build `20260807`.

Windows and Linux are built and tested here. macOS was supported in the original project
and is wanted back, but there is no Mac to build and test on at the moment - help with that
is welcome.

## Why a fork

Upstream Xpeccy is a multi-platform emulator: ZX Spectrum and clones, plus MSX, Game Boy,
NES, Commodore 64, BK0010, IBM PC and more. Its focus is to support more machines.

This fork narrows the focus to the ZX Spectrum, and spends the effort on the parts a user
touches every day. That is a different plan for the same code, so it is easier to follow it
in a separate project than to steer one project in two directions.

Xpeccy+ is also open to other people's work. The aim is a good ZX emulator built together,
so patches, ideas and bug reports are welcome.

## Goals

- **ZX Spectrum first.** The other machines still work, but they are not the target, and
  support for them will likely be given up where it stands in the way of a better ZX
  experience.
- **Works out of the box.** Releases come as a bundle with configuration and rom sets, so
  the emulator is ready to use right after unpacking.
- **Comfortable to play with.** Better gamepad support, rewind, and a smoother way to work
  with tapes and disks.
- **Fewer loading problems.** Fixes for RZX and for non-standard TZX loaders.
- **A debugger worth using.** Interface work and more tools for people who develop for the
  machine, not only for those who poke at it.
- **A modern build.** Qt 6 and a current SDL, dropping the old paths behind.
- **Documentation** that actually explains things.

Accuracy is not traded away for any of this. The emulation core is inherited from Xpeccy
and keeps doing its job; the work here goes into everything around it.

## What is already different

On top of upstream build `20260807`: lower input lag, a memory heat map and AF/AF' in the
debugger, a release bundled with rom sets and machine profiles, a working ZX Evo (TSConf),
tape loading fixes, and a fix for a crash on profile switching.

See [CHANGELOG.md](CHANGELOG.md) for the full list.

## Build

Requirements: CMake, a C/C++ compiler, Qt (5 or 6), SDL (1.2 or 2), and zlib.

```
mkdir build && cd build
cmake [options] ..
make
```

Options:

| Option | Values | Meaning |
| --- | --- | --- |
| `-DQTVERSION=` | `5` (default) or `6` | Qt version |
| `-DSDL1BUILD=` | `0` (default) or `1` | use SDL 1.2 instead of SDL2 |
| `-DUSEOPENGL=` | `1` (default) or `0` | draw through a QtOpenGL widget |
| `-DUSEQTNETWORK=` | `0` (default) or `1` | QtNetwork support, experimental |
| `-DXRELEASE=` | `0` (default) or `1` | release version string, without `-dev` and build date |
| `-DTRIMDEPLOY=` | `ON` (default) or `OFF` | Windows: drop the Qt plugins the emulator never loads |

The result is the `xpeccy-plus` executable. On Linux you can also build a package with
`make package`, or install with `make install`.

### Linux

On Debian and Ubuntu `packaging/linux-setup.sh` installs the toolchain (build tools, Qt 5,
SDL2, zlib) and downloads the AppImage tools into `~/.cache/xpeccy-plus-tools`. After that:

```
packaging/linux-setup.sh
packaging/make-appimage.sh
```

`make-appimage.sh` configures, builds, installs into an `AppDir` and runs `linuxdeploy`,
leaving `xpeccy-plus-<version>-linux-x86_64.AppImage` in the build directory. `CLEAN=1`,
`RELEASE=1`, `SRC_DIR`, `BUILD_DIR` and `JOBS` change what it does.

The icon it deploys is `images/xpeccy-plus.png`, which has to keep one of the sizes the
icon theme spec allows - `linuxdeploy` refuses anything else, and 128x128 is what it is.

The build directory defaults to `~/build/xpeccy-plus` rather than to `build/` inside the
sources, because `linuxdeploy` makes symlinks and sets permissions in the `AppDir`, which
a Windows filesystem mounted into WSL cannot do. The sources themselves may live anywhere.

An AppImage carries Qt and SDL but not the C library, so it needs a distribution at least
as new as the one it was built on - built on Ubuntu 22.04 it wants glibc 2.35 or newer.

### Windows

Windows is built and tested here daily, unlike in upstream. What has to be installed:

| | |
| --- | --- |
| Qt | 5.15.2 with MinGW 8.1 (32 or 64 bit), or 6.6.2 with MinGW 11.2 (64 bit) |
| MinGW, CMake | come with the Qt installer, under `C:\Qt\Tools` |
| zlib | comes with MinGW and is linked statically |
| SDL2 | fetched by the script below |

Qt 6 has no 32-bit Windows build at all, so a 32-bit binary means Qt 5.

```
packaging\fetch-deps.ps1
packaging\make-dist.ps1 -Preset qt5-x64 -Zip
```

The first downloads SDL2 into `build\deps`. Its version is pinned in
`packaging\deps.json` and the archive is checked against a stored SHA-256, so a build of a
given tag stays reproducible; `-Update` moves the pin to the newest upstream release and
rewrites the manifest, which is meant to be committed on its own.

The second configures, builds, and stages a ready-to-run folder under `build\dist` -
binary, Qt and SDL runtime, `config\`, and the docs - optionally zipped. Presets are
`qt5-x64` (default), `qt5-x86` and `qt6-x64`; useful flags are `-Clean`, `-Release`,
`-Zip` and `-Full`. Paths to the toolchains live in a table at the top of the script.

Assembling that folder is CMake's job, not the script's: `cmake --install <build-dir>
--prefix <dir>` produces the same result. `windeployqt` runs as an install step, after
which the Qt plugins this emulator never loads are removed - see
`cmake/windeploy.cmake.in`, which lists what goes and why. Build with `-DTRIMDEPLOY=OFF`
to keep everything `windeployqt` copied.

`version.h` is generated by CMake from `cmake/version.h.in` and is not stored in the
sources. A development build carries the build date as SemVer build metadata and shows
the version in brackets in the window title - `Xpeccy+ (2026.0-dev+20260807)`, against
`Xpeccy+ (2026.0)` for a release build.

## Bundled ROMs

Releases ship with `config/`, so the emulator works right after unpacking: rom images for
the ZX-compatible machines (`config/roms/`), a romset for each of them and a ready profile
per machine. Machines outside the ZX line are still supported by the code, but their roms
are not bundled.

The images are firmware of the emulated machines and are **not** covered by the MIT license
of this project - they stay under the terms of their own copyright holders. Details:
[`config/roms/LICENSE`](config/roms/LICENSE), the canonical Amstrad notice in
[`config/roms/AMSTRAD.copyright`](config/roms/AMSTRAD.copyright), and a per-file table of
where each image came from in [`config/roms/PROVENANCE.md`](config/roms/PROVENANCE.md).
No image was modified; the copyright messages inside them are intact.

## Credits and license

Xpeccy was written by **SAM style** (<https://github.com/samstyle/Xpeccy>) and is
distributed under the MIT license. The original copyright is kept intact - see
`LICENSE_eng` (`LICENSE_rus` for the Russian text). Xpeccy+ is released under the same
license, and is maintained by Oleksandr ".koval" Kovalchuk.

The original build instructions are kept as `README.upstream`, together with the links to
the original documentation. They describe upstream's process, which already differs from
this fork's.

## Contact

Bug reports, ideas and patches: [GitHub issues](https://github.com/dotkoval/xpeccy-plus/issues)
and pull requests.
