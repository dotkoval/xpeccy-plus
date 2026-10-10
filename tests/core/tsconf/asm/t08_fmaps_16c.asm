; FMAPS takes a CRAM word on its odd byte; 16c honours an odd X offset.
; CRAM (harness): entry 0 = 00 00 (even byte alone does nothing), entry 1 = 11 22,
;   entry 2 = 11 33 (odd byte alone commits with the even byte latched last)
; LINB (harness, line 0 of the frame): 360x288 16c, row 0 = 12 34 56 78, GXOffs = 1
;   -> F2 F3 F4 F5 F6 F7 F8 ...
	include "common.inc"
	org #8000
	di
	TSOUT FMADDR, #1C		; files at #C000
	ld a, #99
	ld (#C000), a			; even byte of entry 0 only
	ld hl, #2211
	ld (#C002), hl			; entry 1, low then high
	ld a, #33
	ld (#C005), a			; odd byte of entry 2 only
	TSOUT FMADDR, 0

	TSOUT PAGE3, #10
	ld hl, row
	ld de, #C000
	ld bc, 8
	ldir
	TSOUT VPAGE, #10
	TSOUT GXOFFSL, 1
	TSOUT #04AF, 0		; the harness stops just after line 0 of a frame is drawn
	TSOUT #05AF, 0
	TSOUT VCONFIG, #C1
	ld b, 0
w:	halt				; no INT: runs into the harness's frame limit
	jr w
row:	db #12, #34, #56, #78, #9A, #BC, #DE, #F0
