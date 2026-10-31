#include "archive.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <cstdlib>
#include <cstring>
#include <zlib.h>

#include "7z.h"
#include "7zCrc.h"

// Everything is read strictly: what the archive states about an entry is checked
// against what comes out of it, and what does not fit stops the reading with a
// message rather than being guessed at.

static bool arc_fail(QString* err, const QString& message) {
	*err = message;
	return false;
}

int arc_type(const QString& path) {
	if (path.endsWith(".zip", Qt::CaseInsensitive)) return ARC_ZIP;
	if (path.endsWith(".gz", Qt::CaseInsensitive)) return ARC_GZ;
	if (path.endsWith(".7z", Qt::CaseInsensitive)) return ARC_7Z;
	return ARC_NONE;
}

static QString arc_too_big(const QString& name) {
	return QString("%1 unpacks to more than %2 MB").arg(name).arg(ARC_MAX_SIZE >> 20);
}

// the file to is written whole or not at all
static bool arc_out(QFile* out, const QString& to, QString* err) {
	out->setFileName(to);
	if (!out->open(QFile::WriteOnly))
		return arc_fail(err, "Cannot create " + to + ": " + out->errorString());
	return true;
}

static bool arc_done(QFile* out, bool ok, QString* err) {
	if (ok && !out->flush())
		ok = arc_fail(err, "Cannot write " + out->fileName() + ": " + out->errorString());
	out->close();
	if (!ok) out->remove();
	return ok;
}

/* zip
 *
 * The central directory at the end lists every entry, each naming the local header its
 * data follows. Entries are stored or deflated and carry a CRC32 and a size to check
 * the result against. The data offset comes from the local header, whose own extra
 * field is not the central directory's.
 */

#define ZIP_LOCAL	0x04034b50
#define ZIP_CENTRAL	0x02014b50
#define ZIP_END		0x06054b50
#define ZIP_LOCAL_LEN	30
#define ZIP_CENTRAL_LEN	46
#define ZIP_END_LEN	22

static quint16 zip_le16(const char* data) {
	return quint16(static_cast<unsigned char>(data[0]))
		| quint16(static_cast<unsigned char>(data[1])) << 8;
}

static quint32 zip_le32(const char* data) {
	return quint32(static_cast<unsigned char>(data[0]))
		| quint32(static_cast<unsigned char>(data[1])) << 8
		| quint32(static_cast<unsigned char>(data[2])) << 16
		| quint32(static_cast<unsigned char>(data[3])) << 24;
}

