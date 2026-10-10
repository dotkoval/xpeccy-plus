; No NMI while in NMI (znmi.v: nmi_start && !in_nmi). The trap is set at #0068,
; inside the handler; the first NMI comes from #BF b3 1->0 with the frame int.
; The handler must run once - the trap in it must not re-enter it.
	org #8000
	include "common.inc"
	TRAP_AT #0068
	BCOUT #00BF, #18	; b3 up, b4: trap on
	BCOUT #00BF, #10	; b3 down: NMI with the next frame int
.w:	ld a, (MBOX + 1)
	or a
	jr z, .w
	SPIN 100
	FINISH
	include "nmi_handler.inc"
