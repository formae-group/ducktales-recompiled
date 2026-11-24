/*
 * decomp_timing.c - the original's time for the C of timed routines
 * (docs/decompilation.md "Timing"). The C marks each decision of the original
 * that the ROM alone does not tell (DC_BR, DC_JUMP and DC_EA in decomp.h);
 * between the marks the walker follows the ROM instruction by instruction:
 * fall-through, JMP, JSR into the routine called and RTS back. Its path (each
 * instruction's address and the cycle its fetch starts at), its total and the
 * cycle of each bus operation are what the hosts use: check mode compares
 * them with the original's bus cycles, the native runtime spends them.
 */
#include "decomp/decomp_backend.h"

#include "ducktales/ram.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define WALK_LIMIT 200000
#define MAX_DEPTH  32

enum {
    M_IMP,
    M_IMM,
    M_ZP,
    M_ZPX,
    M_ZPY,
    M_IZX,
    M_IZY,
    M_REL,
    M_ABS,
    M_ABX,
    M_ABY,
    M_IND
};

enum {
    K_NONE,
    K_READ,
    K_STORE,
    K_RMW,
    K_BRANCH,
    K_JMP,
    K_JMPI,
    K_JSR,
    K_RTS,
    K_RTI,
    K_OTHER
};

typedef struct {
    uint8_t mode, kind, cycles;
} Opcode;

/* The official opcodes: addressing mode, kind and cycles without the page
 * crossing of an indexed read or a taken branch. */
