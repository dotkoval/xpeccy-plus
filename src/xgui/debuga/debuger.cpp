#include "xcore/xcore.h"

#include <stdio.h>

#include <QIcon>
#include <QDebug>
#include <QBuffer>
#include <QPainter>
#include <QShowEvent>
#include <QVector>
#include <QFileDialog>
#include <QTemporaryFile>
#include <QTextCodec>

#include "debuger.h"
#include "dbg_sprscan.h"
#include "filer.h"
#include "../xgui.h"

// debuga.layout format: bump whenever the set of docks changes, so an older
// file is refused instead of scattering panels it knows nothing about.
// 1 = cpu, disasm, misc and stack became docks (the MISCTOOLBAR is gone)
// 2 = empty anchor strips at the left and right edges
#define DBG_LAYOUT_VERSION	2

static QDockWidget* make_edge_anchor(const char*);

int blockStart = -1;
int blockEnd = -1;

int tmpcnt = 0;

int getRFIData(QComboBox*);
void setRFIndex(QComboBox*, QVariant, int);
int dasmSome(Computer*, int, dasmData&);

// trace type
enum {
	DBG_TRACE_ALL = 0x100,
	DBG_TRACE_INT,
	DBG_TRACE_KEY,
	DBG_TRACE_HERE,
	DBG_TRACE_LOG
};

enum {
	NES_SCR_OFF = 0x00,
	NES_SCR_0,
	NES_SCR_1,
	NES_SCR_2,
	NES_SCR_3,
	NES_SCR_ALL,
	NES_SCR_TILES
};

enum {
	NES_TILE_0000 = 0x0000,
	NES_TILE_1000 = 0x1000
};

typedef struct {
	QLabel* name;
	QLineEdit* edit;
} dbgRegPlace;

// #define SETCOLOR(_n, _r) col = conf.pal[_n]; if (col.isValid()) pal.setColor(_r, col)

#define FLG_ALTCOLOR	1
#define FLG_LIGHTER	(1<<1)

QString getStyleString(QString bgcn, QString txcn, int f = 0, int g = 100) {
	QString str;
	QColor col = conf.pal[bgcn];
	if (col.isValid()) {
		if (f & FLG_LIGHTER) {
			str.append(QString("background-color:%0;").arg(col.lighter(g).name()));
		} else {
			str.append(QString("background-color:%0;").arg(col.name()));
		}
		if (f & FLG_ALTCOLOR) {
			str.append(QString("alternate-background-color:%0;").arg(col.lighter(g).name()));
		}
	}
	col = conf.pal[txcn];
	if (col.isValid()) str.append(QString("color:%0;").arg(col.name()));
	return str;
}

void DebugWin::updateStyle() {
	QString str;
#if 0
	QString bcstr;
	bcstr = getStyleString("dbg.window", "dbg.text");
	if (!bcstr.isEmpty()) {
		str.append("QWidget {").append(bcstr).append("}");		// main color (window + all widgets by default), must be 1st
		bcstr = getStyleString("dbg.window", "dbg.text", FLG_LIGHTER, 80);
		str.append("QTabBar::tab {").append(bcstr).append("}");		// by some reason, shape is disappearing
		bcstr = getStyleString("dbg.window", "dbg.text", FLG_LIGHTER, 120);
		str.append("QTabBar::tab:selected {").append(bcstr).append("}");
	}
	bcstr = getStyleString("dbg.table.bg", "dbg.table.txt", FLG_ALTCOLOR, 80);
	if (!bcstr.isEmpty()) {
		// qDebug() << bcstr;
		str.append("QTableView {").append(bcstr).append("}");
	}
	bcstr = getStyleString("dbg.input.bg", "dbg.input.txt");
	if (!bcstr.isEmpty()) {
		str.append("QLineEdit {").append(bcstr).append("}");
		str.append("QComboBox {").append(bcstr).append("}");
	}
	setStyleSheet(str);
#endif
// headers
	str = getStyleString("dbg.header.bg", "dbg.header.txt");
	ui_cpu.labHeadFlags->setStyleSheet(str);
	ui_misc.labHeadRay->setStyleSheet(str);
	ui_misc.labHeadSignal->setStyleSheet(str);
	ui_misc.labPorts->setStyleSheet(str);
	foreach(xDockWidget* dw, dockWidgets) {
		dw->titleBarWidget()->setStyleSheet(str);
	}

	setFont(conf.dbg.font);
	// A style sheet gives every widget a font of its own, which stops the
	// propagation from this window: the panels would fall back to the
	// interface font. Hand the font over one by one.
	foreach(QWidget* wid, findChildren<QWidget*>()) {
		wid->setFont(conf.dbg.font);
	}
	foreach(xHexSpin* xhs, dbgRegEdit) {
		xhs->updatePal();	// takes the new font from the parent
		xhs->refitWidth();
	}
	curCpuCore = nullptr;		// font changed: re-measure the register columns
	fillDisasm();
	wid_dump->draw();
	//ui.dumpTable->update();
	wid_disk_dump->draw();
}

void DebugWin::save_mem_map() {
	Computer* comp = conf.prof.cur->zx;
	for (int i = 0; i < 256; i++) {
		wid_mmap->mem_map[i] = comp->mem->map[i];
	}
}

void DebugWin::rest_mem_map() {
	Computer* comp = conf.prof.cur->zx;
	for (int i = 0; i < 256; i++) {
		 comp->mem->map[i] = wid_mmap->mem_map[i];
	}
	fillAll();
}

void DebugWin::d_remap(int _b, int _t, int _n) {
	Computer* comp = conf.prof.cur->zx;
	memSetBank(comp->mem, _b, _t, _n, MEM_16K, NULL, NULL, NULL);
	wid_dump->draw();
	ui_asm.dasmTable->updContent();
	wid_mmap->draw();
}

void DebugWin::start() {
	if (isVisible()) {
		activateWindow();
		return;
	}
	blockStart = -1;
	blockEnd = -1;
	save_mem_map();
	Computer* comp = conf.prof.cur->zx;
	if (comp->hw->grp != tabMode) {
		onPrfChange();		// update tabs
	}
	if (!comp->vid->tail)
		vid_dark_tail(comp->vid);

	this->move(conf.dbg.pos);
	comp->vid->debug = 1;
	comp->flgDBG = 1;
	comp->flgBRK = 0;
	comp->cpu->flgRetBRK = 0;

	brk_clear_tmp(comp);		// clear temp breakpoints

	updateStyle();		// this will call fillAll
	show();
// fillall redrawing all vivisble widgets
	if (!fillAll()) {
		ui_asm.dasmTable->setAdr(cpu_get_pc(comp->cpu) + comp->cpu->cs.base);
	}
	if (memViewer->vis) {
		memViewer->move(memViewer->winPos);
		memViewer->show();
		memViewer->fillImage();
	}
	wid_brk->moved();		// to redraw all icons
	wid_zxscr->setZoom(conf.dbg.scrzoom);
	activateWindow();
	ui_asm.dasmTable->setFocus();
}

void DebugWin::stop() {
	Computer* comp = conf.prof.cur->zx;
	if (!ui_asm.cbAccT->isChecked())
		tCount = comp->tickCount;	// before compExec to add current opcode T
	if (comp->flgDBG) compExec(comp);			// to prevent double breakpoint catch
	comp->flgDBG = 0;		// back to normal work, turn breakpoints on
	comp->vid->debug = 0;
	comp->flgMAP = ui_asm.actMaping->isChecked() ? 1 : 0;
	stopTrace();

	memViewer->vis = memViewer->isVisible() ? 1 : 0;
	memViewer->winPos = memViewer->pos();

	foreach(xDockWidget* dw, dockWidgets) {
		dw->setFloating(false);
	}

	memViewer->hide();
	hide();
	emit closed();
}

void DebugWin::resetTCount() {
	Computer* comp = conf.prof.cur->zx;
	if (ui_asm.cbAccT->isChecked()) {
		tCount = comp->tickCount;
		ui_asm.labTcount->setText(QString("%0 / %1").arg(comp->tickCount - tCount).arg(comp->frmtCount));
	}
}

void DebugWin::onPrfChange() {
	xProfile* prf = conf.prof.cur;
	if (!prf) return;
	Computer* comp = prf->zx;
	save_mem_map();

	tabMode = comp->hw->grp;
	foreach (xDockWidget* dw, dockWidgets) {
		dw->setHidden(!(dw->hwList.isEmpty() || dw->hwList.contains(tabMode)));
	}

	// set input line base
	foreach(xHexSpin* xhs, dbgRegEdit) {
		xhs->setBase(comp->hw->base);
	}
	unsigned int lim = (1 << comp->hw->adrbus);
	wid_dump->setLimit(lim);
	ui_asm.dasmScroll->setMaximum(lim - 1);

	dui.leStart->setMax(lim - 1);
	//dui.leEnd->setMax(lim - 1);		// TODO:why segfault
	dui.leLen->setMax(lim);

	// ui.tabDiskDump->setDrive(ui.cbDrive->currentIndex());
	wid_disk_dump->draw();
	wid_vmem_dump->setVMem(conf.prof.cur->zx->vid->ram);

	wid_dump->setBase(comp->hw->base, comp->hw->id);
	wid_brk->moved();

	fillAll();
}

// void DebugWin::reject() {stop();}
void DebugWin::closeEvent(QCloseEvent*) {stop();}

