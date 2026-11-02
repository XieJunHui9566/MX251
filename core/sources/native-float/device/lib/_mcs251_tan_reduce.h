/* MCS251 binary32 argument reduction.
 * Distributed under the same GPL-2.0-or-later with SDCC library exception
 * as the including tancotf.c. No binary64 arithmetic is required.
 */
#include <stdint.h>

typedef union { float f; uint32_t u; } mcs251_tan_bits;

/* floor((2/pi) * 2^224), least significant byte first.
 * 224 位定点常数避免大参数消减和 pi/2 附近的相减丢失精度。
 * 对最大 binary32 参数，常数截断给商带来的绝对误差小于 2^-96。
 */
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
/* 以数值声明相邻两位基数256的常量，不转换指针或依赖宿主字节序。
 * 每个word精确等于旧表低字节 | (高字节 << 8)，保留全部224位。 */
static const __code uint16_t mcs251_two_over_pi[14] = {
    0x63ab,0xfe51,0x9041,0x3c43,0x9599,0xdb62,0xddc0,0xf534,0x57d1,0xfc27,0x1529,0x4e44,0x836e,0xa2f9
};
#else
static const __code uint8_t mcs251_two_over_pi[28] = {
    0xab,0x63,0x51,0xfe,0x41,0x90,0x43,0x3c,0x99,0x95,0x62,0xdb,
    0xc0,0xdd,0x34,0xf5,0xd1,0x57,0x27,0xfc,0x29,0x15,0x44,0x4e,
    0x6e,0x83,0xf9,0xa2
};
#endif

/* 纯读位、定点乘法和整数约简使用原生 ABI4；浮点包装层保留原后端。
   原生 ABI4 现支持平坦 generic 指针，不强制把栈对象改成 xdata。 */
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
static uint8_t mcs251_product_bit(const uint8_t *p, int bit) __reentrant __sdcccall(4)
#else
static uint8_t mcs251_product_bit(const uint8_t *p, int bit) _FLOAT_FUNC_REENTRANT
#endif
{
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
    /* 负int转换为同宽无符号后必定大于247；一次比较保持完整边界语义。 */
    if ((unsigned int)bit >= 248U)
#else
    if (bit < 0 || bit >= 248)
#endif
        return 0;
    return (p[(unsigned int)bit >> 3] >> (bit & 7)) & 1;
}

#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
/* 将有完整整数参考的定点乘法单独迁移；不缩短常数或丢弃积的低位。
 * 非原生 profile 在原位置展开同一片段，保持此前的调用及存储约定。 */
static void mcs251_multiply_two_over_pi(uint32_t significand, uint8_t *product) __reentrant __sdcccall(4)
{
    uint8_t j;
    uint16_t carry, word;
    const __code uint16_t *factor;
    uint8_t *digits;
    uint32_t accumulation;
#include "_mcs251_tan_product.inc"
}
#endif

#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
/* 浮点位型边界留在包装层；整数核心可使用 ABI4，不解除编译器迁移保护。
 * 调用方已排除 |x|<=0.5 和非有限输入，完整 31 字节积与舍入规则保留。 */
static uint8_t mcs251_reduce_fraction(uint32_t bits, uint32_t *fraction) __reentrant __sdcccall(4)
{
    uint8_t product[31];
    uint8_t i, j, round_up, parity, negative, sticky;
    uint16_t carry, digit;
    int point, top, bit, exponent;
    uint32_t significand;

    negative = (uint8_t)(bits >> 31);
    bits &= 0x7fffffffUL;
#define MCS251_TAN_BITS bits
#include "_mcs251_tan_fraction.inc"
#undef MCS251_TAN_BITS
    *fraction = bits;
    return parity;
}
#endif

/* Return N modulo 2 and r in x = N*pi/2 + r, |r| <= pi/4.
 * 只保留商的奇偶性，不把巨大商转换成 16 位 int。
 */
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
static inline uint8_t mcs251_reduce_pio2(float x, float *reduced) _FLOAT_FUNC_REENTRANT
#else
static uint8_t mcs251_reduce_pio2(float x, float *reduced) _FLOAT_FUNC_REENTRANT
#endif
{
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
    mcs251_tan_bits value;
    uint8_t parity;
    value.f = x;
    if ((value.u & 0x7fffffffUL) <= 0x3f000000UL) {
        *reduced = x;
        return 0;
    }
    parity = mcs251_reduce_fraction(value.u, &value.u);
    *reduced = value.f * 1.57079632679489661923f;
    return parity;
#else
    mcs251_tan_bits value;
    uint8_t product[31];
    uint8_t i, j, round_up, parity, negative, sticky;
    uint16_t carry, digit, word;
    int point, top, bit, exponent;
    uint32_t significand, accumulation;

    value.f = x;
    negative = (uint8_t)(value.u >> 31);
    value.u &= 0x7fffffffUL;
    if (value.u <= 0x3f000000UL) {
        *reduced = x;
        return 0;
    }
#define MCS251_TAN_BITS value.u
#include "_mcs251_tan_fraction.inc"
#undef MCS251_TAN_BITS
    *reduced = value.f * 1.57079632679489661923f;
    return parity;
#endif
}
