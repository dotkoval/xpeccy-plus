#pragma once

#include <QTableView>
#include <QDockWidget>
#include <QResizeEvent>
#include <QHeaderView>

#include "../classes.h"

class xDiskDumpModel : public xTableModel {
	public:
		xDiskDumpModel(QObject* = NULL);
		QVariant data(const QModelIndex&, int) const;
		void setDrive(int);
		void setTrack(int);
		void recount();			// rows from the track length and rowBytes
		int rowBytes;			// bytes on one line, a multiple of DUMP_GROUP
	private:
		int drv;
		int trk;
};

class xDiskDump : public QTableView {
	Q_OBJECT
	public:
		xDiskDump(QWidget* = NULL);
		void update();
		void setRowBytes(int);		// 0 = fit as many groups as there is room for
	public slots:
		void setTrack(int);
		void setDrive(int);
		void toTarget();
	private:
		int drv;
		int rowBytes;			// what setRowBytes was given
		void layoutColumns();
		xDiskDumpModel* mod;
		void resizeEvent(QResizeEvent*);
};

#include "ui_form_fdddump.h"

class xDiskDumpWidget : public xDockWidget {
	Q_OBJECT
	public:
		xDiskDumpWidget(QString, QString, QWidget* = nullptr);
	public slots:
		void draw();
		void setDrive(int);
	private:
		Ui::FDDDump ui;
	private slots:
		void toTarget();
		void bytes_changed();
};
