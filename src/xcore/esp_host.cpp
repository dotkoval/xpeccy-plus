// The lines an ESP-01 hangs on (xEspHost, esp.h), one a machine: the Next's UART 0, whose
// far end the module is, and the ZX Evo's ZiFi, the AVR's second UART.

#include "esp.h"
#include "../libxpeccy/spectrum.h"
#include "../libxpeccy/hardware/hardware.h"

// == the Next: UART 0, time in CLK_28 edges ==

#define NX_ESP_UART	0

static double nxh_clk(Computer* comp) {return nx_clk28_hz(comp);}
static long long nxh_now(Computer* comp) {return nx_now(comp);}
static long long nxh_sync(Computer* comp) {return nx_uart_sync(comp);}

static void nxh_tx_cfg(Computer* comp, long* bit, int* frame) {
	int rtr;
	nx_uart_cfg(comp, NX_ESP_UART, bit, frame, &rtr);
}

static int nxh_rx_room(Computer* comp) {return nx_uart_feed_room(comp, NX_ESP_UART);}
static int nxh_rx_left(Computer* comp) {return nx_uart_feed_left(comp, NX_ESP_UART);}
static void nxh_rx_rate(Computer* comp, long long bit16, int frame) {nx_uart_far(comp, NX_ESP_UART, bit16, frame);}

static void nxh_rx_feed(Computer* comp, const unsigned char* b, int n, long long at) {
	nx_uart_feed_at(comp, NX_ESP_UART, b, n, at);
}

static int nxh_reset(Computer* comp) {return nx_esp_reset(comp);}	// NR 02 b7
static int nxh_gpio0(Computer* comp) {return nx_esp_gpio0(comp);}	// NR A8/A9
static int nxh_gpio2(Computer* comp) {return nx_esp_gpio2(comp);}
static void nxh_pins(int v) {next_esp_pins = v;}
static void nxh_fitted(int on) {next_esp_fitted = on;}

static const xEspHost nxHost = {
	"UART 0", nxh_clk, nxh_now, nxh_sync, nxh_tx_cfg,
	nxh_rx_room, nxh_rx_left, nxh_rx_rate, nxh_rx_feed,
	nxh_reset, nxh_gpio0, nxh_gpio2, nxh_pins, nxh_fitted
};

// == the ZX Evo's ZiFi: the AVR's USART0, time in ns ==
// The module hangs on the AVR's ISP header by GND, RX and TX, fed from the standby supply
// with CH_PD tied high: the board has no reset line to it and sees neither of its pins.

static double zfh_clk(Computer*) {return 1e9;}
static long long zfh_now(Computer* comp) {return zifi_now(comp);}
static long long zfh_sync(Computer* comp) {return zifi_sync(comp);}
static void zfh_tx_cfg(Computer*, long* bit, int* frame) {zifi_tx_cfg(bit, frame);}
static int zfh_rx_room(Computer* comp) {return zifi_feed_room(comp);}
static int zfh_rx_left(Computer* comp) {return zifi_feed_left(comp);}
static void zfh_rx_rate(Computer* comp, long long bit16, int frame) {zifi_far(comp, bit16, frame);}

static void zfh_rx_feed(Computer* comp, const unsigned char* b, int n, long long at) {
	zifi_feed_at(comp, b, n, at);
}

static int zfh_reset(Computer*) {return 0;}
static int zfh_pin(Computer*) {return 1;}		// pulled up on the module
static void zfh_pins(int) {}
static void zfh_fitted(int) {}

static const xEspHost zfHost = {
	"ZiFi", zfh_clk, zfh_now, zfh_sync, zfh_tx_cfg,
	zfh_rx_room, zfh_rx_left, zfh_rx_rate, zfh_rx_feed,
	zfh_reset, zfh_pin, zfh_pin, zfh_pins, zfh_fitted
};

const xEspHost* esp_host_of(Computer* comp) {
	if (!comp || !comp->hw) return nullptr;
	if (comp->hw->id == HW_ZXNEXT) return &nxHost;
	if ((comp->hw->id == HW_TSLAB) && comp->zifi->on) return &zfHost;
	return nullptr;
}
