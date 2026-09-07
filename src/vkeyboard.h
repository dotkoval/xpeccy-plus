#pragma once

#include <QWidget>
#include <QDialog>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPixmap>
#include <QResizeEvent>
#include <QShowEvent>

#include "libxpeccy/input/input.h"

class keyWindow : public QDialog {
	Q_OBJECT
	public:
		keyWindow(QWidget* = NULL);
	signals:
		void s_key_press(QKeyEvent*);
		void s_key_release(QKeyEvent*);
	public slots:
		void switcher();
		void upd(Keyboard*);
		void rall(Keyboard*);
		void snap();		// sit under the emulator window
	private:
		Keyboard* kb;
		keyEntry xent;
		QPixmap pxm;
		unsigned dock:1;	// glued under the emulator window
		double scale();
		double storedZoom();
		QPoint imgPos(QPoint);
		void setZoom(double);
		void setDock(bool);
		void showMenu(QPoint);
		void paintEvent(QPaintEvent*);
		void mousePressEvent(QMouseEvent*);
		void mouseReleaseEvent(QMouseEvent*);
		void keyPressEvent(QKeyEvent*);
		void keyReleaseEvent(QKeyEvent*);
		void resizeEvent(QResizeEvent*);
		void showEvent(QShowEvent*);
};
