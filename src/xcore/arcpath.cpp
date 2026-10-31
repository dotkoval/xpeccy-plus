#include "arcpath.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QMap>

#include <memory>

bool arc_split(const QString& path, QString* archive, QString* entry) {
	const QString clean = QDir::fromNativeSeparators(path);
	// the first prefix that is an archive file: a file has nothing under it but its entries
	for (int at = clean.indexOf('/'); at >= 0; at = clean.indexOf('/', at + 1)) {
		const QString head = clean.left(at);
		if ((arc_type(head) == ARC_NONE) || !QFileInfo(head).isFile()) continue;
		const QString rest = clean.mid(at + 1);
		if (rest.isEmpty()) return false;
		if (archive) *archive = head;
		if (entry) *entry = rest;
		return true;
	}
	return false;
}

QString arc_join(const QString& archive, const QString& entry) {
	return QDir::fromNativeSeparators(archive) + "/" + entry;
}

bool arc_bare(const QString& path) {
	return (arc_type(path) != ARC_NONE) && QFileInfo(path).isFile();
}

QString arc_name(const QString& path) {
	QString archive, entry;
	if (!arc_split(path, &archive, &entry)) return QFileInfo(path).fileName();
	return QFileInfo(archive).fileName() + " > " + QFileInfo(entry).fileName();
}

bool arc_exists(const QString& path) {
	QString archive;
	return arc_split(path, &archive) ? QFileInfo(archive).isFile() : QFileInfo::exists(path);
}

QString arc_canonical(const QString& path) {
	QString archive, entry;
	if (!arc_split(path, &archive, &entry)) return QFileInfo(path).canonicalFilePath();
	const QString head = QFileInfo(archive).canonicalFilePath();
	return head.isEmpty() ? head : arc_join(head, entry);
}

QString arc_beside(const QString& path, const QString& ext) {
	QString archive, entry;
	if (!arc_split(path, &archive, &entry)) {
		archive = path;
		entry = path;
	}
	const QFileInfo image(entry);
	const QString name = ext.isEmpty() ? image.fileName() : image.completeBaseName() + ext;
	return QFileInfo(archive).dir().filePath(name);
}

/* The temporary folder
 *
 * One per running program, <temp>/xpeccy-media-<pid>, holding a lock while it runs;
 * every entry unpacked goes to a numbered folder of its own in it, under its own file
 * name, since loaders and the type pick go by the name. A folder whose lock no running
 * program holds is a crashed run's and is removed.
 */

static const char arc_prefix[] = "xpeccy-media-";

static QString arc_root() {
	static const QString root = QDir(QDir::tempPath()).filePath(
		arc_prefix + QString::number(QCoreApplication::applicationPid()));
	return root;
}

static std::unique_ptr<QLockFile> arc_lock;

void arc_start() {
	QDir temp(QDir::tempPath());
	const QStringList found = temp.entryList({QString(arc_prefix) + "*"}, QDir::Dirs | QDir::NoDotAndDotDot);
	for (const QString& name : found) {
		const QString dir = temp.filePath(name);
		if (dir == arc_root()) continue;
		QLockFile lock(QDir(dir).filePath("lock"));
		if (!lock.tryLock(0)) continue;		// a program running now
		lock.unlock();
		QDir(dir).removeRecursively();
	}
}

void arc_finish() {
	if (!arc_lock) return;
	arc_lock->unlock();
	arc_lock.reset();
	QDir(arc_root()).removeRecursively();
}

static bool arc_fail(QString* err, const QString& message) {
	*err = message;
	return false;
}

static bool arc_own_root(QString* err) {
	if (arc_lock) return true;
	if (!QDir().mkpath(arc_root()))
		return arc_fail(err, "Cannot make the folder for unpacked images in " + QDir::toNativeSeparators(QDir::tempPath()));
	arc_lock.reset(new QLockFile(QDir(arc_root()).filePath("lock")));
	if (!arc_lock->tryLock(0)) {
		arc_lock.reset();
		return arc_fail(err, "The folder for unpacked images is in use");
	}
	return true;
}

// a file name any system takes, its extension kept
static QString arc_file_name(const QString& entry) {
	QString name = QFileInfo(entry).fileName();
	for (QChar& ch : name) {
		if ((ch.unicode() < 32) || QString("<>:\"|?*\\/").contains(ch)) ch = '_';
	}
	while (name.endsWith('.') || name.endsWith(' ')) name.chop(1);
	return name.isEmpty() ? QString("image") : name;
}

// what was read or unpacked, kept while the archive stays the same file
struct arcKept {
	qint64 size = -1;
	QDateTime time;
	QVector<arcEntry> entries;
	QMap<QString, QString> copies;		// entry -> its unpacked copy
};

static QMap<QString, arcKept> arc_kept;

static arcKept* arc_keep(const QString& archive) {
	const QFileInfo info(archive);
	const QString key = info.canonicalFilePath();
	if (key.isEmpty()) return nullptr;
	arcKept& kept = arc_kept[key];
	if ((kept.size != info.size()) || (kept.time != info.lastModified())) {
		kept = arcKept();
		kept.size = info.size();
		kept.time = info.lastModified();
	}
	return &kept;
}

const QVector<arcEntry>* arc_entries(const QString& archive, QString* err) {
	arcKept* kept = arc_keep(archive);
	if (!kept) {
		arc_fail(err, QDir::toNativeSeparators(archive) + " is not there");
		return nullptr;
	}
	if (kept->entries.isEmpty() && !arc_list(archive, &kept->entries, err)) {
		kept->size = -1;		// read again next time
		return nullptr;
	}
	return &kept->entries;
}

QString arc_local(const QString& path, QString* err) {
	QString archive, entry;
	if (!arc_split(path, &archive, &entry)) return path;
	arcKept* kept = arc_keep(archive);
	if (!kept) {
		arc_fail(err, QDir::toNativeSeparators(archive) + " is not there");
		return QString();
	}
	const QString have = kept->copies.value(entry);
	if (!have.isEmpty() && QFileInfo(have).isFile()) return have;
	if (!arc_own_root(err)) return QString();
	static int count = 0;
	const QString dir = QDir(arc_root()).filePath(QString::number(++count));
	if (!QDir().mkpath(dir)) {
		arc_fail(err, "Cannot make the folder for unpacked images");
		return QString();
	}
	const QString copy = QDir(dir).filePath(arc_file_name(entry));
	if (!arc_extract(archive, entry, copy, err)) {
		QDir(dir).removeRecursively();
		return QString();
	}
	kept->copies.insert(entry, copy);
	return copy;
}
