#include <QDialog>
#include <QFileInfo>
#include <QPushButton>
#include <QToolButton>
#include <QTableWidget>

#include "favorites.h"
#include "ui_umadial.h"
#include "ui_favorites.h"
#include "filer.h"
#include "xcore/xcore.h"

bool fav_edit(QWidget* parent, int idx, const QString& path) {
	QDialog dia(parent);
	Ui::UmaDial ui;
	ui.setupUi(&dia);
	if (idx < 0) {
		dia.setWindowTitle("Add to Favorites");
		ui.namele->setText(QFileInfo(path).fileName());
		ui.pathle->setText(path);
	} else {
		ui.namele->setText(QString::fromLocal8Bit(conf.bookmarkList[idx].name.c_str()));
		ui.pathle->setText(QString::fromLocal8Bit(conf.bookmarkList[idx].path.c_str()));
	}
	ui.namele->selectAll();
	ui.namele->setFocus();
	QObject::connect(ui.umasptb, &QToolButton::clicked, &dia, [&ui]() {
		int id = FG_ALL;
		int drv = -1;
		QString fpath = file_ask_open(conf.zx, &id, &drv);	// the same dialog as Open
		if (fpath.isEmpty()) return;
		ui.pathle->setText(fpath);
		if (ui.namele->text().isEmpty())
			ui.namele->setText(QFileInfo(fpath).fileName());
	});
	QObject::connect(ui.umaok, &QPushButton::clicked, &dia, [&dia, &ui]() {
		if (!ui.namele->text().isEmpty() && !ui.pathle->text().isEmpty())
			dia.accept();
	});
	QObject::connect(ui.umacn, &QPushButton::clicked, &dia, &QDialog::reject);
	if (dia.exec() != QDialog::Accepted)
		return false;
	std::string name(ui.namele->text().toLocal8Bit().data());
	std::string fpath(ui.pathle->text().toLocal8Bit().data());
	if (idx < 0) {
		addBookmark(name, fpath);
	} else {
		setBookmark(idx, name, fpath);
	}
	return true;
}

static void fav_fill(QTableWidget* tab, int row) {
	tab->setRowCount(conf.bookmarkList.size());
	for (int i = 0; i < conf.bookmarkList.size(); i++) {
		tab->setItem(i, 0, new QTableWidgetItem(QString::fromLocal8Bit(conf.bookmarkList[i].name.c_str())));
		tab->setItem(i, 1, new QTableWidgetItem(QString::fromLocal8Bit(conf.bookmarkList[i].path.c_str())));
	}
	if (row >= tab->rowCount())
		row = tab->rowCount() - 1;
	if (row >= 0)
		tab->selectRow(row);
}

void fav_manage(QWidget* parent) {
	QDialog dia(parent);
	Ui::FavDialog ui;
	ui.setupUi(&dia);
	QTableWidget* tab = ui.favlist;
	tab->setColumnWidth(0, 160);
	fav_fill(tab, 0);
	QObject::connect(ui.tbAdd, &QToolButton::clicked, &dia, [&dia, tab]() {
		// start from the image in use, unless it is already on the list
		QString media = media_current();
		if (!media.isEmpty() && (findBookmark(media) >= 0))
			media.clear();
		if (fav_edit(&dia, -1, media))
			fav_fill(tab, conf.bookmarkList.size() - 1);
	});
	QObject::connect(tab, &QTableWidget::doubleClicked, &dia, [&dia, tab](const QModelIndex& idx) {
		if (fav_edit(&dia, idx.row()))
			fav_fill(tab, idx.row());
	});
	QObject::connect(ui.tbUp, &QToolButton::clicked, &dia, [tab]() {
		int row = tab->currentRow();
		if (row < 1) return;
		swapBookmarks(row, row - 1);
		fav_fill(tab, row - 1);
	});
	QObject::connect(ui.tbDown, &QToolButton::clicked, &dia, [tab]() {
		int row = tab->currentRow();
		if ((row < 0) || (row >= conf.bookmarkList.size() - 1)) return;
		swapBookmarks(row, row + 1);
		fav_fill(tab, row + 1);
	});
	QObject::connect(ui.tbDel, &QToolButton::clicked, &dia, [tab]() {
		int row = tab->currentRow();
		if (row < 0) return;
		delBookmark(row);
		fav_fill(tab, row);
	});
	QObject::connect(ui.pbClose, &QPushButton::clicked, &dia, &QDialog::accept);
	dia.exec();
	saveConfig();
}
