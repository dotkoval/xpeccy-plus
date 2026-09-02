#pragma once

#include "../libxpeccy/spectrum.h"

// autostart kinds
enum {
	AS_NONE = 0,
	AS_TAPE,
	AS_DISK,	// tr-dos image
	AS_DISK3	// +3 disk
};

// arm() once, when the media is opened; frame() ticks it until it is done.
// arm() answers 0 when this machine cannot start that kind of media at all
int autostart_arm(Computer*, int kind);
void autostart_frame(Computer*);
// the machine is being driven to the media: nothing of it is to be shown
int autostart_busy();
