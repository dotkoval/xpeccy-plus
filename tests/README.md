# Regression tests

`regress.py` runs a staged build headless over a suite of images and compares what the
machine did with a reference: each case prints `--bench-hash` hashes of every frame, every
sound sample, the RAM and the CPU state, so a case either ran exactly as before or it did not.

```
python tests/regress.py run                 # every suite against the reference
python tests/regress.py run timing -k 'float*'
python tests/regress.py run --twice         # also flag a case whose two runs differ
python tests/regress.py bless timing        # take the current results as the reference
python tests/regress.py list
```

- **The build** is the newest dev folder in `build/dist`, or `--app <folder>`. A build made
  with `XRELEASE=1` has no `--bench` and cannot be tested.
- **The images and the references** are not in this repository - most of the images are not
  ours to publish. `XPECCY_CORPUS` (or `--corpus`) is the folder the suite paths are relative
  to, `XPECCY_GOLDEN` (or `--golden`) holds one `<suite>.json` per suite.
- **Suites** are `suites/<name>.txt`, one case a line:
  `name machine auto|open skip frames file [extra emulator args]`. `auto` starts the image
  with `--autostart`, `open` just opens it (a snapshot). The machine runs `skip` frames
  unhashed, then `frames` hashed.
- Every case gets a fresh copy of the build's `config/`, so nothing carries over between
  cases; they run in parallel (`-j`, half the CPU threads by default).

A difference is not a failure by itself: a change meant to alter timing moves the hashes of
the machines it touches, and those are blessed once the change is checked against real
hardware. A change that should alter nothing - a refactor, a speed-up - must leave every
hash where it was.

## Quick tier

```
python tests/regress.py quick                 # build, run every bench, PASS/FAIL per case
python tests/regress.py quick tsconf -k 't1*'
python tests/regress.py quick tape --bless    # take the current output as expected
python tests/core/mkbins.py                   # reassemble the Z80 programs (needs sjasmplus)
```

Seconds, no Qt and no app: `tests/CMakeLists.txt` is a project of its own that builds
libxpeccy as a static library and the benches in `core/` against it, into
`build/out/tests` (on Windows with the qt6-x64 MinGW of `packaging/make-dist.ps1`, or
`--mingw`). Each bench's output is compared, case by case, with `expected.txt` beside it.

- **z80** - `tools/z80test`, Fuse's per-instruction tests.
- **tsconf**, **baseconf** - small Z80 programs on the TSConf and BaseConf cores, each
  checking one thing the hardware's RTL says; `cases.txt` lists them. The programs are
  committed assembled in `bin/` beside their sources in `asm/`, so a run needs no assembler.
- **tape** - TAP and TZX images written by `core/tape/mktape.py`: the blocks as read, the
  bytes decoded back, the pulse lengths a loader measures, and a TAP -> WAV -> TAP round trip.
