// The ZX Spectrum Next's own SD card: made from a System/Next release - an archive
// of it beside the program, else downloaded from GitLab with the system's curl -
// as a folder or an image, or one the user has. A release known here is checked
// before it is used; one that is not says so.

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <functional>

#include "xgui.h"
#include "../xcore/xcore.h"
#include "../xcore/sysnext.h"

enum {NC_FOLDER = 0, NC_IMAGE, NC_HAVE};

class xNextCard : public QDialog {
	public:
		xNextCard(QWidget* p);
		QString path;		// the card, once there is one
		bool own;		// made here
		void reject() override;
	private:
		QComboBox* cbKind;
		QLineEdit* lePath;
		QPushButton* btnPath;
		QComboBox* cbSize;
		QComboBox* cbRel;
		QLabel* labFrom;
		QLabel* lab;
		QLabel* labLine;	// what the program says, as it goes
		QProgressBar* bar;
		QDialogButtonBox* box;
		QPushButton* btnGo;
		QList<snRelease> rels;
		QProcess* prc;		// the step running
		QProcess* tagPrc;	// the tag list, looked up as the window opens
		bool running;
		QString tail;		// the program's last line, what a failure shows
		QString tmp;		// <config>/sysnext.part/: the download and what it unpacks to
		QString archive;	// the archive in use
		QString target;		// the card being made
		snRelease rel;
		bool treeKnown;		// the archive's own checksum matched: the tree is the known one
		bool dropArchive;	// a downloaded archive not checked yet: a failure takes it away
		QString pathFor[2];	// what the path field held for a new folder and a new image
		int kindWas;
		void kindChanged();
		void formEnabled(bool on);
		void fillReleases();
		void showFrom();
		void browse();
		void go();
		void verify();
		void unpack();
		void place();
		void run(const QString& prog, const QStringList& args, std::function<void()> next);
		void output(const QByteArray& chunk);
		void fail(const QString& msg);
		bool tick();
};

#ifdef _WIN32
// the system's own, not one a shell put first on PATH
static QString nc_tool(const QString& name) {
	QString path = qEnvironmentVariable("SystemRoot") + "/System32/" + name + ".exe";
	return QFileInfo(path).isFile() ? path : QStandardPaths::findExecutable(name);
}
#else
static QString nc_tool(const QString& name) {
	return QStandardPaths::findExecutable(name);
}
#endif

// for a card on another disk than the config folder, where a rename cannot go
static bool nc_copy_tree(const QString& src, const QString& dst, std::function<bool()> tick) {
	if (!QDir().mkpath(dst)) return false;
	QDirIterator it(src, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		QString from = it.next();
		QString to = dst + "/" + QDir(src).relativeFilePath(from);
		if (it.fileInfo().isDir()) {
			if (!QDir().mkpath(to)) return false;
		} else if (!QFile::copy(from, to) || !tick()) {
			return false;
		}
	}
	return true;
}

static QString nc_size_name(qint64 bytes) {
	return QString("%0 MB").arg((bytes + (1 << 19)) >> 20);
}

