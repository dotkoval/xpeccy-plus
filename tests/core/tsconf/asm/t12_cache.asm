; 14 MHz timing: loop iterations counted over 5 frames (MBOX+3, 16 bit).
; The loop is 38 T and 9 memory reads; a frame is 286720 ticks at 14 MHz.
; (counted over 5 frames)
; CACHE=0 (SysConfig #02): every read misses, +2..3 each -> about 4800 a frame
; CACHE=1 (SysConfig #06): the loop sits in the cache -> about 7500 a frame
; AYIN=1: adds IN A,(C) from #FFFD (12 T + 4 for the AY) to each turn
	include "common.inc"
	ifndef CACHE
CACHE = 0
	endif
	ifndef AYIN
AYIN = 0
	endif
	org #8000
	di
	TSOUT SYSCONF, #02 + CACHE * 4
	TSOUT INTMASK, 1
	IM2_SETUP
	xor a
	ld (MBOX+1), a
	ld hl, 0
	ld bc, #FFFD
	ei
	halt				; start on a frame
	xor a
	ld (MBOX+1), a
loop:	inc hl
	if AYIN
	in a, (c)
	endif
	ld a, (MBOX+1)
	cp 5
	jr c, loop
	ld (MBOX+3), hl
	FINISH

	ds #8181 - $
	push af
	ld a, (MBOX+1)
	inc a
	ld (MBOX+1), a
	pop af
	ei
	ret
