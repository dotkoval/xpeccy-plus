; FDDVirt b7 opens the disk ports outside DOS (zports.v: open_vg).
; MBOX+1 = track register written and read through #3F with b7 set (expect 5A)
; MBOX+2 = the same with b7 clear (expect FF: nothing answers)
	include "common.inc"
	org #8000
	di
	TSOUT FDDVIRT, #80
	ld a, #5A
	out (#3F), a
	in a, (#3F)
	ld (MBOX+1), a
	TSOUT FDDVIRT, #00
	ld a, #33
	out (#3F), a
	in a, (#3F)
	ld (MBOX+2), a
	FINISH
