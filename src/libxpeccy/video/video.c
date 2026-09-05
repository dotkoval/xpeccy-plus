#include <stdio.h>
#include "../xlog.h"
#include <string.h>
#include <math.h>

#include <assert.h>

#include "video.h"

#if defined(USEOPENGL)
#define SCRBUF_SIZE	2048*768*4
#else
#define SCRBUF_SIZE	3700*2050*4
#endif

int bytesPerLine = 768;
int greyScale = 0;
int noflic = 0;
int noflicMode = 0;
float noflicGamma = 2.2f;

static unsigned char bufa[SCRBUF_SIZE];
static unsigned char bufb[SCRBUF_SIZE];
unsigned char* scrimg = bufa;			// current screen (raw/bw)
unsigned char* bufimg = bufb;			// previous screen (mixed)
static int curbuf = 0;
int bufSize = 3;

// Ring buffer is used for antiflicker to store the history of frames.
// At least 5 frames are required to perform basic 3-Color mode detection.
#define RING_FRAMES 5
unsigned char pscr[SCRBUF_SIZE*RING_FRAMES] __attribute__((aligned(4)));

typedef void(*cbdot)(Video*, unsigned char);

static int32_t outcol;

// The buffer holds the whole raster at 2 pixels per dot, so a dot always lands
// at the same place whatever part of it is on screen. Cutting to the shown
// frame is the drawing side's job (MainWin::uploadFrame).

inline void vid_dot_full(Video* vid, unsigned char idx) {
	if (vid->nodraw) return;
	outcol = greyScale ? vid->gpal[idx] : vid->pal[idx];
	*(int32_t*)(vid->ray.ptr) = outcol;
	*(int32_t*)(vid->ray.ptr + 4) = outcol;
	vid->ray.ptr += 8;
}

inline void vid_dot_half(Video* vid, unsigned char idx) {
	if (vid->nodraw) return;
	outcol = greyScale ? vid->gpal[idx] : vid->pal[idx];
	*(int32_t*)(vid->ray.ptr) = outcol;
	vid->ray.ptr += 4;
}

// Black out both image buffers. Wanted when the machine changes: the buffers
// are shared by every machine and read back with the current one's row length,
// so a frame left there by the machine before would come out skewed. Black is
// what a machine that has drawn nothing yet should show, and that is also the
// answer when it cannot be asked to draw - while the debugger holds it, say.
void vid_clear_image(void) {
	int32_t* pa = (int32_t*)scrimg;
	int32_t* pb = (int32_t*)bufimg;
	int cnt = bufSize / (int)sizeof(int32_t);
	while (cnt-- > 0) {
		*pa++ = 0xff000000;
		*pb++ = 0xff000000;
	}
}

// end of a raster line: move to the next row of the buffer
void vid_line(Video* vid) {
	if (vid->linedbl) {
		memcpy(vid->ray.lptr + bytesPerLine, vid->ray.lptr, bytesPerLine);
		vid->ray.lptr += bytesPerLine;
	}
	vid->ray.lptr += bytesPerLine;
	vid->ray.ptr = vid->ray.lptr;
}

void vid_frame(Video* vid) {
	if (!vid->debug) {
		scrimg = curbuf ? bufb : bufa;
		bufimg = curbuf ? bufa : bufb;
		curbuf = !curbuf;
	}
	vid->ray.lptr = scrimg;
	vid->ray.ptr = scrimg;
	vid->newFrame = 1;
	vid->xirq(IRQ_VID_FRAME, vid->xptr);
}

Video* vidCreate(cbxrd cb, cbirq ci, void* dptr) {
	Video* vid = (Video*)malloc(sizeof(Video));
	memset(vid,0x00,sizeof(Video));
	vid->mrd = cb;
	vid->xirq = ci;
	vid->xptr = dptr;
	vid_set_dot_ns(vid, 150);
	vid->res.x = -1;
	vid->res.y = -1;
	vid_set_mode(vid, VID_UNKNOWN);
	vLayout vlay = {{448,320},{74,48},{64,32},{256,192},{0,0},64};
	vid_set_layout(vid, &vlay);
	vid->inten = 0x01;		// FRAME INT for all

	vid->ula = ula_create();
	vid->txt7220 = upd7220_create();
	vid->grf7220 = upd7220_create();

	vid_set_border(vid, VID_BRD_FULL);

	vid->brdstep = 1;
	vid->nextbrd = 0;
	vid->vidPage = 5;
	vid->fcnt = 0;

	vid->nsDrawFixed = 0;
	vid->nsOwedFixed = 0;
	vid->ray.x = 0;
	vid->ray.y = 0;
	vid->idx = 0;

	vid->ray.ptr = scrimg;
	vid->ray.lptr = scrimg;

	return vid;
}

void vidDestroy(Video* vid) {
	ula_destroy(vid->ula);
	upd7220_destroy(vid->txt7220);
	upd7220_destroy(vid->grf7220);
	free(vid);
}

// The one place the dot period is set. nsPerDot is the whole-ns value other
// code reads; nsPerDotFixed keeps the fraction, and is what the ray steps by.
void vid_set_dot_ns(Video* vid, double nspd) {
	vid->nsPerDotExact = nspd;
	vid->nsPerDot = (int)llround(nspd);
	vid->nsPerDotFixed = NSD_TO_FIXED(nspd);
	if (vid->nsPerDotFixed < 1)
		vid->nsPerDotFixed = 1;		// never let vid_sync_fixed spin forever
}

void vid_upd_timings(Video* vid, double nspd) {
	// nsPerLine/nsPerFrame are each rounded once from the precise nspd, not
	// multiplied up from the rounded nsPerDot, so they don't inherit its error.
	double nsLine = nspd * vid->full.x;
	vid_set_dot_ns(vid, nspd);
	vid->nsPerLine = (int)llround(nsLine);
	vid->nsPerFrame = (int)llround(nsLine * vid->full.y);
#ifdef ISDEBUG
	// printf("%i / %i / %i\n", vid->nsPerDot, vid->nsPerLine, vid->nsPerFrame);
#endif
}

