// the lines an ESP-01 hangs on (xEspHost, esp.h), one a machine: the Next's UART 0 and the ZX Evo's ZiFi

#include "esp.h"
#include "../libxpeccy/spectrum.h"
#include "../libxpeccy/hardware/hardware.h"

// == the Next: UART 0, time in CLK_28 edges ==

#define NX_ESP_UART	0

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

static void nxh_pins(int v) {next_esp_pins = v;}
static void nxh_fitted(int on) {next_esp_fitted = on;}

static const xEspHost nxHost = {
	"UART 0", nx_clk28_hz, nx_now, nx_uart_sync, nxh_tx_cfg,
	nxh_rx_room, nxh_rx_left, nxh_rx_rate, nxh_rx_feed,
	nx_esp_reset, nx_esp_gpio0, nx_esp_gpio2, nxh_pins, nxh_fitted
};

// == the ZX Evo's ZiFi: the AVR's USART0, time in ns ==
// the module is on the AVR's ISP header by GND, RX and TX: the board has no reset line to it and sees neither pin

static double zfh_clk(Computer*) {return 1e9;}
static int zfh_reset(Computer*) {return 0;}
static int zfh_pin(Computer*) {return 1;}		// pulled up on the module
static void zfh_pins(int) {}
static void zfh_fitted(int) {}

static const xEspHost zfHost = {
	"ZiFi", zfh_clk, zifi_now, zifi_sync, zifi_tx_cfg,
	zifi_feed_room, zifi_feed_left, zifi_far, zifi_feed_at,
	zfh_reset, zfh_pin, zfh_pin, zfh_pins, zfh_fitted
};

const xEspHost* esp_host_of(Computer* comp) {
	if (!comp || !comp->hw) return nullptr;
	if (comp->hw->id == HW_ZXNEXT) return &nxHost;
	return hw_zifi_on(comp) ? &zfHost : nullptr;
}
