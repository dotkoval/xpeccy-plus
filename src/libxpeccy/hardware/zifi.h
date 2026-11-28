#pragma once

// ZiFi: the ZX Evo's AVR as TSConf's #xxEF ports show it (the TS firmware's rs232.c); the FPGA's side is tslab.c's

#ifdef __cplusplus
extern "C" {
#endif

struct Computer;

#define ZF_RXF	512		// a receive ring: 511 bytes held
#define ZF_TXF	256		// a transmit ring: 255 bytes held
#define ZF_HQ	8192		// from the module, not on the line yet: a message goes in whole
#define ZF_SCH	64

// one of the AVR's transmitters: the ring, the data register and the shifter behind it
typedef struct {
	unsigned char buf[ZF_TXF];
	unsigned char hd, tl;		// written at hd, sent from tl
	unsigned char udrFull, udr;	// the byte waiting for the shifter
	long long udrFree;		// the data register emptied
	long long shEnd;		// the shifter is busy until
} zfTx;

typedef struct {
	long long t;
	int lv;
} zfEdge;

typedef struct {
	int on;				// fitted: the ports answer and a module is on the line
	long long off;			// its clock is the machine's ns plus this
	int io;				// inside a port access (zifi_now)
	// rs232.c's own
	unsigned char sel;		// DR serves ZiFi (a FIFO register of its was read last)
	unsigned char imr, isr, api, err;
	unsigned char zibtr, zitor, ribtr, ritor;
	unsigned char dll, dlm, ier, lcr, mcr, scr;
	long long pass;			// the main loop's pass the interrupt sources were last looked at in
	// the UART to the module: 115200, 8 bits, 2 stop bits out
	zfTx ztx;
	unsigned char rx[ZF_RXF];
	int rxHd, rxTl;
	long long rxLast;		// a byte last came: the timeout counts from here
	// the line from the module and the receiver on it
	unsigned char hq[ZF_HQ];
	int hqHead, hqN;
	long long farBit;		// the module's bit, ns 16.16 (0: the AVR's own)
	int farFrame;
	long long feedAt, lineFree;
	zfEdge sch[ZF_SCH];		// level changes to come
	int schHead, schN;
	int schLv;			// the level after the last of them
	int lin;			// the level now
	int inFrame, bit, shift;
	long long t0;			// the start bit's edge
	// the UART to the RS-232 connector, nothing on its line
	zfTx rtx;
	long long rsBit;		// its bit, ns 16.16
	int rsBits;			// bits a frame
	long long rsTxc;		// its first byte was out by then (0: none sent yet)
} xZifi;

// a source may raise the interrupt (inten: TSConf's INTMask, b3 lets it through): stepped for it, else worked out at a port
static inline int zifi_irq_live(const xZifi* z, int inten) {
	if (!(inten & 8)) return 0;
	if (z->isr || ((z->imr & 4) && !z->ribtr)) return 1;
	int held = (z->rxHd - z->rxTl) & (ZF_RXF - 1);
	int coming = z->hqN || z->schN || z->inFrame;
	return ((z->imr & 1) && (coming || (held >= z->zibtr))) || ((z->imr & 2) && (coming || held));
}

xZifi* zifi_create(void);
void zifi_destroy(xZifi*);
void zifi_power(struct Computer*);		// the AVR starts: rs232_init()
void zifi_fit(struct Computer*, int on);	// fitted or taken out
void zifi_absent(struct Computer*);		// a file had none of it
void zifi_loaded(struct Computer*);		// its state came from a file: kept inside its rings

// a register as the AVR is handed it (the high byte of the port), after WAIT
int zifi_rd(struct Computer*, int hi);
void zifi_wr(struct Computer*, int hi, int val);
void zifi_step(struct Computer*);		// an instruction went by with zifi_irq_live
int zifi_wait_ns(void);				// how long the AVR takes to answer

// the module's side: time is zifi_now()'s ns
long long zifi_now(struct Computer*);
long long zifi_sync(struct Computer*);		// both UARTs run up to now, which is returned
extern void (*zifi_host_tx)(struct Computer*, int byte, long long start);	// a byte to the module, from its start bit
void zifi_tx_cfg(struct Computer*, long* bit, int* frame);	// that transmitter's bit (ns) and frame
void zifi_far(struct Computer*, long long bit16, int frame);	// how the module sends; 0: as the AVR
void zifi_feed_at(struct Computer*, const unsigned char*, int, long long at);	// none on the line before at
int zifi_feed_room(struct Computer*);
int zifi_feed_left(struct Computer*);

#ifdef __cplusplus
}
#endif
