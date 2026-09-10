#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>

#include "xcore.h"
#include "../filer.h"
#include "../xgui/xgui.h"
#include "autostart.h"
#include "vfat_scan.h"
#include "vscalers.h"

#include <fstream>
#include <algorithm>

// Machines: what a machine is, and the one that is running.
//
// A definition says what a machine is - one file per machine, named by its id,
// read from the binary's own resources and from machines/ in the config
// directory, where a file of the same id shadows the built-in one. The running
// machine is built from a definition plus whatever the user changed on it, and
// the last part of this file brings a pre-machines profile across.
// See docs/machines-plan.md for the format.

#define	MAC_RES_DIR	":/res/machines"
#define	MAC_USER_DIR	"machines"
#define	MAC_SUFFIX	".conf"

static QList<xMachine> macList;

// one key of one file, kept as read so inherit can be resolved before any of
// it is applied

typedef struct {
	std::string sect;
	std::string name;
	std::string val;
} xMacLine;

typedef struct {
	QList<xMacLine> lines;
} xMacFile;

static QMap<QString, xMacFile> macSrc;

// value vocabularies

typedef struct {
	const char* name;
	int val;
} xMacWord;

static xMacWord psgTypeTab[] = {
	{"none", SND_NONE}, {"ay", SND_AY}, {"ym", SND_YM}, {"ym2203", SND_YM2203}, {NULL, 0}
};

static xMacWord sdrvTab[] = {
	{"none", SDRV_NONE}, {"covox", SDRV_COVOX},
	{"soundrive1", SDRV_105_1}, {"soundrive2", SDRV_105_2}, {NULL, 0}
};

static xMacWord diskTab[] = {
	{"none", DIF_NONE}, {"trdos", DIF_BDI}, {"plus3", DIF_P3DOS}, {NULL, 0}
};

static xMacWord ideTab[] = {
	{"none", IDE_NONE}, {"nemo", IDE_NEMO}, {"nemo-a8", IDE_NEMOA8},
	{"nemo-evo", IDE_NEMO_EVO}, {"smuc", IDE_SMUC}, {"atm", IDE_ATM},
	{"profi", IDE_PROFI}, {NULL, 0}
};

static xMacWord resetTab[] = {
	{"basic128", RES_128}, {"basic48", RES_48},
	{"dos", RES_DOS}, {"shadow", RES_SHADOW}, {NULL, 0}
};

static int mac_word(xMacWord* tab, const std::string& val, int def, const char* id) {
	for (int i = 0; tab[i].name; i++) {
		if (!strcmp(tab[i].name, val.c_str())) return tab[i].val;
	}
	xlog(XLG_CONF, XLL_WARN, "machine %s: unknown value '%s'", id, val.c_str());
	return def;
}

// file

static QList<xMacLine> mac_read(const QString& path) {
	QList<xMacLine> res;
	QFile file(path);		// QFile, not ifstream: the built-in ones are resources
	if (!file.open(QFile::ReadOnly | QFile::Text)) {
		xlog(XLG_CONF, XLL_ERROR, "can't read machine %s", path.toLocal8Bit().data());
		return res;
	}
	QTextStream stream(&file);
	std::string sect;
	xMacLine ln;
	while (!stream.atEnd()) {
		QString line = stream.readLine().section('#', 0, 0).section(';', 0, 0).trimmed();
		if (line.isEmpty()) continue;
		if (line.startsWith('[')) {
			sect = line.mid(1, line.indexOf(']') - 1).trimmed().toLower().toLocal8Bit().data();
			continue;
		}
		if (!line.contains('=')) continue;
		std::pair<std::string,std::string> spl = splitline(line.toLocal8Bit().data());
		ln.sect = sect;
		ln.name = spl.first;
		ln.val = spl.second;
		res << ln;
	}
	return res;
}

// rom

// file[:foffset[:fsize]], both in KB, 0 size = as far as the file reaches

// an empty file name empties the bank, which is how a child, a variant or the
// user says "there is nothing here" over a set that has something

static void mac_rom_add(xMachineRoms* set, const std::string& val, int bank) {
	std::vector<std::string> part = splitstr(val, ":");
	if (part.empty() || part[0].empty()) {
		for (int i = 0; i < set->roms.size(); i++) {
			if (set->roms[i].roffset == bank * 16) {
				set->roms.removeAt(i);
				return;
			}
		}
		return;
	}
	xRomFile rom;
	rom.name = part[0];
	rom.foffset = (part.size() > 1) ? atoi(part[1].c_str()) : 0;
	rom.fsize = (part.size() > 2) ? atoi(part[2].c_str()) : 0;
	rom.roffset = bank * 16;
	for (int i = 0; i < set->roms.size(); i++) {
		if (set->roms[i].roffset == rom.roffset) {
			set->roms[i] = rom;	// a child names the banks it changes
			return;
		}
	}
	set->roms << rom;
}

static xMachineRoms* mac_rom_set(xMachine& mac, const std::string& sect) {
	std::string id = (sect.size() > 4) ? sect.substr(4) : "";
	for (int i = 0; i < mac.roms.size(); i++) {
		if (mac.roms[i].id == id) return &mac.roms[i];
	}
	xMachineRoms set;
	set.id = id;
	mac.roms << set;
	return &mac.roms.last();
}

// build

static void mac_defaults(xMachine& mac) {
	mac.memory = 128;
	mac.cpu = "Z80";
	mac.cpufrq = 3500000;
	mac.resbank = RES_128;
	mac.contio = 0;
	mac.contmem = 0;
	mac.scrpwait = 0;
	mac.contPattern = 0;
	mac.early = 0;
	mac.brd4t = 0;
	mac.psgCount = 1;
	mac.psgType = SND_AY;
	mac.soundrive = SDRV_NONE;
	mac.disk = DIF_NONE;
	mac.ide = IDE_NONE;
	mac.mouse = 0;
	mac.joyButtons = 0;
	mac.gs = 0;
	mac.saa = 0;
	mac.ulaplus = 0;
	mac.ddpal = 0;
}

