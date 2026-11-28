// Headless TSConf bench: runs a raw Z80 binary on libxpeccy's TSConf core
// and prints the mailbox the program fills in.
//
//	tsconf-bench <prog.bin|prog.spg> [frames]
//
// XPECCY_ROOT is the source tree, for config/roms/tsconf.rom (default: the
// current folder). The machine is set up by hand after res/machines/evo-tsconf.conf
// (layout 448x320, 4 MB, Beta disk, Nemo-Evo IDE): a change there does not reach it.
//
// A .bin is loaded at #8000 (RAM page 2), windows 1..3 hold pages 5,2,0,
// SP = #BF00, DI, IM 1; a .spg goes through the real loadSPG. The program
// reports through the mailbox at #BF00..#BF7F: byte 0 = #AA ends the run,
// bytes 1.. are printed.
//
// Environment: PIX="x,y ..." prints the palette index of dots of the last whole
// frame, PIXH the same in half dots, DUMP=f.ppm writes the frame, TSU=1 prints
// the TSU state, HIDE=mask hides layers (TSL_HIDE_*), STATE=1 prints a hash of
// the RAM, the CPU registers and the T count.
//
// ZIFI=1 fits ZiFi (the AVR's #xxEF ports) with the bench as the module on its line:
//	ZFEED="us:spec ..."   bytes the module sends, each group handed over us microseconds
//	                      after the start: hex pairs, or +N for the N bytes 1, 2, 3...
//	ZFAR=baud             ...at that rate (8N1), else the AVR's own 115200
//	ZLOOP=1               what the AVR sends comes back, a byte as its frame ends
//	ZRESET=frame          the machine is reset at that frame and the program started again
//	ZSZX=us1,us2          an SZX is made at us1 and loaded at us2: ZSZX says whether ZiFi and
//	                      its clock came back as they were
// It prints what the AVR sent (ZTX: how many, the first of them, when the first and the
// last started, in us) and when the wait-port interrupt was latched (ZINT, us).

#include "../bench.h"
#include "filetypes/filetypes.h"
#include "filetypes/szx.h"
#include "xreplay.h"
#include "hardware/zx_step.h"

static unsigned fnv(const unsigned char* p, size_t n) {
	unsigned h = 2166136261u;
	while (n--) h = (h ^ *p++) * 16777619u;
	return h;
}

static void state_line(Computer* comp, const char* tag) {
	cpuGetRegs(comp->cpu);		// F is made from the flags when asked for
	printf("%s: ram %08X pc %04X sp %04X af %04X t %lld\n", tag, fnv(comp->mem->ramData, MEM_4M),
		comp->cpu->regPC, comp->cpu->regSP, comp->cpu->regAF, comp->tickTotal);
}

// == ZiFi: the bench as the module ==

#define ZTX_MAX	1024
#define ZINT_MAX 16

static struct {
	int on, loop;
	long long t0;
	const char* feed;
	int ntx;
	unsigned char tx[ZTX_MAX];
	long long txFirst, txLast;
	int nint, wint;
	long long intAt[ZINT_MAX];
	// ZSZX
	long szxAt[2];
	int szxStage;			// 0 none asked, 1 to save, 2 to load, 3 done
	szxBuf szx;
	xZifi was;
	long long wasClk;
} zf;

static void zf_tx(Computer* comp, int b, long long start) {
	if (zf.ntx < ZTX_MAX) zf.tx[zf.ntx] = (unsigned char)b;
	if (!zf.ntx) zf.txFirst = start;
	zf.txLast = start;
	zf.ntx++;
	if (zf.loop) {
		unsigned char c = (unsigned char)b;
		zifi_feed_at(comp, &c, 1, start + 95486);	// 11 bits at 115200
	}
}

