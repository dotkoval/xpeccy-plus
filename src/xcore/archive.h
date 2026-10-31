#pragma once

// Archives read inside the program: .zip, .gz and .7z, so an image packed in one opens
// with no archiver on the machine. Qt Core, zlib and the LZMA SDK only, so the archive
// tests build this file on its own.

#include <QByteArray>
#include <QString>
#include <QVector>
#include <functional>

class QFile;
class QIODevice;

enum {
	ARC_NONE = 0,
	ARC_ZIP,
	ARC_GZ,
	ARC_7Z
};

// what one entry may unpack to (a 7z: its solid block); the largest image known is a
// few MB, a long WAV recording a hundred or so
#define ARC_MAX_SIZE (256LL << 20)

struct arcEntry {
	QString name;		// the path inside, folders split by '/'
	qint64 size;		// unpacked, as the archive states it; a .gz may not know
	bool folder;
};

// ARC_* by the file's name
int arc_type(const QString& path);
// every entry, in the archive's own order
bool arc_list(const QString& archive, QVector<arcEntry>* list, QString* err);
// one entry, by its name in arc_list(), into the file to; nothing is left there on failure
bool arc_extract(const QString& archive, const QString& entry, const QString& to, QString* err);

// The zip format itself, shared with the System/Next card's unpacker, which keeps
// its own stricter rules on names and paths.
struct zipEntry {
	QByteArray name;	// as the central directory spells it
	int flags;			// general purpose bits: 1 encrypted, 0x800 a UTF-8 name
	qint64 local;		// where its local header is
	qint64 packed;		// bytes in the archive
	qint64 unpacked;
	quint32 crc;
	int method;
	bool folder;
};

// the central directory: refuses multi-volume, ZIP64, encryption and methods other than
// stored and deflate
bool zip_dir(QFile* file, QVector<zipEntry>* list, QString* err);
// where the entry's data starts, once its local header agrees with the directory
bool zip_data_at(QFile* file, const zipEntry& entry, qint64* at, QString* err);
// its bytes into out, checked against the size and CRC32 stated; progress(count) is
// told each block written and stops it by returning false
bool zip_data(QFile* file, const zipEntry& entry, qint64 at, QIODevice* out, QString* err,
	const std::function<bool(int)>& progress = nullptr);
// the name as text: UTF-8 when flagged or valid, else CP866 or CP437 (see archive.cpp)
QString zip_name(const zipEntry& entry);
