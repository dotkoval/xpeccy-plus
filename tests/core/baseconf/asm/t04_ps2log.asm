; ps/2 keyboard log, cmos cells F0..FF in mode 2 (avr ps2.c ps2keyboard_from_log):
; 0 when empty, the bytes in order, FF once when 16 bytes filled it up - which
; also clears it. The harness queues 3 bytes per KEYS before the run.
	org #8000
	include "common.inc"
	BCOUT #EFF7, #94		; b7: the clock ports open outside DOS
	BCOUT #DFF7, #F0
	BCOUT #BFF7, 2
	ld hl, MBOX + 1
	ld e, 24
.r:	ld bc, #BFF7
	in a, (c)
	ld (hl), a
	inc hl
	dec e
	jr nz, .r
	FINISH
