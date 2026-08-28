#pragma once

#include <QWidget>
#include <QList>
#include <QString>

// one line of the stack panel: the offset column ("-2:", or SP's own address
// at +0) and the word that lies there
typedef struct {
	QString name;
	QString value;
	bool anchor;		// the row at SP itself: painted on a tinted band
} xStackRow;

// The stack panel. Painted instead of laid out: it shows as many lines as its
// current height fits, and the words have to stand in one column whatever the
// offsets beside them are.
class xStackView : public QWidget {
	public:
		xStackView(QWidget* = nullptr);
		int rowsFit() const;			// lines that fit now, never less than 1
		void setRows(const QList<xStackRow>&);
		QSize minimumSizeHint() const;
		QSize sizeHint() const;
	protected:
		void paintEvent(QPaintEvent*);
	private:
		QList<xStackRow> rows;
		int rowPitch() const;
};
