; video mode decode: #xx77 b2..0 = atm mode, #EFF7 b0 = 16c, b5 = hw multicolor.
; RTL: top.v pent_vmode {peff7[0],peff7[5]}; video_modedecode.v takes it only in atm
; mode 011, where 10 = pentagon 16c, 01 = hw multicolor, 00/11 = zx; the other atm
; modes ignore it. The harness prints the mode the core picked (MODE).
	org #8000
	include "common.inc"
	ifndef M77
M77 = 3
	endif
	ifndef EFF
EFF = 0
	endif
	OUT77 M77
	BCOUT #EFF7, EFF | #14
	RDPORT #0BBE, 1
	RDPORT #0CBE, 2
	FINISH
