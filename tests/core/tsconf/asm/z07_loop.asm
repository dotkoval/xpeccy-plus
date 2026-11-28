; ZiFi: both ways at the UART's pace. The bench sends every byte back as its frame ends
; (ZLOOP). 300 bytes, 1, 2, 3..., are written as ZOFR shows room and read as ZIFR shows
; them; then two frames for the last to come back.
;  +1,+2 = how many came back: 012C (300)    +3 = their sum: 5E
; RATE=1 instead reads what a module sending at 74880 (the ESP's boot ROM log) leaves in the
; ring: the bench hands "ready" over at that rate (ZFEED, ZFAR), and the AVR's receiver at
; 115200 samples other bits than were sent.
;  +1 = bytes in the ring, +2.. the first six. Not "ready": 72 65 61 64 79 does not appear.
;  (What exactly is the receiver model's own; no document says.)
	include "common.inc"
	include "zifi.inc"
	ifndef RATE
RATE = 0
	endif
	org #8000
	di
	TSOUT INTMASK, 1
	IM2_SETUP
	TSOUT ZF_CR, ZF_API1
	if RATE
	ei
	halt
	halt
	di
	ld hl, MBOX + 1
	ld bc, ZF_IFR
	in a, (c)
	ld (hl), a
	inc hl
	ld d, 6
r1:	ld bc, ZF_DR
	in a, (c)
	ld (hl), a
	inc hl
	dec d
	jr nz, r1
	FINISH
	else
	ld ix, 0			; came back
	ld e, 0				; their sum
	ld hl, 1			; the next to send
send:	call drain
	ld bc, ZF_OFR
	in a, (c)
	or a
	jr z, send
	ld bc, ZF_DR
	out (c), l
	inc hl
	ld a, h
	cp 1
	jr nz, send
	ld a, l
	cp #2D				; 301
	jr nz, send
	ei
	halt
	halt
	di
	call drain
	ld (MBOX + 1), ix
	ld a, e
	ld (MBOX + 3), a
	FINISH

drain:	ld bc, ZF_IFR
	in a, (c)
	or a
	ret z
	ld bc, ZF_DR
	in a, (c)
	add a, e
	ld e, a
	inc ix
	jr drain
	endif

	IM2_STUB
