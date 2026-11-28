// ZiFi and RS-232 on the ZX Evo's AVR as the TS firmware has them (rs232.c, rs232_asm.S); time is ns, kept up at a port or zifi_sync

#include <stdlib.h>
#include <string.h>

#include "zifi.h"
#include "uartframe.h"
#include "hardware.h"
#include "zx_step.h"
#include "../spectrum.h"

#define ZF		(comp->zifi)

// not measured on a board: the firmware's path puts an answer to a port and a pass of its main loop at 15-30 us
#define ZF_WAIT_NS	20000
#define ZF_PASS_NS	20000
#define ZF_MS_NS	1000000		// TIMER1: OCR1A 11059 at 11.0592 MHz
#define ZF_F_CPU	11059200LL
#define ZF_BIT16	568888889LL	// a bit at 115200 (UBRR 5), ns 16.16
#define ZF_FRAME_TX	0x19		// 8 bits, no parity, 2 stop bits, as the Next's uart_framing_r
#define ZF_LIM		0xbf		// ZF_DR_REG_LIM: DR's last address, and where the counts stop

enum {
	ZF_IFR = 0xc0, ZF_OFR, RS_IFR, RS_OFR, ZF_ISR, ZF_IBTR, ZF_ITOR, ZF_CR, RS_IBTR, RS_ITOR,
	ZF_REG_LIM = 0xcf,
	UART_DAT = 0xf8, UART_IER, UART_FCR, UART_LCR, UART_MCR, UART_LSR, UART_MSR, UART_SPR
};

void (*zifi_host_tx)(Computer*, int, long long) = NULL;

int zifi_wait_ns(void) {
	return ZF_WAIT_NS;
}

// between instructions comp_ns() counts the last one twice: its T are in tickTotal already and still in the CPU's count
long long zifi_now(Computer* comp) {
	long long ns = comp_ns(comp) + ZF->off;
	if (!ZF->io) ns -= (long long)(comp->cpu->t * comp->nsPerTick);
	return ns;
}

void zifi_tx_cfg(Computer* comp, long* bit, int* frame) {
	if (bit) *bit = (long)((ZF_BIT16 + 0x8000) >> 16);
	if (frame) *frame = ZF_FRAME_TX;
}

// == a transmitter ==

static int tx_free(zfTx* x) {
	return (unsigned char)(x->tl - 1 - x->hd);
}

// up to t: a byte goes to the data register as that empties, to the shifter when the one before is out
static void tx_run(Computer* comp, zfTx* x, long long t, long long bit16, int bits) {
	long long len = (bits * bit16) >> 16;
	for (;;) {
		if (!x->udrFull) {
			if (x->hd == x->tl) break;
			x->udr = x->buf[x->tl++];
			x->udrFull = 1;
		}
		long long s = (x->shEnd > x->udrFree) ? x->shEnd : x->udrFree;
		if (s > t) break;
		x->udrFull = 0;
		x->udrFree = s;
		x->shEnd = s + len;
		if (x == &ZF->ztx) {
			// run ahead, it is sent again for real
			if (!x_runahead && zifi_host_tx) zifi_host_tx(comp, x->udr, s);
		} else if (!ZF->rsTxc) {
			ZF->rsTxc = s + len;
		}
	}
}

static void tx_put(Computer* comp, zfTx* x, int val, long long now) {
	if (!tx_free(x)) return;
	x->buf[x->hd++] = val & 0xff;
	if (!x->udrFull && (x->udrFree < now)) x->udrFree = now;	// straight into the data register
}

// == the receiver on the module's line ==

// the bytes handed over onto the line, a frame at a time at the module's rate
static void rx_feed_line(xZifi* z) {
	while (z->hqN && (z->schN + 12 <= ZF_SCH)) {
		long long p = z->farBit ? z->farBit : ZF_BIT16;
		long long t = (z->lineFree > z->feedAt) ? z->lineFree : z->feedAt;
		int lv[12];
		int n = uart_frame_levels(z->hq[z->hqHead], z->farBit ? z->farFrame : 0x18, lv);
		for (int k = 0; k < n; k++) {
			if (lv[k] == z->schLv) continue;
			zfEdge* e = &z->sch[(z->schHead + z->schN) % ZF_SCH];
			e->t = t + ((k * p) >> 16);
			e->lv = lv[k];
			z->schN++;
			z->schLv = lv[k];
		}
		z->lineFree = t + ((n * p) >> 16);
		z->hqHead = (z->hqHead + 1) % ZF_HQ;
		z->hqN--;
	}
}

