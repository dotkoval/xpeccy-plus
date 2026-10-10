; Loaded as an SPG 1.0 (mkspg.py wraps it). The loader must leave BASIC 48 at #0000
; and IY = #5C3A. MBOX+1..8 = bytes at #0000 (48K rom starts F3 AF 11 FF FF C3 CB 11),
; MBOX+9 = IY.
	include "common.inc"
	org #8000
	ld hl, 0
	ld de, MBOX+1
	ld bc, 8
	ldir
	ld (MBOX+9), iy
	FINISH
