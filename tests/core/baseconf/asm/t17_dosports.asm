; The shadow (DOS) side of the ports (zports.v, vg93.v):
; - #FF reads {intrq, drq, 1, and b4..0 of the last #FF write}: 3C written reads
;   3C under mask 3F.
; - #77 reads 0 in DOS as well (SDCFG dout = 00).
; - a drive marked virtual through #13BD does not reach the WD1793 (vg_cs_n =
;   vg_matched_n): with no disk in A, #3F <- 55 then #3F reads FF, not 55.
; - covox #FB is written in DOS too (covox_wr has no shadow term): COVOX 77.
; Mailbox 1..3: #FF & 3F, #77, #3F.
	org #8000
	include "common.inc"
	BCOUT #00BF, 1
	BCOUT #00FF, #3C
	ld bc, #00FF
	in a, (c)
	and #3F
	ld (MBOX + 1), a
	RDPORT #0077, 2
	BCOUT #13BD, #01
	BCOUT #003F, #55
	RDPORT #003F, 3
	BCOUT #13BD, #00
	BCOUT #00FB, #77
	BCOUT #00BF, 0
	FINISH
