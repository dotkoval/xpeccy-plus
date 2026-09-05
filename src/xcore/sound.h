#pragma once

#include <vector>
#include <string>

#if defined(__linux) || defined(__BSD)
	#include <sys/ioctl.h>
	#include <sys/soundcard.h>
	#include <fcntl.h>
#endif

#include "xcore.h"

enum xSoundOutput {
	xOutputNone = 0,
	xOutputOss,
	xOutputAlsa,
	xOutputSDL,
	xOutputWave
};

typedef struct {
	int id;
	const char* name;
	int (*open)();
	void (*play)();		// start playing what has been buffered so far
	void (*close)();
} OutSys;

// The ring is sbuf in sound.cpp, 4 bytes to a frame - 341 ms at 48kHz. It is
// sized well over the latency ceiling: a bluetooth headset needs far more of a
// buffer than a wired one, and the ring has to hold the worst of them.
#define SND_RING_SIZE	0x10000
#define SND_RING_MASK	(SND_RING_SIZE - 1)

// Filling stops here. Past it the writer would overwrite sound that has not
// been played yet.
#define SND_RING_HIGH_WATER	(SND_RING_SIZE * 7 / 8)

// One audio callback block, in ms. The callback asks for a whole block at once,
// so the ring can never hold less than this without clicking: the block is the
// floor under conf.snd.latency, and the target is at least two of them.
#define SND_BLOCK_MS	5
#define SND_LATENCY_MIN	(2 * SND_BLOCK_MS)
#define SND_LATENCY_MAX	150
// where a fresh install starts. Above where the regulator comes to rest, on
// purpose: Auto walks a too-high value quietly down, while too low a one is
// heard before it climbs back.
#define SND_LATENCY_DEF	30

// the highest rate the app offers. the ring holds fewer ms the higher this
// goes, so it is what the size has to be checked against (see pacing.cpp)
#define SND_MAX_RATE		48000

extern OutSys sndTab[];
extern OutSys* sndOutput;

extern long long nsPerSampleFixed;

void sndInit();
void addOutput(std::string, bool(*)(),void(*)(),void(*)());
void setOutput(const char*);

void sndClose();
int sndSync(Computer*);
int sndGetRingDistance();
int sndGetRingTargetBytes();
int sndPlaybackActive();
void sndAutoTick(long long);

int snd_wav_open(const char*);
void snd_wav_close();
void snd_wav_write();

std::string sndGetName();
void sndDebug();
