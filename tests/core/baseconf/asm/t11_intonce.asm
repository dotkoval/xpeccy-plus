; The frame INT is taken once a frame even by a handler that re-enables at once:
; the acknowledge ends the pulse (zint.v intend = ... || iorq & m1), and the pulse
; is 32T at 3.5 MHz, 64T at 7. The handler is inc c / ei / ret, ~40T from the INT,
; run at 7 MHz so it re-enables inside the pulse.
; Mailbox: INTs counted over 10 frames (HALTs).
	org #8000
	include "common.inc"
	ld hl, #A000
	ld de, #A001
	ld bc, 256
	ld (hl), #A2
	ldir
	ld a, #A2
	ld (#A2A2), a		; handler at #A2A2: inc c / ei / ret
	ld hl, #A2A2
	ld (hl), #0C
	inc hl
	ld (hl), #FB
	inc hl
	ld (hl), #C9
	ld a, #A0
	ld i, a
	im 2
	BCOUT #EFF7, #04	; 7 MHz
	ei
	halt			; line up on a frame
	ld c, 0
	ld b, 10
.w:	halt
	djnz .w
	di
	ld a, c
	ld (MBOX + 1), a
	FINISH
