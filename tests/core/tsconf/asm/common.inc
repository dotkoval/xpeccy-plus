; shared definitions for tstest programs (org #8000, mailbox at #BF00)
MBOX	equ #BF00
DONE	equ #AA

VCONFIG	equ #00AF
VPAGE	equ #01AF
GXOFFSL	equ #02AF
TSCONFIG equ #06AF
PALSEL	equ #07AF
BORDER	equ #0FAF
PAGE0	equ #10AF
PAGE3	equ #13AF
FMADDR	equ #15AF
SGPAGE	equ #19AF
DMASAL	equ #1AAF
DMASAH	equ #1BAF
DMASAX	equ #1CAF
DMADAL	equ #1DAF
DMADAH	equ #1EAF
DMADAX	equ #1FAF
SYSCONF	equ #20AF
MEMCONF	equ #21AF
HSINT	equ #22AF
VSINTL	equ #23AF
VSINTH	equ #24AF
DMALEN	equ #26AF
DMACTR	equ #27AF
DMANUM	equ #28AF
FDDVIRT	equ #29AF
INTMASK	equ #2AAF

; out (port),value through bc/a
	macro TSOUT port, val
		ld bc, port
		ld a, val
		out (c), a
	endm

; IM 2 with every vector going to HANDLER (#8181): table at #BD00
	macro IM2_SETUP
		ld hl, #BD00
		ld de, #BD01
		ld bc, 256
		ld (hl), #81
		ldir
		ld a, #BD
		ld i, a
		im 2
	endm

	macro FINISH
		di
		ld a, DONE
		ld (MBOX), a
		halt
	endm

; wait for DMAStatus b7 to drop
	macro DMA_WAIT
.w:	ld bc, DMACTR
	in a, (c)
	rla
	jr c, .w
	endm