// before a step: the groups whose time has come, and the interrupt's latch
static void zf_step(Computer* comp) {
	if (!zf.on) return;
	long long us = (zifi_now(comp) - zf.t0) / 1000;
	while (zf.feed && *zf.feed) {
		char* e;
		long at = strtol(zf.feed, &e, 10);
		if ((*e != ':') || (at > us)) break;
		e++;
		unsigned char buf[2048];
		int n = 0;
		if (*e == '+') {
			int cnt = (int)strtol(e + 1, &e, 10);
			for (int k = 1; (k <= cnt) && (n < (int)sizeof(buf)); k++) buf[n++] = k & 0xff;
		} else {
			unsigned v;
			while ((n < (int)sizeof(buf)) && e[0] && (e[0] != ' ') && e[1] && (sscanf(e, "%2x", &v) == 1)) {
				buf[n++] = v & 0xff;
				e += 2;
			}
		}
		zifi_feed_at(comp, buf, n, 0);
		while (*e == ' ') e++;
		zf.feed = e;
	}
	if (comp->TSL_FLG_WINT && !zf.wint && (zf.nint < ZINT_MAX)) zf.intAt[zf.nint++] = us;
	zf.wint = comp->TSL_FLG_WINT;
	if ((zf.szxStage == 1) && (us >= zf.szxAt[0])) {
		zf.wasClk = zifi_sync(comp);
		int err = szx_build(comp, &zf.szx);
		zf.was = *comp->zifi;
		zf.szxStage = (err || zf.szx.fail) ? 3 : 2;
		if (zf.szxStage == 3) printf("ZSZX : not made (%d)\n", err);
	} else if ((zf.szxStage == 2) && (us >= zf.szxAt[1])) {
		int err = loadSZX_buf(comp, zf.szx.data, zf.szx.len);
		long long clk = zifi_now(comp);
		xZifi now = *comp->zifi;
		now.off = zf.was.off = 0;	// the clock's own: compared by what it reads
		printf("ZSZX : load %d, the clock %s, ZiFi %s\n", err,
			(clk == zf.wasClk) ? "goes on" : "MOVED", memcmp(&now, &zf.was, sizeof(xZifi)) ? "DIFFERS" : "as saved");
		free(zf.szx.data);
		zf.szxStage = 3;
	}
}

static void zf_start(Computer* comp) {
	if (!getenv("ZIFI")) return;
	zf.on = 1;
	comp->zifi->on = 1;
	zifi_power(comp);
	zifi_host_tx = zf_tx;
	zf.loop = (getenv("ZLOOP") != NULL);
	zf.feed = getenv("ZFEED");
	const char* far = getenv("ZFAR");
	if (far) zifi_far(comp, (long long)(1e9 / atof(far) * 65536.0), 0x18);
	zf.t0 = zifi_now(comp);
	const char* szx = getenv("ZSZX");
	if (szx && (sscanf(szx, "%ld,%ld", &zf.szxAt[0], &zf.szxAt[1]) == 2)) zf.szxStage = 1;
}

static void zf_print(void) {
	if (!zf.on) return;
	printf("ZTX  : %d", zf.ntx);
	for (int k = 0; (k < zf.ntx) && (k < 16); k++) printf(" %02X", zf.tx[k]);
	if (zf.ntx) printf(" at %lld..%lld", (zf.txFirst - zf.t0) / 1000, (zf.txLast - zf.t0) / 1000);
	printf("\nZINT :");
	for (int k = 0; k < zf.nint; k++) printf(" %lld", zf.intAt[k]);
	printf("\n");
}

// QUIET=1: steps as fast mode takes them (emuCycle's quiet loop): a HALT run on to the
// next event, the steps run on in one go, else one quiet step - all of which must leave
// the machine as compExec() does, one at a time
static void bench_step(Computer* comp) {
	static int quiet = -1;
	int ns;
	if (quiet < 0) quiet = (getenv("QUIET") != NULL);
	zf_step(comp);
	if (!quiet) {
		compExec(comp);
		return;
	}
	if (comp->cpu->flgHALT && comp_halt_skip(comp, &ns)) return;
	if (comp_run_quiet(comp, &ns, NULL, 0)) return;
	comp_exec_quiet(comp);
}

