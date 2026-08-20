// C-like expression compiler/evaluator
//
// numbers use the machine's own base by default (0x/#/$ force hex), names are
// looked up as cpu register -> pseudo-variable -> label -> number. registers
// win over numbers, so BC is the register pair and 0BC is the number.

#include "xcore.h"
#include "xexpr.h"

#include <cstring>
#include <cstdio>

// operator priority: bigger binds tighter

static int xe_prio(int op) {
	switch (op) {
		case XE_LOR: return 1;
		case XE_LAND: return 2;
		case XE_OR: return 3;
		case XE_XOR: return 4;
		case XE_AND: return 5;
		case XE_EQ: case XE_NE: return 6;
		case XE_LT: case XE_GT: case XE_LE: case XE_GE: return 7;
		case XE_SHL: case XE_SHR: return 8;
		case XE_ADD: case XE_SUB: return 9;
		case XE_MUL: case XE_DIV: case XE_MOD: return 10;
		case XE_MRDB: return 11;		// '->' : byte at (left + right)
	}
	return -1;
}

static const char* xe_var_tab[] = {
	"RD", "WR", "MDT", "IN", "OUT", "VAL", "DOS",
	"SLOT0", "SLOT1", "SLOT2", "SLOT3", "FRAME", "RAYX", "RAYY", "HITS", NULL
};

static const char* xe_var_name(int id) {
	if (id < 0) return NULL;
	if (id >= (int)(sizeof(xe_var_tab) / sizeof(xe_var_tab[0])) - 1) return NULL;
	return xe_var_tab[id];
}

static bool x_isletter(char c) {
	if ((c >= 'A') && (c <= 'Z')) return true;
	if ((c >= 'a') && (c <= 'z')) return true;
	return (c == '_');
}

static int x_digit(char c, int base) {
	int res = -1;
	if ((c >= '0') && (c <= '9')) res = c - '0';
	else if ((c >= 'A') && (c <= 'Z')) res = c - 'A' + 10;
	else if ((c >= 'a') && (c <= 'z')) res = c - 'a' + 10;
	return (res < base) ? res : -1;
}

// parser

typedef struct {
	const char* src;
	const char* ptr;
	Computer* comp;
	std::string err;
	int errpos;
	std::vector<xExprItem>* code;
} xeParser;

static void xe_fail(xeParser* prs, const char* msg) {
	if (!prs->err.empty()) return;		// keep the first error
	prs->err = msg;
	prs->errpos = prs->ptr - prs->src;
}

static void xe_emit(xeParser* prs, int op, unsigned val, const char* name) {
	xExprItem itm;
	itm.op = op;
	itm.val = val;
	itm.base = 10;
	if (name) itm.name = name;
	prs->code->push_back(itm);
}

static void xe_emit_num(xeParser* prs, unsigned val, int base) {
	xe_emit(prs, XE_NUM, val, NULL);
	prs->code->back().base = base;
}

static void xe_skip(xeParser* prs) {
	while ((*prs->ptr == ' ') || (*prs->ptr == '\t')) prs->ptr++;
}

// returns operator id, doesn't move the pointer

static int xe_peek_op(xeParser* prs, int* len) {
	const char* p = prs->ptr;
	*len = 2;
	if (!strncmp(p, "<<", 2)) return XE_SHL;
	if (!strncmp(p, ">>", 2)) return XE_SHR;
	if (!strncmp(p, "<=", 2)) return XE_LE;
	if (!strncmp(p, ">=", 2)) return XE_GE;
	if (!strncmp(p, "==", 2)) return XE_EQ;
	if (!strncmp(p, "!=", 2)) return XE_NE;
	if (!strncmp(p, "&&", 2)) return XE_LAND;
	if (!strncmp(p, "||", 2)) return XE_LOR;
	if (!strncmp(p, "->", 2)) return XE_MRDB;
	*len = 1;
	switch (*p) {
		case '*': return XE_MUL;
		case '/': return XE_DIV;
		case '%': return XE_MOD;
		case '+': return XE_ADD;
		case '-': return XE_SUB;
		case '<': return XE_LT;
		case '>': return XE_GT;
		case '=': return XE_EQ;		// alias for ==
		case '&': return XE_AND;
		case '^': return XE_XOR;
		case '|': return XE_OR;
	}
	*len = 0;
	return -1;
}