xNextCard::xNextCard(QWidget* p):QDialog(p) {
	setWindowTitle("ZX Spectrum Next SD Card");
	own = false;
	running = false;
	prc = nullptr;
	tagPrc = nullptr;
	treeKnown = false;
	dropArchive = false;
	kindWas = NC_FOLDER;
	pathFor[NC_FOLDER] = sn_default_path(false);
	pathFor[NC_IMAGE] = sn_default_path(true);
	QVBoxLayout* lay = new QVBoxLayout(this);
	QLabel* intro = new QLabel(QString("The Next starts from an SD card with System/Next on it: its ROMs, NextZXOS and "
		"the programs that come with it. It is not part of Xpeccy+. A card is made here from the official "
		"release, unchanged - an archive of it beside the program, or downloaded from "
		"<a href=\"%0\">%1</a>.").arg(sn_repo_url()).arg(QString(sn_repo_url()).remove("https://")));
	intro->setWordWrap(true);
	intro->setTextInteractionFlags(Qt::TextBrowserInteraction);
	intro->setOpenExternalLinks(true);
	intro->setMinimumWidth(460);
	lay->addWidget(intro);
	xOptSheet sheet;
	cbKind = new QComboBox;
	cbKind->addItem("New folder", NC_FOLDER);
	cbKind->addItem("New image", NC_IMAGE);
	cbKind->addItem("A card I have", NC_HAVE);
	sheet.field("Card", cbKind);
	QWidget* pw = new QWidget;
	QHBoxLayout* pl = new QHBoxLayout(pw);
	pl->setContentsMargins(0, 0, 0, 0);
	lePath = new QLineEdit;
	lePath->setMinimumWidth(280);
	btnPath = new QPushButton("...");
	btnPath->setFixedWidth(btnPath->fontMetrics().height() * 2 + 8);
	pl->addWidget(lePath, 1);
	pl->addWidget(btnPath);
	sheet.field("Path", pw);
	cbSize = new QComboBox;
	for (int gb : {1, 2, 4, 8, 16})
		cbSize->addItem(QString("%0 GB").arg(gb), (qint64)gb << 30);
	cbSize->setCurrentIndex(2);
	sheet.field("Size", cbSize, "On the disk it takes only what is written to it");
	cbRel = new QComboBox;
	sheet.field("Release", cbRel);
	labFrom = new QLabel;
	labFrom->setWordWrap(true);
	sheet.field("From", labFrom);
	lay->addWidget(sheet.body);
	lab = new QLabel;
	lab->setWordWrap(true);
	lab->hide();
	lay->addWidget(lab);
	bar = new QProgressBar;
	bar->setTextVisible(false);
	bar->hide();
	lay->addWidget(bar);
	labLine = new QLabel;
	QFont fnt = labLine->font();
	fnt.setItalic(true);
	labLine->setFont(fnt);
	labLine->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	labLine->hide();
	lay->addWidget(labLine);
	box = new QDialogButtonBox(QDialogButtonBox::Cancel);
	btnGo = box->addButton("Create", QDialogButtonBox::AcceptRole);
	btnGo->setDefault(true);
	btnGo->setIcon(QIcon(":/images/ok-apply.png"));
	box->button(QDialogButtonBox::Cancel)->setIcon(QIcon(":/images/cancel.png"));
	lay->addWidget(box);
	// named for the control port's set-value and click-widget
	cbKind->setObjectName("nextCardKind");
	lePath->setObjectName("nextCardPath");
	cbSize->setObjectName("nextCardSize");
	cbRel->setObjectName("nextCardRelease");
	btnGo->setObjectName("nextCardGo");
	connect(box, &QDialogButtonBox::rejected, this, &xNextCard::reject);
	connect(btnGo, &QPushButton::clicked, this, &xNextCard::go);
	connect(btnPath, &QPushButton::clicked, this, &xNextCard::browse);
	connect(cbKind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {kindChanged();});
	connect(cbRel, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {showFrom();});
	rels = sn_releases();
	fillReleases();
	lePath->setText(pathFor[NC_FOLDER]);
	kindChanged();
	// releases newer than the ones known here, if GitLab can be reached; quietly not otherwise
	QString curl = nc_tool("curl");
	if (!curl.isEmpty()) {
		tagPrc = new QProcess(this);
		connect(tagPrc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code, QProcess::ExitStatus st) {
			if ((st != QProcess::NormalExit) || code) return;
			QString was = cbRel->currentData().toString();
			sn_add_tags(&rels, tagPrc->readAllStandardOutput());
			fillReleases();
			int idx = cbRel->findData(was);
			if (idx >= 0) cbRel->setCurrentIndex(idx);
		});
		tagPrc->start(curl, QStringList() << "-fsSL" << "--max-time" << "15" << sn_tags_url());
	}
}

