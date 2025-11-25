#include "../../utils.h"
#include "../shared/instr.h"
#include "instr_luts.h"
#include "arm.h"




// TODO: making this compile time generated might enable better compiler optimizations?
void (*ARM7_InstructionLUT[0x1000])(struct ARM*, struct ARM_Instr);
void (*THUMB7_InstructionLUT[0x1000])(struct ARM*, struct ARM_Instr);


#define CHECK(cmp, mask, ptr) \
if (PatternMatch((struct Pattern) {0b##cmp, 0b##mask}, i)) \
{ \
    ARM7_InstructionLUT[i] = ARM_##ptr ; \
} \
else

#define CHECK7(cmp, mask, ptr) \
if (PatternMatch((struct Pattern) {0b##cmp, 0b##mask}, i)) \
{ \
    ARM7_InstructionLUT[i] = ARM7_##ptr ; \
} \
else

void ARM7_InitInstrLUT()
{
    for (u16 i = 0; i <= 0xFFF; i++)
    {
        // most specific ones should go first

        // multiply extension space
        CHECK (0000'0000'1001, 1111'0000'1111, Mul)
        // control/dsp extension space
        CHECK (0001'0000'0000, 1111'1011'1111, MRS) // mrs
        CHECK (0001'0010'0000, 1111'1011'1111, MSR) // msr (reg)
        CHECK (0001'0010'0001, 1111'1111'1101, BranchExchange) // b(l)x (reg)
        CHECK (0001'0110'0001, 1111'1111'1111, CLZ)
        CHECK (0001'0000'1000, 1111'1001'1001, UNIMPL) // signed multiplies
        // load/store extension space
        CHECK (0001'0000'1001, 1111'1011'1111, UNIMPL) // swp
        //CHECK (0001'1000'1001, 1111'1000'1111, UNIMPL) // ldrex/strex (and variants)
        CHECK (0000'0000'1001, 1110'0000'1001, LoadStoreMisc)
        // explicitly defined undefined space
        CHECK7(0111'1111'1111, 1111'1111'1111, UndefinedInstruction)
        // coproc extension space
        CHECK (1100'0001'0000, 1111'0001'0000, LDC) // ldc
        CHECK (1100'0000'0000, 1111'0001'0000, UNIMPL) // stc
        CHECK (1100'0000'0000, 1111'1010'0000, UNIMPL) // coprocessor? - checkme: longer undef?
        // data processing
        CHECK (0000'0000'0000, 1100'0000'0000, DataProc)
        // load/store
        CHECK (0100'0000'0000, 1100'0000'0000, LoadStore)
        // coprocessor data processing
        CHECK (1110'0000'0000, 1111'0000'0001, UNIMPL) // cdp - checkme: longer undef?
        // coprocessor register transfers
        CHECK (1110'0000'0001, 1111'0001'0001, MCR) // mcr
        CHECK (1110'0001'0001, 1111'0001'0001, MRC) // mrc
        // multiple load/store
        CHECK (1000'0000'0000, 1110'0000'0000, LoadStoreMultiple) // ldm/stm
        // branch
        CHECK (1010'0000'0000, 1110'0000'0000, Branch) // b/bl
        CHECK7(1111'0000'0000, 1111'0000'0000, SoftwareInterrupt)
        CHECK (1110'0000'0000, 1111'0000'0000, UNIMPL) // coprocessor instruction - checkme: longer undef?
        CHECK7(0000'0000'0000, 0000'0000'0000, UndefinedInstruction)
        unreachable();
    }
}

#undef CHECK
#undef CHECK7

#define CHECK(cmp, mask, ptr) \
if (PatternMatch((struct Pattern) {0b##cmp, 0b##mask}, decode)) \
    THUMB##ptr(ARM, instr_data); \
else

void THUMB7_Misc(struct ARM* ARM, const struct ARM_Instr instr_data)
{
    const u16 decode = (instr_data.Raw >> 3) & 0x1FF;

    CHECK(0000'0000'0, 1111'0000'0, _AdjustSP) // adjust sp
    CHECK(0100'0000'0, 1110'0000'0, _Push) // push
    CHECK(1100'0000'0, 1110'0000'0, _Pop) // pop
    CHECK(0000'0000'0, 0000'0000'0, 7_UndefinedInstruction)
    unreachable();
}

#undef CHECK

#define CHECK(cmp, mask, ptr) \
if (PatternMatch((struct Pattern) {0b##cmp, 0b##mask}, i)) \
{ \
    THUMB7_InstructionLUT[i] = THUMB_##ptr; \
} \
else

#define CHECK7(cmp, mask, ptr) \
if (PatternMatch((struct Pattern) {0b##cmp, 0b##mask}, i)) \
{ \
    THUMB7_InstructionLUT[i] = THUMB7_##ptr ; \
} \
else

void THUMB7_InitInstrLUT()
{
    for (int i = 0; i <= 0x3F; i++)
    {
        CHECK (0001'10, 1111'10, AddSub) // adds/subs reg/imm3
        CHECK (0000'00, 1110'00, ShiftImm) // shift imm5
        CHECK (0010'00, 1111'10, MovsImm8) // movs imm8
        CHECK (0010'00, 1110'00, DataProcImm8) // adds/sub/cmp imm8
        CHECK (0100'00, 1111'11, DataProcReg) // data proc reg
        CHECK (0100'01, 1111'11, DataProcHiReg) // data proc/b(l)x hi-regs
        CHECK (0100'10, 1111'10, LoadPCRel) // ldr pc-rel
        CHECK (0101'00, 1111'00, LoadStoreReg) // ldr/str reg offs
        CHECK (0110'00, 1111'00, LoadStoreWordImm) // ldr/str imm offs
        CHECK (0111'00, 1111'00, LoadStoreByteImm) // ldrb/strb imm offs
        CHECK (1000'00, 1111'00, LoadStoreHalfwordImm) // ldr/strh imm offs
        CHECK (1001'00, 1111'00, LoadStoreSPRel) // load/store sp-rel
        CHECK (1010'00, 1111'00, AddPCSPRel) // sp/pc-rel add?
        CHECK7(1011'00, 1111'00, Misc) // misc
        CHECK (1100'00, 1111'00, LoadStoreMultiple) // ldm/stm
        CHECK (1101'00, 1111'00, BranchCond) // cond b/udf/swi
        CHECK (1110'00, 1110'00, Branch) // b/bl(x) prefix/suffix
        CHECK7(0000'00, 0000'00, UndefinedInstruction)
        unreachable();
    }
}

#undef CHECK
#undef CHECK7
