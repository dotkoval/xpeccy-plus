// Headless bench for the emulated ESP-01: the Next with the ESP on UART 0, wired as the
// program wires it (every byte the UART sends goes to esp_byte, esp_frame at each frame
// end), a guest program talking AT to it, and a TCP server on 127.0.0.1.
//
//	esp-bench <prog.bin> <frames>
//
// prog.bin is put at 0 of the SRAM (ROM0: the program starts at PC 0). It prints with an
// OUT of one character to 0x2003 and ends the run with an OUT to 0x0103. The server's port
// is written at 0x3F00 as decimal text; the output shows that port as <port>, so it reads
// the same every run. FLASH=file gives the ESP a flash file (none: it keeps nothing).
//
// SCRIPT=file (in core/esp/scripts) runs a script through bridge.bin instead: the guest
// passes bytes between the bench and the UART, and the bench prints what was sent (>>) and
// what came back (<<), escaped. One step a line, # starts a comment:
//	send TEXT        TEXT and \r\n to the ESP        raw TEXT     TEXT alone
//	wait TEXT [ms]   till TEXT has come (10 s)       quiet MS     till nothing came for MS
//	pause MS         let MS go by                    print TEXT   TEXT into the output
//	nr R V           nextreg R := V (hex)            nrget R      nextreg R read, printed
//	presc N          UART 0's prescaler := N
//	server MODE      what the server does with a connection: line (default: a line in, "big"
//	                 gets 48000 bytes, anything else "hello"; then it closes), echo, hold
//	dial PORT        the bench connects to 127.0.0.1:PORT (the ESP's server); it is the peer
//	peer TEXT        the peer (the dialled or the last held connection) sends TEXT
//	peerclose        the peer closes                 peershow     what the peer got, printed
//	mask RE TO       from now on, what matches RE is printed as TO
//	udp PORT TEXT    the bench's UDP socket (<uport>) sends TEXT to 127.0.0.1:PORT
//	udpshow          the datagrams it got, printed
// TEXT takes \r \n \t \\ \xNN, and <port> / <uport> for the server's / the UDP socket's port.
//
// MACHINE=zifi makes the machine a ZX Evo (TSConf) with the ESP on ZiFi, the AVR's FIFOs at
// #xxEF: scripts only, through zbridge.bin at #8000, both rings in its RAM (zbridge.asm).
// nr, nrget and presc mean nothing there.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "spectrum.h"
#include "cpu/Z80/z80.h"		// the register names
#include "../../../src/xcore/esp.h"
#include "../../../src/xcore/espnet.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define sock_close closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
typedef int sock_t;
#define sock_close close
#define INVALID_SOCKET (-1)
#endif

#define PAGE 48000
// the bridge's ring in the SRAM: CPU 0x8100, page 4 as reset leaves it; on the Evo CPU #9100,
// page 2, with the ring the ESP's bytes come back in at #A000
#define RING (zifi ? 0x9100 : 0x048100)
#define RING_WR (zifi ? 0x9000 : 0x048000)
#define RING_RD (zifi ? 0x9001 : 0x048001)
#define ZB_OWR 0x9002
#define ZB_ORD 0x9003
#define ZB_FIN 0x9004
#define ZB_OBUF 0xa000

static bool zifi = false;		// MACHINE=zifi

static Computer* comp;
static std::string guest;		// what the guest printed (the ESP's bytes, in script mode)
static std::deque<int> nrvals;		// nextreg values the bridge read
static int done = 0;
static long srvBytes = 0, srvSum = 0;	// the "big" pages sent, as the guest should count them
static std::string port;		// the server's, as text

static void log_out(Computer*, int p, int val) {
	if ((p & 0xffff) == 0x2003) guest += (char)(val & 0xff);
	if ((p & 0xffff) == 0x2103) nrvals.push_back(val & 0xff);
	if ((p & 0xffff) == 0x0103) done = 1;
}

static void host_tx(Computer* c, int uart, int b, long long start) {
	if (uart == 0) esp_byte(c, b, start);
}

static void zifi_tx(Computer* c, int b, long long start) {
	esp_byte(c, b, start);
}

static int frame_no();

static unsigned char page_byte(long i) {
	return (unsigned char)((i * 31 + (i >> 8)) & 0xff);
}

static void sock_nb(sock_t s) {
#ifdef _WIN32
	u_long nb = 1;
	ioctlsocket(s, FIONBIO, &nb);
#else
	fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
}

