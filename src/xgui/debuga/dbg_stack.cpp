#include "dbg_stack.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOption>

#define	STACK_PAD	3	// margin around the block of lines
#define	STACK_GAP	6	// between the offset column and the word

xStackView::xStackView(QWidget* par):QWidget(par) {
}

int xStackView::rowPitch() const {
	return fontMetrics().height() + 3;
}

int xStackView::rowsFit() const {
	int one = fontMetrics().height();
	int free = height() - 2 * STACK_PAD;
	if (free < one) return 1;		// a squeezed panel still shows the top of SP
	return (free - one) / rowPitch() + 1;
}

// one line, and room for the name column: "0000:" (the +0 row's own address)
// is always wider than an offset label like "+00:", so it sets the width
QSize xStackView::minimumSizeHint() const {
	QFontMetrics fm = fontMetrics();
	return QSize(fm.horizontalAdvance("0000:") + STACK_GAP + fm.horizontalAdvance("0000") + 2 * STACK_PAD,
		fm.height() + 2 * STACK_PAD);
}

QSize xStackView::sizeHint() const {
	return minimumSizeHint();
}

void xStackView::setRows(const QList<xStackRow>& lst) {
	rows = lst;
	update();
}

// a faint tint of the panel background, the way item views alternate their rows.
// Derived rather than taken from the palette: a plain widget gets no
// alternate-background-color from the style sheet, that is an item view thing
static QColor row_tint(const QColor& bg) {
	return (bg.lightness() < 128) ? bg.lighter(130) : bg.darker(107);
}

void xStackView::paintEvent(QPaintEvent*) {
	QStyleOption opt;
	opt.initFrom(this);
	QPainter pnt(this);
	// a custom widget gets the style sheet background only by asking for it
	style()->drawPrimitive(QStyle::PE_Widget, &opt, &pnt, this);
	if (rows.isEmpty()) return;
	QFontMetrics fm = fontMetrics();
	int nw = 0;
	int vw = 0;
	foreach (const xStackRow& row, rows) {
		nw = qMax(nw, fm.horizontalAdvance(row.name));
		vw = qMax(vw, fm.horizontalAdvance(row.value));
	}
	int gap = nw ? STACK_GAP : 0;
	// the block is centered as a whole, so the words keep one left edge
	int x = (width() - nw - gap - vw) / 2;
	if (x < STACK_PAD) x = STACK_PAD;
	int hgt = fm.height();
	int pitch = rowPitch();
	int y = STACK_PAD;
	QColor tint = row_tint(opt.palette.color(QPalette::Window));
	pnt.setPen(opt.palette.color(QPalette::WindowText));
	foreach (const xStackRow& row, rows) {
		if (row.anchor)
			pnt.fillRect(QRect(0, y, width(), hgt), tint);
		if (!row.name.isEmpty())
			pnt.drawText(QRect(x, y, nw, hgt), Qt::AlignRight | Qt::AlignVCenter, row.name);
		pnt.drawText(QRect(x + nw + gap, y, vw, hgt), Qt::AlignLeft | Qt::AlignVCenter, row.value);
		y += pitch;
	}
}
