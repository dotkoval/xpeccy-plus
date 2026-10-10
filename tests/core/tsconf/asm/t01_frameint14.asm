; Frame INT taken once per frame at 14 MHz with a short ISR.
; Result: MBOX+1 = ISRs counted (stops at 100), harness prints the frames it took.
; Expect ~100 frames. Fewer frames means the frame INT was taken more than once.
	include "common.inc"
	org #8000
	di
	TSOUT SYSCONF, 2		; 14 MHz
	TSOUT INTMASK, 1		; frame only
	IM2_SETUP
	ld hl, 0
	ld (MBOX+1), hl
	ei
wait:	ld hl, (MBOX+1)
	ld a, l
	cp 100
	jr c, wait
	FINISH

	ds #8181 - $
handler:
	push hl
	ld hl, (MBOX+1)
	inc hl
	ld (MBOX+1), hl
	pop hl
	ei
	ret
