; ZiFi: the threshold and timeout interrupts, on TSConf's wait-port source.
; rs232_task() looks at the sources once a pass of the AVR's main loop and, while ISR is not
; zero, pulses the FPGA's wait-port line every pass (rs232.c:619-664); zint.v latches it with
; INTMask b3 and hands the vector #F9. A source clears its IMR bit as it fires; reading ISR
; clears it. The timeout counts whole ms from the last byte received.
; INTMask = 8 alone, ZIBTR = 4, IMR = 1; the module sends 4 bytes at 3000 us (ZFEED). The
; fourth is in the ring 3 * 86.8 + 82 us later, at 3343 us:
;  - the first interrupt comes on the next pass, 3343..3363 us. Its handler leaves ISR alone,
;    so the line is pulsed again and a second one follows
;  - from then on the handler reads ISR. 01 (the threshold): it sets ZITOR = 2 and IMR = 2,
;    the timeout of what is in the ring, which fires 2 ms after the last byte, 5343..5363 us,
;    and reads 02. Nothing is armed again: the 2 bytes at 8000 us bring no interrupt
;  +1 = the interrupts in all     +2 = those that read ISR as 00
;  +3, +4 = the ISR values that were not 00, in order: 01 02     +5 = ZIFR at the end: 06
; An interrupt that reads 00 is the line pulsed once more between the acknowledge and the
; read of ISR, which itself waits for the AVR. How many there are (+1, +2) follows from the
; length of the AVR's pass, taken as 20 us and never measured; the rest does not.
; ZINT (the bench): when the line was latched, us.
	include "common.inc"
	include "zifi.inc"
	org #8000
	di
	TSOUT INTMASK, 8
	IM2_SETUP
	ld hl, wtp
	ld (#BDF9), hl
	ZOUT ZF_CR, ZF_API1
	ZOUT ZF_IBTR, 4
	ZOUT ZF_IMR, 1
	ld hl, 0
	ei
loop:	inc hl				; 29 T a turn: 3072 turns are 25 ms
	ld a, h
	cp 12
	jr nz, loop
	di
	ld bc, ZF_IFR
	in a, (c)
	ld (MBOX + 5), a
	FINISH

wtp:	push af
	push bc
	push hl
	ld a, (MBOX + 1)
	inc a
	ld (MBOX + 1), a
	cp 1
	jr z, w9			; the first: ISR stays as it is
	ld bc, ZF_IMR
	in a, (c)
	or a
	jr nz, w2
	ld hl, MBOX + 2
	inc (hl)
	jr w9
w2:	ld hl, (slot)
	ld (hl), a
	inc hl
	ld (slot), hl
	cp 1
	jr nz, w9
	ld bc, ZF_ITOR
	ld a, 2
	out (c), a
	ld bc, ZF_IMR
	out (c), a
w9:	pop hl
	pop bc
	pop af
	ei
	ret

slot:	dw MBOX + 3

	ds #8181 - $
	ei
	ret
