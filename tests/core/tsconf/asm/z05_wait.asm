; ZiFi: WAIT. Every #xxEF access holds the Z80 until the AVR has answered over SPI (zwait.v,
; zx_wait_task). Nobody has measured how long; the firmware's path puts it at 15-30 us and the
; core takes 20 us: 70 T at 3.5 MHz, 140 T at 7, 280 T at 14.
; 100 reads of ZIFR with the interrupts off. STATE's T count against the same run with ZiFi
; not fitted (where nothing answers and nothing waits) is 101 accesses' worth: 7070 T at
; CLK=0, 28280 T at CLK=6.
;  +1 = the last byte read: 00 (ZIFR, the API on); FF with ZiFi not fitted
; And the frame INT is not lost in it: zint.v stops the pulse's 32-clock count while WAIT is
; low. FRAMES=1: the same reads go on with the frame INT on until it has come 50 times -
; "done after 50 frames", as with ZiFi not fitted.
	include "common.inc"
	include "zifi.inc"
	ifndef CLK
CLK = 0
	endif
	ifndef FRAMES
FRAMES = 0
	endif
	org #8000
	di
	TSOUT SYSCONF, CLK
	TSOUT ZF_CR, ZF_API1
	if FRAMES
	TSOUT INTMASK, 1
	IM2_SETUP
	ld bc, ZF_IFR
	ei
l2:	in a, (c)
	jr l2
	else
	ld bc, ZF_IFR
	ld d, 100
l1:	in a, (c)
	dec d
	jr nz, l1
	ld (MBOX + 1), a
	FINISH
	endif

	ds #8181 - $
	push af
	ld a, (MBOX + 1)
	inc a
	ld (MBOX + 1), a
	cp 50
	jr z, fin
	pop af
	ei
	ret
fin:	FINISH
