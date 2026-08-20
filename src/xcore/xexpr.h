#pragma once

#include <string>
#include <vector>

#include "../libxpeccy/spectrum.h"

// C-like expression compiler/evaluator
// used by breakpoint conditions and by the watcher

// opcodes of the compiled (RPN) form
enum {
	XE_NUM = 0,	// push val
	XE_REG,		// push register named 'name'
	XE_LAB,		// push address of label 'name'
	XE_VAR,		// push pseudo-variable, val = XV_*
	// unary
	XE_NOT, XE_INV, XE_NEG,
	XE_MRDB,	// byte from memory
	XE_MRDW,	// word from memory (L-H)
	// binary
	XE_MUL, XE_DIV, XE_MOD,
	XE_ADD, XE_SUB,
	XE_SHL, XE_SHR,
	XE_LT, XE_GT, XE_LE, XE_GE,
	XE_EQ, XE_NE,
	XE_AND, XE_XOR, XE_OR,
	XE_LAND, XE_LOR
};

// pseudo-variables: last memory/io event and machine state
enum {
	XV_RD = 0,	// address of last memory read
	XV_WR,		// address of last memory write
	XV_MDT,		// data of last memory read/write
	XV_IN,		// port of last 'in'
	XV_OUT,		// port of last 'out'
	XV_VAL,		// data of last 'in'/'out'
	XV_DOS,		// dos rom is on
	XV_SLOT0,	// page number in memory window 0..3
	XV_SLOT1,
	XV_SLOT2,
	XV_SLOT3,
	XV_FRAME,	// frames since reset
	XV_RAYX,	// beam position, the two numbers of the RAY panel
	XV_RAYY,
	XV_HITS		// how many times this breakpoint was hit
};

typedef struct {
	int op;
	unsigned val;
	int base;		// XE_NUM: how the number was written, to read it back
	std::string name;
} xExprItem;

typedef struct {
	std::vector<xExprItem> code;
	std::string src;	// source text as typed
	std::string err;	// error message, empty if compiled
	int errpos;		// position of error in src
} xExpr;

xExpr xexpr_compile(const char*);
unsigned xexpr_eval(const xExpr&, Computer*, bool* = nullptr, int = 0);
bool xexpr_ok(const xExpr&);
// the expression as it was understood: hex numbers, priorities as brackets
std::string xexpr_text(const xExpr&);
// does the expression use this pseudo-variable (XV_*)
bool xexpr_uses_var(const xExpr&, int);