static const Opcode OPCODES[256] = {
    [0x01] = { M_IZX, K_READ, 6 },   /* ORA */
    [0x05] = { M_ZP, K_READ, 3 },    /* ORA */
    [0x06] = { M_ZP, K_RMW, 5 },     /* ASL */
    [0x08] = { M_IMP, K_OTHER, 3 },  /* PHP */
    [0x09] = { M_IMM, K_READ, 2 },   /* ORA */
    [0x0A] = { M_IMP, K_OTHER, 2 },  /* ASL */
    [0x0D] = { M_ABS, K_READ, 4 },   /* ORA */
    [0x0E] = { M_ABS, K_RMW, 6 },    /* ASL */
    [0x10] = { M_REL, K_BRANCH, 2 }, /* BPL */
    [0x11] = { M_IZY, K_READ, 5 },   /* ORA */
    [0x15] = { M_ZPX, K_READ, 4 },   /* ORA */
    [0x16] = { M_ZPX, K_RMW, 6 },    /* ASL */
    [0x18] = { M_IMP, K_OTHER, 2 },  /* CLC */
    [0x19] = { M_ABY, K_READ, 4 },   /* ORA */
    [0x1D] = { M_ABX, K_READ, 4 },   /* ORA */
    [0x1E] = { M_ABX, K_RMW, 7 },    /* ASL */
    [0x20] = { M_ABS, K_JSR, 6 },    /* JSR */
    [0x21] = { M_IZX, K_READ, 6 },   /* AND */
    [0x24] = { M_ZP, K_READ, 3 },    /* BIT */
    [0x25] = { M_ZP, K_READ, 3 },    /* AND */
    [0x26] = { M_ZP, K_RMW, 5 },     /* ROL */
    [0x28] = { M_IMP, K_OTHER, 4 },  /* PLP */
    [0x29] = { M_IMM, K_READ, 2 },   /* AND */
    [0x2A] = { M_IMP, K_OTHER, 2 },  /* ROL */
    [0x2C] = { M_ABS, K_READ, 4 },   /* BIT */
    [0x2D] = { M_ABS, K_READ, 4 },   /* AND */
    [0x2E] = { M_ABS, K_RMW, 6 },    /* ROL */
    [0x30] = { M_REL, K_BRANCH, 2 }, /* BMI */
    [0x31] = { M_IZY, K_READ, 5 },   /* AND */
    [0x35] = { M_ZPX, K_READ, 4 },   /* AND */
    [0x36] = { M_ZPX, K_RMW, 6 },    /* ROL */
    [0x38] = { M_IMP, K_OTHER, 2 },  /* SEC */
    [0x39] = { M_ABY, K_READ, 4 },   /* AND */
    [0x3D] = { M_ABX, K_READ, 4 },   /* AND */
    [0x3E] = { M_ABX, K_RMW, 7 },    /* ROL */
    [0x40] = { M_IMP, K_RTI, 6 },    /* RTI */
    [0x41] = { M_IZX, K_READ, 6 },   /* EOR */
    [0x45] = { M_ZP, K_READ, 3 },    /* EOR */
    [0x46] = { M_ZP, K_RMW, 5 },     /* LSR */
    [0x48] = { M_IMP, K_OTHER, 3 },  /* PHA */
    [0x49] = { M_IMM, K_READ, 2 },   /* EOR */
    [0x4A] = { M_IMP, K_OTHER, 2 },  /* LSR */
    [0x4C] = { M_ABS, K_JMP, 3 },    /* JMP */
    [0x4D] = { M_ABS, K_READ, 4 },   /* EOR */
    [0x4E] = { M_ABS, K_RMW, 6 },    /* LSR */
    [0x50] = { M_REL, K_BRANCH, 2 }, /* BVC */
    [0x51] = { M_IZY, K_READ, 5 },   /* EOR */
    [0x55] = { M_ZPX, K_READ, 4 },   /* EOR */
    [0x56] = { M_ZPX, K_RMW, 6 },    /* LSR */
    [0x58] = { M_IMP, K_OTHER, 2 },  /* CLI */
    [0x59] = { M_ABY, K_READ, 4 },   /* EOR */
    [0x5D] = { M_ABX, K_READ, 4 },   /* EOR */
    [0x5E] = { M_ABX, K_RMW, 7 },    /* LSR */
    [0x60] = { M_IMP, K_RTS, 6 },    /* RTS */
    [0x61] = { M_IZX, K_READ, 6 },   /* ADC */
    [0x65] = { M_ZP, K_READ, 3 },    /* ADC */
    [0x66] = { M_ZP, K_RMW, 5 },     /* ROR */
    [0x68] = { M_IMP, K_OTHER, 4 },  /* PLA */
    [0x69] = { M_IMM, K_READ, 2 },   /* ADC */
    [0x6A] = { M_IMP, K_OTHER, 2 },  /* ROR */
    [0x6C] = { M_IND, K_JMPI, 5 },   /* JMP */
    [0x6D] = { M_ABS, K_READ, 4 },   /* ADC */
    [0x6E] = { M_ABS, K_RMW, 6 },    /* ROR */
    [0x70] = { M_REL, K_BRANCH, 2 }, /* BVS */
    [0x71] = { M_IZY, K_READ, 5 },   /* ADC */
    [0x75] = { M_ZPX, K_READ, 4 },   /* ADC */
    [0x76] = { M_ZPX, K_RMW, 6 },    /* ROR */
    [0x78] = { M_IMP, K_OTHER, 2 },  /* SEI */
    [0x79] = { M_ABY, K_READ, 4 },   /* ADC */
    [0x7D] = { M_ABX, K_READ, 4 },   /* ADC */
    [0x7E] = { M_ABX, K_RMW, 7 },    /* ROR */
    [0x81] = { M_IZX, K_STORE, 6 },  /* STA */
    [0x84] = { M_ZP, K_STORE, 3 },   /* STY */
    [0x85] = { M_ZP, K_STORE, 3 },   /* STA */
    [0x86] = { M_ZP, K_STORE, 3 },   /* STX */
    [0x88] = { M_IMP, K_OTHER, 2 },  /* DEY */
    [0x8A] = { M_IMP, K_OTHER, 2 },  /* TXA */
    [0x8C] = { M_ABS, K_STORE, 4 },  /* STY */
    [0x8D] = { M_ABS, K_STORE, 4 },  /* STA */
    [0x8E] = { M_ABS, K_STORE, 4 },  /* STX */
    [0x90] = { M_REL, K_BRANCH, 2 }, /* BCC */
    [0x91] = { M_IZY, K_STORE, 6 },  /* STA */
    [0x94] = { M_ZPX, K_STORE, 4 },  /* STY */
    [0x95] = { M_ZPX, K_STORE, 4 },  /* STA */
    [0x96] = { M_ZPY, K_STORE, 4 },  /* STX */
    [0x98] = { M_IMP, K_OTHER, 2 },  /* TYA */
    [0x99] = { M_ABY, K_STORE, 5 },  /* STA */
    [0x9A] = { M_IMP, K_OTHER, 2 },  /* TXS */
    [0x9D] = { M_ABX, K_STORE, 5 },  /* STA */
    [0xA0] = { M_IMM, K_READ, 2 },   /* LDY */
    [0xA1] = { M_IZX, K_READ, 6 },   /* LDA */
    [0xA2] = { M_IMM, K_READ, 2 },   /* LDX */
    [0xA4] = { M_ZP, K_READ, 3 },    /* LDY */
    [0xA5] = { M_ZP, K_READ, 3 },    /* LDA */
    [0xA6] = { M_ZP, K_READ, 3 },    /* LDX */
    [0xA8] = { M_IMP, K_OTHER, 2 },  /* TAY */
    [0xA9] = { M_IMM, K_READ, 2 },   /* LDA */
    [0xAA] = { M_IMP, K_OTHER, 2 },  /* TAX */
    [0xAC] = { M_ABS, K_READ, 4 },   /* LDY */
    [0xAD] = { M_ABS, K_READ, 4 },   /* LDA */
    [0xAE] = { M_ABS, K_READ, 4 },   /* LDX */
    [0xB0] = { M_REL, K_BRANCH, 2 }, /* BCS */
    [0xB1] = { M_IZY, K_READ, 5 },   /* LDA */
    [0xB4] = { M_ZPX, K_READ, 4 },   /* LDY */
    [0xB5] = { M_ZPX, K_READ, 4 },   /* LDA */
    [0xB6] = { M_ZPY, K_READ, 4 },   /* LDX */
    [0xB8] = { M_IMP, K_OTHER, 2 },  /* CLV */
    [0xB9] = { M_ABY, K_READ, 4 },   /* LDA */
    [0xBA] = { M_IMP, K_OTHER, 2 },  /* TSX */
    [0xBC] = { M_ABX, K_READ, 4 },   /* LDY */
    [0xBD] = { M_ABX, K_READ, 4 },   /* LDA */
    [0xBE] = { M_ABY, K_READ, 4 },   /* LDX */
    [0xC0] = { M_IMM, K_READ, 2 },   /* CPY */
    [0xC1] = { M_IZX, K_READ, 6 },   /* CMP */
    [0xC4] = { M_ZP, K_READ, 3 },    /* CPY */
    [0xC5] = { M_ZP, K_READ, 3 },    /* CMP */
    [0xC6] = { M_ZP, K_RMW, 5 },     /* DEC */
    [0xC8] = { M_IMP, K_OTHER, 2 },  /* INY */
    [0xC9] = { M_IMM, K_READ, 2 },   /* CMP */
    [0xCA] = { M_IMP, K_OTHER, 2 },  /* DEX */
    [0xCC] = { M_ABS, K_READ, 4 },   /* CPY */
    [0xCD] = { M_ABS, K_READ, 4 },   /* CMP */
    [0xCE] = { M_ABS, K_RMW, 6 },    /* DEC */
    [0xD0] = { M_REL, K_BRANCH, 2 }, /* BNE */
    [0xD1] = { M_IZY, K_READ, 5 },   /* CMP */
    [0xD5] = { M_ZPX, K_READ, 4 },   /* CMP */
    [0xD6] = { M_ZPX, K_RMW, 6 },    /* DEC */
    [0xD8] = { M_IMP, K_OTHER, 2 },  /* CLD */
    [0xD9] = { M_ABY, K_READ, 4 },   /* CMP */
    [0xDD] = { M_ABX, K_READ, 4 },   /* CMP */
    [0xDE] = { M_ABX, K_RMW, 7 },    /* DEC */
    [0xE0] = { M_IMM, K_READ, 2 },   /* CPX */
    [0xE1] = { M_IZX, K_READ, 6 },   /* SBC */
    [0xE4] = { M_ZP, K_READ, 3 },    /* CPX */
    [0xE5] = { M_ZP, K_READ, 3 },    /* SBC */
    [0xE6] = { M_ZP, K_RMW, 5 },     /* INC */
    [0xE8] = { M_IMP, K_OTHER, 2 },  /* INX */
    [0xE9] = { M_IMM, K_READ, 2 },   /* SBC */
    [0xEA] = { M_IMP, K_OTHER, 2 },  /* NOP */
    [0xEC] = { M_ABS, K_READ, 4 },   /* CPX */
    [0xED] = { M_ABS, K_READ, 4 },   /* SBC */
    [0xEE] = { M_ABS, K_RMW, 6 },    /* INC */
    [0xF0] = { M_REL, K_BRANCH, 2 }, /* BEQ */
    [0xF1] = { M_IZY, K_READ, 5 },   /* SBC */
    [0xF5] = { M_ZPX, K_READ, 4 },   /* SBC */
    [0xF6] = { M_ZPX, K_RMW, 6 },    /* INC */
    [0xF8] = { M_IMP, K_OTHER, 2 },  /* SED */
    [0xF9] = { M_ABY, K_READ, 4 },   /* SBC */
    [0xFD] = { M_ABX, K_READ, 4 },   /* SBC */
    [0xFE] = { M_ABX, K_RMW, 7 },    /* INC */
};

