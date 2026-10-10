#!/usr/bin/env python3
"""Run the emulator headless over a suite of images and compare the hashes with a reference.

Each case starts a fresh copy of the app's config, runs the machine for a fixed number of
frames with --bench-hash and records the frame, sound, RAM and CPU hashes it prints. Two
builds that print the same hashes ran the machine identically.

  regress.py run [suite...] [-k PATTERN]   compare with the reference, exit 1 on a difference
  regress.py bless [suite...] [-k PATTERN] take the current results as the reference
  regress.py list [suite...]
  regress.py quick [bench...] [-k PATTERN]  the headless libxpeccy benches, no app needed

The images and the references are not in this repository: --corpus/XPECCY_CORPUS and
--golden/XPECCY_GOLDEN point at them. Needs a build without XRELEASE (no --bench there).
"""

import argparse
import concurrent.futures as cf
import fnmatch
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SUITES = Path(__file__).resolve().parent / "suites"
EXE = "xpeccy-plus.exe" if os.name == "nt" else "xpeccy-plus"
FIELDS = ("frames", "sound", "ram", "cpu", "pc", "T")
HASH_RE = re.compile(r"^hash: frames (\w+) sound (\w+) ram (\w+) cpu (\w+) pc (\w+) T (-?\d+)", re.M)


class Case:
    def __init__(self, suite, name, machine, how, skip, frames, file, extra):
        self.suite, self.name, self.machine, self.how = suite, name, machine, how
        self.skip, self.frames, self.file, self.extra = int(skip), int(frames), file, extra

    @property
    def key(self):
        return f"{self.suite}/{self.name}"


def load_suite(name):
    path = SUITES / f"{name}.txt"
    cases = []
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        f = shlex.split(line)
        if len(f) < 6 or f[2] not in ("auto", "open"):
            sys.exit(f"{path}:{n}: expected: name machine auto|open skip frames file [args...]")
        cases.append(Case(name, f[0], f[1], f[2], f[3], f[4], f[5], f[6:]))
    return cases


def all_suites():
    return sorted(p.stem for p in SUITES.glob("*.txt"))


def find_app(arg):
    """The staged dist folder to run: given, or the newest dev build in build/dist."""
    if arg:
        app = Path(arg)
    else:
        dist = ROOT / "build" / "dist"
        cands = [d for d in dist.glob("*-dev+*") if (d / EXE).exists()] if dist.exists() else []
        if not cands:
            sys.exit("no dev build in build/dist; pass --app <staged folder>")
        app = max(cands, key=lambda d: (d / EXE).stat().st_mtime)
    if not (app / EXE).exists() or not (app / "config").is_dir():
        sys.exit(f"{app}: not a staged build (needs {EXE} and config/)")
    return app


