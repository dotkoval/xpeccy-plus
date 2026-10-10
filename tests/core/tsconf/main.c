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
// the TSU state.

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

// PIX="x,y x,y ...": palette index of dots of the last whole frame (first index of that colour)
static void probe(Computer* comp, const char* env, const char* tag, int step) {
	const char* pix = getenv(env);
	if (!pix) return;
	int x, y, k;
	printf("%s:", tag);
	while (sscanf(pix, "%d,%d%n", &x, &y, &k) == 2) {
		int32_t c = *(int32_t*)(bufimg + y * bytesPerLine + x * step);
		int idx = -1;
		for (int i = 0; i < 256 && idx < 0; i++)
			if (comp->vid->pal[i] == c) idx = i;
		printf(" %d,%d=%02X", x, y, idx & 0xff);
		pix += k;
		while (*pix == ' ') pix++;
	}
	printf("\n");
}

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: tsconf-bench prog.bin [frames]\n");
		return 2;
	}
	int frames = (argc > 2) ? atoi(argv[2]) : 50;
	const char* root = getenv("XPECCY_ROOT");
	char romf[4096];
	snprintf(romf, sizeof(romf), "%s/config/roms/tsconf.rom", root ? root : ".");
	xhost_time_fixed = 1759665600;		// Sunday 2025-10-05 12:00 UTC: runs repeat

	Computer* comp = compCreate();
	if (!compSetHardware(comp, "TSConf")) {
		fprintf(stderr, "no TSConf core\n");
		return 1;
	}
	vLayout lay = {{448, 320}, {52, 48}, {88, 32}, {256, 192}, {0, 0}, 64};
	comp_set_layout(comp, &lay);
	bytesPerLine = comp->vid->full.x * 8;	// as emulwin.cpp sets it: a whole raster row per image row
	memSetSize(comp->mem, MEM_4M, MEM_512K);
	FILE* f = fopen(romf, "rb");
	if (!f) {
		fprintf(stderr, "no rom '%s'\n", romf);
		return 1;
	}
	if (fread(comp->mem->romData, 1, MEM_512K, f) == 0)
		fprintf(stderr, "empty rom\n");
	fclose(f);
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
		f = fopen(argv[1], "rb");
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
		comp->cpu->regPC = 0x8000;
		comp->cpu->regSP = MBOX;
		comp->cpu->flgIFF1 = 0;
		comp->cpu->flgIFF2 = 0;
		comp->cpu->regIM = 1;
	}

	int fstart = comp->vid->fcnt;
	while ((mbox_rd(comp, MBOX) != 0xaa) && (comp->vid->fcnt - fstart < frames))
		compExec(comp);
	printf("%s: %zu bytes, %s after %d frames, pc=%04X\n", base_name(argv[1]), n,
		(mbox_rd(comp, MBOX) == 0xaa) ? "done" : "TIMEOUT",
		comp->vid->fcnt - fstart, comp->cpu->regPC);
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
	printf("BRD  : %02X\n", comp->vid->brdcol);
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