static int sock_recv(sock_t s, char* b, int n) {	// >0 bytes, 0 none yet, -1 closed
	int r = (int)recv(s, b, n, 0);
	if (r > 0) return r;
	if (r == 0) return -1;
#ifdef _WIN32
	return (WSAGetLastError() == WSAEWOULDBLOCK) ? 0 : -1;
#else
	return ((errno == EWOULDBLOCK) || (errno == EAGAIN)) ? 0 : -1;
#endif
}

// == the server and the peer, serviced between instructions: no threads ==

struct Conn {
	sock_t s = INVALID_SOCKET;
	std::string mode;
	std::string in;			// what it got
	std::string out;		// still to send
	bool closing = false;		// close once out is sent
	bool gone = false;
};

static sock_t lsock = INVALID_SOCKET;
static std::string srvMode = "line";
static std::vector<Conn> conns;
static int peer = -1;			// the index in conns of the peer
static std::string cport;		// the dialled connection's own port, as text
static sock_t usock = INVALID_SOCKET;	// the bench's UDP socket
static std::string uport;
static std::vector<std::string> ugot;	// datagrams it got

static void conn_service() {
	if (usock != INVALID_SOCKET) {
		char b[2048];
		int r;
		while ((r = sock_recv(usock, b, sizeof(b))) > 0) ugot.push_back(std::string(b, r));
	}
	if (lsock != INVALID_SOCKET) {
		sock_t c = accept(lsock, nullptr, nullptr);
		if (c != INVALID_SOCKET) {
			sock_nb(c);
			Conn n;
			n.s = c;
			n.mode = srvMode;
			conns.push_back(n);
			if (srvMode == "hold") peer = (int)conns.size() - 1;
		}
	}
	for (Conn& c : conns) {
		if (c.gone) continue;
		char b[4096];
		int r;
		while ((r = sock_recv(c.s, b, sizeof(b))) > 0) {
			c.in.append(b, r);
			if (c.mode == "echo") c.out.append(b, r);
		}
		if (r < 0) {
			sock_close(c.s);
			c.gone = true;
			continue;
		}
		if ((c.mode == "line") && !c.closing && (c.in.find('\n') != std::string::npos)) {
			std::string req = c.in.substr(0, c.in.find('\n'));
			while (!req.empty() && (req.back() == '\r')) req.pop_back();
			if (req == "big") {
				for (long i = 0; i < PAGE; i++) c.out += (char)page_byte(i);
				srvBytes += PAGE;
				for (long i = 0; i < PAGE; i++) srvSum = (srvSum + page_byte(i)) & 0xffff;
			} else {
				c.out += "hello\r\n";
			}
			c.closing = true;
		}
		while (!c.out.empty()) {
			int n = (int)send(c.s, c.out.data(), (int)c.out.size(), 0);
			if (n <= 0) break;
			c.out.erase(0, n);
		}
		if (c.closing && c.out.empty()) {
#ifdef _WIN32
			shutdown(c.s, SD_SEND);
#else
			shutdown(c.s, SHUT_WR);
#endif
			c.closing = false;
			c.mode = "drain";		// till the ESP closes its end
		}
	}
}

static bool server_open() {
	lsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa.sin_port = 0;
	socklen_t sl = sizeof(sa);
	if ((lsock == INVALID_SOCKET) || bind(lsock, (sockaddr*)&sa, sizeof(sa)) || listen(lsock, 4) || getsockname(lsock, (sockaddr*)&sa, &sl))
		return false;
	sock_nb(lsock);
	port = std::to_string(ntohs(sa.sin_port));
	usock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	sa.sin_port = 0;
	sl = sizeof(sa);
	if ((usock == INVALID_SOCKET) || bind(usock, (sockaddr*)&sa, sizeof(sa)) || getsockname(usock, (sockaddr*)&sa, &sl))
		return false;
	sock_nb(usock);
	uport = std::to_string(ntohs(sa.sin_port));
	return true;
}

static void udp_send(int p, const std::string& b) {
	sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa.sin_port = htons((unsigned short)p);
	sendto(usock, b.data(), (int)b.size(), 0, (sockaddr*)&sa, sizeof(sa));
}

