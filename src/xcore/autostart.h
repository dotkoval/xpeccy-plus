#pragma once

#include "../libxpeccy/spectrum.h"

// autostart kinds
enum {
	AS_NONE = 0,
	AS_TAPE,
	AS_DISK,	// tr-dos image
	AS_DISK3	// +3 disk
};

// arm() once, from the command line; frame() ticks it until it is done
void autostart_arm(Computer*, int kind);
void autostart_frame(Computer*);
// the machine is being driven to the media: nothing of it is to be shown
int autostart_busy();
