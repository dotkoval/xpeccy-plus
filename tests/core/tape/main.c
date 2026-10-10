// Headless tape bench: reads the images mktape.py writes through libxpeccy's own
// TAP and TZX readers and prints, a section each:
//
//	<img>_blocks	every block as built: pulse lengths, counts, a hash of all pulses
//	<img>_bytes	the bytes decoded back from each block against what mktape wrote
//	<img>_replay	the pulse lengths a loader polling every 16 T at 3.5 MHz measures
//			through tapSync(), block by block
//	tap_wav		test.tap -> saveWAV -> loadWAV -> saveTAP gives the same file
//
//	tape-bench <folder>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spectrum.h"
#include "filetypes/filetypes.h"

#define CPU_HZ	3500000.0
#define POLL_T	16

static unsigned fnv(const unsigned char* p, int n) {
	unsigned h = 2166136261u;
	for (int i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
	return h;
}

static Computer* load(const char* dir, const char* name) {
	char path[4096];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	Computer* comp = compCreate();
	tape_set_tick_ns(comp->tape, 1e9 / CPU_HZ);
	const char* ext = strrchr(name, '.');
	int err = (ext && !strcmp(ext, ".tzx")) ? loadTZX(comp, path, 0) : loadTAP(comp, path, 0);
	if (err != ERR_OK) {
		printf("%s: load error %d\n", name, err);
		return NULL;
	}
	return comp;
}

static void print_blocks(Tape* tape) {
	printf("blocks: %d\n", tape->blkCount);
	for (int i = 0; i < tape->blkCount; i++) {
		TapeBlock* b = &tape->blkData[i];
		TapeBlockInfo inf = tapGetBlockInfo(tape, i);
		unsigned h = 2166136261u;
		for (int j = 0; j < b->sigCount; j++) {
			unsigned char s[5] = {b->data[j].size & 0xff, (b->data[j].size >> 8) & 0xff,
				(b->data[j].size >> 16) & 0xff, b->data[j].size >> 24, b->data[j].vol};
			for (int k = 0; k < 5; k++) h = (h ^ s[k]) * 16777619u;
		}
		printf("%2d: sig=%d pilot=%d s1=%d s2=%d l0=%d l1=%d data@%d time=%d hdr=%d bytes=%d stop=%d stop48=%d size=%d",
			i, b->sigCount, b->plen, b->s1len, b->s2len, b->len0, b->len1, b->dataPos, b->time,
			b->isHeader, b->hasBytes, b->stopMark, b->stop48, inf.size);
		if (inf.type == TAPE_HEAD)
			printf(" head=%d '%.10s' dlen=%d par1=%d", inf.htype, (const char*)inf.name, inf.dlen, inf.par1);
		if (inf.text[0]) printf(" text='%s'", inf.text);
		printf(" pulses=%08X\n", h);
		// run-length view of the first pulses
		printf("    head:");
		int j = 0, shown = 0;
		while ((j < b->sigCount) && (shown < 8)) {
			int sz = b->data[j].size, cnt = 1;
			while ((j + cnt < b->sigCount) && (b->data[j + cnt].size == sz)) cnt++;
			printf(" %dx%d", sz, cnt);
			j += cnt;
			shown++;
		}
		printf("%s\n", (j < b->sigCount) ? " ..." : "");
	}
}

// the blocks that carry bytes, in order, against "<len> <fnv>" lines of <img>.bytes
static void check_bytes(Tape* tape, const char* dir, const char* name) {
	char path[4096];
	snprintf(path, sizeof(path), "%s/%s.bytes", dir, name);
	FILE* f = fopen(path, "r");
	if (!f) {
		printf("no %s.bytes\n", name);
		return;
	}
	static unsigned char buf[0x10000];
	for (int i = 0; i < tape->blkCount; i++) {
		TapeBlock* b = &tape->blkData[i];
		if (!b->hasBytes) continue;
		// size counts the bytes between the flag and the checksum
		int n = tapGetBlockData(tape, i, buf, tapGetBlockInfo(tape, i).size + 2);
		unsigned h = fnv(buf, n);
		int wlen;
		unsigned wfnv;
		if (fscanf(f, "%d %x", &wlen, &wfnv) != 2) {
			printf("%2d: %d bytes %08X, not written by mktape\n", i, n, h);
			continue;
		}
		printf("%2d: %d bytes %08X %s\n", i, n, h, ((n == wlen) && (h == wfnv)) ? "as written" : "DIFFERS from what was written");
	}
	int wlen;
	unsigned wfnv;
	while (fscanf(f, "%d %x", &wlen, &wfnv) == 2)
		printf("written but not decoded: %d bytes %08X\n", wlen, wfnv);
	fclose(f);
}

// Each block from its start until the tape moves to the next one, the ear sampled
// every POLL_T; lengths within 8 T of each other count as one.
static void replay(Tape* tape) {
	double nsPerT = 1e9 / CPU_HZ;
	for (int blk = 0; blk < tape->blkCount; blk++) {
		tapRewind(tape, blk);
		tape->on = 0;
		tapPlay(tape);
		long long tick = 0, lastT = 0;
		double acc = 0;
		int lastLev = tape->volPlay & 0x80;
		int edges = 0;
		#define NH 32
		long long hv[NH];
		int hc[NH], hn = 0;
		while (tape->on && (tape->block == blk) && (tick < 200000000LL)) {
			acc += POLL_T * nsPerT;
			int ns = (int)acc;
			acc -= ns;
			tapSync(tape, ns);
			tick += POLL_T;
			int lev = tape->volPlay & 0x80;
			if (lev == lastLev) continue;
			long long d = tick - lastT;
			lastT = tick;
			lastLev = lev;
			edges++;
			int k = 0;
			while ((k < hn) && ((hv[k] > d + 8) || (hv[k] < d - 8))) k++;
			if ((k == hn) && (hn < NH)) {
				hv[hn] = d;
				hc[hn++] = 0;
			}
			if (k < hn) hc[k]++;
		}
		printf("%2d: %d edges:", blk, edges);
		for (int k = 0; k < hn; k++) printf(" %lldTx%d", hv[k], hc[k]);
		printf("\n");
	}
}

static long fsize(const char* path, unsigned char** data) {
	FILE* f = fopen(path, "rb");
	if (!f) return -1;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	*data = malloc(n ? n : 1);
	n = (long)fread(*data, 1, n, f);
	fclose(f);
	return n;
}

static void wav_round_trip(Computer* comp, const char* dir) {
	char tap[4096], wav[4096], back[4096];
	snprintf(tap, sizeof(tap), "%s/test.tap", dir);
	snprintf(wav, sizeof(wav), "%s/test.wav", dir);
	snprintf(back, sizeof(back), "%s/back.tap", dir);
	if (saveWAV(comp, wav, 0) != ERR_OK) {
		printf("saveWAV failed\n");
		return;
	}
	Computer* c2 = compCreate();
	tape_set_tick_ns(c2->tape, 1e9 / CPU_HZ);
	if (loadWAV(c2, wav, 0) != ERR_OK) {
		printf("loadWAV failed\n");
		return;
	}
	printf("wav: %d blocks\n", c2->tape->blkCount);
	if (saveTAP(c2, back, 0) != ERR_OK) {
		printf("saveTAP failed\n");
		return;
	}
	unsigned char *a, *b;
	long na = fsize(tap, &a), nb = fsize(back, &b);
	long i = 0;
	while ((i < na) && (i < nb) && (a[i] == b[i])) i++;
	if ((na == nb) && (i == na))
		printf("tap -> wav -> tap: identical, %ld bytes\n", na);
	else
		printf("tap -> wav -> tap: DIFFERS at byte %ld (%ld vs %ld bytes)\n", i, na, nb);
	compDestroy(c2);
}

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: tape-bench <folder written by mktape.py>\n");
		return 2;
	}
	const char* dir = argv[1];
	static const char* imgs[] = {"test.tap", "test.tzx"};
	for (int i = 0; i < 2; i++) {
		Computer* comp = load(dir, imgs[i]);
		if (!comp) return 1;
		const char* tag = (i == 0) ? "tap" : "tzx";
		printf("== %s_blocks\n", tag);
		print_blocks(comp->tape);
		printf("== %s_bytes\n", tag);
		check_bytes(comp->tape, dir, imgs[i]);
		printf("== %s_replay\n", tag);
		replay(comp->tape);
		compDestroy(comp);
	}
	printf("== tap_wav\n");
	Computer* tapComp = load(dir, imgs[0]);
	if (tapComp) wav_round_trip(tapComp, dir);
	fflush(stdout);
	return 0;
}