static bool dial(int p) {
	sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) return false;
	sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa.sin_port = htons((unsigned short)p);
	if (connect(s, (sockaddr*)&sa, sizeof(sa))) {
		sock_close(s);
		return false;
	}
	sock_nb(s);
	socklen_t sl = sizeof(sa);
	if (!getsockname(s, (sockaddr*)&sa, &sl)) cport = std::to_string(ntohs(sa.sin_port));
	Conn n;
	n.s = s;
	n.mode = "hold";
	conns.push_back(n);
	peer = (int)conns.size() - 1;
	return true;
}

// == the machine ==

static int frame_no() {
	return zifi ? comp->vid->fcnt : comp->next->vsyncCount;
}

static int frame0;
static void step() {
	compExec(comp);
	if (zifi) {			// what the bridge has put into its ring
		unsigned char* ram = comp->mem->ramData;
		while (ram[ZB_ORD] != ram[ZB_OWR]) {
			guest += (char)ram[ZB_OBUF + ram[ZB_ORD]];
			ram[ZB_ORD]++;
		}
		if (ram[ZB_FIN]) done = 1;
	}
	if (frame_no() != frame0) {
		frame0 = frame_no();
		esp_frame(comp);
		conn_service();
	}
}

// A ZX Evo as tsconf-bench makes it, ZiFi fitted: the bridge at #8000 of page 2, windows
// 1..3 = pages 5, 2, 0
static Computer* machine_zifi(const char* prog) {
	Computer* c = compCreate();
	if (!compSetHardware(c, "TSConf")) {
		fprintf(stderr, "no TSConf core\n");
		exit(1);
	}
	vLayout lay = {{448, 320}, {52, 48}, {88, 32}, {256, 192}, {0, 0}, 64};
	comp_set_layout(c, &lay);
	memSetSize(c->mem, MEM_4M, MEM_512K);
	compSetBaseFrq(c, 3.5);
	compReset(c, RES_DEFAULT);
	FILE* f = fopen(prog, "rb");
	if (!f) {
		fprintf(stderr, "can't open %s\n", prog);
		exit(1);
	}
	size_t n = fread(c->mem->ramData + 0x8000, 1, 0x1000, f);
	fclose(f);
	(void)n;
	memset(c->mem->ramData + 0x9000, 0, 0x2000);
	c->hw->out(c, 0x11af, 5);
	c->hw->out(c, 0x12af, 2);
	c->hw->out(c, 0x13af, 0);
	c->cpu->regPC = 0x8000;
	c->cpu->regSP = 0xbf00;
	c->cpu->flgIFF1 = c->cpu->flgIFF2 = 0;
	c->zifi->on = 1;
	zifi_power(c);
	zifi_host_tx = zifi_tx;
	comp = c;			// frame_no() below
	esp_flash_file(getenv("FLASH"));
	esp_attach(c);
	frame0 = frame_no();
	return c;
}

static Computer* machine(const char* prog) {
	if (zifi) return machine_zifi(prog);
	Computer* c = compCreate();
	if (!compSetHardware(c, "ZXNext")) {
		fprintf(stderr, "no ZXNext core\n");
		exit(1);
	}
	compSetBaseFrq(c, 3.5);
	compReset(c, RES_DEFAULT);
	comp_kbd_release(c);
	c->next->busRomcs = 1;
	next_reset_open = 1;
	next_ear_tied = 1;
	FILE* f = fopen(prog, "rb");
	if (!f) {
		fprintf(stderr, "can't open %s\n", prog);
		exit(1);
	}
	size_t n = fread(c->mem->ramData, 1, 0x3f00, f);
	fclose(f);
	(void)n;
	memcpy(c->mem->ramData + 0x3f00, port.c_str(), port.size() + 1);
	next_log_out = log_out;
	next_uart_host_tx = host_tx;
	esp_flash_file(getenv("FLASH"));
	esp_attach(c);
	frame0 = c->next->vsyncCount;
	return c;
}

// == scripts ==

static std::deque<unsigned char> ringq;		// 4-byte entries waiting for room in the ring
static std::vector<std::pair<std::regex, std::string>> masks;
static size_t shown = 0;			// how much of guest is printed

static void ring_feed() {
	unsigned char* ram = comp->mem->ramData;
	while (ringq.size() >= 4) {
		int wr = ram[RING_WR], rd = ram[RING_RD];
		if (((wr + 1) & 63) == rd) return;
		for (int i = 0; i < 4; i++) {
			ram[RING + wr * 4 + i] = ringq.front();
			ringq.pop_front();
		}
		ram[RING_WR] = (wr + 1) & 63;
	}
}

