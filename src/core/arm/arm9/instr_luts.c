#include "../../utils.h"
#include "../shared/instr.h"
#include "instr_luts.h"
#include "instr_il.h"
#include "arm.h"




// TODO: making this compile time generated might enable better compiler optimizations?
void (*ARM9_InstructionLUT[0x1000])(struct ARM*, struct ARM_Instr);
s8 (*ARM9_InterlockLUT[0x1000])(struct ARM946ES*, struct ARM_Instr);
void (*THUMB9_InstructionLUT[0x1000])(struct ARM*, struct ARM_Instr);
s8 (*THUMB9_InterlockLUT[0x1000])(struct ARM946ES*, struct ARM_Instr);


// these should all be sorted in order of likelyhood of usage:
// blx immediate is actually useful.
// pld is a nop; this isn't useful but it looks like a useful instruction so someone might wind up using it maybe.
// mcr2 and mrc2 are functionally copies of mcr and mrc so maybe you could use them for something i guess???
// the rest just raise undefined instruction exceptions

#define CHECK(cmp, mask, ptr) \
if (PatternMatch((struct Pattern) {0b##cmp, 0b##mask}, instr_data.Raw)) \
    ARM##ptr(cpu, instr_data); \
else

void ARM9_Uncond(struct ARM* cpu, const struct ARM_Instr instr_data)
{
    CHECK(1111'1010'0000'0000'0000'0000'0000'0000, 1111'1110'0000'0000'0000'0000'0000'0000, _BLXImm) // BLX IMM
    CHECK(1111'0101'0101'0000'1111'0000'0000'0000, 1111'1101'0111'0000'1111'0000'0000'0000, _UNIMPL) // PLD
    CHECK(1111'1110'0000'0000'0000'0000'0001'0000, 1111'1111'0001'0000'0000'0000'0001'0000, _MCR2)
    CHECK(1111'1110'0001'0000'0000'0000'0001'0000, 1111'1111'0001'0000'0000'0000'0001'0000, _MRC2)
    CHECK(1111'1110'0000'0000'0000'0000'0000'0000, 1111'1111'0000'0000'0000'0000'0001'0000, _UNIMPL) // CDP2
    CHECK(1111'1100'0001'0000'0000'0000'0000'0000, 1111'1110'0001'0000'0000'0000'0000'0000, _UNIMPL) // LDC2
    CHECK(1111'1100'0000'0000'0000'0000'0000'0000, 1111'1110'0001'0000'0000'0000'0000'0000, _UNIMPL) // STC2
    CHECK(1111'1100'0100'0000'0000'0000'0000'0000, 1111'1111'1111'0000'0000'0000'0000'0000, _UNIMPL) // MCRR2
    CHECK(1111'1100'0101'0000'0000'0000'0000'0000, 1111'1111'1111'0000'0000'0000'0000'0000, _UNIMPL) // MRRC2
    CHECK(1111'0001'0010'0000'0000'0000'0111'0000, 1111'1111'1111'0000'0000'0000'1111'0000, 9_PrefetchAbort) // special BKPT handling
    ARM9_UndefinedInstruction(cpu, instr_data); // checkme?
}

#undef CHECK

#define CHECK(cmp, mask, ptr) \
if (PatternMatch((struct Pattern) {0b##cmp, 0b##mask}, instr_data.Raw)) \
    return ARM9_##ptr##_Interlocks(ARM9, instr_data); \
else

s8 ARM9_Uncond_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    CHECK(1111'1010'0000'0000'0000'0000'0000'0000, 1111'1110'0000'0000'0000'0000'0000'0000, None) // BLX IMM
    CHECK(1111'0101'0101'0000'1111'0000'0000'0000, 1111'1101'0111'0000'1111'0000'0000'0000, UNIMPL) // PLD
    // Note: all of these use the same interlock behaviors as their standard counterparts
    CHECK(1111'1110'0000'0000'0000'0000'0001'0000, 1111'1111'0001'0000'0000'0000'0001'0000, MCR) // MCR2
    CHECK(1111'1110'0001'0000'0000'0000'0001'0000, 1111'1111'0001'0000'0000'0000'0001'0000, MRC) // MRC2
    CHECK(1111'1110'0000'0000'0000'0000'0000'0000, 1111'1111'0000'0000'0000'0000'0001'0000, UNIMPL) // CDP2
    CHECK(1111'1100'0001'0000'0000'0000'0000'0000, 1111'1110'0001'0000'0000'0000'0000'0000, LDC) // LDC2
    CHECK(1111'1100'0000'0000'0000'0000'0000'0000, 1111'1110'0001'0000'0000'0000'0000'0000, UNIMPL) // STC2
    CHECK(1111'1100'0100'0000'0000'0000'0000'0000, 1111'1111'1111'0000'0000'0000'0000'0000, UNIMPL) // MCRR2
    CHECK(1111'1100'0101'0000'0000'0000'0000'0000, 1111'1111'1111'0000'0000'0000'0000'0000, UNIMPL) // MRRC2
    return ARM9_None_Interlocks(ARM9, instr_data); // UDF / BKPT
}

#undef CHECK

#define CHECK(cmp, mask, ptr) \
if (PatternMatch((struct Pattern) {0b##cmp, 0b##mask}, i)) \
{ \
    ARM9_InstructionLUT[i] = ARM_##ptr ; \
    ARM9_InterlockLUT[i] = ARM9_##ptr##_Interlocks; \
} \
else

#define CHECK9(cmp, mask, ptr) \
if (PatternMatch((struct Pattern) {0b##cmp, 0b##mask}, i)) \
{ \
    ARM9_InstructionLUT[i] = ARM9_##ptr ; \
    ARM9_InterlockLUT[i] = ARM9_None_Interlocks; \
} \
else

void ARM9_InitInstrLUT()
{
    for (u16 i = 0; i <= 0xFFF; i++)
    {
        // most specific ones should go first

        // multiply extension space
        CHECK9(0000'0100'1001, 1111'1100'1111, UndefinedInstruction) // UMAAL (undefined on ARM9)
        CHECK (0000'0000'1001, 1111'0000'1111, Mul)
        // control/dsp extension space
        CHECK (0001'0000'0000, 1111'1011'1111, MRS) // mrs
        CHECK (0001'0010'0000, 1111'1011'1111, MSR) // msr (reg)
        CHECK (0001'0010'0001, 1111'1111'1101, BranchExchange) // b(l)x (reg)
        CHECK (0001'0110'0001, 1111'1111'1111, CLZ)
        CHECK (0001'0000'0101, 1111'1001'1111, SatMath)
        CHECK9(0001'0010'0111, 1111'1111'1111, PrefetchAbort) // bkpt | we're reusing bkpt as a faster way to handle prefetch aborts
        CHECK (0001'0000'1000, 1111'1001'1001, UNIMPL) // signed multiplies
        // load/store extension space
        CHECK (0001'0000'1001, 1111'1011'1111, UNIMPL) // swp
        //CHECK (0001'1000'1001, 1111'1000'1111, UNIMPL) // ldrex/strex (and variants)
        CHECK (0000'0000'1001, 1110'0000'1001, LoadStoreMisc)
        // explicitly defined undefined space
        CHECK9(0111'1111'1111, 1111'1111'1111, UndefinedInstruction)
        // coproc extension space
        CHECK (1100'0001'0000, 1111'0001'0000, LDC) // ldc
        CHECK (1100'0000'0000, 1111'0001'0000, UNIMPL) // stc
        CHECK (1100'0100'0000, 1111'1111'0000, UNIMPL) // mcrr
        CHECK (1100'0101'0000, 1111'1111'0000, UNIMPL) // mrrc
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
        CHECK (1000'0000'0000, 1110'0000'0000, UNIMPL) // ldm/stm
        // branch
        CHECK (1010'0000'0000, 1110'0000'0000, Branch) // b/bl
        CHECK9(1111'0000'0000, 1111'0000'0000, SoftwareInterrupt)
        CHECK (1110'0000'0000, 1111'0000'0000, UNIMPL) // coprocessor instruction - checkme: longer undef?
        CHECK9(0000'0000'0000, 0000'0000'0000, UndefinedInstruction)
        unreachable();
    }
}

#undef CHECK
#undef CHECK9

#define CHECK(cmp, mask, ptr) \
if (PatternMatch((struct Pattern) {0b##cmp, 0b##mask}, decode)) \
    THUMB##ptr(ARM, instr_data); \
else

void THUMB9_Misc(struct ARM* ARM, const struct ARM_Instr instr_data)
{
    const u16 decode = (instr_data.Raw >> 3) & 0x1FF;

    CHECK(0000'0000'0, 1111'0000'0, _AdjustSP) // adjust sp
    CHECK(0100'0000'0, 1110'0000'0, _Push) // push
    CHECK(1100'0000'0, 1110'0000'0, _Pop) // pop
    CHECK(1110'0000'0, 1110'0000'0, 9_PrefetchAbort)
    CHECK(0000'0000'0, 0000'0000'0, 9_UndefinedInstruction)
    unreachable();
}

#undef CHECK
#undef CHECK9

#define CHECK(cmp, mask, ptr) \
if (PatternMatch((struct Pattern) {0b##cmp, 0b##mask}, decode)) \
    return THUMB9_##ptr##_Interlocks(ARM9, instr_data); \
else

s8 THUMB9_Misc_Interlocks(struct ARM946ES* ARM9, const struct ARM_Instr instr_data)
{
    const u16 decode = (instr_data.Raw >> 3) & 0x1FF;

    CHECK(0000'0000'0, 1111'0000'0, AdjustSP) // adjust sp
    CHECK(0100'0000'0, 1110'0000'0, Push) // push
    CHECK(1100'0000'0, 1110'0000'0, Pop) // pop
    CHECK(0000'0000'0, 0000'0000'0, None) // BKPT / UDF
    unreachable();
}

#undef CHECK

#define CHECK(cmp, mask, ptr) \
if (PatternMatch((struct Pattern) {0b##cmp, 0b##mask}, i)) \
{ \
    THUMB9_InstructionLUT[i] = THUMB_##ptr; \
    THUMB9_InterlockLUT[i] = THUMB9_##ptr##_Interlocks; \
} \
else

#define CHECK9(cmp, mask, ptr) \
if (PatternMatch((struct Pattern) {0b##cmp, 0b##mask}, i)) \
{ \
    THUMB9_InstructionLUT[i] = THUMB9_##ptr ; \
    THUMB9_InterlockLUT[i] = THUMB9_UNIMPL_Interlocks; \
} \
else

void THUMB9_InitInstrLUT()
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
        CHECK9(1011'00, 1111'00, Misc) // misc
        CHECK (1100'00, 1111'00, UNIMPL) // ldm/stm
        CHECK (1101'00, 1111'00, BranchCond) // cond b/udf/swi
        CHECK (1110'00, 1110'00, Branch) // b/bl(x) prefix/suffix
        CHECK9(0000'00, 0000'00, UndefinedInstruction)
        unreachable();
    }
}

#undef CHECK
#undef CHECK9
