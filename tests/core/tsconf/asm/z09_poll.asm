; ZiFi: the interrupt sources polled with the wait-port interrupt masked (INTMask b3 off).
; The AVR knows nothing of the mask: ISR fills all the same, whenever it is read.
; ZIBTR = 4, ZITOR = 2, IMR = 3; the module sends 4 bytes at 3000 us and one more at 6500 (ZFEED):
;  - the threshold is met at 3343 us and the timeout 2 ms later. The fifth byte starts the
;    timeout's count again, but what was up stays up: ISR read at 7 ms is 03, read again 00
;  - IMR = 2 once more: up 2 ms after the fifth byte, ISR read at 10 ms is 02
;  +1 = interrupts taken: 00     +2..+4 = ISR: 03 00 02     +5 = ZIFR at the end: 05
	include "common.inc"
	include "zifi.inc"
	org #8000
	di
	TSOUT INTMASK, 0
	IM2_SETUP
	TSOUT ZF_CR, ZF_API1
	TSOUT ZF_IBTR, 4
	TSOUT ZF_ITOR, 2
	TSOUT ZF_IMR, 3
	ei
	ZDELAY 750			; 7.2 ms from the start, IM2_SETUP's 1.6 in them
	ld hl, MBOX + 2
	ZIN ZF_IMR
	ZIN ZF_IMR
	TSOUT ZF_IMR, 2
	ZDELAY 400			; 3 ms more
	ZIN ZF_IMR
	ZIN ZF_IFR
	FINISH

	ds #8181 - $
	push af
	ld a, (MBOX + 1)
	inc a
	ld (MBOX + 1), a
	pop af
	ei
	ret