static void xe_binary(xeParser*, int);

// number, name, char, bracketed sub-expression

static void xe_primary(xeParser* prs) {
	xe_skip(prs);
	char c = *prs->ptr;
	unsigned val = 0;
	int dig;
	int base;
	std::string name;
	std::string uname;
	bool err;
	xAdr xadr;
	if (!c) {
		xe_fail(prs, "unexpected end of expression");
		return;
	}
	if (c == '(') {				// (expr)
		prs->ptr++;
		xe_binary(prs, 1);
		xe_skip(prs);
		if (*prs->ptr != ')') {
			xe_fail(prs, "')' expected");
			return;
		}
		prs->ptr++;
		return;
	}
	if (c == '[') {				// [expr] : word from memory
		prs->ptr++;
		xe_binary(prs, 1);
		xe_skip(prs);
		if (*prs->ptr != ']') {
			xe_fail(prs, "']' expected");
			return;
		}
		prs->ptr++;
		xe_emit(prs, XE_MRDW, 0, NULL);
		return;
	}
	if (c == 0x27) {			// 'c' : char
		prs->ptr++;
		if (!*prs->ptr || (prs->ptr[1] != 0x27)) {
			xe_fail(prs, "single char expected");
			return;
		}
		val = *prs->ptr & 0xff;
		prs->ptr += 2;
		xe_emit_num(prs, val, 10);
		return;
	}
	if (c == '#') {				// #nnnn : hex number
		prs->ptr++;
		if (x_digit(*prs->ptr, 16) < 0) {
			xe_fail(prs, "hex digit expected");
			return;
		}
		while ((dig = x_digit(*prs->ptr, 16)) >= 0) {
			val = (val << 4) | dig;
			prs->ptr++;
		}
		xe_emit_num(prs, val, 16);
		return;
	}
	if (x_digit(c, 10) >= 0) {		// decimal, 0x = hex, leading zero = octal
		base = 10;
		if ((c == '0') && ((prs->ptr[1] == 'x') || (prs->ptr[1] == 'X'))) {
			prs->ptr += 2;
			base = 16;
		} else if ((c == '0') && (x_digit(prs->ptr[1], 8) >= 0)) {
			prs->ptr++;
			base = 8;
		}
		if (x_digit(*prs->ptr, base) < 0) {
			xe_fail(prs, "wrong number");
			return;
		}
		while ((dig = x_digit(*prs->ptr, base)) >= 0) {
			val = val * base + dig;
			prs->ptr++;
		}
		if (x_digit(*prs->ptr, 16) >= 0) {	// 0FF and such: no bare hex any more
			xe_fail(prs, "wrong digit for this base, hex needs 0x or #");
			return;
		}
		xe_emit_num(prs, val, base);
		return;
	}
	if ((c == '.') || x_isletter(c)) {	// name : register, pseudo-variable, label
		bool isreg = (c == '.');	// .name is always a register
		if (isreg) prs->ptr++;
		while (x_isletter(*prs->ptr) || (x_digit(*prs->ptr, 10) >= 0) || (*prs->ptr == '.') || (*prs->ptr == 0x27)) {
			name.push_back(*prs->ptr);
			prs->ptr++;
		}
		if (name.empty()) {
			xe_fail(prs, "name expected");
			return;
		}
		uname = name;
		for (auto& ch : uname) {
			if ((ch >= 'a') && (ch <= 'z')) ch = ch - 'a' + 'A';
		}
		// M(expr) : byte from memory
		if (!isreg && (uname == "M")) {
			xe_skip(prs);
			if (*prs->ptr == '(') {
				prs->ptr++;
				xe_binary(prs, 1);
				xe_skip(prs);
				if (*prs->ptr != ')') {
					xe_fail(prs, "')' expected");
					return;
				}
				prs->ptr++;
				xe_emit(prs, XE_MRDB, 0, NULL);
				return;
			}
		}
		// cpu register
		if (prs->comp) {
			cpu_get_reg(prs->comp->cpu, uname.c_str(), &err);
			if (!err) {
				xe_emit(prs, XE_REG, 0, uname.c_str());
				return;
			}
		}
		if (isreg) {
			xe_fail(prs, "no such register");
			return;
		}
		// pseudo-variable
		for (int i = 0; xe_var_tab[i]; i++) {
			if (uname == xe_var_tab[i]) {
				xe_emit(prs, XE_VAR, i, NULL);
				return;
			}
		}
		// label
		xadr = find_label(QString(name.c_str()));
		if (xadr.type >= 0) {
			xe_emit(prs, XE_LAB, 0, name.c_str());
			return;
		}
		xe_fail(prs, "no such register, variable or label");
		return;
	}
	xe_fail(prs, "unexpected symbol");
}

