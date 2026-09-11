#pragma once

#include <QWidget>
#include <string>

#include "libxpeccy/filetypes/filetypes.h"
#include "libxpeccy/spectrum.h"

enum {
	FL_NONE = 0,
	FL_TAP,
	FL_TZX,
	FL_WAV,
	FL_SCL,
	FL_TRD,
	FL_FDI,
	FL_UDI,
	FL_DSK,
	FL_TD0,
	FL_SNA,
	FL_Z80,
	FL_SPG,
	FL_RZX,
	FL_HOBETA,
	FL_RAW,
	FL_SLT_BIN,
	FL_SLT_ROM,
	FL_GB,
	FL_GBC,
	FL_MSX,
	FL_MX1,
	FL_MX2,
	FL_CAS,
	FL_NES,
	FL_T64,
	FL_C64TAP,
	FL_C64PRG,
	FL_BKBIN,
	FL_BKRAWTAP,
	FL_BKIMG,
	FL_BKBKD,
	FL_RKS,
	FL_IMA,
	FL_PCIMG,
	FL_98FDI,
	FL_98HDM,
	FL_98HDI
};

enum {
	FG_DISK = -1,
	FG_ALL = 0,
	FG_TAPE = (1 << 10),
	FG_DISK_A,
	FG_DISK_B,
	FG_DISK_C,
	FG_DISK_D,
	FG_SNAPSHOT,
	FG_RZX,
	FG_HOBETA,
	FG_RAW,
	FG_IF2_ROM,
	FG_GAMEBOY,
	FG_MSX,
	FG_MSXTAPE,
	FG_NES,
	FG_CMDTAPE,
	FG_CMDSNAP,
	FG_BKDATA,
	FG_BKTAPE,
	FG_BKRAW,
	FG_BKDISK,
	FG_RKSMEM,
	FG_RKSTAP,
	FG_PCDISK,
	FG_98DISK
};

enum {
	FH_SPECTRUM = (1 << 12),
	FH_ALF,
	FH_MSX,
	FH_GAMEBOY,
	FH_NES,
	FH_CMD,
	FH_BK,
	FH_SPCLST,
	FH_PC,
	FH_98XX,
	FH_SLOTS,
	FH_DRIVE_A,
	FH_DRIVE_B,
	FH_DRIVE_C,
	FH_DRIVE_D
};

void initFileDialog(QWidget*);
void fitFileDialog(QWidget*);
// the open dialog alone: the path, with id and drv set to what was picked in it
QString file_ask_open(Computer*, int* id, int* drv);
int load_file(Computer* comp, const char* name, int id, int drv);
// the machine that file should be opened on, before it is (xcore/filemachine.h):
// *mac comes back empty to keep the running one, false means do not open it
bool media_machine(Computer*, const QString& path, int id, int drv, int run, std::string* mac);
// AS_* the last loaded file would need to start, see xcore/autostart.h
int file_autostart_kind();
// reset the machine and start what was just opened, if run says so
void media_autorun(Computer*, int run);
// drop that record: media a profile puts back was not opened by the user
void media_autorun_forget();
int save_file(Computer* comp, const char* name, int id, int drv);

int saveChangedDisk(Computer*,int);
