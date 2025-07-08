#include "utils.h"




struct Bus9
{
    u64 TSInstruction; // internal instruction bus
    u64 TSData; // internal data bus
    u64 TSExternal; // main bus of the arm9

    // queue instruction
    // queue data
    // queue external
};

// MainRAM is a type of FCRAM.
// gbatek lists the following chips as being used in retail DS models:
// Fujitsu 82DBS02163C-70L
// ST Microelectronics M69AB048BL70ZA8
// MainRAM only natively supports 16 bit accesses.
// not 100% clear what mechanic the nds uses to support 8 bit writes?
// 8bit reads presumably use some special logic or rely on the fact that our specific ARM processors
// seem to always perform a rotate right and bit masking operation on byte reads
struct BusMainRAM
{
    u64 Timestamp; // 
    // Internal control reg for the FCRAM chip on the NDS.
    // NTR/USG ARM9 BIOS has init code for mainRAM @ offset 0x180.
    // Should be initialized using halfword r/w to the most significant halfword of mainRAM.
    // CR set sequence for Fujitsu MB82DBS02163C-70L:
    // Read data -> write the data back twice -> 2 more writes of any value -> read from any mainram address (address is written to the control reg).
    // BIOS init code goes out of its way to use specific values for the last two writes for some reason. Might be used for other chips?
    // MainRAM is addressed in halfwords so the LSB of the address is ignored(?).
    // Not sure how this works with byte accesses (most likely depends on what jank they're doing to fake byte support).
    // Word accesses shouldn't dont work though.
    // TODO: implement main ram init process?
    union
    {
        struct
        {
            // "must be one"
            u32 : 7;
            // 0 = single clock pulse control, no write suspend;
            // 1 = level control, write suspend;
            bool WriteControl : 1;
            // "must be one"
            u32 : 1;
            // 0 = falling edge;
            // 1 = rising edge;
            bool ValidClockEdge : 1;
            // disables burst write support when set
            bool SingleWrite : 1;
            // 0 = reserved;
            // 1 = sequential;
            bool BurstSequence : 1;
            // 1-3 = 4-6 cycles (read) & 3-5 cycles (write);
            // other values are reserved;
            u32 Latency : 3;
            // 0 = synchronous;
            // 1 = asynchronous;
            bool Mode : 1;
            // 2 = 8 word;
            // 3 = 16 word;
            // 7 = continuous; (seems to have a 241 cycle limit?)
            // other values are reserved;
            u32 BurstLength : 3;
            // power saving feature?
            // 0 = 8 M-bit partial;
            // 1 = 4 M-bit partial;
            // 2 = reserved;
            // 3 = Sleep;
            u32 PartialSize : 2;
        };
    } ControlReg;
};

void Bus_MainRAM();
void Bus9();
