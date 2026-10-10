; What a reset leaves (harness RESETAFTER): ATM EGA and 3.5 MHz set here, then the
; reset. zports.v: atm_scr_mode = 011 and peff7 = 00 at reset, so a zx screen and
; turbo {atm_turbo, ~peff7[4]} = 01, 7 MHz (top.v .turbo).
	org #8000
	include "common.inc"
	OUT77 0
	BCOUT #EFF7, #14
	FINISH
