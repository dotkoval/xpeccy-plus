#include "sysnext.h"
#include "archive.h"

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
	std::function<bool(qint64, qint64)> tick, std::function<QString(const QString&)> pick) {
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
			// only the files the caller picks, where it says
			bool skip = false;
			if (pick) {
				const QString to = (type == '5') ? QString() : pick(relative);
				skip = to.isEmpty();
				if (!skip && !sn_entry_path(to.toUtf8(), &relative))
					return sn_fail(err, "Unsafe path picked from tar archive: " + to);
			}
			if (skip) {
				for (qint64 left = size; left; ) {
					const int count = int(qMin<qint64>(left, sizeof(data)));
					if (!input.get(data, count)) return false;
					left -= count;
				}
			} else if (type == '5') {
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
 * Read by the shared zip reader (archive.cpp); the card adds its own rules on top:
 * names in UTF-8 that FAT can hold, each once, nothing outside the folder.
 */

static bool sn_unpack_zip(const QString& archive, const QString& dir, QString* err,
	std::function<bool(qint64, qint64)> tick, std::function<QString(const QString&)> pick) {
	QFile file(archive);
	if (!file.open(QFile::ReadOnly))
		return sn_fail(err, file.errorString());
	QVector<zipEntry> listed;
	if (!zip_dir(&file, &listed, err)) return false;
	QVector<zipEntry> entries;
	QVector<QString> paths;
	QMap<QString, bool> seen;
	qint64 total = 0;
	for (const zipEntry& entry : listed) {
		const QByteArray bare = entry.folder ? entry.name.left(entry.name.size() - 1) : entry.name;
		QString path;
		if (!sn_entry_path(bare, &path))
			return sn_fail(err, "Unsafe path in ZIP archive: " + QString::fromUtf8(bare));
		if (seen.contains(path)) return sn_fail(err, "The archive holds " + path + " twice");
		seen.insert(path, true);
		// only the files the caller picks, where it says
		if (pick) {
			if (entry.folder) continue;
			const QString to = pick(path);
			if (to.isEmpty()) continue;
			if (!sn_entry_path(to.toUtf8(), &path))
				return sn_fail(err, "Unsafe path picked from ZIP archive: " + to);
		}
		if (!entry.folder) total += entry.unpacked;
		entries.append(entry);
		paths.append(path);
	}
	QDir root;
	if (!sn_destination(dir, err, &root)) return false;
	qint64 done = 0;
	auto progress = [&](int count) {
		if (!tick) return true;
		done += count;
		return tick(qMin(done, total), total);
	};
	for (int i = 0; i < entries.size(); i++) {
		const zipEntry& entry = entries[i];
		qint64 at;
		if (!zip_data_at(&file, entry, &at, err)) return false;
		if (entry.folder) {
			if (entry.unpacked || !QDir().mkpath(root.filePath(paths[i])))
				return sn_fail(err, "Cannot create archive folder " + paths[i]);
			continue;
		}
		QString path;
		if (!sn_take_path(root, paths[i], err, &path)) return false;
		QFile out(path);
		if (!out.open(QFile::WriteOnly))
			return sn_fail(err, "Cannot create archive file " + paths[i]);
		if (!zip_data(&file, entry, at, &out, err, progress)) return false;
		if (!out.flush())
			return sn_fail(err, "Cannot write archive file " + paths[i] + ": " + out.errorString());
	}
	return true;
}

bool sn_unpack_archive(const QString& archive, const QString& dir, QString* err,
	std::function<bool(qint64, qint64)> tick, std::function<QString(const QString&)> pick) {
	err->clear();
	if (archive.endsWith(".tar.gz", Qt::CaseInsensitive))
		return sn_unpack_tar_gz(archive, dir, err, tick, pick);
	if (archive.endsWith(".zip", Qt::CaseInsensitive))
		return sn_unpack_zip(archive, dir, err, tick, pick);
	return sn_fail(err, "This archive is neither .tar.gz nor .zip");
}