void xNextCard::fillReleases() {
	QSignalBlocker blk(cbRel);
	cbRel->clear();
	for (const snRelease& r : rels)
		cbRel->addItem(QString("System/Next %0%1").arg(r.release).arg(r.checked() ? "" : " (not checked)"), r.release);
	// the newest one known here, unless an archive of another is beside the program
	int pick = -1;
	for (int i = 0; i < rels.size(); i++) {
		if (!rels[i].local.isEmpty() && (pick < 0)) pick = i;
	}
	for (int i = 0; (pick < 0) && (i < rels.size()); i++) {
		if (rels[i].checked()) pick = i;
	}
	cbRel->setCurrentIndex(qMax(pick, 0));
	showFrom();
}

void xNextCard::showFrom() {
	int idx = cbRel->currentIndex();
	if ((idx < 0) || (idx >= rels.size())) {
		labFrom->clear();
		return;
	}
	const snRelease& r = rels[idx];
	QString from = !r.local.isEmpty()
		? QString(r.cached ? "%0, downloaded before" : "%0, beside the program").arg(QFileInfo(r.local).fileName())
		: QString("gitlab.com%0").arg(r.bytes ? QString(", about %0").arg(nc_size_name(r.bytes)) : QString());
	if (!r.checked()) from += "<br><i>No checksum of this release is known here: it is taken as it comes.</i>";
	labFrom->setText(from);
}

void xNextCard::kindChanged() {
	int kind = cbKind->currentData().toInt();
	if (kindWas != NC_HAVE) pathFor[kindWas] = lePath->text();
	lePath->setText((kind == NC_HAVE) ? QString() : pathFor[kind]);
	lePath->setPlaceholderText((kind == NC_HAVE) ? "A folder or an image with System/Next on it" : QString());
	cbSize->setEnabled(kind == NC_IMAGE);
	cbRel->setEnabled(kind != NC_HAVE);
	labFrom->setEnabled(kind != NC_HAVE);
	btnGo->setText((kind == NC_HAVE) ? "Use" : "Create");
	kindWas = kind;
}

// the form, while a card is being made: its size and release follow the kind again after
void xNextCard::formEnabled(bool on) {
	QWidget* form[] = {cbKind, lePath, btnPath, cbSize, cbRel, btnGo};
	for (QWidget* w : form) w->setEnabled(on);
}

void xNextCard::browse() {
	int kind = cbKind->currentData().toInt();
	QString got;
	QString at = lePath->text();
	if (kind == NC_FOLDER) {
		got = QFileDialog::getExistingDirectory(this, "Folder for the card", QFileInfo(at).path(), QFileDialog::DontUseNativeDialog | QFileDialog::ShowDirsOnly);
		// a folder picked is where the card's own folder goes, unless it is empty
		if (!got.isEmpty() && !QDir(got).isEmpty()) got = QDir(got).filePath("next-sd");
	} else if (kind == NC_IMAGE) {
		got = QFileDialog::getSaveFileName(this, "Image for the card", at, "Images (*.img);;All files (*)", NULL, QFileDialog::DontUseNativeDialog);
	} else {
		QMenu menu(this);
		QAction* dir = menu.addAction("Folder...");
		QAction* img = menu.addAction("Image...");
		QAction* act = menu.exec(btnPath->mapToGlobal(QPoint(0, btnPath->height())));
		if (act == dir) got = QFileDialog::getExistingDirectory(this, "The card's folder", at, QFileDialog::DontUseNativeDialog | QFileDialog::ShowDirsOnly);
		if (act == img) got = QFileDialog::getOpenFileName(this, "The card's image", at, "All files (*)", NULL, QFileDialog::DontUseNativeDialog);
	}
	if (!got.isEmpty()) lePath->setText(QDir::toNativeSeparators(got));
}

bool xNextCard::tick() {
	QApplication::processEvents();
	return running;
}

