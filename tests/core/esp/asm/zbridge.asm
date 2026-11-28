; esp-bench guest for scripts on a ZX Evo (TSConf): a bridge between the bench and ZiFi,
; the AVR's FIFOs at #xxEF (MACHINE=zifi). Loaded at #8000 of RAM page 2.
; The bench queues work in a ring at #9100 (64 entries of 4 bytes: type, a, b, 0; it writes
; #9000, we write #9001):
;   0 a      a byte to the ESP
;   3        the run ends: #9004 := 1
; (the Next bridge's other types mean nothing here and are passed over)
; Every byte the ESP sends goes into a ring of 256 at #A000 (we write #9002, the bench
; #9003): the bench takes them from there.

ZF_DR	equ #BFEF
ZF_IFR	equ #C0EF
ZF_OFR	equ #C1EF
ZF_CR	equ #C7EF
WR	equ #9000
RD	equ #9001
OWR	equ #9002
ORD	equ #9003
FIN	equ #9004
RING	equ #9100
OBUF	equ #A000

	org #8000
	di
	ld sp, #BF00
	ld bc, #20AF			; 14 MHz, the cache on
	ld a, 6
	out (c), a
	xor a
	ld (RD), a
	ld (OWR), a
	ld (FIN), a
	ld bc, ZF_CR
	ld a, #F1			; the API on
	out (c), a

loop:	ld bc, ZF_IFR			; what the ESP sent comes first; the read makes DR ZiFi's
	in a, (c)
	or a
	jr z, .work
	ld a, (OWR)			; room in the bench's ring?
	inc a
	ld hl, ORD
	cp (hl)
	jr z, .work
	ld bc, ZF_DR
	in a, (c)
	ld hl, (OWR)
	ld h, high OBUF
	ld (hl), a
	ld a, l
	inc a
	ld (OWR), a
	jr loop
.work	ld a, (RD)
	ld hl, WR
	cp (hl)
	jr z, loop
	ld l, a
	ld h, 0
	add hl, hl
	add hl, hl
	ld de, RING
	add hl, de
	ld a, (hl)
	inc hl
	ld d, (hl)
	or a
	jr z, .tx
	cp 3
	jr nz, .next
	ld a, 1
	ld (FIN), a
	jr $
.tx	ld bc, ZF_OFR
.txw	in a, (c)
	or a
	jr z, .txw
	ld bc, ZF_DR
	out (c), d
.next	ld a, (RD)
	inc a
	and 63
	ld (RD), a
	jr loop
