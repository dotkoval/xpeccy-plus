; #FE family and #7FFD decode (zports.v):
; - #7FFD is A15 = 0 and the low byte FD or FC; A14 is not decoded (portfd_wr):
;   #3FFD <- 13 reads back 13 at #0ABE.
; - the border is written by FE, F6 and FC, A3 low making it bright (portfe_wr_fclk):
;   #FFFC <- 03 gives 03, #FFF6 <- 05 gives 0D (#0FBE).
; - the beeper is FE's alone (beeper_wr): FE b4 = 0, then F6 b4 = 1 leaves it 0 (BEEP).
; - #FE reads {1, tape, 0, keys}: b5 = 0.
	org #8000
	include "common.inc"
	BCOUT #3FFD, #13
	RDPORT #0ABE, 1
	BCOUT #7FFD, #10
	BCOUT #FFFC, #03
	RDPORT #0FBE, 2
	BCOUT #FFFE, #00
	BCOUT #FFF6, #15
	RDPORT #0FBE, 3
	ld a, #FF
	in a, (#FE)
	and #20
	ld (MBOX + 4), a
	FINISH