static void mac_apply(xMachine& mac, const QList<xMacLine>& lines) {
	const char* id = mac.id.c_str();
	xArg arg;
	foreach(xMacLine ln, lines) {
		const std::string& nam = ln.name;
		const std::string& val = ln.val;
		arg.s = val.c_str();
		arg.b = str2bool(val) ? 1 : 0;
		arg.i = strtol(arg.s, NULL, 0);
		if (ln.sect == "machine") {
			if (nam == "name") mac.name = val;
			else if (nam == "family") mac.family = val;
			else if (nam == "hw") mac.hw = val;
			else if (nam == "cpu") mac.cpu = val;
			else if (nam == "memory") mac.memory = arg.i;
			else if (nam == "cpu.frq") mac.cpufrq = arg.i;
			else if (nam == "reset") mac.resbank = mac_word(resetTab, val, RES_128, id);
			else if (nam == "contio") mac.contio = arg.b;
			else if (nam == "contmem") mac.contmem = arg.b;
			else if (nam == "scrp.wait") mac.scrpwait = arg.b;
		} else if (ln.sect == "video") {
			if (nam == "geometry") mac.geometry = val;
			else if (nam == "contPattern") mac.contPattern = arg.i;
			else if (nam == "earlyTiming") mac.early = arg.b;
			else if (nam == "4t-border") mac.brd4t = arg.b;
			else if (nam == "ULAplus") mac.ulaplus = arg.b;
			else if (nam == "DDpal") mac.ddpal = arg.b;
		} else if (ln.sect == "sound") {
			if (nam == "psg.count") mac.psgCount = toLimits(arg.i, 0, 3);
			else if (nam == "psg.type") mac.psgType = mac_word(psgTypeTab, val, SND_AY, id);
			else if (nam == "soundrive") mac.soundrive = mac_word(sdrvTab, val, SDRV_NONE, id);
			else if (nam == "gs") mac.gs = arg.b;
			else if (nam == "saa") mac.saa = arg.b;
		} else if (ln.sect == "storage") {
			if (nam == "disk") mac.disk = mac_word(diskTab, val, DIF_NONE, id);
			else if (nam == "ide") mac.ide = mac_word(ideTab, val, IDE_NONE, id);
		} else if (ln.sect == "input") {
			if (nam == "mouse") mac.mouse = arg.b;
			else if (nam == "joy.buttons") mac.joyButtons = arg.b;
		} else if (ln.sect.compare(0, 3, "rom") == 0) {
			xMachineRoms* set = mac_rom_set(mac, ln.sect);
			if (nam == "name") set->name = val;
			else if (nam == "gs") set->gsFile = val;
			else if (nam == "font") set->fntFile = val;
			else if ((nam.compare(0, 3, "rom") == 0) && isdigit(nam[3]))
				mac_rom_add(set, val, atoi(nam.c_str() + 3));
			else
				xlog(XLG_CONF, XLL_WARN, "machine %s: unknown rom key '%s'", id, nam.c_str());
		}
	}
}

static xMachine mac_build(const QString& id, int depth) {
	xMachine mac;
	QList<xMacLine> lines = macSrc.value(id).lines;
	QString parent;
	foreach(xMacLine ln, lines) {
		if ((ln.sect == "machine") && (ln.name == "inherit"))
			parent = QString::fromLocal8Bit(ln.val.c_str());
	}
	if (parent.isEmpty()) {
		mac_defaults(mac);
	} else if (!macSrc.contains(parent) || (depth > 8)) {
		xlog(XLG_CONF, XLL_ERROR, "machine %s: can't inherit %s",
			id.toLocal8Bit().data(), parent.toLocal8Bit().data());
		mac_defaults(mac);
	} else {
		// the parent's own rom set comes along, its variants do not
		mac = mac_build(parent, depth + 1);
		for (int i = mac.roms.size() - 1; i >= 0; i--) {
			if (!mac.roms[i].id.empty()) mac.roms.removeAt(i);
		}
	}
	mac.id = id.toLocal8Bit().data();
	mac_apply(mac, lines);
	if (mac.name.empty()) mac.name = mac.id;
	return mac;
}

// load

static void mac_scan(const QString& dir) {
	QDir qdir(dir);
	if (!qdir.exists()) return;
	xMacFile mf;
	foreach(QString name, qdir.entryList(QStringList() << ("*" MAC_SUFFIX), QDir::Files)) {
		mf.lines = mac_read(qdir.filePath(name));
		macSrc[QFileInfo(name).completeBaseName()] = mf;
	}
}

// the machine list reads in the order the cores are in, which phase 1 put in
// lineage order - the Sinclair machines, then the clones that came from them

extern "C" tabHwItem tabHwPtr[];

static int mac_core_order(const std::string& hw) {
	for (int i = 0; tabHwPtr[i].id != HW_NULL; i++) {
		if (tabHwPtr[i].core && (hw == tabHwPtr[i].core->name)) return i;
	}
	return 9999;
}

static bool mac_before(const xMachine& a, const xMachine& b) {
	int oa = mac_core_order(a.hw);
	int ob = mac_core_order(b.hw);
	return (oa != ob) ? (oa < ob) : (a.id < b.id);
}

void xm_load_all() {
	macList.clear();
	macSrc.clear();
	mac_scan(MAC_RES_DIR);
	mac_scan(QString::fromLocal8Bit(conf.path.confDir.c_str()) + SLASH MAC_USER_DIR);
	foreach(QString id, macSrc.keys()) {
		macList << mac_build(id, 0);
	}
	std::sort(macList.begin(), macList.end(), mac_before);
	macSrc.clear();		// only inherit needs the files, and it is done
	xlog(XLG_CONF, XLL_INFO, "%i machines", macList.size());
}

const QList<xMachine>& xm_list() {
	return macList;
}

const xMachine* xm_find(std::string id) {
	for (int i = 0; i < macList.size(); i++) {
		if (macList[i].id == id) return &macList[i];
	}
	return NULL;
}

// the first machine built on that core. More than one machine can share a
// core - a 48K with a Beta Disk is still a ZX48 - so this answers "what does
// this core usually come as", not "which machine is running".

const xMachine* xm_find_by_core(std::string hw) {
	for (int i = 0; i < macList.size(); i++) {
		if (macList[i].hw == hw) return &macList[i];
	}
	return NULL;
}

// id "" is the machine's own set

const xMachineRoms* xm_find_roms(const xMachine* mac, std::string id) {
	if (!mac) return NULL;
	for (int i = 0; i < mac->roms.size(); i++) {
		if (mac->roms[i].id == id) return &mac->roms[i];
	}
	return NULL;
}


#define	PS_NONE		0
#define	PS_MACHINE	1
#define	PS_ROMSET	2
#define	PS_VIDEO	3
#define	PS_SOUND	4
#define	PS_INPUT	5
#define	PS_TAPE		6
#define	PS_DISK		7
#define	PS_IDE		8
#define	PS_SDC		9
#define	PS_SLOT		10
#define	PS_DEBUGA	11

// ------------------------------------------------------------- the machine

// One machine per process. It is built from its definition, then whatever the
// user changed is applied on top - so a fix shipped in an update reaches a
// machine the user has been tweaking. What the user changed is kept per
// machine id, which is what makes switching away and back remember it, and
// what keeps a machine we never touch from losing what it had.

typedef QList<QPair<std::string, std::string> > xMacOver;
static QMap<QString, xMacOver> macOver;

void xm_over_clear() {
	macOver.clear();
}

void xm_over_add(const std::string& id, const std::string& nam, const std::string& val) {
	macOver[QString::fromLocal8Bit(id.c_str())] << qMakePair(nam, val);
}

// what a machine keeps for itself, in nvram/<id>.*

static std::string mac_nv_path(const char* ext) {
	return conf.path.nvDir + SLASH + conf.macId + ext;
}

static void mac_nv_read(const std::string& path, unsigned char* dst, int size, const char* seed) {
	FILE* file = fopen(path.c_str(), "rb");
	if (!file && seed) {			// first run of this machine: give it its own
		copyFile(seed, path.c_str());
		file = fopen(path.c_str(), "rb");
	}
	if (!file) return;
	if (fread(dst, size, 1, file) != 1)
		xlog(XLG_CONF, XLL_WARN, "short read from %s", path.c_str());
	fclose(file);
}

