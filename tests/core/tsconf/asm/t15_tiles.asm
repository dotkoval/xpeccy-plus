; Tile layer 0 from the tile map (video_ts.v). 360x288 window (ray = window coords), NOGFX,
; border #3F. Map page #30 row 2 col 3 = tile 1; tile 1 in graphics page #40 is colour 7.
; YOFS=0: probes 26,18 = 07 (row 2 is lines 16..23), 26,15 = 3F, 22,18 = 3F
; YOFS=4: the row moves up 4 lines: 26,13 = 07, 26,20 = 3F
; Row 0 col 5 = tile 1 too: its map is read in the frame before (the window starts at line 0),
; so 42,3 = 07 with YOFS=0 and 42,1 = 07 with YOFS=4
	include "common.inc"
	ifndef YOFS
YOFS = 0
	endif
	org #8000
	di
	TSOUT FMADDR, #1C
	ld hl, #8C63 : ld (#C000 + #07 * 2), hl
	ld hl, #8421 : ld (#C000 + #3F * 2), hl
	TSOUT FMADDR, 0
	TSOUT PAGE3, #30
	ld hl, #C000
	ld de, #C001
	ld bc, #3FFF
	ld (hl), 0
	ldir
	ld hl, 1
	ld (#C000 + 2 * 256 + 3 * 2), hl
	ld (#C000 + 5 * 2), hl
	TSOUT PAGE3, #40
	ld hl, #C004				; tile 1: bytes 4..7 of lines 0..7
	ld c, 8
tg:	ld b, 4
	push hl
tg1:	ld (hl), #77
	inc hl
	djnz tg1
	pop hl
	inc h
	dec c
	jr nz, tg
	TSOUT #16AF, #30			; TMPage
	TSOUT #17AF, #40			; T0GPage
	TSOUT #42AF, YOFS			; T0YOffsL
	TSOUT BORDER, #3F
	TSOUT VCONFIG, #E1			; 360x288, 16c, NOGFX
	TSOUT TSCONFIG, #20
	jr $