DebugWin::DebugWin(QWidget* par):QMainWindow(par) {
	int i;

	curCpuCore = nullptr;
	regCols = 0;
	regPairW = 0;
	regWideW = 0;
	cpuWideDock = 0;
	reformPending = 0;
	winShown = 0;
	reformWait = 0;

	// AllowNestedDocks is off by default, and without it a left/right area can
	// only stack docks vertically: nothing can be dropped beside anything.
	// AnimatedDocks is on by default and makes dragging panels feel sluggish
	setDockOptions(QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);
	setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
	setWindowTitle("Xpeccy+ deBUGa");
	setWindowIcon(QIcon(":/images/bug.png"));

	wid_cpu = new QWidget;
	QWidget* wid_dasm = new QWidget;
	ui_cpu.setupUi(wid_cpu);
	ui_asm.setupUi(wid_dasm);

	// No central widget: with one, nothing can ever be docked above or below the
	// disassembler, and the boundaries around it carry no splitter handle. As a
	// dock among docks it can be arranged - and resized - like everything else
	wid_cpu->installEventFilter(this);	// reflows to 1 or 2 columns, see reFormCPU
	wid_cpu_dock = new xDockWidget("", "CPU");
	wid_cpu_dock->setObjectName("CPU");
	wid_cpu_dock->setWidget(wid_cpu);

	wid_dasm_dock = new xDockWidget("", "Disasm");
	wid_dasm_dock->setObjectName("DISASM");
	wid_dasm_dock->setWidget(wid_dasm);

	wid_dump = new xDumpWidget("","DUMP");
	wid_rdump = new xRDumpWidget("","REG-DUMP");
	wid_disk_dump = new xDiskDumpWidget(":/images/floppy.png","FDD");
	wid_cmos_dump = new xCmosDumpWidget("","CMOS");
	wid_vmem_dump = new xVMemDumpWidget("","VMEM");
	wid_zxscr = new xZXScrWidget(":/images/rulers.png","Screen");
	wid_dma = new xDmaWidget("","DMA");
	wid_pit = new xPitWidget("","PIT");
	wid_pic = new xPicWidget("","PIC");
	wid_vga = new xVgaWidget(":/images/display.png","VGA");
	wid_ay = new xAYWidget(":/images/note.png","Sound Chip");
	wid_tape = new xTapeWidget(":/images/tape.png","Tape");
	wid_fdd = new xFDDWidget(":/images/floppy.png","FDC");
	wid_brk = new xBreakWidget(":/images/stop.png","Breakpoints");
	wid_gb = new xGameboyWidget(":/images/gameboy.png","GameBoy");
	wid_gbv = new xGBVideoWidget(":/images/gameboy.png", "GBVideo");
	wid_ppu = new xPPUWidget(":/images/nespad.png","NES PPU");
	wid_cia = new xCiaWidget("","CIA");
	wid_vic = new xVicWidget("","VIC");
	wid_mmap = new xMMapWidget(":/images/memory.png","Memory map");
	wid_ps2 = new xPS2Widget("","PS/2");
	wid_pal = new xPalWidget(":/images/palette.png", "Palette");

	dockWidgets << wid_dump << wid_rdump << wid_disk_dump << wid_vmem_dump << wid_cmos_dump;
	dockWidgets << wid_brk << wid_zxscr << wid_ay << wid_tape;
	dockWidgets << wid_fdd << wid_mmap << wid_gb << wid_gbv << wid_ppu << wid_pal;
	dockWidgets << wid_cia << wid_dma << wid_pic << wid_pit << wid_vga << wid_ps2;

	// misc used to be one monolithic MISCTOOLBAR pinned to the window edge.
	// two docks instead, so they can be dragged and split like the rest
	QWidget* wmisc = new QWidget;
	ui_misc.setupUi(wmisc);
	wid_misc = new xDockWidget("", "MEMMAP");
	wid_misc->setObjectName("MISC");
	wid_misc->setWidget(wmisc);

	QWidget* wstack = new QWidget;
	ui_stack.setupUi(wstack);
	wid_stack = new xDockWidget("", "STACK");
	wid_stack->setObjectName("STACK");
	wid_stack->setWidget(wstack);

	wid_anchor_l = make_edge_anchor("ANCHOR_L");
	wid_anchor_r = make_edge_anchor("ANCHOR_R");

	// deliberately not in dockWidgets: they carry no content, so nothing should
	// style them, hide them per hardware, or redraw them
	dockWidgets << wid_misc << wid_stack << wid_cpu_dock << wid_dasm_dock;

	setDefaultLayout();

	setContextMenuPolicy(Qt::PreventContextMenu);

	dumpwin = new QDialog(this);
	labswin = new xLabeList(this);
// create registers group
	xLabel* lab;
	xHexSpin* xhs;
	QLabel* qlb;
	QCheckBox* qcb;
	for(i = 0; i < 32; i++) {		// set max registers here
		// parent them at once: reFormCPU() shows a register before putting it into
		// the layout, and a parentless visible widget is a window of its own
		lab = new xLabel(wid_cpu);
		lab->id = i;
		lab->setVisible(false);
		lab->setProperty("isbit", false);
		lab->setContentsMargins(0, 0, 4, 0);	// don't let it touch the value

		xhs = new xHexSpin(wid_cpu);
		xhs->setXFlag(XHS_BGR | XHS_DEC | XHS_FILL | XHS_AUTOW);
		xhs->setVisible(false);
		xhs->setFrame(false);
		xhs->setMinimumHeight(21);
		xhs->setAlignment(Qt::AlignCenter);
		qcb = new QCheckBox(wid_cpu);
		qcb->setVisible(false);
		dbgRegLabs.append(lab);
		dbgRegEdit.append(xhs);
		dbgRegBits.append(qcb);
		// widgets are put into the layout by reFormCPU(), it knows the cpu register set
		connect(lab, &xLabel::clicked, this, &DebugWin::regClick);
		connect(xhs, &xHexSpin::textChanged, this, &DebugWin::setCPU);
		connect(qcb, SIGNAL(toggled(bool)), this, SLOT(setCPU()));
	}
// create flags group (for cpu->f, 16 bit max)
	flagrp = new QButtonGroup;
	flagrp->setExclusive(false);
	for(i = 0; i < 16; i++) {
		qlb = new QLabel(wid_cpu);
		qcb = new QCheckBox(wid_cpu);
		qlb->setVisible(false);
		qcb->setVisible(false);
		dbgFlagLabs.append(qlb);
		dbgFlagBox.append(qcb);
		flagrp->addButton(qcb);
		// placed by reFormFlags(), it follows the register columns
	}
	connect(flagrp, SIGNAL(buttonClicked(QAbstractButton*)), this, SLOT(setFlags()));

	conf.dbg.labels = 1;
	conf.dbg.segment = 0;
	ui_asm.actShowLabels->setChecked(conf.dbg.labels);
	ui_asm.actShowSeg->setChecked(conf.dbg.segment);
	ui_asm.actDimAddr->setChecked(conf.dbg.dimadr);
	ui_asm.actDimOps->setChecked(conf.dbg.dimops);
	ui_asm.actSyntax->setChecked(conf.dbg.synhl);
	ui_asm.actBlockSep->setChecked(conf.dbg.blocksep);

	xid_none = new xItemDelegate(XTYPE_NONE);
	xid_byte = new xItemDelegate(XTYPE_BYTE);
	xid_labl = new xItemDelegate(XTYPE_LABEL);
	xid_octw = new xItemDelegate(XTYPE_OCTWRD);
	xid_dump = new xItemDelegate(XTYPE_DUMP);

// actions data
	ui_asm.actFetch->setData(MEM_BRK_FETCH);
	ui_asm.actRead->setData(MEM_BRK_RD);
	ui_asm.actWrite->setData(MEM_BRK_WR);

	ui_asm.actViewOpcode->setData(DBG_VIEW_EXEC);
	ui_asm.actViewByte->setData(DBG_VIEW_BYTE);
	ui_asm.actViewWord->setData(DBG_VIEW_WORD);
	ui_asm.actViewAddr->setData(DBG_VIEW_ADDR);
	ui_asm.actViewText->setData(DBG_VIEW_TEXT);

	ui_asm.actTrace->setData(DBG_TRACE_ALL);
	ui_asm.actTraceHere->setData(DBG_TRACE_HERE);
	ui_asm.actTraceINT->setData(DBG_TRACE_INT);
	ui_asm.actTraceLog->setData(DBG_TRACE_LOG);

	ui_asm.dasmTable->setFocus();

// disasm table
	ui_asm.dasmTable->setItemDelegateForColumn(0, xid_labl);
	ui_asm.dasmTable->setItemDelegateForColumn(1, xid_dump);
	ui_asm.dasmTable->setItemDelegateForColumn(2, new xDasmSyntax(ui_asm.dasmTable));

// actions
	ui_asm.tbBreak->addAction(ui_asm.actFetch);
	ui_asm.tbBreak->addAction(ui_asm.actRead);
	ui_asm.tbBreak->addAction(ui_asm.actWrite);

	ui_asm.tbLabels->addAction(ui_asm.actShowLabels);
	ui_asm.tbLabels->addAction(ui_asm.actLoadLabels);
	ui_asm.tbLabels->addAction(ui_asm.actSaveLabels);
	ui_asm.tbLabels->addAction(ui_asm.actLabelsList);
//	ui_asm.tbLabels->addAction(ui_asm.actLabManager);

	ui_asm.tbView->addAction(ui_asm.actViewOpcode);
	ui_asm.tbView->addAction(ui_asm.actViewByte);
	ui_asm.tbView->addAction(ui_asm.actViewText);
	ui_asm.tbView->addAction(ui_asm.actViewWord);
	ui_asm.tbView->addAction(ui_asm.actViewAddr);

	ui_asm.tbSaveDasm->addAction(ui_asm.actDisasm);
	ui_asm.tbSaveDasm->addAction(ui_asm.actLoadDump);
	ui_asm.tbSaveDasm->addAction(ui_asm.actSaveDump);
//	ui_asm.tbSaveDasm->addAction(ui_asm.actLoadLabels);
//	ui_asm.tbSaveDasm->addAction(ui_asm.actSaveLabels);
	ui_asm.tbSaveDasm->addAction(ui_asm.actLoadMap);
	ui_asm.tbSaveDasm->addAction(ui_asm.actSaveMap);

	ui_asm.tbTrace->addAction(ui_asm.actTrace);
	ui_asm.tbTrace->addAction(ui_asm.actTraceHere);
	ui_asm.tbTrace->addAction(ui_asm.actTraceINT);
	ui_asm.tbTrace->addAction(ui_asm.actTraceLog);

	ui_asm.tbTool->addAction(ui_asm.actSearch);
	ui_asm.tbTool->addAction(ui_asm.actFill);
	ui_asm.tbTool->addAction(ui_asm.actSprScan);
	ui_asm.tbTool->addAction(ui_asm.actShowKeys);
	ui_asm.tbTool->addAction(ui_asm.actWutcha);
//	ui_asm.tbTool->addAction(ui_asm.actLabelsList);

//	ui_asm.tbDbgOpt->addAction(ui_asm.actShowLabels);
	ui_asm.tbDbgOpt->addAction(ui_asm.actDimAddr);
	ui_asm.tbDbgOpt->addAction(ui_asm.actDimOps);
	ui_asm.tbDbgOpt->addAction(ui_asm.actSyntax);
	ui_asm.tbDbgOpt->addAction(ui_asm.actBlockSep);
	ui_asm.tbDbgOpt->addAction(ui_asm.actShowSeg);
	ui_asm.tbDbgOpt->addAction(ui_asm.actRomWr);
	ui_asm.tbDbgOpt->addAction(ui_asm.actMaping);
	ui_asm.tbDbgOpt->addAction(ui_asm.actMapingClear);
	ui_asm.tbDbgOpt->addAction(ui_asm.actHeatEnable);
	ui_asm.tbDbgOpt->addAction(ui_asm.actHeatReset);
	ui_asm.tbDbgOpt->addAction(ui_asm.actHeatExport);
	QAction* actResetLayout = new QAction("Reset panel layout", this);
	ui_asm.tbDbgOpt->addAction(actResetLayout);
	connect(actResetLayout, &QAction::triggered, this, &DebugWin::resetLayout);

// connections
	connect(this, &DebugWin::needStep, this, &DebugWin::doStep);
	connect(ui_asm.cbAccT, &QCheckBox::toggled, this, &DebugWin::resetTCount);
	connect(ui_asm.actMapingClear, &QAction::triggered, this, &DebugWin::mapClear);
	connect(ui_asm.actHeatEnable, &QAction::toggled, this, &DebugWin::heatToggle);
	connect(ui_asm.actHeatReset, &QAction::triggered, this, &DebugWin::heatReset);
	connect(ui_asm.actHeatExport, &QAction::triggered, this, &DebugWin::heatExport);
	connect(ui_asm.dasmTable, &xDisasmTable::customContextMenuRequested, this, &DebugWin::putBreakPoint);
	//connect(ui_asm.dasmTable, &xDisasmTable::rqRefill, this, &DebugWin::fillDisasm);		// must update internally
	connect(ui_asm.dasmTable, &xDisasmTable::rqRefill, wid_dump, &xDumpWidget::draw);
	connect(ui_asm.dasmTable, &xDisasmTable::rqRefill, wid_zxscr, &xZXScrWidget::draw);
	connect(ui_asm.dasmTable, &xDisasmTable::rqRefill, wid_brk, &xBreakWidget::draw);
	connect(ui_asm.dasmTable, &xDisasmTable::rqRefillAll, this, &DebugWin::fillAll);
	connect(ui_asm.dasmTable, &xDisasmTable::s_adrch, ui_asm.dasmScroll, &QScrollBar::setValue);
	connect(ui_asm.dasmScroll, &QScrollBar::valueChanged, ui_asm.dasmTable, &xDisasmTable::setAdrX);

	connect(wid_dump, &xDumpWidget::s_blockch, this, &DebugWin::fillDisasm);
	connect(wid_dump, &xDumpWidget::s_datach, this, &DebugWin::fillAll);
	connect(wid_dump, &xDumpWidget::s_brkrq, this, &DebugWin::brkRequest);

	connect(wid_brk, &xBreakWidget::rqDisasm, ui_asm.dasmTable, &xDisasmTable::setAdrX);
	connect(wid_brk, &xBreakWidget::updated, this, &DebugWin::fillDisasm);
	connect(wid_brk, &xBreakWidget::updated, wid_dump, &xDumpWidget::draw);

	connect(ui_asm.actSearch, &QAction::triggered, this, &DebugWin::doFind);
	connect(ui_asm.actFill, &QAction::triggered, this, &DebugWin::doFill);
	connect(ui_asm.actSprScan, &QAction::triggered, this, &DebugWin::doMemView);
	connect(ui_asm.actShowKeys, &QAction::triggered, this, &DebugWin::wannaKeys);
	connect(ui_asm.actWutcha, &QAction::triggered, this, &DebugWin::wannaWutch);

	connect(ui_asm.actLabelsList, &QAction::triggered, labswin, &xLabeList::show);
	connect(labswin, &xLabeList::labSelected, this, &DebugWin::jumpToLabel);
	connect(labswin, &xLabeList::labSetChanged, this, &DebugWin::fillDisasm);

	connect(ui_asm.actShowLabels, &QAction::toggled, this, &DebugWin::setShowLabels);
	connect(ui_asm.actDimAddr, &QAction::toggled, this, &DebugWin::fillDisasm);
	connect(ui_asm.actDimOps, &QAction::toggled, this, &DebugWin::fillDisasm);
	connect(ui_asm.actSyntax, &QAction::toggled, this, &DebugWin::fillDisasm);
	connect(ui_asm.actBlockSep, &QAction::toggled, this, &DebugWin::fillDisasm);
	connect(ui_asm.actShowSeg, &QAction::toggled, this, &DebugWin::setShowSegment);
	connect(ui_asm.actRomWr, &QAction::toggled, this, &DebugWin::setRomWriteable);

	connect(ui_asm.tbView, &QToolButton::triggered, this, &DebugWin::chaCellProperty);
	connect(ui_asm.tbBreak, SIGNAL(triggered(QAction*)),this,SLOT(chaCellProperty(QAction*)));
	connect(ui_asm.tbTrace, SIGNAL(triggered(QAction*)),this,SLOT(doTrace(QAction*)));

	connect(ui_asm.actLoadDump, SIGNAL(triggered(bool)),this,SLOT(doOpenDump()));
	connect(ui_asm.actSaveDump, SIGNAL(triggered(bool)),dumpwin,SLOT(show()));
	connect(ui_asm.actLoadLabels, SIGNAL(triggered(bool)),this,SLOT(dbgLLab()));
	connect(ui_asm.actSaveLabels, SIGNAL(triggered(bool)),this,SLOT(dbgSLab()));
	connect(ui_asm.actLoadMap, SIGNAL(triggered(bool)),this,SLOT(loadMap()));
	connect(ui_asm.actSaveMap, SIGNAL(triggered(bool)),this,SLOT(saveMap()));
	connect(ui_asm.actDisasm, SIGNAL(triggered(bool)),this,SLOT(saveDasm()));
	connect(ui_asm.tbRefresh, SIGNAL(released()), this, SLOT(reload()));

	connect(wid_mmap, SIGNAL(s_remap(int, int, int)), this, SLOT(d_remap(int,int,int)));
	connect(wid_mmap, &xMMapWidget::s_restore, this, &DebugWin::rest_mem_map);

//	connect (ui.tbSaveVRam, SIGNAL(released()), this, SLOT(saveVRam()));
// registers
//	connect(ui.flagGroup,SIGNAL(buttonClicked(int)),this,SLOT(setFlags()));

	block = 0;
	tCount = 0;
	trace = 0;
// subwindows
	dui.setupUi(dumpwin);
	dui.tbSave->addAction(dui.aSaveBin);
	dui.tbSave->addAction(dui.aSaveHobeta);
	dui.tbSave->addAction(dui.aSaveToA);
	dui.tbSave->addAction(dui.aSaveToB);
	dui.tbSave->addAction(dui.aSaveToC);
	dui.tbSave->addAction(dui.aSaveToD);
	dui.leStart->setMin(0);
	dui.leStart->setMax(0xffffff);
	dui.leEnd->setMin(0);
	dui.leEnd->setMax(0xffffff);
	dui.leLen->setMin(1);
	dui.leLen->setMax(0x1000000);

	connect(dui.aSaveBin,SIGNAL(triggered()),this,SLOT(saveDumpBin()));
	connect(dui.aSaveHobeta,SIGNAL(triggered()),this,SLOT(saveDumpHobeta()));
	connect(dui.aSaveToA,SIGNAL(triggered()),this,SLOT(saveDumpToA()));
	connect(dui.aSaveToB,SIGNAL(triggered()),this,SLOT(saveDumpToB()));
	connect(dui.aSaveToC,SIGNAL(triggered()),this,SLOT(saveDumpToC()));
	connect(dui.aSaveToD,SIGNAL(triggered()),this,SLOT(saveDumpToD()));
	connect(dui.leStart,SIGNAL(valueChanged(int)),this,SLOT(dmpLimChanged()));
	connect(dui.leEnd,SIGNAL(valueChanged(int)),this,SLOT(dmpLimChanged()));
	connect(dui.leLen,SIGNAL(valueChanged(int)),this,SLOT(dmpLenChanged()));

	openDumpDialog = new QDialog(this);
	oui.setupUi(openDumpDialog);
	connect(oui.tbFile,SIGNAL(clicked()),this,SLOT(chDumpFile()));
	connect(oui.leStart,SIGNAL(textChanged(QString)),this,SLOT(dmpStartOpen()));
	connect(oui.butOk,SIGNAL(clicked()), this, SLOT(loadDump()));

	memViewer = new MemViewer(this);

	memFinder = new xMemFinder(this);
	connect(memFinder, SIGNAL(patFound(int)), this, SLOT(onFound(int)));

	memFiller = new xMemFiller(this);
	connect(memFiller, SIGNAL(rqRefill()),this,SLOT(fillNotCPU()));
	connect(memFiller, &xMemFiller::rqRefill, this, &DebugWin::fillDisasm);

// context menu
	cellMenu = new QMenu(this);
	QMenu* bpMenu = new QMenu("Breakpoints");
	bpMenu->setIcon(QIcon(":/images/stop.png"));
	cellMenu->addMenu(bpMenu);
	bpMenu->addAction(ui_asm.actFetch);
	bpMenu->addAction(ui_asm.actRead);
	bpMenu->addAction(ui_asm.actWrite);
	QMenu* viewMenu = new QMenu("View");
	viewMenu->setIcon(QIcon(":/images/bars.png"));
	cellMenu->addMenu(viewMenu);
	viewMenu->addAction(ui_asm.actViewOpcode);
	viewMenu->addAction(ui_asm.actViewByte);
	viewMenu->addAction(ui_asm.actViewText);
	viewMenu->addAction(ui_asm.actViewWord);
	viewMenu->addAction(ui_asm.actViewAddr);
	cellMenu->addSeparator();
	cellMenu->addAction(ui_asm.actLabelsList);
	cellMenu->addAction(ui_asm.actTraceHere);
	cellMenu->addAction(ui_asm.actShowLabels);
	// NOTE: actions already connected to slots by main menu. no need to double it here

	resize(minimumSize());

	// A dock moving can change what sits under the cpu panel. Queued: the
	// geometry is not settled yet while the drop is being handled
	foreach (xDockWidget* dw, dockWidgets) {
		connect(dw, &QDockWidget::dockLocationChanged,
			this, &DebugWin::updateCpuDockWidth, Qt::QueuedConnection);
	}

	// A saved layout only makes sense for the set of panels it was written for.
	// restoreState refuses a state whose version differs, and leaves docks it
	// finds no entry for wherever they happen to be - which is how an old file
	// scatters the new panels. Refused or damaged: fall back to the default.
	QString path = conf.path.confDir.c_str();
	path.append("/debuga.layout");
	QFile file(path);
	if (file.open(QFile::ReadOnly)) {
		QByteArray state = file.readAll();
		file.close();
		// resetLayout, not setDefaultLayout: a refused restore can leave the
		// window stretched, and only the former puts the size back too
		if (!restoreState(state, DBG_LAYOUT_VERSION))
			resetLayout();
	}
}