static void mac_nv_write(const std::string& path, unsigned char* src, int size) {
	FILE* file = fopen(path.c_str(), "wb");
	if (!file) return;
	fwrite(src, size, 1, file);
	fclose(file);
}

void xm_load_nvram() {
	std::string seed = std::string(":/res/nvram/") + conf.macId + ".cmos";
	bool have = QFile::exists(QString::fromLocal8Bit(seed.c_str()));
	mac_nv_read(mac_nv_path(".cmos"), conf.zx->cmos.data, 256, have ? seed.c_str() : NULL);
	mac_nv_read(mac_nv_path(".nvram"), conf.zx->ide->smuc.nv->mem, 256, NULL);
}

void xm_save_nvram() {
	if (conf.macId.empty()) return;
	mac_nv_write(mac_nv_path(".cmos"), conf.zx->cmos.data, 256);
	if (conf.zx->ide->type == IDE_SMUC)
		mac_nv_write(mac_nv_path(".nvram"), conf.zx->ide->smuc.nv->mem, 256);
}

// romset

// An empty name is the machine's own ROMs, out of its definition; a name is
// one of the sets in the config file, which is what a machine carries after
// the migration and what the romset editor makes.

static void mac_load_rom(Computer* comp, const QList<xRomFile>& roms, const std::string& gsf, const std::string& fntf) {
	std::string fpath;
	int romsz = MEM_256;
	int fsze;
	FILE* file;
	memset(comp->mem->romData, 0xff, MEM_512K);
	foreach(xRomFile xrf, roms) {
		int foff = xrf.foffset * 1024;
		int roff = xrf.roffset * 1024;
		fpath = conf.path.romDir + SLASH + xrf.name;
		file = fopen(fpath.c_str(), "rb");
		if (!file) {
			xlog(XLG_CONF, XLL_ERROR, "can't load rom file '%s'", fpath.c_str());
			continue;
		}
		if (xrf.fsize <= 0) {			// no size given: as far as the file reaches
			fseek(file, 0, SEEK_END);
			fsze = ftell(file);
			rewind(file);
		} else {
			fsze = xrf.fsize * 1024;
		}
		if (roff + fsze > romsz) {
			romsz = toPower(toLimits(roff + fsze, MEM_256, MEM_512K));
		}
		if (roff + fsze > romsz)
			fsze = romsz - roff;
		if ((foff >= 0) && (roff >= 0) && (roff < MEM_512K) && (fsze > 0)) {
			fseek(file, foff, SEEK_SET);
			if (fread(comp->mem->romData + roff, fsze, 1, file) != 1)
				xlog(XLG_CONF, XLL_WARN, "short read from '%s'", fpath.c_str());
		}
		fclose(file);
	}
	// a machine whose roms are all missing still needs a rom space to page
	if (romsz < MEM_16K) romsz = MEM_16K;
	memSetSize(comp->mem, -1, romsz);
	comp_heat_sync(comp);
	if (gsf.empty()) {
		memset((char*)comp->gs->mem->romData, 0xff, MEM_32K);
	} else {
		fpath = conf.path.romDir + SLASH + gsf;
		file = fopen(fpath.c_str(), "rb");
		if (file) {
			if (fread(comp->gs->mem->romData, MEM_32K, 1, file) != 1)
				xlog(XLG_CONF, XLL_WARN, "short read from '%s'", fpath.c_str());
			fclose(file);
		} else {
			xlog(XLG_CONF, XLL_ERROR, "can't load gs rom '%s'", fpath.c_str());
			memset((char*)comp->gs->mem->romData, 0xff, MEM_32K);
		}
	}
	if (fntf.empty()) {
		vid_fnt_del(comp->vid);
	} else {
		fpath = conf.path.romDir + SLASH + fntf;
		vid_fnt_load(comp->vid, fpath.c_str());
	}
}

// The roms of a machine before anything of the user's: its own set with the
// chosen variant laid over it.

static xRomset mac_roms_of(const xMachine* mac, const std::string& variant) {
	xRomset res;
	res.name = variant;
	const xMachineRoms* set = xm_find_roms(mac, "");
	if (set) {
		res.roms = set->roms;
		res.gsFile = set->gsFile;
		res.fntFile = set->fntFile;
	}
	const xMachineRoms* var = variant.empty() ? NULL : xm_find_roms(mac, variant);
	if (var) {
		if (!var->gsFile.empty()) res.gsFile = var->gsFile;
		if (!var->fntFile.empty()) res.fntFile = var->fntFile;
		foreach(xRomFile rf, var->roms) {
			int i = 0;
			while ((i < res.roms.size()) && (res.roms[i].roffset != rf.roffset)) i++;
			if (i < res.roms.size()) res.roms[i] = rf;
			else res.roms << rf;
		}
	}
	return res;
}

// what one of the machine's sets holds, without loading it

xRomset xm_roms_of(std::string variant) {
	return mac_roms_of(xm_find(conf.macId), variant);
}

QList<QString> xm_rom_variants() {
	QList<QString> res;
	const xMachine* mac = xm_find(conf.macId);
	if (!mac) return res;
	foreach(xMachineRoms set, mac->roms) {
		if (!set.id.empty()) res << QString::fromLocal8Bit(set.id.c_str());
	}
	return res;
}

// A file the user named for this machine, kept as read until the roms are
// built - the block is read before the machine is up.

static QList<QPair<std::string, std::string> > macRomOver;

void xm_rom_over_clear() {
	macRomOver.clear();
}

void xm_rom_over_add(const std::string& nam, const std::string& val) {
	macRomOver << qMakePair(nam, val);
}

static void mac_rom_over_apply(xRomset& rs) {
	xMachineRoms tmp;
	tmp.roms = rs.roms;
	foreach(xMacOver::value_type kv, macRomOver) {
		if (kv.first == "gs") rs.gsFile = kv.second;
		else if (kv.first == "font") rs.fntFile = kv.second;
		else mac_rom_add(&tmp, kv.second, atoi(kv.first.c_str() + 3));
	}
	rs.roms = tmp.roms;
	macRomOver.clear();
}

void xm_set_romset(std::string variant) {
	if (!conf.zx) return;
	conf.romSet = variant;
	xRomset rs = mac_roms_of(xm_find(conf.macId), variant);
	mac_rom_over_apply(rs);
	xm_set_roms(rs);
}

// what conf.roms says, into the machine

void xm_set_roms(const xRomset& rs) {
	if (!conf.zx) return;
	emu_lock();				// rom data is rewritten under the running machine
	conf.roms = rs;
	Computer* comp = conf.zx;
	memset(comp->vid->bios, 0xff, MEM_64K);
	comp->vid->vga.cga = 1;
	tsSetRomSize(comp->ts, 0);
	mac_load_rom(comp, rs.roms, rs.gsFile, rs.fntFile);
	emu_unlock();
}

static void mac_put(QStringList& out, const char* nam, const std::string& val, const std::string& def) {
	if (val != def) out << QString("%1 = %2").arg(nam).arg(QString::fromLocal8Bit(val.c_str()));
}

static void mac_put(QStringList& out, const char* nam, int val, int def) {
	if (val != def) out << QString("%1 = %2").arg(nam).arg(val);
}

