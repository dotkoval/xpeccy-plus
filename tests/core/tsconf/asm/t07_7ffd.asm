; #7FFD under each LCK128 mode; Page3 read back through #13AF after every write.
; MBOX+1 1024K  #6B -> #2B       MBOX+2 1024K then #01 -> #01 (bit 5 did not lock)
; MBOX+3 auto, OUT (#FD),A #43 -> #03   MBOX+4 auto, OUT (C),A #43 -> #0B
; MBOX+5 128K  #43 -> #03        MBOX+6 512K #C3 -> #1B
; MBOX+7 512K  #22 -> #02, then #05 is locked out -> MBOX+8 #02
	include "common.inc"
	org #8000
	di
	TSOUT MEMCONF, #C4
	ld a, #6B
	call wr
	ld (MBOX+1), a
	ld a, #01
	call wr
	ld (MBOX+2), a

	TSOUT MEMCONF, #84
	ld a, #43
	out (#FD), a
	call rd
	ld (MBOX+3), a
	ld a, #43
	call wr
	ld (MBOX+4), a

	TSOUT MEMCONF, #44
	ld a, #43
	call wr
	ld (MBOX+5), a

	TSOUT MEMCONF, #04
	ld a, #C3
	call wr
	ld (MBOX+6), a
	ld a, #22
	call wr
	ld (MBOX+7), a
	ld a, #05
	call wr
	ld (MBOX+8), a
	FINISH

wr:	ld bc, #7FFD
	out (c), a
rd:	ld bc, PAGE3
	in a, (c)
	ret