DebugWin::~DebugWin() {
	QByteArray state = saveState(DBG_LAYOUT_VERSION);
	QString path = conf.path.confDir.c_str();
	path.append("/debuga.layout");
	QFile file(path);
	if (file.open(QFile::WriteOnly)) {
		file.write(state);
		file.close();
	}
	dumpwin->deleteLater();
	openDumpDialog->deleteLater();
	memViewer->deleteLater();
	memFiller->deleteLater();
	memFinder->deleteLater();
}

void DebugWin::setShowLabels(bool f) {
	conf.dbg.labels = !!f;
	fillDisasm();
}

void DebugWin::setShowSegment(bool f) {
	conf.dbg.segment = !!f;
	fillDisasm();
	wid_dump->draw();
	//fillDump();
}

void DebugWin::setRomWriteable(bool f) {
	conf.dbg.romwr = !!f;
}

/*
void DebugWin::setDumpCP() {
	int cp = getRFIData(ui.cbCodePage);
	ui.dumpTable->setCodePage(cp);
	fillDump();
}

void DebugWin::chDumpView() {
	int mode,page,pbase,psize;
	Computer* comp = conf.prof.cur->zx;
	mode = getRFIData(ui.cbDumpView);
	page = ui.sbDumpPage->value();
	pbase = ui.leDumpPageBase->getValue();
	if (mode == XVIEW_CPU) {
		psize = (comp->hw->id == HW_IBM_PC) ? MEM_4M : MEM_64K;
	} else {
		psize = getRFIData(ui.cbDumpPageSize);
	}
	ui.widDumpPage->setDisabled(mode == XVIEW_CPU);
	ui.dumpTable->setMode(mode, page, pbase, psize);
	ui.dumpScroll->setMaximum(psize-1);
	ui.dumpTable->setLimit(psize);
}
*/

static QFile logfile;

void DebugWin::doStep() {
	Computer* comp = conf.prof.cur->zx;
	if (!ui_asm.cbAccT->isChecked())
		tCount = comp->tickCount;
	compExec(comp);
	if (!fillAll()) {
		ui_asm.dasmTable->setAdr(cpu_get_pc(comp->cpu) + comp->cpu->cs.base);
		//fillDisasm();
	}
}

void DebugWin::doTraceHere() {
	doTrace(ui_asm.actTraceHere);
}

void DebugWin::doTrace(QAction* act) {
	if (trace) return;

	traceType = act->data().toInt();

	if (traceType == DBG_TRACE_LOG) {
		QString path = QFileDialog::getSaveFileName(this, "Log file",QString(),QString(),nullptr,QFileDialog::DontUseNativeDialog);
		if (path.isEmpty()) return;
		logfile.setFileName(path);
		if (!logfile.open(QFile::WriteOnly)) return;
		logfile.write("addr|command");
		xRegBunch regs = cpuGetRegs(conf.prof.cur->zx->cpu);
		int i = 0;
		while (regs.regs[i].id != REG_EOT) {
			if (regs.regs[i].id != REG_EMPTY) {
				logfile.write("|");
				logfile.write(regs.regs[i].name);
			}
			i++;
		}
		logfile.write("\n");
	}

	trace = 1;
	traceAdr = getAdr();
	ui_asm.tbTrace->setEnabled(false);
	QApplication::postEvent(this, new QEvent((QEvent::Type)DBG_EVENT_STEP));
}