static void mac_put_yn(QStringList& out, const char* nam, int val, int def) {
	if (!val != !def) out << QString("%1 = %2").arg(nam).arg(YESNO(val));
}

// the files that are the user's own, as keys of the machine's block

static void mac_put_roms(QStringList& out) {
	xRomset def = mac_roms_of(xm_find(conf.macId), conf.romSet);
	foreach(xRomFile rf, conf.roms.roms) {
		int i = 0;
		while ((i < def.roms.size()) && (def.roms[i].roffset != rf.roffset)) i++;
		bool same = (i < def.roms.size()) && (def.roms[i].name == rf.name)
			&& (def.roms[i].foffset == rf.foffset) && (def.roms[i].fsize == rf.fsize);
		if (same) continue;
		QString val = QString::fromLocal8Bit(rf.name.c_str());
		if (rf.foffset || rf.fsize)
			val += QString(":%1:%2").arg(rf.foffset).arg(rf.fsize);
		out << QString("rom%1 = %2").arg(rf.roffset / 16).arg(val);
	}
	foreach(xRomFile rf, def.roms) {		// a bank the user emptied
		int i = 0;
		while ((i < conf.roms.roms.size()) && (conf.roms.roms[i].roffset != rf.roffset)) i++;
		if (i >= conf.roms.roms.size())
			out << QString("rom%1 = ").arg(rf.roffset / 16);
	}
	mac_put(out, "gs", conf.roms.gsFile, def.gsFile);
	mac_put(out, "font", conf.roms.fntFile, def.fntFile);
}

// layout

bool xm_set_layout(std::string nm) {
	xLayout* lay = findLayout(nm);
	if (lay == NULL) return false;
	conf.layName = nm;
	comp_set_layout(conf.zx, &lay->lay);
	vid_set_border(conf.zx->vid, brd_mode_for(conf.zx, conf.vid.border));
	if ((conf.zx->vid->res.x > 0) && (conf.zx->vid->res.y > 0))
		vid_set_resolution(conf.zx->vid, conf.zx->vid->res.x, conf.zx->vid->res.y);
	return true;
}

// core

// Core names as they were spelled before the machine list was cleaned up. Only
// a config written by an older build carries one; a definition names the new
// name. The other half of the compatibility story, old profile name -> machine
// id, is the migration in config.cpp.

static const struct {
	const char* oldName;
	const char* newName;
} hwAliasTab[] = {
	{"ZX48K",	"ZX48"},
	{"Spectrum +2",	"Plus2A"},	// that core has always been a +2A
	{"Spectrum +3",	"Plus3"},
	{"PentEvo",	"Baseconf"},
	{"PentEvo21",	"Baseconf21"},
	{"TSLab",	"TSConf"},	// TS-Labs is the group, not the machine
	{NULL, NULL}
};

int xm_set_hardware(std::string nm) {
	for (int i = 0; hwAliasTab[i].oldName; i++) {
		if (nm == hwAliasTab[i].oldName) {
			nm = hwAliasTab[i].newName;
			break;
		}
	}
	return compSetHardware(conf.zx, nm.empty() ? NULL : nm.c_str());
}

// the machine's own settings: first its definition, then the user's own block.
// Both go through here, so what a key means is written once.

static int mac_ram_size(int kb, Computer* comp) {
	int sz = kb ? kb : 64;
	sz = toLimits(toPower(sz << 10), MEM_256, MEM_4M);
	if ((comp->hw->mask != 0) && (~comp->hw->mask & sz)) {	// the core has no such size
		sz = MEM_4M;
		while (!(comp->hw->mask & sz) && sz)
			sz >>= 1;
	}
	return sz;
}

static int mac_psg_count(Computer* comp) {
	if (comp->ts->chipA->type == SND_NONE) return 0;
	return (comp->ts->type == TS_ZXNEXT) ? 3 : (comp->ts->type == TS_NEDOPC) ? 2 : 1;
}

static void mac_set_psg(Computer* comp, int count, int type) {
	aymChip* psg[3] = {comp->ts->chipA, comp->ts->chipB, comp->ts->chipC};
	for (int i = 0; i < 3; i++)
		chip_set_type(psg[i], (i < count) ? type : SND_NONE);
	comp->ts->type = (count > 2) ? TS_ZXNEXT : (count > 1) ? TS_NEDOPC : TS_NONE;
}

static void mac_set_cpu(Computer* comp, const std::string& val) {
	std::pair<std::string,std::string> spl = splitline(val, '@');	// NAME@LIBRARY
	if (spl.second.empty()) {
		cpu_set_type(comp->cpu, spl.first.c_str(), NULL, NULL);
	} else {
		std::string dir = conf.path.plgDir + SLASH + "cpu";
		cpu_set_type(comp->cpu, spl.first.c_str(), dir.c_str(), spl.second.c_str());
	}
}

static void mac_from_def(const xMachine* mac) {
	Computer* comp = conf.zx;
	conf.macName = mac->name;
	xm_set_hardware(mac->hw);
	mac_set_cpu(comp, mac->cpu);
	compSetBaseFrq(comp, mac->cpufrq / 1e6);
	memSetSize(comp->mem, mac_ram_size(mac->memory, comp), -1);
	comp->resbank = mac->resbank;
	comp->flgCNTI = mac->contio;
	comp->flgCNTM = mac->contmem;
	comp->flgEM1 = mac->scrpwait;
	comp->flgDDP = mac->ddpal;
	comp->vid->ula->conttype = mac->contPattern;
	comp->vid->ula->early = mac->early;
	comp->vid->ula->enabled = mac->ulaplus;
	comp->vid->brdstep = mac->brd4t ? 7 : 1;
	mac_set_psg(comp, mac->psgCount, mac->psgType);
	comp->gs->enable = mac->gs;
	comp->saa->enabled = mac->saa;
	comp->sdrv->type = mac->soundrive;
	difSetHW(comp->dif, mac->disk);
	ide_set_type(comp->ide, mac->ide);
	comp->mouse->enable = mac->mouse;
	comp->joy->extbuttons = mac->joyButtons;
	conf.layName = mac->geometry;
	conf.romSet.clear();
}

// one key of the user's own block

