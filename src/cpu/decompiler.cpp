#include "recompiler_il.h"
#include "recompiler_exec.h"

#include <stdio.h>
namespace Emu293 {

static int32_t sign_extend(uint32_t x, uint8_t b) {
  uint32_t m = 1UL << (b - 1);

  x = x & ((1UL << b) - 1);
  return (x ^ m) - m;
}

struct ChunkDecompiler {
    ChunkDecompiler(uint32_t start_pc, const uint8_t *progmem, uint32_t progmem_mask = 0x01FFFFFC)
        : start_pc(start_pc), progmem(progmem), progmem_mask(progmem_mask) {};
    uint32_t start_pc = 0xa0000000;
    const uint8_t *progmem = nullptr;
    uint32_t progmem_mask = 0x01FFFFFC;

    ScoreChunk ch;

    // working stuff...
    uint32_t pc;
    uint32_t furthest_near_branch;
    bool codepath_end = false;

    // TODO: decompile should stop if it hits already-decompiled code...
    void init() {
        pc = start_pc;
        furthest_near_branch = pc;
        ch.start_pc = start_pc;
    }

    void run() {
        init();
        while(!codepath_end) {
            bool result = step();
            if (!result)
                break;
        }
    }

    bool step() {
        // fetch instruction and do 15/30-bit decode
        codepath_end = false;
        uint32_t val = get_uint32le(progmem + (pc & progmem_mask));
        int label = int(ch.ops.size());
        if ((pc & 0x3) == 0x02)
            val >>= 16;
        bool is_16b = false;
        if (((val & 0x80008000) == 0x80008000) && ((pc & 0x03) == 0)) {
            // Remove p0 and p1 bits before handling the instruction as 30bit
            uint32_t instruction = val;
            uint32_t w2 = instruction & 0xFFFF0000;
            instruction &= 0x00007FFF;
            instruction |= w2 >> 1;
            instruction &= 0x3FFFFFFF;
            if(!decomp32(instruction)) {
                printf("DYNAREC WARN: unsupported 32-bit instruction %08x at %08x\n", val, pc);
                return false;
            }
        } else {
            if ((val & 0x8000) && ((pc & 0x03) == 0)) {
                printf("DYNAREC WARN: unsupported PCE at pc=%08x\n", pc);
                return false;
            } else {
                is_16b = true;
                if(!decomp16(val & 0x7FFF)) {
                    printf("DYNAREC WARN: unsupported 16-bit instruction %04x at %08x\n", val & 0x7FFF, pc);
                    return false;
                }
            }
        }
         // add label for current PC if decompiling succeeded
        while (ch.pc2op.size() < (pc-start_pc)/2)
            ch.pc2op.push_back(-1); // pad with null labels
        ch.pc2op.push_back(label);
        pc += (is_16b ? 2 : 4);
        return true;
    }
    void emit(OpEntry e) {
        ch.ops.push_back(e);
    }
    bool decomp32(uint32_t instr) {
        uint8_t op = (instr >> 25) & 0x1F;
        switch (op) {
        case 0x00: {
            uint8_t rD = (instr >> 20) & 0x1F;
            uint8_t rA = (instr >> 15) & 0x1F;
            uint8_t rB = (instr >> 10) & 0x1F;
            uint8_t func6 = (instr >> 1) & 0x3F;
            bool cu = instr & 0x1;
            switch (func6) {
                case 0x00: // nop
                    emit(OpEntry::NOP());
                    return true;
                case 0x04: // br{cond}[l] rA
                    return false; // TODO
                case 0x08: // add[.c] rD, rA, rB
                    emit(OpEntry::ALU(ScoreOp::ADD, ScoreVal::R(rD), ScoreVal::R(rA), ScoreVal::R(rB), cu));
                    return true;
                case 0x09: // addc[.c] rD, rA, rB
                    emit(OpEntry::ALU(ScoreOp::ADDC, ScoreVal::R(rD), ScoreVal::R(rA), ScoreVal::R(rB), cu));
                    return true;
                case 0x0A: // sub[.c] rD, rA, rB
                    emit(OpEntry::ALU(ScoreOp::SUB, ScoreVal::R(rD), ScoreVal::R(rA), ScoreVal::R(rB), cu));
                    return true;
                case 0x0B: // subc[.c] rD, rA, rB
                    emit(OpEntry::ALU(ScoreOp::SUBC, ScoreVal::R(rD), ScoreVal::R(rA), ScoreVal::R(rB), cu));
                    return true;
                case 0x0C: // cmp{tcs}.c RA, rB
                    if (rD & 0x03 != 0x03)
                        return false; // {tcs} not supported
                    emit(OpEntry::ALU(ScoreOp::SUB, ScoreVal(), ScoreVal::R(rA), ScoreVal::R(rB), true));
                    return true;
                case 0x0D: // cmpz{tcs}.c RA, rB
                    if (rD & 0x03 != 0x03)
                        return false; // {tcs} not supported
                    emit(OpEntry::ALU(ScoreOp::SUB, ScoreVal(), ScoreVal::R(rA), ScoreVal::I(0), true));
                    return true;
                case 0x0F: // neg[.c] rD, rB
                    emit(OpEntry::ALU(ScoreOp::SUB, ScoreVal::R(rD), ScoreVal::I(0), ScoreVal::R(rB), cu));
                    return true;
                case 0x10: // and[.c] rD, rA, rB
                    emit(OpEntry::ALU(ScoreOp::AND, ScoreVal::R(rD), ScoreVal::R(rA), ScoreVal::R(rB), cu));
                    return true;
                case 0x11: // or[.c] rD, rA, rB
                    emit(OpEntry::ALU(ScoreOp::OR, ScoreVal::R(rD), ScoreVal::R(rA), ScoreVal::R(rB), cu));
                    return true;
                case 0x12: // not[.c] rD, rA, rB
                    emit(OpEntry::ALU(ScoreOp::XOR, ScoreVal::R(rD), ScoreVal::R(rA), ScoreVal::I(0xFFFFFFFF), cu));
                    return true;
                case 0x13: // xor[.c] rD, rA, rB
                    emit(OpEntry::ALU(ScoreOp::XOR, ScoreVal::R(rD), ScoreVal::R(rA), ScoreVal::R(rB), cu));
                    return true;
                case 0x14: // bitclr[.c] rD, rA, imm5
                    emit(OpEntry::ALU(ScoreOp::AND, ScoreVal::R(rD), ScoreVal::R(rA), ScoreVal::I(~(1U << rB)), cu));
                    return true;
                case 0x15: // bitset[.c] rD, rA, imm5
                    emit(OpEntry::ALU(ScoreOp::OR, ScoreVal::R(rD), ScoreVal::R(rA), ScoreVal::I(1U << rB), cu));
                    return true;
                case 0x16: // bittst[.c] rD, rA, imm5
                    emit(OpEntry::ALU(ScoreOp::AND, ScoreVal(), ScoreVal::R(rA), ScoreVal::I(1U << rB), cu));
                    return true;
                case 0x17: // bittgl[.c] rA, imm5
                    emit(OpEntry::ALU(ScoreOp::XOR, ScoreVal(), ScoreVal::R(rA), ScoreVal::I(1U << rB), cu));
                    return true;
                case 0x18: // sll[.c] rD, rA, rB
                    emit(OpEntry::ALU(ScoreOp::SLL, ScoreVal::R(rD), ScoreVal::R(rA), ScoreVal::R(rB), cu));
                    return true;
                case 0x1A: // srl[.c] rD, rA, rB
                    emit(OpEntry::ALU(ScoreOp::SRL, ScoreVal::R(rD), ScoreVal::R(rA), ScoreVal::R(rB), cu));
                    return true;
                case 0x1B: // sra[.c] rD, rA, rB
                    emit(OpEntry::ALU(ScoreOp::SRA, ScoreVal::R(rD), ScoreVal::R(rA), ScoreVal::R(rB), cu));
                    return true;
                case 0x1C: // ror[.c] rD, rA, rB
                    emit(OpEntry::ALU(ScoreOp::ROR, ScoreVal::R(rD), ScoreVal::R(rA), ScoreVal::R(rB), cu));
                    return true;
                case 0x1E: // rol[.c] rD, rA, rB
                    emit(OpEntry::ALU(ScoreOp::ROL, ScoreVal::R(rD), ScoreVal::R(rA), ScoreVal::R(rB), cu));
                    return true;
                case 0x2B: // mv{cond} rD, rA
                    emit(OpEntry::MOV(ScoreVal::R(rD), ScoreVal::R(rA), ScoreCond(rB)));
                    return true;
                // TODO: ...
            }
        } break;
        case 0x01: {
            uint8_t rD = (instr >> 20) & 0x1F;
            uint8_t func3 = (instr >> 17) & 0x7;
            uint32_t imm16 = (instr >> 1) & 0xFFFF;
            bool cu = instr & 0x1;
            switch (func3) {
                case 0x6: // ldi rD, imm16
                    emit(OpEntry::MOV(ScoreVal::R(rD), ScoreVal::I(sign_extend(imm16, 16))));
                    return true;
            }
        }; break;
        case 0x05: {
            uint8_t rD = (instr >> 20) & 0x1F;
            uint8_t func3 = (instr >> 17) & 0x7;
            uint32_t imm16 = (instr >> 1) & 0xFFFF;
            bool cu = instr & 0x1;
            switch (func3) {
                case 0x6: // ldis rD, imm16
                    emit(OpEntry::MOV(ScoreVal::R(rD), ScoreVal::I(imm16 << 16U)));
                    return true;
            }
        }; break;
        };
        return false;
    }
    bool decomp16(uint32_t instr) {
        uint8_t op = (instr >> 12) & 0x7;
        switch (op) {
        case 0x0: {
            uint8_t rD = (instr >> 8) & 0xF;
            uint8_t rA = (instr >> 4) & 0xF;
            uint8_t func4 = instr & 0xF;
            switch (func4) {
                case 0x3: // mv! rDg0, rAg0
                    emit(OpEntry::MOV(ScoreVal::R(rD), ScoreVal::R(rA)));
                    return true;
            }
        }; break;
        case 0x2: {
            uint8_t rDgh = (instr >> 8) & 0xF;
            bool h = (instr & 0x80);
            uint8_t rDh = h ? (rDgh + 16) : rDgh;
            uint8_t rAg = (instr >> 4) & 0x7;
            uint8_t func4 = instr & 0xF;
            switch (func4) {
                case 0xA: // pop! rDgh, [rAg0]
                    emit(OpEntry::ST(ScoreOp::LOAD32, ScoreVal::R(rDh), ScoreVal::R(rAg)));
                    emit(OpEntry::ALU(ScoreOp::ADD, ScoreVal::R(rAg), ScoreVal::R(rAg), ScoreVal::I(4)));
                    return true;
                case 0xE: // push! rDgh, [rAg0]
                    emit(OpEntry::ALU(ScoreOp::SUB, ScoreVal::R(rAg), ScoreVal::R(rAg), ScoreVal::I(4)));
                    emit(OpEntry::ST(ScoreOp::STORE32, ScoreVal::R(rDh), ScoreVal::R(rAg)));
                    return true;
            }
        }; break;
        case 0x7: {
            uint8_t rD = (instr >> 8) & 0x0F;
            uint8_t imm5 = (instr >> 3) & 0x1F;
            uint8_t func3 = (instr & 0x7);
            switch (func3) {
                case 0x4: // swp! rDg0, Imm7
                    emit(OpEntry::ST(ScoreOp::STORE32, ScoreVal::R(rD), ScoreVal::RR(2, imm5 << 2U)));
                    return true;
            }
        }; break;
        }
        return false;
    }
};

ScoreChunk do_decompile(uint32_t start_pc, const uint8_t *progmem, uint32_t progmem_mask) {
    ChunkDecompiler dec(start_pc, progmem, progmem_mask);
    dec.run();
    return dec.ch;
}

}