void DebugWin::stopTrace() {
	trace = 0;
	ui_asm.tbTrace->setEnabled(true);
	if (logfile.isOpen()) logfile.close();
}

void DebugWin::reload() {
	Computer* comp = conf.prof.cur->zx;
	if (comp->mem->snapath) {
		load_file(comp, comp->mem->snapath, FG_SNAPSHOT, 0);
		ui_asm.dasmTable->setAdr(cpu_get_pc(comp->cpu) + comp->cpu->cs.base);
	}
	qDebug() << conf.labpath;
	if (!conf.labpath.isEmpty()) {
		loadLabels(conf.labpath.toLocal8Bit().data());
	}
	fillAll();
}

void DebugWin::keyPressEvent(QKeyEvent* ev) {
	if (trace && !ev->isAutoRepeat()) {
		stopTrace();
		return;
	}
	int i;
	int key = shortcut_check(SCG_DEBUGA, QKeySequence(ev->key() | ev->modifiers()));
	if (key < 0)
		key = ev->key() | ev->modifiers();
	unsigned char* ptr;
	int len;
	dasmData drow;
	QModelIndex idx;
	Computer* comp = conf.prof.cur->zx;
	int pc = cpu_get_pc(comp->cpu);
	switch (key) {
		case XCUT_OPTIONS:
			emit wannaOptions();
			break;
		case XCUT_LOAD:
			load_file(comp, NULL, FG_ALL, -1);
			ui_asm.dasmTable->setAdr(pc + comp->cpu->cs.base);
			//fillAll();
			activateWindow();
			break;
		case XCUT_SAVE:
			save_file(comp, NULL, FG_ALL, -1);
			activateWindow();
			break;
		case XCUT_STEPIN:
			if (!ev->isAutoRepeat()) {
				doStep();
			} else if (!trace) {
				doTrace(ui_asm.actTrace);
			}
			break;
		case XCUT_STEPOVER:
			len = dasmSome(comp, pc + comp->cpu->cs.base, drow);
			if (drow.oflag & OF_SKIPABLE) {
				ptr = getBrkPtr(comp, pc + comp->cpu->cs.base + len);
				*ptr |= MEM_BRK_TFETCH;
				stop();
			} else {
				doStep();
			}
			break;
		case XCUT_STEPOUT:
			comp->cpu->flgRetBRK = 1;
			comp->cpu->regCallCnt = 0;
			stop();
			break;
		case XCUT_FASTSTEP:
			for (i = 10; i > 0; i--)
				doStep();
			break;
		case XCUT_TMPBRK:
			if (!ui_asm.dasmTable->hasFocus()) break;
			idx = ui_asm.dasmTable->currentIndex();
			i = ui_asm.dasmTable->getData(idx.row(), 0, Qt::UserRole).toInt();
			ptr = getBrkPtr(comp, i);
			stop();
			*ptr |= MEM_BRK_TFETCH;
			break;
		case XCUT_RESET:
			rzxStop(comp);
			compReset(comp, RES_DEFAULT);
			if (!fillAll()) {
				ui_asm.dasmTable->setAdr(pc + comp->cpu->cs.base);
				//fillDisasm();
			}
			break;
		case XCUT_TRACE:
			doTrace(ui_asm.actTrace);
			break;
		case XCUT_LABELS:
			ui_asm.actShowLabels->setChecked(!conf.dbg.labels);
			break;
		case XCUT_LABLIST:
			labswin->show();
			break;
		case XCUT_DBG_RELOAD:
			reload();
			break;
		case XCUT_KEYBOARD:
			emit wannaKeys();
			break;
		case XCUT_OPEN_DUMP:
			doOpenDump();
			break;
		case XCUT_SAVE_DUMP:
			//doSaveDump();
			dumpwin->show();
			break;
		case XCUT_OPEN_XMAP:
			loadMap();
			break;
		case XCUT_SAVE_XMAP:
			saveMap();
			break;
		case XCUT_FINDER:
			doFind();
			break;
		case XCUT_DEBUG:
			if (!ev->isAutoRepeat())
				stop();
			break;
	}
}

void DebugWin::keyReleaseEvent(QKeyEvent* ev) {
	QKeySequence seq(ev->key() | ev->modifiers());
	if (!ev->isAutoRepeat() && (shortcut_match(SCG_DEBUGA, XCUT_STEPIN, seq) != QKeySequence::NoMatch)) {
		stopTrace();
	}
}

static xRegBunch traceregs;
static QString tracestr;
static dasmData tracemnm;
extern int dasmrd(int, void*);

void DebugWin::customEvent(QEvent* ev) {
	Computer* comp = conf.prof.cur->zx;
	int pcadr = cpu_get_pc(comp->cpu);
	switch(ev->type()) {
		case DBG_EVENT_STEP:
			if ((traceType == DBG_TRACE_LOG) && logfile.isOpen()) {
				dasmSome(comp, pcadr + comp->cpu->cs.base, tracemnm);
				tracestr = "\"";			// to avoid numbers conversion, like 3e4->3000
				if (comp->cpu->core->group == CPUG_X86) {
					tracestr.append(gethexword(cpu_get_regtype(comp->cpu, REG_CS)));
					tracestr.append(":");
					tracestr.append(gethexword(pcadr));
				} else {
					tracestr.append(gethexword(pcadr));
				}
				tracestr.append("\"|");
				doStep();
				traceregs = cpuGetRegs(comp->cpu);
				tracestr.append(tracemnm.command);
				int i = 0;
				while (traceregs.regs[i].id != REG_EOT) {
					if (traceregs.regs[i].id != REG_EMPTY) {			// mustn't be visible
						tracestr.append("|\"");
						// tracestr.append(traceregs.regs[i].name).append(":");
						switch(traceregs.regs[i].size) {
							case REG_BIT: tracestr.append(traceregs.regs[i].value ? "1" : "0"); break;
							case REG_BYTE: tracestr.append(gethexbyte(traceregs.regs[i].value)); break;
							case REG_WORD: tracestr.append(gethexword(traceregs.regs[i].value)); break;
							case REG_24: tracestr.append(gethex6(traceregs.regs[i].value)); break;
							case REG_32: tracestr.append(gethexint(traceregs.regs[i].value)); break;
						}
						tracestr.append("\"");
					}
					i++;
				}
				tracestr.append("\n");
				logfile.write(tracestr.toUtf8());
			} else {
				doStep();
			}
			switch(traceType) {
				case DBG_TRACE_INT:
					if (comp->cpu->intrq & comp->cpu->inten)
						stopTrace();
					break;
				case DBG_TRACE_HERE:
					if (cpu_get_pc(comp->cpu) == traceAdr)
						stopTrace();
					break;
			}
			if (trace) {
				QApplication::postEvent(this, new QEvent((QEvent::Type)DBG_EVENT_STEP));
			}
			break;
		default:
			break;
	}
}

void DebugWin::moveEvent(QMoveEvent* ev) {
	if (!isVisible()) return;
	conf.dbg.pos = ev->pos();
}

void DebugWin::resizeEvent(QResizeEvent* ev) {
	if (!isVisible()) return;
	conf.dbg.siz = ev->size();
}

void setSignal(QLabel* lab, int on) {
	QFont fnt = lab->font();
	fnt.setBold(on);
	lab->setFont(fnt);
}

/*
void DebugWin::fillTabs() {
}
*/

void DebugWin::fillNotCPU() {
	Computer* comp = conf.prof.cur->zx;
	ui_asm.labTcount->setText(QString("%0 / %1").arg(comp->tickCount - tCount).arg(comp->frmtCount));

	fillMem();
	foreach(xDockWidget* dw, dockWidgets) {
		if (dw->isVisible())
			dw->draw();
	}

	setSignal(ui_misc.labDOS, comp->flgDOS);
	setSignal(ui_misc.labROM, comp->flgROM);
	setSignal(ui_misc.labCPM, comp->flgCPM);
	setSignal(ui_misc.labINT, comp->cpu->intrq & comp->cpu->inten);

	ui_misc.labRX->setNum(comp->vid->ray.x);
	setSignal(ui_misc.labRX, comp->vid->hblank);
	ui_misc.labRY->setNum(comp->vid->ray.y);
	setSignal(ui_misc.labRY, comp->vid->vblank);

	if (memViewer->isVisible())
		memViewer->fillImage();
	fillStack();
	fillPorts();
}

bool DebugWin::fillAll() {
	fillCPU();
	fillNotCPU();
	return fillDisasm();
}


void DebugWin::setScrAtr(int adr, int atr) {
	wid_zxscr->setAddress(adr, atr);
//	ui.leScr->setValue(adr);
//	ui.leAtr->setValue(atr);
}

// ...

// NOTE: called from start()
void DebugWin::chLayout() {
}

int dbg_get_reg_adr(CPU* cpu, xRegister* reg) {
	int a = reg->value;
	if (reg->flag & REG_SEG) {
		a = reg->base;
	} else if (cpu->core->group == CPUG_X86) {
		a += reg->base;
	}
	return a;
}

void DebugWin::regClick(QMouseEvent* ev) {
	xLabel* lab = qobject_cast<xLabel*>(sender());
	int id = lab->id;
	if (id < 0) return;
	Computer* comp = conf.prof.cur->zx;
	xRegBunch bunch = cpuGetRegs(comp->cpu);
	xRegister reg = bunch.regs[id];
	int adr = dbg_get_reg_adr(comp->cpu, &reg);
	//qDebug() << adr;
	switch (ev->button()) {
		case Qt::RightButton:
			//ui.dumpTable->setAdr(adr);
			wid_dump->setAdr(adr);
			break;
		case Qt::LeftButton:
			ui_asm.dasmTable->setAdr(adr, 1);
			break;
		default:
			break;
	}
}

// fdc

/*
void DebugWin::fillFDC() {
	if (ui.tabsPanel->currentWidget() != ui.fdcTab) return;
	Computer* comp = conf.prof.cur->zx;
	ui.fdcBusyL->setText(comp->dif->fdc->idle ? "0" : "1");
	ui.fdcComL->setText(comp->dif->fdc->idle ? "--" : gethexbyte(comp->dif->fdc->com));
	ui.fdcIrqL->setText(comp->dif->fdc->irq ? "1" : "0");
	ui.fdcDrqL->setText(comp->dif->fdc->drq ? "1" : "0");
	ui.fdcTrkL->setText(gethexbyte(comp->dif->fdc->trk));
	ui.fdcSecL->setText(gethexbyte(comp->dif->fdc->sec));
	ui.fdcHeadL->setText(comp->dif->fdc->side ? "1" : "0");
	ui.fdcDataL->setText(gethexbyte(comp->dif->fdc->data));
	ui.fdcStateL->setText(gethexbyte(comp->dif->fdc->state));
	ui.fdcSr0L->setText(gethexbyte(comp->dif->fdc->sr0));
	ui.fdcSr1L->setText(gethexbyte(comp->dif->fdc->sr1));
	ui.fdcSr2L->setText(gethexbyte(comp->dif->fdc->sr2));
	ui.flpCRC->setText(gethexword(comp->dif->fdc->crc));
	ui.flpInt->setText(comp->dif->fdc->intr ? "1" : "0");
	ui.flpDma->setText(comp->dif->fdc->dma ? "1" : "0");
	ui.flpIntEn->setText(comp->dif->fdc->inten ? "1" : "0");

	ui.flpCurL->setText(QString(QChar(('A' + comp->dif->fdc->flp->id) & 0xff)));
	ui.flpRdyL->setText((comp->dif->fdc->flp->insert && comp->dif->fdc->flp->door) ? "1" : "0");
	ui.flpTrkL->setText(gethexbyte(comp->dif->fdc->flp->trk));
	ui.flpPosL->setText(gethexword(comp->dif->fdc->flp->pos));
	ui.flpIdxL->setText(comp->dif->fdc->flp->index ? "1" : "0");
	ui.flpDataL->setText(comp->dif->fdc->flp->insert ? gethexbyte(flpRd(comp->dif->fdc->flp, comp->dif->fdc->side)): "--"); comp->dif->fdc->flp->rd = 0;
	ui.flpMotL->setText(comp->dif->fdc->flp->motor ? "1" : "0");
}
*/
// CPU

