; Sprite changed mid-frame (the TSU draws a line ahead, video_ts.v). 360x288, NOGFX.
; Sprite 0: x 100, y 0, 8x64, colour 5 of palette 1 (#15). The frame INT (VSINT 0, HSINT 1)
; zeroes a line counter and the line INT counts it up; the frame INT is taken ahead of the
; line INT of line 0, so the count is 40 on hardware line 39, and the handler writes X = 200
; around dot 234 of it. The TSU drew window line 8 (hardware line 40) during line 39 from
; dot 88, before the write: lines 0..8 keep x 100, from 9 on the sprite is at x 200.
; (A TSU that draws a line during the line itself moves it from line 8.)
; Probes (harness): 104,y and 204,y for y 7..9
	include "common.inc"
	org #8000
	di
	TSOUT FMADDR, #1C
	ld hl, #C400 : ld (#C000 + #15 * 2), hl
	ld hl, #8421 : ld (#C000 + #3F * 2), hl
	ld hl, spr0
	ld de, #C200
	ld bc, 6
	ldir
	TSOUT FMADDR, 0
	TSOUT PAGE3, #20			; sprite graphics: tile 0 column, 64 lines, colour 5
	ld hl, #C000
	ld c, 64
sg:	ld (hl), #55
	inc hl
	ld (hl), #55
	inc hl
	ld (hl), #55
	inc hl
	ld (hl), #55
	dec hl
	dec hl
	dec hl
	inc h
	dec c
	jr nz, sg
	TSOUT SGPAGE, #20
	TSOUT BORDER, #3F
	TSOUT VCONFIG, #E1
	TSOUT TSCONFIG, #80
	TSOUT HSINT, 1
	TSOUT VSINTL, 0
	TSOUT VSINTH, 0
	TSOUT FMADDR, #1C			; the sprite file stays mapped for the handler
	IM2_SETUP
	ld a, #82
	ld (#BDFD), a
	ld (#BDFE), a
	TSOUT INTMASK, 3
	ei
	jr $

spr0:	db 0, #20 | (7 << 1), 100, 0, 0, #10

	ds #8181 - $
	push af				; frame: count from here
	xor a
	ld (cnt), a
	ld a, 100			; and put the sprite back
	ld (#C202), a
	xor a
	ld (#C203), a
	pop af
	ei
	ret
cnt:	db 0

	ds #8282 - $
	push af
	ld a, (cnt)
	inc a
	ld (cnt), a
	cp 40
	jr nz, lx
	ld a, 200
	ld (#C202), a
	xor a
	ld (#C203), a
lx:	pop af
	ei
	ret
