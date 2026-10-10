; DMA address registers are the working addresses (dma.v).
; A 4096-word copy from page #10 to page #20 is started, and DMADAddrX is set to #30 at once:
; the rest of the copy goes to page #30. Then a one-word copy from page #11 with only
; DMADAddrL/H written must land in page #30, not back in page #20.
; MBOX+1 = page #20 offset 0 (the first words went there: 01)
; MBOX+2 = page #20 offset #1FFE (never reached: 00)
; MBOX+3 = page #30 offset 0 (the second copy: 77)
	include "common.inc"
	org #8000
	di
	TSOUT PAGE3, #10
	ld hl, #C000
	ld (hl), 1
	ld de, #C001
	ld bc, #3FFF
	ldir
	TSOUT PAGE3, #11
	ld a, #77
	ld (#C000), a
	TSOUT DMASAL, 0
	TSOUT DMASAH, 0
	TSOUT DMASAX, #10
	TSOUT DMADAL, 0
	TSOUT DMADAH, 0
	TSOUT DMADAX, #20
	TSOUT DMALEN, 255
	TSOUT DMANUM, 15
	TSOUT DMACTR, #01
	TSOUT DMADAX, #30		; while it runs
	DMA_WAIT
	TSOUT DMASAL, 0
	TSOUT DMASAH, 0
	TSOUT DMASAX, #11
	TSOUT DMADAL, 0
	TSOUT DMADAH, 0
	TSOUT DMALEN, 0
	TSOUT DMANUM, 0
	TSOUT DMACTR, #01
	DMA_WAIT
	TSOUT PAGE3, #20
	ld a, (#C000)
	ld (MBOX+1), a
	ld a, (#DFFE)
	ld (MBOX+2), a
	TSOUT PAGE3, #30
	ld a, (#C000)
	ld (MBOX+3), a
	FINISH
