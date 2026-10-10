; pentagon hardware multicolor (#EFF7 b5): pixels at #4000 in the zx layout, the
; attribute of each 8x1 cell at +#2000 of it (video_addrgen.v addr_phm, gctr[0]).
; Pixels all 0, attributes paper 2 (#10): the screen has to be paper 2, not 0.
	org #8000
	include "common.inc"
	ld hl, #4000
	ld de, #4001
	ld bc, #17ff
	ld (hl), 0
	ldir
	ld hl, #6000
	ld de, #6001
	ld bc, #17ff
	ld (hl), #10
	ldir
	OUT77 3
	BCOUT #EFF7, #34
	ld b, 3
.w:	ei
	halt
	djnz .w
	FINISH