bool zip_dir(QFile* file, QVector<zipEntry>* list, QString* err) {
	list->clear();
	const qint64 size = file->size();
	// the end record is the last 22 bytes, or that plus a comment of up to 65535
	const int tail = int(qMin<qint64>(size, 65557));
	if (tail < ZIP_END_LEN) return arc_fail(err, "This is not a ZIP archive");
	QByteArray end(tail, 0);
	if (!file->seek(size - tail) || file->read(end.data(), tail) != tail)
		return arc_fail(err, "This is not a ZIP archive");
	int at = tail - ZIP_END_LEN;
	while (at >= 0 && zip_le32(end.constData() + at) != ZIP_END) at--;
	if (at < 0) return arc_fail(err, "This is not a ZIP archive");
	const int count = zip_le16(end.constData() + at + 10);
	const quint32 table = zip_le32(end.constData() + at + 16);
	if (zip_le16(end.constData() + at + 4) || zip_le16(end.constData() + at + 6))
		return arc_fail(err, "A ZIP archive on more than one disk is not supported");
	if (quint64(zip_le32(end.constData() + at + 12)) + table > quint64(size))
		return arc_fail(err, "The ZIP archive is truncated");
	if (!file->seek(table)) return arc_fail(err, "The ZIP archive is truncated");
	for (int i = 0; i < count; i++) {
		char header[ZIP_CENTRAL_LEN];
		if (file->read(header, sizeof(header)) != sizeof(header))
			return arc_fail(err, "The ZIP archive is truncated");
		if (zip_le32(header) != ZIP_CENTRAL)
			return arc_fail(err, "The ZIP archive has a damaged central directory");
		zipEntry entry;
		const int nameLen = zip_le16(header + 28);
		const int extraLen = zip_le16(header + 30);
		const int commentLen = zip_le16(header + 32);
		entry.flags = zip_le16(header + 8);
		entry.method = zip_le16(header + 10);
		entry.crc = zip_le32(header + 16);
		entry.packed = zip_le32(header + 20);
		entry.unpacked = zip_le32(header + 24);
		entry.local = zip_le32(header + 42);
		entry.name = QByteArray(nameLen, 0);
		if (file->read(entry.name.data(), nameLen) != nameLen
			|| !file->seek(file->pos() + extraLen + commentLen))
			return arc_fail(err, "The ZIP archive is truncated");
		if (entry.flags & 1) return arc_fail(err, "An encrypted ZIP archive is not supported");
		if (entry.packed == 0xffffffff || entry.unpacked == 0xffffffff || entry.local == 0xffffffff)
			return arc_fail(err, "A ZIP64 archive is not supported");
		if (entry.method != 0 && entry.method != 8)
			return arc_fail(err, "A ZIP archive packed with method " + QString::number(entry.method) + " is not supported");
		if (entry.local + ZIP_LOCAL_LEN > size)
			return arc_fail(err, "The ZIP archive is truncated");
		entry.folder = entry.name.endsWith('/');
		list->append(entry);
	}
	return true;
}

bool zip_data_at(QFile* file, const zipEntry& entry, qint64* at, QString* err) {
	char header[ZIP_LOCAL_LEN];
	if (!file->seek(entry.local) || file->read(header, sizeof(header)) != sizeof(header))
		return arc_fail(err, "The ZIP archive is truncated");
	if (zip_le32(header) != ZIP_LOCAL)
		return arc_fail(err, "The ZIP archive has no local header for " + zip_name(entry));
	if (zip_le16(header + 6) & 1) return arc_fail(err, "An encrypted ZIP archive is not supported");
	const int nameLen = zip_le16(header + 26);
	const int extraLen = zip_le16(header + 28);
	QByteArray name(nameLen, 0);
	if (file->read(name.data(), nameLen) != nameLen)
		return arc_fail(err, "The ZIP archive is truncated");
	if (name != entry.name)
		return arc_fail(err, "The ZIP archive names " + zip_name(entry) + " two different ways");
	*at = entry.local + ZIP_LOCAL_LEN + nameLen + extraLen;
	if (*at + entry.packed > file->size()) return arc_fail(err, "The ZIP archive is truncated");
	return true;
}