// TODO: for zx only?
void vid_reset(Video* vid) {
	int i;
	for (i = 0; i<16; i++) {
		vid_reset_col(vid, i);
	}
	vid->ula->active = 0;
	vid->vidPage = 5;
	vid->nsDrawFixed = 0;
	vid->nsOwedFixed = 0;
//	vidSetMode(vid, VID_NORMAL);
}

// move ray to 1 dot before INT
void vid_reset_ray(Video* vid) {
/*
	vid->ray.x = vid->intp.x - 1;
	vid->ray.y = vid->intp.y;
	if (vid->ray.x < 0) {
		vid->ray.x += vid->full.x;
		vid->ray.y--;
		if (vid->ray.y < 0)
			vid->ray.y += vid->full.y;
	}
*/
	vid_set_ray(vid, -1);
}

void vid_set_ray(Video* vid, int dots) {
	dots += vid->full.x * vid->intp.y;
	dots += vid->intp.x;
	dots %= vid->dotPerFrame;
	// C keeps the sign, and vid_reset_ray() asks for one dot before the INT:
	// a layout with intpos 0:0 (TSConf, and the built-in default) would put the
	// ray a dot before the buffer and the next tick would write there
	if (dots < 0) dots += vid->dotPerFrame;
	vid->ray.y = dots / vid->full.x;
	vid->ray.x = dots % vid->full.x;
	vid->ray.lptr = scrimg + vid->ray.y * bytesPerLine;
	vid->ray.ptr = vid->ray.lptr + vid->ray.x * 8;
}

// Border shown on each side, in dots and lines. The frame is the same size on
// every machine (256x192 screen plus these), so the picture does not jump when
// profiles are switched. Every ZX layout has at least 48 dots/lines of border
// on all four sides, so nothing up to VID_BRD_FULL needs padding.
// Overscan asks for more than any raster has, so the clamp below settles it.
static const vCoord brdMargin[] = {
	{0, 0},			// VID_BRD_NONE		256x192
	{8, 8},			// VID_BRD_XSMALL	272x208
	{16, 16},		// VID_BRD_SMALL	288x224
	{32, 24},		// VID_BRD_MEDIUM	320x240
	{48, 48},		// VID_BRD_FULL		352x288
	{0x7fff, 0x7fff}	// VID_BRD_OVERSCAN	as much as there is
};

// The shown frame, without changing anything: the screen in the middle, that
// mode's border around it, clamped to what the raster holds. A layout with
// less border than the mode asks for (a hand-made one - no shipped machine is
// like that) gets a smaller frame rather than black bars.
// the border the raster has beside the screen, whichever side has less of it
static int brd_max_x(Video* vid) {
	int mx = vid->bord.x;					// border left of the screen
	if (vid->full.x - vid->send.x < mx)			// ...and right of it
		mx = vid->full.x - vid->send.x;
	return mx;
}

static void vid_crop_rect(Video* vid, int mode, vCoord* lcut, vCoord* rcut) {
	int mx, my;
	if (mode == VID_BRD_NATIVE) {				// whole visible area, as-is
		lcut->x = 0;
		lcut->y = 0;
		*rcut = vid->vend;
		return;
	}
	mx = brd_max_x(vid);
	my = vid->bord.y;					// border above the screen
	if (vid->full.y - vid->send.y < my)			// ...and below it
		my = vid->full.y - vid->send.y;
	if (brdMargin[mode].x < mx) mx = brdMargin[mode].x;
	if (brdMargin[mode].y < my) my = brdMargin[mode].y;
	lcut->x = vid->bord.x - mx;
	lcut->y = vid->bord.y - my;
	rcut->x = vid->send.x + mx;
	rcut->y = vid->send.y + my;
}

// size of the frame a mode gives on this machine
vCoord vid_crop_size(Video* vid, int mode) {
	vCoord lcut, rcut, sze;
	vid_crop_rect(vid, mode, &lcut, &rcut);
	sze.x = rcut.x - lcut.x;
	sze.y = rcut.y - lcut.y;
	return sze;
}

// A fullscreen picture rarely fills a 16:9 screen: the scale comes out of the
// height and there is room to spare on the sides. Show border there instead of
// black, as far as the raster goes - the screen stays in the middle and a dot
// keeps its shape, there is simply more border on show. Never narrower than
// the border size asks for.
void vid_widen_crop(Video* vid, int wid) {
	int mx;
	if (vid->brdmode == VID_BRD_NATIVE) return;	// not a screen with a border around it
	mx = (wid - vid->scrn.x) / 2;
	if (mx > brd_max_x(vid)) mx = brd_max_x(vid);
	if (mx <= vid->bord.x - vid->lcut.x) return;
	vid->lcut.x = vid->bord.x - mx;
	vid->rcut.x = vid->send.x + mx;
	vid->vsze.x = vid->rcut.x - vid->lcut.x;
}

void vid_upd_crop(Video* vid) {
	vid_crop_rect(vid, vid->brdmode, &vid->lcut, &vid->rcut);
	vid->vsze.x = vid->rcut.x - vid->lcut.x;
	vid->vsze.y = vid->rcut.y - vid->lcut.y;
}

// new layout:
// [ bord ][ scr ][ ? ][ blank ]
// [ <--------- full --------> ]
// ? = brdr = full - bord - scr - blank
void vid_upd_layout(Video* vid) {
	vid->vend.x = vid->full.x - vid->blank.x;		// visible right dot
	vid->vend.y = vid->full.y - vid->blank.y;		// visible bottom line
	vid->send.x = vid->bord.x + vid->scrn.x;		// screen end column
	vid->send.y = vid->bord.y + vid->scrn.y;		// screen end line
	vid_upd_crop(vid);
	vid->dotPerFrame = vid->full.y * vid->full.x;
	vid_upd_timings(vid, vid->nsPerDotExact);
}

void vid_set_layout(Video* vid, vLayout* lay) {
	vid->full = lay->full;
	vid->bord = lay->bord;
	vid->blank = lay->blank;
	vid->scrn = lay->scr;
	vid->intp = lay->intpos;
	vid->intsize = lay->intSize;
	vid->frmsz = lay->full.x * lay->full.y;
	vid_upd_layout(vid);
}

