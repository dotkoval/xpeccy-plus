#include "dbg_heat.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOption>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFileDialog>

#include <math.h>

#define	HEAT_PAD	2	// margin around the raster
#define	HEAT_CELLMAX	8	// biggest square one byte is drawn as
#define	HEAT_GRIDMIN	3	// tile size the separator grid starts being drawn at
#define	HEAT_SWATCH	9	// legend swatch, in pixels
#define	HEAT_LEGGAP	8	// between two legend entries

// the ground and the quantized steps are the ones tools/heatmap_png.py draws
// with --style detail. the three counter colours are that tool's named palette
// (COL_READ/COL_WRITE/COL_EXEC), which it only uses for --style sectors: they
// are what a cell of one counter alone, at full intensity, comes out as here.
// pure #00ff00 - what the tool's own blend gives - glares on the light ground
#define	HEAT_BG		qRgb(248, 248, 248)
#define	HEAT_EMPTY	qRgb(214, 214, 214)	// in the bank, never touched
// a plainer grey than the viewer's own #e8e8e8, which is so close to the
// ground that untouched cells and the empty frame read as one
#define	HEAT_COL_RD	qRgb(46, 207, 46)
#define	HEAT_COL_WR	qRgb(255, 57, 57)
#define	HEAT_COL_EX	qRgb(36, 159, 159)
// marks drawn over the raster, so they belong to its palette rather than the
// interface style
#define	HEAT_COL_GRID	QColor(255, 255, 255)	// tile separators
#define	HEAT_COL_BOUND	QColor(0x60, 0x60, 0x60)	// 16K window boundaries
#define	HEAT_COL_HINT	QColor(0x80, 0x80, 0x80)	// "collecting is off"

// HEATCH_RGB shows all three counters; any other setting shows only itself
static bool heat_shown(int chan, int which) {
	return (chan == HEATCH_RGB) || (chan == which);
}

// VIEW