static void ring_put(int type, int a = 0, int b = 0) {
	ringq.push_back(type);
	ringq.push_back(a & 0xff);
	ringq.push_back(b & 0xff);
	ringq.push_back(0);
}

static int frames_of(long ms) {
	return (int)((ms + 19) / 20);
}

static bool run_frames(int n, int limit) {
	int end = frame_no() + n;
	while ((frame_no() < end) && (frame_no() < limit)) {
		ring_feed();
		step();
	}
	return frame_no() < limit;
}

// the ring empty and the bridge done with it
static void flush(int limit) {
	unsigned char* ram = comp->mem->ramData;
	while ((!ringq.empty() || (ram[RING_WR] != ram[RING_RD])) && (frame_no() < limit)) {
		ring_feed();
		step();
	}
}

static std::string unescape(const std::string& s) {
	std::string r;
	for (size_t i = 0; i < s.size(); i++) {
		if ((s[i] == '<') && (s.compare(i, 6, "<port>") == 0)) {
			r += port;
			i += 5;
		} else if ((s[i] == '<') && (s.compare(i, 7, "<uport>") == 0)) {
			r += uport;
			i += 6;
		} else if ((s[i] == '\\') && (i + 1 < s.size())) {
			char c = s[++i];
			if (c == 'r') r += '\r';
			else if (c == 'n') r += '\n';
			else if (c == 't') r += '\t';
			else if ((c == 'x') && (i + 2 < s.size())) {
				r += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
				i += 2;
			} else r += c;
		} else r += s[i];
	}
	return r;
}

static std::string shown_text(const std::string& s) {
	std::string t;
	for (unsigned char c : s) {
		if (c == '\r') t += "\\r";
		else if (c == '\n') t += "\\n\n";
		else if (c == '\\') t += "\\\\";
		else if ((c < 0x20) || (c > 0x7e)) {
			char h[8];
			snprintf(h, sizeof(h), "\\x%02X", c);
			t += h;
		} else t += (char)c;
	}
	return t;
}

static std::string masked(std::string s) {
	if (!port.empty()) for (size_t at; (at = s.find(port)) != std::string::npos;) s.replace(at, port.size(), "<port>");
	if (!cport.empty()) for (size_t at; (at = s.find(cport)) != std::string::npos;) s.replace(at, cport.size(), "<cport>");
	if (!uport.empty()) for (size_t at; (at = s.find(uport)) != std::string::npos;) s.replace(at, uport.size(), "<uport>");
	for (auto& m : masks) s = std::regex_replace(s, m.first, m.second);
	return s;
}

static void show(const std::string& tag, const std::string& s) {
	std::string t = shown_text(s);
	size_t a = 0;
	while (a < t.size()) {
		size_t b = t.find('\n', a);
		if (b == std::string::npos) b = t.size();
		printf("%s%s\n", tag.c_str(), masked(t.substr(a, b - a)).c_str());
		a = b + 1;
	}
}

static void show_rx() {
	if (shown < guest.size()) show("<< ", guest.substr(shown));
	shown = guest.size();
}

