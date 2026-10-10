; Text mode (video_render.v tx_pix, vplex_out; video_out.v vdata). 256x192 text, PalSel #02,
; border #3F, char 1 across row 0 with attr #A5 and font line #B0 (text pixels 0, 2, 3 ink).
; Sprite 0 (pal 1, colour 6 -> #16) over dots 0..15 of lines 0..7. In hires only the low
; nibble of the mix goes out, under PalSel: ink #25, paper #2A, sprite #26, border #2F.
; A text pixel is half a dot and is mixed on its own. Probes in half dots (PIXH), ray y 48:
; MODE 0 plain : 104..107 = 26 (sprite over the text), 136 = 25, 137 = 2A, border 40,40 = 2F
; MODE 1 GFXOVR: 104 = 25, 105 = 26, 106 = 25, 107 = 25, 108 = 26, 136 = 25, 137 = 2F
	include "common.inc"
	ifndef MODE
MODE = 0
	endif
	org #8000
	di
	TSOUT FMADDR, #1C
	ld hl, #0401 : ld (#C000 + #25 * 2), hl
	ld hl, #0822 : ld (#C000 + #26 * 2), hl
	ld hl, #1043 : ld (#C000 + #2A * 2), hl
	ld hl, #2064 : ld (#C000 + #2F * 2), hl
	ld hl, spr
	ld de, #C200
	ld bc, 6
	ldir
	TSOUT FMADDR, 0
	TSOUT PAGE3, #20			; sprite graphics: tiles 0 and 1, lines 0..7, colour 6
	ld hl, #C000
	ld c, 8
sg:	ld b, 8
	push hl
sg1:	ld (hl), #66
	inc hl
	djnz sg1
	pop hl
	inc h
	dec c
	jr nz, sg
	TSOUT PAGE3, #10			; row 0: char 1, attr #A5
	ld hl, #C000
	ld b, #80
tx:	ld (hl), 1
	set 7, l
	ld (hl), #A5
	res 7, l
	inc l
	djnz tx
	TSOUT PAGE3, #11			; font: char 1 is #B0 on every line
	ld hl, #C008
	ld b, 8
fn:	ld (hl), #B0
	inc l
	djnz fn
	TSOUT VPAGE, #10
	TSOUT SGPAGE, #20
	TSOUT BORDER, #3F
	TSOUT PALSEL, #02
	if MODE == 1
	TSOUT VCONFIG, #0B
	else
	TSOUT VCONFIG, #03
	endif
	TSOUT TSCONFIG, #80
	jr $

	; y, ACT|ys, x, xs, tnum, pal<<4
spr:	db 0, #20, 0, #02, 0, #10
