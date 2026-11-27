#pragma once

// macOS: Shift, Ctrl, Option and Command told left from right (keys_mac.mm)

#include <QKeyEvent>

#define MACMOD_LSHIFT	(1 << 0)
#define MACMOD_RSHIFT	(1 << 1)
#define MACMOD_LCTRL	(1 << 2)	// the physical Ctrl, which Qt calls Meta
#define MACMOD_RCTRL	(1 << 3)
#define MACMOD_LALT	(1 << 4)	// Option
#define MACMOD_RALT	(1 << 5)
#define MACMOD_LCMD	(1 << 6)	// Command, which Qt calls Control: a machine key only when tapped
#define MACMOD_RCMD	(1 << 7)
#define MACMOD_CMDS	(MACMOD_LCMD | MACMOD_RCMD)

// the nativeScanCode of a modifier key event of our own: nativeVirtualKey is its XKEY_*
#define MACMOD_EVENT	0x584d4f44

// cb gets the MACMOD_* keys held, the raw flags and the key's code on every change of
// them; other is told of every key and mouse button going down in the program
void mac_mods_watch(void (*cb)(unsigned, unsigned long, unsigned), void (*other)());
// the MACMOD_* keys held as the system says now; last: for a pair it gives no side of
unsigned mac_mods_live(unsigned last, unsigned long* flagsOut);
double mac_key_idle();		// seconds since a key last went down anywhere in the system
// emulwin.cpp: the machine given the modifiers held (key: ahead of a key press), and let go of them
void mac_mods_sync(bool key);
void mac_mods_let_go();
// emulwin.cpp: a tapped Command's key let go once the machine has had it for a few frames
void mac_tap_frame();

// Qt's own event of a Shift, Ctrl, Option or Command: no side, and not every one comes;
// the machine and the pads take these keys from our own events instead
inline bool mac_mod_key(const QKeyEvent* ev) {
	if (ev->nativeScanCode() == MACMOD_EVENT) return false;
	int key = ev->key();
	return (key == Qt::Key_Shift) || (key == Qt::Key_Meta) || (key == Qt::Key_Alt) || (key == Qt::Key_Control);
}
