; The avr's clock registers (avr rtc.c gluk_get_reg/gluk_set_reg), host time fixed
; by the harness to Sunday 2025-10-05 12:00 UTC.
; - A is stored: 2A written reads 2A.
; - B keeps its binary bit and reads b1 set (24h): 06 written reads 06.
; - day of week is 1..7, Sunday 1 (DS12887): 01 in binary mode.
; - an alarm register is stored: #01 <- 33 reads 33 (binary mode, as written).
; - D is the ctrl/alt/shift keys with b7 clear: 00 with no key held.
; - #0E is the Win/Menu keys, not nvram: 55 written reads 00.
; Mailbox 1..6 in that order.
	org #8000
	include "common.inc"
	BCOUT #EFF7, #94	; b7: the clock ports outside DOS
	macro CMW reg, val
		BCOUT #DFF7, reg
		BCOUT #BFF7, val
	endm
	macro CMR reg, slot
		BCOUT #DFF7, reg
		RDPORT #BFF7, slot
	endm
	CMW #0A, #2A
	CMR #0A, 1
	CMW #0B, #06
	CMR #0B, 2
	CMR #06, 3
	CMW #01, #33
	CMR #01, 4
	CMR #0D, 5
	CMW #0E, #55
	CMR #0E, 6
	CMW #0B, #02		; back to BCD
	FINISH
