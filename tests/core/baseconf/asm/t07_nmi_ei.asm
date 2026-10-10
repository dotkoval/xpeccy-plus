; An NMI is not held off by EI: the trap on an EI must still call the handler.
	org #8000
	include "common.inc"
	TRAP_AT eipos
	BCOUT #00BF, #10	; b4: trap on
eipos:	ei
	nop
	di
	SPIN 50
	FINISH
	include "nmi_handler.inc"