bool zip_data(QFile* file, const zipEntry& entry, qint64 at, QIODevice* out, QString* err,
	const std::function<bool(int)>& progress) {
	if (!file->seek(at)) return arc_fail(err, "The ZIP archive is truncated");
	quint32 crc = crc32(0, Z_NULL, 0);
	qint64 left = entry.packed, written = 0;
	unsigned char in[65536], produced[65536];
	// what comes out is written and counted the same way, stored or deflated
	auto put = [&](const unsigned char* bytes, int count) -> bool {
		// stopped at the size it states, not after: deflate can make 1000 times its size
		if (count > entry.unpacked - written)
			return arc_fail(err, "The ZIP entry " + zip_name(entry) + " does not unpack to the size it states");
		if (out->write(reinterpret_cast<const char*>(bytes), count) != count)
			return arc_fail(err, "Cannot write archive file " + zip_name(entry));
		crc = crc32(crc, bytes, uint(count));
		written += count;
		return !progress || progress(count) || arc_fail(err, "Unpacking cancelled");
	};
	if (entry.method == 0) {
		while (left) {
			const int count = int(qMin<qint64>(left, sizeof(in)));
			if (file->read(reinterpret_cast<char*>(in), count) != count)
				return arc_fail(err, "The ZIP archive is truncated");
			left -= count;
			if (!put(in, count)) return false;
		}
	} else {
		z_stream zs;
		memset(&zs, 0, sizeof(zs));
		if (inflateInit2(&zs, -MAX_WBITS) != Z_OK)
			return arc_fail(err, "Cannot start the decompressor");
		bool ok = true;
		for (;;) {
			if (!zs.avail_in && left) {
				const int count = int(qMin<qint64>(left, sizeof(in)));
				if (file->read(reinterpret_cast<char*>(in), count) != count) {
					ok = arc_fail(err, "The ZIP archive is truncated");
					break;
				}
				left -= count;
				zs.next_in = in;
				zs.avail_in = uint(count);
			}
			zs.next_out = produced;
			zs.avail_out = uint(sizeof(produced));
			const uint before = zs.avail_in;
			const int code = inflate(&zs, Z_NO_FLUSH);
			const int count = int(sizeof(produced) - zs.avail_out);
			if (count && !put(produced, count)) {
				ok = false;
				break;
			}
			if (code == Z_STREAM_END) break;
			if (code != Z_OK && code != Z_BUF_ERROR) {
				ok = arc_fail(err, "The deflate stream in the ZIP archive is damaged");
				break;
			}
			// neither output nor input used: it cannot go on, and must not loop
			if (!count && (zs.avail_in == before)) {
				ok = arc_fail(err, left ? "The deflate stream in the ZIP archive is damaged"
					: "The deflate stream in the ZIP archive is truncated");
				break;
			}
		}
		inflateEnd(&zs);
		if (!ok) return false;
	}
	if (written != entry.unpacked)
		return arc_fail(err, "The ZIP entry " + zip_name(entry) + " does not unpack to the size it states");
	if (crc != entry.crc)
		return arc_fail(err, "The ZIP entry " + zip_name(entry) + " does not match its checksum");
	return true;
}

// The format says a name without the UTF-8 flag is CP437, but DOS archivers wrote the
// system's own code page. Measured on the archives about: Russian names are CP866 and
// spell words, so their letters stand together; Western names are CP437 with a letter
// accented here and there.
static const quint16 cp437_high[128] = {
	0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
	0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
	0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
	0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
	0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
	0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
	0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
	0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
	0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
	0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
	0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
	0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
	0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
	0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
	0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0,
};

static const quint16 cp866_high[128] = {
	0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
	0x0418, 0x0419, 0x041a, 0x041b, 0x041c, 0x041d, 0x041e, 0x041f,
	0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
	0x0428, 0x0429, 0x042a, 0x042b, 0x042c, 0x042d, 0x042e, 0x042f,
	0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
	0x0438, 0x0439, 0x043a, 0x043b, 0x043c, 0x043d, 0x043e, 0x043f,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
	0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
	0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
	0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
	0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
	0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
	0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
	0x0448, 0x0449, 0x044a, 0x044b, 0x044c, 0x044d, 0x044e, 0x044f,
	0x0401, 0x0451, 0x0404, 0x0454, 0x0407, 0x0457, 0x040e, 0x045e,
	0x00b0, 0x2219, 0x00b7, 0x221a, 0x2116, 0x00a4, 0x25a0, 0x00a0,
};

QString zip_name(const zipEntry& entry) {
	const QByteArray& raw = entry.name;
	QString utf = QString::fromUtf8(raw);
	if ((entry.flags & 0x800) || (utf.toUtf8() == raw)) return utf;
	bool run = false;
	for (int i = 1; i < raw.size() && !run; i++)
		run = (raw[i] & 0x80) && (raw[i - 1] & 0x80);
	const quint16* high = run ? cp866_high : cp437_high;
	QString res;
	for (char ch : raw) {
		unsigned char byte = static_cast<unsigned char>(ch);
		res.append(QChar(byte < 0x80 ? byte : high[byte - 0x80]));
	}
	return res;
}

