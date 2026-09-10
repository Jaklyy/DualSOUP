#include "arm.h"
#include "core/bus/bus.h"
#include "core/arm/shared/arm.h"
#include "core/scheduler.h"




void A7TDMI_DataRead(ARM7TDMI* a7tdmi, const timestamp now)
{
    A7TDMI_PostMem* pass = &a7tdmi->PostMem;
    u32 addr = pass->Addr;
    ARM_DataWidth size = pass->Size;

    // nds hardware seems to force align words (but not halfwords!?) when accessing the gba sram interface.
    // this still occurs in gba mode (on 3ds at least) and the same code run on actual gba hardware will result in unaligned accesses as expected.
    // so im suspecting, but unable to outright confirm, a cpu revision difference.
    // though it could be a difference anywhere from the arm7tdmi bus interface, the bus itself, the gba cart sram interface, etc.
    // but it is almost certainly a hardware revision somewhere along the chain.
    if (size == ARMDataWidth_32) addr &= ~3;

    BusReq req = {
        .Addr = addr,
        .WrVal = 0,
        .Write = false,
        .Lock = (pass->DataCB == A7TDMIDataCB_SwapLoad),
        .Man7 = MAN7_ARM7,
        .Prot = {
            .Data = true,
            .Privileged = pass->Priv
        }, // the other signals might just not exist on arm7 bus?
        .Size = (AHB_HSIZE)size,
        .Type = ((pass->NumFetchCompleted == 0) ? HTRANS_NONSEQ : HTRANS_SEQ),
        .CB = CB7_7TDMIData,
    };
    Bus_Req(a7tdmi->ARM.Sys, &req, now, false);
}

void A7TDMI_InstrRead(ARM7TDMI* a7tdmi, const timestamp now)
{
    u32 addr = a7tdmi->ARM.PC;
    ARM_DataWidth size = (a7tdmi->ARM.CPSR.Thumb ? ARMDataWidth_16 : ARMDataWidth_32);
    bool nseq = !a7tdmi->ARM.CodeSeq;

    // cheeky way to force align addr for 32 bit fetches
    // see note in above A7TDMI_DataRead function for why addr is force aligned.
    addr &= ~size;

    BusReq req = {
        .Addr = addr,
        .WrVal = 0,
        .Write = false,
        .Lock = false,
        .Man7 = MAN7_ARM7,
        .Prot = {
            .Data = false,
            .Privileged = a7tdmi->ARM.Privileged,
        }, // the other signals might just not exist on arm7 bus?
        .Size = (AHB_HSIZE)size,
        .Type = (nseq ? HTRANS_NONSEQ : HTRANS_SEQ),
        .CB = CB7_7TDMIInstr,
    };
    Bus_Req(a7tdmi->ARM.Sys, &req, now, false);
}

void A7TDMI_DataWrite(ARM7TDMI* a7tdmi, const timestamp now)
{
    A7TDMI_PostMem* pass = &a7tdmi->PostMem;
    u32 addr = pass->Addr;
    ARM_DataWidth size = pass->Size;

    // TODO: how is misalignment of address handled?
    //if (size == ARMDataWidth_32) addr &= ~3;

    BusReq req = {
        .Addr = addr,
        .WrVal = pass->WrData[pass->NumFetchCompleted],
        .Write = true,
        .Lock = false, // bus takes this as a signal its the last locked fetch //(pass->DataCB == A7TDMIDataCB_SwapStore),
        .Man7 = MAN7_ARM7,
        .Prot = {
            .Data = true,
            .Privileged = pass->Priv
        }, // the other signals might just not exist on arm7 bus?
        .Size = (AHB_HSIZE)size,
        .Type = ((pass->NumFetchCompleted == 0) ? HTRANS_NONSEQ : HTRANS_SEQ),
        .CB = CB7_7TDMIData,
    };
    Bus_Req(a7tdmi->ARM.Sys, &req, now, false);
}

