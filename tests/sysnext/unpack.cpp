#include "../../src/xcore/sysnext.h"
#include "../../src/xcore/archive.h"

#include <QCoreApplication>
#include <cstdio>

// The media readers: --list <archive> prints "size<TAB>name" per entry, a folder's name
// ending in "/"; --extract <archive> <entry> <to> unpacks one. Errors go to stderr, exit 1.
static int media(const QStringList& args) {
	QString error;
	bool ok = false;
	if ((args[1] == "--list") && (args.size() == 3)) {
		QVector<arcEntry> list;
		ok = arc_list(args[2], &list, &error);
		for (const arcEntry& entry : list)
			std::printf("%lld\t%s%s\n", static_cast<long long>(entry.size),
				entry.name.toUtf8().constData(), entry.folder ? "/" : "");
	} else if ((args[1] == "--extract") && (args.size() == 5)) {
		ok = arc_extract(args[2], args[3], args[4], &error);
	} else {
		return 2;
	}
	if (ok) return 0;
	std::fprintf(stderr, "%s\n", error.toUtf8().constData());
	return error.isEmpty() ? 2 : 1;
}

// unpack.cpp <archive> <dir> [cancel at callback N]
// The archive's own name picks the reader: .tar.gz or .zip, as the app does.
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	const QStringList args = app.arguments();
	if (args.size() < 3) return 2;
	if (args[1].startsWith("--")) return media(args);
	const int cancel = args.value(3).toInt();
	int calls = 0;
	qint64 previous = 0, total = 0;
	bool validProgress = true;
	QString error;
	const bool ok = sn_unpack_archive(args[1], args[2], &error, [&](qint64 done, qint64 size) {
		validProgress = validProgress && done >= previous && done <= size && size > 0;
		previous = done;
		total = size;
		calls++;
		return validProgress && calls != cancel;
	});
	if (!validProgress) return 2;
	if (!ok) {
		std::fprintf(stderr, "%s\n", error.toUtf8().constData());
		return error.isEmpty() ? 2 : 1;
	}
	if (!calls || previous != total) return 2;
	std::printf("PASS %i callbacks, %lld bytes\n", calls, static_cast<long long>(total));
	return 0;
}