static void mac_set_key(const std::string& nam, const std::string& val) {
	Computer* comp = conf.zx;
	const char* id = conf.macId.c_str();
	xArg arg;
	arg.s = val.c_str();
	arg.b = str2bool(val) ? 1 : 0;
	arg.i = strtol(arg.s, NULL, 0);
	if (nam == "hw") xm_set_hardware(val);
	else if (nam == "cpu") mac_set_cpu(comp, val);
	else if (nam == "cpu.frq") compSetBaseFrq(comp, toLimits(arg.i, 100000, 28000000) / 1e6);
	else if (nam == "memory") memSetSize(comp->mem, mac_ram_size(arg.i, comp), -1);
	else if (nam == "reset") comp->resbank = mac_word(resetTab, val, RES_128, id);
	else if (nam == "contio") comp->flgCNTI = arg.b;
	else if (nam == "contmem") comp->flgCNTM = arg.b;
	else if (nam == "scrp.wait") comp->flgEM1 = arg.b;
	else if (nam == "geometry") conf.layName = val;
	else if (nam == "contPattern") comp->vid->ula->conttype = arg.i;
	else if (nam == "earlyTiming") comp->vid->ula->early = arg.b;
	else if (nam == "4t-border") comp->vid->brdstep = arg.b ? 7 : 1;
	else if (nam == "ULAplus") comp->vid->ula->enabled = arg.b;
	else if (nam == "DDpal") comp->flgDDP = arg.b;
	else if (nam == "psg.count") mac_set_psg(comp, toLimits(arg.i, 0, 3), comp->ts->chipA->type);
	else if (nam == "psg.type") mac_set_psg(comp, mac_psg_count(comp), mac_word(psgTypeTab, val, SND_AY, id));
	else if (nam == "gs") comp->gs->enable = arg.b;
	else if (nam == "saa") comp->saa->enabled = arg.b;
	else if (nam == "soundrive") comp->sdrv->type = mac_word(sdrvTab, val, SDRV_NONE, id);
	else if (nam == "disk") difSetHW(comp->dif, mac_word(diskTab, val, DIF_NONE, id));
	else if (nam == "ide") ide_set_type(comp->ide, mac_word(ideTab, val, IDE_NONE, id));
	else if (nam == "mouse") comp->mouse->enable = arg.b;
	else if (nam == "joy.buttons") comp->joy->extbuttons = arg.b;
	else if (nam == "romset") conf.romSet = val;
	else if ((nam == "gs") || (nam == "font")
		|| ((nam.compare(0, 3, "rom") == 0) && isdigit(nam[3]))) xm_rom_over_add(nam, val);
	else xlog(XLG_CONF, XLL_WARN, "machine %s: unknown setting '%s'", id, nam.c_str());
}

bool xm_set(std::string id) {
	const xMachine* mac = xm_find(id);
	if (!mac) {
		xlog(XLG_CONF, XLL_ERROR, "no such machine: %s", id.c_str());
		return false;
	}
	emu_lock();
	conf.emu.pause |= PR_EXTRA;
	if (!conf.macId.empty()) {			// what the machine we leave keeps
		xm_save_nvram();
		ideCloseFiles(conf.zx->ide);
		sdcCloseFile(conf.zx->sdc);
	}
	conf.macId = id;
	xm_rom_over_clear();
	mac_from_def(mac);
	foreach(xMacOver::value_type kv, macOver.value(QString::fromLocal8Bit(id.c_str())))
		mac_set_key(kv.first, kv.second);
	xm_set_romset(conf.romSet);
	if (!xm_set_layout(conf.layName)) xm_set_layout("default");
	loadPalette();
	xm_load_nvram();
	comp_kbd_release(conf.zx);
	mouseReleaseAll(conf.zx->mouse);
	compReset(conf.zx, RES_DEFAULT);
	conf.emu.pause &= ~PR_EXTRA;
	emu_unlock();
	xlog(XLG_CONF, XLL_INFO, "machine: %s (%s, romset %s)", conf.macId.c_str(),
		conf.zx->hw->name, conf.romSet.empty() ? "own" : conf.romSet.c_str());
	return true;
}

// what to write back: only what differs from the definition, so a machine the
// user never touched carries nothing and takes every fix an update brings.

static const char* mac_word_name(xMacWord* tab, int val) {
	for (int i = 0; tab[i].name; i++) {
		if (tab[i].val == val) return tab[i].name;
	}
	return "none";
}

// is this named romset the machine's own set under another name?

static bool mac_same_roms(const xRomset* rs, const xRomset* set) {
	if (!rs || !set) return false;
	if ((rs->gsFile != set->gsFile) || (rs->fntFile != set->fntFile)) return false;
	if (!rs->vBiosFile.empty() || !rs->sBiosFile.empty()) return false;
	if (rs->roms.size() != set->roms.size()) return false;
	for (int i = 0; i < rs->roms.size(); i++) {
		if ((rs->roms[i].name != set->roms[i].name)
			|| (rs->roms[i].roffset != set->roms[i].roffset)
			|| (rs->roms[i].foffset != set->roms[i].foffset)) return false;
	}
	return true;
}

// everything about the machine in use that differs from what it ships with

static void mac_put_all(QStringList& out) {
	const xMachine* mac = xm_find(conf.macId);
	if (!mac || !conf.zx) return;
	Computer* comp = conf.zx;
	std::string cpu = comp->cpu->core->name;
	if (comp->cpu->lib) cpu += std::string("@") + comp->cpu->libname;
	mac_put(out, "hw", comp->hw->name, mac->hw);
	mac_put(out, "cpu", cpu, mac->cpu);
	mac_put(out, "cpu.frq", int(comp->cpuFrq * 1e6), mac->cpufrq);
	mac_put(out, "memory", comp->mem->ramSize >> 10, mac->memory);
	mac_put(out, "reset", mac_word_name(resetTab, comp->resbank), mac_word_name(resetTab, mac->resbank));
	mac_put_yn(out, "contio", comp->flgCNTI, mac->contio);
	mac_put_yn(out, "contmem", comp->flgCNTM, mac->contmem);
	mac_put_yn(out, "scrp.wait", comp->flgEM1, mac->scrpwait);
	mac_put(out, "geometry", conf.layName, mac->geometry);
	mac_put(out, "contPattern", comp->vid->ula->conttype, mac->contPattern);
	mac_put_yn(out, "earlyTiming", comp->vid->ula->early, mac->early);
	mac_put_yn(out, "4t-border", comp->vid->brdstep & 0x06, mac->brd4t);
	mac_put_yn(out, "ULAplus", comp->vid->ula->enabled, mac->ulaplus);
	mac_put_yn(out, "DDpal", comp->flgDDP, mac->ddpal);
	mac_put(out, "psg.count", mac_psg_count(comp), mac->psgCount);
	if (mac_psg_count(comp) > 0)		// with no chips there is no type to keep
		mac_put(out, "psg.type", mac_word_name(psgTypeTab, comp->ts->chipA->type), mac_word_name(psgTypeTab, mac->psgType));
	mac_put_yn(out, "gs", comp->gs->enable, mac->gs);
	mac_put_yn(out, "saa", comp->saa->enabled, mac->saa);
	mac_put(out, "soundrive", mac_word_name(sdrvTab, comp->sdrv->type), mac_word_name(sdrvTab, mac->soundrive));
	mac_put(out, "disk", mac_word_name(diskTab, comp->dif->type), mac_word_name(diskTab, mac->disk));
	mac_put(out, "ide", mac_word_name(ideTab, comp->ide->type), mac_word_name(ideTab, mac->ide));
	mac_put_yn(out, "mouse", comp->mouse->enable, mac->mouse);
	mac_put_yn(out, "joy.buttons", comp->joy->extbuttons, mac->joyButtons);
	mac_put(out, "romset", conf.romSet, std::string());
	mac_put_roms(out);
}

// the settings that differ from what the machine ships with - the ui marks
// them, and "machine defaults" throws them away

QStringList xm_over_keys() {
	QStringList out;
	mac_put_all(out);
	QStringList res;
	foreach(QString line, out)
		res << line.section('=', 0, 0).trimmed();
	return res;
}