static int run_script(const char* path, int limit) {
	FILE* f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "can't open %s\n", path);
		return 1;
	}
	char buf[8192];
	while (fgets(buf, sizeof(buf), f) && (frame_no() < limit)) {
		std::string l(buf);
		while (!l.empty() && ((l.back() == '\n') || (l.back() == '\r'))) l.pop_back();
		size_t i = l.find_first_not_of(" \t");
		if ((i == std::string::npos) || (l[i] == '#')) continue;
		l = l.substr(i);
		size_t sp = l.find(' ');
		std::string op = l.substr(0, sp), arg = (sp == std::string::npos) ? "" : l.substr(sp + 1);
		if ((op == "send") || (op == "raw")) {
			flush(limit);
			show_rx();
			std::string b = unescape(arg) + ((op == "send") ? "\r\n" : "");
			show(">> ", b);
			for (unsigned char c : b) ring_put(0, c);
		} else if (op == "wait") {
			std::string what = arg;
			long ms = 10000;
			size_t s2 = arg.rfind(' ');
			if ((s2 != std::string::npos) && (arg.find_first_not_of("0123456789", s2 + 1) == std::string::npos) && (s2 + 1 < arg.size())) {
				ms = atol(arg.c_str() + s2 + 1);
				what = arg.substr(0, s2);
			}
			std::string w = unescape(what);
			size_t from = shown;
			int end = frame_no() + frames_of(ms);
			while ((guest.find(w, from) == std::string::npos) && (frame_no() < end) && (frame_no() < limit)) {
				ring_feed();
				step();
			}
			show_rx();
			if (guest.find(w, from) == std::string::npos) printf("-- no %s\n", masked(shown_text(w)).c_str());
		} else if (op == "quiet") {
			int q = frames_of(atol(arg.c_str()));
			int end = frame_no() + frames_of(15000);
			size_t seen = guest.size();
			int since = frame_no();
			while ((frame_no() < end) && (frame_no() < limit)) {
				ring_feed();
				step();
				if (guest.size() != seen) {
					seen = guest.size();
					since = frame_no();
				}
				if (frame_no() - since >= q) break;
			}
			show_rx();
		} else if (op == "pause") {
			run_frames(frames_of(atol(arg.c_str())), limit);
			show_rx();
		} else if (op == "print") {
			show_rx();
			printf("%s\n", arg.c_str());
		} else if (op == "nr") {
			unsigned r = 0, v = 0;
			sscanf(arg.c_str(), "%x %x", &r, &v);
			ring_put(1, r, v);
			flush(limit);
		} else if (op == "nrget") {
			unsigned r = 0;
			sscanf(arg.c_str(), "%x", &r);
			nrvals.clear();
			ring_put(2, r);
			flush(limit);
			while (nrvals.empty() && (frame_no() < limit)) step();
			show_rx();
			printf("NR %02X = %02X\n", r, nrvals.empty() ? 0 : nrvals.front());
		} else if (op == "presc") {
			long n = atol(arg.c_str());
			ring_put(4, n & 0xff, (n >> 8) & 0x3f);
			flush(limit);
		} else if (op == "server") {
			srvMode = arg;
		} else if (op == "dial") {
			show_rx();
			bool ok = dial(atoi(arg.c_str()));
			printf("-- dial %s\n", ok ? "connected" : "refused");
		} else if (op == "peer") {
			if (peer >= 0) conns[peer].out += unescape(arg);
			printf("-> %s\n", masked(shown_text(unescape(arg))).c_str());
		} else if (op == "peerclose") {
			if ((peer >= 0) && !conns[peer].gone) {
				sock_close(conns[peer].s);
				conns[peer].gone = true;
			}
			printf("-- peer closed\n");
		} else if (op == "peershow") {
			show_rx();
			if (peer >= 0) show("<- ", conns[peer].in);
			if (peer >= 0) conns[peer].in.clear();
		} else if (op == "udp") {
			size_t s2 = arg.find(' ');
			std::string t = (s2 == std::string::npos) ? "" : unescape(arg.substr(s2 + 1));
			udp_send(atoi(arg.c_str()), t);
			printf("-> udp %s\n", masked(shown_text(t)).c_str());
		} else if (op == "udpshow") {
			show_rx();
			for (auto& g : ugot) show("<- udp ", g);
			ugot.clear();
		} else if (op == "mask") {
			size_t s2 = arg.rfind(' ');
			if (s2 != std::string::npos) masks.push_back({std::regex(arg.substr(0, s2)), arg.substr(s2 + 1)});
		} else {
			printf("-- bad step: %s\n", l.c_str());
		}
	}
	fclose(f);
	flush(limit);
	show_rx();
	return 0;
}

int main(int argc, char** argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: esp-bench prog.bin frames\n");
		return 2;
	}
	int frames = atoi(argv[2]);
	esp_net_init();
	if (!server_open()) {
		fprintf(stderr, "no server socket\n");
		return 1;
	}
	const char* script = getenv("SCRIPT");
	const char* mach = getenv("MACHINE");
	zifi = mach && !strcmp(mach, "zifi");
	comp = machine(argv[1]);
	if (script) {
		std::string path = script;
		if (getenv("XPECCY_ROOT")) path = std::string(getenv("XPECCY_ROOT")) + "/tests/core/esp/scripts/" + script;
		int r = run_script(path.c_str(), frames);
		if (frame_no() >= frames) printf("-- frame limit\n");
		esp_detach(comp);
		return r;
	}
	while (!done && (frame_no() < frames)) step();
	esp_detach(comp);
	std::string out = masked(guest);
	printf("%s", out.c_str());
	if (!out.empty() && (out.back() != '\n')) printf("\n");
	if (srvBytes) printf("server: %li bytes, sum %li\n", srvBytes, srvSum);
	printf("%s\n", done ? "ended by the program" : "frame limit");	// the frame depends on the host's network
	return 0;
}