// SPRCHK=1: every active sprite as the debugger draws it (tsl_sprite_dots) against
// the last whole frame, dot for dot: a transparent dot has to show what is under it,
// which the test programs make the border
static void spr_check(Computer* comp) {
	Video* vid = comp->vid;
	static unsigned char dots[64 * 64];
	for (int i = 0; i < TSL_SPRITES; i++) {
		tsSprite s;
		tsl_get_sprite(vid, i, &s);
		if (!s.act) continue;
		tsl_sprite_dots(vid, i, dots);
		int n = 0, bad = 0;
		for (int r = 0; r < s.h; r++) {
			for (int c = 0; c < s.w; c++) {
				int x = vid->tsconf.tsXPos + ((s.x + c) & 0x1ff);
				int y = vid->tsconf.tsYPos + ((s.y + r) & 0x1ff);
				if ((x >= vid->full.x) || (y >= vid->full.y)) continue;
				int32_t got = *(int32_t*)(bufimg + y * bytesPerLine + x * 8);
				int idx = dots[r * s.w + c];
				if (got != (int32_t)vid->pal[idx ? idx : vid->brdcol]) {
					if (getenv("SPRWHY"))
						printf("SPR  : %d differs at %d,%d: dot %02X, frame shows %02X\n", i, c, r, idx, pal_index(comp, got) & 0xff);
					bad++;
				}
				n++;
			}
		}
		printf("SPR  : %d at %d,%d %dx%d tile %d pal %d%s%s%s: %d dots, %d differ\n", i, s.x, s.y, s.w, s.h,
			s.tile, s.pal, s.xf ? " XF" : "", s.yf ? " YF" : "", s.leap ? " LEAP" : "", n, bad);
	}
}

// TILECHK=1: the tile/sprite window put together the debugger's way - each enabled
// tile layer's map cell at the dot plus its offsets (tsl_map_entry, tsl_map_dots), T1
// over T0, the border where both are holes - against the last whole frame
static void tile_check(Computer* comp) {
	Video* vid = comp->vid;
	unsigned char dots[64];
	int n = 0, bad = 0, seen[2] = {0, 0};
	for (int dy = 0; dy < vid->tsconf.tsSize.y; dy++) {
		for (int dx = 0; dx < vid->tsconf.tsSize.x; dx++) {
			int want = vid->brdcol;
			for (int lay = 0; lay < 2; lay++) {
				if (!(vid->tsconf.tconfig & (lay ? 0x40 : 0x20))) continue;
				int mx = (dx + (lay ? vid->tsconf.T1XOffset : vid->tsconf.T0XOffset)) & 0x1ff;
				int my = (dy + (lay ? vid->tsconf.T1YOffset : vid->tsconf.T0YOffset)) & 0x1ff;
				tsl_map_dots(vid, lay, tsl_map_entry(vid, lay, my >> 3, mx >> 3), dots);
				int d = dots[(my & 7) * 8 + (mx & 7)];
				if (d) {
					want = d;
					seen[lay]++;
				}
			}
			int x = vid->tsconf.tsXPos + dx;
			int y = vid->tsconf.tsYPos + dy;
			int32_t got = *(int32_t*)(bufimg + y * bytesPerLine + x * 8);
			if (got != (int32_t)vid->pal[want]) {
				if (getenv("TILEWHY") && (bad < 10))
					printf("TILE : differs at %d,%d: want %02X, frame shows %02X\n", dx, dy, want, pal_index(comp, got) & 0xff);
				bad++;
			}
			n++;
		}
	}
	printf("TILE : %dx%d dots, T0 shows in %d, T1 in %d, %d differ\n", vid->tsconf.tsSize.x, vid->tsconf.tsSize.y,
		seen[0], seen[1], bad);
}

