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

// The ring is the 0x4000 byte sbuf in sound.cpp, 4 bytes to a frame - 85 ms at
// 48kHz. Filling stops at the high water mark, past which the writer would
// overwrite sound that has not been played yet: 74 ms at 48kHz, which leaves
// the latency ceiling below clear of it at every rate the app offers.
#define SND_RING_HIGH_WATER	(0x3fff * 7 / 8)

// One audio callback block, in ms. The callback asks for a whole block at once,
// so the ring can never hold less than this without clicking: the block is the
// floor under conf.snd.latency, and the target is at least two of them.
#define SND_BLOCK_MS	10
#define SND_LATENCY_MIN	(2 * SND_BLOCK_MS)
#define SND_LATENCY_MAX	60
#define SND_LATENCY_DEF	30

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

int snd_wav_open(const char*);
void snd_wav_close();
void snd_wav_write();

std::string sndGetName();
void sndDebug();
