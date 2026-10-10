; Line INT in IM 1: RAM at #0000 (page 0, also seen at #C000), RST 38 counts.
; The frame INT is left off; frames are counted by the harness.
; MBOX+1 = INTs taken (stops at 3200 = 10 frames of lines). Expect ~10 frames.
	include "common.inc"
	org #8000
	di
	TSOUT SYSCONF, 2
	ld hl, isr
	ld de, #C038			; page 0 offset #38
	ld bc, isr_end - isr
	ldir
	TSOUT PAGE0, 0
	TSOUT MEMCONF, #0E		; RAM, no map, writable
	TSOUT INTMASK, 2		; line only
	im 1
	ld hl, 0
	ld (MBOX+1), hl
	ei
wait:	ld hl, (MBOX+1)
	ld de, 3200
	or a
	sbc hl, de
	jr c, wait
	FINISH

isr:	push hl
	ld hl, (MBOX+1)
	inc hl
	ld (MBOX+1), hl
	pop hl
	ei
	ret
isr_end:
