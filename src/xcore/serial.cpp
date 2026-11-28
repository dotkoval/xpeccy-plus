// The emulated ESP-01 on the machine that has a line for it (the Next's UART 0, the Evo's
// ZiFi), and the Next's UARTs to the host's serial ports. A port follows what the guest asks of its
// UART: the rate (the prescaler against CLK_28, snapped to a standard rate), the frame,
// RTS from the Rx FIFO with flow control on, break; its CTS goes back to the guest. The
// bytes are moved at frame ends on the emulation thread, both ways traced to the event log
// (NET, debug). On Windows a name may also be a named pipe (\\.\pipe\...), which is a plain
// stream with no rate or modem lines: what a script on the other end needs to stand in for
// a device.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include <QDir>

#include "serial.h"
#include "esp.h"
#include "pacing.h"
#include "xcore.h"
#include "../libxpeccy/spectrum.h"
#include "../libxpeccy/hardware/hardware.h"
#include "../libxpeccy/xlog.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <errno.h>
#endif

#define SER_TXQ_MAX	65536
#define SER_RETRY_NS	2000000000LL	// a port that would not open is tried again this often

static const char* ser_uart_name[2] = {"esp", "pi"};

typedef struct {
	std::string name;		// open on this, empty: closed
	bool tty;			// a serial device: it has a rate, a frame and modem lines
#ifdef _WIN32
	HANDLE h;
#else
	int fd;
#endif
	long baud;			// set on the port, 0 not yet
	int frame;			// the guest's frame bits it was set for, -1 not yet
	int rts, brk, cts;		// the lines as last set or seen, -1 not yet
	std::string failed;		// the name that would not open...
	long long retry;		// ...and when to try it again
	std::vector<unsigned char> txq;	// what the guest sent, still to be written
} serPort;

static serPort ser[2];
static std::mutex serLock;		// want[] and cli[]: the gui writes them, the emulation reads
static std::string serWant[2] = {"esp", ""};	// a Next Plus has its ESP-01 fitted
enum {SER_ESP_NONE = 0, SER_ESP_NEXT, SER_ESP_ZIFI};
static int serEsp = SER_ESP_NONE;		// the emulated ESP is on: the Next's UART 0, the Evo's ZiFi
static std::string serCli[2];
static int serCliSet[2] = {0, 0};

// == the trace ==

static void ser_trace(int i, const char* dir, const unsigned char* b, int n) {
	if (!xlog_on(XLG_NET, XLL_DEBUG)) return;
	for (int k = 0; k < n; k += 16) {
		char hex[16 * 3 + 1], txt[17];
		int m = (n - k > 16) ? 16 : n - k;
		for (int j = 0; j < m; j++) {
			snprintf(hex + j * 3, 4, "%02X ", b[k + j]);
			txt[j] = ((b[k + j] >= 0x20) && (b[k + j] < 0x7f)) ? b[k + j] : '.';
		}
		txt[m] = 0;
		xlog(XLG_NET, XLL_DEBUG, "uart%i %s %s: %-48s|%s|", i, dir, ser[i].name.c_str(), hex, txt);
	}
}

// == the host's side, per system ==

// the rate the guest's prescaler makes, a standard one when it is within 3% of it
static long ser_rate(Computer* comp, long presc) {
	static const long std_rate[] = {300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 31250, 38400, 57600,
		115200, 230400, 250000, 460800, 500000, 921600, 1000000, 1152000, 1500000, 2000000, 3000000, 0};
	if (presc < 1) presc = 1;
	double r = nx_clk28_hz(comp) / presc;
	for (int k = 0; std_rate[k]; k++) {
		if ((r > std_rate[k] * 0.97) && (r < std_rate[k] * 1.03)) return std_rate[k];
	}
	return (long)(r + 0.5);
}

#ifdef _WIN32

static bool ser_open(serPort* p, const std::string& name) {
	std::string path = name;
	if (path.compare(0, 4, "\\\\.\\") != 0) path = "\\\\.\\" + path;
	p->tty = (path.compare(0, 9, "\\\\.\\pipe\\") != 0);
	p->h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (p->h == INVALID_HANDLE_VALUE) return false;
	if (p->tty) {
		SetupComm(p->h, 4096, 4096);
		COMMTIMEOUTS to = {};
		to.ReadIntervalTimeout = MAXDWORD;	// a read takes what is there and returns
		to.WriteTotalTimeoutConstant = 1;	// a write as much as goes at once
		SetCommTimeouts(p->h, &to);
		PurgeComm(p->h, PURGE_RXCLEAR | PURGE_TXCLEAR);
	} else {
		DWORD mode = PIPE_READMODE_BYTE;
		SetNamedPipeHandleState(p->h, &mode, NULL, NULL);
	}
	return true;
}

static void ser_shut(serPort* p) {
	if (p->h != INVALID_HANDLE_VALUE) CloseHandle(p->h);
	p->h = INVALID_HANDLE_VALUE;
}

static bool ser_setup(serPort* p, long baud, int frame) {
	DCB dcb = {};
	dcb.DCBlength = sizeof(dcb);
	if (!GetCommState(p->h, &dcb)) return false;
	dcb.BaudRate = baud;
	dcb.ByteSize = 5 + ((frame >> 3) & 3);
	dcb.fParity = (frame & 4) ? TRUE : FALSE;
	dcb.Parity = !(frame & 4) ? NOPARITY : (frame & 2) ? ODDPARITY : EVENPARITY;
	dcb.StopBits = (frame & 1) ? TWOSTOPBITS : ONESTOPBIT;
	dcb.fBinary = TRUE;
	dcb.fOutxCtsFlow = FALSE;		// the guest waits for CTS itself
	dcb.fOutxDsrFlow = FALSE;
	dcb.fDtrControl = DTR_CONTROL_ENABLE;
	dcb.fRtsControl = RTS_CONTROL_ENABLE;
	dcb.fOutX = dcb.fInX = FALSE;
	dcb.fNull = FALSE;
	dcb.fAbortOnError = FALSE;
	return SetCommState(p->h, &dcb) != 0;
}

static void ser_rts(serPort* p, int on) {
	EscapeCommFunction(p->h, on ? SETRTS : CLRRTS);
}

static void ser_break(serPort* p, int on) {
	if (on) SetCommBreak(p->h); else ClearCommBreak(p->h);
}

static int ser_cts(serPort* p) {
	DWORD st = 0;
	if (!GetCommModemStatus(p->h, &st)) return 1;
	return (st & MS_CTS_ON) ? 1 : 0;
}

// -1: the port is gone
static int ser_read(serPort* p, unsigned char* b, int n) {
	DWORD got = 0;
	if (!p->tty) {
		DWORD avail = 0;
		if (!PeekNamedPipe(p->h, NULL, 0, NULL, &avail, NULL)) return -1;
		if (!avail) return 0;
		if (avail < (DWORD)n) n = avail;
	}
	if (!ReadFile(p->h, b, n, &got, NULL)) {
		DWORD err = GetLastError();
		if (p->tty && (err != ERROR_OPERATION_ABORTED)) {
			DWORD e;
			ClearCommError(p->h, &e, NULL);
			return 0;
		}
		return -1;
	}
	return (int)got;
}

static int ser_write(serPort* p, const unsigned char* b, int n) {
	DWORD put = 0;
	if (!WriteFile(p->h, b, n, &put, NULL)) {
		if (!p->tty) return -1;
		DWORD e;
		ClearCommError(p->h, &e, NULL);
	}
	return (int)put;
}

static bool ser_is_open(serPort* p) {return p->h != INVALID_HANDLE_VALUE;}

#else

static speed_t ser_speed(long baud) {
	static const struct {long b; speed_t s;} tab[] = {
		{300, B300}, {600, B600}, {1200, B1200}, {2400, B2400}, {4800, B4800}, {9600, B9600},
		{19200, B19200}, {38400, B38400}, {57600, B57600}, {115200, B115200}, {230400, B230400},
#ifdef B460800
		{460800, B460800}, {921600, B921600},
#endif
		{0, B0}};
	int best = 0;
	for (int k = 1; tab[k].b; k++) {
		if (labs(tab[k].b - baud) < labs(tab[best].b - baud)) best = k;
	}
	return tab[best].s;
}

static bool ser_open(serPort* p, const std::string& name) {
	p->fd = open(name.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (p->fd < 0) return false;
	p->tty = isatty(p->fd);
	return true;
}

static void ser_shut(serPort* p) {
	if (p->fd >= 0) close(p->fd);
	p->fd = -1;
}

static bool ser_setup(serPort* p, long baud, int frame) {
	struct termios t;
	if (tcgetattr(p->fd, &t) < 0) return false;
	cfmakeraw(&t);
	speed_t s = ser_speed(baud);
	cfsetispeed(&t, s);
	cfsetospeed(&t, s);
	static const tcflag_t csz[4] = {CS5, CS6, CS7, CS8};
	t.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
	t.c_cflag |= csz[(frame >> 3) & 3] | CLOCAL | CREAD;
	if (frame & 4) t.c_cflag |= PARENB | ((frame & 2) ? PARODD : 0);
	if (frame & 1) t.c_cflag |= CSTOPB;
	t.c_cc[VMIN] = 0;
	t.c_cc[VTIME] = 0;
	return tcsetattr(p->fd, TCSANOW, &t) == 0;
}

static void ser_rts(serPort* p, int on) {
	int bit = TIOCM_RTS;
	ioctl(p->fd, on ? TIOCMBIS : TIOCMBIC, &bit);
}

static void ser_break(serPort* p, int on) {
	ioctl(p->fd, on ? TIOCSBRK : TIOCCBRK, 0);
}

static int ser_cts(serPort* p) {
	int st = 0;
	if (ioctl(p->fd, TIOCMGET, &st) < 0) return 1;		// a pty has no modem lines
	return (st & TIOCM_CTS) ? 1 : 0;
}

static int ser_read(serPort* p, unsigned char* b, int n) {
	ssize_t got = read(p->fd, b, n);
	if (got > 0) return (int)got;
	if (got == 0) return p->tty ? 0 : -1;
	return ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR)) ? 0 : -1;
}

static int ser_write(serPort* p, const unsigned char* b, int n) {
	ssize_t put = write(p->fd, b, n);
	if (put >= 0) return (int)put;
	return ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR)) ? 0 : -1;
}

static bool ser_is_open(serPort* p) {return p->fd >= 0;}

#endif

// == both ==

// the ESP's when one is in its socket: a snapshot loaded before the first frame puts one there
// ahead of ser_port_frame
static void ser_host_tx(Computer* comp, int i, int b, long long start) {
	if ((i == 0) && next_esp_fitted) {
		esp_byte(comp, b, start);
		return;
	}
	if ((i < 0) || (i > 1) || !ser_is_open(&ser[i])) return;
	if (ser[i].txq.size() < SER_TXQ_MAX) ser[i].txq.push_back((unsigned char)b);
}

// what the Evo's AVR sends to its module
static void ser_zifi_tx(Computer* comp, int b, long long start) {
	esp_byte(comp, b, start);
}

// the guest's bytes are taken from the start, not from the first frame on
static struct serHooks {
	serHooks() {
		next_uart_host_tx = ser_host_tx;
		zifi_host_tx = ser_zifi_tx;
	}
} serHooksSet;

// the module onto this machine's line, with the flash that is this board's own
static void ser_esp_on(Computer* comp, int kind, const char* flash) {
	if (serEsp != kind) {		// from another machine's line too: esp_attach() takes it off that one
		esp_flash_file((conf.path.nvDir + SLASH + flash).c_str());
		esp_attach(comp);
	}
	esp_set_lan(conf.next.espLan);		// for the servers it opens from now on
	serEsp = kind;
	esp_frame(comp);
}

static void ser_reset_state(serPort* p) {
	p->baud = 0;
	p->frame = -1;
	p->rts = p->brk = p->cts = -1;
	p->txq.clear();
}

static void ser_drop(Computer* comp, int i, const char* why) {
	serPort* p = &ser[i];
	if (!ser_is_open(p)) return;
	xlog(XLG_NET, XLL_INFO, "uart%i: %s closed%s%s", i, p->name.c_str(), why ? ", " : "", why ? why : "");
	ser_shut(p);
	p->name.clear();
	ser_reset_state(p);
	if (comp && comp->hw && (comp->hw->id == HW_ZXNEXT)) nx_uart_cts(comp, i, 0);
}

static std::string ser_wanted(int i) {
	std::lock_guard<std::mutex> lk(serLock);
	return serCliSet[i] ? serCli[i] : serWant[i];
}

static void ser_port_frame(Computer* comp, int i) {
	serPort* p = &ser[i];
	std::string want = ser_wanted(i);
	if ((i == 0) && (want == SERIAL_ESP)) {
		if (ser_is_open(p)) ser_drop(comp, i, NULL);
		ser_esp_on(comp, SER_ESP_NEXT, "esp01.conf");
		return;
	}
	if ((i == 0) && serEsp) {
		esp_detach(comp);
		serEsp = SER_ESP_NONE;
	}
	if (i == 0) esp_forget_snapshot();	// a snapshot's module: none plugged in to take it
	if (p->name != want) ser_drop(comp, i, NULL);
	if (want.empty()) {
		p->failed.clear();
		return;
	}
	if (!ser_is_open(p)) {
		long long now = paceClockNs();
		if ((p->failed == want) && (now < p->retry)) return;
		if (!ser_open(p, want)) {
			if (p->failed != want)
				xlog(XLG_NET, XLL_WARN, "uart%i: cannot open %s", i, want.c_str());
			p->failed = want;
			p->retry = now + SER_RETRY_NS;
			return;
		}
		p->failed.clear();
		p->name = want;
		ser_reset_state(p);
		xlog(XLG_NET, XLL_INFO, "uart%i (%s): %s open", i, ser_uart_name[i], want.c_str());
	}
	long presc;
	int frame, rtr_n;
	nx_uart_cfg(comp, i, &presc, &frame, &rtr_n);
	frame &= 0x7f;
	if (p->tty) {
		long baud = ser_rate(comp, presc);
		int fbits = frame & 0x1f;
		if ((baud != p->baud) || (fbits != p->frame)) {
			if (ser_setup(p, baud, fbits))
				xlog(XLG_NET, XLL_INFO, "uart%i: %s at %li, %i%c%i", i, p->name.c_str(), baud, 5 + ((fbits >> 3) & 3),
					!(fbits & 4) ? 'N' : (fbits & 2) ? 'O' : 'E', (fbits & 1) ? 2 : 1);
			else
				xlog(XLG_NET, XLL_WARN, "uart%i: %s does not take %li, %02X", i, p->name.c_str(), baud, fbits);
			p->baud = baud;
			p->frame = fbits;
			p->rts = p->brk = -1;
		}
		int rts = !((frame & 0x20) && rtr_n);
		if (rts != p->rts) ser_rts(p, rts);
		p->rts = rts;
		int brk = (frame & 0x40) ? 1 : 0;
		if (brk != p->brk) ser_break(p, brk);
		p->brk = brk;
		int cts = ser_cts(p);
		if (cts != p->cts) nx_uart_cts(comp, i, !cts);
		p->cts = cts;
	}
	// what the guest sent
	if (!p->txq.empty()) {
		int put = ser_write(p, p->txq.data(), (int)p->txq.size());
		if (put < 0) {
			ser_drop(comp, i, "the port is gone");
			p->failed = want;
			p->retry = paceClockNs() + SER_RETRY_NS;
			return;
		}
		ser_trace(i, "->", p->txq.data(), put);
		p->txq.erase(p->txq.begin(), p->txq.begin() + put);
	}
	// what comes in, as much as the line can take before the next frame end
	unsigned char buf[1024];
	int room = nx_uart_feed_room(comp, i);
	if (room > (int)sizeof(buf)) room = sizeof(buf);
	if (room > 0) {
		int got = ser_read(p, buf, room);
		if (got < 0) {
			ser_drop(comp, i, "the port is gone");
			p->failed = want;
			p->retry = paceClockNs() + SER_RETRY_NS;
			return;
		}
		if (got > 0) {
			ser_trace(i, "<-", buf, got);
			nx_uart_feed(comp, i, buf, got);
		}
	}
}

