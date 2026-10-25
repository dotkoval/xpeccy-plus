#include "sysnext.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QVector>

#include <cstring>
#include <functional>
#include <limits>
#include <zlib.h>

// .tar.gz and .zip are read here, so a card needs no tar, unzip or PowerShell on the
// machine. Both are read strictly: anything unexpected stops the unpacking rather than
// being guessed at, and no file is written over one already there. The caller's
// checksum of what these make catches the rest.

static bool sn_fail(QString* err, const QString& message) {
	*err = message;
	return false;
}

// a folder to unpack into: made if need be, and empty, so nothing is written over
static bool sn_destination(const QString& dir, QString* err, QDir* root) {
	if (QFileInfo(dir).isSymLink())
		return sn_fail(err, "The unpacking folder is a symbolic link");
	if (!QDir().mkpath(dir) || !QFileInfo(dir).isDir())
		return sn_fail(err, "Cannot make the unpacking folder " + QDir::toNativeSeparators(dir));
	if (!QDir(dir).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
		return sn_fail(err, "The unpacking folder is not empty");
	const QString canonical = QFileInfo(dir).canonicalFilePath();
	if (canonical.isEmpty()) return sn_fail(err, "Cannot access the unpacking folder");
	root->setPath(canonical);
	return true;
}

// the path an entry unpacks to, or nothing: no absolute paths, no "..", no names a FAT
// card cannot hold, so an archive can neither reach outside the folder nor make a card
// the Next cannot read
static bool sn_entry_path(const QByteArray& bytes, QString* path) {
	if (bytes.isEmpty()) return false;
	*path = QString::fromUtf8(bytes);
	if (path->toUtf8() != bytes || path->startsWith('/') || path->contains('\\')) return false;
	const QStringList parts = path->split('/');
	static const QRegularExpression device("^(CON|PRN|AUX|NUL|CONIN\\$|CONOUT\\$|"
		"COM[1-9\\x{00b9}\\x{00b2}\\x{00b3}]|LPT[1-9\\x{00b9}\\x{00b2}\\x{00b3}])( *\\.| *$)",
		QRegularExpression::CaseInsensitiveOption);
	for (const QString& part : parts) {
		if (part == "..") return false;
		if (part.isEmpty() || part == ".") continue;
		if (part.endsWith('.') || part.endsWith(' ') || device.match(part).hasMatch()) return false;
		// the card is FAT: refuse what FAT and Windows do not take, whatever the host
		for (QChar ch : part)
			if (ch.unicode() < 32 || QString("<>:\"|?*").contains(ch)) return false;
	}
	*path = QDir::cleanPath(*path);
	return !path->isEmpty();
}

// where an entry goes, refusing a path already on disk; the caller opens the file
static bool sn_take_path(const QDir& root, const QString& relative, QString* err, QString* path) {
	*path = root.filePath(relative);
	if (QFileInfo::exists(*path))
		return sn_fail(err, "The archive holds " + relative + " twice");
	if (!QDir().mkpath(QFileInfo(*path).path()))
		return sn_fail(err, "Cannot create the folder for " + relative);
	return true;
}

/* tar.gz
 *
 * The tar header is a fixed 512 bytes: the name, the size as octal or base-256, the
 * checksum, the type and - in the POSIX form - the first half of a long name. Names
 * too long for that come in a header of their own first: GNU's 'L' and the 'x' of
 * pax, whose records say "path" and "size" for the entry that follows.
 */

static bool sn_zero(const char* data, int size) {
	for (int i = 0; i < size; i++)
		if (data[i]) return false;
	return true;
}

static bool sn_octal(const char* data, int length, qint64* value) {
	*value = 0;
	const bool binary = (static_cast<unsigned char>(data[0]) & 0x80) != 0;
	bool ended = false;
	bool started = false;
	for (int i = 0; i < length; i++) {
		unsigned char ch = data[i];
		if (binary && !i) {
			if (ch & 0x40) return false;
			ch &= 0x7f;
		}
		if (!binary && (ch == ' ' || ch == 0)) {
			if (started || ch == 0) ended = true;
			continue;
		}
		if (!binary && (ended || ch < '0' || ch > '7')) return false;
		started = true;
		const int digit = binary ? ch : ch - '0';
		const int base = binary ? 256 : 8;
		if (*value > (std::numeric_limits<qint64>::max() - digit) / base) return false;
		*value = *value * base + digit;
	}
	return true;
}

class snTarInput {
	public:
		snTarInput(const QString& path, QString* error, std::function<bool(qint64, qint64)> progress)
			: err(error), tick(progress), total(QFileInfo(path).size()) {
#ifdef _WIN32
			file = gzopen_w(path.toStdWString().c_str(), "rb");
#else
			file = gzopen(QFile::encodeName(path).constData(), "rb");
#endif
			if (!file) sn_fail(err, "Cannot open " + QDir::toNativeSeparators(path));
		}
		~snTarInput() {if (file) gzclose(file);}
		bool get(char* data, int size) {
			if (!file) return false;
			if (size && gzread(file, data, size) != size)
				return sn_fail(err, "The gzip archive is truncated or damaged");
			return progress();
		}
		// the end: what is left may only be the zero padding tar rounds its blocks out with
		bool finish() {
			char data[65536];
			int count;
			while ((count = gzread(file, data, sizeof(data))) > 0) {
				if (!sn_zero(data, count))
					return sn_fail(err, "Unexpected data after the tar end marker");
				if (!progress()) return false;
			}
			int code;
			gzerror(file, &code);
			if (count < 0 || (code != Z_OK && code != Z_STREAM_END))
				return sn_fail(err, "The gzip archive is truncated or damaged");
			return !tick || tick(total, total) || sn_fail(err, "Unpacking cancelled");
		}
	private:
		gzFile file;
		QString* err;
		std::function<bool(qint64, qint64)> tick;
		qint64 total;
		bool progress() {
			return !tick || tick(qMin<qint64>(gzoffset(file), total), total)
				|| sn_fail(err, "Unpacking cancelled");
		}
};

static QByteArray sn_field(const char* field, int size) {
	const char* end = static_cast<const char*>(memchr(field, 0, size));
	return QByteArray(field, end ? int(end - field) : size);
}

static bool sn_decimal(const QByteArray& data, qint64* value) {
	for (char ch : data)
		if (ch < '0' || ch > '9') return false;
	bool ok;
	*value = data.toLongLong(&ok);
	return ok;
}

static bool sn_pax(const QByteArray& data, QMap<QByteArray, QByteArray>* values, bool global) {
	int at = 0;
	while (at < data.size()) {
		const int space = data.indexOf(' ', at);
		if (space < 0) return false;
		qint64 length;
		if (!sn_decimal(data.mid(at, space - at), &length)
			|| length <= space - at + 2 || length > data.size() - at) return false;
		const int end = at + int(length);
		const int equals = data.indexOf('=', space + 1);
		if (data[end - 1] != '\n' || equals <= space + 1 || equals >= end - 1) return false;
		const QByteArray key = data.mid(space + 1, equals - space - 1);
		const QByteArray value = data.mid(equals + 1, end - equals - 2);
		if (key.startsWith("GNU.sparse.") || key == "SCHILY.realsize") return false;
		if (key == "path" || key == "size") {
			if (global && value.isEmpty())
				values->remove(key);
			else
				values->insert(key, value);
		}
		at = end;
	}
	return true;
}

static bool sn_unpack_tar_gz(const QString& archive, const QString& dir, QString* err,
	std::function<bool(qint64, qint64)> tick) {
	QFile source(archive);
	if (!source.open(QFile::ReadOnly))
		return sn_fail(err, source.errorString());
	if (source.read(2) != QByteArray("\x1f\x8b", 2))
		return sn_fail(err, "This is not a gzip archive");
	source.close();
	QDir root;
	if (!sn_destination(dir, err, &root)) return false;
	snTarInput input(archive, err, tick);
	QMap<QByteArray, QByteArray> global, local;
	QByteArray longName;
	bool pending = false;
	char header[512], data[65536];
	for (;;) {
		if (!input.get(header, sizeof(header))) return false;
		// two zero blocks end a tar; one is taken too, and whatever follows it is then
		// checked to be padding, so an archive cut short is still refused
		if (sn_zero(header, sizeof(header))) {
			if (pending) return sn_fail(err, "Invalid tar end marker");
			return input.finish();
		}
		qint64 checksum, size;
		if (!sn_octal(header + 148, 8, &checksum) || !sn_octal(header + 124, 12, &size))
			return sn_fail(err, "Invalid tar size or checksum");
		// the bytes of the header summed: unsigned, or signed as the oldest tar wrote them
		unsigned usum = 0;
		long ssum = 0;
		for (int i = 0; i < 512; i++) {
			const unsigned char ch = (i >= 148 && i < 156) ? ' ' : static_cast<unsigned char>(header[i]);
			usum += ch;
			ssum += static_cast<signed char>(ch);
		}
		if (checksum != qint64(usum) && checksum != qint64(ssum))
			return sn_fail(err, "The tar header checksum does not match");
		const char type = header[156];
		if (type == 'g' || type == 'x' || type == 'L') {
			if (size > 1048576) return sn_fail(err, "The tar metadata is too large");
			QByteArray metadata(int(size), 0);
			if (!input.get(metadata.data(), metadata.size())) return false;
			if (type != 'g') pending = true;
			if (type == 'L') {
				longName = sn_field(metadata.constData(), metadata.size());
				if (longName.isEmpty()) return sn_fail(err, "Invalid tar long filename");
			} else if (!sn_pax(metadata, type == 'g' ? &global : &local, type == 'g'))
				return sn_fail(err, "Invalid or unsupported tar extended header");
		} else {
			if (type != 0 && type != '0' && type != '5')
				return sn_fail(err, "Unsupported tar entry: links and special files are not allowed");
			QByteArray name = longName.isEmpty() ? sn_field(header, 100) : longName;
			if (longName.isEmpty() && !memcmp(header + 257, "ustar\0", 6)) {
				QByteArray prefix = sn_field(header + 345, 155);
				if (!prefix.isEmpty()) name = prefix + "/" + name;
			}
			const QByteArray paxPath = local.value("path", global.value("path"));
			if (!paxPath.isEmpty()) name = paxPath;
			const QByteArray paxSize = local.value("size", global.value("size"));
			if (!paxSize.isEmpty()) {
				if (!sn_decimal(paxSize, &size))
					return sn_fail(err, "Invalid tar extended size");
			}
			local.clear();
			longName.clear();
			pending = false;
			QString relative, path;
			if (!sn_entry_path(name, &relative) || (relative == "." && type != '5'))
				return sn_fail(err, "Unsafe path in tar archive: " + QString::fromUtf8(name));
			if (type == '5') {
				if (size || !QDir().mkpath(root.filePath(relative)))
					return sn_fail(err, "Cannot create archive folder " + relative);
			} else {
				if (!sn_take_path(root, relative, err, &path)) return false;
				QFile output(path);
				if (!output.open(QFile::WriteOnly))
					return sn_fail(err, "Cannot create archive file " + relative);
				qint64 left = size;
				while (left) {
					const int count = int(qMin<qint64>(left, sizeof(data)));
					if (!input.get(data, count)) return false;
					if (output.write(data, count) != count)
						return sn_fail(err, "Cannot write archive file " + relative + ": " + output.errorString());
					left -= count;
				}
				if (!output.flush())
					return sn_fail(err, "Cannot write archive file " + relative + ": " + output.errorString());
			}
		}
		const int padding = int((512 - size % 512) % 512);
		if (!input.get(data, padding)) return false;
	}
}

/* zip
 *
 * The central directory at the end lists every entry, each naming the local header its
 * data follows. Entries are stored or deflated and carry a CRC32 and a size to check
 * the result against. The data offset comes from the local header, whose own extra
 * field is not the central directory's.
 */

#define SN_ZIP_LOCAL	0x04034b50
#define SN_ZIP_CENTRAL	0x02014b50
#define SN_ZIP_END	0x06054b50
#define SN_ZIP_LOCAL_LEN	30
#define SN_ZIP_CENTRAL_LEN	46
#define SN_ZIP_END_LEN	22

static quint16 sn_le16(const char* data) {
	return quint16(static_cast<unsigned char>(data[0]))
		| quint16(static_cast<unsigned char>(data[1])) << 8;
}

static quint32 sn_le32(const char* data) {
	return quint32(static_cast<unsigned char>(data[0]))
		| quint32(static_cast<unsigned char>(data[1])) << 8
		| quint32(static_cast<unsigned char>(data[2])) << 16
		| quint32(static_cast<unsigned char>(data[3])) << 24;
}

struct snZipEntry {
	QByteArray name;	// as the central directory spells it
	QString path;
	qint64 local;		// where its local header is
	qint64 packed;		// bytes in the archive
	qint64 unpacked;
	quint32 crc;
	int method;
	bool folder;
};

// one entry's bytes, stored or deflated, checked against the size and the CRC32 written
static bool sn_zip_data(QFile* file, const snZipEntry& entry, qint64 at, QFile* out, QString* err,
	qint64* done, qint64 total, const std::function<bool(qint64, qint64)>& tick) {
	if (!file->seek(at)) return sn_fail(err, "The ZIP archive is truncated");
	quint32 crc = crc32(0, Z_NULL, 0);
	qint64 left = entry.packed, written = 0;
	unsigned char in[65536], produced[65536];
	// what comes out is written and counted the same way, stored or deflated
	auto put = [&](const unsigned char* bytes, int count) -> bool {
		if (out->write(reinterpret_cast<const char*>(bytes), count) != count)
			return sn_fail(err, "Cannot write archive file " + entry.path);
		crc = crc32(crc, bytes, uint(count));
		written += count;
		if (!tick) return true;
		*done += count;
		return tick(qMin(*done, total), total) || sn_fail(err, "Unpacking cancelled");
	};
	if (entry.method == 0) {
		while (left) {
			const int count = int(qMin<qint64>(left, sizeof(in)));
			if (file->read(reinterpret_cast<char*>(in), count) != count)
				return sn_fail(err, "The ZIP archive is truncated");
			left -= count;
			if (!put(in, count)) return false;
		}
	} else {
		z_stream zs;
		memset(&zs, 0, sizeof(zs));
		if (inflateInit2(&zs, -MAX_WBITS) != Z_OK)
			return sn_fail(err, "Cannot start the decompressor");
		bool ok = true;
		for (;;) {
			if (!zs.avail_in && left) {
				const int count = int(qMin<qint64>(left, sizeof(in)));
				if (file->read(reinterpret_cast<char*>(in), count) != count) {
					ok = sn_fail(err, "The ZIP archive is truncated");
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
				ok = sn_fail(err, "The deflate stream in the ZIP archive is damaged");
				break;
			}
			// neither output nor input used: it cannot go on, and must not loop
			if (!count && (zs.avail_in == before)) {
				ok = sn_fail(err, left ? "The deflate stream in the ZIP archive is damaged"
					: "The deflate stream in the ZIP archive is truncated");
				break;
			}
		}
		inflateEnd(&zs);
		if (!ok) return false;
	}
	if (written != entry.unpacked)
		return sn_fail(err, "The ZIP entry " + entry.path + " does not unpack to the size it states");
	if (crc != entry.crc)
		return sn_fail(err, "The ZIP entry " + entry.path + " does not match its checksum");
	return true;
}

static bool sn_unpack_zip(const QString& archive, const QString& dir, QString* err,
	std::function<bool(qint64, qint64)> tick) {
	QFile file(archive);
	if (!file.open(QFile::ReadOnly))
		return sn_fail(err, file.errorString());
	const qint64 size = file.size();
	// the end record is the last 22 bytes, or that plus a comment of up to 65535
	const int tail = int(qMin<qint64>(size, 65557));
	if (tail < SN_ZIP_END_LEN) return sn_fail(err, "This is not a ZIP archive");
	QByteArray end(tail, 0);
	if (!file.seek(size - tail) || file.read(end.data(), tail) != tail)
		return sn_fail(err, "This is not a ZIP archive");
	int at = tail - SN_ZIP_END_LEN;
	while (at >= 0 && sn_le32(end.constData() + at) != SN_ZIP_END) at--;
	if (at < 0) return sn_fail(err, "This is not a ZIP archive");
	const int count = sn_le16(end.constData() + at + 10);
	const quint32 table = sn_le32(end.constData() + at + 16);
	if (sn_le16(end.constData() + at + 4) || sn_le16(end.constData() + at + 6))
		return sn_fail(err, "A ZIP archive on more than one disk is not supported");
	if (quint64(sn_le32(end.constData() + at + 12)) + table > quint64(size))
		return sn_fail(err, "The ZIP archive is truncated");
	QVector<snZipEntry> entries;
	QMap<QString, bool> seen;
	qint64 total = 0;
	if (!file.seek(table)) return sn_fail(err, "The ZIP archive is truncated");
	for (int i = 0; i < count; i++) {
		char header[SN_ZIP_CENTRAL_LEN];
		if (file.read(header, sizeof(header)) != sizeof(header))
			return sn_fail(err, "The ZIP archive is truncated");
		if (sn_le32(header) != SN_ZIP_CENTRAL)
			return sn_fail(err, "The ZIP archive has a damaged central directory");
		snZipEntry entry;
		const int nameLen = sn_le16(header + 28);
		const int extraLen = sn_le16(header + 30);
		const int commentLen = sn_le16(header + 32);
		entry.method = sn_le16(header + 10);
		entry.crc = sn_le32(header + 16);
		entry.packed = sn_le32(header + 20);
		entry.unpacked = sn_le32(header + 24);
		entry.local = sn_le32(header + 42);
		entry.name = QByteArray(nameLen, 0);
		if (file.read(entry.name.data(), nameLen) != nameLen
			|| !file.seek(file.pos() + extraLen + commentLen))
			return sn_fail(err, "The ZIP archive is truncated");
		if (sn_le16(header + 8) & 1) return sn_fail(err, "An encrypted ZIP archive is not supported");
		if (entry.packed == 0xffffffff || entry.unpacked == 0xffffffff || entry.local == 0xffffffff)
			return sn_fail(err, "A ZIP64 archive is not supported");
		if (entry.method != 0 && entry.method != 8)
			return sn_fail(err, "A ZIP archive packed with method " + QString::number(entry.method) + " is not supported");
		entry.folder = entry.name.endsWith('/');
		const QByteArray bare = entry.folder ? entry.name.left(entry.name.size() - 1) : entry.name;
		if (!sn_entry_path(bare, &entry.path))
			return sn_fail(err, "Unsafe path in ZIP archive: " + QString::fromUtf8(bare));
		if (seen.contains(entry.path)) return sn_fail(err, "The archive holds " + entry.path + " twice");
		seen.insert(entry.path, true);
		if (entry.local + SN_ZIP_LOCAL_LEN > size)
			return sn_fail(err, "The ZIP archive is truncated");
		if (!entry.folder) total += entry.unpacked;
		entries.append(entry);
	}
	QDir root;
	if (!sn_destination(dir, err, &root)) return false;
	qint64 done = 0;
	for (const snZipEntry& entry : entries) {
		char header[SN_ZIP_LOCAL_LEN];
		if (!file.seek(entry.local) || file.read(header, sizeof(header)) != sizeof(header))
			return sn_fail(err, "The ZIP archive is truncated");
		if (sn_le32(header) != SN_ZIP_LOCAL)
			return sn_fail(err, "The ZIP archive has no local header for " + entry.path);
		if (sn_le16(header + 6) & 1) return sn_fail(err, "An encrypted ZIP archive is not supported");
		const int nameLen = sn_le16(header + 26);
		const int extraLen = sn_le16(header + 28);
		QByteArray name(nameLen, 0);
		if (file.read(name.data(), nameLen) != nameLen)
			return sn_fail(err, "The ZIP archive is truncated");
		if (name != entry.name)
			return sn_fail(err, "The ZIP archive names " + entry.path + " two different ways");
		const qint64 at = entry.local + SN_ZIP_LOCAL_LEN + nameLen + extraLen;
		if (at + entry.packed > size) return sn_fail(err, "The ZIP archive is truncated");
		if (entry.folder) {
			if (entry.unpacked || !QDir().mkpath(root.filePath(entry.path)))
				return sn_fail(err, "Cannot create archive folder " + entry.path);
			continue;
		}
		QString path;
		if (!sn_take_path(root, entry.path, err, &path)) return false;
		QFile out(path);
		if (!out.open(QFile::WriteOnly))
			return sn_fail(err, "Cannot create archive file " + entry.path);
		if (!sn_zip_data(&file, entry, at, &out, err, &done, total, tick)) return false;
		if (!out.flush())
			return sn_fail(err, "Cannot write archive file " + entry.path + ": " + out.errorString());
	}
	return true;
}

bool sn_unpack_archive(const QString& archive, const QString& dir, QString* err,
	std::function<bool(qint64, qint64)> tick) {
	err->clear();
	if (archive.endsWith(".tar.gz", Qt::CaseInsensitive))
		return sn_unpack_tar_gz(archive, dir, err, tick);
	if (archive.endsWith(".zip", Qt::CaseInsensitive))
		return sn_unpack_zip(archive, dir, err, tick);
	return sn_fail(err, "This archive is neither .tar.gz nor .zip");
}