void xNextCard::run(const QString& prog, const QStringList& args, std::function<void()> next) {
	if (prog.isEmpty()) {
		fail("Downloading needs curl, and there is none on this machine");
		return;
	}
	if (prc) prc->deleteLater();
	prc = new QProcess(this);
	tail.clear();
	connect(prc, &QProcess::readyReadStandardOutput, this, [this]() {output(prc->readAllStandardOutput());});
	connect(prc, &QProcess::readyReadStandardError, this, [this]() {output(prc->readAllStandardError());});
	connect(prc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, next](int code, QProcess::ExitStatus st) {
		if (!running) return;		// cancelled
		if ((st != QProcess::NormalExit) || (code != 0)) {
			fail(tail.isEmpty() ? QString("It stopped with code %0").arg(code) : tail);
		} else {
			next();
		}
	});
	connect(prc, &QProcess::errorOccurred, this, [this, prog](QProcess::ProcessError err) {
		if (running && (err == QProcess::FailedToStart))
			fail(QString("%0 does not start").arg(QFileInfo(prog).fileName()));
	});
	prc->start(prog, args);
}

// a percentage moves the bar, any other line is kept for a failure to show
void xNextCard::output(const QByteArray& chunk) {
	static QRegularExpression pct("(\\d+(?:\\.\\d+)?)%\\s*$");
	foreach(QString line, QString::fromLocal8Bit(chunk).split(QRegularExpression("[\r\n]"), X_SkipEmptyParts)) {
		static QRegularExpression word("[A-Za-z0-9]");
		line = line.trimmed();
		if (!line.contains(word)) continue;		// curl's bar before it has a figure
		QRegularExpressionMatch m = pct.match(line);
		if (m.hasMatch()) {
			bar->setRange(0, 1000);
			bar->setValue(int(m.captured(1).toDouble() * 10));
		} else {
			tail = line;
			labLine->setText(line);
		}
	}
}

void xNextCard::go() {
	int kind = cbKind->currentData().toInt();
	target = QDir::cleanPath(QDir::fromNativeSeparators(lePath->text().trimmed()));
	lab->show();
	if (target.isEmpty()) {
		lab->setText("Say where the card is.");
		return;
	}
	if (kind == NC_HAVE) {
		if (!sn_card_ok(target)) {
			lab->setText("There is no System/Next on this card: the Next cannot start from it.");
			return;
		}
		path = QDir::toNativeSeparators(target);
		own = false;
		accept();
		return;
	}
	QFileInfo inf(target);
	if ((kind == NC_FOLDER) && inf.exists() && !(inf.isDir() && QDir(target).isEmpty())) {
		lab->setText("The folder for a new card has to be new or empty.");
		return;
	}
	if ((kind == NC_IMAGE) && inf.exists()) {
		lab->setText("There is a file of this name already.");
		return;
	}
	if (!QDir().mkpath(inf.path())) {
		lab->setText("Cannot make the folder this goes into.");
		return;
	}
	int idx = cbRel->currentIndex();
	if ((idx < 0) || (idx >= rels.size())) return;
	rel = rels[idx];
	running = true;
	treeKnown = false;
	formEnabled(false);
	bar->setRange(0, 0);
	bar->show();
	labLine->clear();
	labLine->show();
	tmp = QString::fromLocal8Bit(conf.path.confDir.c_str()) + "/sysnext.part/";
	QDir(tmp).removeRecursively();
	if (!QDir().mkpath(tmp + "x")) {
		fail("Cannot write into the config folder");
		return;
	}
	if (!rel.local.isEmpty()) {
		archive = rel.local;
		dropArchive = rel.cached;
		verify();
		return;
	}
	// kept in the config folder for the next card; under another name until it is whole
	QDir().mkpath(sn_cache_dir());
	archive = sn_cache_dir() + sn_archive_name(rel);
	QString part = archive + ".part";
	QFile::remove(part);
	lab->setText(QString("Downloading System/Next %0").arg(rel.release));
	run(nc_tool("curl"), QStringList() << "-fL" << "--progress-bar" << "-o" << QDir::toNativeSeparators(part) << sn_archive_url(rel),
		[this, part]() {
			QFile::remove(archive);
			if (!QFile::rename(part, archive)) {
				QFile::remove(part);
				fail("Cannot write into the config folder");
				return;
			}
			dropArchive = true;
			verify();
		});
}

