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

// biggest backlog we let build up, in ms of sound. after a long stall (window
// drag, profile switch) the emulation should catch up a bit, not sprint
// through everything it missed
#define PACE_MAX_BACKLOG_MS 100

// The ring has to hold the latency ceiling and a full backlog at once, at the
// highest rate offered. The three numbers live in two files and nothing else
// would notice if they stopped fitting.
static_assert(SND_LATENCY_MAX + PACE_MAX_BACKLOG_MS < SND_RING_SIZE * 250 / SND_MAX_RATE,
	"sound ring too small for the latency ceiling plus the pacer backlog");

// how far the pacer may stretch or squeeze emulated time to hold the ring at
// its target. 0.1% covers any crystal mismatch between this clock and the sound
// card's own, and is far too little to hear or to move the frame rate
#define PACE_TRIM_PPM 1000

static SDL_TimerID paceTimerId = 0;
static QElapsedTimer paceClock;
static qint64 paceLastNs = 0;
static qint64 paceRemainderNs = 0;

// The sound card runs off its own crystal, this clock off the host's, and the
// two never quite agree - left alone the ring slowly empties (a click) or fills
// (added lag). Pull it back towards the target: make time run a touch fast when
// the ring is running dry, a touch slow when it is over full. Nothing plays the
// ring under the NULL device, so there is nothing to steer against there.
static qint64 pace_trim(qint64 dtNs) {
	if (!sndPlaybackActive()) return dtNs;
	int target = sndGetRingTargetBytes();
	if (target < 1) return dtNs;
	int err = target - sndGetRingDistance();			// > 0: ring is running dry
	int ppm = toLimits(err * PACE_TRIM_PPM / target, -PACE_TRIM_PPM, PACE_TRIM_PPM);	// full trim one target away
	return dtNs + dtNs * ppm / 1000000;
}

static Uint32 pace_tick(Uint32 interval, void*) {
	qint64 nowNs = paceClock.nsecsElapsed();
	qint64 dtNs = nowNs - paceLastNs + paceRemainderNs;
	paceLastNs = nowNs;

	if (!conf.emu.fast && !conf.emu.pause) {
		dtNs = pace_trim(dtNs);
		// samples for the elapsed time; keep the remainder to avoid drift
		qint64 samples = (dtNs * conf.snd.rate) / 1000000000LL;
		paceRemainderNs = dtNs - (samples * 1000000000LL) / conf.snd.rate;
		// the ring buffer is only drained by a device that plays it. the NULL
		// device never does, so waiting for it to drain would stop the
		// emulation for good - skip the check when nothing is playing.
		int wait = sndPlaybackActive() && (sndGetRingDistance() >= SND_RING_HIGH_WATER);
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
	sndAutoTick(nowNs);

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
	xlog_set_clock(paceClockNs);	// only now does it read anything sensible
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
