// Cocoa side of the virtual keyboard window.
//
// macOS keeps a window's shape itself once it is told what shape to keep, so
// the aspect is declared here instead of the window putting its own size back
// afterwards - doing that during a live resize means fighting whoever owns the
// frame, which on X11 is exactly what looks broken. Qt has no API for it.

#import <AppKit/AppKit.h>

#include <QWidget>

void vkbd_set_aspect(QWidget* wgt, int wid, int hig) {
	if ((wgt == NULL) || (wid < 1) || (hig < 1)) return;
	NSView* view = (NSView*)wgt->winId();
	if (view == nil) return;
	NSWindow* win = [view window];
	if (win == nil) return;
	[win setContentAspectRatio:NSMakeSize(wid, hig)];
}
