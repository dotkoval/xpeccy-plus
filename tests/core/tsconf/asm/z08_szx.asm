; ZiFi in a snapshot. The module sends 10 bytes, 1..10, at 2000 us (ZFEED). The program
; turns the API on, reads ZIFR (the data register is ZiFi's), waits to about 6 ms, then:
;  +1 = ZIFR 0A    +2 = the sum of 4 bytes read: 0A    +3 = ZIFR after them: 06
; and waits on to about 13 ms. The bench makes an SZX at 4000 us, before the reads, and
; loads it at 9000 us, after them (ZSZX): the machine is back at 4 ms and reads again. With
; ZiFi in the file the ring holds its 10 bytes once more and the mailbox is the same;
; without, the second pass would find 6.
; ZSZX (the bench): the clock ZiFi's times count by goes on from the file's, and ZiFi's
; state is byte for byte what it was when the file was made.
	include "common.inc"
	include "zifi.inc"
	org #8000
	di
	ZOUT ZF_CR, ZF_API1
	ld bc, ZF_IFR
	in a, (c)
	ld bc, 800			; 800 * 26 T = 5.9 ms
w1:	dec bc
	ld a, b
	or c
	jr nz, w1
	ld hl, MBOX + 1
	ZIN ZF_IFR
	ld d, 4
	ld e, 0
r1:	ld bc, ZF_DR
	in a, (c)
	add a, e
	ld e, a
	dec d
	jr nz, r1
	ld (hl), e
	inc hl
	ZIN ZF_IFR
	ld bc, 900			; 6.7 ms more
w2:	dec bc
	ld a, b
	or c
	jr nz, w2
	FINISH