void xm_reset_over() {
	macOver.remove(QString::fromLocal8Bit(conf.macId.c_str()));
	std::string id = conf.macId;
	conf.macId.clear();			// so xm_set does not save what we are dropping
	xm_set(id);
}

void xm_save(FILE* file) {
	if (conf.macId.empty() || !conf.zx) return;
	QStringList out;
	mac_put_all(out);
	if (!out.isEmpty()) {
		fprintf(file, "\n[MACHINE.%s]\n\n", conf.macId.c_str());
		fprintf(file, "%s\n", out.join("\n").toLocal8Bit().data());
	}

	// and the machines the user set up but is not running now
	foreach(QString id, macOver.keys()) {
		if (id == QString::fromLocal8Bit(conf.macId.c_str())) continue;
		if (macOver.value(id).isEmpty()) continue;
		fprintf(file, "\n[MACHINE.%s]\n\n", id.toLocal8Bit().data());
		foreach(xMacOver::value_type kv, macOver.value(id))
			fprintf(file, "%s = %s\n", kv.first.c_str(), kv.second.c_str());
	}
}

// --------------------------------------------------------------- the media

// What is mounted stays with the application, not with the machine: switching
// machines keeps the tape and the disks, and only what the new machine cannot
// take goes quiet.

std::string getDiskString(Floppy* flp) {
	std::string res = "40SW";
	if (flp->trk80) res[0] = '8';
	if (flp->doubleSide) res[2] = 'D';
	if (flp->protect) res[3] = 'R';
	if (flp->path) {
		res += ':';
		res += std::string(flp->path);
	}
	return res;
}

static void mac_set_disk(Floppy* flp, std::string st) {
	if (st.size() < 4) return;
	flp->trk80 = (st.substr(0, 2) == "80") ? 1 : 0;
	flp->doubleSide = (st.substr(2, 1) == "D") ? 1 : 0;
	flp->protect = (st.substr(3, 1) == "R") ? 1 : 0;
	if (flp->path || (st.size() < 5) || !conf.storePaths) return;
	st = st.substr(5);
	if (st.size() > 1)
		flp_insert(flp, st.c_str());		// the image itself is loaded once the machine is up
}

// Settings that live on the machine but belong to the user, not to the model:
// what is mounted, and the preferences the machine happens to hold. They are
// collected while the config file is read and applied once the machine is up,
// so building the machine cannot wipe them.

static QList<QPair<std::string, std::string> > macDefer;

void xm_defer(const std::string& nam, const std::string& val) {
	macDefer << qMakePair(nam, val);
}

static void mac_set_defer_key(const std::string& nam, const std::string& val) {
	Computer* comp = conf.zx;
	xArg arg;
	arg.s = val.c_str();
	arg.b = str2bool(val) ? 1 : 0;
	arg.i = strtol(arg.s, NULL, 0);
	arg.d = strtod(arg.s, NULL);
	if (nam == "tape") {
		if (conf.storePaths) tape_set_path(comp->tape, val.c_str());
	} else if ((nam.size() == 6) && (nam.compare(0, 5, "disk.") == 0)) {
		int drv = nam[5] - 'A';
		if ((drv >= 0) && (drv < 4)) mac_set_disk(comp->dif->flp[drv], val);
	} else if (nam == "hdd.master.type") comp->ide->master->type = arg.i;
	else if (nam == "hdd.master.lba") comp->ide->master->hasLBA = arg.b;
	else if (nam == "hdd.master") ide_mount(comp->ide, IDE_MASTER, QString::fromLocal8Bit(arg.s));
	else if (nam == "hdd.slave.type") comp->ide->slave->type = arg.i;
	else if (nam == "hdd.slave.lba") comp->ide->slave->hasLBA = arg.b;
	else if (nam == "hdd.slave") ide_mount(comp->ide, IDE_SLAVE, QString::fromLocal8Bit(arg.s));
	else if (nam == "sdcard") sdc_mount(comp->sdc, QString::fromLocal8Bit(arg.s));
	else if (nam == "sdcard.lock") sdcSetLock(comp->sdc, arg.b);
	else if (nam == "cartrige.type") comp->slot->mapType = arg.i;
	else if (nam == "cartrige") {
		if (conf.storePaths) sltSetPath(comp->slot, arg.s);
	}
	else if (nam == "frq.mul") compSetTurbo(comp, (arg.d < 0.1) ? 0.1 : (arg.d > 8.0) ? 8.0 : arg.d);
	else if (nam == "tape.speed") { if ((arg.i > 94) && (arg.i < 106)) comp->tape->speed = arg.i; }
	else if (nam == "psg.frq") {
		aymChip* psg[3] = {comp->ts->chipA, comp->ts->chipB, comp->ts->chipC};
		for (int i = 0; i < 3; i++) {
			psg[i]->frq = arg.d;
			chip_set_type(psg[i], psg[i]->type);	// the period follows the clock
		}
	}
	else if (nam == "psg.stereo") {
		comp->ts->chipA->stereo = arg.i;
		comp->ts->chipB->stereo = arg.i;
		comp->ts->chipC->stereo = arg.i;
	}
	else if (nam == "psg.separation") {
		comp->ts->chipA->sep = arg.i;
		comp->ts->chipB->sep = arg.i;
		comp->ts->chipC->sep = arg.i;
	}
	else if (nam == "gs.reset") comp->gs->reset = arg.b;
	else if (nam == "gs.stereo") comp->gs->stereo = arg.b ? GS_12_34 : GS_MONO;
	else if (nam == "mouse.wheel") comp->mouse->hasWheel = arg.b;
	else if (nam == "mouse.swapButtons") comp->mouse->swapButtons = arg.b;
	else if (nam == "mouse.sensitivity") comp->mouse->sensitivity = arg.d;
	else if (nam == "mouse.pctype") comp->mouse->pcmode = arg.i;
	else if (nam == "kbd.scantab") comp->keyb->pcmode = arg.i;
	else if (nam == "ports") setWatchPorts(comp, QString::fromLocal8Bit(arg.s).split(","));
}

// the images themselves, once the machine is built

void xm_finish_load() {
	foreach(xMacOver::value_type kv, macDefer)
		mac_set_defer_key(kv.first, kv.second);
	macDefer.clear();
	if (!conf.storePaths) return;
	Computer* comp = conf.zx;
	if (comp->tape->path)
		load_file(comp, comp->tape->path, FG_TAPE, 0);
	if (comp->slot->path)
		load_file(comp, comp->slot->path, FH_SLOTS, 0);
	for (int i = 0; i < 4; i++) {
		Floppy* flp = comp->dif->flp[i];
		if (flp->path)
			load_file(comp, flp->path, FG_DISK, flp->id);
	}
	// this is the machine being set up as it was left, so nothing here was
	// opened by the user and nothing here is to be started
	media_autorun_forget();
}

