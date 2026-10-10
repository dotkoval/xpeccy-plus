; Layer mixing (video_render.v). 256c 256x192, bitmap row 12: dots 0..7 colour #00,
; 8..15 colour #21. Sprite 0 (pal 1, colour 5 -> #15) covers dots 4..19 of lines 10..17.
; Border #3F. Unique CRAM colours for #00 #21 #15 #3F. Probes on gfx line 12 (ray y = 60):
;   ray x 54 (bitmap #00), 58 (#00 + sprite), 62 (#21 + sprite), 66 (#21 + sprite)
; MODE 0 plain  : 00 15 15 15   (the sprite spans dots 4..19, so it is under 66 too)
; MODE 1 GFXOVR : 3F 15 21 21   (bitmap on top where not 0, border where both are holes)
; MODE 2 NOTSU  : 00 00 21 21
; MODE 3 TSConfig b0: tiles/sprites over 360x288 - sprite 0 then sits at 4,10 of the whole
;   visible area, in the border: probe ray 12,12 = 15, and 40,12 = 3F
; MODE 4 NOGFX  : 3F 15 15 15
; Border through #FE: PalSel = #03, OUT (#FE),5 -> BRD 35 (MODE 5)
	include "common.inc"
	ifndef MODE
MODE = 0
	endif
	org #8000
	di
	; colours, as words through FMAPS at #C000
	TSOUT FMADDR, #1C
	ld hl, #8011 : ld (#C000 + #00 * 2), hl
	ld hl, #8220 : ld (#C000 + #21 * 2), hl
	ld hl, #C400 : ld (#C000 + #15 * 2), hl
	ld hl, #8421 : ld (#C000 + #3F * 2), hl
	; sprites 0 and 1
	ld hl, spr
	ld de, #C200
	ld bc, 6
	ldir
	TSOUT FMADDR, 0
	; sprite graphics: page #20, tile 0 and 1, lines 0..7, colour 5
	TSOUT PAGE3, #20
	ld hl, #C000
	ld c, 8
sg:	ld b, 8
	push hl
sg1:	ld (hl), #55
	inc hl
	djnz sg1
	pop hl
	inc h
	dec c
	jr nz, sg
	; bitmap: page #10, rows 0..31, dots 8..15 = #21
	TSOUT PAGE3, #10
	ld hl, #C008
	ld c, 32
bm:	push hl
	ld b, 8
bm1:	ld (hl), #21
	inc hl
	djnz bm1
	pop hl
	inc h
	inc h
	dec c
	jr nz, bm
	TSOUT VPAGE, #10
	TSOUT SGPAGE, #20
	TSOUT BORDER, #3F
	if MODE == 1
	TSOUT VCONFIG, #0A
	elseif MODE == 2
	TSOUT VCONFIG, #12
	elseif MODE == 4
	TSOUT VCONFIG, #22
	else
	TSOUT VCONFIG, #02
	endif
	if MODE == 3
	TSOUT TSCONFIG, #81
	else
	TSOUT TSCONFIG, #80
	endif
	if MODE == 5
	TSOUT PALSEL, #03
	ld b, 0
dl:	djnz dl				; a line or two, for PalSel to be latched
	ld a, 5
	out (#FE), a
	endif
	jr $

	; y, ACT|ys, x, xs, tnum, pal<<4
spr:	db 10, #20, 4, #02, 0, #10
