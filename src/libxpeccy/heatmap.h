#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// memory access counters: separate read/write/exec hit counts per cell.
// arrays are allocated dynamically, sized to match the currently
// active hardware's actual ram/rom capacity (see comp_heat_sync).
typedef struct {
	unsigned int* rd;
	unsigned int* wr;
	unsigned int* ex;
	int size;		// number of cells currently allocated
} xHeatBank;

enum {
	HEAT_RD = 0,
	HEAT_WR,
	HEAT_EX
};

typedef struct Computer Computer;

// one cell's counters, as read back for display. `valid` is 0 when the address
// is not backed by tracked memory (io/slot cells are never counted)
typedef struct {
	unsigned int rd;
	unsigned int wr;
	unsigned int ex;
	int valid;
} xHeatCell;

void heatBankFree(xHeatBank*);

void comp_heat_sync(Computer*);		// (re)allocate ram/rom banks to match current mem sizes
void comp_heat_reset(Computer*);		// zero all counters, keep allocation
void comp_heat_free(Computer*);		// release ram/rom banks (call on compDestroy)
void comp_heat_hit(Computer*, int adr, int kind);
int comp_heat_save(Computer*, const char* path);	// returns 0 on success

xHeatCell comp_heat_cpu(Computer*, int adr);			// cell behind a cpu address, through the current paging
xHeatCell comp_heat_phys(Computer*, int type, int abs);		// cell by absolute address in MEM_RAM/MEM_ROM

#ifdef __cplusplus
}
#endif
