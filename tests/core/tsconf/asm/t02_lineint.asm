; Frame + line INT together, IM 2, each vector to its own handler.
; MBOX+1 = frames (stops at 50), MBOX+3 = line INTs, MBOX+5 = DMA INTs (none).
; Expect lines = 50 * 320 = 16000 (#3E80), give or take one frame's worth.
	include "common.inc"
	ifndef CLK
CLK = 0
	endif
	org #8000
	di
	TSOUT SYSCONF, CLK
	TSOUT INTMASK, 3
	IM2_SETUP
	ld a, #82			; FD -> #8282
	ld (#BDFD), a
	ld (#BDFE), a
	ld a, #83			; FB -> #8383
	ld (#BDFB), a
	ld (#BDFC), a
	ld hl, 0
	ld (MBOX+1), hl
	ld (MBOX+3), hl
	ld (MBOX+5), hl
	ei
wait:	ld a, (MBOX+1)
	cp 50
	jr c, wait
	FINISH

	ds #8181 - $
	push hl
	ld hl, (MBOX+1)
	inc hl
	ld (MBOX+1), hl
	pop hl
	ei
	ret

	ds #8282 - $
	push hl
	ld hl, (MBOX+3)
	inc hl
	ld (MBOX+3), hl
	pop hl
	ei
	ret

	ds #8383 - $
	push hl
	ld hl, (MBOX+5)
	inc hl
	ld (MBOX+5), hl
	pop hl
	ei
	ret
