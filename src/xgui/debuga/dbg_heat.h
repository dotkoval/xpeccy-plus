#pragma once

#include "dbg_widgets.h"
#include "ui_form_heat.h"

#include <QImage>

// bytes per raster row: one 16K page is then 64 rows, the 64K cpu space 256
#define HEAT_BPR	256

// which counter the raster shows
enum {
	HEATCH_RGB = 0,		// all three at once: red=write, green=read, blue=exec
	HEATCH_RD,
	HEATCH_WR,
	HEATCH_EX
};

// The heat raster. Painted, not laid out: one byte is a square of 1..8 px,
// sized so that the whole source fits the panel when it can, and scrolled by
// whole rows when it cannot.
class xHeatView : public QWidget {
	Q_OBJECT
	public:
		xHeatView(QWidget* = nullptr);
		void setSource(int mode, int page);
		void setChannel(int);
		void setLevels(int);
		void setLogScale(bool);
		void setTop(int);
		int getTop() const;
		int rowsTotal() const;		// rows the whole source takes
		int rowsFit() const;		// rows the panel shows now
		int maxTop() const;		// last row the panel can start at
		QSize minimumSizeHint() const;
	signals:
		void s_adr(int);		// double click on a cell visible to the cpu
		void s_cell(QString, xHeatCell);	// address and counts under the cursor
		void s_geom();			// size changed: the scrollbar range has to follow
		void s_scroll(int);		// wheel: rows to move by
	protected:
		void paintEvent(QPaintEvent*);
		void resizeEvent(QResizeEvent*);
		void mouseMoveEvent(QMouseEvent*);
		void mouseDoubleClickEvent(QMouseEvent*);
		void leaveEvent(QEvent*);
		void wheelEvent(QWheelEvent*);
	private:
		int mode;			// XVIEW_CPU / XVIEW_RAM / XVIEW_ROM
		int page;			// 16K page for XVIEW_RAM / XVIEW_ROM
		int chan;
		int levels;
		bool logscale;
		int top;			// first row shown
		int cellW() const;
		int cellH() const;
		int cellAt(const QPoint&) const;	// offset inside the source, -1 outside
		xHeatCell cellData(int off) const;
		void rowSource(int row, int* type, int* base) const;
		QImage raster(int rows) const;
};

// The legend under the raster, which doubles as the readout: one swatch per
// counter the view is showing, each with the count of the cell under the
// cursor. Painted, and with no signals of its own, so it needs no moc.
class xHeatLegend : public QWidget {
	public:
		xHeatLegend(QWidget* = nullptr);
		void setChannel(int);
		void setCounts(xHeatCell);
		QSize sizeHint() const;
		QSize minimumSizeHint() const;
	protected:
		void paintEvent(QPaintEvent*);
	private:
		// one swatch: its colour, its name, and the count it stands for -
		// NULL for "none", which names the untouched grey and has no count
		struct xHeatLegItem { QRgb col; QString lab; const unsigned* val; };
		int chan;
		xHeatCell cell;			// what the cursor is over
		QList<xHeatLegItem> build() const;
		int numWidth() const;		// the fixed field every count is drawn in
};

class xHeatWidget : public xDockWidget {
	Q_OBJECT
	public:
		xHeatWidget(QString, QString, QWidget* = nullptr);
	signals:
		void s_adr(int);
	public slots:
		void draw();
	private slots:
		void src_changed();
		void opts_changed();
		void show_cell(QString, xHeatCell);
		void collect_toggle(bool);
		void counters_reset();
		void counters_export();
		void scroll_by(int);
		void sync_scroll();
	private:
		Ui::HeatWidget ui;
		xHeatView* view;
		xHeatLegend* legend;
		int pageMax() const;
};