// the archive's own checksum, when one is known: a match settles it. GitLab builds the
// archive on request, so a mismatch is only settled by the tree after unpacking.
void xNextCard::verify() {
	if (!rel.archiveSha.isEmpty() && archive.endsWith(".tar.gz")) {
		lab->setText("Checking the archive");
		bar->setRange(0, 0);
		QString sum = sn_file_hash(archive, [this]() {return tick();});
		if (!running) {
			QDir(tmp).removeRecursively();
			return;
		}
		treeKnown = (sum == rel.archiveSha);
	}
	unpack();
}

void xNextCard::unpack() {
	lab->setText("Unpacking");
	bar->setRange(0, 1000);
	QString err;
	bool ok = sn_unpack_archive(archive, tmp + "x", &err, [this](qint64 done, qint64 total) {
		bar->setValue(total ? int(1000.0 * done / total) : 0);
		return tick();
	});
	if (!running) {
		QDir(tmp).removeRecursively();
		return;
	}
	if (!ok) {
		fail(err);
		return;
	}
	place();
}

void xNextCard::place() {
	QString root = sn_unpacked_root(tmp + "x");
	if (!QFileInfo(root + "/machines/next/enNextZX.rom").isFile()) {
		fail("This archive is not a System/Next release");
		return;
	}
	if (treeKnown || !rel.checked()) dropArchive = false;
	if (!treeKnown && rel.checked()) {
		lab->setText("Checking the files");
		bar->setRange(0, 0);
		QString sum = sn_tree_hash(root, [this]() {return tick();});
		if (!running) {
			QDir(tmp).removeRecursively();
			return;
		}
		if (sum != rel.treeSha) {
			fail(QString("The files are not those of System/Next %0: the archive is damaged or changed").arg(rel.release));
			return;
		}
		dropArchive = false;
	}
	if (cbKind->currentData().toInt() == NC_FOLDER) {
		lab->setText("Putting the card in place");
		if (QFileInfo(target).isDir()) QDir().rmdir(target);	// checked empty
		if (!QDir().rename(root, target) && !nc_copy_tree(root, target, [this]() {return tick();})) {
			if (running) QDir(target).removeRecursively();		// the half copied; a cancel has it already
			if (running) fail("Cannot put the files in " + QDir::toNativeSeparators(target));
			return;
		}
	} else {
		lab->setText("Making the image");
		bar->setRange(0, 100);
		QString part = target + ".part~";
		QString err;
		bool ok = sn_make_image(root, part, cbSize->currentData().toLongLong(), &err, [this](int pct) {
			bar->setValue(pct);
			return tick();
		});
		if (!running) {
			QDir(tmp).removeRecursively();
			return;
		}
		if (!ok || !QFile::rename(part, target)) {
			QFile::remove(part);
			fail(ok ? QString("Cannot write ") + QDir::toNativeSeparators(target) : err);
			return;
		}
	}
	QDir(tmp).removeRecursively();
	running = false;
	path = QDir::toNativeSeparators(target);
	own = true;
	accept();
}

void xNextCard::fail(const QString& msg) {
	running = false;
	if (!tmp.isEmpty()) QDir(tmp).removeRecursively();
	QString more;
	if (dropArchive) {		// downloaded and bad: the next try downloads it again
		QFile::remove(archive);
		dropArchive = false;
		more = "<br><br>The downloaded archive was taken away: Create downloads it again.";
		rels = sn_releases();
		fillReleases();
	}
	bar->hide();
	labLine->hide();
	lab->setText("No card was made.<br><br>" + msg.toHtmlEscaped() + more);
	formEnabled(true);
	kindChanged();
}

// cancel: whatever runs is stopped and what it left is taken away
void xNextCard::reject() {
	bool was = running;
	running = false;
	QProcess* procs[] = {prc, tagPrc};
	for (QProcess* p : procs) {
		if (p && (p->state() != QProcess::NotRunning)) {
			p->kill();
			p->waitForFinished(2000);
		}
	}
	if (was && !tmp.isEmpty()) QDir(tmp).removeRecursively();
	QDialog::reject();
}

bool next_card_dialog(QWidget* parent, QString* path, bool* own) {
	xNextCard dlg(parent);
	if (dlg.exec() != QDialog::Accepted) return false;
	*path = dlg.path;
	*own = dlg.own;
	return true;
}