def run_case(case, app, corpus, shots, timeout):
    image = corpus / case.file
    if not image.exists():
        return case, {"error": f"missing image {image}"}, 0.0
    with tempfile.TemporaryDirectory(prefix="xreg-") as tmp:
        # A fresh config each time: the app restores the last media and machine on start.
        cfg = Path(tmp) / "config"
        shutil.copytree(app / "config", cfg)
        cmd = [str(app / EXE), "--confdir", str(cfg), "-m", case.machine]
        cmd += ["--autostart", str(image)] if case.how == "auto" else [str(image)]
        cmd += ["--bench-skip", str(case.skip), "--bench", str(case.frames), "--bench-hash"]
        if shots:
            cmd += ["--bench-shot", str(shots / f"{case.suite}-{case.name}.ppm")]
        cmd += case.extra
        t0 = time.monotonic()
        try:
            p = subprocess.run(cmd, cwd=app, capture_output=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            return case, {"error": f"timeout after {timeout}s"}, time.monotonic() - t0
        dt = time.monotonic() - t0
    out = p.stdout.decode("latin-1", "replace")
    m = HASH_RE.search(out)
    if not m:
        tail = (out + p.stderr.decode("latin-1", "replace")).strip().splitlines()[-3:]
        return case, {"error": f"no hash line (exit {p.returncode}): {' | '.join(tail)}"}, dt
    return case, dict(zip(FIELDS, m.groups())), dt


def golden_path(golden, suite):
    return golden / f"{suite}.json"


def load_golden(golden, suite):
    p = golden_path(golden, suite)
    return json.loads(p.read_text(encoding="utf-8")) if p.exists() else {}


def pick(args):
    suites = args.suite or all_suites()
    cases = [c for s in suites for c in load_suite(s)]
    if args.k:
        cases = [c for c in cases if any(fnmatch.fnmatch(c.name, k) or fnmatch.fnmatch(c.key, k) for k in args.k)]
    if not cases:
        sys.exit("no cases match")
    return cases


def env_path(arg, var, what):
    v = arg or os.environ.get(var)
    if not v:
        sys.exit(f"no {what}: pass --{what} or set {var}")
    return Path(v)


def execute(args, cases):
    app = find_app(args.app)
    corpus = env_path(args.corpus, "XPECCY_CORPUS", "corpus")
    shots = Path(args.shots).resolve() if args.shots else None
    if shots:
        shots.mkdir(parents=True, exist_ok=True)
    print(f"app {app.name}, {len(cases)} cases, {args.jobs} at a time", flush=True)
    results = {}
    t0 = time.monotonic()
    with cf.ThreadPoolExecutor(args.jobs) as pool:
        futs = [pool.submit(run_case, c, app, corpus, shots, args.timeout) for c in cases]
        for f in cf.as_completed(futs):
            case, res, dt = f.result()
            results[case.key] = res
            if args.verbose:
                print(f"  {case.key:28} {dt:6.1f}s {res.get('error', '')}", flush=True)
    print(f"done in {time.monotonic() - t0:.0f}s", flush=True)
    return results


def cmd_run(args):
    cases = pick(args)
    golden = env_path(args.golden, "XPECCY_GOLDEN", "golden")
    results = execute(args, cases)
    if args.twice:
        again = execute(args, cases)
        for k, r in again.items():
            if r != results[k] and "error" not in r:
                results[k] = {"error": "not deterministic: two runs differ"}
    refs = {s: load_golden(golden, s) for s in {c.suite for c in cases}}
    same = changed = new = failed = 0
    for c in cases:
        r, ref = results[c.key], refs[c.suite].get(c.name)
        if "error" in r:
            failed += 1
            print(f"FAIL     {c.key:28} {r['error']}")
        elif ref is None:
            new += 1
            print(f"NEW      {c.key:28} no reference yet")
        elif all(r[f] == ref.get(f) for f in FIELDS):
            same += 1
        else:
            changed += 1
            which = ", ".join(f for f in FIELDS if r[f] != ref.get(f))
            print(f"DIFFERS  {c.key:28} {which}")
    print(f"\n{same} same, {changed} differ, {new} new, {failed} failed")
    if args.json:
        Path(args.json).write_text(json.dumps(results, indent=1), encoding="utf-8")
    return 1 if changed or failed else 0


def cmd_bless(args):
    cases = pick(args)
    golden = env_path(args.golden, "XPECCY_GOLDEN", "golden")
    results = execute(args, cases)
    golden.mkdir(parents=True, exist_ok=True)
    bad = 0
    for s in sorted({c.suite for c in cases}):
        ref = load_golden(golden, s)
        for c in (c for c in cases if c.suite == s):
            r = results[c.key]
            if "error" in r:
                bad += 1
                print(f"FAIL     {c.key:28} {r['error']} - reference kept")
                continue
            if ref.get(c.name) != r:
                print(f"blessed  {c.key}")
            ref[c.name] = r
        golden_path(golden, s).write_text(json.dumps(dict(sorted(ref.items())), indent=1) + "\n", encoding="utf-8")
    return 1 if bad else 0


def cmd_list(args):
    for c in pick(args):
        print(f"{c.key:28} {c.machine:9} {c.how:4} {c.skip:>5}+{c.frames:<5} {c.file}")
    return 0


def cmd_quick(args):
    sys.path.insert(0, str(Path(__file__).resolve().parent / "core"))
    sys.dont_write_bytecode = True
    import quick
    return quick.run(args)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=("run", "bless", "list", "quick"))
    ap.add_argument("suite", nargs="*", help=f"suites in tests/suites (default: all)")
    ap.add_argument("-k", action="append", help="only cases whose name matches (glob, repeatable)")
    ap.add_argument("--app", help="staged build folder (default: newest dev build in build/dist)")
    ap.add_argument("--corpus", help="image folder (default: $XPECCY_CORPUS)")
    ap.add_argument("--golden", help="reference folder (default: $XPECCY_GOLDEN)")
    ap.add_argument("-j", "--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument("--timeout", type=int, default=300, help="seconds per case")
    ap.add_argument("--shots", help="write each case's last frame here as .ppm")
    ap.add_argument("--twice", action="store_true", help="run every case twice and flag any that differ")
    ap.add_argument("--json", help="write the raw results here")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--no-build", action="store_true", help="quick: use what build/out/tests holds")
    ap.add_argument("--bless", action="store_true", help="quick: take the current output as expected")
    ap.add_argument("--mingw", help="quick, Windows: MinGW folder (default: $XPECCY_MINGW, then the qt6-x64 one)")
    args = ap.parse_args()
    return {"run": cmd_run, "bless": cmd_bless, "list": cmd_list, "quick": cmd_quick}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
