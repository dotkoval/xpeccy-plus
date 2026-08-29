#pragma once

#include <QString>

#include "../libxpeccy/vfat.h"
#include "../libxpeccy/spectrum.h"	// sdcard.h has no extern "C" of its own

vFat* vfat_scan(const QString&);	// build a volume from a host folder, NULL if it can't be read
void sdc_mount(SDCard*, const QString&);	// mount an image file or a host folder, whichever the path is
void sdc_remount(SDCard*);		// re-read what is mounted (a folder may have changed)

void ide_mount(IDE*, int, const QString&);	// same for one IDE device (IDE_MASTER / IDE_SLAVE)
void ide_remount(IDE*);
