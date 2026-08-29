#include <QDir>
#include <QFileInfo>
#include <QDateTime>

#include "vfat_scan.h"

// Host folder -> synthetic FAT32 volume. The scan lives here so libxpeccy stays
// free of platform directory code; vfat.c only ever sees a tree and file paths.

#define VFS_MAXDEPTH	16		// deeper folders are left out
#define VFS_MAXNODES	20000		// and so is anything past this many entries
#define VFS_MAXFILE	0xfffffffeLL	// FAT can't address a bigger file

static void vfat_scan_dir(vFat* vf, int parent, const QString& path, int depth) {
	QDir dir(path);
	QFileInfoList list = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name | QDir::DirsFirst);
	foreach(QFileInfo inf, list) {
		if (vf->nodes >= VFS_MAXNODES) return;
		unsigned int mtime = inf.lastModified().toMSecsSinceEpoch() / 1000;
		QByteArray name = inf.fileName().toUtf8();
		if (inf.isDir()) {
			if (depth >= VFS_MAXDEPTH) continue;
			int idx = vfat_add(vf, parent, name.constData(), NULL, 0, mtime, 1);
			if (idx < 0) return;
			vfat_scan_dir(vf, idx, inf.absoluteFilePath(), depth + 1);
		} else if (inf.isFile() && (inf.size() <= VFS_MAXFILE)) {
			QByteArray host = inf.absoluteFilePath().toLocal8Bit();
			vfat_add(vf, parent, name.constData(), host.constData(), inf.size(), mtime, 0);
		}
	}
}

vFat* vfat_scan(const QString& path) {
	QFileInfo inf(path);
	if (!inf.isDir() || !inf.isReadable()) return NULL;
	vFat* vf = vfat_create();
	if (!vf) return NULL;
	unsigned int mtime = inf.lastModified().toMSecsSinceEpoch() / 1000;
	vfat_add(vf, -1, "", NULL, 0, mtime, 1);		// root
	vfat_scan_dir(vf, 0, path, 0);
	if (!vfat_build(vf, 0)) {
		vfat_free(vf);
		return NULL;
	}
	return vf;
}

void sdc_mount(SDCard* sdc, const QString& path) {
	if (!sdc) return;
	if (path.isEmpty()) {
		sdcSetImage(sdc, "");
	} else if (QFileInfo(path).isDir()) {
		sdcSetFolder(sdc, path.toLocal8Bit().constData(), vfat_scan(path));
	} else {
		sdcSetImage(sdc, path.toLocal8Bit().constData());
	}
}

void sdc_remount(SDCard* sdc) {
	if (!sdc || !sdc->image) return;
	QString path = QString::fromLocal8Bit(sdc->image);	// own the path: mounting frees it
	sdc_mount(sdc, path);
}

void ide_mount(IDE* ide, int dev, const QString& path) {
	if (!ide) return;
	if (path.isEmpty()) {
		ideSetImage(ide, dev, "");
	} else if (QFileInfo(path).isDir()) {
		ideSetFolder(ide, dev, path.toLocal8Bit().constData(), vfat_scan(path));
	} else {
		ideSetImage(ide, dev, path.toLocal8Bit().constData());
	}
}

void ide_remount(IDE* ide) {
	if (!ide) return;
	QString mpath = ide->master->image ? QString::fromLocal8Bit(ide->master->image) : QString();
	QString spath = ide->slave->image ? QString::fromLocal8Bit(ide->slave->image) : QString();
	if (!mpath.isEmpty()) ide_mount(ide, IDE_MASTER, mpath);
	if (!spath.isEmpty()) ide_mount(ide, IDE_SLAVE, spath);
}
