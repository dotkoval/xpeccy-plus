# BaseConf bench

`baseconf-bench` sets up the ZX Evo Baseconf core the way `res/machines/evo-baseconf.conf`
describes it (ULA.Evo, 4 MB, `zxevo-fe.rom`, `sgen.rom` as the font, Beta disk, Nemo-Evo IDE)
and stands it where a 128K snapshot would be (`hw->snapmap`: pager on, 128 rom in window 0,
pages 5,2,0 above it). A program from `bin/` (source in `asm/`) is loaded at #8000 (page 2),
SP = #BF00, DI, IM 1, and reports through the mailbox at #BF00 (byte 0 = #AA ends the run, the
rest is printed). `#9000..#9FFF` of the program is also copied to ram page FF from #0000 - the
NMI page (`asm/nmi_handler.inc`). Palette cell n gets a colour of its own, so `PIX` prints
palette indexes; the screen starts at 52,48.

`asm/common.inc`: `BCOUT`, `RDPORT`, `OUT77` (#xx77 with the pager on, shadow ports opened
through #BF b0), `TRAP_AT`, `SPIN`, `FINISH`. Ground truth is the BaseConf RTL and the avr
firmware beside it; each test says in its header which file.

| Test | Checks |
|---|---|
| t01 | video mode decode, #xx77 x #EFF7 b0/b5 (MODE line) |
| t02 | hardware multicolor attributes at +#2000 |
| t03 | 16c: pages 4,5,4+#2000,5+#2000, dot bit order |
| t04 | ps/2 log: 0 when empty, 16-byte ring, FF on overflow |
| t05 | Nemo IDE #C8 is alternate status / device control |
| t06 | no NMI while in NMI (trap inside the handler) |
| t07 | an NMI on EI is taken |
| t08 | IDE identify: geometry, current CHS, LBA sector count |
| t09 | an absent IDE slave reads status 00 |
| t10 | a reset leaves 7 MHz and the zx screen |
| t11 | the frame INT is taken once a frame at 7 MHz by a handler that EIs at once |
| t12 | a port nothing decodes reads FF |
| t13 | #BF and the palette write through #FF |
| t14 | the #FE family and #7FFD decode |
| t15 | pager details: leaving DOS, protected windows |
| t16 | the avr's clock registers, host time fixed |
| t17 | the shadow (DOS) side of the ports |
| t18 | SD card write with FF clocked out before the data token |
| t19 | CPU timing: the 14 MHz cache, the AY port stall, the TR-DOS rom M1 waits |

`cases.txt` lists how each program is assembled and every run. The environment of a run:
`KEYS=n` presses and releases 'A' n times on the ps/2 keyboard, `HDD=img` and `SDCARD=img`
mount a blank 1 MB image, `PAL=1` prints the 16 cells, `TICKS=1` the T the program took,
`RESETAFTER=1` the state a reset leaves.