// REDRAW="frames,t,mask,dbg,ref[,steps,poke]": run whole frames, then on to T t
// of the next one, and stop there as the debugger (dbg=1) or a pause (dbg=0) does;
// in the debugger, step on steps instructions. Then hide the mask and redraw
// (xreplay), or with ref=1 print what a run that had the mask hidden from the
// start shows. The pictures and the state of the two must match. poke=1 changes a
// byte of RAM before the redraw, which has to make it refuse.
static void redraw_test(Computer* comp, const char* spec) {
	int frames = 2, t = 0, mask = 0, dbg = 0, ref = 0, steps = 0, poke = 0;
	sscanf(spec, "%d,%d,%i,%d,%d,%d,%d", &frames, &t, &mask, &dbg, &ref, &steps, &poke);
	xreplay_arm(!ref);
	if (ref) comp->vid->tsconf.hide = mask;
	int fstart = comp->vid->fcnt;
	while (comp->vid->fcnt - fstart < frames)
		compExec(comp);
	while (comp->frmtCount < t)
		compExec(comp);
	if (dbg) {		// as DebugWin::start()
		vid_dark_tail(comp->vid);
		comp->vid->debug = 1;
		comp->flgDBG = 1;
		while (steps-- > 0)
			compExec(comp);
	}
	if (poke) comp->mem->ramData[0x3fff] ^= 0xff;
	if (!ref) {
		state_line(comp, "BEFORE");
		comp->vid->tsconf.hide = mask;
		int done = xreplay_redraw(comp);
		printf("REDRAW: %s\n", done ? "done" : "refused");
		if (!done) printf("WHY  : %s\n", xreplay_why());
	}
	state_line(comp, "STATE");
	size_t drawn = comp->vid->ray.ptr - scrimg;
	// what is on screen: the debugger shows scrimg, a pause the last whole frame
	printf("PIC  : ray %d,%d scr-drawn %08X", comp->vid->ray.x, comp->vid->ray.y, fnv(scrimg, drawn));
	if (dbg) printf(" scr %08X\n", fnv(scrimg, bufSize));
	else printf(" buf %08X\n", fnv(bufimg, bufSize));
	const char* dump = getenv("DUMP");
	if (dump) dump_ppm(comp, dump);
}

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: tsconf-bench prog.bin [frames]\n");
		return 2;
	}
	int frames = (argc > 2) ? atoi(argv[2]) : 50;
	const char* root = getenv("XPECCY_ROOT");
	xhost_time_fixed = 1759665600;		// Sunday 2025-10-05 12:00 UTC: runs repeat

	Computer* comp = compCreate();
	if (!compSetHardware(comp, "TSConf")) {
		fprintf(stderr, "no TSConf core\n");
		return 1;
	}
	vLayout lay = {{448, 320}, {52, 48}, {88, 32}, {256, 192}, {0, 0}, 64};
	comp_set_layout(comp, &lay);
	bytesPerLine = comp->vid->full.x * 8;	// as emulwin.cpp sets it: a whole raster row per image row
	bufSize = bytesPerLine * comp->vid->full.y;
	memSetSize(comp->mem, MEM_4M, MEM_512K);
	if (!load_rom(comp, root ? root : ".", "tsconf.rom"))
		return 1;
	difSetHW(comp->dif, DIF_BDI);
	ide_set_type(comp->ide, IDE_NEMO_EVO);
	compSetBaseFrq(comp, 3.5);
	compReset(comp, RES_DEFAULT);

	size_t n = 0;
	unsigned char* dst = comp->mem->ramData + (2 << 14);
	const char* ext = strrchr(argv[1], '.');
	if (ext && !strcmp(ext, ".spg")) {		// through the real loader
		memset(dst + 0x3f00, 0, 0x100);
		int err = loadSPG(comp, argv[1], 0);
		if (err != ERR_OK) {
			printf("%s: loadSPG error %d\n", base_name(argv[1]), err);
			return 0;
		}
	} else {
		FILE* f = fopen(argv[1], "rb");
		if (!f) {
			fprintf(stderr, "can't open %s\n", argv[1]);
			return 1;
		}
		n = fread(dst, 1, 0x3f00, f);
		fclose(f);
		memset(dst + 0x3f00, 0, 0x100);
		comp->hw->out(comp, 0x11af, 5);
		comp->hw->out(comp, 0x12af, 2);
		comp->hw->out(comp, 0x13af, 0);
		cpu_start(comp);
	}

	zf_start(comp);
	const char* zreset = getenv("ZRESET");
	int resetAt = zreset ? atoi(zreset) : -1;
	const char* hide = getenv("HIDE");
	if (hide) comp->vid->tsconf.hide = (unsigned char)strtol(hide, NULL, 0);
	// DMAAT="frames,t,...": stop at T t of a frame (after the whole ones) and print
	// where the transfer stands, as the debugger sees it; each pair a stop further on
	const char* dmaat = getenv("DMAAT");
	if (dmaat) {
		int f, t, k, fstart = comp->vid->fcnt;
		while (sscanf(dmaat, "%d,%d%n", &f, &t, &k) == 2) {
			while (comp->vid->fcnt - fstart < f) compExec(comp);
			while (comp->frmtCount < t) compExec(comp);
			tsRegs r;
			ts_get_regs(comp, &r);
			printf("DMAAT: %d,%d act %d src %06X dst %06X burst %d/%d word %d/%d regs %06X %06X\n", f, t, r.dmaact,
				r.dmaCurSrc & 0xffffff, r.dmaCurDst & 0xffffff, r.dmaBurst, r.dmaBursts, r.dmaWord, r.dmaWords, r.dmasrc, r.dmadst);
			dmaat += k;
			while (*dmaat == ' ') dmaat++;
		}
		compDestroy(comp);
		return 0;
	}
	const char* redraw = getenv("REDRAW");
	if (redraw) {
		redraw_test(comp, redraw);
		compDestroy(comp);
		return 0;
	}
	int fstart = comp->vid->fcnt;
	while ((mbox_rd(comp, MBOX) != 0xaa) && (comp->vid->fcnt - fstart < frames)) {
		bench_step(comp);
		if ((resetAt >= 0) && (comp->vid->fcnt - fstart >= resetAt)) {
			resetAt = -1;
			compReset(comp, RES_DEFAULT);
			comp->hw->out(comp, 0x11af, 5);
			comp->hw->out(comp, 0x12af, 2);
			comp->hw->out(comp, 0x13af, 0);
			cpu_start(comp);
		}
	}
	printf("%s: %zu bytes, %s after %d frames, pc=%04X\n", base_name(argv[1]), n,
		(mbox_rd(comp, MBOX) == 0xaa) ? "done" : "TIMEOUT",
		comp->vid->fcnt - fstart, comp->cpu->regPC);
	mbox_print(comp);
	probe(comp, "PIX", "PIX  ", 8);
	probe(comp, "PIXH", "PIXH ", 4);
	printf("BRD  : %02X\n", comp->vid->brdcol);
	zf_print();
	if (getenv("STATE")) state_line(comp, "STATE");
	if (getenv("CACHE"))		// the cpu's cache, and what the steps count of its DRAM cycles
		printf("CACHE: tags %08X conf %X dram %d t %lld\n", fnv((unsigned char*)comp->tsconf.cache, sizeof(comp->tsconf.cache)),
			comp->tsconf.cacheConf, comp->tsconf.cpuDram, comp->tickTotal);
	if (getenv("SPRCHK")) spr_check(comp);
	if (getenv("TILECHK")) tile_check(comp);
	if (getenv("DMALOG")) {
		unsigned cnt = comp->tsconf.dmalog.count;
		printf("DMA  : %u started\n", cnt);
		for (unsigned k = (cnt > TS_DMALOG) ? cnt - TS_DMALOG : 0; k < cnt; k++) {
			tsDmaRec* r = &comp->tsconf.dmalog.rec[k % TS_DMALOG];
			printf("DMA  : #%u frame %d at %d:%d ctrl %02X %06X -> %06X %d x %d, %d T%s\n", k, r->fcnt,
				r->line, r->dot, r->ctrl, r->src, r->dst, r->len, r->num, r->dur, r->cut ? " cut" : "");
		}
	}
	const char* dump = getenv("DUMP");
	if (dump) {
		dump_ppm(comp, dump);
		printf("DUMP : bpl %d full %dx%d\n", bytesPerLine, comp->vid->full.x, comp->vid->full.y);
	}
	if (getenv("TSU")) {
		int nl = 0, nn = 0;
		for (int j = 0; j < 0x200; j++) {nl += !!comp->vid->line[j]; nn += !!comp->vid->tsconf.tsNext[j];}
		printf("TSU  : win %d,%d %dx%d tcfg %02X sgp %02X vconf %02X line %d next %d ray %d\n",
			comp->vid->tsconf.tsXPos, comp->vid->tsconf.tsYPos, comp->vid->tsconf.tsSize.x, comp->vid->tsconf.tsSize.y,
			comp->vid->tsconf.tconfig, comp->vid->tsconf.SGPage, comp->vid->tsconf.p00af, nl, nn, comp->vid->ray.y);
		printf("TSU  : line[96..111]:");
		for (int j = 96; j < 112; j++) printf(" %02X", comp->vid->line[j]);
		printf("  mode %d nogfx %d pal15 %08X pal3F %08X\n", comp->vid->vmode, comp->vid->nogfx, comp->vid->pal[0x15], comp->vid->pal[0x3f]);
	}
	printf("CRAM :");
	for (int j = 0; j < 16; j++) printf(" %02X", comp->vid->tsconf.cram[j]);
	printf("\nLINB :");
	for (int j = 0; j < 16; j++) printf(" %02X", comp->vid->linb[j]);
	printf("\nSFILE:");
	for (int j = 0; j < 16; j++) printf(" %02X", comp->vid->tsconf.sfile[j]);
	printf("\n");
	compDestroy(comp);
	return 0;
}
