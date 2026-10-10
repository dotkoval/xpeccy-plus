// Headless ZX Evo BaseConf bench: runs a raw Z80 binary on libxpeccy's
// Baseconf core and prints the mailbox the program fills in.
//
//	baseconf-bench <prog.bin> [frames]
//
// XPECCY_ROOT is the source tree, for config/roms/zxevo-fe.rom, the sgen.rom
// font and res/machines/evo-baseconf.conf (default: the current folder). Only
// floatbus is read from that definition; the rest is set up by hand after it:
// layout ULA.Evo, 4 MB, Beta disk, Nemo-Evo IDE, covox. It is stood where a
// 128K snapshot would be (hw->snapmap: the pager on, 128 rom in window 0, pages
// 5,2,0 above it).
//
// The program is loaded at #8000 (RAM page 2), SP = #BF00, DI, IM 1, and
// #9000..#9FFF of it is also the NMI page (ram FF) from #0000. It reports
// through the mailbox at #BF00..#BF7F: byte 0 = #AA ends the run, bytes 1..
// are printed.
//
// Environment: KEYS=n presses and releases 'A' n times on the ps/2 keyboard
// first; HDD=img puts an image on the IDE master; SDCARD=img mounts a card;
// PIX="x,y ..." prints palette indexes of dots of the last frame (PIXH: in
// half dots); PAL=1 the 16 cells; TICKS=1 the T taken; DOTS=1 the same in
// dots; RESETAFTER=1 the state a reset leaves; DUMP=f.ppm writes the frame.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spectrum.h"
#include "cpu/Z80/z80.h"
#include "filetypes/filetypes.h"

#define MBOX	0xbf00

static int mbox_rd(Computer* comp, int adr) {
	return comp->mem->ramData[(2 << 14) + (adr & 0x3fff)];
}

static const char* base_name(const char* path) {
	const char* p = path;
	for (const char* s = path; *s; s++)
		if ((*s == '/') || (*s == '\\')) p = s + 1;
	return p;
}

static int pal_index(Computer* comp, int32_t c) {
	for (int i = 0; i < 256; i++)
		if (comp->vid->pal[i] == c) return i;
	return -1;
}