// set visible area size
void vid_set_resolution(Video* vid, int w, int h) {
	if ((vid->vsze.y == h) && (vid->vsze.x == w)) return;
	if ((vid->vsze.x <= 0) || (vid->vsze.y <= 0)) return;
	xlog(XLG_VIDEO, XLL_DEBUG, "vid_set_resolution %i x %i",w,h);
	vid->res.x = w;
	vid->res.y = h;
	vid->scrn = vid->res;
	vid->blank.y = vid->full.y - h;
	vid->blank.x = vid->full.x - w;
	vid_upd_layout(vid);
	vid->upd = 1;
}

void vid_set_border(Video* vid, int brd) {
	if (brd < VID_BRD_NONE) brd = VID_BRD_NONE;
	else if (brd > VID_BRD_NATIVE) brd = VID_BRD_NATIVE;
	vid->brdmode = brd;
	vid_upd_crop(vid);
}

// font

void vid_fnt_load(Video* vid, const char* path) {
	FILE* file = fopen(path, "rb");
	if (file) {
		fseek(file, 0, SEEK_END);
		vid->font.size = ftell(file);
		fseek(file, 0, SEEK_SET);
		vid->font.data = realloc(vid->font.data, vid->font.size);
		fread(vid->font.data, vid->font.size, 1, file);
		fclose(file);
	}
}

void vid_fnt_del(Video* vid) {
	if (!vid->font.data) return;
	free(vid->font.data);
	vid->font.data = NULL;
	vid->font.size = 0;
}

int vid_fnt_rd(Video* vid, int adr) {
	int res = -1;
	if (vid->font.data) {
		if (adr < vid->font.size) {
			res = vid->font.data[adr];
		}
	}
	return res;
}

void vid_fnt_wr(Video* vid, int adr, int val) {
	if (vid->font.data) {
		if (adr < vid->font.size) {
			vid->font.data[adr] = val & 0xff;
		}
	}
}

static int xscr = 0;
static int yscr = 0;
static int adr = 0;
static unsigned char col = 0;
static unsigned char ink = 0;
static unsigned char pap = 0;
static unsigned char scrbyte = 0;
static unsigned char nxtbyte = 0;
static unsigned char nxtatr = 0;

void vid_dark_tail(Video* vid) {
	if (vid->tail) return;				// no filling while current fill is active (till end of frame)
	unsigned char* ptr = vid->ray.ptr;		// fill current line till EOL
	unsigned char* zptr = bufimg + (vid->ray.ptr - scrimg); // ptr to prev.frame (place is same as ray at cur.frame)
	unsigned char* btr = scrimg;			// begin of current buffer
	// current line
	while (ptr - vid->ray.lptr < bytesPerLine) {	// dark tail from prev.frame to cur.frame
		*ptr = ((*zptr - 0x80) >> 2) + 0x80;
		zptr++;
		ptr++;
	}
// fill all till end
	while (ptr - btr < bufSize) {
		*ptr = ((*zptr - 0x80) >> 2) + 0x80;
		zptr++;
		ptr++;
	}
	vid->tail = 1;
}

void vid_dark_all() {
	unsigned char* ptr = scrimg;
	int len = bufSize;
	while (len > 0) {
		*ptr = ((*ptr - 0x80) >> 2) + 0x80;
		ptr++;
		len--;
	}
}

//const unsigned char emptyBox[8] = {0x00,0x18,0x3c,0x7e,0x7e,0x3c,0x18,0x00};
//const unsigned char emptyBox[8] = {0x81,0x00,0x00,0x00,0x00,0x00,0x00,0x81};
static unsigned char emptyBox[8] = {0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00};

void vid_get_screen(Video* vid, unsigned char* dst, int bank, int shift, int flag) {
	if ((bank == 0xff) && (shift > 0x2800)) shift = 0x2800;
	int pixadr = MADR(bank, shift);
	int atradr = pixadr + 0x1800;
	unsigned char sbyte, abyte, aink, apap;
	int prt, lin, row, xpos, bitn, cidx;
	int sadr, aadr;
	unsigned char cr,cg,cb;
	for (prt = 0; prt < 3; prt++) {
		for (lin = 0; lin < 8; lin++) {
			for (row = 0; row < 8; row++) {
				for (xpos = 0; xpos < 32; xpos++) {
					sadr = (prt << 11) | (lin << 5) | (row << 8) | xpos;
					aadr = (prt << 8) | (lin << 5) | xpos;
					sbyte = (flag & 2) ? emptyBox[row] : vid->mrd(pixadr + sadr, vid->xptr);
					abyte = (flag & 1) ? 0x47 : vid->mrd(atradr + aadr, vid->xptr);
					aink = (abyte & 0x07) | ((abyte & 0x40) >> 3);
					apap = (abyte & 0x78) >> 3;
					for (bitn = 0; bitn < 8; bitn++) {
						cidx = (sbyte & (128 >> bitn)) ? aink : apap;
						// TODO: apply palette
						cb = (cidx & 1) ? ((cidx & 8) ? 0xff : 0xa0) : 0x00;
						cr = (cidx & 2) ? ((cidx & 8) ? 0xff : 0xa0) : 0x00;
						cg = (cidx & 4) ? ((cidx & 8) ? 0xff : 0xa0) : 0x00;
						if ((flag & 4) && ((lin ^ xpos) & 1)) {
							*(dst++) = ((cr - 0x80) >> 1) + 0x80;
							*(dst++) = ((cg - 0x80) >> 1) + 0x80;
							*(dst++) = ((cb - 0x80) >> 1) + 0x80;
						} else {
							*(dst++) = cr;
							*(dst++) = cg;
							*(dst++) = cb;
						}
					}
				}
			}
		}
	}
}

// ula 5c/6c horizontal timings:
// 0	255	screen
// 256	319	right border (64)
// 320	415	HBlank (96)
// 416	447	left border (32)
// int @ 64 lines above screen

