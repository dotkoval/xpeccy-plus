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

const xEspHost* esp_host_of(Computer* comp) {
	if (!comp || !comp->hw) return nullptr;
	if (comp->hw->id == HW_ZXNEXT) return &nxHost;
	return nullptr;
}
