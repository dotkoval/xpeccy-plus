; Nemo IDE on ZX Evo: #C8 is CS1 - Alternate Status on a read, Device Control on
; a write - not a task file register (zports.v ide_cs1_n = loa==NIDEC8; Unreal
; hdd.read(8), MAME cs1). Head <- E0, #C8 <- 02 (nIEN): the head must keep E0,
; and #C8 must read what the status register #F0 reads.
	org #8000
	include "common.inc"
	BCOUT #00D0, #E0
	BCOUT #00C8, #02
	RDPORT #00D0, 1
	RDPORT #00C8, 2
	RDPORT #00F0, 3
	FINISH