// the line's next change no later than t, taken; 0: none
static int rx_edge(xZifi* z, long long t, zfEdge* e) {
	if (z->hqN) rx_feed_line(z);
	if (!z->schN || (z->sch[z->schHead].t > t)) return 0;
	*e = z->sch[z->schHead];
	z->schHead = (z->schHead + 1) % ZF_SCH;
	z->schN--;
	return 1;
}

static int zf_ifr(xZifi* z) {
	return (z->rxHd - z->rxTl) & (ZF_RXF - 1);
}

// the timeout source at now: ms since a byte last came, with bytes held
static int zf_timed_out(xZifi* z, long long now) {
	return (now >= z->rxLast) && ((now - z->rxLast) / ZF_MS_NS >= z->zitor) && zf_ifr(z);
}

// up to t, as USART0 takes the line at 115200: a bit is read in its middle, the byte kept whatever the stop bit is
static void rx_run(xZifi* z, long long t) {
	zfEdge e;
	for (;;) {
		if (!z->inFrame) {
			while (!z->inFrame && rx_edge(z, t, &e)) {
				int was = z->lin;
				z->lin = e.lv;
				if (was && !e.lv) {
					z->inFrame = 1;
					z->t0 = e.t;
					z->bit = 0;
					z->shift = 0;
				}
			}
			if (!z->inFrame) break;
		}
		long long ts = z->t0 + (((2 * z->bit + 1) * ZF_BIT16) >> 17);
		if (ts > t) break;
		while (rx_edge(z, ts, &e)) z->lin = e.lv;
		if (!z->bit) {
			if (z->lin) z->inFrame = 0;
			else z->bit++;
		} else if (z->bit <= 8) {
			z->shift |= z->lin << (z->bit - 1);
			z->bit++;
		} else {
			// a timeout up at a pass nobody looked in (zifi_irq_live) is not taken back by this byte
			long long edge = ts - ts % ZF_PASS_NS;
			if ((z->imr & 2) && (edge / ZF_PASS_NS > z->pass) && zf_timed_out(z, edge)) {
				z->imr &= ~2;
				z->isr |= 2;
			}
			if (((z->rxTl - 1) & (ZF_RXF - 1)) != z->rxHd) {
				z->rx[z->rxHd] = z->shift & 0xff;
				z->rxHd = (z->rxHd + 1) & (ZF_RXF - 1);
			}
			z->rxLast = ts;
			z->inFrame = 0;
		}
	}
}

// == rs232_task(): the interrupt sources, once a pass ==

static void zf_task(Computer* comp, long long now) {
	xZifi* z = ZF;
	if ((z->imr & 1) && (zf_ifr(z) >= z->zibtr)) {
		z->imr &= ~1;
		z->isr |= 1;
	}
	// nothing is ever in RS-232's ring: its threshold is met at 0 only, its timeout never
	if ((z->imr & 4) && !z->ribtr) {
		z->imr &= ~4;
		z->isr |= 4;
	}
	if ((z->imr & 2) && zf_timed_out(z, now)) {
		z->imr &= ~2;
		z->isr |= 2;
	}
	// bit 5 of the config register, pulsed every pass while a source is up
	if (z->isr && comp->hw->irq) comp->hw->irq(comp, IRQ_WTP);
}

static void zf_run(Computer* comp, long long now) {
	xZifi* z = ZF;
	tx_run(comp, &z->ztx, now, ZF_BIT16, 11);
	tx_run(comp, &z->rtx, now, z->rsBit, z->rsBits);
	rx_run(z, now);
	long long pass = now / ZF_PASS_NS;
	if (pass > z->pass) {
		zf_task(comp, now);
		z->pass = pass;
	}
}

// nothing is due before the next pass: the UARTs catch up there, or at a port
void zifi_step(Computer* comp) {
	long long now = zifi_now(comp);
	if (now / ZF_PASS_NS > ZF->pass) zf_run(comp, now);
}

long long zifi_sync(Computer* comp) {
	long long now = zifi_now(comp);
	if (ZF->on) zf_run(comp, now);
	return now;
}

// == the module's side ==

// how the module sends: its bit (ns 16.16) and frame; 0 is the AVR's own
void zifi_far(Computer* comp, long long bit16, int frame) {
	ZF->farBit = bit16;
	ZF->farFrame = frame & 0x1f;
}

