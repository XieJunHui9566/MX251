/* SPDX-License-Identifier: GPL-2.0-or-later WITH SDCC-exception-2.0
 * 共用原定点约简步骤；宏仅选择输入/结果位容器，不能改变算术或舍入。 */
    point = 374 - (int)(MCS251_TAN_BITS >> 23);
    significand = (MCS251_TAN_BITS & 0x7fffffUL) | 0x800000UL;
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
    mcs251_multiply_two_over_pi(significand, product);
#else
#include "_mcs251_tan_product.inc"
#endif
    round_up = mcs251_product_bit(product, point-1);
    parity = mcs251_product_bit(product, point) ^ round_up;
    if (round_up) {
        carry = 1;
        for (i = 0; i < 31; ++i) {
            digit = (uint16_t)(product[i] ^ 0xff) + carry;
            product[i] = (uint8_t)digit;
            carry = digit >> 8;
        }
    }
    top = point-1;
    while (top >= 0 && !mcs251_product_bit(product, top))
        --top;
    /* 连续 24 位最多跨四个字节；高于 top 的商位必须掩掉。
       低于数组起点的位按原逐位算法补零，不改变舍入输入。 */
    significand = 0;
    if (top >= 0) {
        j = (uint8_t)((unsigned int)top >> 3);
        significand = product[j];
        for (i = 0; i < 3; ++i) {
            significand <<= 8;
            if (j)
                significand |= product[--j];
        }
        significand = (significand >> (1 + (top & 7))) & 0xffffffUL;
    }
    /* sticky 只表示是否存在任意非零丢弃位：先查完整字节，再查
       最后不足八位的部分；发现非零即可停止，不能把 guard 算进去。 */
    sticky = 0;
    bit = top-24;
    if (bit > 0) {
        j = (uint8_t)((unsigned int)bit >> 3);
        for (i = 0; i < j; ++i) {
            if (product[i]) {
                sticky = 1;
                break;
            }
        }
        if (!sticky && (bit & 7))
            sticky = (product[j] & ((1U << (bit & 7))-1U)) != 0;
    }
    if (mcs251_product_bit(product, top-24) && (sticky || (significand & 1)))
        ++significand;
    if (significand == 0x1000000UL) {
        significand >>= 1;
        ++top;
    }
    exponent = 127 + top - point;
    if (top < 0 || exponent < -23)
        MCS251_TAN_BITS = 0;
    else if (exponent <= 0)
        MCS251_TAN_BITS = significand >> (1-exponent);
    else
        MCS251_TAN_BITS = ((uint32_t)exponent << 23) | (significand & 0x7fffffUL);
    if (negative ^ round_up)
        MCS251_TAN_BITS |= 0x80000000UL;