// ula vertical timings
// 0	191	screen
// 192	247	bottom border (56)
// 248	255	VBlank (8)
// 256	311	top border (56)

// NOTE: waiting cycle starts 8 dots before screen?
// (T14336 here), 4dots pre-wait, 2dots ula read pixels, 2dots ula read attr, (T14340:output starts here), 2 dots ula read next pixels, 2 dots ula read next atr, 4 no-wait dots
// ^ repeat 16 times each 16 dots in each of 192 screen rows

static int contTabA[] = {12,11,10,9,8,7,6,5,4,3,2,1,0,0,0,0};		// 48K 128K +2 (bank 1,3,5,7)
static int contTabB[] = {2,1,0,0,14,13,12,11,10,9,8,7,6,5,4,3};		// +2A +3 (bank 4,5,6,7)

// The delay for one bus cycle, read at the dot the cycle starts on. Returns
// dots, not nanoseconds: the callers want time in fixed point, so multiplying
// by the dot period here would both round and be thrown away.
// mreq tells a real memory cycle from an internal one that only parks an
// address on the bus. The Ferranti ULA of the 48K/128K/+2 contends both, the
// Amstrad ASIC of the +2A/+3 only the former - same split as fuse's
// ula_contention / ula_contention_no_mreq.
// dotofs shifts the window for callers whose cycle is anchored differently -
// see IO_CONT_DOTS in spectrum.c
int vid_wait_dots(Video* vid, int adr, int mreq, int dotofs) {
	int xscr;
	int* contTab = NULL;
	switch (vid->ula->conttype) {
		case CONT_PATA:
			adr &= 0x4000;			// pages 1,3,5,7
			contTab = contTabA;
			break;
		case CONT_PATB:				// pages 4,5,6,7
			if (!mreq) return 0;		// asic contends mreq cycles only
			adr &= 0x10000;
			contTab = contTabB;
			break;
	}
	if (!contTab) return 0;				// unknown patern
	if (!adr) return 0;				// address not in contention limits
	if (vid->vbrd) return 0;			// border (vertical)
	xscr = vid->ray.x - vid->bord.x;
	// dots the ULA starts fetching ahead of the first displayed pixel
	xscr += (vid->ula->early ? 10 : 8) + dotofs;
	if (xscr < 0) return 0;				// line before contention
	if (xscr >= vid->scrn.x) return 0;		// line after contention
	return contTab[xscr & 0x0f];			// wait length in dots
}

void vid_set_grey(int f) {
	greyScale = f;
}

// palette

xColor uint_to_xcol(uint32_t c) {
	xColor xcol;
	xcol.r = c & 0xff;
	xcol.g = (c >> 8) & 0xff;
	xcol.b = (c >> 16) & 0xff;
	return xcol;
}

xColor vid_get_col(Video* vid, int i) {
	return uint_to_xcol(vid->pal[i & 0xff]);
}

void vid_set_col(Video* vid, int i, xColor xcol) {
	vid->pal[i & 0xff] = xcol.r | (xcol.g << 8) | (xcol.b << 16) | (0xff << 24);
	outcol = (xcol.b * 30 + xcol.r * 76 + xcol.g * 148) >> 8;
	vid->gpal[i & 0xff] = outcol | (outcol << 8) | (outcol << 16) | (0xff << 24);
}

void vid_set_red(Video* vid, int i, int v) {
	xColor col = vid_get_col(vid, i);
	col.r = v & 0xff;
	vid_set_col(vid, i, col);
}

void vid_set_green(Video* vid, int i, int v) {
	xColor col = vid_get_col(vid, i);
	col.g = v & 0xff;
	vid_set_col(vid, i, col);
}

void vid_set_blue(Video* vid, int i, int v) {
	xColor col = vid_get_col(vid, i);
	col.b = v & 0xff;
	vid_set_col(vid, i, col);
}

// set base color palette (used for preset loading)
void vid_set_bcol(Video* vid, int i, xColor xcol) {
	vid->bpal[i & 0xff] = xcol.r | (xcol.g << 8) | (xcol.b << 16) | (0xff << 24);
}

// set current palette color from preloaded preset
// NOTE: set gpal too
void vid_reset_col(Video* vid, int i) {
	xColor col = uint_to_xcol(vid->bpal[i & 0xff]);
	vid_set_col(vid, i, col);
}

// video drawing

void vidDrawBorder(Video* vid) {
	vid_dot_full(vid, vid->brdcol);
}

// ZX Screen 256 x 192
void vidDrawNormal(Video* vid) {
	if (vid->vbrd) {
		col = vid->brdcol;
		if (vid->ula->active) col |= 8;
		vid->atrbyte = 0xff;
	} else {
		xscr = vid->ray.x - vid->bord.x;
		yscr = vid->ray.y - vid->bord.y;
		if ((xscr & 7) == 3) {
			adr = (vid->idx & 0x181f) | ((vid->idx & 0x700) >> 3) | ((vid->idx & 0xe0) << 3);
			nxtbyte = vid->mrd(MADR(vid->vidPage, adr), vid->xptr);
		}
		if (vid->hbrd) {
			col = vid->brdcol;
			if (vid->ula->active) col |= 8;
			vid->atrbyte = 0xff;
		} else {
			if ((xscr & 7) == 0) {
				scrbyte = nxtbyte;
				adr = 0x1800 | ((vid->idx & 0x1f00) >> 3) | (vid->idx & 0x1f);
				vid->atrbyte = vid->mrd(MADR(vid->vidPage, adr), vid->xptr);
				if (vid->idx < 0x1b00) vid->idx++;
				if (vid->ula->active) {
					ink = ((vid->atrbyte & 0xc0) >> 2) | (vid->atrbyte & 7);
					pap = ((vid->atrbyte & 0xc0) >> 2) | ((vid->atrbyte & 0x38) >> 3) | 8;
				} else {
					if ((vid->atrbyte & 0x80) && vid->flash) scrbyte ^= 0xff;
					ink = (vid->atrbyte & 0x07) | ((vid->atrbyte & 0x40) >> 3);
					pap = (vid->atrbyte & 0x78) >> 3;
				}
			}
			col = (scrbyte & 0x80) ? ink : pap;
			scrbyte <<= 1;
		}
	}
	vid_dot_full(vid, col);
}