// bytes from the module, the first of them on the line no sooner than at
void zifi_feed_at(Computer* comp, const unsigned char* b, int n, long long at) {
	xZifi* z = ZF;
	long long now = zifi_now(comp);
	rx_run(z, now);		// what was handed over before is on the line by now, or still waits
	if (!z->hqN) {
		if (at < now) at = now;
		if (at > z->feedAt) z->feedAt = at;
	}
	for (int k = 0; (k < n) && (z->hqN < ZF_HQ); k++) {
		z->hq[(z->hqHead + z->hqN) % ZF_HQ] = b[k];
		z->hqN++;
	}
}

int zifi_feed_room(Computer* comp) {
	return ZF_HQ - ZF->hqN;
}

int zifi_feed_left(Computer* comp) {
	return ZF->hqN;
}

// == the registers ==

// USART1's rate from the 16550's divisor (rs232_set_baud): 115200 over it; DLM b7: the rest is UBRR; 0 is 230400
static void rs_set_baud(xZifi* z) {
	int ubrr;
	int div = (z->dlm << 8) | z->dll;
	if (!div) {
		ubrr = 2;
	} else if (z->dlm & 0x80) {
		ubrr = div & 0x7fff;
	} else {
		int i = 115200 / div;
		ubrr = i ? (int)(ZF_F_CPU / 16 / i) - 1 : 0xffff;
	}
	ubrr &= 0xfff;
	z->rsBit = (long long)(16.0 * (ubrr + 1) * 1e9 * 65536.0 / ZF_F_CPU);
}

// ...and its frame from LCR (rs232_set_format): b1,0 the bits, b2 two stop bits, b3 parity
static void rs_set_format(xZifi* z) {
	int par = ((z->lcr & 0x38) == 0x08) || ((z->lcr & 0x38) == 0x18);
	z->rsBits = 1 + 5 + (z->lcr & 3) + par + 1 + ((z->lcr >> 2) & 1);
}

void zifi_wr(Computer* comp, int hi, int val) {
	xZifi* z = ZF;
	z->io = 1;
	long long now = zifi_now(comp);
	zf_run(comp, now);
	val &= 0xff;
	if (hi <= ZF_LIM) {
		if (z->sel) {
			if (z->api == 1) tx_put(comp, &z->ztx, val, now);
		} else if (z->api) {
			tx_put(comp, &z->rtx, val, now);
		}
	} else switch (hi) {
		case ZF_CR:
			if ((val & 0xf8) == 0xf0) {		// Set API: a mode above the last one is off
				z->api = val & 7;
				if (z->api > 1) z->api = 0;
				z->err = 0;
			} else if (z->api) {
				if (val == 0xff) {		// Get Version
					z->err = 1;
				} else if (!(val & 0xfc)) {	// Clear ZiFi's FIFOs: b0 in, b1 out
					if (val & 1) z->rxHd = z->rxTl = 0;
					if (val & 2) z->ztx.hd = z->ztx.tl = 0;
				} else if ((val & 0xfc) == 4) {	// Clear RS-232's
					if (val & 2) z->rtx.hd = z->rtx.tl = 0;
				}
			}
			break;
		case ZF_ISR: z->imr |= val; break;	// IMR
		case ZF_IBTR: z->zibtr = val; break;
		case ZF_ITOR: z->zitor = val; break;
		case RS_IBTR: z->ribtr = val; break;
		case RS_ITOR: z->ritor = val; break;
		// the 16550
		case UART_DAT:
			if (z->lcr & 0x80) {
				z->dll = val;
				rs_set_baud(z);
			} else {
				tx_put(comp, &z->rtx, val, now);
			}
			break;
		case UART_IER:
			if (z->lcr & 0x80) {
				z->dlm = val;
				rs_set_baud(z);
			} else {
				z->ier = val & 0x0f;
			}
			break;
		case UART_FCR:
			if ((val & 1) && (val & 4)) z->rtx.hd = z->rtx.tl = 0;
			break;
		case UART_LCR:
			z->lcr = val;
			rs_set_format(z);
			break;
		case UART_MCR: z->mcr = val & 0x1f; break;
		case UART_SPR: z->scr = val; break;
	}
	zf_run(comp, now);
	z->io = 0;
}

// a count as a register shows it
static int zf_cnt(int n) {
	return (n > ZF_LIM) ? ZF_LIM : n;
}

