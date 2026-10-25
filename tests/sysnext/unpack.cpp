#include "../../src/xcore/sysnext.h"

#include <QCoreApplication>
#include <cstdio>

// unpack.cpp <archive>
// The archive's own name picks the reader: .tar.gz or .zip, as the app does.
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	const QStringList args = app.arguments();
	if (args.size() < 3) return 2;
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
