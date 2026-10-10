// What the mailbox benches share: a Z80 program at #8000 of RAM page 2 reports
// through the mailbox at #BF00..#BF7F (byte 0 = #AA ends the run, bytes 1.. are
// printed), and the frame it leaves can be probed or dumped.

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spectrum.h"
#include "cpu/Z80/z80.h"		// the register names

#define MBOX	0xbf00

static int mbox_rd(Computer* comp, int adr) {
	return comp->mem->ramData[(2 << 14) + (adr & 0x3fff)];
}

// the 16-byte rows of the mailbox after its first byte that are not all zero
static void mbox_print(Computer* comp) {
	for (int i = 1; i < 0x80; i += 16) {
		int any = 0;
		for (int j = 0; j < 16; j++) any |= mbox_rd(comp, MBOX + i + j);
		if (!any) continue;
		printf("%04X:", MBOX + i);
		for (int j = 0; j < 16; j++) printf(" %02X", mbox_rd(comp, MBOX + i + j));
		printf("\n");
	}
}

static const char* base_name(const char* path) {
	const char* p = path;
	for (const char* s = path; *s; s++)
		if ((*s == '/') || (*s == '\\')) p = s + 1;
	return p;
}

// <root>/config/roms/<name> into the ROM, 0 when it is not there
static int load_rom(Computer* comp, const char* root, const char* name) {
	char path[4096];
	snprintf(path, sizeof(path), "%s/config/roms/%s", root, name);
	FILE* f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "no rom '%s'\n", path);
		return 0;
	}
	if (fread(comp->mem->romData, 1, MEM_512K, f) == 0)
		fprintf(stderr, "empty rom\n");
	fclose(f);
	return 1;
}

// the program's start: PC = #8000, SP = the mailbox, DI, IM 1
static void cpu_start(Computer* comp) {
	comp->cpu->regPC = 0x8000;
	comp->cpu->regSP = MBOX;
	comp->cpu->flgIFF1 = 0;
	comp->cpu->flgIFF2 = 0;
	comp->cpu->regIM = 1;
}

// the first palette index of colour c
static int pal_index(Computer* comp, int32_t c) {
	for (int i = 0; i < 256; i++)
		if (comp->vid->pal[i] == c) return i;
	return -1;
}

// env = "x,y x,y ...": palette index of dots of the last whole frame, step bytes a dot
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

// the whole raster as a PPM
static void dump_ppm(Computer* comp, const char* path) {
	FILE* df = fopen(path, "wb");
	if (!df) return;
	int w = bytesPerLine / 8, h = comp->vid->full.y;
	fprintf(df, "P6\n%d %d\n255\n", w, h);
	for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
		unsigned char* p = bufimg + y * bytesPerLine + x * 8;
		fputc(p[0], df); fputc(p[1], df); fputc(p[2], df);
	}
	fclose(df);
}
