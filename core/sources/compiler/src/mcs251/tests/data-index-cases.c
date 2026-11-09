#include "data-index.h"
static volatile u16 __data sampled[4];

u32 data_index_case(u8 seed) {
    u8 index,byte_value=seed;
    u16 i;
    signed char delta;
    u16 value,word_value=0x1234+seed,sample_value=seed;
    u16 __data *p;
    u32 result=0x13579bdfUL;
    u32 long_value=0x10203040UL+seed;
    for(i=0;i<8;++i) {
        indexed_bytes[i]=byte_value;byte_value+=13;
        indexed_words[i]=word_value;word_value+=257;
    }
    for(i=0;i<4;++i) {
        indexed_longs[i]=long_value;long_value+=0x01010101UL;
        sampled[i]=sample_value;sample_value+=19;
    }
    for(i=0;i<8;++i) {
        index=(seed+i)&7;
        value=indexed_words[index]^indexed_bytes[index];
        result=(result<<5)^result^value;
    }
    for(i=0;i<4;++i) {
        index=(seed+i)&3;
        result=(result<<5)^result^indexed_longs[index];
        sampled[index]=sampled[index]+seed+i;
        value=sampled[index];
        result=(result<<5)^result^value;
    }
    /* The saved address must survive the index update and a mutating call. */
    index=seed&3;
    p=&indexed_words[index];
    ++index;
    value=data_index_touch(index,0x5a00+seed);
    result=(result<<5)^result^value;
    result=(result<<5)^result^*p;
    /* Constant and dynamic definitions meet at a branch, in both orders. */
    p=indexed_words+2;
    if(seed&1)p=indexed_words+(seed&3);
    value=*p;
    result=(result<<5)^result^value;
    p=indexed_words+(seed&3);
    if(seed&2)p=indexed_words+5;
    value=*p;
    result=(result<<5)^result^value;
    /* Negative offsets, subtraction, pointer updates and generic conversion. */
    delta=(seed&7)-4;
    p=indexed_words+4+delta;
    value=data_index_read(p);
    result=(result<<5)^result^value;
    p=indexed_words+7-(seed&3);
    value=*p;
    result=(result<<5)^result^value;
    p=indexed_words+1;
    for(i=0;i<3;++i) {
        *p+=seed+i;
        value=*p++;
        result=(result<<5)^result^value;
    }
    return result;
}
