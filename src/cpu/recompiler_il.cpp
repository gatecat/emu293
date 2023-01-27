#include "recompiler_il.h"

namespace Emu293 {
    const std::vector<std::string> op_names = {
        #define X(t) #t ,
        #include "recompiler_ops.inc"
        #undef X
    };
    const std::array<std::string, 16> cond_str = {
        "CS", "CC", "GTU", "LEU", "EQ", "NE", "GT", "LE", "GE", "LT", "MI", "PL", "VS", "VC", "CNZ", "",
    };
    std::string ScoreVal::str() const {
        if (reg == SCORE_NULL)
            return "<void>";
        else if (reg == SCORE_IMM)
            return stringf("0x%x", imm);
        else if (reg == SCORE_PCREL)
            return stringf("(PC + %d)", int32_t(imm));
        else if (reg >= SCORE_Rn && reg < (SCORE_CRn+32))
            return imm != 0 ? stringf("(r%d + %d)", reg-SCORE_Rn, int32_t(imm)) : stringf("r%d", reg-SCORE_Rn);
        else if (reg == SCORE_LO)
            return "lo";
        else if (reg == SCORE_HI)
            return "hi";
        else if (reg == SCORE_LOHI)
            return "{hi,lo}";
        else if (reg >= SCORE_CRn && reg < (SCORE_CRn+32))
            return stringf("cr%d", reg-SCORE_CRn);
        else if (reg >= SCORE_SRn && reg < (SCORE_SRn+4))
            return stringf("sr%d", reg-SCORE_SRn);
        else
            return stringf("<%d?>", reg);
    }
    std::string OpEntry::str() {
        std::string result;
        if (cond != SCORE_AL)
            result += stringf("if (%s) ", cond_str.at(cond).c_str());
        if (dest.reg != SCORE_NULL) {
            result += dest.str();
            result += " = ";
        }
        result += op_names.at(uint32_t(op));
        result += '(';
        if (src0.reg != SCORE_NULL)
            result += src0.str();
        if (src1.reg != SCORE_NULL) {
            result += ", ";
            result += src1.str();
        }
        result += ')';
        if (set_flags)
            result += ", flags";
        return result;
    }
    void ScoreChunk::debug_dump() {
        int pc_idx = -1;
        for (int i = 0; i < int(ops.size()); i++) {
            // check if we've advanced PC by 2 or 4 (16b or 32b instr)
            bool adv = false;
            for (int delta : {1, 2}) {
                int next_idx = pc_idx + delta;
                if (next_idx < int(pc2op.size()) && pc2op.at(next_idx) == i) {
                    pc_idx = next_idx;
                    adv = true;
                    printf("%08x ", start_pc + pc_idx*2);
                    break;
                }
            }
            if (!adv)
                printf("         ");
            // print instruction
            auto op = ops.at(i).str();
            printf("%s\n", op.c_str());
        }
    }
}