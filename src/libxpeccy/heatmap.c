#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include "heatmap.h"
#include "spectrum.h"

void heatBankFree(xHeatBank* bank) {
	free(bank->rd);
	free(bank->wr);
	free(bank->ex);
	bank->rd = NULL;
	bank->wr = NULL;
	bank->ex = NULL;
	bank->size = 0;
}

static void heatBankSync(xHeatBank* bank, int newSize) {
	if (bank->size == newSize)
		return;
	heatBankFree(bank);
	if (newSize > 0) {
		bank->rd = (unsigned int*)calloc(newSize, sizeof(unsigned int));
		bank->wr = (unsigned int*)calloc(newSize, sizeof(unsigned int));
		bank->ex = (unsigned int*)calloc(newSize, sizeof(unsigned int));
		bank->size = newSize;
	}
}

static void heatBankReset(xHeatBank* bank) {
	if (bank->size <= 0) return;
	memset(bank->rd, 0x00, bank->size * sizeof(unsigned int));
	memset(bank->wr, 0x00, bank->size * sizeof(unsigned int));
	memset(bank->ex, 0x00, bank->size * sizeof(unsigned int));
}

static void heatBankHit(xHeatBank* bank, int idx, int kind) {
	if ((idx < 0) || (idx >= bank->size)) return;
	unsigned int* arr;
	switch (kind) {
		case HEAT_RD: arr = bank->rd; break;
		case HEAT_WR: arr = bank->wr; break;
		case HEAT_EX: arr = bank->ex; break;
		default: return;
	}
	if (arr[idx] < UINT_MAX)
		arr[idx]++;
}

void comp_heat_sync(Computer* comp) {
	heatBankSync(&comp->heatRam, comp->mem->ramMask + 1);
	heatBankSync(&comp->heatRom, comp->mem->romMask + 1);
}

void comp_heat_reset(Computer* comp) {
	heatBankReset(&comp->heatRam);
	heatBankReset(&comp->heatRom);
}

void comp_heat_free(Computer* comp) {
	heatBankFree(&comp->heatRam);
	heatBankFree(&comp->heatRom);
}

static const xHeatCell heat_nocell = {0, 0, 0, 0};

// which bank an absolute address lands in, and where in it. NULL when nothing
// is counted there - slot, ext and io cells are not tracked. every read and
// write of the counters goes through here, so the rule lives in one place.

static xHeatBank* heatBankFor(Computer* comp, int type, int abs, int* idx) {
	switch (type) {
		case MEM_RAM: *idx = abs & comp->mem->ramMask; return &comp->heatRam;
		case MEM_ROM: *idx = abs & comp->mem->romMask; return &comp->heatRom;
	}
	return NULL;
}

xHeatCell comp_heat_phys(Computer* comp, int type, int abs) {
	int idx;
	xHeatCell cell = heat_nocell;
	xHeatBank* bank = heatBankFor(comp, type, abs, &idx);
	if (bank && (idx >= 0) && (idx < bank->size)) {
		cell.rd = bank->rd[idx];
		cell.wr = bank->wr[idx];
		cell.ex = bank->ex[idx];
		cell.valid = 1;
	}
	return cell;
}

xHeatCell comp_heat_cpu(Computer* comp, int adr) {
	xAdr xadr = mem_get_xadr(comp->mem, adr);
	return comp_heat_phys(comp, xadr.type, xadr.abs);
}

void comp_heat_hit(Computer* comp, int adr, int kind) {
	int idx;
	xAdr xadr = mem_get_xadr(comp->mem, adr);
	xHeatBank* bank = heatBankFor(comp, xadr.type, xadr.abs, &idx);
	if (bank) heatBankHit(bank, idx, kind);
}

// dump one bank to CSV: "type,page,offset,read,write,exec" per cell, decimal throughout.
// every cell in the bank is written, including untouched ones (all-zero rows), so a reader
// can recover the exact bank size (16k pages * 16384) without any extra size metadata.
static void heatBankSave(FILE* file, xHeatBank* bank, const char* tag) {
	int i;
	for (i = 0; i < bank->size; i++) {
		fprintf(file, "%s,%d,%d,%u,%u,%u\n", tag, i >> 14, i & 0x3fff, bank->rd[i], bank->wr[i], bank->ex[i]);
	}
}

int comp_heat_save(Computer* comp, const char* path) {
	FILE* file = fopen(path, "w");
	if (!file) return -1;
	fprintf(file, "type,page,offset,read,write,exec\n");
	heatBankSave(file, &comp->heatRam, "RAM");
	heatBankSave(file, &comp->heatRom, "ROM");
	fclose(file);
	return 0;
}
