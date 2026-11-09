#include "variadic-call.h"
#include <stdarg.h>
volatile u16 __data variadic_sample;

u32 variadic_mixed(u8 tag,...) {
    va_list ap;
    u16 small,word;
    u32 wide,result;
    const u8 *code,*local;
    union { float value; u32 bits; } floating;
    va_start(ap,tag);
    small=(u16)va_arg(ap,int);
    word=va_arg(ap,u16);
    wide=va_arg(ap,u32);
    code=va_arg(ap,const u8 *);
    local=va_arg(ap,const u8 *);
    floating.value=va_arg(ap,float);
    va_end(ap);
    result=0x12345678UL+tag;
    result=(result<<5)^result^small;
    result=(result<<5)^result^word;
    result=(result<<5)^result^wide;
    result=(result<<5)^result^code[tag&7];
    result=(result<<5)^result^local[2];
    return (result<<5)^result^floating.bits;
}

u32 variadic_samples(u8 tag,...) {
    va_list ap;
    u16 a,b;
    va_start(ap,tag);a=va_arg(ap,u16);b=va_arg(ap,u16);va_end(ap);
    variadic_sample=0x1111;
    return ((u32)a<<16)^b^tag;
}

u16 variadic_many(u8 count,...) {
    va_list ap;
    u16 result=0;
    va_start(ap,count);
    while(count--)result+=va_arg(ap,u16);
    va_end(ap);return result;
}

u16 variadic_mutate(u16 *p,...) {
    va_list ap;
    va_start(ap,p);*p+=va_arg(ap,u16);va_end(ap);return *p;
}

u16 variadic_empty(u16 value,...) { return value^0xa55a; }

u16 variadic_packed(u8 tag,...) {
    va_list ap;
    u8 byte;
    u16 word;
    va_start(ap,tag);byte=va_arg(ap,u8);word=va_arg(ap,u16);va_end(ap);
    return word^((u16)byte<<8)^tag;
}

float variadic_float(u8 tag,...) {
    va_list ap;
    float value;
    va_start(ap,tag);value=va_arg(ap,float);va_end(ap);return value;
}

const u8 *variadic_pointer(u8 tag,...) {
    va_list ap;
    const u8 *value;
    va_start(ap,tag);value=va_arg(ap,const u8 *);va_end(ap);return value;
}
