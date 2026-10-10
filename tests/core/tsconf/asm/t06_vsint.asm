; VSINTH: bit 0 is VSINT[8], bits 7:4 move the INT line on after every frame INT.
; Run for 10 frames (the harness stops it). MBOX+1 = frame INTs.
; VSH = #02 (junk bit, no step): expect 10.  VSH = #80 (step 8): expect 40 a frame = 400.
	include "common.inc"
	ifndef VSH
VSH = #80
	endif
	org #8000
	di
	TSOUT SYSCONF, 2
	TSOUT INTMASK, 1
	TSOUT VSINTL, 0
	TSOUT VSINTH, VSH
	IM2_SETUP
	ld hl, 0
	ld (MBOX+1), hl
	ei
	jr $

	ds #8181 - $
	push hl
	ld hl, (MBOX+1)
	inc hl
	ld (MBOX+1), hl
	pop hl
	ei
	ret