static void probe(Computer* comp, const char* env, const char* tag, int step) {
	const char* pix = getenv(env);
	if (!pix) return;
	int x, y, k;
	printf("%s:", tag);
	while (sscanf(pix, "%d,%d%n", &x, &y, &k) == 2) {
		int32_t c = *(int32_t*)(bufimg + y * bytesPerLine + x * step);
		printf(" %d,%d=%02X", x, y, pal_index(comp, c) & 0xff);
		pix += k;
		while (*pix == ' ') pix++;
	}
	printf("\n");
}

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: baseconf-bench prog.bin [frames]\n");
		return 2;
	}
	int frames = (argc > 2) ? atoi(argv[2]) : 50;
	const char* root = getenv("XPECCY_ROOT");
	if (!root) root = ".";
	char path[4096];

	Computer* comp = compCreate();
	if (!compSetHardware(comp, "Baseconf")) {
		fprintf(stderr, "no Baseconf core\n");
		return 1;
	}
	// ULA.Evo from res/layouts.conf
	vLayout lay = {{448, 320}, {52, 48}, {88, 32}, {256, 192}, {2, 0}, 64};
	comp_set_layout(comp, &lay);
	bytesPerLine = comp->vid->full.x * 8;
	memSetSize(comp->mem, MEM_4M, MEM_512K);
	snprintf(path, sizeof(path), "%s/config/roms/zxevo-fe.rom", root);
	FILE* f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "no rom '%s'\n", path);
		return 1;
	}
	if (fread(comp->mem->romData, 1, MEM_512K, f) == 0)
		fprintf(stderr, "empty rom\n");
	fclose(f);
	snprintf(path, sizeof(path), "%s/config/roms/sgen.rom", root);
	vid_fnt_load(comp->vid, path);
	difSetHW(comp->dif, DIF_BDI);
	comp->sdrv->type = SDRV_COVOX;		// soundrive = covox
	ide_set_type(comp->ide, IDE_NEMO_EVO);
	compSetBaseFrq(comp, 3.5);
	compReset(comp, RES_DEFAULT);
	xhost_time_fixed = 1759665600;		// Sunday 2025-10-05 12:00 UTC: runs repeat
	// what the machine's definition says about the floating bus
	snprintf(path, sizeof(path), "%s/res/machines/evo-baseconf.conf", root);
	FILE* mf = fopen(path, "r");
	if (mf) {
		char line[256], val[32];
		while (fgets(line, sizeof(line), mf)) {
			if (sscanf(line, " floatbus = %31s", val) == 1) {
				comp->fbus = !strcmp(val, "attr") ? FBUS_ATTR : !strcmp(val, "ula") ? FBUS_ULA
					: !strcmp(val, "asic") ? FBUS_ASIC : FBUS_NONE;
			}
		}
		fclose(mf);
	} else {
		fprintf(stderr, "no machine file '%s'\n", path);
		return 1;
	}
	kbdReleaseAll(comp->keyb);		// a zeroed matrix reads as every key down
	if (comp->hw->snapmap) comp->hw->snapmap(comp);
	// a colour of its own for every cell, so PIX can tell them apart
	for (int i = 0; i < 16; i++) {
		xColor c = {(unsigned char)(i * 16), (unsigned char)(i * 8), (unsigned char)(255 - i * 16)};
		vid_set_col(comp->vid, i, c);
	}

	unsigned char* dst = comp->mem->ramData + (2 << 14);
	f = fopen(argv[1], "rb");
	if (!f) {
		fprintf(stderr, "can't open %s\n", argv[1]);
		return 1;
	}
	size_t n = fread(dst, 1, 0x3f00, f);
	fclose(f);
	memset(dst + 0x3f00, 0, 0x100);
	memcpy(comp->mem->ramData + (0xff << 14), dst + 0x1000, 0x1000);
	// KEYS=n: press and release 'A' n times on the ps/2 keyboard (set 2: 1C, F0 1C)
	const char* keys = getenv("KEYS");
	if (keys) {
		comp->keyb->pcmode = KBD_AT;
		for (int i = atoi(keys); i > 0; i--) {
			keyEntry e;
			memset(&e, 0, sizeof(e));
			e.key = -1;
			e.atCode = 0x1c;
			comp->hw->keyp(comp, &e);
			comp->hw->keyr(comp, &e);
		}
	}
	const char* sdimg = getenv("SDCARD");
	if (sdimg) sdcSetImage(comp->sdc, sdimg);
	const char* hdd = getenv("HDD");
	if (hdd) {
		comp->ide->master->type = IDE_ATA;
		ideSetImage(comp->ide, IDE_MASTER, hdd);
	}

	comp->cpu->regPC = 0x8000;
	comp->cpu->regSP = MBOX;
	comp->cpu->flgIFF1 = 0;
	comp->cpu->flgIFF2 = 0;
	comp->cpu->regIM = 1;

	int fstart = comp->vid->fcnt;
	int tick0 = comp->tickCount;
	vid_unlazy(comp->vid);
	long long dot0 = (long long)comp->vid->fcnt * comp->vid->dotPerFrame + comp->vid->ray.y * comp->vid->full.x + comp->vid->ray.x;
	while ((mbox_rd(comp, MBOX) != 0xaa) && (comp->vid->fcnt - fstart < frames))
		compExec(comp);
	long long ticks = comp->tickCount - tick0;
	printf("%s: %zu bytes, %s after %d frames, pc=%04X\n", base_name(argv[1]), n,
		(mbox_rd(comp, MBOX) == 0xaa) ? "done" : "TIMEOUT",
		comp->vid->fcnt - fstart, comp->cpu->regPC);
	if (getenv("TICKS")) printf("TICKS: %lld\n", ticks);
	if (getenv("DOTS")) {
		vid_unlazy(comp->vid);
		long long dot1 = (long long)comp->vid->fcnt * comp->vid->dotPerFrame + comp->vid->ray.y * comp->vid->full.x + comp->vid->ray.x;
		printf("DOTS : %lld\n", dot1 - dot0);
	}
	for (int i = 1; i < 0x80; i += 16) {
		int any = 0;
		for (int j = 0; j < 16; j++) any |= mbox_rd(comp, MBOX + i + j);
		if (!any) continue;
		printf("%04X:", MBOX + i);
		for (int j = 0; j < 16; j++) printf(" %02X", mbox_rd(comp, MBOX + i + j));
		printf("\n");
	}
	probe(comp, "PIX", "PIX  ", 8);
	probe(comp, "PIXH", "PIXH ", 4);
	printf("BRD  : %02X  MODE: %d  TURBO: %.0f  BEEP: %d  COVOX: %02X\n", comp->vid->brdcol, comp->vid->vmode, comp->hwMul, comp->beep->lev, comp->sdrv->chan[0] & 0xff);
	if (getenv("RESETAFTER")) {
		compReset(comp, RES_DEFAULT);
		printf("RESET: MODE: %d  TURBO: %.0f\n", comp->vid->vmode, comp->hwMul);
	}
	if (getenv("PAL")) {
		printf("PAL  :");
		for (int j = 0; j < 16; j++) printf(" %06X", comp->vid->pal[j] & 0xffffff);
		printf("\n");
	}
	const char* dump = getenv("DUMP");
	if (dump) {
		FILE* df = fopen(dump, "wb");
		int w = bytesPerLine / 8, h = comp->vid->full.y;
		fprintf(df, "P6\n%d %d\n255\n", w, h);
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
			unsigned char* p = bufimg + y * bytesPerLine + x * 8;
			fputc(p[0], df); fputc(p[1], df); fputc(p[2], df);
		}
		fclose(df);
	}
	compDestroy(comp);
	return 0;
}
