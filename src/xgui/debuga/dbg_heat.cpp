#include "dbg_heat.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOption>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFileDialog>

#define	HEAT_PAD	2	// margin around the raster
#define	HEAT_CELLMAX	8	// biggest square one byte is drawn as
#define	HEAT_GRIDMIN	3	// tile size the separator grid starts being drawn at
#define	HEAT_SWATCH	9	// legend swatch, in pixels
#define	HEAT_LEGGAP	8	// between two legend entries

// one set of counter colours for every theme - they read on a light ground and
// on a dark one alike. only the grey of an untouched cell has to follow the
// theme, and the yellow between the 16K slots is the memory map panel's own
#define	HEAT_COL_RD	qRgb(46, 207, 46)
#define	HEAT_COL_WR	qRgb(255, 57, 57)
#define	HEAT_COL_EX	qRgb(79, 150, 237)
#define	HEAT_COL_SEP	QColor(224, 224, 0)
#define	HEAT_NONE_LT	qRgb(214, 214, 214)	// untouched, on a light ground
#define	HEAT_NONE_DK	qRgb(72, 72, 72)	// and on a dark one
#define	HEAT_COL_BOUND	QColor(0x60, 0x60, 0x60)	// 16K window boundaries
#define	HEAT_COL_HINT	QColor(0x80, 0x80, 0x80)	// "collecting is off"

#define	HEAT_BLKGAP	1	// separator between two blocks, in pixels
#define	HEAT_BLKMAX	8	// biggest square one group is drawn as
#define	HEAT_HDRPAD	2	// above and below the address heading of a block

// the grey an untouched cell is drawn in, by how light the ground under the
// panel is. the style sheet reaches the palette, so that is where it comes
// from - xStackView reads its own ground the same way
static QRgb heat_none_for(const QColor& gnd) {
	return (gnd.lightness() < 128) ? HEAT_NONE_DK : HEAT_NONE_LT;
}

// counters of one group of bytes, summed. the page is already resolved, so the
// whole group is one bank lookup - a group never straddles a page
static xHeatCell heat_sum(Computer* comp, int type, int base) {
	xHeatCell sum = {0, 0, 0, 0};
	xHeatCell one;
	for (int i = 0; i < HEAT_GROUP; i++) {
		one = comp_heat_phys(comp, type, base + i);
		sum.rd += one.rd;
		sum.wr += one.wr;
		sum.ex += one.ex;
		sum.valid |= one.valid;
	}
	return sum;
}

// which colour one cell gets. counters are summed over whatever the cell
// stands for: exec outweighs everything else, then write against read, and any
// read left over makes it green. no mixing - the strongest one wins outright.
// a cell nothing touched is grey, as is one the single channel on show never
// touched
static QRgb heat_cell_colour(const xHeatCell& c, int chan, QRgb none) {
	if (chan != HEATCH_ALL) {
		unsigned int val = (chan == HEATCH_RD) ? c.rd : (chan == HEATCH_WR) ? c.wr : c.ex;
		if (!val) return none;
		return (chan == HEATCH_RD) ? HEAT_COL_RD : (chan == HEATCH_WR) ? HEAT_COL_WR : HEAT_COL_EX;
	}
	if (!c.rd && !c.wr && !c.ex) return none;
	if (c.ex > c.wr + c.rd) return HEAT_COL_EX;
	if (c.wr > c.rd) return HEAT_COL_WR;
	return HEAT_COL_RD;
}

// HEATCH_ALL shows all three counters; any other setting shows only itself
static bool heat_shown(int chan, int which) {
	return (chan == HEATCH_ALL) || (chan == which);
}

// VIEW