int zifi_rd(Computer* comp, int hi) {
	xZifi* z = ZF;
	z->io = 1;
	long long now = zifi_now(comp);
	zf_run(comp, now);
	z->io = 0;
	int res = 0xff;
	if (hi <= ZF_LIM) {
		// RS-232's ring is empty for good
		if (z->sel && (z->api == 1) && (z->rxTl != z->rxHd)) {
			res = z->rx[z->rxTl];
			z->rxTl = (z->rxTl + 1) & (ZF_RXF - 1);
		}
	} else if (hi <= ZF_REG_LIM) {
		if (z->api) switch (hi) {
			case ZF_IFR:
				z->sel = 1;
				res = zf_cnt(zf_ifr(z));
				break;
			case ZF_OFR:
				z->sel = 1;
				res = zf_cnt(tx_free(&z->ztx));
				break;
			case RS_IFR:
				z->sel = 0;
				res = 0;
				break;
			case RS_OFR:
				z->sel = 0;
				res = zf_cnt(tx_free(&z->rtx));
				break;
			case ZF_ISR:
				res = z->isr;
				z->isr = 0;
				break;
			case ZF_IBTR: res = z->zibtr; break;
			case ZF_ITOR: res = z->zitor; break;
			case ZF_CR: res = z->err; break;	// ER
			case RS_IBTR: res = z->ribtr; break;
			case RS_ITOR: res = z->ritor; break;
		}
	} else {
		res = 0;
		switch (hi) {
			case UART_DAT: if (z->lcr & 0x80) res = z->dll; break;
			case UART_IER: res = (z->lcr & 0x80) ? z->dlm : z->ier; break;
			case UART_FCR: res = 0x01; break;	// ISR: nothing pending
			case UART_LCR: res = z->lcr; break;
			case UART_MCR: res = z->mcr; break;
			// LSR: b5 room in the ring, b6 a byte has gone out (TXC1 is never cleared)
			case UART_LSR:
				res = (tx_free(&z->rtx) ? 0x20 : 0) | ((z->rsTxc && (now >= z->rsTxc)) ? 0x40 : 0);
				break;
			case UART_MSR: res = 0xa0; break;	// DSR and CD as the firmware ties them, CTS off
			case UART_SPR: res = z->scr; break;
		}
	}
	return res;
}

// a snapshot's state is a file's: whatever it says, the rings' places stay inside them
void zifi_loaded(Computer* comp) {
	xZifi* z = ZF;
	z->rxHd &= ZF_RXF - 1;
	z->rxTl &= ZF_RXF - 1;
	z->hqHead = (unsigned)z->hqHead % ZF_HQ;
	if ((z->hqN < 0) || (z->hqN > ZF_HQ)) z->hqN = 0;
	z->schHead = (unsigned)z->schHead % ZF_SCH;
	if ((z->schN < 0) || (z->schN > ZF_SCH)) z->schN = 0;
	if ((z->bit < 0) || (z->bit > 9)) z->inFrame = z->bit = 0;
	z->farFrame &= 0x1f;
	z->lin &= 1;
	z->schLv &= 1;
	z->io = 0;
	if (z->rsBit < 1) z->rsBit = ZF_BIT16;
	if ((z->rsBits < 7) || (z->rsBits > 12)) z->rsBits = 11;
}

// == the AVR starts ==

static void zf_defaults(xZifi* z) {
	memset(z, 0, sizeof(xZifi));
	z->zibtr = z->ribtr = 0x80;
	z->zitor = z->ritor = 1;
	z->dll = 0x01;
	z->scr = 0xff;
	z->lin = z->schLv = 1;
	z->rsBit = ZF_BIT16;
	z->rsBits = 11;
}

void zifi_power(Computer* comp) {
	xZifi* z = ZF;
	int on = z->on;
	long long off = z->off;
	zf_defaults(z);
	z->on = on;
	z->off = off;
	long long now = zifi_now(comp);
	z->rxLast = now;
	z->pass = now / ZF_PASS_NS;
}

// the AVR as it starts, and no interrupt of its asked for
void zifi_absent(Computer* comp) {
	comp->TSL_FLG_WINT = 0;
	if (ZF->on) zifi_power(comp);
}

// taken out with its interrupt asked for: the request goes with it
void zifi_fit(Computer* comp, int on) {
	if (on == ZF->on) return;
	ZF->on = on;
	if (on) zifi_power(comp);
	if (!hw_takes_zifi(comp->hw)) return;
	comp->TSL_FLG_WINT = 0;
	if (!comp->TSL_FLG_LINT && !comp->TSL_FLG_DINT && !comp->vid->intFRAME) comp->cpu->intrq &= ~Z80_INT;
}

xZifi* zifi_create(void) {
	xZifi* z = (xZifi*)malloc(sizeof(xZifi));
	zf_defaults(z);
	return z;
}

void zifi_destroy(xZifi* z) {
	free(z);
}
