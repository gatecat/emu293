#pragma once
#include <stdint.h>
#include <array>
#include <vector>
#include "../helper.h"
#undef NDEBUG
#include <assert.h>


namespace Emu293 {
    enum ScoreReg : int8_t {
        // special flags
        SCORE_PCREL = -3, // PC-relative
        SCORE_IMM = -2, // use immediate
        SCORE_NULL = -1, // discard result
        // registers
        SCORE_Rn = 0, // 0-32: basic registers
        SCORE_LO = 32,
        SCORE_HI = 33,
        SCORE_LOHI = 34,
        SCORE_CRn = 64, // 32x control registers
        SCORE_SRn = 96, // 3x special registers
        // internal recompiler use
        SCORE_CYC = 110,
        SCORE_NPC = 111,
        SCORE_TMP0 = 112,
        SCORE_TMP1 = 113,
    };

    enum ScoreCond : uint8_t {
        SCORE_CS = 0,
        SCORE_CC = 1,
        SCORE_GTU = 2,
        SCORE_LEU = 3,
        SCORE_EQ = 4,
        SCORE_NE = 5,
        SCORE_GT = 6,
        SCORE_LE = 7,
        SCORE_GE = 8,
        SCORE_LT = 9,
        SCORE_MI = 10,
        SCORE_PL = 11,
        SCORE_VS = 12,
        SCORE_VC = 13,
        SCORE_CNZ = 14, // never in IL
        SCORE_AL = 15,
    };

    extern const std::array<std::string, 16> cond_str;

    struct ScoreVal {
        ScoreVal() {};
        explicit ScoreVal(ScoreReg reg) : reg(reg) {};
        ScoreVal(ScoreReg reg, uint32_t imm) : reg(reg), imm(imm) {};
        ScoreReg reg = SCORE_NULL;
        uint32_t imm = 0;
        static ScoreVal R(int reg) {
            assert(reg < 32);
            return ScoreVal(ScoreReg(SCORE_Rn + reg));
        };
        static ScoreVal RR(int reg, int32_t ofs) {
            assert(reg < 32);
            return ScoreVal(ScoreReg(SCORE_Rn + reg), uint32_t(ofs));
        }
        static ScoreVal HI() { return ScoreVal(SCORE_HI); }
        static ScoreVal LO() { return ScoreVal(SCORE_LO); }
        static ScoreVal CR(int reg) { return ScoreVal(ScoreReg(SCORE_CRn + reg)); }
        static ScoreVal SR(int reg) { return ScoreVal(ScoreReg(SCORE_SRn + reg)); }
        static ScoreVal I(uint32_t imm) { return ScoreVal(SCORE_IMM, imm); }
        static ScoreVal PC(int32_t ofs = 0) { return ScoreVal(SCORE_PCREL, uint32_t(ofs)); }
        std::string str() const;
    };

    enum class ScoreOp : uint8_t {
#define X(t) t,
#include "recompiler_ops.inc"
#undef X
    };

    extern const std::vector<std::string> op_names;

    struct OpEntry {
        OpEntry() {};
        OpEntry(ScoreOp op, ScoreVal dest = ScoreVal(),
            ScoreVal src0 = ScoreVal(), ScoreVal src1 = ScoreVal(),
            ScoreCond cond = SCORE_AL, bool set_flags = false) :
            op(op), dest(dest), src0(src0), src1(src1), cond(cond), set_flags(set_flags) {
        }
        ScoreOp op = ScoreOp::NOP;
        ScoreVal dest;
        ScoreVal src0;
        ScoreVal src1;
        ScoreCond cond = SCORE_AL;
        bool set_flags = false;
        static OpEntry NOP() {
            return OpEntry(ScoreOp::NOP);
        }
        static OpEntry MOV(ScoreVal dest, ScoreVal src, ScoreCond cond = SCORE_AL) {
            return OpEntry(ScoreOp::MOVE, dest, src, ScoreVal(), cond);
        }
        static OpEntry ALU(ScoreOp op, ScoreVal dest, ScoreVal src0, ScoreVal src1 = {}, bool set_flags = false) {
            return OpEntry(op, dest, src0, src1, SCORE_AL, set_flags);
        }
        static OpEntry LD(ScoreOp op, ScoreVal dest, ScoreVal addr) {
            return OpEntry(op, dest, addr);
        }
        static OpEntry ST(ScoreOp op, ScoreVal value, ScoreVal addr) {
            return OpEntry(op, ScoreVal(), value, addr);
        }

        std::string str();
    };

    struct ScoreChunk {
        uint32_t start_pc;
        std::vector<OpEntry> ops;
        // (pc offset/2) -> index into ops
        std::vector<int> pc2op;
        uint32_t end_pc() const {
            return start_pc + 2*pc2op.size(); // pc is byte address, pc2op is word based
        }
        void debug_dump();
    };
};