void DebugWin::fillFlags(const char* fnam) {
	if (fnam == NULL)
		fnam = cpuGetRegs(conf.prof.cur->zx->cpu).flags;
	int flgcnt = strlen(fnam);
	QString allflags = QString(fnam).rightJustified(16, '-');
	int f = cpu_get_flag(conf.prof.cur->zx->cpu);
	for (int i = 0; i < 16; i++) {
		if (i < flgcnt) {
			dbgFlagBox[i]->setVisible(true);
			dbgFlagLabs[i]->setVisible(true);
			dbgFlagLabs[i]->setText(allflags.at(15 - i));
			dbgFlagBox[i]->setChecked(f & (1 << i));
		} else {
			dbgFlagBox[i]->setVisible(false);
			dbgFlagLabs[i]->setVisible(false);
		}
	}
}

// grid columns: 0,1 = left pair (name + value), 3,4 = right pair, 2 is a gap between them
#define RCOL_LEFT	0
#define RCOL_RIGHT	3
#define RCOL_GAP	10
// items of ui_cpu.verticalLayout, see form_cpu.ui:
// registers, gap, flags header, flags, spacer.
// the cpu header is gone: the dock's title bar carries the name now
#define VLI_REGS	0
#define VLI_FLAGS	3

// flags follow the registers: 8 in a row when there is room for 2 columns, 4 otherwise
void DebugWin::reFormFlags(int per) {
	delete ui_cpu.verticalLayout->takeAt(VLI_FLAGS);	// flags stay alive, wid_cpu owns them
	ui_cpu.flagsGrid = new QGridLayout;
	ui_cpu.flagsGrid->setSpacing(2);
	ui_cpu.flagsGrid->setContentsMargins(0, 2, 0, 0);
	ui_cpu.verticalLayout->insertLayout(VLI_FLAGS, ui_cpu.flagsGrid);
	for (int i = 0; i < 16; i++) {
		int p = 15 - i;				// flag 15 is the leftmost one
		ui_cpu.flagsGrid->addWidget(dbgFlagLabs[i], (p / per) * 2, p % per, Qt::AlignCenter);
		ui_cpu.flagsGrid->addWidget(dbgFlagBox[i], (p / per) * 2 + 1, p % per, Qt::AlignCenter);
	}
	for (int i = 0; i < per; i++)			// spread them over the panel width
		ui_cpu.flagsGrid->setColumnStretch(i, 1);
}

// put one register into the grid. col is RCOL_LEFT or RCOL_RIGHT
void DebugWin::placeReg(xRegBunch* b, int i, int row, int col) {
	ui_cpu.formRegs->addWidget(dbgRegLabs[i], row, col);
	dbgRegLabs[i]->setVisible(true);
	if (b->regs[i].size == REG_BIT) {
		dbgRegLabs[i]->setProperty("isbit", true);
		ui_cpu.formRegs->addWidget(dbgRegBits[i], row, col + 1);
		dbgRegBits[i]->setCheckable(!(b->regs[i].flag & REG_RO));
		dbgRegBits[i]->setVisible(true);
		dbgRegEdit[i]->setVisible(false);
	} else {
		dbgRegLabs[i]->setProperty("isbit", false);
		ui_cpu.formRegs->addWidget(dbgRegEdit[i], row, col + 1);
		dbgRegEdit[i]->setReadOnly(b->regs[i].flag & REG_RO);
		dbgRegEdit[i]->setVisible(true);
		dbgRegBits[i]->setVisible(false);
	}
}

// index of a register in the bunch, -1 if it is not there
static int find_bunch_reg(xRegBunch* b, int cnt, int id) {
	for (int i = 0; i < cnt; i++) {
		if (b->regs[i].id == id) return i;
	}
	return -1;
}

void DebugWin::reFormCPU(xRegBunch* b) {
	// Dock sizes from a restored layout only arrive on the first show, and the
	// column count follows the panel width. Building earlier builds one column,
	// which is tall enough to push the docks below it out of place for good.
	if (!winShown) {
		reformWait = 1;
		return;
	}
	int i;
	int cnt = 0;			// visible registers (bunch holds no REG_EMPTY ones)
	int labw = 0;
	int fldw = 0;
	// 1st pass: set names and sizes, measure the widest name and value
	while ((cnt < dbgRegLabs.size()) && (b->regs[cnt].id != REG_EOT)) {
		dbgRegLabs[cnt]->setText(b->regs[cnt].name);
		dbgRegLabs[cnt]->setProperty("regid", b->regs[cnt].id);
		int mx;
		switch(b->regs[cnt].size) {
			case REG_2: mx = 2; break;
			case REG_BYTE: mx = 0xff; break;
			case REG_24: mx = 0xffffff; break;
			case REG_32: mx = 0xffffffff; break;
			default: mx = 0xffff; break;
		}
		// setMax resets the input mask and the text with it, so don't
		// touch it on a simple reflow: it would drop the 'changed' color
		if (dbgRegEdit[cnt]->getMax() != mx)
			dbgRegEdit[cnt]->setMax(mx);
		if (dbgRegLabs[cnt]->sizeHint().width() > labw) labw = dbgRegLabs[cnt]->sizeHint().width();
		if (dbgRegEdit[cnt]->minimumWidth() > fldw) fldw = dbgRegEdit[cnt]->minimumWidth();
		cnt++;
	}
	regPairW = labw + fldw;
	// two columns is as wide as the panel ever needs to be, but the flags row
	// (8 of them side by side) may still ask for more
	regWideW = regPairW * 2 + RCOL_GAP;
	int flgw = dbgFlagBox[0]->sizeHint().width() * 8 + 7 * 2;
	if (flgw > regWideW) regWideW = flgw;
	// never allow more than two columns: dragging further would only add empty space
	wid_cpu->setMaximumWidth(regWideW);
	// only apply the mode here. Recomputing it would change the minimum, which
	// resizes the panel, which lands back in this function through eventFilter
	wid_cpu->setMinimumWidth(cpuWideDock ? regWideW : (regPairW + RCOL_GAP));
	// same value is the switch point: sizes are only exact once the widgets are
	// styled, two thresholds could drift apart and lock the second column out
	regCols = (wid_cpu->width() >= regWideW) ? 2 : 1;

	// 2nd pass: (re)build the layout
	delete ui_cpu.verticalLayout->takeAt(VLI_REGS);	// registers stay alive, wid_cpu owns them
	ui_cpu.formRegs = new QGridLayout;
	ui_cpu.formRegs->setHorizontalSpacing(0);
	ui_cpu.formRegs->setVerticalSpacing(2);
	ui_cpu.formRegs->setColumnMinimumWidth(2, RCOL_GAP);
	ui_cpu.formRegs->setColumnStretch(5, 1);	// keep registers packed to the left
	ui_cpu.verticalLayout->insertLayout(VLI_REGS, ui_cpu.formRegs);
	QVector<char> done(dbgRegLabs.size(), 0);
	int row = 0;
	for (i = 0; i < cnt; i++) {
		if (done[i]) continue;			// already taken as someone's pair
		placeReg(b, i, row, RCOL_LEFT);
		done[i] = 1;
		if ((regCols > 1) && b->regs[i].pair) {
			int p = find_bunch_reg(b, cnt, b->regs[i].pair);
			if ((p >= 0) && !done[p]) {
				placeReg(b, p, row, RCOL_RIGHT);
				done[p] = 1;
			}
		}
		row++;
	}
	for (i = cnt; i < dbgRegLabs.size(); i++) {	// rest of the widgets is unused
		dbgRegLabs[i]->setVisible(false);
		dbgRegBits[i]->setVisible(false);
		dbgRegEdit[i]->setVisible(false);
	}
	reFormFlags((regCols > 1) ? 8 : 4);
}

// Reset from the menu: put the panels back and take the window back to the size
// that arrangement asks for. Without the resize the window keeps whatever the
// dragged-apart layout had stretched it to.
void DebugWin::resetLayout() {
	setDefaultLayout();
	QTimer::singleShot(0, this, [this]() {
		// same size the config hands out on a first run, never below what fits
		resize(QSize(960, 720).expandedTo(minimumSizeHint()));
	});
}

// A narrow strip of nothing, pinned to a window edge. QMainWindow works out
// drop zones from the docks already there, so once panels cover the whole
// window there is nowhere to aim for "outside everything" - a new outermost
// column. An anchor is that target: drop beside it and the column appears.
// Fixed, featureless and title-less, so it cannot be dragged or resized away.
static QDockWidget* make_edge_anchor(const char* name) {
	QDockWidget* dock = new QDockWidget;
	dock->setObjectName(name);
	dock->setTitleBarWidget(new QWidget);		// empty: no title bar at all
	dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
	QWidget* pad = new QWidget;
	pad->setFixedWidth(4);
	dock->setWidget(pad);
	return dock;
}

