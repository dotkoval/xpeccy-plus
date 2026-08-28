#pragma once

#include <QDialog>
#include <QMainWindow>
#include <QLineEdit>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QKeyEvent>
#include <QTimer>
#include <QItemDelegate>
#include <QMenu>
#include <functional>
#include <QTableWidget>
#if QT_VERSION >= QT_VERSION_CHECK(5,4,0)
#include <QRegularExpressionValidator>
#else
#include <QRegExpValidator>
#endif

#include "../labelist.h"
#include "libxpeccy/spectrum.h"
#include "dbg_widgets.h"
#include "dbg_stack.h"

#include "ui_dumpdial.h"
#include "ui_openDump.h"
// #include "ui_debuger.h"

#include "ui_form_cpu.h"
#include "ui_form_disasm.h"
#include "ui_form_misc.h"

enum {
	DMP_MEM = 1,
	DMP_REG
};

enum {
	DBG_EVENT_STEP = QEvent::User
};

typedef struct {
	QIcon icon;
	QString name;
	QWidget* wid;
} tabDSC;

class DebugWin : public QMainWindow {
	Q_OBJECT
	public:
		DebugWin(QWidget* = NULL);
		~DebugWin();

//		void reject();
		void stop();
		void prewarm();

	signals:
		void closed();
		void wannaKeys();
		void wannaWutch();
		void wannaOptions();
		void needStep();
	public slots:
		void start();
		void onPrfChange();
		void setScrAtr(int, int);
		void updateStyle();
	private:
		unsigned block:1;
		int tabMode;
		cpuCore* curCpuCore;
		QWidget* wid_cpu;
		xDockWidget* wid_cpu_dock;
		int regCols;		// register columns that fit now (1 or 2)
		int regPairW;		// width one 'name + value' pair needs
		int regWideW;		// panel width that fits two columns: max and switch point
		unsigned cpuWideDock:1;	// two panels sit side by side under the cpu dock
		unsigned winShown:1;	// window has been shown: the dock sizes are real
		unsigned reformWait:1;	// a build was asked for before the first show
		unsigned reformPending:1;	// a reflow is already queued
		// tracer
		unsigned trace:1;
		int traceType;
		int traceAdr;
		long tCount;

		QImage scrImg;
		QMap<int, QList<tabDSC> > tablist;

		xDockWidget* wid_dasm_dock;
		QString xmap_path;
		// widgets
		Ui::CPUWidget ui_cpu;
		Ui::DisasmWidget ui_asm;
		Ui::FormDbgMisc ui_misc;
		xDockWidget* wid_misc;		// memmap + ports + signals + ray
		xDockWidget* wid_stack;
		xStackView* wid_stack_view;	// its content, see fillStack
		QDockWidget* wid_anchor_l;	// empty strips at the window edges: something
		QDockWidget* wid_anchor_r;	// to drop a panel next to, see make_edge_anchor
		xDumpWidget* wid_dump;
		xRDumpWidget* wid_rdump;
		xDiskDumpWidget* wid_disk_dump;
		xCmosDumpWidget* wid_cmos_dump;
		xVMemDumpWidget* wid_vmem_dump;
		xZXScrWidget* wid_zxscr;
		xDmaWidget* wid_dma;
		xPitWidget* wid_pit;
		xPicWidget* wid_pic;
		xVgaWidget* wid_vga;
		xPS2Widget* wid_ps2;
		xAYWidget* wid_ay;
		xTapeWidget* wid_tape;
		xFDDWidget* wid_fdd;
		xBreakWidget* wid_brk;
		xGameboyWidget* wid_gb;
		xGBVideoWidget* wid_gbv;
		xPPUWidget* wid_ppu;
		xPalWidget* wid_pal;
		// apu (future)
		xCiaWidget* wid_cia;
		xVicWidget* wid_vic;
		xMMapWidget* wid_mmap;
		QList<xDockWidget*> dockWidgets;

		QList<xLabel*> dbgRegLabs;
		QList<xHexSpin*> dbgRegEdit;
		QList<QCheckBox*> dbgRegBits;

		QList<QLabel*> dbgFlagLabs;
		QList<QCheckBox*> dbgFlagBox;
		QButtonGroup* flagrp;

		QDialog* dumpwin;
		Ui::DumpDial dui;
		QByteArray getDumpData();

		QDialog* openDumpDialog;
		Ui::oDumpDial oui;
		QString dumpPath;

		xMemFiller* memFiller;
		xMemFinder* memFinder;
		MemViewer* memViewer;
		xBrkManager* brkManager;
		xLabeList* labswin;

		QMenu* labMenu;
		QMenu* labSetMenu;

		QMenu* cellMenu;
		void doBreakPoint(unsigned short);
		int getAdr();

		xItemDelegate* xid_none;
		xItemDelegate* xid_byte;
		xItemDelegate* xid_labl;
		xItemDelegate* xid_octw;
		xItemDelegate* xid_dump;

		void fillCPU();
		void fillFlags(const char*);
		void fillMem();
		void fillStack();
		void fillPorts();
		void setPortRow(int, QString, QString);
		void setMiscBlocks();
		void editWatchPorts();
		void setLabelMenu(QWidget*, QString, QString, std::function<void()>);
		void reFormCPU(xRegBunch*);
		void reFormFlags(int);
		void placeReg(xRegBunch*, int, int, int);
		bool eventFilter(QObject*, QEvent*);

		void chLayout();

	private slots:
		void reformCpuLater();
		void resetLayout();
		void setDefaultLayout();
		void updateCpuDockWidth();
		void styleTabBars();
		void setShowLabels(bool);
		void setShowSegment(bool);
		void setRomWriteable(bool);
		void resetTCount();

		bool fillAll();
		void fillNotCPU();
		void doStep();

		void saveDasm();
		void d_remap(int, int, int);
		void save_mem_map();
		void rest_mem_map();

		void dbgLLab();
		void dbgSLab();
		void jumpToLabel(QString);

		void saveMap();
		void loadMap();
		void mapClear();
		void mapAuto();

		void heatToggle(bool);
		void heatReset();
		void heatExport();

		int fillDisasm();
		void regClick(QMouseEvent*);
		void reload();

		void setCPU();
		void setFlags();

		void brkRequest(int, int, int);
		void putBreakPoint();
		void chaCellProperty(QAction*);

		void doMemView();
		void doFill();

		void doFind();
		void onFound(int);

		void doTrace(QAction*);
		void doTraceHere();
		void stopTrace();

		void doOpenDump();
//		void doSaveDump();
		void loadDump();
		void chDumpFile();
		void dmpStartOpen();
		void dmpLimChanged();
		void dmpLenChanged();
		void saveDumpBin();
		void saveDumpHobeta();
		void saveDumpToDisk(int);
		void saveDumpToA();
		void saveDumpToB();
		void saveDumpToC();
		void saveDumpToD();
		void saveVRam();
	protected:
		void keyPressEvent(QKeyEvent*);
		void keyReleaseEvent(QKeyEvent*);
		void showEvent(QShowEvent*);
		void resizeEvent(QResizeEvent*);
		void moveEvent(QMoveEvent*);
		void closeEvent(QCloseEvent*);
		void customEvent(QEvent*);
};

int loadDUMP(Computer*, const char*, int);
