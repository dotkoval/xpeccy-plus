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
	FL_IMA,
	FL_PCIMG,
	FL_SZX,
	FL_POK,
	FL_NEX,
	FL_SNX
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
	FG_POK
};

enum {
	FH_SPECTRUM = (1 << 12),
	FH_ALF,
	FH_SLOTS,
	FH_DRIVE_A,
	FH_DRIVE_B,
	FH_DRIVE_C,
	FH_DRIVE_D
};

void initFileDialog(QWidget*);
void fitFileDialog(QWidget*);
void file_errors(int);
bool file_is_pok(const QString&);	// cheats: the Cheats window opens them, load_file does not
// the save dialog alone, for a file type the tables do not carry
// suggest: a path to start from, folder and name; empty for the last folder
QString file_ask_save(const char* title, const char* filter, const char* ext, const QString& suggest = QString(), const QString& own = QString());
// the open dialog alone: the path, with id and drv set to what was picked in it
QString file_ask_open(Computer*, int* id, int* drv);
QString file_ask_load(const char* title, const char* filter);
int load_file(Computer* comp, const char* name, int id, int drv);
// An archive's images the request can take (id and drv as load_file has them), in
// natural order; empty, err set, when none
QStringList media_candidates(Computer*, const QString& archive, int id, int drv, QString* err);
// the image to open from an archive, as an archive path: the only one, the one picked
// when ask, the first otherwise; empty when none or the user cancelled (said why)
QString media_from_archive(Computer*, const QString& archive, int id, int drv, bool ask);
// the last open the user made, which Reload makes again
typedef struct {
	QString path;
	int id;
	int drv;
	int run;
	bool pinned;	// on the running machine, whatever the file wants
} xMediaOpen;
// a file the user opened: kept for Reload, and its labels come with it
void media_opened(const QString& path, int id, int drv, int run, bool pinned);
xMediaOpen media_last_open();
// a snapshot loaded since the last call, empty when none
QString file_take_snapshot();
// the last recording opened for playback
QString rzx_current();
// the image in use, as the window title names it; empty when none
QString media_current();
QString media_image_name();
void media_set_current(const QString& path);
// the machine that file should be opened on, before it is (xcore/filemachine.h):
// *mac comes back empty to keep the running one, false means do not open it
bool media_machine(Computer*, const QString& path, int id, int drv, int run, std::string* mac);
// a tape, or a disk going into drive A: there is a choice between running it and mounting it
bool media_runnable(Computer*, const QString& path, int id, int drv);
bool media_to_drive(Computer*, const QString& path, int id);	// a disk image, or a file put on one
// AS_* the last loaded file would need to start, see xcore/autostart.h
int file_autostart_kind();
// reset the machine and start what was just opened, if run says so
void media_autorun(Computer*, int run);
// a run with no window: nothing is asked or shown, the log says what was refused
void media_autorun_headless(int on);
// drop that record: media a profile puts back was not opened by the user
void media_autorun_forget();
// live: the machine was running, so a snapshot may run it on to a better moment;
// suggest: where the save dialog starts, folder and name, when name is empty
int save_file(Computer* comp, const char* name, int id, int drv, int live = 0, const QString& suggest = QString());
// write back to path, or ask where when its format cannot be written
int save_back(Computer*, const char* path, int id, int drv);
// One slot per machine: kept in memory, and as an .szx too, so it outlives the
// session. Save: 0 failed, 1 memory only, 2 memory and file. Load: 0 nothing to
// load, 1 done.
int quick_save(Computer*);
int quick_load(Computer*);
int quick_undo(Computer*);

// a changed disk or tape asked about: ERR_OK when it is safe to lose
int saveChangedDisk(Computer*,int);
int saveChangedTape(Computer*);
// the Drives menu and the Disk manager name what is in a drive alike
QString drive_media(const char* path, bool in);
int drive_count(Computer*);