// The shipped arrangement: cpu on the left, [ dump / brk ] [ misc / stack ] on
// the right. Also the way back when the docks have been dragged into a corner
// an empty area gives no drop zone to return to.
void DebugWin::setDefaultLayout() {
	foreach (xDockWidget* dw, dockWidgets) {
		// a dock reports itself floating until it is added to a main window, and
		// un-floating one that has no main window yet crashes. parentWidget()
		// tells the two apart: null before the first addDockWidget
		if (dw->parentWidget() && dw->isFloating()) dw->setFloating(false);
	}
	// anchors first and outermost, then everything else splits in between them
	addDockWidget(Qt::LeftDockWidgetArea, wid_anchor_l);
	splitDockWidget(wid_anchor_l, wid_anchor_r, Qt::Horizontal);
	splitDockWidget(wid_anchor_l, wid_cpu_dock, Qt::Horizontal);
	splitDockWidget(wid_cpu_dock, wid_dasm_dock, Qt::Horizontal);
	splitDockWidget(wid_dasm_dock, wid_dump, Qt::Horizontal);
	splitDockWidget(wid_dump, wid_misc, Qt::Horizontal);
	splitDockWidget(wid_dump, wid_brk, Qt::Vertical);
	splitDockWidget(wid_misc, wid_stack, Qt::Vertical);

	tabifyDockWidget(wid_dump, wid_rdump);
	tabifyDockWidget(wid_dump, wid_disk_dump);
	tabifyDockWidget(wid_dump, wid_vmem_dump);
	tabifyDockWidget(wid_dump, wid_cmos_dump);
	tabifyDockWidget(wid_brk, wid_zxscr);
	tabifyDockWidget(wid_brk, wid_ay);
	tabifyDockWidget(wid_brk, wid_tape);
	tabifyDockWidget(wid_brk, wid_fdd);
	tabifyDockWidget(wid_brk, wid_mmap);
	tabifyDockWidget(wid_brk, wid_gb);
	tabifyDockWidget(wid_brk, wid_gbv);
	tabifyDockWidget(wid_brk, wid_ppu);
	tabifyDockWidget(wid_brk, wid_cia);
	tabifyDockWidget(wid_brk, wid_dma);
	tabifyDockWidget(wid_brk, wid_pic);
	tabifyDockWidget(wid_brk, wid_pit);
	tabifyDockWidget(wid_brk, wid_vga);
	tabifyDockWidget(wid_brk, wid_ps2);
	tabifyDockWidget(wid_brk, wid_pal);
	wid_dump->raise();
	wid_brk->raise();

	// Qt hands every dock in a column an equal share, which parks a short panel
	// like STACK in the middle of empty space. Ask for the heights the contents
	// actually want and let the taller neighbour keep the slack.
	QList<QDockWidget*> col;
	QList<int> hgt;
	col << wid_misc << wid_stack;
	// the top one gets exactly its content, the bottom one swallows the rest,
	// so the two sit against each other instead of being spread apart
	hgt << wid_misc->widget()->sizeHint().height() << 10000;
	resizeDocks(col, hgt, Qt::Vertical);

	QList<QDockWidget*> row;
	QList<int> wdt;
	row << wid_cpu_dock << wid_dasm_dock << wid_dump << wid_misc;
	wdt << 1 << 5 << 5 << 1;		// relative, resizeDocks normalises them
	resizeDocks(row, wdt, Qt::Horizontal);

	// Nothing sits under the cpu panel in this arrangement, so the narrow mode
	// is known: set it outright. Measuring instead would race the layout - the
	// geometry still describes the old arrangement for another cycle or two,
	// and this runs on paths where no dock changes area, so nothing would come
	// along later to correct the guess.
	// Nothing sits under the cpu panel in this arrangement, so the narrow mode
	// is known: set it here, while still inside the call that rearranges the
	// docks. Deferring it to the event loop lowers the minimum only after the
	// layout pass has already handed the panel a two column width, and a
	// smaller minimum does not shrink a widget that is already wider.
	cpuWideDock = 0;
	if (regPairW) wid_cpu->setMinimumWidth(regPairW + RCOL_GAP);
}

// Two panels sitting side by side under the cpu dock is the cue for the wide
// (two column) register layout: demand the width that needs, so the row below
// can split in half. A single panel under it keeps the column narrow.
void DebugWin::updateCpuDockWidth() {
	if (!regPairW || !regWideW) return;
	QRect cr = wid_cpu_dock->geometry();
	// only what sits under the cpu panel counts: a dock merely standing beside it
	// (the disassembler, say) says nothing about how wide the column should be
	QList<QRect> below;
	foreach (xDockWidget* dw, dockWidgets) {
		if ((dw == wid_cpu_dock) || dw->isHidden() || dw->isFloating()) continue;
		QRect r = dw->geometry();
		if (r.top() < cr.bottom()) continue;			// not below
		if ((r.left() >= cr.right()) || (cr.left() >= r.right())) continue;	// not in this column
		below << r;
	}
	bool wide = false;
	for (int i = 0; !wide && (i < below.size()); i++) {
		for (int j = i + 1; j < below.size(); j++) {
			QRect a = below.at(i);
			QRect b = below.at(j);
			// overlapping vertical bands = the two are next to each other
			if ((a.top() < b.bottom()) && (b.top() < a.bottom())) {
				wide = true;
				break;
			}
		}
	}
	if (wide == !!cpuWideDock) return;
	cpuWideDock = wide ? 1 : 0;
	wid_cpu->setMinimumWidth(wide ? regWideW : (regPairW + RCOL_GAP));
}

// the dock sizes are in place now, so the cpu panel can be built at the
// width it is really going to have
void DebugWin::showEvent(QShowEvent* ev) {
	QMainWindow::showEvent(ev);
	winShown = 1;
	if (reformWait && conf.prof.cur && conf.prof.cur->zx) {
		reformWait = 0;
		xRegBunch bunch = cpuGetRegs(conf.prof.cur->zx->cpu);
		reFormCPU(&bunch);
	}
}

// cpu panel resized: reflow if another number of columns fits now
bool DebugWin::eventFilter(QObject* obj, QEvent* ev) {
	// isVisible: while the window is hidden the panel is squeezed to its minimum
	if ((obj == wid_cpu) && (ev->type() == QEvent::Resize) && isVisible()
			&& regPairW && conf.prof.cur && conf.prof.cur->zx) {
		int cols = (wid_cpu->width() >= regWideW) ? 2 : 1;
		// Never rebuild from inside the resize event. reFormCPU() deletes and
		// re-adds layout items, and a separator drag delivers a stream of
		// resizes while Qt is walking the dock layout - mutating it underneath
		// leaves that walk holding freed items
		if ((cols != regCols) && !reformPending) {
			reformPending = 1;
			QTimer::singleShot(0, this, &DebugWin::reformCpuLater);
		}
	}
	return QMainWindow::eventFilter(obj, ev);
}

void DebugWin::reformCpuLater() {
	reformPending = 0;
	if (!conf.prof.cur || !conf.prof.cur->zx) return;
	if (((wid_cpu->width() >= regWideW) ? 2 : 1) == regCols) return;	// settled meanwhile
	xRegBunch bunch = cpuGetRegs(conf.prof.cur->zx->cpu);
	reFormCPU(&bunch);	// values are kept, no fillCPU: it would clear the 'changed' color
}

void DebugWin::fillCPU() {
	block = 1;
//	Computer* comp = conf.prof.cur->zx;
	CPU* cpu = conf.prof.cur->zx->cpu;
	xRegBunch bunch = cpuGetRegs(cpu);
#if 1
	if (cpu->core != curCpuCore) {
		curCpuCore = cpu->core;
		reFormCPU(&bunch);
	}
	int c = 0;
	int r = 0;
	while (bunch.regs[c].id != REG_EOT) {
		if (bunch.regs[c].id != REG_EMPTY) {
			if (bunch.regs[c].size == REG_BIT) {
				dbgRegBits[r]->setChecked(bunch.regs[c].value);
			} else {
				dbgRegEdit[r]->setValue(bunch.regs[c].value);
			}
			r++;
		}
		c++;
	}

#else
	xRegister* rp;
	int i;
	int t;
	bool f;
	for (i = 0; i < dbgRegLabs.size(); i++) {
		rp = &bunch.regs[i];
		switch (rp->id) {
			case REG_EMPTY:
			case REG_NONE:
				dbgRegLabs[i]->setVisible(false);
				dbgRegEdit[i]->setVisible(false);
				dbgRegBits[i]->setVisible(false);
				break;
			default:
				dbgRegLabs[i]->setText(rp->name);
				dbgRegLabs[i]->setProperty("regid", rp->id);
				t = rp->type;
				switch (t) {
					case REG_2: dbgRegEdit[i]->setMax(2); break;
					case REG_BYTE: dbgRegEdit[i]->setMax(0xff); break;
					case REG_24: dbgRegEdit[i]->setMax(0xffffff); break;
					case REG_32: dbgRegEdit[i]->setMax(0xffffffff); break;
					default: dbgRegEdit[i]->setMax(0xffff); break;
				}
				f = !!(rp->flag & REG_RO);
				dbgRegLabs[i]->setVisible(true);
				// NOTE: setWidget return a error if a cell is occuped
				// NOTE: takeRow exists from Qt5.8
				if (t == REG_BIT) {
					if (!dbgRegLabs[i]->property("isbit").toBool()) {
						ui_cpu.formRegs->takeRow(i);
						ui_cpu.formRegs->insertRow(i, dbgRegLabs[i], dbgRegBits[i]);
						dbgRegLabs[i]->setProperty("isbit", true);
					}
					dbgRegBits[i]->setCheckable(!f);		// if read only
					dbgRegBits[i]->setChecked(rp->value);
					dbgRegBits[i]->setVisible(true);
					dbgRegEdit[i]->setVisible(false);
				} else {
					if (dbgRegLabs[i]->property("isbit").toBool()) {
						ui_cpu.formRegs->takeRow(i);
						ui_cpu.formRegs->insertRow(i, dbgRegLabs[i], dbgRegEdit[i]);
						dbgRegLabs[i]->setProperty("isbit", false);
					}
					dbgRegEdit[i]->setValue(rp->value);
					dbgRegEdit[i]->setReadOnly(f);
					dbgRegEdit[i]->setVisible(true);
					dbgRegBits[i]->setVisible(false);
				}
				break;
		}
	}
#endif
	fillFlags(bunch.flags);
	fillStack();
	block = 0;
}

// called only from connection. checkboxes to cpu->f
void DebugWin::setFlags() {
	if (block) return;
	unsigned short f = 0;
	for (int i = 0; i < 16; i++) {
		if (dbgFlagBox[i]->isVisible() && dbgFlagBox[i]->isChecked())
			f |= (1 << i);
	}
	cpu_set_flag(conf.prof.cur->zx->cpu, f);
	fillCPU();
}

void DebugWin::setCPU() {
	if (block) return;
	Computer* comp = conf.prof.cur->zx;
	CPU* cpu = comp->cpu;
	int i = 0;
	xRegBunch bunch;
	foreach(xLabel* xlb, dbgRegLabs) {
		if (xlb->isVisible()) {
			bunch.regs[i].id = xlb->property("regid").toInt();
			if (xlb->property("isbit").toBool()) {
				bunch.regs[i].value = dbgRegBits[i]->isChecked() ? 1 : 0;
			} else {
				bunch.regs[i].value = dbgRegEdit[i]->getValue();
			}
			i++;
		} else {
			bunch.regs[i].id = REG_EOT;
		}
	}
	cpuSetRegs(cpu, bunch);
	fillFlags(NULL);
	fillStack();
	fillDisasm();
}

// memory map section

QString getPageName(MemPage& pg) {
	QString res;
	switch(pg.type) {
		case MEM_RAM: res = "RAM:"; break;
		case MEM_ROM: res = "ROM:"; break;
		case MEM_EXT: res = "EXT:"; break;
		case MEM_SLOT: res = "SLT:"; break;
		default: res = "---:"; break;
	}
	res.append(gethexbyte(pg.num >> 6));
	return res;
}

void DebugWin::fillMem() {
	Computer* comp = conf.prof.cur->zx;
	ui_misc.labPG0->setText(getPageName(comp->mem->map[0x00]));
	ui_misc.labPG1->setText(getPageName(comp->mem->map[0x40]));
	ui_misc.labPG2->setText(getPageName(comp->mem->map[0x80]));
	ui_misc.labPG3->setText(getPageName(comp->mem->map[0xc0]));
}

void DebugWin::loadMap() {
	QString path = QFileDialog::getOpenFileName(this, "Open the universe", "", "Xpeccy memory map (*.xmap)",nullptr,QFileDialog::DontUseNativeDialog);
	if (path.isEmpty()) return;
	load_xmap(path);
	xmap_path = path;
	brkInstallAll();
	fillAll();
}

void DebugWin::saveMap() {
	QString path = QFileDialog::getSaveFileName(this, "Save the universe", xmap_path, "Xpeccy memory map (*.xmap)",nullptr,QFileDialog::DontUseNativeDialog);
	if (path.isEmpty()) return;
	if (!path.endsWith(".xmap",Qt::CaseInsensitive))
		path.append(".xmap");
	save_xmap(path);
	xmap_path = path;
}