// this mode default for ZX48K ULA (defferent moments of pix/atr read)
void ula_dot(Video* vid) {
	if (vid->vbrd) {
		col = vid->brdcol;
		if (vid->ula->active) col |= 8;
		vid->atrbyte = 0xff;
	} else {
		xscr = vid->ray.x - vid->bord.x;
		yscr = vid->ray.y - vid->bord.y;
		switch(xscr & 15) {
			case 12:
				adr = (vid->idx & 0x181f) | ((vid->idx & 0x700) >> 3) | ((vid->idx & 0xe0) << 3);
				nxtbyte = vid->mrd(MADR(vid->vidPage, adr), vid->xptr);
				break;		// 4dots before each even box: box pix
			case 14:
				adr = 0x1800 | ((vid->idx & 0x1f00) >> 3) | (vid->idx & 0x1f);
				nxtatr = vid->mrd(MADR(vid->vidPage, adr), vid->xptr);
				break;		// 2dots before each even box: box atr
			case 0:
				scrbyte = nxtbyte;
				vid->atrbyte = nxtatr;
				vid->idx++;		// lame (idx is still not updated, but we need address of next box)
				adr = (vid->idx & 0x181f) | ((vid->idx & 0x700) >> 3) | ((vid->idx & 0xe0) << 3);
				nxtbyte = vid->mrd(MADR(vid->vidPage, adr), vid->xptr);
				vid->idx--;
				break;		// start of even box: next (odd) box pix
			case 1:
				adr = 0x1800 | ((vid->idx & 0x1f00) >> 3) | (vid->idx & 0x1f);
				nxtatr = vid->mrd(MADR(vid->vidPage, adr), vid->xptr);
				break;		// 2nd dot of even box: next (odd) box atr
			case 8:
				scrbyte = nxtbyte;
				vid->atrbyte = nxtatr;
				break;		// odd box start
		}
		if (vid->hbrd) {
			col = vid->brdcol;
			if (vid->ula->active) col |= 8;
			vid->atrbyte = 0xff;
		} else {
			if ((xscr & 7) == 0) {
				if (vid->idx < 0x1b00) vid->idx++;
				if (vid->ula->active) {
					ink = ((vid->atrbyte & 0xc0) >> 2) | (vid->atrbyte & 7);
					pap = ((vid->atrbyte & 0xc0) >> 2) | ((vid->atrbyte & 0x38) >> 3) | 8;
				} else {
					if ((vid->atrbyte & 0x80) && vid->flash) scrbyte ^= 0xff;
					ink = (vid->atrbyte & 0x07) | ((vid->atrbyte & 0x40) >> 3);
					pap = (vid->atrbyte & 0x78) >> 3;
				}
			}
			col = (scrbyte & 0x80) ? ink : pap;
			scrbyte <<= 1;
		}
	}
	vid_dot_full(vid, col);
}

// alco 16col
void vidDrawAlco(Video* vid) {
	if (vid->vbrd || vid->hbrd) {
		col = vid->brdcol;
	} else {
		yscr = vid->ray.y - vid->bord.y;
		xscr = vid->ray.x - vid->bord.x;
//		if ((xscr < 0) || (xscr > 255)) {
//			col = vid->brdcol;
//		} else {
			adr = ((yscr & 0xc0) << 5) | ((yscr & 7) << 8) | ((yscr & 0x38) << 2) | ((xscr & 0xf8) >> 3);
			switch (xscr & 7) {
				case 0:
					scrbyte = vid->mrd(MADR(vid->vidPage ^ 1, adr), vid->xptr);
					col = (scrbyte & 7) | ((scrbyte & 0x40) >> 3);
					break;
				case 2:
					scrbyte = vid->mrd(MADR(vid->vidPage, adr), vid->xptr);
					col = (scrbyte & 7) | ((scrbyte & 0x40) >> 3);
					break;
				case 4:
					scrbyte = vid->mrd(MADR(vid->vidPage ^ 1, adr + 0x2000), vid->xptr);
					col = (scrbyte & 7) | ((scrbyte & 0x40) >> 3);
					break;
				case 6:
					scrbyte = vid->mrd(MADR(vid->vidPage, adr + 0x2000), vid->xptr);
					col = (scrbyte & 7) | ((scrbyte & 0x40) >> 3);
					break;
				default:
					col = ((scrbyte & 0x38)>>3) | ((scrbyte & 0x80)>>4);
					break;

			}
//		}
	}
	vid_dot_full(vid, col);
}

// hardware multicolor
void vidDrawHwmc(Video* vid) {
	if (vid->vbrd) {
		col = vid->brdcol;
	} else {
		xscr = vid->ray.x - vid->bord.x;
		yscr = vid->ray.y - vid->bord.y;
		if ((xscr & 7) == 4) {
			adr = ((yscr & 0xc0) << 5) | ((yscr & 7) << 8) | ((yscr & 0x38) << 2) | (((xscr + 4) & 0xf8) >> 3);
			nxtbyte = vid->mrd(MADR(vid->vidPage, adr), vid->xptr);
		}
		if (vid->hbrd) {
			col = vid->brdcol;
		} else {
			if ((xscr & 7) == 0) {
				scrbyte = nxtbyte;
				adr = ((yscr & 0xc0) << 5) | ((yscr & 7) << 8) | ((yscr & 0x38) << 2) | ((xscr & 0xf8) >> 3);
				vid->atrbyte = vid->mrd(MADR(vid->vidPage, adr), vid->xptr);
				if ((vid->atrbyte & 0x80) && vid->flash) scrbyte ^= 0xff;
				ink = (vid->atrbyte & 0x07) | ((vid->atrbyte & 0x40) >> 3);
				pap = (vid->atrbyte & 0x78) >> 3;
			}
			col = (scrbyte & 0x80) ? ink : pap;
			scrbyte <<= 1;
		}
	}
	vid_dot_full(vid, col);
}

