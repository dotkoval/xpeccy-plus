; An absent device 1 selected on a bus with device 0: device 0 answers for it and
; its Status and Alternate Status read 00 (ATA: device 0 responds for device 1).
; NedoOS hddfdisk selects device 1 by mistake before INITIALIZE and waits for BSY
; to drop. Status, Alt Status with the slave selected, then Status of the master.
	org #8000
	include "common.inc"
	BCOUT #00D0, #10
	RDPORT #00F0, 1
	RDPORT #00C8, 2
	BCOUT #00D0, #E0
	RDPORT #00F0, 3
	FINISH
