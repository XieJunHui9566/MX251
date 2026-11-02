/* SPDX-License-Identifier: GPL-2.0-or-later WITH SDCC-exception-2.0
 * 共用原定点约简步骤；宏仅选择输入/结果位容器，不能改变算术或舍入。 */
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
    /* 输入符号已清零；拆成目标原生16位操作，精确提取8位指数。 */
    point = 374 - (int)(uint8_t)(((uint16_t)(MCS251_TAN_BITS >> 16) * 2U) >> 8);
#else
    point = 374 - (int)(MCS251_TAN_BITS >> 23);
#endif
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
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
    /* 舍入至最近整数后，半周期位必为零：常数为奇数，非零24位
       significand最多含23个低零位，而point-1>=118，不可能恰好半周期。
       round_up=1时补码的进位也不可能到达该位，可跳过这一次已知零读位。 */
    top = point-2;
#else
    top = point-1;
#endif
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
    /* P的最低非零位至多为23；低point位在取补码前后都非零。
       从已知半周期位以下搜索，必在数组内找到1，top不会降至负数。 */
    while (!mcs251_product_bit(product, top))
        --top;
#else
    while (top >= 0 && !mcs251_product_bit(product, top))
        --top;
#endif
    /* 连续 24 位最多跨四个字节；高于 top 的商位必须掩掉。
       低于数组起点的位按原逐位算法补零，不改变舍入输入。 */
    significand = 0;
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
    digit = 0;
#endif
#if !defined(__SDCC_MCS251_NATIVE_RUNTIME)
    if (top >= 0)
#endif
    {
        j = (uint8_t)((unsigned int)top >> 3);
        significand = product[j];
        for (i = 0; i < 3; ++i) {
            significand <<= 8;
            if (j)
                significand |= product[--j];
        }
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
        /* 四字节窗口已经包含 guard 位。先对齐到第0位，再提取24位尾数；
           数组下界外的窗口位由上方循环补零，不能额外读取 product。 */
        significand >>= (top & 7);
        digit = (uint8_t)significand & 1;
        significand = (significand >> 1) & 0xffffffUL;
#else
        significand = (significand >> (1 + (top & 7))) & 0xffffffUL;
#endif
    }
    /* sticky 只表示是否存在任意非零丢弃位：先查完整字节，再查
       最后不足八位的部分；发现非零即可停止，不能把 guard 算进去。 */
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
    /* 固定224位奇常数与全部24位尾数的精确整数域证明：
       最近余数最高位至少94，最低非零位至多23；guard以下必有1。
       sticky恒为1，保留guard判断即为原最近偶数舍入。 */
    if (digit)
        ++significand;
#else
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
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
    if (digit && (sticky || (significand & 1)))
#else
    if (mcs251_product_bit(product, top-24) && (sticky || (significand & 1)))
#endif
        ++significand;
#endif
    if (significand == 0x1000000UL) {
        significand >>= 1;
        ++top;
    }
    exponent = 127 + top - point;
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
    /* 同一完整整数域证明给出编码指数至少97；舍入只可能增加它。
       私有调用方只传入有限且绝对值大于0.5的binary32。 */
    MCS251_TAN_BITS = ((uint32_t)((uint16_t)exponent << 7) << 16) | (significand & 0x7fffffUL);
#else
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
    if (exponent < -23)
#else
    if (top < 0 || exponent < -23)
#endif
        MCS251_TAN_BITS = 0;
    else if (exponent <= 0)
        MCS251_TAN_BITS = significand >> (1-exponent);
    else
        MCS251_TAN_BITS = ((uint32_t)exponent << 23) | (significand & 0x7fffffUL);
#endif
    if (negative ^ round_up)
        MCS251_TAN_BITS |= 0x80000000UL;