static const uint8_t LENGTH[] = { 1, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3 };

TimeStep dc_path[MAX_PATH];
int dc_npath;
uint64_t dc_cycles;
uint32_t dc_op_at[MAX_BUS_OPS];
TimeMark dc_marks[MAX_MARKS];
int dc_nmarks;

static struct {
    bool on;
    int paused;
    uint16_t pc;
    uint8_t bank;
    int pending_bank;
    int depth;
    uint16_t stack[MAX_DEPTH];
} w;

static bool active(void)
{
    return w.on && !w.paused && !dc_run_original;
}

static Opcode decode(uint16_t pc, uint16_t *operand)
{
    Opcode op = OPCODES[Game_ReadPrg(w.bank, pc)];
    uint16_t lo = Game_ReadPrg(w.bank, (uint16_t)(pc + 1));
    *operand = LENGTH[op.mode] == 3 ? (uint16_t)(lo | Game_ReadPrg(w.bank, (uint16_t)(pc + 2)) << 8) : lo;
    return op;
}

const char *dc_mark_text(int mark, char *buf, size_t size)
{
    if (mark < 0 || mark >= dc_nmarks) {
        snprintf(buf, size, "the routine's start");
        return buf;
    }
    const TimeMark *m = &dc_marks[mark];
    if (!m->file) {
        snprintf(buf, size, "%s ($%04X)", m->what, m->pc);
        return buf;
    }
    const char *file = m->file;
    for (const char *p = m->file; *p; p++) {
        if ((p[0] == 's' && p[1] == 'r' && p[2] == 'c' && (p[3] == '/' || p[3] == '\\'))) {
            file = p;
        }
    }
    snprintf(buf, size, "%s($%04X) at %s:%d", m->what, m->pc, file, m->line);
    for (char *p = buf; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    return buf;
}

static void fail(const char *fmt, ...)
{
    char what[160], mark[200], msg[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(what, sizeof what, fmt, ap);
    va_end(ap);
    snprintf(msg, sizeof msg, "TIMING: %s, after %s", what, dc_mark_text(dc_nmarks - 1, mark, sizeof mark));
    w.on = false;
    dc_fail(msg, dc_top->addr);
}

static void mark(const char *what, const char *file, int line, uint16_t pc)
{
    if (dc_nmarks == MAX_MARKS) {
        fail("more than %d marks in one call", MAX_MARKS);
    }
    dc_marks[dc_nmarks++] = (TimeMark){ what, file, line, pc };
}

static void run(uint16_t pc, unsigned cycles)
{
    if (dc_npath == MAX_PATH) {
        fail("more than %d instructions in one call", MAX_PATH);
    }
    dc_path[dc_npath++] = (TimeStep){ pc, (uint16_t)(dc_nmarks - 1), (uint32_t)dc_cycles };
    dc_cycles += cycles;
}

static bool absolute(Opcode op)
{
    return op.mode == M_ABS || op.mode == M_ABX || op.mode == M_ABY;
}

static bool memory(Opcode op)
{
    return op.kind == K_READ || op.kind == K_STORE || op.kind == K_RMW;
}

static bool device(Opcode op, uint16_t operand)
{
    return memory(op) && absolute(op) && operand >= 0x2000 && operand < 0x4020;
}

/* One instruction that needs no mark. */
static void step(void)
{
    uint16_t pc = w.pc, operand;
    Opcode op = decode(pc, &operand);
    switch (op.kind) {
        case K_NONE:
            fail("the path reaches $%04X, which holds no official opcode", pc);
            return;
        case K_BRANCH:
            fail("the path reaches the branch at $%04X with no DC_BR", pc);
            return;
        case K_JMPI:
            fail("the path reaches the indirect jump at $%04X with no DC_JUMP", pc);
            return;
        case K_RTI:
            fail("the path reaches the RTI at $%04X", pc);
            return;
        case K_RTS:
            if (!w.depth) {
                fail("the path returns at $%04X before it gets there", pc);
            }
            run(pc, op.cycles);
            w.pc = w.stack[--w.depth];
            return;
        case K_JSR:
            if (w.depth == MAX_DEPTH) {
                fail("the path nests more than %d calls", MAX_DEPTH);
            }
            run(pc, op.cycles);
            w.stack[w.depth++] = (uint16_t)(pc + 3);
            w.pc = operand;
            return;
        case K_JMP:
            run(pc, op.cycles);
            w.pc = operand;
            return;
        default:
            break;
    }
    if (device(op, operand)) {
        fail("the path reaches the device access at $%04X with no bus operation", pc);
    }
    if ((op.kind == K_STORE || op.kind == K_RMW) && absolute(op) && operand >= 0x8000) {
        if (w.pending_bank < 0) {
            fail("the path writes the mapper at $%04X with no Decomp_SelectBank", pc);
        }
        w.bank = (uint8_t)w.pending_bank;
        w.pending_bank = -1;
    }
    run(pc, op.cycles);
    w.pc = (uint16_t)(pc + LENGTH[op.mode]);
}

static void walk_to(uint16_t target)
{
    for (int n = 0; w.pc != target; n++) {
        if (n == WALK_LIMIT) {
            fail("the path does not reach $%04X", target);
        }
        step();
    }
}

void dc_timing_begin(const DecompRoutine *rt)
{
    memset(&w, 0, sizeof w);
    dc_npath = dc_nmarks = 0;
    dc_cycles = 0;
    w.on = rt->timed;
    w.pc = rt->addr;
    w.bank = dc_c_bank;
    w.pending_bank = -1;
}

void dc_timing_end(const DecompRoutine *rt)
{
    if (!active()) {
        w.on = false;
        return;
    }
    if (dc_handoff_set) {
        walk_to(dc_handoff);
        mark("the hand-off", NULL, 0, dc_handoff);
    } else {
        for (int n = 0;; n++) {
            uint16_t operand;
            Opcode op = decode(w.pc, &operand);
            if (!w.depth && op.kind == (rt->interrupt ? K_RTI : K_RTS)) {
                mark(rt->interrupt ? "the RTI" : "the RTS", NULL, 0, w.pc);
                run(w.pc, op.cycles);
                break;
            }
            if (n == WALK_LIMIT) {
                fail("the path does not return");
            }
            step();
        }
    }
    w.on = false;
}

bool dc_timing_on(void)
{
    return active();
}

void dc_timing_pause(bool pause)
{
    w.paused += pause ? 1 : -1;
}

void dc_timing_enter(uint16_t routine, const char *what)
{
    if (!active()) {
        return;
    }
    walk_to(routine);
    mark(what, NULL, 0, routine);
}

void dc_timing_bank(uint8_t bank)
{
    if (active()) {
        w.pending_bank = bank;
    }
}

void dc_timing_op(int index)
{
    if (!active()) {
        return;
    }
    const BusOp *bus = &dc_ops[index];
    if (bus->wait) {
        fail("a timed routine waits on a device (Decomp_BusWait)");
    }
    bool mapper = bus->addr >= 0x8000;
    for (int n = 0;; n++) {
        uint16_t pc = w.pc, operand;
        Opcode op = decode(pc, &operand);
        bool reaches = op.mode == M_ABS ? operand == bus->addr : bus->addr >= operand && bus->addr - operand < 0x100;
        bool access = mapper ? op.kind == K_STORE && absolute(op) && operand >= 0x8000 : device(op, operand);
        if (access && reaches) {
            if (op.kind == K_RMW || (op.kind == K_STORE) != bus->write) {
                fail("the bus operation on $%04X does not match the access at $%04X", bus->addr, pc);
            }
            mark(bus->write ? "the bus write" : "the bus read", NULL, 0, bus->addr);
            dc_op_at[index] = (uint32_t)(dc_cycles + op.cycles - 1);
            if (mapper) {
                w.bank = dc_c_bank;
                w.pending_bank = -1;
            }
            run(pc, op.cycles);
            w.pc = (uint16_t)(pc + LENGTH[op.mode]);
            return;
        }
        if (n == WALK_LIMIT) {
            fail("the path does not reach the access of $%04X", bus->addr);
        }
        step();
    }
}

bool Decomp_Branch(uint16_t pc, bool taken, const char *file, int line)
{
    if (!active()) {
        return taken;
    }
    walk_to(pc);
    mark("DC_BR", file, line, pc);
    uint16_t operand;
    Opcode op = decode(pc, &operand);
    if (op.kind != K_BRANCH) {
        fail("DC_BR names $%04X, which is no branch", pc);
    }
    uint16_t next = (uint16_t)(pc + 2);
    uint16_t target = (uint16_t)(next + (int8_t)operand);
    run(pc, op.cycles + (taken ? 1u + ((next ^ target) >> 8 != 0) : 0u));
    w.pc = taken ? target : next;
    return taken;
}

void Decomp_Jump(uint16_t pc, uint16_t target, const char *file, int line)
{
    if (!active()) {
        return;
    }
    walk_to(pc);
    mark("DC_JUMP", file, line, pc);
    uint16_t operand;
    Opcode op = decode(pc, &operand);
    if (op.kind != K_JMPI && op.kind != K_RTS && op.kind != K_RTI) {
        fail("DC_JUMP names $%04X, which is no indirect jump, RTS or RTI", pc);
    }
    run(pc, op.cycles);
    w.pc = target;
}

void Decomp_Address(uint16_t pc, uint16_t addr, const char *file, int line)
{
    if (!active()) {
        return;
    }
    walk_to(pc);
    mark("DC_EA", file, line, pc);
    uint16_t operand;
    Opcode op = decode(pc, &operand);
    if (op.kind != K_READ || (op.mode != M_ABX && op.mode != M_ABY && op.mode != M_IZY)) {
        fail("DC_EA names $%04X, which is no indexed read", pc);
    }
    if (device(op, operand)) {
        fail("DC_EA names the device access at $%04X", pc);
    }
    uint16_t base = op.mode == M_IZY ? (uint16_t)(gRam[operand & 0xFF] | gRam[(operand + 1) & 0xFF] << 8) : operand;
    run(pc, op.cycles + ((base ^ addr) >> 8 != 0));
    w.pc = (uint16_t)(pc + LENGTH[op.mode]);
}
