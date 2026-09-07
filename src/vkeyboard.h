#pragma once

#include <QWidget>
#include <QDialog>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPixmap>
#include <QColor>
#include <QResizeEvent>
#include <QShowEvent>
#include <QMoveEvent>

#include "libxpeccy/input/input.h"

#if defined(__APPLE__)
// vkeyboard_mac.mm: let the window keep its shape by itself
void vkbd_set_aspect(QWidget*, int, int);
#endif

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
		QColor ground;		// what the picture is drawn to sit on
		unsigned dock:1;	// glued under the emulator window
		double scale();
		double storedZoom();
		int higFor(int);
		int widFor(int);
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
		void moveEvent(QMoveEvent*);
		void showEvent(QShowEvent*);
#if defined(_WIN32)
	#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
		bool nativeEvent(const QByteArray&, void*, qintptr*);
	#else
		bool nativeEvent(const QByteArray&, void*, long*);
	#endif
#endif
};
