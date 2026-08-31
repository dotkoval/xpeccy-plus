#!/usr/bin/env python3
# Copyright (c) 2026 Oleksandr Kovalchuk. GNU GPL v2 or later - see LICENSE.
# A development tool, not part of the emulator; see coretest.c for where it
# comes from.

"""Compare coretest output with fuse's tests.expected.

Splits the failures in two, because they mean different things:

  timing  - a bus cycle happened on the wrong tstate, in the wrong order, or the
            instruction took the wrong number of ticks. This is what the ULA sees.
  state   - registers, flags, MEMPTR or memory came out wrong. Same timing.

Two differences are expected and filtered out:

  * fuse's harness prints PC (port contend) lines we cannot match - port
    contention is machine-side here, not in the cpu core, so we emit none.
  * on a conditional jump that is not taken fuse contends the displacement read
    but throws the byte away, so it prints MC without the matching MR. The bus
    cycle is the same; only the printout differs.

The state failures are core bugs we know about and have not fixed yet. They are
listed in known-failures.txt so a new one stands out: the exit code is 0 while
the state failures are exactly the ones on that list, and 1 as soon as a timing
failure turns up or that list changes either way.

Usage: compare.py <our output> <tests.expected> [known-failures.txt]
"""

import os
import re
import sys
from itertools import zip_longest

EVENT = re.compile(r"\s*(\d+) (MC|MR|MW|PR|PW|PC) ([0-9a-f]{4})(?: ([0-9a-f]{2}))?$")


def load(path, drop_pc=False):
	"""Parse one side into [{name, ev, tail}]. drop_pc is for fuse's output."""
	tests = []
	cur = None
	with open(path, encoding="latin-1") as f:
		for raw in f:
			ln = raw.strip()
			if not ln:
				continue
			m = EVENT.match(raw.rstrip())
			if m:
				if not (drop_pc and m.group(2) == "PC"):
					cur["ev"].append((int(m.group(1)), m.group(2),
							m.group(3), m.group(4)))
			elif " " in ln:			# register, state or memory line
				cur["tail"].append(ln)
			else:
				cur = {"name": ln, "ev": [], "tail": []}
				tests.append(cur)
	return tests


def drop_discarded_reads(ours, theirs):
	"""Drop our MR lines that fuse suppressed because it discarded the byte."""
	seen = set(theirs)
	return [e for e in ours if not (
		e[1] == "MR" and e not in seen and (e[0] - 3, "MC", e[2], None) in seen)]


def load_baseline(path):
	if not os.path.exists(path):
		return None
	with open(path, encoding="latin-1") as f:
		return {ln.strip() for ln in f if ln.strip() and not ln.startswith("#")}


def main():
	if not 3 <= len(sys.argv) <= 4:
		sys.exit(__doc__)
	here = os.path.dirname(os.path.abspath(__file__))
	got = {t["name"]: t for t in load(sys.argv[1])}
	exp = load(sys.argv[2], drop_pc=True)
	baseline = load_baseline(sys.argv[3] if len(sys.argv) > 3
				else os.path.join(here, "known-failures.txt"))

	bad_time = []
	bad_state = []
	missing = []
	for t in exp:
		g = got.get(t["name"])
		if g is None:
			missing.append(t["name"])
			continue
		ev = drop_discarded_reads(g["ev"], t["ev"])
		# the second tail line ends with the final tstate count
		if ev != t["ev"] or g["tail"][1].split()[-1] != t["tail"][1].split()[-1]:
			bad_time.append((t, g, ev))
		elif g["tail"] != t["tail"]:
			bad_state.append((t, g))

	print("%d tests, %d timing failures, %d state failures" %
		(len(exp), len(bad_time), len(bad_state)))
	if missing:
		print("missing from our output: %s" % " ".join(missing))

	if bad_time:
		print("\n--- timing")
		for t, g, ev in bad_time:
			print("%s:" % t["name"])
			for a, b in zip_longest(t["ev"], ev):
				if a != b:
					print("   fuse %-28s xpeccy %s" % (a, b))
			if t["tail"][1] != g["tail"][1]:
				print("   fuse %-28s xpeccy %s" % (t["tail"][1], g["tail"][1]))

	if bad_state:
		print("\n--- state")
		for t, g in bad_state:
			print("%s:" % t["name"])
			for a, b in zip(t["tail"], g["tail"]):
				if a != b:
					print("   fuse   %s\n   xpeccy %s" % (a, b))

	if baseline is None:
		return 1 if (bad_time or bad_state or missing) else 0
	names = {t["name"] for t, _ in bad_state}
	fixed = sorted(baseline - names)
	new = sorted(names - baseline)
	if fixed:
		print("\nfixed since the baseline, drop from known-failures.txt: %s" % " ".join(fixed))
	if new:
		print("\nNEW state failures: %s" % " ".join(new))
	return 1 if (bad_time or missing or fixed or new) else 0


if __name__ == "__main__":
	sys.exit(main())