static bool zip_list(QFile* file, QVector<arcEntry>* list, QString* err) {
	QVector<zipEntry> dir;
	if (!zip_dir(file, &dir, err)) return false;
	QSet<QString> seen;
	for (const zipEntry& entry : dir) {
		arcEntry item;
		item.name = zip_name(entry);
		item.folder = entry.folder;
		if (item.folder) item.name.chop(1);
		item.size = entry.unpacked;
		// the same name twice: the first is the one an archiver shows
		if (item.name.isEmpty() || seen.contains(item.name)) continue;
		seen.insert(item.name);
		list->append(item);
	}
	return true;
}

static bool zip_extract(QFile* file, const QString& name, const QString& to, QString* err) {
	QVector<zipEntry> dir;
	if (!zip_dir(file, &dir, err)) return false;
	for (const zipEntry& entry : dir) {
		if (entry.folder || (zip_name(entry) != name)) continue;
		if (entry.unpacked > ARC_MAX_SIZE) return arc_fail(err, arc_too_big(name));
		qint64 at;
		if (!zip_data_at(file, entry, &at, err)) return false;
		QFile out;
		if (!arc_out(&out, to, err)) return false;
		return arc_done(&out, zip_data(file, entry, at, &out, err), err);
	}
	return arc_fail(err, "The archive has no " + name);
}

/* gz
 *
 * One stream, or several written one after another, each with its own CRC32 and size
 * that zlib checks. The header may carry the original file's name (FNAME); the entry
 * is named after the archive itself, which is the name the user sees, unless that has
 * no extension left once ".gz" is off.
 */

static QString gz_name(QFile* file) {
	QString own = QFileInfo(file->fileName()).fileName();
	own.chop(3);
	if (own.contains('.') || !file->seek(0)) return own;
	QByteArray head = file->read(10);
	if ((head.size() < 10) || (head[0] != '\x1f') || (head[1] != '\x8b') || !(head[3] & 8))
		return own;
	if (head[3] & 4) {		// FEXTRA comes before the name
		QByteArray len = file->read(2);
		if (len.size() < 2) return own;
		file->seek(file->pos() + zip_le16(len.constData()));
	}
	QByteArray name;
	char ch;
	while ((name.size() < 256) && file->getChar(&ch) && ch) name.append(ch);
	name = name.mid(name.lastIndexOf('/') + 1);
	name = name.mid(name.lastIndexOf('\\') + 1);
	return name.isEmpty() ? own : QString::fromLatin1(name);
}

static bool gz_list(QFile* file, QVector<arcEntry>* list, QString* err) {
	char magic[2];
	if (!file->seek(0) || (file->read(magic, 2) != 2) || (magic[0] != '\x1f') || (magic[1] != '\x8b'))
		return arc_fail(err, "This is not a gzip file");
	arcEntry item;
	item.name = gz_name(file);
	item.folder = false;
	item.size = -1;
	// ISIZE closes the file: the size of the last stream, the whole file's when it is one
	char size[4];
	if ((file->size() >= 18) && file->seek(file->size() - 4) && (file->read(size, 4) == 4))
		item.size = zip_le32(size);
	list->append(item);
	return true;
}