void A7TDMI_DataPost(ARM7TDMI* a7tdmi, const timestamp now, u32 rdata)
{
    A7TDMI_PostMem* pass = &a7tdmi->PostMem;

    if ((pass->DataCB == A7TDMIDataCB_LoadSingle) || (pass->DataCB == A7TDMIDataCB_LoadMultiple) || (pass->DataCB == A7TDMIDataCB_SwapLoad))
        pass->RData[pass->NumFetchCompleted] = rdata;

    pass->Addr += 4;
    pass->NumFetchCompleted++;

    if (pass->NumFetchCompleted != pass->NumFetch)
    {
        switch(pass->DataCB)
        {
        case A7TDMIDataCB_LoadSingle:
        case A7TDMIDataCB_LoadMultiple:
        case A7TDMIDataCB_SwapLoad:
            A7TDMI_DataRead(a7tdmi, now); break;
        case A7TDMIDataCB_StoreSingle:
        case A7TDMIDataCB_StoreMultiple:
        case A7TDMIDataCB_SwapStore:
            A7TDMI_DataWrite(a7tdmi, now); break;
        }
    }
    else
    {
        switch(pass->DataCB)
        {
            case A7TDMIDataCB_LoadSingle: A7TDMI_LDR_Post(a7tdmi); break;
            case A7TDMIDataCB_LoadMultiple: A7TDMI_LDM_Post(a7tdmi); break;
            case A7TDMIDataCB_StoreSingle: A7TDMI_STR_Post(a7tdmi); break;
            case A7TDMIDataCB_StoreMultiple: A7TDMI_STM_Post(a7tdmi); break;
            case A7TDMIDataCB_SwapLoad: A7TDMI_SWPLoad_Post(a7tdmi); break;
            case A7TDMIDataCB_SwapStore: A7TDMI_SWPStore_Post(a7tdmi); break;
        }
    }

}

void A7TDMI_InstrReadPost(ARM7TDMI* a7tdmi, const timestamp now, u32 rdata)
{
    ARM* cpu = &a7tdmi->ARM;
    if (cpu->CPSR.Thumb && (cpu->PC & 2)) rdata = ROR32(rdata, 16);

    cpu->Instr[2] = (ARM_Instr){.Raw = rdata,
                                            .Aborted = false, // only used in theory
                                            .CoprocPriv = false}; // this is for an arm9 specific bug

    if (cpu->FlushProg > 0)
    {
        cpu->FlushProg--;
        ARM_PipelineStep(cpu);
        ARM_StepPC(cpu, cpu->CPSR.Thumb);
        A7TDMI_InstrRead(a7tdmi, now);
    }
    else
    {
        Sched_AddEvent(cpu->Sys, now, Evt_ARM7);
    }
}

void A7TDMI_RotateExtendUnit(u32* rdata, const u32 addr, const ARM_DataWidth size, const bool signext)
{
    switch (size)
    {
    case ARMDataWidth_8:
    {
        *rdata = ROR32(*rdata, ((addr & 0x3) * 8));

        if (signext) *rdata = (s32)(s8)*rdata;
        else *rdata &= 0xFF; // zero extend
        break;
    }
    case ARMDataWidth_16:
    {
        // misaligned halfword reads are weird on ARM7TDMI
        // it selects unused byte lanes to zero/sign fill using bit 1 of the address (properly halfword aligned)
        // but then does the ASR/ROR to put them into place using bits 1 & 0 (not aligned!!!)
        // this results in misaligned ldrsh giving behavior similar to ldrsb and misaligned ldrh putting a byte into the high portion of the register
        // presumably the halfword select logic is unique, but the ASR/ROR logic is reused from byte/word fetch logic
        if (signext)
        {
            // put sign bit in high lanes and arithmetic right shift them into the proper spot
            if (!(addr & 0x2)) *rdata = (s32)(s16)*rdata;
            *rdata = ((s32)*rdata) >> ((addr&0x3)*8);
        }
        else
        {
            // isolate byte lanes and then rotate them into place
            *rdata &= (0xFFFF << ((addr&0x2)*8));
            *rdata = ROR32(*rdata, ((addr&0x3)*8));
        }
        break;
    }
    case ARMDataWidth_32:  *rdata = ROR32(*rdata, ((addr & 0x3) * 8)); break;
    default: unreachable();
    }
}
