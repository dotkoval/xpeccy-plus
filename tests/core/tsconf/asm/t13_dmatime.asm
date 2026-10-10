; DMA takes time: RAM->RAM, 16 bursts of 256 words = 8192 DRAM cycles, ZX mode.
; MBOX+1 = turns of the DMAStatus polling loop before it ends (16 bit), MBOX+3 = the
; DMA INT came (1). The loop is 34 T (68 dots at 3.5 MHz) and 5 DRAM cycles.
; CLK=0 (3.5 MHz): ~9000 dots, the loop taking a tenth of the cycles -> about 133 turns
; CLK=6 (14 MHz, cache on): the loop is served by the cache -> about 8200 dots, ~480 turns
	include "common.inc"
	ifndef CLK
CLK = 0
	endif
	org #8000
	di
	TSOUT SYSCONF, CLK
	TSOUT INTMASK, 4
	IM2_SETUP
	ld a, #83			; FB -> #8383
	ld (#BDFB), a
	ld (#BDFC), a
	xor a
	ld (MBOX+3), a
	TSOUT DMASAL, 0
	TSOUT DMASAH, 0
	TSOUT DMASAX, #10
	TSOUT DMADAL, 0
	TSOUT DMADAH, 0
	TSOUT DMADAX, #20
	TSOUT DMALEN, 255
	TSOUT DMANUM, 15
	ld hl, 0
	ld bc, DMACTR
	ld a, #01
	ei
	out (c), a
w:	inc hl
	in a, (c)
	rla
	jr c, w
	ld (MBOX+1), hl
	ld b, 0
w2:	djnz w2				; room for the INT
	FINISH

	ds #8383 - $
	ld a, 1
	ld (MBOX+3), a
	ei
	ret
