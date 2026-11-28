; ZiFi: the registers, the API switch, the address mirrors and the 16550 view.
; Every byte below is what rs232.c answers (rs232_init, rs232_zx_read, rs232_zx_write):
;  +1..+4   API off: ER, ZIFR, ZIBTR, DR read #FF (the registers answer only with the API on)
;           ZIBTR := #55 with the API off - writes of the thresholds are taken all the same
;  +5       Set API 1 -> ER 00
;  +6..+9   ZIBTR 55, ZITOR 01, RIBTR 80, RITOR 01 (the last three as rs232_init leaves them)
;  +10      Get Version -> ER 01 (ZF_VER)
;  +11..+14 ZIFR 00, ZOFR BF, RIFR 00, ROFR BF: empty rings; the free count 255 stops at 191
;  +15      ISR 00
;  +16      #CAEF, no register there: FF
;  +17..+19 the mirrors (slavespi.v hands the AVR #C0 + the low nibble for #C0..#EF, and the
;           data register for #F0..#F7): #D5EF is ZIBTR 55, #E7EF is ER 01, #F3EF is DR, empty: FF
;  +20      Set API 2: above the last mode, the API goes off -> ER reads FF
;  +21      Set API 1 again -> ER 00
;  +22..    the 16550 at #F8EF..#FFEF: data 00 (its ring is empty), IER 00, then 0F after #FF
;           is written (four bits kept), ISR 01, LCR 00; with LCR b7 the divisor: DLL 01,
;           DLM 00; MCR 00, then 1F after #FF (five bits kept); LSR 20 (room to send,
;           nothing sent yet); MSR A0; the scratch register FF, then 5A as written
; Expected: BF01: FF FF FF FF 00 55 01 80 01 01 00 BF 00 BF 00 FF
;           BF11: 55 01 FF FF 00 00 00 0F 01 00 01 00 00 1F 20 A0
;           BF21: FF 5A
; With ZiFi not fitted nothing answers #xxEF: every read floats (FF).
	include "common.inc"
	include "zifi.inc"
	org #8000
	di
	ld hl, MBOX + 1
	ZIN ZF_CR
	ZIN ZF_IFR
	ZIN ZF_IBTR
	ZIN ZF_DR
	ZOUT ZF_IBTR, #55
	ZOUT ZF_CR, ZF_API1
	ZIN ZF_CR
	ZIN ZF_IBTR
	ZIN ZF_ITOR
	ZIN RS_IBTR
	ZIN RS_ITOR
	ZOUT ZF_CR, ZF_VER
	ZIN ZF_CR
	ZIN ZF_IFR
	ZIN ZF_OFR
	ZIN RS_IFR
	ZIN RS_OFR
	ZIN ZF_IMR
	ZIN #CAEF
	ZIN #D5EF
	ZIN #E7EF
	ZIN #F3EF
	ZOUT ZF_CR, #F2
	ZIN ZF_CR
	ZOUT ZF_CR, ZF_API1
	ZIN ZF_CR
	ZIN #F8EF
	ZIN #F9EF
	ZOUT #F9EF, #FF
	ZIN #F9EF
	ZIN #FAEF
	ZIN #FBEF
	ZOUT #FBEF, #80
	ZIN #F8EF
	ZIN #F9EF
	ZOUT #FBEF, #03
	ZIN #FCEF
	ZOUT #FCEF, #FF
	ZIN #FCEF
	ZIN #FDEF
	ZIN #FEEF
	ZIN #FFEF
	ZOUT #FFEF, #5A
	ZIN #FFEF
	FINISH