static void xe_unary(xeParser* prs) {
	xe_skip(prs);
	switch (*prs->ptr) {
		case '!':
			if (prs->ptr[1] == '=') break;		// != is binary
			prs->ptr++;
			xe_unary(prs);
			xe_emit(prs, XE_NOT, 0, NULL);
			return;
		case '~':
			prs->ptr++;
			xe_unary(prs);
			xe_emit(prs, XE_INV, 0, NULL);
			return;
		case '-':
			prs->ptr++;
			xe_unary(prs);
			xe_emit(prs, XE_NEG, 0, NULL);
			return;
		case '+':
			prs->ptr++;
			xe_unary(prs);
			return;
	}
	xe_primary(prs);
}

// left-associative binary operators with priority >= minp

static void xe_binary(xeParser* prs, int minp) {
	int op, len, prio;
	xe_unary(prs);
	while (prs->err.empty()) {
		xe_skip(prs);
		op = xe_peek_op(prs, &len);
		if (op < 0) break;
		prio = xe_prio(op);
		if (prio < minp) break;
		prs->ptr += len;
		xe_binary(prs, prio + 1);
		if (op == XE_MRDB) {		// a->b is byte at (a+b)
			xe_emit(prs, XE_ADD, 0, NULL);
			xe_emit(prs, XE_MRDB, 0, NULL);
		} else {
			xe_emit(prs, op, 0, NULL);
		}
	}
}

xExpr xexpr_compile(const char* src) {
	xExpr res;
	xeParser prs;
	res.errpos = -1;
	if (!src) src = "";
	res.src = src;
	prs.src = src;
	prs.ptr = src;
	prs.comp = conf.prof.cur ? conf.prof.cur->zx : NULL;
	prs.errpos = -1;
	prs.code = &res.code;
	xe_skip(&prs);
	if (*prs.ptr) {
		xe_binary(&prs, 1);
		if (prs.err.empty()) {
			xe_skip(&prs);
			if (*prs.ptr) xe_fail(&prs, "unexpected symbol");
		}
	}
	if (!prs.err.empty()) {
		res.code.clear();
		res.err = prs.err;
		res.errpos = prs.errpos;
	}
	return res;
}

bool xexpr_ok(const xExpr& exp) {
	return exp.err.empty() && !exp.code.empty();
}

bool xexpr_uses_var(const xExpr& exp, int id) {
	for (auto it = exp.code.begin(); it != exp.code.end(); it++) {
		if ((it->op == XE_VAR) && ((int)it->val == id)) return true;
	}
	return false;
}

// text of an operator, for reading the compiled form back