// atm ega
void vidDrawATMega(Video* vid) {
	yscr = vid->ray.y - 76 + 32;	// ???
	xscr = vid->ray.x - 96 + 64;
	if ((yscr < 0) || (yscr > 199) || (xscr < 0) || (xscr > 319)) {
		col = vid->brdcol;
	} else {
		adr = (yscr * 40) + (xscr >> 3);
		switch (xscr & 7) {
			case 0:
				scrbyte = vid->mrd(MADR(vid->vidPage ^ 4, adr), vid->xptr) & 0xff;
				col = (scrbyte & 7) | ((scrbyte & 0x40) >> 3); // inkTab[scrbyte & 0x7f];
				break;
			case 2:
				scrbyte = vid->mrd(MADR(vid->vidPage, adr), vid->xptr) & 0xff;
				col = (scrbyte & 7) | ((scrbyte & 0x40) >> 3);
				break;
			case 4:
				scrbyte = vid->mrd(MADR(vid->vidPage ^ 4, adr + 0x2000), vid->xptr) & 0xff;
				col = (scrbyte & 7) | ((scrbyte & 0x40) >> 3);
				break;
			case 6:
				scrbyte = vid->mrd(MADR(vid->vidPage, adr + 0x2000), vid->xptr) & 0xff;
				col = (scrbyte & 7) | ((scrbyte & 0x40) >> 3);
				break;
			default:
				col = ((scrbyte & 0x38) >> 3) | ((scrbyte & 0x80) >> 4);
				break;
		}
	}
	vid_dot_full(vid, col);
}

// atm text

void vidDrawByteDD(Video* vid) {		// draw byte $scrbyte with colors $ink,$pap at double-density mode
	for (int i = 0x80; i > 0; i >>= 1) {
		vid_dot_half(vid, (scrbyte & i) ? ink : pap);
	}
}

void vidATMDoubleDot(Video* vid,unsigned char colr) {
	ink = (colr & 0x07) | ((colr & 0x40) >> 3);
	pap = ((colr & 0x38) >> 3) | ((colr & 0x80) >> 4);
	vidDrawByteDD(vid);
}

void vidDrawATMtext(Video* vid) {
	yscr = vid->ray.y - 76 + 32;
	xscr = vid->ray.x - 96 + 64;
	if ((yscr < 0) || (yscr > 199) || (xscr < 0) || (xscr > 319)) {
		vid_dot_full(vid, vid->brdcol);
	} else {
		adr = 0x1c0 + ((yscr & 0xf8) << 3) + (xscr >> 3);
		if ((xscr & 3) == 0) {
			if ((xscr & 7) == 0) {
				scrbyte = vid->mrd(MADR(vid->vidPage, adr), vid->xptr) & 0xff;
				col = vid->mrd(MADR(vid->vidPage ^ 4, adr ^ 0x2000), vid->xptr) & 0xff;
			} else {
				scrbyte = vid->mrd(MADR(vid->vidPage, adr ^ 0x2000), vid->xptr) & 0xff;
				col = vid->mrd(MADR(vid->vidPage ^ 4, adr + 1), vid->xptr) & 0xff;
			}
			scrbyte = vid_fnt_rd(vid, (scrbyte << 3) | (yscr & 7));	// vid->font[(scrbyte << 3) | (yscr & 7)];
			vidATMDoubleDot(vid,col);
		}
	}
}

// atm hardware multicolor
void vidDrawATMhwmc(Video* vid) {
	yscr = vid->ray.y - 76 + 32;
	xscr = vid->ray.x - 96 + 64;
	if ((yscr < 0) || (yscr > 199) || (xscr < 0) || (xscr > 319)) {
		vid_dot_full(vid, vid->brdcol);
	} else {
		//xscr = vid->ray.x - 96;
		//yscr = vid->ray.y - 76;
		adr = (yscr * 40) + (xscr >> 3);
		if ((xscr & 3) == 0) {
			if ((xscr & 7) == 0) {
				scrbyte = vid->mrd(MADR(vid->vidPage, adr), vid->xptr);
				col = vid->mrd(MADR(vid->vidPage ^ 4, adr), vid->xptr);
			} else {
				scrbyte = vid->mrd(MADR(vid->vidPage, adr + 0x2000), vid->xptr);
				col = vid->mrd(MADR(vid->vidPage ^ 4, adr + 0x2000), vid->xptr);
			}
			vidATMDoubleDot(vid,col);
		}
		//vid->ray.ptr++;
		//if (vidFlag & VF_DOUBLE) vid->ray.ptr++;
	}
}

// baseconf text

void vidDrawEvoText(Video* vid) {
	yscr = vid->ray.y - 76;
	xscr = vid->ray.x - 96;
	if ((yscr < 0) || (yscr > 199) || (xscr < 0) || (xscr > 319)) {
		vid_dot_full(vid, vid->brdcol);
	} else {
		if ((xscr & 3) == 0) {
			adr = 0x1c0 + ((yscr & 0xf8) << 3) + (xscr >> 3);
			if ((xscr & 7) == 0) {
				scrbyte = vid->mrd(MADR(vid->vidPage + 3, adr), vid->xptr);
				col = vid->mrd(MADR(vid->vidPage + 3, adr + 0x3000), vid->xptr);
			} else {
				scrbyte = vid->mrd(MADR(vid->vidPage + 3, adr + 0x1000), vid->xptr);
				col = vid->mrd(MADR(vid->vidPage + 3, adr + 0x2001), vid->xptr);
			}
			scrbyte = vid_fnt_rd(vid, (scrbyte << 3) | (yscr & 7)); // vid->font[(scrbyte << 3) | (yscr & 7)];
			vidATMDoubleDot(vid,col);
		}
	}
}

// profi 512x240

