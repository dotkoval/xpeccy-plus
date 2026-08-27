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

// one line, and room for the widest offset the panel is likely to reach
QSize xStackView::minimumSizeHint() const {
	QFontMetrics fm = fontMetrics();
	return QSize(fm.horizontalAdvance("+00:") + STACK_GAP + fm.horizontalAdvance("0000") + 2 * STACK_PAD,
		fm.height() + 2 * STACK_PAD);
}

QSize xStackView::sizeHint() const {
	return minimumSizeHint();
}

void xStackView::setRows(const QList<xStackRow>& lst) {
	rows = lst;
	update();
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
	pnt.setPen(opt.palette.color(QPalette::WindowText));
	foreach (const xStackRow& row, rows) {
		if (!row.name.isEmpty())
			pnt.drawText(QRect(x, y, nw, hgt), Qt::AlignRight | Qt::AlignVCenter, row.name);
		pnt.drawText(QRect(x + nw + gap, y, vw, hgt), Qt::AlignLeft | Qt::AlignVCenter, row.value);
		y += pitch;
	}
}
