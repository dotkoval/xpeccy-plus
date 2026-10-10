; A port nothing decodes reads FF: the fpga drives FF on any unclaimed read
; (zbus.v drive_ff), there is no floating bus. Attributes set to #47, then #FF read
; 4096 times across a frame: the AND of all reads goes to the mailbox.
	org #8000
	include "common.inc"
	ld hl, #5800
	ld de, #5801
	ld bc, #2FF
	ld (hl), #47
	ldir
	ld e, #FF
	ld hl, 4096
.r:	in a, (#FF)
	and e
	ld e, a
	dec hl
	ld a, h
	or l
	jr nz, .r
	ld a, e
	ld (MBOX + 1), a
	FINISH