void xm_save_media(FILE* file) {
	Computer* comp = conf.zx;
	fprintf(file, "\n[MEDIA]\n\n");
	fprintf(file, "tape = %s\n", comp->tape->path ? comp->tape->path : "");
	for (int i = 0; i < 4; i++)
		fprintf(file, "disk.%c = %s\n", 'A' + i, getDiskString(comp->dif->flp[i]).c_str());
	fprintf(file, "hdd.master.type = %i\n", comp->ide->master->type);
	fprintf(file, "hdd.master.lba = %s\n", YESNO(comp->ide->master->hasLBA));
	fprintf(file, "hdd.master = %s\n", comp->ide->master->image ? comp->ide->master->image : "");
	fprintf(file, "hdd.slave.type = %i\n", comp->ide->slave->type);
	fprintf(file, "hdd.slave.lba = %s\n", YESNO(comp->ide->slave->hasLBA));
	fprintf(file, "hdd.slave = %s\n", comp->ide->slave->image ? comp->ide->slave->image : "");
	fprintf(file, "sdcard = %s\n", comp->sdc->image ? comp->sdc->image : "");
	fprintf(file, "sdcard.lock = %s\n", YESNO(comp->sdc->lock));
	fprintf(file, "cartrige.type = %i\n", comp->slot->mapType);
	fprintf(file, "cartrige = %s\n", comp->slot->path ? comp->slot->path : "");
}

// ports the debugger watches, as text: "7FFD" is watched, "-7FFD" keeps its
// place in the list switched off. The editor wants them all; the config file
// takes only what the user decided, since the ports the machine brings itself
// are put back by comp_pwatch_sync() anyway

QStringList getWatchPorts(Computer* comp, int all) {
	QStringList res;
	for (int i = 0; i < comp->pwcount; i++) {
		if (!all && comp->pwatch[i].hw && comp->pwatch[i].on) continue;
		QString str = getPortString(comp->pwatch[i].port, comp->pwatch[i].mask);
		res << (comp->pwatch[i].on ? str : "-" + str);
	}
	return res;
}

// the list is built anew every time: a port that leaves takes its history with
// it, and the emulation reads the same array between two instructions

void setWatchPorts(Computer* comp, QStringList ports) {
	int port, mask;
	emu_lock();
	comp_pwatch_clear(comp);
	foreach(QString str, ports) {
		bool on = !str.startsWith('-');
		if (parsePort(on ? str : str.mid(1), &port, &mask))
			comp_pwatch_add(comp, port, mask, on);
	}
	comp_pwatch_sync(comp);
	emu_unlock();
}

// ----------------------------------------------------------- old profiles

// Read once, on the first run of a build that has no profiles any more. The
// old directories are left exactly where they are - nothing here deletes or
// rewrites them.

static const struct {
	const char* profile;
	const char* id;
} macIdTab[] = {
	{"ZX Spectrum 48K",		"zx48"},
	{"ZX Spectrum 48K + TR-DOS",	"zx48-trdos"},
	{"ZX Spectrum 128K",		"zx128"},
	{"ZX Spectrum 128K + TR-DOS",	"zx128-trdos"},
	{"ZX Spectrum +2",		"zx128"},	// the grey +2 is a 128K, see the plan 7.6
	{"ZX Spectrum +2A",		"zxplus2a"},
	{"ZX Spectrum +3",		"zxplus3"},
	{"Pentagon 128",		"pent"},
	{"Pentagon 512",		"pent"},	// 512K rides in as an override
	{"Pentagon 1024 SL",		"pent1024"},
	{"Scorpion ZS 256",		"scorp"},
	{"Profi",			"profi"},
	{"ATM Turbo 2+",		"atm2"},
	{"ZXM-Phoenix",			"phoenix"},
	{"ZX Evo (BaseConf)",		"evo-baseconf"},
	{"ZX Evo (TSConf)",		"evo-tsconf"},
	{NULL, NULL}
};

// what the old file said the machine was, when the name is not one of ours

static std::string mac_id_by_core(const std::string& path) {
	std::ifstream file(path);
	std::string line;
	bool inmac = false;
	while (std::getline(file, line)) {
		std::pair<std::string,std::string> spl = splitline(line);
		if (spl.second.empty() && !spl.first.empty() && (spl.first[0] == '[')) {
			inmac = (spl.first == "[MACHINE]") || (spl.first == "[GENERAL]");
			continue;
		}
		if (!inmac || (spl.first != "current")) continue;
		xm_set_hardware(spl.second);		// resolves an old core name too
		const xMachine* mac = xm_find_by_core(conf.zx->hw->name);
		if (mac) return mac->id;
		break;
	}
	return std::string();
}

static std::string mac_id_for(const std::string& name, const std::string& path) {
	std::string id = xm_id_for_name(name);
	return id.empty() ? mac_id_by_core(path) : id;
}

// a machine id, the name an old profile went by, or a machine's display name

std::string xm_id_for_name(const std::string& name) {
	if (xm_find(name)) return name;
	for (int i = 0; macIdTab[i].profile; i++) {
		if (name == macIdTab[i].profile) return macIdTab[i].id;
	}
	foreach(const xMachine& mac, macList) {
		if (mac.name == name) return mac.id;
	}
	return std::string();
}

// one key of an old profile file, onto the machine that is already up

static std::string oldRomset;

