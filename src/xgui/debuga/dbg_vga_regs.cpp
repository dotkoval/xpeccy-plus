#include "dbg_widgets.h"
#include "../../libxpeccy/video/vga.h"

xVgaRegModel::xVgaRegModel(QObject* p):xTableModel(p) {}

int xVgaRegModel::rowCount(const QModelIndex&) const {
	return 0x19;
}

int xVgaRegModel::columnCount(const QModelIndex&) const {
	return 4;
}

static const char* vga_reg_hnames[4] = {"CRT","SEQ","GRF","ATR"};

QVariant xVgaRegModel::headerData(int sec, Qt::Orientation ori, int role) const {
	QVariant res;
	if (role == Qt::DisplayRole) {
		if (ori == Qt::Horizontal) {
			res = vga_reg_hnames[sec];
		} else if (ori == Qt::Vertical) {
			res = QString("R#%0").arg(gethexbyte(sec));
		}
	}
	return res;
}

QVariant xVgaRegModel::data(const QModelIndex& idx, int role) const {
	int row = idx.row();
	int col = idx.column();
	int n;
	QVariant res;
	switch(role) {
		case Qt::DisplayRole:
			switch(col) {
				case 0:
					if (row <= VGA_CRB)
						res = gethexbyte(conf.zx->CRT_REG(row));
					break;
				case 1:
					if (row <= VGA_SRC)
						res = gethexbyte(conf.zx->SEQ_REG(row));
					break;
				case 2:
					if (row <= VGA_GRC)
						res = gethexbyte(conf.zx->GRF_REG(row));
					break;
				case 3:
					if (row <= VGA_ATC)
						res = gethexbyte(conf.zx->ATR_REG(row));
					break;
			}
			break;
		case Qt::ForegroundRole:
			n = -1;
			switch (col) {
				case 0: n = conf.zx->CRT_IDX; break;
				case 1: n = conf.zx->SEQ_IDX; break;
				case 2: n = conf.zx->GRF_IDX; break;
				case 3: n = conf.zx->ATR_IDX; break;
			}
			if (row == n) {
				res = QColor(0xe0, 0x20, 0x20);
			}
			break;
	}
	return res;
}

// widget

xVgaWidget::xVgaWidget(QString i, QString t, QWidget* p):xDockWidget(i,t,p) {
//	setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
	QWidget* wid = new QWidget;
	setWidget(wid);
	ui.setupUi(wid);
	setObjectName("VGAWIDGET");
	ui.table->setModel(new xVgaRegModel());
//	connect(this, &QDockWidget::visibilityChanged, this, &xVgaWidget::draw);
	hwList << HWG_PC;
}

void xVgaWidget::draw() {
	((xTableModel*)(ui.table->model()))->update();
}
