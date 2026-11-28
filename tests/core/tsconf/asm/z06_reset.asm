; ZiFi: the Z80's reset is not the AVR's. rs232_init() runs when the AVR starts (zx_init),
; nothing in the firmware watches the Z80's reset line: the API mode, the selector and the
; thresholds stay.
; The first pass turns the API on, reads ZIFR (the data register is ZiFi's) and sets
; ZIBTR = #33; the bench resets the machine at frame 2 (ZRESET) and the program starts again:
;  +1 = ER 00 (the API still on; off it would read FF)   +2 = ZIBTR 33
;  and 'Z' written to DR goes to the module - ZTX: 1 byte, 5A
	include "common.inc"
	include "zifi.inc"
	org #8000
	di
	ld hl, (flag)
	ld de, #3CC3
	or a
	sbc hl, de
	jr z, second
	ld (flag), de
	ZOUT ZF_CR, ZF_API1
	ld bc, ZF_IFR
	in a, (c)
	ZOUT ZF_IBTR, #33
	jr $

second:	ld hl, MBOX + 1
	ZIN ZF_CR
	ZIN ZF_IBTR
	ZOUT ZF_DR, 'Z'
	ld bc, 200
w:	dec bc
	ld a, b
	or c
	jr nz, w
	FINISH

flag:	dw 0
