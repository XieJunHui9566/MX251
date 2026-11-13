#include "indexed-address.h"

static u8 __xdata cells[512];
static const u8 __code bytes[16]={3,19,41,67,89,113,137,163,181,199,211,223,229,239,241,251};
static const u16 __code words[16]={0,1,127,128,255,256,32767,32768,65535,0x5555,0xaaaa,0x1234,0x8001,0x7ffe,0xff00,0x00ff};

void indexed_init(void)
{
    u16 i;
    for(i=0;i<512;++i)cells[i]=(u8)((i*29U)^(i>>3)^0x96U);
}

u32 indexed_case(u8 seed)
{
    u32 result=0x13579bdfUL;
    u8 i,saved,value;
#define MIX(v) result=(result<<5)^result^(v)
    for(i=0;i<8;++i) {
        u16 offset=(u16)seed+i*8U;
        MIX(index_twice(cells,0x8000U+offset));
        MIX(index_four(cells,0xc000U+offset));
        MIX(index_code_byte(bytes,(seed+i)&7));
        MIX(index_code_word(words,(seed+i)&15));
        MIX(index_reused(cells,0x8000U+offset));
        saved=cells[offset*2U];value=seed+i;
        index_write(cells,0x8000U+offset,value);
        MIX(cells[offset*2U]);
        index_write(cells,0x8000U+offset,saved);
    }
    return result;
}