static const char* xe_op_text(int op) {
	switch (op) {
		case XE_MUL: return "*";
		case XE_DIV: return "/";
		case XE_MOD: return "%";
		case XE_ADD: return "+";
		case XE_SUB: return "-";
		case XE_SHL: return "<<";
		case XE_SHR: return ">>";
		case XE_LT: return "<";
		case XE_GT: return ">";
		case XE_LE: return "<=";
		case XE_GE: return ">=";
		case XE_EQ: return "==";
		case XE_NE: return "!=";
		case XE_AND: return "&";
		case XE_XOR: return "^";
		case XE_OR: return "|";
		case XE_LAND: return "&&";
		case XE_LOR: return "||";
	}
	return "?";
}

// how the expression was really understood: numbers in hex, priorities as
// brackets. shown while typing, so a wrong base or priority can't hide

std::string xexpr_text(const xExpr& exp) {
	std::vector<std::string> stk;
	std::string a, b;
	char buf[32];
	const char* nam;
	if (!xexpr_ok(exp)) return "";
	for (auto it = exp.code.begin(); it != exp.code.end(); it++) {
		if (it->op <= XE_VAR) {
			switch (it->op) {
				case XE_NUM:
					// hex and decimal read back as typed; octal is shown as its
					// value, that is the part a leading zero hides
					switch (it->base) {
						case 16: snprintf(buf, sizeof(buf), "0x%X", it->val); break;
						default: snprintf(buf, sizeof(buf), "%u", it->val); break;
					}
					stk.push_back(buf);
					break;
				case XE_REG:
				case XE_LAB:
					stk.push_back(it->name);
					break;
				case XE_VAR:
					nam = xe_var_name(it->val);
					stk.push_back(nam ? nam : "?");
					break;
			}
			continue;
		}
		if (stk.empty()) return "";
		if (it->op <= XE_MRDW) {			// unary
			a = stk.back();
			stk.pop_back();
			switch (it->op) {
				case XE_NOT: a = "!(" + a + ")"; break;
				case XE_INV: a = "~(" + a + ")"; break;
				case XE_NEG: a = "-(" + a + ")"; break;
				case XE_MRDB: a = "M(" + a + ")"; break;
				case XE_MRDW: a = "[" + a + "]"; break;
			}
			stk.push_back(a);
			continue;
		}
		if (stk.size() < 2) return "";
		b = stk.back();
		stk.pop_back();
		a = stk.back();
		stk.pop_back();
		stk.push_back("(" + a + " " + xe_op_text(it->op) + " " + b + ")");
	}
	if (stk.size() != 1) return "";
	return stk.front();
}

// evaluation

static unsigned xe_var_value(Computer* comp, unsigned id, int hits) {
	switch (id) {
		case XV_RD: return (unsigned)comp->brkev.rd;
		case XV_WR: return (unsigned)comp->brkev.wr;
		case XV_MDT: return (unsigned)comp->brkev.mdt;
		case XV_IN: return (unsigned)comp->brkev.in;
		case XV_OUT: return (unsigned)comp->brkev.out;
		case XV_VAL: return (unsigned)comp->brkev.val;
		case XV_DOS: return comp->flgDOS ? 1 : 0;
		case XV_SLOT0: case XV_SLOT1: case XV_SLOT2: case XV_SLOT3:
			// map[] is indexed by the high byte of the address, so the four
			// 16K windows sit at 00, 40, 80 and C0; num counts 256-byte pages,
			// >> 6 turns it into the 16K page number deBUGa shows
			return comp->mem->map[((id - XV_SLOT0) << 6) & 0xff].num >> 6;
		case XV_FRAME: return comp->frmCount;
		case XV_RAYX: return comp->vid->ray.x;
		case XV_RAYY: return comp->vid->ray.y;
		case XV_HITS: return hits;
	}
	return 0;
}

#define XE_STACK 64

