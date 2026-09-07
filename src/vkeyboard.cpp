// virtual keyboard

#include "vkeyboard.h"
#include "xcore/xcore.h"

#include <QIcon>
#include <QPainter>
#include <QMenu>

typedef struct {
	int x;
	int y;
	int dx;
	int dy;
} xRect;

typedef struct {
	unsigned char ch;
	xRect rect;
	xRect rect2;
} xVKeyMap;

static const xVKeyMap vkZxMap[] = {
	{'1',{2,10,46,60},{0,0,0,0}},{'2',{2 + 49,10,46,60},{0,0,0,0}},{'3',{2 + 49*2,10,46,60},{0,0,0,0}},{'4',{2 + 49*3,10,46,60},{0,0,0,0}},{'5',{2 + 49*4,10,46,60},{0,0,0,0}},
	{'6',{2 + 49*5,10,46,60},{0,0,0,0}},{'7',{2 + 49*6,10,46,60},{0,0,0,0}},{'8',{2 + 49*7,10,46,60},{0,0,0,0}},{'9',{2 + 49*8,10,46,60},{0,0,0,0}},{'0',{2 + 49*9,10,46,60},{0,0,0,0}},
	{'q',{26,70,46,60},{0,0,0,0}},{'w',{26 + 49,70,46,60},{0,0,0,0}},{'e',{26 + 49*2,70,46,60},{0,0,0,0}},{'r',{26 + 49*3,70,46,60},{0,0,0,0}},{'t',{26 + 49*4,70,46,60},{0,0,0,0}},
	{'y',{26 + 49*5,70,46,60},{0,0,0,0}},{'u',{26 + 49*6,70,46,60},{0,0,0,0}},{'i',{26 + 49*7,70,46,60},{0,0,0,0}},{'o',{26 + 49*8,70,46,60},{0,0,0,0}},{'p',{26 + 49*9,70,46,60},{0,0,0,0}},
	{'a',{38,130,46,60},{0,0,0,0}},{'s',{38 + 49,130,46,60},{0,0,0,0}},{'d',{38 + 49*2,130,46,60},{0,0,0,0}},{'f',{38 + 49*3,130,46,60},{0,0,0,0}},{'g',{38 + 49*4,130,46,60},{0,0,0,0}},
	{'h',{38 + 49*5,130,46,60},{0,0,0,0}},{'j',{38 + 49*6,130,46,60},{0,0,0,0}},{'k',{38 + 49*7,130,46,60},{0,0,0,0}},{'l',{38 + 49*8,130,46,60},{0,0,0,0}},{'E',{38 + 49*9,130,46,60},{0,0,0,0}},
	{'C',{2,190,46+12,60},{0,0,0,0}},{'z',{14 + 49,190,46,60},{0,0,0,0}},{'x',{14 + 49*2,190,46,60},{0,0,0,0}},{'c',{14 + 49*3,190,46,60},{0,0,0,0}},{'v',{14 + 49*4,190,46,60},{0,0,0,0}},
	{'b',{14 + 49*5,190,46,60},{0,0,0,0}},{'n',{14 + 49*6,190,46,60},{0,0,0,0}},{'m',{14 + 49*7,190,46,60},{0,0,0,0}},{'S',{14 + 49*8,190,46,60},{0,0,0,0}},{' ',{14 + 49*9,190,46+24,60},{0,0,0,0}},
	{0,{0,0,0,0},{0,0,0,0}}
};

keyWindow::keyWindow(QWidget* p):QDialog(p) {
	kb = NULL;
	dock = 0;
	memset(&xent, 0, sizeof(xent));
	xent.key = ENDKEY;
	pxm = QPixmap(":/images/keymap_volutar.png");
	setModal(false);
	setWindowModality(Qt::NonModal);
	setSizeGripEnabled(true);
	// without a cursor of its own the window keeps the sizing arrows the frame
	// drag left behind
	setCursor(Qt::ArrowCursor);
	setMinimumSize(pxm.width() / 2, pxm.height() / 2);
	setZoom(storedZoom());
	setWindowIcon(QIcon(":/images/keyboard.png"));
	setWindowTitle("ZX Keyboard");
	if (conf.keywin.dock)
		setDock(true);
}

// window pixels per picture pixel. The picture keeps its shape, so a window of
// another aspect gets an empty strip beside it
double keyWindow::scale() {
	double sx = width() / (double)pxm.width();
	double sy = height() / (double)pxm.height();
	return (sx < sy) ? sx : sy;
}

// the size the window was left at last time
double keyWindow::storedZoom() {
	return (conf.keywin.width > 0) ? (conf.keywin.width / (double)pxm.width()) : 1.0;
}

QPoint keyWindow::imgPos(QPoint pos) {
	double sc = scale();
	if (sc <= 0.0) return QPoint(-1, -1);
	int x = (pos.x() - (width() - pxm.width() * sc) / 2) / sc;
	int y = (pos.y() - (height() - pxm.height() * sc) / 2) / sc;
	return QPoint(x, y);
}

void keyWindow::setZoom(double z) {
	resize(qRound(pxm.width() * z), qRound(pxm.height() * z));
}

// docked: no frame of its own, the emulator window drags it around
void keyWindow::setDock(bool d) {
	bool vis = isVisible();
	dock = d;
	conf.keywin.dock = dock;
	setWindowFlags(d ? (Qt::Tool | Qt::FramelessWindowHint) : Qt::Dialog);
	setSizeGripEnabled(!d);
	if (d) {
		snap();
	} else {
		setZoom(storedZoom());
	}
	if (vis) show();		// changing the flags takes the window off screen
}

