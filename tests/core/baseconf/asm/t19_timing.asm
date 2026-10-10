; CPU timing (harness TICKS: ticks from the start to the end of the program).
; MODE 0: 14 MHz, 256 x ld a,(hl) / inc hl / djnz from ram. A ram read the two-word
;   cache (zmem.v: the last code word and the last data word, by A15..A1) cannot
;   answer waits for the 7 MHz DRAM: 6..3 fclk an opcode, 5..2 data, by the DRAM
;   cycle's phase. Three misses a turn: the two opcode words and (hl); djnz's
;   offset is in the opcode word just read.
; MODE 1: 14 MHz, 256 x in a,(c) on #FFFD: an AY or WD1793 access holds the clock
;   for 6 fclk, 3 ticks (zclock.v io_wait).
; MODE 2: 3.5 MHz, 256 x call #3D2F (nop, ret in the TR-DOS rom): every M1 from #3Dxx
;   of the 48 rom with the DOS bit holds the clock for 4 fclk (atm_pager.v
;   zclk_stall): a tick each, two M1 a call.
; MODE 3: the same at 14 MHz, where 4 fclk are 2 ticks.
	org #8000
	include "common.inc"
	ifndef MODE
MODE = 0
	endif
	if (MODE == 2)
	BCOUT #EFF7, #14	; 3.5 MHz
	else
	BCOUT #00BF, 1
	BCOUT #4377, #0B	; b3: 14 MHz
	BCOUT #00BF, 0
	endif
	ld b, 0
	ld hl, #C000
	if (MODE == 0)
	jr .m
	align 2
.m:	ld a, (hl)
	inc hl
	djnz .m
	endif
	if (MODE == 1)
	ld de, #FFFD
.i:	push bc
	ld b, d
	ld c, e
	in a, (c)
	pop bc
	djnz .i
	endif
	if (MODE >= 2)
.c:	call #3D2F
	djnz .c
	endif
	FINISH
