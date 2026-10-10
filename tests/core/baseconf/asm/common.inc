; shared definitions for bctest programs (org #8000, mailbox at #BF00)
MBOX	equ #BF00
DONE	equ #AA

	macro FINISH
		di
		ld a, DONE
		ld (MBOX), a
		halt
	endm

; in a,(port) into (MBOX+slot)
	macro RDPORT port, slot
		ld bc, port
		in a, (c)
		ld (MBOX + slot), a
	endm

; out (port),value through bc/a
	macro BCOUT port, val
		ld bc, port
		ld a, val
		out (c), a
	endm

; #xx77 with the pager on, cp/m off, palette closed (A14.A9.A8 = 1); shadow ports
; are opened around it through #BF b0
	macro OUT77 val
		BCOUT #00BF, 1
		BCOUT #4377, val
		BCOUT #00BF, 0
	endm

; the hardware trap at adr, enabled by #BF b4 (zports.v #10BD/#11BD, zbreak.v)
	macro TRAP_AT adr
		BCOUT #10BD, low adr
		BCOUT #11BD, high adr
	endm

; wait ~n*1000 T
	macro SPIN n
		ld de, n * 40
.s:	dec de
		ld a, d
		or e
		jr nz, .s
	endm
