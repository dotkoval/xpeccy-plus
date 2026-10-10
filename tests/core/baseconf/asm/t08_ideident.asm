; Nemo IDE IDENTIFY on a raw 1 MB image (2048 sectors, 16 heads x 63 = 2 cylinders):
; word 1 = cylinders, 3 = heads, 6 = sectors, 49 b9 = LBA, 54..56 = current CHS,
; 57..58 = current capacity, 60..61 = LBA sectors (ATA-4 identify layout).
; Mailbox: words 1, 3, 6, 54, 55, 56, 57, 58, 60, 61 as low/high pairs.
	org #8000
	include "common.inc"
	BCOUT #00D0, #E0	; master, LBA
	BCOUT #00F0, #EC
	ld hl, #A000		; the 256 words, read #10 then #11 as the Nemo port does
	ld b, 0
.r:	push bc
	ld bc, #0010
	in a, (c)
	ld (hl), a
	inc hl
	ld bc, #0011
	in a, (c)
	ld (hl), a
	inc hl
	pop bc
	djnz .r
	ld de, MBOX + 1
	ld hl, words
.w:	ld a, (hl)
	inc hl
	cp #FF
	jr z, .e
	push hl
	ld l, a
	ld h, 0
	add hl, hl
	ld bc, #A000
	add hl, bc
	ldi
	ldi
	pop hl
	jr .w
.e:	FINISH
words:	db 1, 3, 6, 54, 55, 56, 57, 58, 60, 61, #FF
