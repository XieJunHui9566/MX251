#ifndef MX251_NATIVE_NARROW_DIVISION_H
#define MX251_NATIVE_NARROW_DIVISION_H
typedef unsigned char div_u8;
typedef unsigned int div_u16;
typedef unsigned long div_u32;
#ifndef DIV_ABI
#define DIV_ABI
#endif
div_u8 div_q8(div_u8, div_u8) DIV_ABI;
div_u8 div_r8(div_u8, div_u8) DIV_ABI;
div_u16 div_q16(div_u16, div_u16) DIV_ABI;
div_u16 div_r16(div_u16, div_u16) DIV_ABI;
div_u8 div_narrow_q(div_u16, div_u16) DIV_ABI;
div_u8 div_narrow_r(div_u16, div_u16) DIV_ABI;
div_u16 div_mixed(div_u16, div_u8) DIV_ABI;
div_u16 div_byte_word(div_u8, div_u16) DIV_ABI;
div_u32 div_decimal(div_u16) DIV_ABI;
div_u16 div_live(div_u16, div_u16, div_u16, div_u16,
                div_u16, div_u16, div_u16, div_u16) DIV_ABI;
div_u16 div_volatile(div_u16, div_u16) DIV_ABI;
#endif