void vidProfiScr(Video* vid) {
	yscr = vid->ray.y - vid->bord.y + 24;	// (240-192)/2 = 24
	if ((yscr < 0) || (yscr > 239)) {
		vid_dot_full(vid, vid->brdcol);
	} else {
		xscr = vid->ray.x - vid->bord.x;
		if ((xscr < 0) || (xscr > 255)) {
			vid_dot_full(vid, vid->brdcol);
		} else {
			if ((xscr & 3) == 0) {
				//adr = scrAdrs[vid->idx & 0x1fff] & 0x1fff;
				adr = (vid->idx & 0x181f) | ((vid->idx & 0x700) >> 3) | ((vid->idx & 0xe0) << 3);
				if (xscr & 4) {
					vid->idx++;
				} else {
					adr |= 0x2000;
				}
				if (vid->vidPage == 7) {
					scrbyte = vid->mrd(MADR(6, adr), vid->xptr);
					col = vid->mrd(MADR(0x3a, adr), vid->xptr);		// b0..2 ink, b3..5 pap, b6 inkBR, b7 papBR
				} else {
					scrbyte = vid->mrd(MADR(4, adr), vid->xptr);
					col = vid->mrd(MADR(0x38, adr), vid->xptr);
				}
				ink = (col & 0x07) | ((col & 0x40) >> 3);
				pap = (col & 0x78) >> 3;
				vidDrawByteDD(vid);
			}
		}
	}
}

// tsconf

void vts_hblk(Video*);
void vts_line(Video*);
void vts_frame(Video*);
void vidDrawTSLNormal(Video*);
void vidDrawTSLExt(Video*);
void vidDrawTSLText(Video*);
void vidDrawEvoText(Video*);

// c64 vic-II

void vidC64TDraw(Video*);
void vidC64TMDraw(Video*);
void vidC64BDraw(Video*);
void vidC64BMDraw(Video*);
void vidC64Line(Video*);
void vidC64Fram(Video*);

// debug

void vidBreak(Video* vid) {
	xlog(XLG_VIDEO, XLL_DEBUG, "vid->mode = 0x%.2X",vid->vmode);
	// assert(0);
}

// bk

void bk_bw_dot(Video*);
void bk_col_dot(Video*);

// specialist

void spc_dot(Video*);
void spcv_ini(Video*);

// vga

void cga_t40_frm(Video*);
void cga_t40_line(Video*);
void cga320_2bpp_line(Video*);
void cga640_1bpp_line(Video*);
void vga320_4bpp_line(Video*);
void vga640_4bpp_line(Video*);
void vga256_line(Video*);
void cga_t40_dot(Video*);
void cga_lores_dot(Video*);
void ega_hires_dot(Video*);
void cga_t40_ini(Video*);
void cga_t80_ini(Video*);
void vga_glo_ini(Video*);
void vga_ghi_ini(Video*);

// weiter

// id,(@on),(@every_visible_dot),(@HBlank),(@LineStart),(@VBlank),(@Frame)
static xVideoMode vidModeTab[] = {
	{VID_NORMAL, NULL, vidDrawNormal, NULL, NULL, NULL, NULL},
	{VID_ULA_SCR, NULL, ula_dot, NULL, NULL, NULL, NULL},
	{VID_ALCO, NULL, vidDrawAlco, NULL, NULL, NULL, NULL},
	{VID_HWMC, NULL, vidDrawHwmc, NULL, NULL, NULL, NULL},
	{VID_ATM_EGA, NULL, vidDrawATMega, NULL, NULL, NULL, NULL},
	{VID_ATM_TEXT, NULL, vidDrawATMtext, NULL, NULL, NULL, NULL},
	{VID_ATM_HWM, NULL, vidDrawATMhwmc, NULL, NULL, NULL, NULL},
	{VID_EVO_TEXT, NULL, vidDrawEvoText, NULL, NULL, NULL, NULL},
	{VID_TSL_NORMAL, NULL, vidDrawTSLNormal, vts_hblk, vts_line, NULL, vts_frame},
	{VID_TSL_16, NULL, vidDrawTSLExt, vts_hblk, vts_line, NULL, vts_frame},			// vidDrawTSL16
	{VID_TSL_256, NULL, vidDrawTSLExt, vts_hblk, vts_line, NULL, vts_frame},		// vidDrawTSL256
	{VID_TSL_TEXT, NULL, vidDrawTSLText, vts_hblk, vts_line, NULL, vts_frame},
	{VID_PRF_MC, NULL, vidProfiScr, NULL, NULL, NULL, NULL},

	{VID_GBC, NULL, gbcvDraw, NULL, gbcvLine, gbcvVBL, gbcvFram},
	{VID_NES, NULL, ppuDraw, ppuHBL, ppuLine, ppuFram, NULL},

	{VDP_TEXT1, NULL, vdpText1, vdpHBlk, NULL, NULL, NULL},
	{VDP_TEXT2, NULL, vdpDummy, vdpHBlk, NULL, NULL, NULL},
	{VDP_MCOL, NULL, vdpMultcol, vdpHBlk, vdp_line, NULL, NULL},
	{VDP_GRA1, NULL, vdpGra1, vdpHBlk, vdp_line, NULL, NULL},
	{VDP_GRA2, NULL, vdpGra2, vdpHBlk, vdp_line, NULL, NULL},
	{VDP_GRA3, NULL, vdpGra2, vdpHBlk, vdp_linex, NULL, NULL},
	{VDP_GRA4, NULL, vdpGra4, vdpHBlk, vdp_linex, NULL, NULL},
	{VDP_GRA5, NULL, vdpGra5, vdpHBlk, vdp_linex, NULL, NULL},
	{VDP_GRA6, NULL, vdpGra6, vdpHBlk, vdp_linex, NULL, NULL},
	{VDP_GRA7, NULL, vdpGra7, vdpHBlk, vdp_linex, NULL, NULL},

	{VID_C64_TEXT, NULL, vidC64TDraw, NULL, vidC64Line, vidC64Fram, NULL},
	{VID_C64_TEXT_MC, NULL, vidC64TMDraw, NULL, vidC64Line, vidC64Fram, NULL},
	{VID_C64_BITMAP, NULL, vidC64BDraw, NULL, vidC64Line, vidC64Fram, NULL},
	{VID_C64_BITMAP_MC, NULL, vidC64BMDraw, NULL, vidC64Line, vidC64Fram, NULL},

	{VID_BK_BW, NULL, bk_bw_dot, NULL, NULL, NULL, NULL},
	{VID_BK_COL, NULL, bk_col_dot, NULL, NULL, NULL, NULL},

	{VID_SPCLST, spcv_ini, spc_dot, NULL, NULL, NULL, NULL},

	{CGA_TXT_L, cga_t40_ini, cga_lores_dot, NULL, cga_t40_line, NULL, cga_t40_frm},
	{CGA_TXT_H, cga_t80_ini, cga_t40_dot, NULL, cga_t40_line, NULL, cga_t40_frm},
	{CGA_GRF_L, NULL, cga_lores_dot, NULL, cga320_2bpp_line, NULL, cga_t40_frm},
	{CGA_GRF_H, NULL, cga_t40_dot, NULL, cga640_1bpp_line, NULL, cga_t40_frm},
	{VGA_GRF_L, vga_glo_ini, cga_t40_dot, NULL, vga320_4bpp_line, NULL, cga_t40_frm},
	{VGA_GRF_H, vga_ghi_ini, cga_t40_dot, NULL, vga640_4bpp_line, NULL, cga_t40_frm},
	{VGA_GRF_256, vga_glo_ini, cga_lores_dot, NULL, vga256_line, NULL, cga_t40_frm},


	{VID_PC98XX, NULL, upd7220_dot, NULL, upd7220_line, NULL, upd7220_frame},

	{VID_UNKNOWN, NULL, vidDrawBorder, NULL, NULL, NULL, NULL}
};

