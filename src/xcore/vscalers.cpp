// #include <math.h>
#include <string.h>

#include "xcore.h"
#include "vscalers.h"

#include <QApplication>
#include <QScreen>
#include <QDebug>

// The border sizes offered to the user. VID_BRD_NATIVE is not here: it is
// picked automatically for machines outside the ZX family, whose raster is
// not a 256x192 screen with a border around it.
int drawX = 0;
int drawY = 0;
int drawW = 0;
int drawH = 0;

const char* brdModeTab[] = {
	"no",		// VID_BRD_NONE
	"small",
	"medium",
	"full",
	"overscan",	// VID_BRD_OVERSCAN
	NULL
};

const char* brd_mode_name(int mode) {
	if ((mode < VID_BRD_NONE) || (mode > VID_BRD_OVERSCAN))
		mode = VID_BRD_FULL;
	return brdModeTab[mode];
}

int brd_mode_id(const char* nam) {
	for (int i = 0; brdModeTab[i]; i++) {
		if (!strcmp(brdModeTab[i], nam))
			return i;
	}
	return VID_BRD_FULL;
}

// pre-2026.4 configs kept the border as a percentage of the machine's own
int brd_mode_pcnt(int pcnt) {
	if (pcnt < 5) return VID_BRD_NONE;
	if (pcnt < 35) return VID_BRD_SMALL;
	if (pcnt < 70) return VID_BRD_MEDIUM;
	if (pcnt < 100) return VID_BRD_FULL;
	return VID_BRD_OVERSCAN;
}

// Where the frame lands inside the window. The zoom is a whole number of
// window pixels per dot, so a dot is never split - in fullscreen that means
// the largest zoom that fits, centered, with black around it. 'ignore aspect
// ratio' is the one way to get the old stretch-to-fill.
void vid_upd_scale() {
	QSize scrsz;
	Video* vid = conf.prof.cur->zx->vid;
	double xscale = conf.prof.cur->zx->hw->xscale;		// BK is 2 pixels wide per dot
	int dwid;
	int dhei;
	int zoom;
	if (conf.vid.fullScreen) {
#if QT_VERSION >= QT_VERSION_CHECK(5,14,0)
		scrsz = QApplication::screens().first()->size();
#else
		scrsz = QApplication::desktop()->screenGeometry().size();
#endif
		dwid = scrsz.width();
		dhei = scrsz.height();
		if (conf.vid.keepRatio) {
			zoom = int(dwid / (vid->vsze.x * xscale));
			if (dhei / vid->vsze.y < zoom)
				zoom = dhei / vid->vsze.y;
			if (zoom < 1) zoom = 1;
			drawW = int(vid->vsze.x * zoom * xscale);
			drawH = vid->vsze.y * zoom;
		} else {
			drawW = dwid;
			drawH = dhei;
		}
	} else {
		zoom = conf.vid.scale;
		drawW = int(vid->vsze.x * zoom * xscale);
		drawH = vid->vsze.y * zoom;
		dwid = drawW;
		dhei = drawH;
	}
	drawX = (dwid - drawW) / 2;
	drawY = (dhei - drawH) / 2;
}

void vid_set_zoom(int zoom) {
	if (zoom < 1) return;
	if (zoom > 6) return;
	conf.vid.scale = zoom;
	vid_upd_scale();
}

void vid_set_fullscreen(int f) {
	conf.vid.fullScreen = f ? 1 : 0;
	vid_upd_scale();
}

// The border sizes are a ZX thing: the rest of the machines have no 256x192
// screen with a border around it, and keep whatever their layout shows.
int brd_mode_for(Computer* comp, int mode) {
	return (comp->hw->grp == HWG_ZX) ? mode : VID_BRD_NATIVE;
}

void vid_set_border_mode(int mode) {
	Computer* comp = conf.prof.cur->zx;
	conf.vid.border = mode;
	vid_set_border(comp->vid, brd_mode_for(comp, mode));
	vid_upd_scale();
}

void vid_set_ratio(int f) {
	conf.vid.keepRatio = f ? 1 : 0;
	vid_upd_scale();
}

