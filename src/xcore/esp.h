#pragma once

// an ESP-01 with Espressif's NONOS AT 1.x firmware, its network the host's, on a machine's serial line (xEspHost, esp_host.cpp)

struct Computer;

// what the module asks of its line: time is the host's own count, clk() a second; a frame as uart_frame_levels() takes it
typedef struct {
	const char* name;			// for the log
	double (*clk)(Computer*);
	long long (*now)(Computer*);
	long long (*sync)(Computer*);		// the line run up to now, which is returned
	void (*tx_cfg)(Computer*, long* bit, int* frame);	// how the guest sends: its bit, its frame
	// what the module sends, to the guest's receiver
	int (*rx_room)(Computer*);		// bytes that can still be handed over
	int (*rx_left)(Computer*);		// handed over, not on the line yet
	void (*rx_rate)(Computer*, long long bit16, int frame);	// the module's bit (16.16) and frame; 0: the guest's own
	void (*rx_feed)(Computer*, const unsigned char*, int, long long at);	// none on the line before at
	// the module's pins as the board wires them
	int (*reset)(Computer*);		// held in reset
	int (*gpio0)(Computer*);		// the level on the pin
	int (*gpio2)(Computer*);
	void (*pins)(int);			// what the module drives: b0 GPIO0 out, b1 its level, b2 GPIO2 out, b3 its level
	void (*fitted)(int);			// one is in its socket
} xEspHost;

const xEspHost* esp_host_of(Computer* comp);	// the line this machine has for it, NULL: none

void esp_flash_file(const char* path);		// what its flash is kept in; before esp_attach
void esp_set_lan(bool on);			// its server listens on every interface (else 127.0.0.1)
void esp_attach(Computer* comp);		// plugged in: it boots
void esp_detach(Computer* comp);		// taken out; comp may be NULL
void esp_byte(Computer* comp, int byte, long long start);	// a byte the guest sends
void esp_frame(Computer* comp);			// frame end: timers, the network, the line
void esp_forget_snapshot();			// a snapshot's module state waiting for one plugged in: none is
