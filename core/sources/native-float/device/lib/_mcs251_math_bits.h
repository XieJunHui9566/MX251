/* Native binary32 transport; same license as the SDCC math library.
 * 整数只承载位型；每次算术仍调用原运行库，保持逐次舍入和特殊值政策。
 * 公开 float ABI 不变，联合体只在公开入口边界使用，不跨内部调用反复物化。
 */
#ifndef MCS251_MATH_BITS_H
#define MCS251_MATH_BITS_H
#include <stdint.h>
typedef uint32_t mcs251_math_word;
typedef union { float f; uint32_t u; } mcs251_math_value;
#define MATH_ABI __reentrant __sdcccall(4)
/* 这些符号已有同宽整数位型实现；BuildNativeFloat 的既有映射保持不变。 */
uint32_t mcs251_math_add_bits(uint32_t, uint32_t) MATH_ABI;
uint32_t mcs251_math_sub_bits(uint32_t, uint32_t) MATH_ABI;
uint32_t mcs251_math_mul_bits(uint32_t, uint32_t) MATH_ABI;
uint32_t mcs251_math_div_bits(uint32_t, uint32_t) MATH_ABI;
/* 同一位型主体供原生入口和独立旧ABI2兼容包装调用；不改变公开math.h。 */
uint32_t mcs251_sincos_bits(uint32_t, unsigned char) MATH_ABI;
uint32_t mcs251_tancot_bits(uint32_t, unsigned char) MATH_ABI;
unsigned char bits_lt(uint32_t, uint32_t) MATH_ABI;
uint32_t bits_f2s(uint32_t) MATH_ABI;
uint32_t bits_s2f(uint32_t) MATH_ABI;
#define M_ADD(a,b) mcs251_math_add_bits((a),(b))
#define M_SUB(a,b) mcs251_math_sub_bits((a),(b))
#define M_MUL(a,b) mcs251_math_mul_bits((a),(b))
#define M_DIV(a,b) mcs251_math_div_bits((a),(b))
#define M_NEG(a) ((a)^0x80000000UL)
#define M_ABS(a) ((a)&0x7fffffffUL)
/* 目标 int 为16位；保留 _fs2sint 先转signed32、再饱和的现有策略。
 * NaN/越界转换也是既有库政策，不在此声称标准 C 对这些输入有保证。 */
static inline int16_t mcs251_math_f2i16(uint32_t x) MATH_ABI
{
    int32_t value=(int32_t)bits_f2s(x);
    if(value>=32767L) return 32767;
    if(value<=(-32767L-1L)) return (-32767-1);
    return (int16_t)value;
}
static inline uint32_t mcs251_math_i16f(int16_t x) MATH_ABI
{
    /* 以无符号字明确复制符号位，等价于int16先符号扩展为int32。 */
    uint32_t value=(uint16_t)x;
    if(value&0x8000UL) value|=0xffff0000UL;
    return bits_s2f(value);
}

/* 原十进制常量的 binary32 位型，另由同编译器常量对象核对。 */
#define MB_ONE 0x3f800000UL
#define MB_HALF 0x3f000000UL
#define MB_HALF_PI 0x3fc90fdbUL
#define MB_REDUCE_PI2 0x3fc90fdbUL
#define MB_IPI 0x3ea2f983UL
#define MB_YMAX 0x46490c00UL
#define MB_EPS 0x39800000UL
#define MB_EPS2 0x337ffff3UL
#define MB_SC_C1 0x40490000UL
#define MB_SC_C2 0x3a7daa22UL
#define MB_SC_R1 0xbe2aaaa4UL
#define MB_SC_R2 0x3c08873eUL
#define MB_SC_R3 0xb94fb222UL
#define MB_SC_R4 0x362e9c5bUL
#define MB_AT_K1 0x3e8930a3UL
#define MB_AT_K2 0x3f3b67afUL
#define MB_AT_K3 0x3fddb3d7UL
#define MB_AT_P0 0xbef110f6UL
#define MB_AT_P1 0xbd508691UL
#define MB_AT_Q0 0x3fb4ccd3UL
#define MB_AT_Q1 0x3f800000UL
#define MB_AT_A0 0x00000000UL
#define MB_AT_A1 0x3f060a92UL
#define MB_AT_A2 0x3fc90fdbUL
#define MB_AT_A3 0x3f860a92UL
#define MB_TC_P1 0xbdc433b8UL
#define MB_TC_Q0 0x3f800000UL
#define MB_TC_Q1 0xbedbb7afUL
#define MB_TC_Q2 0x3c1f3375UL
#endif
