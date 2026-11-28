#pragma once

// A byte on a serial line as its levels, a bit each: start, data from b0, parity, stop bits.
// fr as the Next's uart_framing_r: b4,3 bits - 5, b2 parity, b1 odd, b0 two stop bits. lv[12].
static inline int uart_frame_levels(int b, int fr, int* lv) {
	int bits = 5 + ((fr >> 3) & 3);
	int parity = (fr >> 1) & 1;
	int n = 0;
	lv[n++] = 0;
	for (int k = 0; k < bits; k++) {
		lv[n++] = (b >> k) & 1;
		parity ^= (b >> k) & 1;
	}
	if (fr & 4) lv[n++] = parity;
	lv[n++] = 1;
	if (fr & 1) lv[n++] = 1;
	return n;
}
