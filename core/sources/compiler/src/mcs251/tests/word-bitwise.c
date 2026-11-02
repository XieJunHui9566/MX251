/* 字逻辑回归：由独立翻译单元传入运行期输入，覆盖交叉字、活跃输入、条件和窄位宽。 */
typedef unsigned long u32;
typedef unsigned int u16;
u32 logic0(u32 a,u32 b) { return a^b; }
u32 logic1(u32 a,u32 b) { return a&b; }
u32 logic2(u32 a,u32 b) { return a|b; }
u32 logic3(u32 a,u32 b) { return (a+b)^b; }
u32 logic4(u32 a,u32 b) { return (a-b)|b; }
u32 logic5(u32 a,u32 b) { return (a&b)^((a|b)+a); }
u32 logic6(u32 a,u32 b) { return ((a<<16)|(a>>16))^b; }
u32 logic7(u32 a,u32 b) { return (a^b)+a; }
u32 logic8(u32 a,u32 b) { return (a&b)?a:b; }
u32 logic9(u32 a,u32 b) { return (u16)((u16)a^(u16)b); }
u32 logic10(u32 a,u32 b) { return (u16)((u16)a&(u16)b); }
u32 logic11(u32 a,u32 b) { return (u16)((u16)a|(u16)b); }
u32 logic12(u32 a,u32 b) { return (u32)(u16)a^b; }
u32 logic13(u32 a,u32 b) { return (long)((int)a^(int)b); }
u32 logic14(u32 a,u32 b) { return (u32)((unsigned _BitInt(31))a^(unsigned _BitInt(31))b); }
u32 logic15(u32 a,u32 b) { return (a&0xffff0000UL)^b; }
