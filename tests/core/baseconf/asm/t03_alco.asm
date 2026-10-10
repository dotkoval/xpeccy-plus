; pentagon 16c (#EFF7 b0): each 8-dot column is four bytes - page 4 (7FFD b3: 6),
; page 5 (7), page 4 +#2000, page 5 +#2000 - two dots a byte, {b6,b2..0} then
; {b7,b5..3} (video_addrgen.v addr_p16c + video_render.v pgroup; Unreal draw_p16).
; Column 0 = bytes #47 #B8 #12 #21 -> dots F 0 0 F 2 2 1 4.
	org #8000
	include "common.inc"
	BCOUT #7FFD, #14		; page 4 at #C000
	ld a, #47
	ld (#C000), a
	ld a, #12
	ld (#E000), a
	BCOUT #7FFD, #15		; page 5 at #C000
	ld a, #B8
	ld (#C000), a
	ld a, #21
	ld (#E000), a
	BCOUT #7FFD, #10
	OUT77 3
	BCOUT #EFF7, #15
	ld b, 3
.w:	ei
	halt
	djnz .w
	FINISH
