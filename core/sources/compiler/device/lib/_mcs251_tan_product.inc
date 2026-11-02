/* Same GPL-2.0-or-later with SDCC library exception as _mcs251_tan_reduce.h.
 * 24-bit significand times the full 224-bit 2/pi constant; exact 31-byte product. */
    /* 基数从 256 改为 65536：24 位尾数分为低 16 位和高 8 位，
       共 28 次 16x16 乘法，仍生成原来的完整 31 字节积。
       显式拼拆小端数字，不依赖 MCS251 的内存字节序或非对齐访问。 */
    carry = 0;
    for (j = 0; j < 28; j += 2) {
        word = (uint16_t)mcs251_two_over_pi[j] |
               ((uint16_t)mcs251_two_over_pi[j+1] << 8);
        accumulation = (uint32_t)(uint16_t)significand * (uint32_t)word + carry;
        product[j] = (uint8_t)accumulation;
        product[j+1] = (uint8_t)(accumulation >> 8);
        carry = (uint16_t)(accumulation >> 16);
    }
    product[28] = (uint8_t)carry;
    product[29] = (uint8_t)(carry >> 8);
    carry = 0;
    for (j = 0; j < 28; j += 2) {
        word = (uint16_t)mcs251_two_over_pi[j] |
               ((uint16_t)mcs251_two_over_pi[j+1] << 8);
        accumulation = (uint32_t)(uint16_t)(significand >> 16) * (uint32_t)word;
        accumulation += ((uint16_t)product[j+2] |
                         ((uint16_t)product[j+3] << 8)) + (uint32_t)carry;
        product[j+2] = (uint8_t)accumulation;
        product[j+3] = (uint8_t)(accumulation >> 8);
        carry = (uint16_t)(accumulation >> 16);
    }
    /* 第二遍最大累加值不超过 2^24-1，最终进位必在单字节内。 */
    product[30] = (uint8_t)carry;