unsigned xexpr_eval(const xExpr& exp, Computer* comp, bool* errp, int hits) {
	unsigned stk[XE_STACK];
	int sp = 0;
	bool err = false;
	bool rerr;
	int val;
	xAdr xadr;
	if (!comp || exp.code.empty() || !exp.err.empty()) {
		if (errp) *errp = true;
		return 0;
	}
	for (auto it = exp.code.begin(); (it != exp.code.end()) && !err; it++) {
		// operands
		if (it->op <= XE_VAR) {
			if (sp >= XE_STACK) {
				err = true;
				break;
			}
			switch (it->op) {
				case XE_NUM:
					stk[sp] = it->val;
					break;
				case XE_REG:
					val = cpu_get_reg(comp->cpu, it->name.c_str(), &rerr);
					if (rerr) err = true;
					stk[sp] = val;
					break;
				case XE_LAB:
					xadr = find_label(QString(it->name.c_str()));
					if (xadr.type < 0) err = true;
					stk[sp] = xadr.adr;
					break;
				case XE_VAR:
					stk[sp] = xe_var_value(comp, it->val, hits);
					break;
			}
			sp++;
			continue;
		}
		// unary
		if (it->op <= XE_MRDW) {
			if (sp < 1) {
				err = true;
				break;
			}
			switch (it->op) {
				case XE_NOT: stk[sp-1] = !stk[sp-1]; break;
				case XE_INV: stk[sp-1] = ~stk[sp-1]; break;
				case XE_NEG: stk[sp-1] = -(int)stk[sp-1]; break;
				case XE_MRDB:
					stk[sp-1] = memRd(comp->mem, stk[sp-1]) & 0xff;
					break;
				case XE_MRDW:
					val = memRd(comp->mem, stk[sp-1]) & 0xff;
					val |= (memRd(comp->mem, stk[sp-1] + 1) & 0xff) << 8;
					stk[sp-1] = val;
					break;
			}
			continue;
		}
		// binary
		if (sp < 2) {
			err = true;
			break;
		}
		sp--;
		switch (it->op) {
			case XE_MUL: stk[sp-1] *= stk[sp]; break;
			case XE_DIV: stk[sp-1] = stk[sp] ? (stk[sp-1] / stk[sp]) : 0; break;
			case XE_MOD: stk[sp-1] = stk[sp] ? (stk[sp-1] % stk[sp]) : 0; break;
			case XE_ADD: stk[sp-1] += stk[sp]; break;
			case XE_SUB: stk[sp-1] -= stk[sp]; break;
			case XE_SHL: stk[sp-1] = (stk[sp] < 32) ? (stk[sp-1] << stk[sp]) : 0; break;
			case XE_SHR: stk[sp-1] = (stk[sp] < 32) ? (stk[sp-1] >> stk[sp]) : 0; break;
			case XE_LT: stk[sp-1] = (stk[sp-1] < stk[sp]); break;
			case XE_GT: stk[sp-1] = (stk[sp-1] > stk[sp]); break;
			case XE_LE: stk[sp-1] = (stk[sp-1] <= stk[sp]); break;
			case XE_GE: stk[sp-1] = (stk[sp-1] >= stk[sp]); break;
			case XE_EQ: stk[sp-1] = (stk[sp-1] == stk[sp]); break;
			case XE_NE: stk[sp-1] = (stk[sp-1] != stk[sp]); break;
			case XE_AND: stk[sp-1] &= stk[sp]; break;
			case XE_XOR: stk[sp-1] ^= stk[sp]; break;
			case XE_OR: stk[sp-1] |= stk[sp]; break;
			case XE_LAND: stk[sp-1] = (stk[sp-1] && stk[sp]); break;
			case XE_LOR: stk[sp-1] = (stk[sp-1] || stk[sp]); break;
		}
	}
	if (sp != 1) err = true;
	if (errp) *errp = err;
	return err ? 0 : stk[0];
}

// compatibility wrapper for the old watcher evaluator

xResult xEval(const char* ptr, int) {
	xResult res;
	bool err = false;
	xExpr exp = xexpr_compile(ptr);
	res.ptr = ptr ? (ptr + strlen(ptr)) : ptr;
	if (!xexpr_ok(exp)) {
		res.err = 1;
		res.value = 0;
	} else {
		res.value = xexpr_eval(exp, conf.prof.cur ? conf.prof.cur->zx : NULL, &err);
		res.err = err ? 1 : 0;
	}
	return res;
}
