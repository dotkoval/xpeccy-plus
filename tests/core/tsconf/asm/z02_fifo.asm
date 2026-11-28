; ZiFi: the input FIFO holds 511 bytes, its count stops at 191, INIR with the count in B
; reads that many; the output FIFO and Clear FIFO.
; The module sends 600 bytes, 1, 2, 3... (ZFEED=2000:+600): 52 ms at 115200. After 100 ms:
;  +1  ZIFR BF: 191, though 511 are held (rs232.c: min(count, ZF_DR_REG_LIM))
;  +2  the sum of 191 bytes read by ld bc,#BFEF : inir (the port's high byte runs #BF..#01,
;      all of it the data register): 1..191 -> A0
;  +3  ZIFR BF again (320 left)       +4  the next 191: 192..382 -> 21
;  +5  ZIFR 81: 129 left, so 511 were kept and the last 89 dropped (the ring is 512 long)
;  +6  those 129: 383..511 -> 3F      +7  ZIFR 00      +8  DR with nothing in it: FF
;  +9  ZOFR BF: the output ring empty
; Then 200 bytes are written to DR as fast as the port takes them, one every 98 T (28 us:
; 70 of them WAIT), while one leaves for the module every 95.5 us (11 bits). The 200 take
; 5.6 ms, in which 58 or 59 go out:
;  +10 ZOFR right after: 255 less the 140 or so still in the ring -> about 73
;      Clear FIFO (out)
;  +11 ZOFR BF: the ring is empty again
; ZTX counts what reached the module: about 60 - what had gone out, with the byte in the
; data register and the one in the shifter, which a clear does not take back.
	include "common.inc"
	include "zifi.inc"
	org #8000
	di
	TSOUT INTMASK, 1
	IM2_SETUP
	ZOUT ZF_CR, ZF_API1
	ei
	ld b, 5
w1:	halt
	djnz w1
	di
	ld ix, MBOX + 1
	call count
	ld b, #BF
	call take
	call count
	ld b, #BF
	call take
	call count
	ld b, a
	call take
	call count
	ld bc, ZF_DR
	in a, (c)
	ld (ix), a
	inc ix
	ld bc, ZF_OFR
	in a, (c)
	ld (ix), a
	inc ix
	ld bc, ZF_DR
	ld d, 200
w2:	out (c), d
	dec d
	jr nz, w2
	ld bc, ZF_OFR
	in a, (c)
	ld (ix), a
	inc ix
	ZOUT ZF_CR, #02
	ld bc, ZF_OFR
	in a, (c)
	ld (ix), a
	FINISH

; ZIFR into the mailbox and A
count:	ld bc, ZF_IFR
	in a, (c)
	ld (ix), a
	inc ix
	ret

; B bytes by INIR, their sum into the mailbox
take:	ld hl, buf
	ld e, b
	ld c, #EF
	inir
	ld hl, buf
	ld b, e
	xor a
t1:	add a, (hl)
	inc hl
	djnz t1
	ld (ix), a
	inc ix
	ret

	ds #8181 - $
	ei
	ret

buf:	ds 192