static bool gz_data(QFile* file, QIODevice* out, QString* err) {
	z_stream zs;
	memset(&zs, 0, sizeof(zs));
	if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK)
		return arc_fail(err, "Cannot start the decompressor");
	unsigned char in[65536], produced[65536];
	qint64 written = 0;
	bool ok = true, ended = false;
	file->seek(0);
	for (;;) {
		if (!zs.avail_in) {
			const qint64 count = file->read(reinterpret_cast<char*>(in), sizeof(in));
			if (count <= 0) {
				if (!ended) ok = arc_fail(err, "The gzip file is truncated");
				break;
			}
			zs.next_in = in;
			zs.avail_in = uint(count);
		}
		if (ended) {
			// another stream follows, or nothing but padding
			if (*zs.next_in == 0) {
				zs.next_in++;
				zs.avail_in--;
				continue;
			}
			inflateReset(&zs);
			ended = false;
		}
		zs.next_out = produced;
		zs.avail_out = uint(sizeof(produced));
		const int code = inflate(&zs, Z_NO_FLUSH);
		const int count = int(sizeof(produced) - zs.avail_out);
		written += count;
		if (written > ARC_MAX_SIZE) {
			ok = arc_fail(err, arc_too_big(QFileInfo(file->fileName()).fileName()));
			break;
		}
		if (count && (out->write(reinterpret_cast<const char*>(produced), count) != count)) {
			ok = arc_fail(err, "Cannot write the unpacked file");
			break;
		}
		if (code == Z_STREAM_END) {
			ended = true;
		} else if (code != Z_OK) {
			ok = arc_fail(err, (code == Z_BUF_ERROR) ? "The gzip file is truncated" : "The gzip file is damaged");
			break;
		}
	}
	inflateEnd(&zs);
	return ok;
}

static bool gz_extract(QFile* file, const QString& name, const QString& to, QString* err) {
	if (name != gz_name(file)) return arc_fail(err, "The archive has no " + name);
	QFile out;
	if (!arc_out(&out, to, err)) return false;
	return arc_done(&out, gz_data(file, &out, err), err);
}

/* 7z
 *
 * Through the LZMA SDK's reader. Entries packed together (a solid block) come out
 * together, so the block is unpacked whole in memory and the entry copied from it.
 */

struct szStream {
	ISeekInStream vt;		// first: the SDK hands back a pointer to it
	QFile* file;
};

static SRes sz_read(ISeekInStreamPtr pp, void* buf, size_t* size) {
	QFile* file = reinterpret_cast<const szStream*>(pp)->file;
	const qint64 got = *size ? file->read(static_cast<char*>(buf), qint64(*size)) : 0;
	*size = (got > 0) ? size_t(got) : 0;
	return (got < 0) ? SZ_ERROR_READ : SZ_OK;
}

static SRes sz_seek(ISeekInStreamPtr pp, Int64* pos, ESzSeek origin) {
	QFile* file = reinterpret_cast<const szStream*>(pp)->file;
	qint64 to = *pos;
	if (origin == SZ_SEEK_CUR) to += file->pos();
	if (origin == SZ_SEEK_END) to += file->size();
	if ((to < 0) || !file->seek(to)) return SZ_ERROR_READ;
	*pos = to;
	return SZ_OK;
}

static void* sz_alloc(ISzAllocPtr, size_t size) {
	return size ? malloc(size) : nullptr;
}

static void sz_free(ISzAllocPtr, void* address) {
	free(address);
}

static const ISzAlloc sz_mem = {sz_alloc, sz_free};

static QString sz_error(SRes res) {
	switch (res) {
		case SZ_ERROR_NO_ARCHIVE: return "This is not a 7z archive";
		case SZ_ERROR_UNSUPPORTED: return "This 7z archive is encrypted or packed in a way that is not supported";
		case SZ_ERROR_MEM: return "Not enough memory to unpack the 7z archive";
		case SZ_ERROR_READ: return "Cannot read the 7z archive";
		case SZ_ERROR_INPUT_EOF: return "The 7z archive is truncated";
	}
	return "The 7z archive is damaged";
}

