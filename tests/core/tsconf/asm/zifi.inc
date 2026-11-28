; ZiFi: the ZX Evo's AVR at #xxEF, as the TS firmware has it (pentevo/avr/current/rs232.c)
ZF_DR	equ #BFEF	; data: #00EF..#BFEF, B as INIR's count
ZF_IFR	equ #C0EF	; ZiFi's input FIFO, bytes held (191 at most); DR is ZiFi's from here on
ZF_OFR	equ #C1EF	; ZiFi's output FIFO, room (191 at most); the same
RS_IFR	equ #C2EF	; RS-232's; DR is RS-232's from here on
RS_OFR	equ #C3EF
ZF_IMR	equ #C4EF	; written: the interrupt mask; read: the sources, cleared
ZF_IBTR	equ #C5EF
ZF_ITOR	equ #C6EF
ZF_CR	equ #C7EF	; written: a command; read: its result
RS_IBTR	equ #C8EF
RS_ITOR	equ #C9EF

ZF_API1	equ #F1
ZF_VER	equ #FF

; in a,(port) into the mailbox at (hl), hl on
	macro ZIN port
		ld bc, port
		in a, (c)
		ld (hl), a
		inc hl
	endm

	macro ZOUT port, val
		ld bc, port
		ld a, val
		out (c), a
	endm