xHeatView::xHeatView(QWidget* par):QWidget(par) {
	mode = XVIEW_CPU;
	page = 0;
	chan = HEATCH_ALL;
	top = 0;
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

// the cpu space is shown as blocks when it really is four 16K slots: a machine
// with a wider bus keeps the plain raster
bool xHeatView::blockView() const {
	return (mode == XVIEW_CPU) && (conf.prof.cur->zx->mem->busmask == 0xffff);
}

QSize xHeatView::minimumSizeHint() const {
	if (blockView())
		return QSize(HEAT_BLOCKS * HEAT_BLKDIM + (HEAT_BLOCKS - 1) * HEAT_BLKGAP + 2 * HEAT_PAD,
			HEAT_BLKDIM + blkHead() + 2 * HEAT_PAD);
	return QSize(HEAT_BPR / 2, 64);
}

void xHeatView::setSource(int md, int pg) {
	mode = md;
	page = pg;
	top = 0;
	updateGeometry();		// the block view asks for a different minimum
	update();
}

void xHeatView::setChannel(int c) {
	chan = c;
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
	if (blockView()) return 0;		// the blocks always fit
	int max = rowsTotal() - rowsFit();
	return (max < 0) ? 0 : max;
}

// a byte is one cell wide, as wide as the panel allows: 256 of them fit in a
// dock 512 px across, and 1 px each below that. taken for a given width, not
// only the current one - see fits()
int xHeatView::cellWFor(int wid) const {
	int cw = (wid - 2 * HEAT_PAD) / HEAT_BPR;
	if (cw < 1) cw = 1;
	if (cw > HEAT_CELLMAX) cw = HEAT_CELLMAX;
	return cw;
}

// a cell may be taller than it is wide, up to 4:1: a 16K page is 64 rows and
// would otherwise sit as a thin strip in an empty panel. never shorter, and
// never taller than the source needs
int xHeatView::cellHFor(int cw) const {
	int total = rowsTotal();
	int ch = total ? ((height() - 2 * HEAT_PAD) / total) : cw;
	if (ch < cw) ch = cw;
	if (ch > cw * 4) ch = cw * 4;
	return ch;
}

int xHeatView::cellW() const {
	return cellWFor(width());
}

int xHeatView::cellH() const {
	return cellHFor(cellW());
}

int xHeatView::rowsFitFor(int ch) const {
	int rows = (height() - 2 * HEAT_PAD) / ch;
	if (rows < 1) rows = 1;
	int total = rowsTotal();
	return (rows > total) ? total : rows;
}

int xHeatView::rowsFit() const {
	return rowsFitFor(cellH());
}

// does the whole source fit with no scrolling, measured as if the view were
// `extra` pixels wider - the width it takes back when the scrollbar goes. the
// answer has to be free of the scrollbar, or hiding one would widen the view,
// flip the answer, and set the layout swinging
bool xHeatView::fits(int extra) const {
	if (blockView()) return true;		// the blocks are laid out to fit
	int ch = cellHFor(cellWFor(width() + extra));
	return ((height() - 2 * HEAT_PAD) / ch) >= rowsTotal();
}

// counters of one cell, by its offset inside the current source. a block cell
// stands for a group of bytes and answers with their sum
xHeatCell xHeatView::cellData(int off) const {
	Computer* comp = conf.prof.cur->zx;
	if (blockView()) {
		xAdr xadr = mem_get_xadr(comp->mem, off);
		return heat_sum(comp, xadr.type, xadr.abs);
	}
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

// one cell per byte, coloured the same way the block view colours a group
QImage xHeatView::raster(int rows, QRgb none) const {
	Computer* comp = conf.prof.cur->zx;
	QImage img(HEAT_BPR, rows, QImage::Format_RGB32);
	int type, base, x, y;
	xHeatCell cell;
	QRgb* line;
	for (y = 0; y < rows; y++) {
		line = (QRgb*)img.scanLine(y);
		rowSource(top + y, &type, &base);
		for (x = 0; x < HEAT_BPR; x++) {
			cell = comp_heat_phys(comp, type, base + x);
			line[x] = heat_cell_colour(cell, chan, none);
		}
	}
	return img;
}

// BLOCK VIEW

// the heading each block carries: the range of cpu addresses it covers
static QString heat_blk_name(int blk) {
	return QString("#%0-#%1").arg(gethexword(blk * MEM_16K)).arg(gethexword((blk + 1) * MEM_16K - 1));
}

// the heading is a strip of the same colour the dock titles use
int xHeatView::blkHead() const {
	return fontMetrics().height() + 2 * HEAT_HDRPAD;
}

// blocks stay square, so the shorter side of the panel sets the cell, and
// everything else follows from it
xHeatView::xBlkGeom xHeatView::blkGeom() const {
	xBlkGeom g;
	g.hh = blkHead();
	int w = (width() - 2 * HEAT_PAD - (HEAT_BLOCKS - 1) * HEAT_BLKGAP) / (HEAT_BLOCKS * HEAT_BLKDIM);
	int h = (height() - 2 * HEAT_PAD - g.hh) / HEAT_BLKDIM;
	g.cs = (w < h) ? w : h;
	if (g.cs < 1) g.cs = 1;
	if (g.cs > HEAT_BLKMAX) g.cs = HEAT_BLKMAX;
	g.span = HEAT_BLKDIM * g.cs + HEAT_BLKGAP;
	int wid = HEAT_BLOCKS * g.span - HEAT_BLKGAP;		// no gap after the last block
	int hei = HEAT_BLKDIM * g.cs;
	// a panel too short to hold the whole thing loses the bottom, not the
	// headings: the cells stay readable without them, the other way round not
	int y = (height() - hei - g.hh) / 2 + g.hh;
	if (y < g.hh + HEAT_PAD) y = g.hh + HEAT_PAD;
	g.all = QRect((width() - wid) / 2, y, wid, hei);
	return g;
}

// cpu address of the group under the point, -1 outside the blocks or in a gap
int xHeatView::blkAt(const QPoint& pos) const {
	xBlkGeom g = blkGeom();
	if (!g.all.contains(pos)) return -1;
	int x = pos.x() - g.all.left();
	int blk = x / g.span;
	int inx = (x % g.span) / g.cs;
	int iny = (pos.y() - g.all.top()) / g.cs;
	if ((blk >= HEAT_BLOCKS) || (inx >= HEAT_BLKDIM) || (iny >= HEAT_BLKDIM)) return -1;
	return (blk * MEM_16K) + (iny * HEAT_BLKDIM + inx) * HEAT_GROUP;
}

void xHeatView::paintBlocks(QPainter& pnt, QRgb none) {
	Computer* comp = conf.prof.cur->zx;
	xBlkGeom g = blkGeom();
	int bw = HEAT_BLKDIM * g.cs;
	// once a cell is big enough to have an inside, leave a gap along its right
	// and bottom: the style ground shows through it and is the grid
	int fill = (g.cs >= HEAT_GRIDMIN) ? (g.cs - 1) : g.cs;
	QColor hbg = conf.pal.value("dbg.header.bg");
	QColor htx = conf.pal.value("dbg.header.txt");
	QFontMetrics fm = fontMetrics();
	int blk, bx, x, y, type, base;
	QString name;
	for (blk = 0; blk < HEAT_BLOCKS; blk++) {
		bx = g.all.left() + blk * g.span;
		// the heading, in the dock titles' own colours. a narrow block gets
		// the start of its range, or nothing at all rather than a clipped word
		QRect head(bx, g.all.top() - g.hh, bw, g.hh);
		pnt.fillRect(head, hbg);
		name = heat_blk_name(blk);
		if (fm.horizontalAdvance(name) > bw)
			name = QString("#%0").arg(gethexword(blk * MEM_16K));
		if (fm.horizontalAdvance(name) <= bw) {
			pnt.setPen(htx);
			pnt.drawText(head, Qt::AlignCenter, name);
		}
		for (y = 0; y < HEAT_BLKDIM; y++) {
			// one row is 256 bytes and never straddles a page, so the map is
			// looked up once for the whole of it
			rowSource(blk * HEAT_BLKDIM + y, &type, &base);
			for (x = 0; x < HEAT_BLKDIM; x++) {
				QRgb col = heat_cell_colour(heat_sum(comp, type, base + x * HEAT_GROUP), chan, none);
				pnt.fillRect(bx + x * g.cs, g.all.top() + y * g.cs, fill, fill, QColor(col));
			}
		}
	}
	// the yellow between the 16K slots, headings included so it reads as one
	// line all the way down
	for (blk = 1; blk < HEAT_BLOCKS; blk++) {
		bx = g.all.left() + blk * g.span;
		pnt.fillRect(bx - HEAT_BLKGAP, g.all.top() - g.hh, HEAT_BLKGAP, g.all.height() + g.hh, HEAT_COL_SEP);
	}
	// the same frame the raster gets, around the headings and the cells alike
	pnt.setPen(palette().color(QPalette::Mid));
	pnt.drawRect(g.all.left() - 1, g.all.top() - g.hh - 1, g.all.width() + 1, g.all.height() + g.hh + 1);
	paintOffHint(pnt, g.all);
}

// nothing is being counted: say so over whichever picture is up
void xHeatView::paintOffHint(QPainter& pnt, const QRect& box) const {
	if (conf.prof.cur->zx->flgHEAT) return;
	pnt.setPen(HEAT_COL_HINT);
	pnt.drawText(box, Qt::AlignCenter, "collecting is off");
}

// the raster gets a frame to say where it ends: a 16K page at one pixel per
// byte is a narrow strip in a wide panel
void xHeatView::paintEvent(QPaintEvent*) {
	QStyleOption opt;
	opt.initFrom(this);
	QPainter pnt(this);
	// a custom widget gets the style sheet background only by asking for it
	style()->drawPrimitive(QStyle::PE_Widget, &opt, &pnt, this);
	// the ground the style paints: the untouched grey and the lines that part
	// one cell from the next both come out of it
	QColor gnd = opt.palette.color(QPalette::Window);
	QRgb none = heat_none_for(gnd);
	if (blockView()) {
		paintBlocks(pnt, none);
		return;
	}
	int cw = cellWFor(width());
	int ch = cellHFor(cw);
	int rows = rowsFitFor(ch);
	QImage img = raster(rows, none);
	int wid = HEAT_BPR * cw;
	QRect dst((width() - wid) / 2, HEAT_PAD, wid, rows * ch);
	pnt.setPen(palette().color(QPalette::Mid));
	pnt.drawRect(dst.adjusted(-1, -1, 0, 0));
	pnt.drawImage(dst, img);
	// once a tile is big enough to have an inside, separate the tiles the way
	// the block view does: a line of the style's own ground along the top and
	// left of each
	if ((cw >= HEAT_GRIDMIN) && (ch >= HEAT_GRIDMIN)) {
		pnt.setPen(gnd);
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
	paintOffHint(pnt, dst);
}

void xHeatView::resizeEvent(QResizeEvent*) {
	setTop(top);		// a taller panel may have scrolled past the end
	emit s_geom();
}

// offset inside the current source under the cursor, -1 when the point is
// outside the raster
int xHeatView::cellAt(const QPoint& pos) const {
	if (blockView()) return blkAt(pos);
	int cw = cellWFor(width());
	int ch = cellHFor(cw);
	int left = (width() - HEAT_BPR * cw) / 2;
	if ((pos.x() < left) || (pos.y() < HEAT_PAD)) return -1;
	int x = (pos.x() - left) / cw;
	int y = (pos.y() - HEAT_PAD) / ch;
	if ((x < 0) || (x >= HEAT_BPR)) return -1;
	if ((y < 0) || (y >= rowsFitFor(ch))) return -1;
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
		// every number in the readout carries the listing's own hash, the way
		// the block headings do. a block cell covers a group of bytes: name
		// the whole of it
		QString adr = blockView() ? QString("#%0-#%1").arg(gethexword(off)).arg(gethexword(off + HEAT_GROUP - 1))
			: QString("#%0").arg(gethexword(off));
		str = QString("%0  %1 #%2:#%3").arg(adr).arg(tp)
			.arg(gethexbyte((xadr.bank >> 6) & 0xff)).arg(gethexword(xadr.abs & 0x3fff));
	} else {
		str = QString("%0 #%1:#%2").arg((mode == XVIEW_ROM) ? "ROM" : "RAM")
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
	if (blockView()) return;		// nothing to scroll
	int delta = ev->angleDelta().y();
	if (delta == 0) return;
	emit s_scroll((delta > 0) ? -4 : 4);
	ev->accept();
}

// LEGEND

xHeatLegend::xHeatLegend(QWidget* par):QWidget(par) {
	chan = HEATCH_ALL;
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

// the swatches, in the colour a cell of that counter comes out as, and "None"
// for the grey of a cell nothing ever touched - the views' own palette, so the
// legend follows the theme with them
QList<xHeatLegend::xHeatLegItem> xHeatLegend::build() const {
	QList<xHeatLegItem> res;
	if (heat_shown(chan, HEATCH_RD)) res << xHeatLegItem{HEAT_COL_RD, "Read", &cell.rd};
	if (heat_shown(chan, HEATCH_WR)) res << xHeatLegItem{HEAT_COL_WR, "Write", &cell.wr};
	if (heat_shown(chan, HEATCH_EX)) res << xHeatLegItem{HEAT_COL_EX, "Exec", &cell.ex};
	res << xHeatLegItem{heat_none_for(palette().color(QPalette::Window)), "None", NULL};
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
	// the address has a line of its own above the legend, both against the
	// right edge: side by side the legend took the room and the address clipped
	ui.layHeatFoot->addWidget(legend, 0, Qt::AlignRight);
	ui.labHeatInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

	ui.cbHeatView->addItem("CPU", XVIEW_CPU);
	ui.cbHeatView->addItem("RAM", XVIEW_RAM);
	ui.cbHeatView->addItem("ROM", XVIEW_ROM);
	ui.cbHeatChan->addItem("All", HEATCH_ALL);
	ui.cbHeatChan->addItem("Read", HEATCH_RD);
	ui.cbHeatChan->addItem("Write", HEATCH_WR);
	ui.cbHeatChan->addItem("Exec", HEATCH_EX);

	connect(ui.cbHeatView, SIGNAL(currentIndexChanged(int)), this, SLOT(src_changed()));
	connect(ui.sbHeatPage, SIGNAL(valueChanged(int)), this, SLOT(src_changed()));
	connect(ui.cbHeatChan, SIGNAL(currentIndexChanged(int)), this, SLOT(opts_changed()));
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
// worked out again after every resize and every source change. the scrollbar
// only takes room when it can actually scroll: a full-height thumb is nothing
// but noise, and the block view never scrolls at all
void xHeatWidget::sync_scroll() {
	int extra = ui.heatScroll->isVisible() ? ui.heatScroll->width() : 0;
	ui.heatScroll->setVisible(!view->fits(extra));
	ui.heatScroll->blockSignals(true);
	ui.heatScroll->setMaximum(view->maxTop());
	ui.heatScroll->setPageStep(view->rowsFit());
	ui.heatScroll->setValue(view->getTop());
	ui.heatScroll->blockSignals(false);
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
