; #BF and the palette write through #FF.
; - #BF reads {00, pal444, brk, nmi, fntw, romrw, shadow} (zports.v): #E1 written reads #21.
; - #BF b5 (pal444) takes the low bit of each channel from the port's high byte
;   (video_palframe.v); without it they repeat the high bits. Data 00, port #FFFF:
;   cell 2 = CC,CC,CC with pal444 (PAL line, cell 2), cell 3 = FF,FF,FF without.
; - #0Dxx reads the cell back: the low bits with pal444 on, the data without, b3..2
;   as 1 (video_palframe.v palcolor, zports.v BD_COLORRD): FF, then 0C.
; - the cell written is the border's as just set by #FE (zports.v border), not a
;   latched copy: OUT (#FE),2 right before the write lands in cell 2.
	org #8000
	include "common.inc"
	BCOUT #00BF, #E1	; shadow on, pal444 on, b7/b6 set
	RDPORT #00BF, 1
	BCOUT #0377, 3		; A14 low: palette open, pager on
	ld a, 1
	out (#FE), a
	ld a, 2
	out (#FE), a
	ld bc, #FFFF
	xor a
	out (c), a		; cell 2 <- 00 / FF
	RDPORT #0DBE, 2		; pal444: the low bits, FF
	BCOUT #00BF, #01
	RDPORT #0DBE, 3		; without: the data, 00 -> 0C
	BCOUT #00BF, #21
	ld a, 3
	out (#FE), a
	BCOUT #00BF, #01	; pal444 off
	ld bc, #FFFF
	xor a
	out (c), a		; cell 3 <- 00 / FF
	BCOUT #4377, 3		; palette closed
	BCOUT #00BF, 0
	FINISH
