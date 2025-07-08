#include "utils.h"
#include "arm/arm9/arm.h"
#include "arm/arm7/arm.h"




#define MAINRAM_SIZE        (4   * 1024 * 1024)
#define SHARED_WRAM_SIZE    (32  * 1024)
#define ARM7_WRAM_SIZE      (64  * 1024)
#define ARM9_BIOS           (4   * 1024)
#define ARM7_BIOS           (16  * 1024)
#define VRAM_A_SIZE         (128 * 1024)
#define VRAM_B_SIZE         (128 * 1024)
#define VRAM_C_SIZE         (128 * 1024)
#define VRAM_D_SIZE         (128 * 1024)
#define VRAM_E_SIZE         (64  * 1024)
#define VRAM_F_SIZE         (16  * 1024)
#define VRAM_G_SIZE         (16  * 1024)
#define VRAM_H_SIZE         (32  * 1024)
#define VRAM_I_SIZE         (16  * 1024)



struct NDS
{
    struct ARM946E_S ARM9;
    struct ARM7TDMI ARM7;

    struct
    {
        
    } IO;

    alignas(64)
    u8 MainRAM[MAINRAM_SIZE];
    u8 SharedWRAM[SHARED_WRAM_SIZE];
    u8 ARM7WRAM[ARM7_WRAM_SIZE];
    u8 VRAMA[VRAM_A_SIZE];
    u8 VRAMB[VRAM_B_SIZE];
    u8 VRAMC[VRAM_C_SIZE];
    u8 VRAMD[VRAM_D_SIZE];
    u8 VRAME[VRAM_E_SIZE];
    u8 VRAMF[VRAM_F_SIZE];
    u8 VRAMG[VRAM_G_SIZE];
    u8 VRAMH[VRAM_H_SIZE];
    u8 VRAMI[VRAM_I_SIZE];
    u8 Bios9[ARM9_BIOS];
    u8 Bios7[ARM7_BIOS];
};