static void mac_set_old_key(int sect, const std::string& nam, const std::string& val) {
	Computer* comp = conf.zx;
	xArg arg;
	arg.s = val.c_str();
	arg.b = str2bool(val) ? 1 : 0;
	arg.i = strtol(arg.s, NULL, 0);
	arg.d = strtod(arg.s, NULL);
	switch (sect) {
		case PS_MACHINE:
			if (nam == "current") xm_set_hardware(val);
			else if (nam == "cpu.type") mac_set_cpu(comp, val);
			else if (nam == "cpu.frq") {
				int frq = arg.i;
				if ((frq > 1) && (frq < 58)) frq *= 5e5;	// the old 2..28 field
				compSetBaseFrq(comp, toLimits(frq, 100000, 28000000) / 1e6);
			}
			else if (nam == "frq.mul") compSetTurbo(comp, (arg.d < 0.1) ? 0.1 : (arg.d > 8.0) ? 8.0 : arg.d);
			else if (nam == "memory") memSetSize(comp->mem, mac_ram_size(arg.i, comp), -1);
			else if (nam == "contmem") comp->flgCNTM = arg.b;
			else if (nam == "contio") comp->flgCNTI = arg.b;
			else if (nam == "scrp.wait") comp->flgEM1 = arg.b;
			else if (nam == "lastdir") conf.lastDir = val;
			break;
		case PS_ROMSET:
			if (nam == "current") oldRomset = val;
			else if (nam == "reset") {
				comp->resbank = RES_48;
				if ((val == "basic128") || (val == "0")) comp->resbank = RES_128;
				if ((val == "basic48") || (val == "1")) comp->resbank = RES_48;
				if ((val == "shadow") || (val == "2")) comp->resbank = RES_SHADOW;
				if ((val == "dos") || (val == "3")) comp->resbank = RES_DOS;
			}
			break;
		case PS_VIDEO:
			if (nam == "geometry") conf.layName = val;
			else if (nam == "4t-border") comp->vid->brdstep = arg.b ? 7 : 1;
			else if (nam == "ULAplus") comp->vid->ula->enabled = arg.b;
			else if (nam == "contPattern") comp->vid->ula->conttype = arg.i;
			else if (nam == "earlyTiming") comp->vid->ula->early = arg.b;
			else if (nam == "DDpal") comp->flgDDP = arg.b;
			else if (nam == "palette") conf.palette = val;
			break;
		case PS_SOUND:
			if (nam == "psg.count") mac_set_psg(comp, toLimits(arg.i, 0, 3), comp->ts->chipA->type);
			else if (nam == "psg.type") mac_set_psg(comp, mac_psg_count(comp), arg.i);
			else if ((nam == "psg.frq") || (nam == "psg.stereo") || (nam == "psg.separation")
				|| (nam == "gs.reset") || (nam == "gs.stereo")) xm_defer(nam, val);
			else if (nam == "gs") comp->gs->enable = arg.b;
			else if (nam == "soundrive_type") comp->sdrv->type = arg.i;
			else if (nam == "saa") comp->saa->enabled = arg.b;
			break;
		case PS_TAPE:
			if (nam == "path") xm_defer("tape", val);
			else if ((nam == "speed") && (arg.i > 94) && (arg.i < 106)) comp->tape->speed = arg.i;
			break;
		case PS_DISK:
			if (nam == "type") difSetHW(comp->dif, arg.i);
			else if ((nam.size() == 1) && (nam[0] >= 'A') && (nam[0] <= 'D'))
				xm_defer(std::string("disk.") + nam, val);
			break;
		case PS_IDE:
			if (nam == "iface") ide_set_type(comp->ide, arg.i);
			else if (nam == "master.type") xm_defer("hdd.master.type", val);
			else if (nam == "master.lba") xm_defer("hdd.master.lba", val);
			else if (nam == "master.image") xm_defer("hdd.master", val);
			else if (nam == "slave.type") xm_defer("hdd.slave.type", val);
			else if (nam == "slave.lba") xm_defer("hdd.slave.lba", val);
			else if (nam == "slave.image") xm_defer("hdd.slave", val);
			break;
		case PS_INPUT:
			if (nam == "mouse") comp->mouse->enable = arg.b;
			else if ((nam == "mouse.wheel") || (nam == "mouse.swapButtons")
				|| (nam == "mouse.sensitivity") || (nam == "mouse.pctype")
				|| (nam == "kbd.scantab")) xm_defer(nam, val);
			else if (nam == "joy.extbuttons") comp->joy->extbuttons = arg.b;
			else if (nam == "keymap") conf.kmapName = val;
			else if (nam == "gamepad.map") conf.jmapNameA = val;
			else if (nam == "gamepad2.map") conf.jmapNameB = val;
			break;
		case PS_SDC:
			if (nam == "sdcimage") xm_defer("sdcard", val);
			else if (nam == "sdclock") xm_defer("sdcard.lock", val);
			break;
		case PS_SLOT:
			if ((nam == "slot.type") || (nam == "slotA.type") || (nam == "type"))
				xm_defer("cartrige.type", val);
			else if (nam == "path") xm_defer("cartrige", val);
			break;
		case PS_DEBUGA:
			if (nam == "ports") setWatchPorts(comp, QString::fromLocal8Bit(val.c_str()).split(","));
			break;
	}
}

// only what is really there: an absent file must not become an empty one

static void mac_copy_nv(const std::string& name, const std::string& id, const char* ext) {
	std::string src = conf.path.prfDir + SLASH + name + SLASH + name + ext;
	if (!QFile::exists(QString::fromLocal8Bit(src.c_str()))) return;
	copyFile(src.c_str(), (conf.path.nvDir + SLASH + id + ext).c_str());
}

// An old config named a romset out of its own [ROMSETS] table. If that set is
// the machine's own, or one of its variants, it becomes that; anything else
// becomes the files themselves, as the user's own.

static void mac_migrate_romset(const std::string& id) {
	const xMachine* mac = xm_find(id);
	xRomset* rs = oldRomset.empty() ? NULL : findRomset(oldRomset);
	oldRomset.clear();
	if (!rs) {
		xm_set_romset("");
		return;
	}
	xRomset own = mac_roms_of(mac, "");
	if (mac_same_roms(rs, &own)) {
		xm_set_romset("");
		return;
	}
	foreach(xMachineRoms set, mac->roms) {
		if (set.id.empty()) continue;
		xRomset var = mac_roms_of(mac, set.id);
		if (!mac_same_roms(rs, &var)) continue;
		xm_set_romset(set.id);
		return;
	}
	xm_set_romset("");			// the machine's own, with these files over it
	xRomset user = conf.roms;
	user.gsFile = rs->gsFile;
	user.fntFile = rs->fntFile;
	user.roms = rs->roms;
	xm_set_roms(user);
}

// name is the old profile's directory, file its .conf inside it

bool xm_migrate(const std::string& name, const std::string& file) {
	std::string path = conf.path.prfDir + SLASH + name + SLASH + file;
	std::string id = mac_id_for(name, path);
	if (id.empty() || !xm_find(id)) {
		xlog(XLG_CONF, XLL_ERROR, "profile '%s': no machine to migrate it to", name.c_str());
		return false;
	}
	if (!xm_set(id)) return false;
	// the machine's own nvram, before anything reads it
	mac_copy_nv(name, id, ".cmos");
	mac_copy_nv(name, id, ".nvram");
	xm_load_nvram();

	std::ifstream ifile(path);
	if (!ifile.good()) {
		xlog(XLG_CONF, XLL_WARN, "profile '%s': no settings file, taking the machine as it ships", name.c_str());
		return true;
	}
	std::string line;
	int sect = PS_NONE;
	while (std::getline(ifile, line)) {
		size_t pos = line.find_first_of("#;");
		if (pos != std::string::npos) line.erase(pos);
		std::pair<std::string,std::string> spl = splitline(line);
		if (spl.second.empty() && !spl.first.empty() && (spl.first[0] == '[')) {
			if ((spl.first == "[MACHINE]") || (spl.first == "[GENERAL]")) sect = PS_MACHINE;
			else if (spl.first == "[ROMSET]") sect = PS_ROMSET;
			else if (spl.first == "[VIDEO]") sect = PS_VIDEO;
			else if (spl.first == "[SOUND]") sect = PS_SOUND;
			else if (spl.first == "[TAPE]") sect = PS_TAPE;
			else if (spl.first == "[DISK]") sect = PS_DISK;
			else if (spl.first == "[IDE]") sect = PS_IDE;
			else if (spl.first == "[INPUT]") sect = PS_INPUT;
			else if (spl.first == "[SDC]") sect = PS_SDC;
			else if (spl.first == "[SLOT]") sect = PS_SLOT;
			else if (spl.first == "[DEBUGA]") sect = PS_DEBUGA;
			else sect = PS_NONE;
			continue;
		}
		if (spl.first.empty()) continue;
		mac_set_old_key(sect, spl.first, spl.second);
	}
	mac_migrate_romset(id);
	if (!xm_set_layout(conf.layName)) xm_set_layout("default");
	loadPalette();
	xlog(XLG_CONF, XLL_INFO, "profile '%s' migrated to machine '%s'; the old profiles/ is left as it is",
		name.c_str(), id.c_str());
	return true;
}
