/* 覆盖普通入口、异步入口和 callee-saves；活跃性不包含外部上下文。 */
typedef unsigned int u16;
typedef unsigned long u32;
volatile __xdata u16 mul_a, mul_b;
volatile __xdata u32 mul_result;

u32 ordinary_mul(u16 a, u16 b) { return (u32)a * b; }

#pragma callee_saves saved_mul
u32 saved_mul(u16 a, u16 b) { return (u32)a * b; }

void irq_mul(void) __interrupt(1) {
    mul_result = (u32)mul_a * mul_b;
}

void irq_bank_mul(void) __interrupt(3) __using(1) {
    mul_result = (u32)mul_a * mul_b;
}

/* 保持第一个乘积存活，让成本选择确实覆盖新计划。 */
u32 ordinary_chain(u32 a, u32 b) __reentrant {
    u16 al = (u16)a, bl = (u16)b, ah = (u16)(a >> 16), bh = (u16)(b >> 16);
    u32 low = (u32)al * bl;
    u32 cross = (u32)ah * bl + (u32)al * bh;
    return low + (cross << 16);
}

/* 编译检查专用非易失输入；不声明它们适合与主线程无同步共享。
 * 原 irq_mul 继续覆盖 volatile 回退；此函数覆盖原生计划的 ISR 保存。 */
extern __xdata u16 mul_plan_a, mul_plan_b;
void irq_plan_mul(void) __interrupt(5) {
    mul_result = ((u32)mul_plan_a * mul_plan_b) ^ 0x12345678UL;
}
