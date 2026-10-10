; NMI handler: #9000.. of the program is ram page FF, which stands at #0000 in
; NMI. It counts itself in MBOX+1 and leaves through out (#BE) + RETN.
	ds #9066 - $, 0
	nop			; the 0066 fetch reads as NOP whatever is here
	nop
	ld hl, MBOX + 1		; 0068
	inc (hl)
	ld bc, #00BE
	out (c), a
	retn
