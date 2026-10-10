// Headless ZX Evo BaseConf bench: runs a raw Z80 binary on libxpeccy's
// Baseconf core and prints the mailbox the program fills in.
//
//	baseconf-bench <prog.bin> [frames]
//
// XPECCY_ROOT is the source tree, for config/roms/zxevo-fe.rom and the sgen.rom
// font (default: the current folder). The machine is stood where a 128K
// snapshot would be (hw->snapmap: the pager on, 128 rom in window 0, pages
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

#include "../bench.h"

// where the ray is, in dots since the machine started
static long long dot_pos(Computer* comp) {
	vid_unlazy(comp->vid);
	return (long long)comp->vid->fcnt * comp->vid->dotPerFrame + comp->vid->ray.y * comp->vid->full.x + comp->vid->ray.x;
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
	// set up by hand after res/machines/evo-baseconf.conf: a change there does not reach it
	vLayout lay = {{448, 320}, {52, 48}, {88, 32}, {256, 192}, {2, 0}, 64};	// ULA.Evo
	comp_set_layout(comp, &lay);
	bytesPerLine = comp->vid->full.x * 8;
	memSetSize(comp->mem, MEM_4M, MEM_512K);
	if (!load_rom(comp, root, "zxevo-fe.rom"))
		return 1;
	snprintf(path, sizeof(path), "%s/config/roms/sgen.rom", root);
	vid_fnt_load(comp->vid, path);
	difSetHW(comp->dif, DIF_BDI);
	comp->sdrv->type = SDRV_COVOX;		// soundrive = covox
	ide_set_type(comp->ide, IDE_NEMO_EVO);
	compSetBaseFrq(comp, 3.5);
	compReset(comp, RES_DEFAULT);
	xhost_time_fixed = 1759665600;		// Sunday 2025-10-05 12:00 UTC: runs repeat
	comp->fbus = FBUS_NONE;
	comp_kbd_release(comp);			// a zeroed matrix reads as every key down
	comp_snap_map(comp);
	// a colour of its own for every cell, so PIX can tell them apart
	for (int i = 0; i < 16; i++) {
		xColor c = {(unsigned char)(i * 16), (unsigned char)(i * 8), (unsigned char)(255 - i * 16)};
		vid_set_col(comp->vid, i, c);
	}

	unsigned char* dst = comp->mem->ramData + (2 << 14);
	FILE* f = fopen(argv[1], "rb");
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
	cpu_start(comp);

	int fstart = comp->vid->fcnt;
	int tick0 = comp->tickCount;
	long long dot0 = dot_pos(comp);
	while ((mbox_rd(comp, MBOX) != 0xaa) && (comp->vid->fcnt - fstart < frames))
		compExec(comp);
	long long ticks = comp->tickCount - tick0;
	printf("%s: %zu bytes, %s after %d frames, pc=%04X\n", base_name(argv[1]), n,
		(mbox_rd(comp, MBOX) == 0xaa) ? "done" : "TIMEOUT",
		comp->vid->fcnt - fstart, comp->cpu->regPC);
	if (getenv("TICKS")) printf("TICKS: %lld\n", ticks);
	if (getenv("DOTS")) printf("DOTS : %lld\n", dot_pos(comp) - dot0);
	mbox_print(comp);
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
	if (dump) dump_ppm(comp, dump);
	compDestroy(comp);
	return 0;
}