xHeatView::xHeatView(QWidget* par):QWidget(par) {
	mode = XVIEW_CPU;
	page = 0;
	chan = HEATCH_RGB;
	levels = 4;
	logscale = true;
	top = 0;
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QSize xHeatView::minimumSizeHint() const {
	return QSize(HEAT_BPR / 2, 64);
}

void xHeatView::setSource(int md, int pg) {
	mode = md;
	page = pg;
	top = 0;
	update();
}

void xHeatView::setChannel(int c) {
	chan = c;
	update();
}

void xHeatView::setLevels(int l) {
	if (l < 1) l = 1;
	levels = l;
	update();
}

void xHeatView::setLogScale(bool f) {
	logscale = f;
	update();
}

void xHeatView::setTop(int r) {
	int max = maxTop();
	if (r < 0) r = 0;
	if (r > max) r = max;
	if (r == top) return;
	top = r;
	update();
}

int xHeatView::getTop() const {
	return top;
}

int xHeatView::rowsTotal() const {
	Computer* comp = conf.prof.cur->zx;
	int size = (mode == XVIEW_CPU) ? (comp->mem->busmask + 1) : MEM_16K;
	return size / HEAT_BPR;
}

int xHeatView::maxTop() const {
	int max = rowsTotal() - rowsFit();
	return (max < 0) ? 0 : max;
}

// a byte is one cell wide, as wide as the panel allows: 256 of them fit in a
// dock 512 px across, and 1 px each below that
int xHeatView::cellW() const {
	int cw = (width() - 2 * HEAT_PAD) / HEAT_BPR;
	if (cw < 1) cw = 1;
	if (cw > HEAT_CELLMAX) cw = HEAT_CELLMAX;
	return cw;
}

// a cell may be taller than it is wide, up to 4:1: a 16K page is 64 rows and
// would otherwise sit as a thin strip in an empty panel. never shorter, and
// never taller than the source needs
int xHeatView::cellH() const {
	int cw = cellW();
	int total = rowsTotal();
	int ch = total ? ((height() - 2 * HEAT_PAD) / total) : cw;
	if (ch < cw) ch = cw;
	if (ch > cw * 4) ch = cw * 4;
	return ch;
}

int xHeatView::rowsFit() const {
	int rows = (height() - 2 * HEAT_PAD) / cellH();
	if (rows < 1) rows = 1;
	int total = rowsTotal();
	return (rows > total) ? total : rows;
}

// counters of one cell, by its offset inside the current source
xHeatCell xHeatView::cellData(int off) const {
	Computer* comp = conf.prof.cur->zx;
	if (mode == XVIEW_CPU) return comp_heat_cpu(comp, off);
	return comp_heat_phys(comp, (mode == XVIEW_ROM) ? MEM_ROM : MEM_RAM, (page << 14) | (off & 0x3fff));
}

// where one raster row's bytes live, as a bank and an offset into it. a row
// never straddles two memory pages - mem->map is indexed by adr >> pgshift and
// pgshift is never below 8 - so one page lookup serves all 256 of them, which
// is what keeps the whole-source scan below off the page-lookup path
void xHeatView::rowSource(int row, int* type, int* base) const {
	Computer* comp = conf.prof.cur->zx;
	int off = row * HEAT_BPR;
	if (mode == XVIEW_CPU) {
		xAdr xadr = mem_get_xadr(comp->mem, off);
		*type = xadr.type;
		*base = xadr.abs;
	} else {
		*type = (mode == XVIEW_ROM) ? MEM_ROM : MEM_RAM;
		*base = (page << 14) | (off & 0x3fff);
	}
}

// the divisor a channel's counts are scaled by, worked out once per raster
static double heat_denom(unsigned int max, bool logscale) {
	if (max == 0) return 0.0;
	return logscale ? log(1.0 + max) : (double)max;
}

// bucket a count into 0..levels against that channel's own maximum. 0 is
// untouched, and a single hit already lands on step 1 rather than sharing a
// bucket with it - between a byte read twice and one read a million times there
// are six decades, and a smooth ramp puts all but the hottest loop in the dark
static int heat_level(unsigned int val, double denom, int levels, bool logscale) {
	if ((val == 0) || (denom <= 0.0)) return 0;
	double f = (logscale ? log(1.0 + val) : (double)val) / denom;
	int lvl = (int)ceil(f * levels);
	if (lvl < 1) lvl = 1;
	if (lvl > levels) lvl = levels;
	return lvl;
}

// subtractive blend, each amount 0..1. a counter takes away as much of each
// component as its own colour is missing, so at full intensity alone it lands
// exactly on that colour, and mixtures darken the way overlaid inks do
static int heat_sub(double a, double w, double e, int cr, int cw, int ce) {
	double sum = a * (255 - cr) + w * (255 - cw) + e * (255 - ce);
	if (sum > 255.0) sum = 255.0;
	return 255 - (int)(sum + 0.5);
}

static QRgb heat_blend(double r, double w, double e) {
	return qRgb(heat_sub(r, w, e, qRed(HEAT_COL_RD), qRed(HEAT_COL_WR), qRed(HEAT_COL_EX)),
		heat_sub(r, w, e, qGreen(HEAT_COL_RD), qGreen(HEAT_COL_WR), qGreen(HEAT_COL_EX)),
		heat_sub(r, w, e, qBlue(HEAT_COL_RD), qBlue(HEAT_COL_WR), qBlue(HEAT_COL_EX)));
}

QImage xHeatView::raster(int rows) const {
	Computer* comp = conf.prof.cur->zx;
	QImage img(HEAT_BPR, rows, QImage::Format_RGB32);
	// scale against the whole source, not the visible part, so scrolling does
	// not repaint the same cells in different colours
	unsigned int mrd = 0, mwr = 0, mex = 0;
	int total = rowsTotal();
	int type, base, x, y;
	xHeatCell cell;
	for (y = 0; y < total; y++) {
		rowSource(y, &type, &base);
		for (x = 0; x < HEAT_BPR; x++) {
			cell = comp_heat_phys(comp, type, base + x);
			if (cell.rd > mrd) mrd = cell.rd;
			if (cell.wr > mwr) mwr = cell.wr;
			if (cell.ex > mex) mex = cell.ex;
		}
	}
	double drd = heat_denom(mrd, logscale);
	double dwr = heat_denom(mwr, logscale);
	double dex = heat_denom(mex, logscale);
	double ar, aw, ae;
	QRgb* line;
	for (y = 0; y < rows; y++) {
		line = (QRgb*)img.scanLine(y);
		rowSource(top + y, &type, &base);
		for (x = 0; x < HEAT_BPR; x++) {
			cell = comp_heat_phys(comp, type, base + x);
			if (!cell.valid) {
				line[x] = HEAT_BG;		// not ram/rom: nothing is counted here
				continue;
			}
			if (!cell.rd && !cell.wr && !cell.ex) {
				line[x] = HEAT_EMPTY;		// in the bank, never touched
				continue;
			}
			// a channel the view is not showing contributes nothing, which
			// leaves the single-channel views in that channel's own colour
			ar = heat_shown(chan, HEATCH_RD) ? (heat_level(cell.rd, drd, levels, logscale) / (double)levels) : 0.0;
			aw = heat_shown(chan, HEATCH_WR) ? (heat_level(cell.wr, dwr, levels, logscale) / (double)levels) : 0.0;
			ae = heat_shown(chan, HEATCH_EX) ? (heat_level(cell.ex, dex, levels, logscale) / (double)levels) : 0.0;
			line[x] = heat_blend(ar, aw, ae);
		}
	}
	return img;
}

// the raster keeps its own near-white ground whatever the interface style is,
// so it gets a frame to say where it ends: a 16K page at one pixel per byte is
// a narrow strip in a wide panel
void xHeatView::paintEvent(QPaintEvent*) {
	QStyleOption opt;
	opt.initFrom(this);
	QPainter pnt(this);
	// a custom widget gets the style sheet background only by asking for it
	style()->drawPrimitive(QStyle::PE_Widget, &opt, &pnt, this);
	int cw = cellW();
	int ch = cellH();
	int rows = rowsFit();
	QImage img = raster(rows);
	int wid = HEAT_BPR * cw;
	QRect dst((width() - wid) / 2, HEAT_PAD, wid, rows * ch);
	pnt.setPen(palette().color(QPalette::Mid));
	pnt.drawRect(dst.adjusted(-1, -1, 0, 0));
	pnt.drawImage(dst, img);
	// once a tile is big enough to have an inside, separate the tiles the way
	// the export tool does: a light line along the top and left of each
	if ((cw >= HEAT_GRIDMIN) && (ch >= HEAT_GRIDMIN)) {
		pnt.setPen(HEAT_COL_GRID);
		for (int x = dst.left(); x < dst.right(); x += cw)
			pnt.drawLine(x, dst.top(), x, dst.bottom());
		for (int y = dst.top(); y < dst.bottom(); y += ch)
			pnt.drawLine(dst.left(), y, dst.right(), y);
	}
	// the 16K window boundaries, the same marks the memory map panel draws
	if (mode == XVIEW_CPU) {
		pnt.setPen(HEAT_COL_BOUND);
		int step = MEM_16K / HEAT_BPR;
		for (int r = top + step - (top % step); r < top + rows; r += step) {
			int y = HEAT_PAD + (r - top) * ch - 1;
			pnt.drawLine(dst.left(), y, dst.right(), y);
		}
	}
	if (!conf.prof.cur->zx->flgHEAT) {
		pnt.setPen(HEAT_COL_HINT);
		pnt.drawText(dst, Qt::AlignCenter, "collecting is off");
	}
}

void xHeatView::resizeEvent(QResizeEvent*) {
	setTop(top);		// a taller panel may have scrolled past the end
	emit s_geom();
}

// offset inside the current source under the cursor, -1 when the point is
// outside the raster
int xHeatView::cellAt(const QPoint& pos) const {
	int cw = cellW();
	int left = (width() - HEAT_BPR * cw) / 2;
	if ((pos.x() < left) || (pos.y() < HEAT_PAD)) return -1;
	int x = (pos.x() - left) / cw;
	int y = (pos.y() - HEAT_PAD) / cellH();
	if ((x < 0) || (x >= HEAT_BPR)) return -1;
	if ((y < 0) || (y >= rowsFit())) return -1;
	return (top + y) * HEAT_BPR + x;
}

void xHeatView::mouseMoveEvent(QMouseEvent* ev) {
	static const xHeatCell nocell = {0, 0, 0, 0};
	int off = cellAt(ev->pos());
	if (off < 0) {
		emit s_cell(QString(), nocell);
		return;
	}
	Computer* comp = conf.prof.cur->zx;
	QString str;
	if (mode == XVIEW_CPU) {
		xAdr xadr = mem_get_xadr(comp->mem, off);
		const char* tp = "---";
		switch (xadr.type) {
			case MEM_RAM: tp = "RAM"; break;
			case MEM_ROM: tp = "ROM"; break;
			case MEM_SLOT: tp = "SLT"; break;
			case MEM_IO: tp = "IO"; break;
		}
		str = QString("%0  %1 %2:%3").arg(gethexword(off)).arg(tp)
			.arg(gethexbyte((xadr.bank >> 6) & 0xff)).arg(gethexword(xadr.abs & 0x3fff));
	} else {
		str = QString("%0 %1:%2").arg((mode == XVIEW_ROM) ? "ROM" : "RAM")
			.arg(gethexbyte(page)).arg(gethexword(off & 0x3fff));
	}
	emit s_cell(str, cellData(off));
}

void xHeatView::leaveEvent(QEvent*) {
	static const xHeatCell nocell = {0, 0, 0, 0};
	emit s_cell(QString(), nocell);
}

// jump the disassembler there, when the cell is one the cpu can see at all
void xHeatView::mouseDoubleClickEvent(QMouseEvent* ev) {
	int off = cellAt(ev->pos());
	if (off < 0) return;
	Computer* comp = conf.prof.cur->zx;
	if (mode == XVIEW_CPU) {
		emit s_adr(off);
	} else {
		int adr = memFindAdr(comp->mem, (mode == XVIEW_ROM) ? MEM_ROM : MEM_RAM, (page << 14) | (off & 0x3fff));
		if (adr >= 0) emit s_adr(adr);
	}
}

void xHeatView::wheelEvent(QWheelEvent* ev) {
	int delta = ev->angleDelta().y();
	if (delta == 0) return;
	emit s_scroll((delta > 0) ? -4 : 4);
	ev->accept();
}

// LEGEND

xHeatLegend::xHeatLegend(QWidget* par):QWidget(par) {
	chan = HEATCH_RGB;
	cell.rd = cell.wr = cell.ex = 0;
	cell.valid = 0;
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
}

void xHeatLegend::setChannel(int c) {
	chan = c;
	updateGeometry();
	update();
}

void xHeatLegend::setCounts(xHeatCell c) {
	// every mouse move outside the raster repeats the same empty reading
	if ((c.valid == cell.valid) && (c.rd == cell.rd) && (c.wr == cell.wr) && (c.ex == cell.ex)) return;
	cell = c;
	update();
}

// the swatches, in the colour a cell carrying that counter alone comes out as -
// which is the counter's own colour, since heat_blend at full intensity lands
// exactly there. "none" names the flat grey of a cell nothing ever touched
QList<xHeatLegend::xHeatLegItem> xHeatLegend::build() const {
	QList<xHeatLegItem> res;
	if (heat_shown(chan, HEATCH_RD)) res << xHeatLegItem{HEAT_COL_RD, "read", &cell.rd};
	if (heat_shown(chan, HEATCH_WR)) res << xHeatLegItem{HEAT_COL_WR, "write", &cell.wr};
	if (heat_shown(chan, HEATCH_EX)) res << xHeatLegItem{HEAT_COL_EX, "exec", &cell.ex};
	res << xHeatLegItem{HEAT_EMPTY, "none", NULL};
	return res;
}

// every count is drawn in one field this wide, so the labels keep their places
// whatever the numbers do
int xHeatLegend::numWidth() const {
	return fontMetrics().horizontalAdvance("999999");
}

// counts above six digits lose their tail rather than the field its width
static QString heat_count(unsigned int v) {
	if (v < 1000000) return QString::number(v);
	if (v < 1000000000) return QString("%0M").arg(v / 1000000.0, 0, 'f', 1);
	return QString("%0M").arg(v / 1000000);
}

QSize xHeatLegend::sizeHint() const {
	QFontMetrics fm = fontMetrics();
	QList<xHeatLegItem> items = build();
	int w = -HEAT_LEGGAP;			// no gap after the last entry
	foreach (const xHeatLegItem& it, items) {
		w += HEAT_SWATCH + 3 + fm.horizontalAdvance(it.val ? (it.lab + ":") : it.lab) + HEAT_LEGGAP;
		if (it.val) w += 3 + numWidth();
	}
	return QSize(w, qMax(HEAT_SWATCH, fm.height()));
}

QSize xHeatLegend::minimumSizeHint() const {
	return sizeHint();
}

void xHeatLegend::paintEvent(QPaintEvent*) {
	QPainter pnt(this);
	QFontMetrics fm = fontMetrics();
	int nw = numWidth();
	int y = (height() - HEAT_SWATCH) / 2;
	int x = 0;
	foreach (const xHeatLegItem& it, build()) {
		QString lab = it.val ? (it.lab + ":") : it.lab;
		int tw = fm.horizontalAdvance(lab);
		pnt.fillRect(x, y, HEAT_SWATCH, HEAT_SWATCH, QColor(it.col));
		pnt.setPen(palette().color(QPalette::Mid));
		pnt.drawRect(x, y, HEAT_SWATCH - 1, HEAT_SWATCH - 1);
		pnt.setPen(palette().color(QPalette::WindowText));
		pnt.drawText(QRect(x + HEAT_SWATCH + 3, 0, tw, height()), Qt::AlignLeft | Qt::AlignVCenter, lab);
		x += HEAT_SWATCH + 3 + tw;
		if (it.val) {
			// left, so the count sits against its own label rather than against
			// the next one; the field keeps its width either way
			pnt.drawText(QRect(x + 3, 0, nw, height()), Qt::AlignLeft | Qt::AlignVCenter,
				cell.valid ? heat_count(*it.val) : QString("-"));
			x += 3 + nw;
		}
		x += HEAT_LEGGAP;
	}
}

// WIDGET

xHeatWidget::xHeatWidget(QString i, QString t, QWidget* p):xDockWidget(i,t,p) {
	QWidget* wid = new QWidget;
	setWidget(wid);
	ui.setupUi(wid);
	setObjectName("HEATWIDGET");

	view = new xHeatView;
	ui.layHeatView->insertWidget(0, view);		// before the scrollbar
	legend = new xHeatLegend;
	ui.layHeatFoot->addWidget(legend);
	// the legend keeps its own width whatever is beside it, so the labels never
	// shift; the address gives way and clips instead
	ui.labHeatInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

	ui.cbHeatView->addItem("CPU", XVIEW_CPU);
	ui.cbHeatView->addItem("RAM", XVIEW_RAM);
	ui.cbHeatView->addItem("ROM", XVIEW_ROM);
	ui.cbHeatChan->addItem("rgb", HEATCH_RGB);
	ui.cbHeatChan->addItem("read", HEATCH_RD);
	ui.cbHeatChan->addItem("write", HEATCH_WR);
	ui.cbHeatChan->addItem("exec", HEATCH_EX);

	connect(ui.cbHeatView, SIGNAL(currentIndexChanged(int)), this, SLOT(src_changed()));
	connect(ui.sbHeatPage, SIGNAL(valueChanged(int)), this, SLOT(src_changed()));
	connect(ui.cbHeatChan, SIGNAL(currentIndexChanged(int)), this, SLOT(opts_changed()));
	connect(ui.sbHeatLevels, SIGNAL(valueChanged(int)), this, SLOT(opts_changed()));
	connect(ui.cbHeatLog, &QCheckBox::toggled, this, &xHeatWidget::opts_changed);
	connect(ui.cbHeatOn, &QCheckBox::toggled, this, &xHeatWidget::collect_toggle);
	connect(ui.tbHeatReset, &QToolButton::clicked, this, &xHeatWidget::counters_reset);
	connect(ui.tbHeatExport, &QToolButton::clicked, this, &xHeatWidget::counters_export);
	connect(ui.heatScroll, &QScrollBar::valueChanged, view, &xHeatView::setTop);
	connect(view, &xHeatView::s_geom, this, &xHeatWidget::sync_scroll);
	connect(view, &xHeatView::s_scroll, this, &xHeatWidget::scroll_by);
	connect(view, &xHeatView::s_cell, this, &xHeatWidget::show_cell);
	connect(view, &xHeatView::s_adr, this, &xHeatWidget::s_adr);

	// no machine state here: the panel is built long before the debugger is
	// first shown, and the first draw() picks the rest up
	ui.sbHeatPage->setEnabled(false);		// the CPU view it starts on has no page
	opts_changed();
}

// pages the current machine has in the bank being shown
int xHeatWidget::pageMax() const {
	Computer* comp = conf.prof.cur->zx;
	int size = (getRFIData(ui.cbHeatView) == XVIEW_ROM) ? (comp->mem->romMask + 1) : (comp->mem->ramMask + 1);
	int cnt = size / MEM_16K;
	return (cnt > 0) ? (cnt - 1) : 0;
}

void xHeatWidget::src_changed() {
	int mode = getRFIData(ui.cbHeatView);
	ui.sbHeatPage->setEnabled(mode != XVIEW_CPU);
	ui.sbHeatPage->setMaximum(pageMax());
	view->setSource(mode, ui.sbHeatPage->value());
	sync_scroll();
}

void xHeatWidget::opts_changed() {
	int chan = getRFIData(ui.cbHeatChan);
	view->setChannel(chan);
	view->setLevels(ui.sbHeatLevels->value());
	view->setLogScale(ui.cbHeatLog->isChecked());
	legend->setChannel(chan);
}

void xHeatWidget::collect_toggle(bool on) {
	Computer* comp = conf.prof.cur->zx;
	comp->flgHEAT = on ? 1 : 0;
	if (on) comp_heat_sync(comp);		// banks may not match the current hardware yet
	view->update();
}

void xHeatWidget::counters_reset() {
	if (!areSure("Reset memory heat-map counters?")) return;
	comp_heat_reset(conf.prof.cur->zx);
	view->update();
}

void xHeatWidget::counters_export() {
	QString path = QFileDialog::getSaveFileName(this, "Export memory heat-map", QString(), "Heat-map CSV (*.csv)", nullptr, QFileDialog::DontUseNativeDialog);
	if (path.isEmpty()) return;
	if (!path.endsWith(".csv", Qt::CaseInsensitive))
		path.append(".csv");
	if (comp_heat_save(conf.prof.cur->zx, path.toLocal8Bit().data()) != 0)
		shitHappens("Can't write heat-map file");
}

void xHeatWidget::scroll_by(int rows) {
	ui.heatScroll->setValue(ui.heatScroll->value() + rows);
}

void xHeatWidget::show_cell(QString adr, xHeatCell cell) {
	ui.labHeatInfo->setText(adr);
	legend->setCounts(cell);
}

// the raster shows as many rows as its height fits, so the range has to be
// worked out again after every resize and every source change
void xHeatWidget::sync_scroll() {
	ui.heatScroll->blockSignals(true);
	ui.heatScroll->setMaximum(view->maxTop());
	ui.heatScroll->setPageStep(view->rowsFit());
	ui.heatScroll->setValue(view->getTop());
	ui.heatScroll->blockSignals(false);
	ui.heatScroll->setEnabled(view->maxTop() > 0);
}

void xHeatWidget::draw() {
	Computer* comp = conf.prof.cur->zx;
	ui.cbHeatOn->blockSignals(true);
	ui.cbHeatOn->setChecked(comp->flgHEAT);
	ui.cbHeatOn->blockSignals(false);
	ui.sbHeatPage->setMaximum(pageMax());	// may clamp the page: let that reach the view
	sync_scroll();
	view->update();
}