// labels

void DebugWin::dbgLLab() {
	if (!loadLabels(NULL)) {
		shitHappens("Can't open file");
	}
	fillDisasm();
}
void DebugWin::dbgSLab() {saveLabels(NULL);}

void DebugWin::jumpToLabel(QString lab) {
	xAdr xadr = find_label(lab);
	if (xadr.type >= 0) {
		int cadr = memFindAdr(conf.prof.cur->zx->mem, xadr.type, xadr.abs);
		if (cadr >= 0) {
			ui_asm.dasmTable->setAdr(cadr, 1);
		}
	}
}

// disasm table

int rdbyte(int adr, void* ptr) {
	Computer* comp = (Computer*)ptr;
	int res = -1;
//	if (comp->hw->id == HW_IBM_PC) {
//		res = comp->hw->mrd(comp, adr, 0);
//	} else {
		MemPage* pg = mem_get_page(comp->mem, adr);	// = &comp->mem->map[(adr >> 8) & 0xff];
		int fadr = mem_get_phys_adr(comp->mem, adr);	// = pg->num << 8) | (adr & 0xff);
		switch (pg->type) {
			case MEM_RAM: res = comp->mem->ramData[fadr & comp->mem->ramMask]; break;
			case MEM_ROM: res = comp->mem->romData[fadr & comp->mem->romMask]; break;
			case MEM_SLOT:
				if (!comp->slot) break;
				if (!comp->slot->data) break;
				res = sltRead(comp->slot, SLT_PRG, adr & 0xffff); break;
		}
//	}
	return res;
}

int DebugWin::fillDisasm() {
	conf.dbg.dimadr = ui_asm.actDimAddr->isChecked() ? 1 : 0;
	conf.dbg.dimops = ui_asm.actDimOps->isChecked() ? 1 : 0;
	conf.dbg.synhl = ui_asm.actSyntax->isChecked() ? 1 : 0;
	conf.dbg.blocksep = ui_asm.actBlockSep->isChecked() ? 1 : 0;
	conf.dbg.labels = ui_asm.actShowLabels->isChecked() ? 1 : 0;
	return ui_asm.dasmTable->updContent();
}

void DebugWin::saveDasm() {
	QString path = QFileDialog::getSaveFileName(this, "Save disasm",QString(),QString(),nullptr,QFileDialog::DontUseNativeDialog);
	if (path.isEmpty()) return;
	QFile file(path);
	dasmData drow;
	QList<dasmData> list;
	Computer* comp = conf.prof.cur->zx;
	if (file.open(QFile::WriteOnly)) {
		QTextStream strm(&file);
		int adr = (blockStart < 0) ? 0 : (blockStart & comp->mem->busmask);
		int end = (blockEnd < 0) ? comp->mem->busmask : (blockEnd & comp->mem->busmask);
		int work = 1;
		strm << "; Created by Xpeccy+ deBUGa\n\n";
		strm << "\tORG 0x" << gethexword(adr) << "\n\n";
		while ((adr <= end) && work) {
			list = getDisasm(comp, adr);
			foreach (drow, list) {
				if (adr > comp->mem->busmask)
					work = 0;		// address overfill (FFFF+)
				if (drow.isequ) {
					strm << drow.aname << ":";
					strm << drow.command;
				} else if (drow.islab) {
					if (drow.iscom) {
						strm << drow.aname;
					} else {
						strm << drow.aname << ":";
					}
				} else {
					strm << "\t" << drow.command;
				}
				strm << "\n";
			}
		}
		file.close();
	} else {
		shitHappens("Can't write to file");
	}
}

// memory dump

/*
void DebugWin::fillDump() {
	block = 1;
	ui.dumpTable->update();
	fillStack();
	dumpChadr(ui.dumpTable->getAdr());
	block = 0;
}

void DebugWin::dumpChadr(int adr) {
	ui.dumpScroll->setValue(adr);
	QModelIndex idx = ui.dumpTable->selectionModel()->currentIndex();
	int col = idx.column();
	adr += idx.row() << 3;
	if ((col > 0) && (col < 9)) {
		 adr += (col - 1);
	}
	if (ui.dumpTable->mode != XVIEW_CPU) {
		adr &= 0x3fff;
	} else {
		adr %= ui.dumpTable->limit();
	}
	ui.tabsDump->setTabText(0, QString::number(adr, 16).right(6).toUpper().rightJustified(6,'0'));
}
*/

// maping

void DebugWin::mapClear() {
	if (!areSure("Clear memory mapping?")) return;
	Computer* comp = conf.prof.cur->zx;
	int adr;
	for (adr = 0; adr < 0x400000; adr++) {
		comp->brkRamMap[adr] &= 0x0f;
		if (adr < 0x80000) comp->brkRomMap[adr] &= 0x0f;
		if (comp->slot->data && (adr <= comp->slot->memMask))
			comp->slot->brkMap[adr] &= 0x0f;
	}
	fillDisasm();
}

void DebugWin::mapAuto() {

}

// memory heat-map (read/write/exec usage counters)

void DebugWin::heatToggle(bool on) {
	Computer* comp = conf.prof.cur->zx;
	comp->flgHEAT = on;
	if (on) comp_heat_sync(comp);		// make sure banks are allocated for current hardware
}

void DebugWin::heatReset() {
	if (!areSure("Reset memory heat-map counters?")) return;
	comp_heat_reset(conf.prof.cur->zx);
}

void DebugWin::heatExport() {
	QString path = QFileDialog::getSaveFileName(this, "Export memory heat-map", QString(), "Heat-map CSV (*.csv)", nullptr, QFileDialog::DontUseNativeDialog);
	if (path.isEmpty()) return;
	if (!path.endsWith(".csv", Qt::CaseInsensitive))
		path.append(".csv");
	if (comp_heat_save(conf.prof.cur->zx, path.toLocal8Bit().data()) != 0)
		shitHappens("Can't write heat-map file");
}

// stack

void DebugWin::fillStack() {
	Computer* comp = conf.prof.cur->zx;
	int adr = cpu_get_sp(comp->cpu) + comp->cpu->ss.base;
	QString str;
	for (int i = -2; i < 16; i+=2) {
		str.append(gethexbyte(rdbyte(adr+i+1, comp)));
		str.append(gethexbyte(rdbyte(adr+i, comp)));
	}
	ui_stack.labSPm2->setText(str.left(4));
	ui_stack.labSP->setText(str.mid(4,4));
	ui_stack.labSP2->setText(str.mid(8,4));
	ui_stack.labSP4->setText(str.mid(12,4));
	ui_stack.labSP6->setText(str.mid(16,4));
	ui_stack.labSP8->setText(str.mid(20,4));
	ui_stack.labSP10->setText(str.mid(24,4));
	ui_stack.labSP12->setText(str.mid(28,4));
	ui_stack.labSP14->setText(str.mid(32,4));
}

// ports

void DebugWin::fillPorts() {
	Computer* comp = conf.prof.cur->zx;
	xPortValue* tab = hwGetPorts(comp);
	QLabel* wid;
	int i = 0;
	int cnt = ui_misc.formPort->rowCount();
	if (tab) {
		if (tab[0].port > 0) {
			while (tab[i].port > 0) {
				if (i >= cnt) {
					ui_misc.formPort->addRow(new QLabel, new QLabel);
					cnt++;
				}
				wid = (QLabel*)(ui_misc.formPort->itemAt(i, QFormLayout::LabelRole)->widget());
				wid->setVisible(true);
				wid->setText(gethexword(tab[i].port));
				wid = (QLabel*)(ui_misc.formPort->itemAt(i, QFormLayout::FieldRole)->widget());
				wid->setVisible(true);
				wid->setText(gethexbyte(tab[i].value));
				i++;
			}
			ui_misc.labPorts->setVisible(true);
		} else {
			ui_misc.labPorts->setVisible(false);
		}
	} else {
		ui_misc.labPorts->setVisible(false);
	}
	while (i < cnt) {
		((QLabel*)(ui_misc.formPort->itemAt(i, QFormLayout::LabelRole)->widget()))->setVisible(false);
		((QLabel*)(ui_misc.formPort->itemAt(i, QFormLayout::FieldRole)->widget()))->setVisible(false);
		//ui_misc.formPort->takeAt(i); // removeRow(i);
		i++;
	}
}

// breakpoint

int DebugWin::getAdr() {
	int adr;
//	int col;
	QModelIndex idx;
	Computer* comp = conf.prof.cur->zx;

//	if (ui.dumpTable->hasFocus()) {
//		idx = ui.dumpTable->currentIndex();
//		col = idx.column();
//		adr = (ui.dumpTable->getAdr() + (idx.row() << 3));
//		if ((col > 0) && (col < 9)) {
//			adr += idx.column() - 1;
//		}
//	} else {
		idx = ui_asm.dasmTable->currentIndex();
		adr = ui_asm.dasmTable->getData(idx.row(), 0, Qt::UserRole).toInt();		// already +cs.base
//	}

	adr &= comp->mem->busmask;
	return adr;
}

void DebugWin::brkRequest(int t, int m, int a) {
	int bgn, end, fadr;
	if ((a < blockStart) || (a > blockEnd)) {	// pointer outside block : process 1 cell
		bgn = a;
		end = a;
	} else {								// pointer inside block : process all block
		bgn = blockStart;
		end = blockEnd;
	}
	if (end < bgn) {
		fadr = bgn;
		bgn = end;
		end = fadr;
	}
	brkSet(t, m, bgn, end);
	fillDisasm();
	wid_dump->draw();
	wid_brk->draw();
}

void DebugWin::putBreakPoint() {
	int adr = getAdr();
	if (adr < 0) return;
	doBreakPoint(adr);
	cellMenu->move(QCursor::pos());
	cellMenu->show();
}

void DebugWin::doBreakPoint(unsigned short adr) {
//	bpAdr = adr;
	unsigned char flag = getBrk(conf.prof.cur->zx, adr);
	ui_asm.actFetch->setChecked(flag & MEM_BRK_FETCH);
	ui_asm.actRead->setChecked(flag & MEM_BRK_RD);
	ui_asm.actWrite->setChecked(flag & MEM_BRK_WR);
}

