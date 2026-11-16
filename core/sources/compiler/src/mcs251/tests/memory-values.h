#ifndef MEMORY_VALUES_H
#define MEMORY_VALUES_H
typedef unsigned char mv_u8;
typedef unsigned int mv_u16;
typedef unsigned long mv_u32;
#ifndef TEST_ABI
#define TEST_ABI 2
#endif
#define MV_ABI __sdcccall(TEST_ABI)
extern __edata mv_u16 mv_a;
void mv_change(mv_u16 value) MV_ABI;
mv_u32 mv_constant(void) MV_ABI;
mv_u16 mv_forward(mv_u16 seed) MV_ABI;
mv_u16 mv_alias(mv_u16 seed, volatile __xdata mv_u16 *pointer) MV_ABI;
mv_u16 mv_partial(mv_u16 seed) MV_ABI;
mv_u16 mv_call(mv_u16 seed) MV_ABI;
mv_u16 mv_callback(mv_u16 seed, void (*change)(mv_u16) MV_ABI) MV_ABI;
mv_u16 mv_volatile(mv_u16 seed) MV_ABI;
mv_u32 mv_signed(signed char seed) MV_ABI;
mv_u16 mv_branch(mv_u16 seed) MV_ABI;
mv_u16 mv_redefine(mv_u16 seed) MV_ABI;
mv_u16 mv_overlap(mv_u16 seed) MV_ABI;
#endif
