; SD card write: the host may clock FF out before the data token and the card waits
; for the token (SD spec, SPI mode data packets). CMD24 to LBA 1 with an FF gap,
; token FE, data 11 22 A5 A5..., then CMD17 reads it back: 11 22 A5, not FE 11 22.
; Mailbox 1..3: first bytes read back; 4: the data response & 1F (05 = accepted).
	org #8000
	include "common.inc"
	BCOUT #0077, 0		; CS low
	ld a, #FF
	call spiw
	ld a, #40		; CMD0
	ld de, 0
	call cmd
	ld a, #77		; CMD55
	ld de, 0
	call cmd
	ld a, #69		; ACMD41
	ld de, 0
	call cmd
	ld a, #58		; CMD24, LBA 1
	ld de, 1
	call cmd
	ld a, #FF		; the gap
	call spiw
	ld a, #FE		; token
	call spiw
	ld a, #11
	call spiw
	ld a, #22
	call spiw
	ld hl, 510
.d:	ld a, #A5
	call spiw
	dec hl
	ld a, h
	or l
	jr nz, .d
	ld a, #FF		; crc
	call spiw
	ld a, #FF
	call spiw
	call r1			; data response
	and #1F
	ld (MBOX + 4), a
	ld b, 0			; busy
.b:	call spir
	inc a
	jr nz, .bb
	djnz .b
.bb:	ld a, #51		; CMD17, LBA 1
	ld de, 1
	call cmd
	ld b, 0			; the token
.t:	call spir
	cp #FE
	jr z, .tk
	djnz .t
.tk:	call spir
	ld (MBOX + 1), a
	call spir
	ld (MBOX + 2), a
	call spir
	ld (MBOX + 3), a
	ld hl, 511		; the rest and the crc
.s:	call spir
	dec hl
	ld a, h
	or l
	jr nz, .s
	BCOUT #0077, 2		; CS high
	FINISH

; command A, argument DE (low 16 bits), crc 95; returns R1 in A
cmd:	call spiw
	xor a
	call spiw
	xor a
	call spiw
	ld a, d
	call spiw
	ld a, e
	call spiw
	ld a, #95
	call spiw
r1:	push bc
	ld b, 16
.w:	call spir
	cp #FF
	jr nz, .x
	djnz .w
.x:	pop bc
	ret

spiw:	push bc
	ld bc, #0057
	out (c), a
	pop bc
	ret

spir:	push bc
	ld bc, #0057
	in a, (c)
	pop bc
	ret
