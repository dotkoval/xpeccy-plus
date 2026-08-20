#pragma once

// the list of ports the debugger watches. The same editor in two places: the
// deBUGa page of the options, and behind a right click on the PORTS block

#include <QDialog>
#include <QStringList>
#include <QListWidget>
#include <QWidget>

#include "../xcore/xcore.h"

class xPortWatch : public QWidget {
	public:
		xPortWatch(QWidget* = nullptr);
		void setPorts(QStringList);
		QStringList getPorts();
	private:
		QListWidget* list;
		void addPort(QString, bool);
};

class xPortWatchDialog : public QDialog {
	public:
		xPortWatchDialog(QWidget* = nullptr);
		xPortWatch* wid;
};
