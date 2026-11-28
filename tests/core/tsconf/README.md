# TSConf bench

`tsconf-bench` sets up the TSConf core the way `res/machines/evo-tsconf.conf` describes it and
runs one small Z80 program from `bin/` (source in `asm/`). A `.bin` is loaded at #8000 (page
2) with windows 1..3 = pages 5,2,0, SP = #BF00, DI, IM 1; a `.spg` goes through the real
`loadSPG`. The program reports through the mailbox at #BF00: byte 0 = #AA ends the run, the
rest is printed, followed by the first 16 bytes of CRAM, SFILE and `vid->linb` (the last
bitmap line drawn - a run ends just after line 0 of a frame).

`asm/common.inc` has the register names and `IM2_SETUP` (every vector -> #8181, table at
#BD00; a test patches #BDFD/#BDFB to split line and DMA INTs off). Each test says in its
header what the right answer is and where it comes from; the ground truth is the RTL of
the ZX Evo TSConf configuration.

| Test | Checks |
|---|---|
| t01 | frame INT taken once per frame at 14 MHz with a short ISR |
| t02 | line INT every line with the frame INT on (3.5 and 14 MHz builds) |
| t03 | line INT acknowledged in IM 1 |
| t04 | DMA: CRAM in bursts, addresses carried over, align wrap, fill, 4 MB wrap |
| t05 | #7FFD and #77 inside DOS; no INT in VDOS, the held frame INT after it |
| t06 | VSINTH: bit 0 only, bits 7..4 step the INT line |
| t07 | #7FFD in 512K / 128K / auto (by opcode) / 1024K, and LOCK |
| t08 | FMAPS writes CRAM as words; 16c odd X offset |
| t09 | SPG loader: signature, BASIC 48 at #0000, IY |
| t10 | layer mixing: plain, GFXOVR, NOTSU, TSConfig b0, NOGFX (PIX probes), #FE border with PalSel |
| t11 | FDDVirt b7 opens the WD1793 ports outside DOS |
| t12 | 14 MHz: loop speed with the cache off/on, and the AY port stall |
| t13 | DMA takes time (polling turns at 3.5 and 14 MHz), and its INT |
| t14 | DMA address registers are the working ones: a write mid-transfer moves it |
| t15 | tile layer 0 from the tile map, with a Y offset |
| t16 | a sprite moved mid-frame: the TSU draws a line ahead |
| t17 | text mode: half-dot pixels mixed with a sprite, under PalSel |
| z01 | ZiFi: the registers, the API switch, the address mirrors, the 16550 view; nothing answers with ZiFi not fitted |
| z02 | ZiFi: 511 bytes held, counts stop at 191, INIR by the count; the output FIFO and Clear FIFO |
| z03 | ZiFi: the data register is RS-232's until ZIFR or ZOFR is read; the API off drops writes |
| z04 | ZiFi: the threshold and timeout interrupts on the wait-port source (INTMask b3, #F9) |
| z05 | ZiFi: WAIT at 3.5 and 14 MHz, and no frame INT lost in it |
| z06 | ZiFi: the Z80's reset leaves the AVR's state |
| z07 | ZiFi: 300 bytes out and back at the UART's pace; a module at 74880 read at 115200 |
| z08 | ZiFi in an SZX: made, loaded, the state and its clock as saved |

The z tests run with `ZIFI=1`, the bench standing in for the module on ZiFi's line (`ZFEED`, `ZFAR`,
`ZLOOP`, `ZRESET`, `ZSZX`: `main.c` says how); `ZTX` and `ZINT` are what the AVR sent and when its
interrupt was latched.

`cases.txt` lists how each program is assembled and every run; `PIX="x,y ..."` prints the
palette index of dots of the last whole frame (tests give each colour a CRAM entry of its
own), `PIXH` the same in half dots.