// TODO: breakpoints on block
void DebugWin::chaCellProperty(QAction* act) {
	int data = act->data().toInt();		// flag to change. b4..7 = type, b0..3 = brk
	int adr = getAdr();
	int bgn, end;
	unsigned char bt;
	int fadr;
	unsigned char* ptr;
	if ((adr < blockStart) || (adr > blockEnd)) {	// pointer outside block : process 1 cell
		bgn = adr;
		end = adr;
	} else {								// pointer inside block : process all block
		bgn = blockStart;
		end = blockEnd;
	}
	if (end < bgn) {
		fadr = bgn;
		bgn = end;
		end = fadr;
	}
//	int proc = 1;
	bt = 0;
	xAdr xadr;
	xAdr xend;
	Computer* comp = conf.prof.cur->zx;
	if (ui_asm.actFetch->isChecked()) bt |= MEM_BRK_FETCH;
	if (ui_asm.actRead->isChecked()) bt |= MEM_BRK_RD;
	if (ui_asm.actWrite->isChecked()) bt |= MEM_BRK_WR;
	adr = bgn;
	if (data & MEM_BRK_ANY) {				// if set breakpoint
		if (ui_asm.dasmTable->hasFocus()) {			// from disasm table
			xadr = mem_get_xadr(comp->mem, bgn);
			xend = mem_get_xadr(comp->mem, end);
			if (xadr.type == MEM_ROM) {
				bt |= MEM_BRK_ROM;
			} else if (xadr.type == MEM_RAM) {
				bt |= MEM_BRK_RAM;
			} else {
				bt |= MEM_BRK_SLT;
			}
			brkSet(BRK_MEMCELL, bt, xadr.abs, xend.abs);
//		} else if (ui.dumpTable->hasFocus()) {		// from dump table
//			int fadr = getRFIData(ui.cbDumpView);
//			switch(fadr) {	// XVIEW_RAM/ROM/CPU
//				case XVIEW_CPU:
//					xadr = mem_get_xadr(comp->mem, bgn);
//					xend = mem_get_xadr(comp->mem, end);
//					switch (xadr.type) {
//						case MEM_ROM: bt |= MEM_BRK_ROM; break;
//						case MEM_RAM: bt |= MEM_BRK_RAM; break;
//						default: bt |= MEM_BRK_SLT; break;
//					}
//					brkSet(BRK_MEMCELL, bt, xadr.abs, xend.abs);
//					break;
//				case XVIEW_ROM:
//				case XVIEW_RAM: bt |= (fadr == XVIEW_ROM) ? MEM_BRK_ROM : MEM_BRK_RAM;
//					brkSet(BRK_MEMCELL, bt, bgn, end);
//					break;
//			}
		}
	} else {						// change cell type
		while (adr <= end) {
			ptr = getBrkPtr(comp, adr);
			*ptr &= 0x0f;
			if ((data & 0xf0) == DBG_VIEW_TEXT) {
				bt = rdbyte(adr, comp);
				if ((bt < 32) || (bt > 127)) {
					*ptr |= DBG_VIEW_BYTE;
				} else {
					*ptr |= DBG_VIEW_TEXT;
				}
			} else {
				*ptr |= (data & 0xf0);
			}
			adr++;
		}
	}
	wid_brk->draw();
	//ui.bpList->update();
	fillDisasm();
	wid_dump->draw();
	//fillDump();
//	fillBrkTable();
}

// memDump

//void DebugWin::doSaveDump() {
//	dumpwin->show();
//}

void DebugWin::dmpLimChanged() {
	int start = dui.leStart->getValue();
	int end = dui.leEnd->getValue();
	if (end < start) end = start;
	int len = end - start + 1;
	start = dui.leEnd->cursorPosition();
	dui.leEnd->setValue(end);
	dui.leLen->setValue(len);
	dui.leEnd->setCursorPosition(start);
}

void DebugWin::dmpLenChanged() {
	int start = dui.leStart->getValue();
	int len = dui.leLen->getValue();
	int end = dui.leEnd->getMax();
	if (start + len >= end) {
		len = end - start;
		dui.leLen->setValue(len);
	}
	end = start + len - 1;
	start = dui.leLen->cursorPosition();
	dui.leEnd->setValue(end);
	dui.leLen->setCursorPosition(start);
}

QByteArray DebugWin::getDumpData() {
	//int bank = dui.leBank->text().toInt(NULL,16);
	int adr = dui.leStart->getValue();
	int len = dui.leLen->getValue();
	QByteArray res;
	while (len > 0) {
		//if (adr < 0xc000) {
		res.append(rdbyte(adr, conf.prof.cur->zx));
		//} else {
		//	res.append(comp->mem->ramData[(bank << 14) | (adr & 0x3fff)]);
		//}
		adr++;
		len--;
	}
	return res;
}

void DebugWin::saveDumpBin() {
	QByteArray data = getDumpData();
	if (data.size() == 0) return;
	QString path = QFileDialog::getSaveFileName(this,"Save memory dump",QString(),QString(),nullptr,QFileDialog::DontUseNativeDialog);
	if (path.isEmpty()) return;
	QFile file(path);
	if (file.open(QFile::WriteOnly)) file.write(data);
	dumpwin->hide();
}

void DebugWin::saveDumpHobeta() {
	QByteArray data = getDumpData();
	if (data.size() == 0) return;
	QString path = QFileDialog::getSaveFileName(this,"Save memory dump as hobeta","","Hobeta files (*.$C)",nullptr,QFileDialog::DontUseNativeDialog);
	if (path.isEmpty()) return;
	TRFile dsc;
	QString name = dui.leStart->text();
	// name.append(".").append(dui.leBank->text());
	std::string nms = name.toStdString();
	nms.resize(8,' ');
	memcpy(dsc.name,nms.c_str(),8);
	dsc.ext = 'C';
	int start = dui.leStart->getValue();
	int len = data.size();
	dsc.hst = (start >> 8) & 0xff;
	dsc.lst = start & 0xff;
	dsc.hlen = (len >> 8) & 0xff;
	dsc.llen = len & 0xff;
	dsc.slen = dsc.hlen + ((len & 0xff) ? 1 : 0);
	saveHobeta(dsc, data.data(), path.toStdString().c_str());
	dumpwin->hide();
}

void DebugWin::saveDumpToA() {saveDumpToDisk(0);}
void DebugWin::saveDumpToB() {saveDumpToDisk(1);}
void DebugWin::saveDumpToC() {saveDumpToDisk(2);}
void DebugWin::saveDumpToD() {saveDumpToDisk(3);}

void DebugWin::saveDumpToDisk(int idx) {
	QByteArray data = getDumpData();
	if (data.size() == 0) return;
	if (data.size() > 0xff00) return;
	int start = dui.leStart->getValue();
	int len = dui.leLen->getValue();
	QString name = dui.leStart->text();
	// name.append(".").append(dui.leBank->text());
	Floppy* flp = conf.prof.cur->zx->dif->flp[idx & 3];
	if (!flp->insert) {
		flp_insert(flp, NULL);
		trd_format(flp);
	}
	TRFile dsc = diskMakeDescriptor(name.toStdString().c_str(), 'C', start, len);
	if (diskCreateFile(flp, dsc, (unsigned char*)data.data(), data.size()) == ERR_OK)
		dumpwin->hide();

}

// videoram

void DebugWin::saveVRam() {
	QString path = QFileDialog::getSaveFileName(this, "Save video ram", "", "All files (*)", nullptr, QFileDialog::DontUseNativeDialog);
	if (path.isEmpty()) return;
	QFile file(path);
	if (file.open(QFile::WriteOnly)) {
		file.write((char*)conf.prof.cur->zx->vid->ram, MEM_256K);
		file.close();
	}
}

// memfinder

void DebugWin::doFind() {
	Computer* comp = conf.prof.cur->zx;
	memFinder->mem = comp->mem;
	if (memFinder->adr < 0)
		memFinder->adr = (ui_asm.dasmTable->getAdr() + 1) & comp->mem->busmask;
	memFinder->show();
}

void DebugWin::onFound(int adr) {
	ui_asm.dasmTable->setAdr(adr);
	wid_dump->setAdr(adr);
	// ui.dumpTable->setAdr(adr);
}

// memfiller

void DebugWin::doFill() {
	Computer* comp = conf.prof.cur->zx;
	memFiller->start(comp->mem, blockStart, blockEnd);
}

// spr scanner

void DebugWin::doMemView() {
	Computer* comp = conf.prof.cur->zx;
	memViewer->mem = comp->mem;
	memViewer->ui.sbPage->setValue(comp->mem->map[0xc0].num >> 6);
	memViewer->fillImage();
	memViewer->show();
}

// open dump

void dbg_mem_wr(Computer* comp, int adr, unsigned char bt) {
	MemPage* pg = mem_get_page(comp->mem, adr);	// = &comp->mem->map[(adr >> 8) & 0xff];
	int fadr = mem_get_phys_adr(comp->mem, adr);	// = pg->num << 8) | (adr & 0xff);
	switch (pg->type) {
		case MEM_RAM:
			comp->mem->ramData[fadr & comp->mem->ramMask] = bt;
			break;
		case MEM_ROM:
			if (conf.dbg.romwr)
				comp->mem->romData[fadr & comp->mem->romMask] = bt;
			break;
	}
}

int loadDUMP(Computer* comp, const char* name, int adr) {
	FILE* file = fopen(name, "rb");
	if (!file) return ERR_CANT_OPEN;
	int bt;
	while (adr < 0x10000) {
		bt = fgetc(file);
		if (feof(file)) break;
		dbg_mem_wr(comp, adr & 0xffff, bt & 0xff);
		adr++;
	}
	return ERR_OK;
}

void DebugWin::doOpenDump() {
	dumpPath.clear();
	oui.laPath->clear();
	oui.leStart->setText("C000");
	openDumpDialog->show();
}

void DebugWin::chDumpFile() {
	QString path = QFileDialog::getOpenFileName(this,"Open dump",QString(),QString(),nullptr,QFileDialog::DontUseNativeDialog);
	if (path.isEmpty()) return;
	QFileInfo inf(path);
	if ((inf.size() == 0) || (inf.size() > 0xff00)) {
		shitHappens("File is too long");
	} else {
		dumpPath = path;
		oui.laPath->setText(path);
		oui.leLen->setValue(inf.size() & 0xffff);
		dmpStartOpen();
	}
}

void DebugWin::dmpStartOpen() {
	int start = oui.leStart->getValue();
	int len = oui.leLen->getValue();
	int pos = oui.leStart->cursorPosition();
	int end = oui.leEnd->getMax();
	if (start + len > end) {
		start = end - len + 1;
	} else {
		end = start + len - 1;
	}
	oui.leStart->setValue(start);
	oui.leEnd->setValue(end);
	oui.leStart->setCursorPosition(pos);
}

void DebugWin::loadDump() {
	if (dumpPath.isEmpty()) return;
	int res = loadDUMP(conf.prof.cur->zx, dumpPath.toLocal8Bit().data(),oui.leStart->text().toInt(NULL,16));
	fillAll();
	if (res == ERR_OK) {
		openDumpDialog->hide();
	} else {
		shitHappens("Can't open file");
	}
}

// ps/2 widget (tmp here)

xPS2Widget::xPS2Widget(QString i, QString t, QWidget* p):xDockWidget(i,t,p) {
	QWidget* wid = new QWidget;
	setWidget(wid);
	ui.setupUi(wid);
	setObjectName("PS2WIDGET");
	hwList << HWG_PC;
}

QString get_hex_queue_z(unsigned long d) {
	QString r;
	while (d & 0xff) {
		if (!r.isEmpty()) r.append(",");
		r.append(gethexbyte(d & 0xff));
		d >>= 8;
	}
	return r;
}

QString get_hex_queue_n(unsigned long d, int l) {
	QString r;
	while (l > 0) {
		if (!r.isEmpty()) r.append(",");
		r.append(gethexbyte(d & 0xff));
		d >>= 8;
		l--;
	}
	return r;
}

void xPS2Widget::draw() {
	PS2Ctrl* ctrl = conf.prof.cur->zx->ps2c;
//	Keyboard* k = ctrl->kbd;
//	Mouse* m = ctrl->mouse;
	ui.lab_ps2ctrl->setText(getbinbyte(ctrl->ram[0x00]));
	ui.lab_ps2status->setText(getbinbyte(ctrl->status));
	ui.lab_ps2outbuf->setText(gethexbyte(ctrl->outbuf));
	ui.lab_ps2inbuf->setText(gethexbyte(ctrl->inbuf));
//	ui.lab_ps2kdata->setText(k->outbuf ? get_hex_queue_z(k->outbuf) : "-");
//	ui.lab_ps2mdata->setText(m->queueSize ? get_hex_queue_n(m->outbuf, m->queueSize) : "-");
}
