#include <QFileInfo>
#include <QDir>

#include "xcore.h"

void addBookmark(std::string nm, std::string fp) {
	xBookmark nbm;
	nbm.name = nm;
	nbm.path = fp;
	conf.bookmarkList.push_back(nbm);
}

// one file whichever way its path was written; no disk access, so a favorite
// on a drive that is gone cannot hold the menu up
static QString bkm_key(const QString& path) {
	return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

int findBookmark(const QString& path) {
#ifdef _WIN32
	Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
	Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
	QString key = bkm_key(path);
	for (int i = 0; i < conf.bookmarkList.size(); i++) {
		if (bkm_key(QString::fromLocal8Bit(conf.bookmarkList[i].path.c_str())).compare(key, cs) == 0)
			return i;
	}
	return -1;
}

void swapBookmarks(int p1, int p2) {
	xBookmark bm = conf.bookmarkList[p1];
	conf.bookmarkList[p1] = conf.bookmarkList[p2];
	conf.bookmarkList[p2] = bm;
}

void setBookmark(int idx,std::string nm, std::string fp) {
	conf.bookmarkList[idx].name = nm;
	conf.bookmarkList[idx].path = fp;
}

void delBookmark(int idx) {
	conf.bookmarkList.erase(conf.bookmarkList.begin() + idx);
}

void clearBookmarks() {
	conf.bookmarkList.clear();
}
