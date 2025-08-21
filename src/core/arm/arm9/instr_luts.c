#include "../../utils.h"
#include "../shared/instr.h"
#include "instr_luts.h"
#include "instr_il.h"
#include "arm.h"




// TODO: making this compile time generated might enable better compiler optimizations?
void (*ARM9_InstructionLUT[0x1000])(struct ARM*, u32);
s8  (*ARM9_InterlockLUT[0x1000])(struct ARM946ES*, u32);
void (*THUMB9_InstructionLUT[0x1000])(struct ARM*, u16);
s8  (*THUMB9_InterlockLUT[0x1000])(struct ARM946ES*, u16);

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
        CHECK (0000'0000'1001, 1111'0000'1111, Mul)
        // control/dsp extension space
        CHECK (0001'0000'0000, 1111'1011'1111, UNIMPL) // mrs
        CHECK (0001'0010'0000, 1111'1011'1111, UNIMPL) // msr (reg)
        CHECK (0001'0010'0001, 1111'1111'1101, UNIMPL) // b(l)x (reg)
        CHECK (0001'0110'0001, 1111'1111'1111, CLZ)
        CHECK (0001'0000'0100, 1111'1001'1111, SatMath)
        CHECK9(0001'0010'0111, 1111'1111'1111, PrefetchAbort) // bkpt | we're reusing bkpt as a faster way to handle prefetch aborts
        CHECK (0001'0000'1000, 1111'1001'1001, UNIMPL) // signed multiplies
        // load/store extension space
        CHECK (0001'0000'1001, 1111'1011'1111, UNIMPL) // swp
        CHECK (0001'1000'1001, 1111'1000'1111, UNIMPL) // ldrex/strex (and variants)
        CHECK (0000'0000'1011, 1110'0000'1111, UNIMPL) // ldrh/strh
        CHECK (0000'0001'1101, 1110'0001'1101, UNIMPL) // ldrsh/ldrsb
        CHECK (0000'0000'1101, 1110'0001'1101, UNIMPL) // ldrd/strd
        // explicitly defined undefined space
        CHECK9(0111'1111'1111, 1111'1111'1111, UndefinedInstruction)
        // coproc extension space
        CHECK9(1100'0000'0000, 1111'1010'0000, UndefinedInstruction) // coprocessor? - checkme: longer undef?
        // data processing
        CHECK (0000'0000'0000, 1100'0000'0000, DataProc)
        // load/store
        CHECK (0100'0000'0000, 1100'0000'0000, UNIMPL)
        // coprocessor data processing
        CHECK9(1110'0000'0000, 1111'0000'0001, UndefinedInstruction) // cdp - checkme: longer undef?
        // coprocessor register transfers
        CHECK (1110'0000'0001, 1111'0001'0001, UNIMPL) // mcr
        CHECK (1110'0001'0001, 1111'0001'0001, UNIMPL) // mrc
        // multiple load/store
        CHECK (1000'0000'0000, 1110'0000'0000, UNIMPL) // ldm/stm
        // branch
        CHECK (1010'0000'0000, 1110'0000'0000, UNIMPL) // b/bl
        CHECK9(1111'0000'0000, 1111'0000'0000, SoftwareInterrupt)
        CHECK9(1110'0000'0000, 1111'0000'0000, UndefinedInstruction) // coprocessor instruction - checkme: longer undef?
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

void THUMB9_Misc(struct ARM* ARM, const u16 instr_data)
{
    const u16 decode = (instr_data >> 3) & 0x1FF;

    CHECK(0000'0000'0, 1111'0000'0, _UNIMPL) // adjust sp
    CHECK(0100'0000'0, 1110'0000'0, _UNIMPL) // push
    CHECK(1100'0000'0, 1110'0000'0, _UNIMPL) // pop
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

int THUMB9_Misc_Interlocks(struct ARM946ES* ARM9, const u16 instr_data)
{
    const u16 decode = (instr_data >> 3) & 0x1FF;

    CHECK(0000'0000'0, 1111'0000'0, UNIMPL) // adjust sp
    CHECK(0100'0000'0, 1110'0000'0, UNIMPL) // push
    CHECK(1100'0000'0, 1110'0000'0, UNIMPL) // pop
    CHECK(0000'0000'0, 0000'0000'0, None)
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
        CHECK (0001'10, 1111'10, UNIMPL) // add/sub reg/imm
        CHECK (0000'00, 1110'00, UNIMPL) // shift imm
        CHECK (0010'00, 1110'00, UNIMPL) // add/sub/cmp/mov imm
        CHECK (0100'00, 1111'11, UNIMPL) // data proc reg
        CHECK (0100'00, 1111'11, UNIMPL) // data proc/b(l)x hi-regs
        CHECK (0100'10, 1111'10, UNIMPL) // ldr pc-rel
        CHECK (0101'00, 1111'00, UNIMPL) // ldr/str reg offs
        CHECK (0110'00, 1110'00, UNIMPL) // ldr/str(b) imm offs
        CHECK (1000'00, 1111'00, UNIMPL) // ldr/strh imm offs
        CHECK (1010'00, 1111'00, UNIMPL) // sp/pc-rel add?
        CHECK9(1011'00, 1111'00, Misc) // misc
        CHECK (1100'00, 1111'00, UNIMPL) // ldm/stm
        CHECK (1101'00, 1111'00, UNIMPL) // cond b/udf/swi
        CHECK (1110'00, 1111'10, UNIMPL) // b
        CHECK (1111'00, 1111'10, UNIMPL) // bl(x) prefix
        CHECK (1110'10, 1110'10, UNIMPL) // bl(x) suffix
        CHECK9(0000'00, 0000'00, UndefinedInstruction)
        unreachable();
    }
}

#undef CHECK
#undef CHECK9