void keyWindow::snap() {
	if (!dock) return;
	QWidget* par = parentWidget();
	if (!par) return;
	QRect rc = par->frameGeometry();
	int wid = rc.width();
	int hig = qRound(wid * pxm.height() / (double)pxm.width());
	int y = rc.bottom() + 1;
	// fullscreen, or the window sits too low: lay the keyboard over the picture
	if (par->isFullScreen() || (y + hig > SCREENSIZE.height()))
		y = rc.bottom() - hig + 1;
	setGeometry(rc.left(), y, wid, hig);
}

void keyWindow::showMenu(QPoint gpos) {
	QMenu mnu(this);
	QAction* act;
	const double zval[] = {1.0, 1.5, 2.0, 3.0};
	for (int i = 0; i < 4; i++) {
		act = mnu.addAction(QString("Zoom x%1").arg(zval[i]));
		act->setData(zval[i]);
		act->setDisabled(dock);
	}
	mnu.addSeparator();
	QAction* dck = mnu.addAction("Dock under the emulator");
	dck->setCheckable(true);
	dck->setChecked(dock);
	mnu.addSeparator();
	QAction* rall = mnu.addAction("Release all keys");
	act = mnu.exec(gpos);
	if (act == NULL) return;
	if (act == dck) {
		setDock(!dock);
	} else if (act == rall) {
		if (kb) kbdReleaseAll(kb);
		xent.zxKey[0] = 0;
		update();
	} else if (act->data().isValid()) {
		setZoom(act->data().toDouble());
	}
}

void keyWindow::resizeEvent(QResizeEvent* ev) {
	if (!dock)
		conf.keywin.width = ev->size().width();
	QDialog::resizeEvent(ev);
}

void keyWindow::showEvent(QShowEvent* ev) {
	QDialog::showEvent(ev);
	snap();
}

void keyWindow::switcher() {
	if (isVisible())
		hide();
	else
		show();
}

void keyWindow::upd(Keyboard* k) {
	kb = k;
	if (isVisible())
		repaint();
}

void keyWindow::rall(Keyboard* k) {
	if (!isVisible()) {
		kbdReleaseAll(k);
	}
}

// TODO: untide from ZX-keyboard (row,pos calculation)
void keyWindow::paintEvent(QPaintEvent*) {
	QPainter pnt;
	const xRect* prct;
	unsigned char val;
	int row, pos;
	double sc = scale();
	pnt.begin(this);
	pnt.fillRect(rect(), palette().window());
	// everything below is in the picture's own pixels, the painter sizes it
	pnt.translate((width() - pxm.width() * sc) / 2, (height() - pxm.height() * sc) / 2);
	pnt.scale(sc, sc);
	pnt.setRenderHint(QPainter::SmoothPixmapTransform, true);
	if (kb) {
		for(int i = 0; i < 8; i++) {
			pos = (i & 4) ? 0 : 9;
			row = (i & 4) ? (i & 3) : (~i & 3);
			val = ~kb->map[i] & 0x1f;
			while(val) {
				if (val & 1) {
					prct = &vkZxMap[row * 10 + pos].rect;
					pnt.fillRect(prct->x, prct->y, prct->dx, prct->dy, qRgb(0,200,255));
				}
				val >>= 1;
				pos += (i & 4) ? 1 : -1;
			}
		}
	}
	pnt.drawPixmap(0, 0, pxm);
	pnt.end();
}

void keyWindow::mousePressEvent(QMouseEvent* ev) {
	if (!kb) return;

	const xRect* prct = NULL;
	const xRect* rctp;
	int idx = 0;
	QPoint ipos = imgPos(QPoint(ev->xEventX, ev->xEventY));
	int x = ipos.x();
	int y = ipos.y();
	int dx,dy;
	while(vkZxMap[idx].ch != 0) {
		rctp = &vkZxMap[idx].rect;
		dx = x - rctp->x;
		dy = y - rctp->y;
		if ((dx >= 0) && (dy >= 0) && (dx < rctp->dx) && (dy < rctp->dy)) {
			prct = rctp;
			xent.zxKey[0] = vkZxMap[idx].ch;
		} else {
			rctp = &vkZxMap[idx].rect2;
			dx = x - rctp->x;
			dy = y - rctp->y;
			if ((dx >= 0) && (dy >= 0) && (dx < rctp->dx) && (dy < rctp->dy)) {
				prct = rctp;
				xent.zxKey[0] = vkZxMap[idx].ch;
			}
		}
		idx++;
	}
	if (prct == NULL) {			// no hit: nothing to release on mouse up either
		xent.zxKey[0] = 0;
		// the right button holds a key down, so the menu is on a miss
		if (ev->button() == Qt::RightButton)
			showMenu(mapToGlobal(QPoint(ev->xEventX, ev->xEventY)));
		return;
	}
	xent.zxKey[1] = 0;
	switch(ev->button()) {
		case Qt::LeftButton:
			kbd_press(kb, &xent);
			update();
			break;
		case Qt::RightButton:
			kbdTrigger(kb, &xent);
			update();
			break;
		case Qt::MiddleButton:
			kbdReleaseAll(kb);
			xent.zxKey[0] = 0;
			update();
			break;
		default:
			break;
	}
}

void keyWindow::mouseReleaseEvent(QMouseEvent* ev) {
	if (!kb) return;
	if (ev->button() == Qt::LeftButton) {
		kbd_release(kb, &xent);
	}
	update();
}

void keyWindow::keyPressEvent(QKeyEvent* ev) {
	if (ev->key() == Qt::Key_Escape) {
		hide();
	} else {
		emit s_key_press(ev);
	}
}

void keyWindow::keyReleaseEvent(QKeyEvent* ev) {
	if (ev->key() != Qt::Key_Escape)
		emit s_key_release(ev);
}