void vid_set_core(Video* vid, xVideoMode* xvm) {
	vid->cb = xvm;
	if (xvm->init)
		xvm->init(vid);
}

void vid_set_mode(Video* vid, int mode) {
	vid->vmode = mode;
	int i = 0;
	while ((vidModeTab[i].id != VID_UNKNOWN) && (vidModeTab[i].id != mode)) {
		i++;
	}
	vid_set_core(vid, &vidModeTab[i]);
}

// NOTE: VBlank starts after last HBlank

void vid_tick(Video* vid) {
	if ((vid->ray.x & vid->brdstep) == 0)
		vid->brdcol = vid->nextbrd;

	if (vid->cb->dot)
		vid->cb->dot(vid);
	// move ray to next dot, update counters
	vid->ray.x++;
	vid->ray.xb++;
	vid->ray.xs++;
	if (vid->ray.x >= vid->full.x) {			// new line
		vid_line(vid);					// next row of the image buffer
		vid->hblank = 0;
		vid->ray.x = 0;
		vid->ray.y++;
		if (vid->ray.y == vid->vend.y) {		// vblank (@ start of line)
			vid->vblank = 1;
			vid_irq(vid, IRQ_VID_VBLANK);
			if (vid->cb->vbl)
				vid->cb->vbl(vid);
		}
		if (vid->ray.y >= vid->full.y) {		// new frame
			vid_frame(vid);				// complete frame image
			vid->idx = 0;
			vid->ray.y = 0;
			vid->vblank = 0;
			vid->fcnt++;
			vid->flash = (vid->fcnt & 0x10) ? 1 : 0;
			if (vid->cb->frm)
				vid->cb->frm(vid);
			vid->tail = 0;
			if (vid->debug)
				vid_dark_all();
		}
		if (vid->cb->line)
			vid->cb->line(vid);
		vid->vbrd = (vid->ray.y < vid->bord.y) || (vid->ray.y >= vid->send.y);
	}
	if (vid->ray.x == vid->bord.x) {
		vid->ray.xs = 0;
		vid->ray.ys++;
		if (vid->ray.y == vid->bord.y) vid->ray.ys = 0;
	}
	if (vid->ray.x == vid->vend.x) {			// hblank
		vid->ray.xb = 0;
		vid->ray.yb++;
		if (vid->ray.y == vid->vend.y - 1) {
			vid->ray.yb = 0;
		}
		vid->hblank = 1;
		vid_irq(vid, IRQ_VID_HBLANK);
		if (vid->cb->hbl)
			vid->cb->hbl(vid);
	}
	vid->hbrd = (vid->ray.x < vid->bord.x) || (vid->ray.x >= vid->send.x);
	// generate int
	if (vid->intFRAME) {
		vid->intFRAME--;
		if (!vid->intFRAME)
			vid->xirq(IRQ_VID_IEND, vid->xptr);
	} else if ((vid->ray.yb == vid->intp.y) && (vid->ray.xb == vid->intp.x) && (vid->inten & 1)) {		// added: ...and frame int enabled
		vid->xirq(IRQ_VID_INT, vid->xptr);
	}
	if (vid->busy > 0) {
		vid->busy--;
		if ((vid->busy == 0) && vid->cbCount)
			vid->cbCount(vid);
	}
	if (vid->inth > 0) vid->inth--;
	if (vid->intf > 0) vid->intf--;
}

// The ray steps in fixed point ns. vid->time stays whole ns for everything
// downstream (sound pacing among others); the fraction it is owed rides along
// in nsOwedFixed rather than being dropped once per call.
void vid_sync_fixed(Video* vid, long long nsFixed) {
	if (!nsFixed) return;			// no time passed: the tail below is a no-op
	vid->nsDrawFixed += nsFixed;
	while (vid->nsDrawFixed >= vid->nsPerDotFixed) {
		vid->nsDrawFixed -= vid->nsPerDotFixed;
		vid->nsOwedFixed += vid->nsPerDotFixed;
		vid_tick(vid);
	}
	// whole nanoseconds out, the rest stays owed. Once per call, not per dot:
	// the dot loop runs ~143k times a frame.
	long long whole = FIXED_TO_NS(vid->nsOwedFixed);
	vid->nsOwedFixed -= NS_TO_FIXED(whole);
	vid->time += (int)whole;
}

void vid_sync(Video* vid, int ns) {
	vid_sync_fixed(vid, NS_TO_FIXED(ns));
}