void serial_frame(Computer* comp) {
	static bool hooked = false;
	if (!hooked) {
#ifdef _WIN32
		ser[0].h = ser[1].h = INVALID_HANDLE_VALUE;
#else
		ser[0].fd = ser[1].fd = -1;
#endif
		ser_reset_state(&ser[0]);
		ser_reset_state(&ser[1]);
		hooked = true;
	}
	bool next = comp && comp->hw && (comp->hw->id == HW_ZXNEXT);
	if (hw_zifi_on(comp)) {
		ser_esp_on(comp, SER_ESP_ZIFI, "esp01-zifi.conf");
	} else if (!next && serEsp) {
		esp_detach(comp);
		serEsp = SER_ESP_NONE;
	}
	for (int i = 0; i < 2; i++) {
		if (next) ser_port_frame(comp, i);
		else ser_drop(NULL, i, NULL);
	}
}

void serial_set(int i, const QString& name, bool user) {
	if ((i < 0) || (i > 1)) return;
	std::lock_guard<std::mutex> lk(serLock);
	serWant[i] = name.trimmed().toLocal8Bit().constData();
	if (user) serCliSet[i] = 0;
}

QString serial_conf(int i) {
	if ((i < 0) || (i > 1)) return QString();
	std::lock_guard<std::mutex> lk(serLock);
	return QString::fromLocal8Bit(serWant[i].c_str());
}

void serial_cli(int i, const QString& name) {
	if ((i < 0) || (i > 1)) return;
	std::lock_guard<std::mutex> lk(serLock);
	serCli[i] = name.trimmed().toLocal8Bit().constData();
	serCliSet[i] = 1;
}

QString serial_name(int i) {
	if ((i < 0) || (i > 1)) return QString();
	std::lock_guard<std::mutex> lk(serLock);
	return QString::fromLocal8Bit((serCliSet[i] ? serCli[i] : serWant[i]).c_str());
}

QStringList serial_ports() {
	QStringList lst;
#ifdef _WIN32
	HKEY key;
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &key) == ERROR_SUCCESS) {
		char nam[256];
		unsigned char val[256];
		for (DWORD k = 0; ; k++) {
			DWORD nlen = sizeof(nam), vlen = sizeof(val) - 1, type;
			if (RegEnumValueA(key, k, nam, &nlen, NULL, &type, val, &vlen) != ERROR_SUCCESS) break;
			if (type != REG_SZ) continue;
			val[vlen] = 0;
			lst << QString::fromLocal8Bit((const char*)val);
		}
		RegCloseKey(key);
	}
	// COM2 before COM10
	std::sort(lst.begin(), lst.end(), [](const QString& a, const QString& b) {
		if (a.size() != b.size()) return a.size() < b.size();
		return a < b;
	});
#else
	QDir dev("/dev");
#ifdef __APPLE__
	QStringList pat = QStringList() << "cu.*";
#else
	QStringList pat = QStringList() << "ttyUSB*" << "ttyACM*" << "ttyS*" << "rfcomm*";
#endif
	foreach(QString f, dev.entryList(pat, QDir::System | QDir::Files, QDir::Name))
		lst << dev.filePath(f);
#endif
	return lst;
}

void serial_close() {
	if (serEsp) esp_detach(NULL);
	serEsp = SER_ESP_NONE;
	for (int i = 0; i < 2; i++) ser_drop(NULL, i, NULL);
}
