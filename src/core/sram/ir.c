#include <stdckdint.h>
#include "ir.h"
#include "flash.h"




u8 IRhle_CMDSend(IRhle* ir, const u8 val, const bool chipsel)
{
    if (!ir->PrevChipSelect)
    {
        ir->CurCmd = val;
        ir->CmdLen = 0;
    }

    u8 ret;
    switch(ir->CurCmd)
    {
        case 0x00: ret = (ir->CmdLen > 0) ? Flash_CMDSend(&ir->Flash, val, chipsel) : 0; break;
        case 0x08: ret = (ir->CmdLen == 1) ? 0xAA : 0; break;
        default: ret = 0xFF; break;
    }
    // this is probably overkill but eh
    u16 cmdtmp;
    if (!ckd_add(&cmdtmp, ir->CmdLen, 1))
    {
        ir->CmdLen = cmdtmp;
    }
    ir->PrevChipSelect = chipsel;

    return ret;
}
