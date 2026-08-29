#include <SDL.h>
#include <QElapsedTimer>

#include "pacing.h"
#include "sound.h"
#include "xcore.h"

#if USEMUTEX
#include <QMutex>
#include <QWaitCondition>
extern QMutex emutex;
extern QWaitCondition qwc;
#else
extern int sleepy;
#endif

// wake period in ms, independent of the audio buffer size
#define PACE_TICK_MS 2

// if the audio ring buffer is fuller than this (in bytes), do not add
// samples this tick, so we do not run ahead of real playback
#define RING_HIGH_WATER (0x3fff * 3 / 4)

// biggest backlog we let build up, in ms of sound. after a long stall (window
// drag, profile switch) the emulation should catch up a bit, not sprint
// through everything it missed
#define PACE_MAX_BACKLOG_MS 100

static SDL_TimerID paceTimerId = 0;
static QElapsedTimer paceClock;
static qint64 paceLastNs = 0;
static qint64 paceRemainderNs = 0;

static Uint32 pace_tick(Uint32 interval, void*) {
	qint64 nowNs = paceClock.nsecsElapsed();
	qint64 dtNs = nowNs - paceLastNs + paceRemainderNs;
	paceLastNs = nowNs;

	if (!conf.emu.fast && !conf.emu.pause) {
		// samples for the elapsed time; keep the remainder to avoid drift
		qint64 samples = (dtNs * conf.snd.rate) / 1000000000LL;
		paceRemainderNs = dtNs - (samples * 1000000000LL) / conf.snd.rate;
		// the ring buffer is only drained by a device that plays it. the NULL
		// device never does, so waiting for it to drain would stop the
		// emulation for good - skip the check when nothing is playing.
		int wait = sndPlaybackActive() && (sndGetRingDistance() >= RING_HIGH_WATER);
		if ((samples > 0) && !wait) {
			int maxNeed = conf.snd.rate * PACE_MAX_BACKLOG_MS / 1000;
			conf.snd.need += (int)samples;
			if (conf.snd.need > maxNeed)
				conf.snd.need = maxNeed;
		}
	} else {
		conf.snd.need = 0;
		paceRemainderNs = 0;
	}

#if USEMUTEX
	qwc.wakeAll();
#else
	sleepy = 0;
#endif
	return interval;
}

long long paceClockNs() {
	return paceClock.nsecsElapsed();
}

void pacingInit() {
	paceClock.start();
	paceLastNs = paceClock.nsecsElapsed();
	paceRemainderNs = 0;
	paceTimerId = SDL_AddTimer(PACE_TICK_MS, pace_tick, NULL);
}

void pacingClose() {
	if (paceTimerId) {
		SDL_RemoveTimer(paceTimerId);
		paceTimerId = 0;
	}
}
