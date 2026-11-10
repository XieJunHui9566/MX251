#ifndef MX251_NATIVE_DIVMOD_PAIRS_H
#define MX251_NATIVE_DIVMOD_PAIRS_H
#include <stdint.h>
#ifndef PAIR_ABI
#define PAIR_ABI
#endif
uint16_t pair_qr(uint16_t,uint16_t) PAIR_ABI;
uint16_t pair_rq(uint16_t,uint16_t) PAIR_ABI;
uint16_t pair_byte(uint8_t,uint8_t) PAIR_ABI;
uint16_t pair_byte_wide(uint8_t,uint8_t) PAIR_ABI;
uint16_t pair_truncated(uint16_t,uint16_t) PAIR_ABI;
uint16_t pair_changed(uint16_t,uint16_t) PAIR_ABI;
uint16_t pair_branch(uint16_t,uint16_t) PAIR_ABI;
uint16_t pair_volatile(uint16_t,uint16_t) PAIR_ABI;
uint16_t pair_call(uint16_t,uint16_t) PAIR_ABI;
uint16_t pair_touch(uint16_t) PAIR_ABI;
uint16_t pair_loop(uint16_t,uint16_t) PAIR_ABI;
#endif
