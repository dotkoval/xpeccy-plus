; ZiFi: which device the data register serves, and the API switch on it.
; rs232.c: select_zf starts at 0 and is set by a read of ZIFR or ZOFR, cleared by one of
; RIFR or ROFR; a write to DR goes to ZiFi's ring only with select_zf and the API in mode 1.
;  +1  LSR (#FDEF) at the start: 20 - room in RS-232's ring, nothing sent on it yet
;      'A' to DR before any FIFO register was read: RS-232's
;      ZOFR read, 'B': ZiFi's       ROFR read, 'C': RS-232's      ZIFR read, 'D': ZiFi's
;      API off (#F0), 'E': dropped  API on again, 'F': ZiFi's (the selector kept)
;  +2  LSR 2 ms on: 60 - a byte has gone out of USART1 (TXC1, which the firmware never clears)
; ZTX: 3 bytes, 42 44 46.
	include "common.inc"
	include "zifi.inc"
	org #8000
	di
	ZOUT ZF_CR, ZF_API1
	ld bc, #FDEF
	in a, (c)
	ld (MBOX + 1), a
	ZOUT ZF_DR, 'A'
	ld bc, ZF_OFR
	in a, (c)
	ZOUT ZF_DR, 'B'
	ld bc, RS_OFR
	in a, (c)
	ZOUT ZF_DR, 'C'
	ld bc, ZF_IFR
	in a, (c)
	ZOUT ZF_DR, 'D'
	ZOUT ZF_CR, #F0
	ZOUT ZF_DR, 'E'
	ZOUT ZF_CR, ZF_API1
	ZOUT ZF_DR, 'F'
	ld bc, 500			; 500 * 26 T = 3.7 ms
w:	dec bc
	ld a, b
	or c
	jr nz, w
	ld bc, #FDEF
	in a, (c)
	ld (MBOX + 2), a
	FINISH