// the archive opened, for the length of one call
class szArchive {
public:
	CSzArEx db;
	explicit szArchive(QFile* file) {
		static const bool crc = (CrcGenerateTable(), true);
		(void)crc;
		stream.vt.Read = sz_read;
		stream.vt.Seek = sz_seek;
		stream.file = file;
		LookToRead2_CreateVTable(&look, False);
		look.buf = static_cast<Byte*>(malloc(1 << 18));
		look.bufSize = 1 << 18;
		look.realStream = &stream.vt;
		LookToRead2_INIT(&look)
		SzArEx_Init(&db);
	}
	~szArchive() {
		SzArEx_Free(&db, &sz_mem);
		free(look.buf);
	}
	ILookInStreamPtr in() {return &look.vt;}
	bool open(QString* err) {
		if (!look.buf) return arc_fail(err, sz_error(SZ_ERROR_MEM));
		const SRes res = SzArEx_Open(&db, &look.vt, &sz_mem, &sz_mem);
		return (res == SZ_OK) || arc_fail(err, sz_error(res));
	}
	QString name(UInt32 index) {
		const size_t len = SzArEx_GetFileNameUtf16(&db, index, nullptr);
		QVector<UInt16> buf(int(len) + 1, 0);
		SzArEx_GetFileNameUtf16(&db, index, buf.data());
		QString res = QString::fromUtf16(reinterpret_cast<const char16_t*>(buf.constData()));
		return res.replace('\\', '/');
	}
private:
	szStream stream;
	CLookToRead2 look;
};

static bool sz_list(QFile* file, QVector<arcEntry>* list, QString* err) {
	szArchive arc(file);
	if (!arc.open(err)) return false;
	for (UInt32 i = 0; i < arc.db.NumFiles; i++) {
		arcEntry item;
		item.name = arc.name(i);
		item.folder = SzArEx_IsDir(&arc.db, i);
		item.size = qint64(SzArEx_GetFileSize(&arc.db, i));
		if (!item.name.isEmpty()) list->append(item);
	}
	return true;
}

static bool sz_extract(QFile* file, const QString& name, const QString& to, QString* err) {
	szArchive arc(file);
	if (!arc.open(err)) return false;
	for (UInt32 i = 0; i < arc.db.NumFiles; i++) {
		if (SzArEx_IsDir(&arc.db, i) || (arc.name(i) != name)) continue;
		const UInt32 block = arc.db.FileToFolder[i];
		if ((block != UInt32(-1)) && (SzAr_GetFolderUnpackSize(&arc.db.db, block) > UInt64(ARC_MAX_SIZE)))
			return arc_fail(err, arc_too_big(name));
		UInt32 cached = UInt32(-1);
		Byte* data = nullptr;
		size_t dataSize = 0, offset = 0, size = 0;
		const SRes res = SzArEx_Extract(&arc.db, arc.in(), i, &cached, &data, &dataSize,
			&offset, &size, &sz_mem, &sz_mem);
		bool ok = (res == SZ_OK) || arc_fail(err, sz_error(res));
		QFile out;
		if (ok) ok = arc_out(&out, to, err);
		if (ok) {
			ok = !size || (out.write(reinterpret_cast<const char*>(data + offset), qint64(size)) == qint64(size))
				|| arc_fail(err, "Cannot write " + to + ": " + out.errorString());
			ok = arc_done(&out, ok, err);
		}
		free(data);
		return ok;
	}
	return arc_fail(err, "The archive has no " + name);
}

bool arc_list(const QString& archive, QVector<arcEntry>* list, QString* err) {
	list->clear();
	err->clear();
	QFile file(archive);
	if (!file.open(QFile::ReadOnly)) return arc_fail(err, file.errorString());
	switch (arc_type(archive)) {
		case ARC_ZIP: return zip_list(&file, list, err);
		case ARC_GZ: return gz_list(&file, list, err);
		case ARC_7Z: return sz_list(&file, list, err);
	}
	return arc_fail(err, "This is not an archive the program reads");
}

bool arc_extract(const QString& archive, const QString& entry, const QString& to, QString* err) {
	err->clear();
	QFile file(archive);
	if (!file.open(QFile::ReadOnly)) return arc_fail(err, file.errorString());
	switch (arc_type(archive)) {
		case ARC_ZIP: return zip_extract(&file, entry, to, err);
		case ARC_GZ: return gz_extract(&file, entry, to, err);
		case ARC_7Z: return sz_extract(&file, entry, to, err);
	}
	return arc_fail(err, "This is not an archive the program reads");
}
