; Pager details (atm_pager.v, zports.v):
; - leaving DOS remaps whatever 7FFD b4 says: dos feeds the rom entries of both maps.
;   DOS held by #xx77 A9 low, 7FFD b4 <- 0, A9 back up; the next M1 from ram leaves
;   DOS and window 0 must show rom page 1E (byte #0001 = 01), not 1F (C3).
; - #xBF7 write protect covers the map's own page only: with ram0 at #0000 (#EFF7
;   b3) the window takes writes (wrdisable = trdemu_wr_disable there).
;   Window 0 of map 0 protected, ram0 in: #0000 <- 5A reads 5A.
; - a protected ram window refuses writes: window 3 of map 0 is page 0 too, the one
;   ram0 put at #0000, so #C000 <- A5 leaves the 5A written there.
; - #12BD is read only (#12BE reads the bits #xBF7 set): #12BD <- FF leaves 09.
	org #8000
	include "common.inc"
	BCOUT #00BF, 1
	BCOUT #4177, 3		; A9 low: DOS held on
	BCOUT #00BF, 0
	BCOUT #7FFD, #00	; 7FFD b4 = 0: map 0
	BCOUT #00BF, 1
	BCOUT #4377, 3		; A9 up
	BCOUT #00BF, 0
	nop			; an M1 from ram: DOS is left here
	ld a, (#0001)
	ld (MBOX + 1), a
	; write protect, map 0: window 0 and window 3
	xor a
	ld (#C000), a
	BCOUT #00BF, 1
	BCOUT #0BF7, 1		; window 0
	BCOUT #CBF7, 1		; window 3
	BCOUT #00BF, 0
	BCOUT #EFF7, #1C	; b3: ram0 at #0000
	ld a, #5A
	ld (#0000), a
	ld a, (#0000)
	ld (MBOX + 2), a
	ld a, #A5
	ld (#C000), a
	ld a, (#C000)
	ld (MBOX + 3), a
	BCOUT #12BD, #FF
	RDPORT #12BE, 4
	FINISH
