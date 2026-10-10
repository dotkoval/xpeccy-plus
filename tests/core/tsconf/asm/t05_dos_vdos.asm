; Ports inside DOS and INT inside VDOS.
; Window 0 is RAM in map mode, Page0 = #20: DOS off -> page #23, DOS on -> page #21,
; VDOS -> page #FF. The same code block sits at #3D00 in #21 and #FF; #23 only reports
; that DOS never came on.
; MBOX+1 = page3 read back after #7FFD written in DOS (expect 03)
; MBOX+2 = #77 read in DOS (expect not FF)
; MBOX+3 = byte at #3FF0 seen from VDOS (expect FF = page #FF)
; MBOX+#10 = frame INTs so far; +#12 at VDOS entry, +#14 at VDOS exit (expect equal),
; +#16 after EI back in RAM (expect +#14 plus 1: the INT held through VDOS)
	include "common.inc"
	org #8000
	di
	TSOUT INTMASK, 1
	IM2_SETUP
	ld hl, 0
	ld (MBOX+#10), hl
	; page #23: DOS did not come on
	TSOUT PAGE3, #23
	ld hl, nodos
	ld de, #FD00
	ld bc, nodos_end - nodos
	ldir
	; pages #21 and #FF: the test block, and a marker at #3FF0
	TSOUT PAGE3, #21
	call putblk
	ld a, #21
	ld (#FFF0), a
	TSOUT PAGE3, #FF
	call putblk
	ld a, #FF
	ld (#FFF0), a
	TSOUT PAGE3, 0
	TSOUT FDDVIRT, 1		; drive A virtual
	TSOUT PAGE0, #20
	TSOUT MEMCONF, #09		; RAM, map mode, ROM128 = 1
	jp #3D00

putblk:	ld hl, dosblk
	ld de, #FD00
	ld bc, dosblk_end - dosblk
	ldir
	ret

nodos:	ld a, #EE
	ld (MBOX+1), a
	FINISH
nodos_end:

dosblk:
	disp #3D00
	nop
	ld bc, #7FFD
	ld a, #13
	out (c), a
	ld bc, PAGE3
	in a, (c)
	ld (MBOX+1), a
	ld bc, #0077
	in a, (c)
	ld (MBOX+2), a
	xor a
	out (#FF), a			; drive A is virtual: VDOS from the next fetch
	ld a, (#3FF0)
	ld (MBOX+3), a
	ld hl, (MBOX+#10)
	ld (MBOX+#12), hl
	ei
	ld de, 20000			; ~7 frames at 3.5 MHz
dly:	dec de
	ld a, d
	or e
	jr nz, dly
	di
	ld hl, (MBOX+#10)
	ld (MBOX+#14), hl
	in a, (#1F)			; a VG register access ends VDOS
	jp back
	ent
dosblk_end:

back:	ei
	nop
	nop
	nop
	nop
	di
	ld hl, (MBOX+#10)
	ld (MBOX+#16), hl
	FINISH

	ds #8181 - $
	push hl
	ld hl, (MBOX+#10)
	inc hl
	ld (MBOX+#10), hl
	pop hl
	ei
	ret
