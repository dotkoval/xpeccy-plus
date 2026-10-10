; DMA semantics (dma.v). Results in the mailbox, CRAM printed by the harness.
; MBOX+1..8   : ram->cram, 2 bursts of 1 word then 2 more without reloading addresses:
;               CRAM 0..7 must read 11 22 33 44 55 66 77 88 (harness line CRAM)
; MBOX+1..16  : dst block after an aligned (256) copy of 16 words from offset #F0:
;               bytes #F0..#FF and #00..#0F of page #11 - expect 01..10 then 11..20,
;               copied here as #F0..#FF first (16 bytes) then #00..#0F (16 bytes) at MBOX+#11
; MBOX+#31..  : fill, 2 bursts of 2 words, dalgn 256: page #12 offsets 0..3 and #100..#103 = A5 5A
; MBOX+#41..  : copy from page #FF offset #3FF8 for 8 words wraps into page 0 offset 0
	include "common.inc"
	org #8000
	di
	; --- source data in page #10 (via window 3)
	TSOUT PAGE3, #10
	ld hl, #C000
	ld b, 64
	ld a, 1
fill1:	ld (hl), a
	inc hl
	inc a
	djnz fill1
	ld hl, crsrc			; cram source at #10:#0100
	ld de, #C100
	ld bc, 8
	ldir

	; --- ram->cram: 2 bursts of 1 word, then again without new addresses
	TSOUT DMASAL, #00
	TSOUT DMASAH, #01
	TSOUT DMASAX, #10
	TSOUT DMADAL, 0
	TSOUT DMADAH, 0
	TSOUT DMADAX, 0
	TSOUT DMALEN, 0
	TSOUT DMANUM, 1
	TSOUT DMACTR, #84
	DMA_WAIT
	TSOUT DMACTR, #84
	DMA_WAIT

	; --- aligned copy: 16 words from #10:#0000 to #11:#00F0, D_ALGN 256
	TSOUT DMASAL, 0
	TSOUT DMASAH, 0
	TSOUT DMASAX, #10
	TSOUT DMADAL, #F0
	TSOUT DMADAH, 0
	TSOUT DMADAX, #11
	TSOUT DMALEN, 15
	TSOUT DMANUM, 0
	TSOUT DMACTR, #11
	DMA_WAIT
	TSOUT PAGE3, #11
	ld hl, #C0F0
	ld de, MBOX+1
	ld bc, 16
	ldir
	ld hl, #C000
	ld bc, 16
	ldir

	; --- fill: word A5 5A, 2 bursts of 2 words, D_ALGN 256, into page #12
	TSOUT PAGE3, #10
	ld hl, #5AA5
	ld (#C200), hl
	TSOUT DMASAL, 0
	TSOUT DMASAH, 2
	TSOUT DMASAX, #10
	TSOUT DMADAL, 0
	TSOUT DMADAH, 0
	TSOUT DMADAX, #12
	TSOUT DMALEN, 1
	TSOUT DMANUM, 1
	TSOUT DMACTR, #14
	DMA_WAIT
	TSOUT PAGE3, #12
	ld hl, #C000
	ld de, MBOX+#31
	ld bc, 4
	ldir
	ld hl, #C100
	ld bc, 4
	ldir

	; --- top of memory: page #FF offset #3FF8, 8 words, dst page #13
	TSOUT PAGE3, #FF
	ld hl, #FFF8
	ld b, 8
	ld a, #C1
t1:	ld (hl), a
	inc hl
	inc a
	djnz t1
	TSOUT PAGE3, 0
	ld hl, #C000
	ld b, 8
	ld a, #D1
t2:	ld (hl), a
	inc hl
	inc a
	djnz t2
	TSOUT DMASAL, #F8
	TSOUT DMASAH, #3F
	TSOUT DMASAX, #FF
	TSOUT DMADAL, 0
	TSOUT DMADAH, 0
	TSOUT DMADAX, #13
	TSOUT DMALEN, 7
	TSOUT DMANUM, 0
	TSOUT DMACTR, #01
	DMA_WAIT
	TSOUT PAGE3, #13
	ld hl, #C000
	ld de, MBOX+#41
	ld bc, 16
	ldir
	FINISH

crsrc:	db #11, #22, #33, #44, #55, #66, #77, #88
