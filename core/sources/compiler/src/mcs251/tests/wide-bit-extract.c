typedef unsigned char u8;typedef unsigned int u16;typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
u8 pick8_0(u8 x) ABI {return (__bit)(x&0x1UL);}
u8 pick8_1(u8 x) ABI {return (__bit)(x&0x2UL);}
u8 pick8_2(u8 x) ABI {return (__bit)(x&0x4UL);}
u8 pick8_3(u8 x) ABI {return (__bit)(x&0x8UL);}
u8 pick8_4(u8 x) ABI {return (__bit)(x&0x10UL);}
u8 pick8_5(u8 x) ABI {return (__bit)(x&0x20UL);}
u8 pick8_6(u8 x) ABI {return (__bit)(x&0x40UL);}
u8 pick8_7(u8 x) ABI {return (__bit)(x&0x80UL);}
u8 pick16_0(u16 x) ABI {return (__bit)(x&0x1UL);}
u8 pick16_1(u16 x) ABI {return (__bit)(x&0x2UL);}
u8 pick16_2(u16 x) ABI {return (__bit)(x&0x4UL);}
u8 pick16_3(u16 x) ABI {return (__bit)(x&0x8UL);}
u8 pick16_4(u16 x) ABI {return (__bit)(x&0x10UL);}
u8 pick16_5(u16 x) ABI {return (__bit)(x&0x20UL);}
u8 pick16_6(u16 x) ABI {return (__bit)(x&0x40UL);}
u8 pick16_7(u16 x) ABI {return (__bit)(x&0x80UL);}
u8 pick16_8(u16 x) ABI {return (__bit)(x&0x100UL);}
u8 pick16_9(u16 x) ABI {return (__bit)(x&0x200UL);}
u8 pick16_10(u16 x) ABI {return (__bit)(x&0x400UL);}
u8 pick16_11(u16 x) ABI {return (__bit)(x&0x800UL);}
u8 pick16_12(u16 x) ABI {return (__bit)(x&0x1000UL);}
u8 pick16_13(u16 x) ABI {return (__bit)(x&0x2000UL);}
u8 pick16_14(u16 x) ABI {return (__bit)(x&0x4000UL);}
u8 pick16_15(u16 x) ABI {return (__bit)(x&0x8000UL);}
u8 pick32_0(u32 x) ABI {return (__bit)(x&0x1UL);}
u8 pick32_1(u32 x) ABI {return (__bit)(x&0x2UL);}
u8 pick32_2(u32 x) ABI {return (__bit)(x&0x4UL);}
u8 pick32_3(u32 x) ABI {return (__bit)(x&0x8UL);}
u8 pick32_4(u32 x) ABI {return (__bit)(x&0x10UL);}
u8 pick32_5(u32 x) ABI {return (__bit)(x&0x20UL);}
u8 pick32_6(u32 x) ABI {return (__bit)(x&0x40UL);}
u8 pick32_7(u32 x) ABI {return (__bit)(x&0x80UL);}
u8 pick32_8(u32 x) ABI {return (__bit)(x&0x100UL);}
u8 pick32_9(u32 x) ABI {return (__bit)(x&0x200UL);}
u8 pick32_10(u32 x) ABI {return (__bit)(x&0x400UL);}
u8 pick32_11(u32 x) ABI {return (__bit)(x&0x800UL);}
u8 pick32_12(u32 x) ABI {return (__bit)(x&0x1000UL);}
u8 pick32_13(u32 x) ABI {return (__bit)(x&0x2000UL);}
u8 pick32_14(u32 x) ABI {return (__bit)(x&0x4000UL);}
u8 pick32_15(u32 x) ABI {return (__bit)(x&0x8000UL);}
u8 pick32_16(u32 x) ABI {return (__bit)(x&0x10000UL);}
u8 pick32_17(u32 x) ABI {return (__bit)(x&0x20000UL);}
u8 pick32_18(u32 x) ABI {return (__bit)(x&0x40000UL);}
u8 pick32_19(u32 x) ABI {return (__bit)(x&0x80000UL);}
u8 pick32_20(u32 x) ABI {return (__bit)(x&0x100000UL);}
u8 pick32_21(u32 x) ABI {return (__bit)(x&0x200000UL);}
u8 pick32_22(u32 x) ABI {return (__bit)(x&0x400000UL);}
u8 pick32_23(u32 x) ABI {return (__bit)(x&0x800000UL);}
u8 pick32_24(u32 x) ABI {return (__bit)(x&0x1000000UL);}
u8 pick32_25(u32 x) ABI {return (__bit)(x&0x2000000UL);}
u8 pick32_26(u32 x) ABI {return (__bit)(x&0x4000000UL);}
u8 pick32_27(u32 x) ABI {return (__bit)(x&0x8000000UL);}
u8 pick32_28(u32 x) ABI {return (__bit)(x&0x10000000UL);}
u8 pick32_29(u32 x) ABI {return (__bit)(x&0x20000000UL);}
u8 pick32_30(u32 x) ABI {return (__bit)(x&0x40000000UL);}
u8 pick32_31(u32 x) ABI {return (__bit)(x&0x80000000UL);}
u32 pressure0(u32 a,u32 b) ABI {u32 v0=(a+0x1234567UL)^b,v1=(a+0x2468aceUL)^b,v2=(a+0x369d035UL)^b,v3=(a+0x48d159cUL)^b,v4=(a+0x5b05b03UL)^b,v5=(a+0x6d3a06aUL)^b,v6=(a+0x7f6e5d1UL)^b,v7=(a+0x91a2b38UL)^b,v8=(a+0xa3d709fUL)^b,v9=(a+0xb60b606UL)^b,v10=(a+0xc83fb6dUL)^b,v11=(a+0xda740d4UL)^b;u8 w0=(__bit)(v0&0x1UL),w1=(__bit)(v1&0x1UL),w2=(__bit)(v2&0x1UL),w3=(__bit)(v3&0x1UL),w4=(__bit)(v4&0x1UL),w5=(__bit)(v5&0x1UL),w6=(__bit)(v6&0x1UL),w7=(__bit)(v7&0x1UL),w8=(__bit)(v8&0x1UL),w9=(__bit)(v9&0x1UL),w10=(__bit)(v10&0x1UL),w11=(__bit)(v11&0x1UL);return ((u32)w0+w1+w2+w3+w4+w5+w6+w7+w8+w9+w10+w11)^(v0^v1^v2^v3^v4^v5^v6^v7^v8^v9^v10^v11);}
u32 pressure7(u32 a,u32 b) ABI {u32 v0=(a+0x1234567UL)^b,v1=(a+0x2468aceUL)^b,v2=(a+0x369d035UL)^b,v3=(a+0x48d159cUL)^b,v4=(a+0x5b05b03UL)^b,v5=(a+0x6d3a06aUL)^b,v6=(a+0x7f6e5d1UL)^b,v7=(a+0x91a2b38UL)^b,v8=(a+0xa3d709fUL)^b,v9=(a+0xb60b606UL)^b,v10=(a+0xc83fb6dUL)^b,v11=(a+0xda740d4UL)^b;u8 w0=(__bit)(v0&0x80UL),w1=(__bit)(v1&0x80UL),w2=(__bit)(v2&0x80UL),w3=(__bit)(v3&0x80UL),w4=(__bit)(v4&0x80UL),w5=(__bit)(v5&0x80UL),w6=(__bit)(v6&0x80UL),w7=(__bit)(v7&0x80UL),w8=(__bit)(v8&0x80UL),w9=(__bit)(v9&0x80UL),w10=(__bit)(v10&0x80UL),w11=(__bit)(v11&0x80UL);return ((u32)w0+w1+w2+w3+w4+w5+w6+w7+w8+w9+w10+w11)^(v0^v1^v2^v3^v4^v5^v6^v7^v8^v9^v10^v11);}
u32 pressure8(u32 a,u32 b) ABI {u32 v0=(a+0x1234567UL)^b,v1=(a+0x2468aceUL)^b,v2=(a+0x369d035UL)^b,v3=(a+0x48d159cUL)^b,v4=(a+0x5b05b03UL)^b,v5=(a+0x6d3a06aUL)^b,v6=(a+0x7f6e5d1UL)^b,v7=(a+0x91a2b38UL)^b,v8=(a+0xa3d709fUL)^b,v9=(a+0xb60b606UL)^b,v10=(a+0xc83fb6dUL)^b,v11=(a+0xda740d4UL)^b;u8 w0=(__bit)(v0&0x100UL),w1=(__bit)(v1&0x100UL),w2=(__bit)(v2&0x100UL),w3=(__bit)(v3&0x100UL),w4=(__bit)(v4&0x100UL),w5=(__bit)(v5&0x100UL),w6=(__bit)(v6&0x100UL),w7=(__bit)(v7&0x100UL),w8=(__bit)(v8&0x100UL),w9=(__bit)(v9&0x100UL),w10=(__bit)(v10&0x100UL),w11=(__bit)(v11&0x100UL);return ((u32)w0+w1+w2+w3+w4+w5+w6+w7+w8+w9+w10+w11)^(v0^v1^v2^v3^v4^v5^v6^v7^v8^v9^v10^v11);}
u32 pressure15(u32 a,u32 b) ABI {u32 v0=(a+0x1234567UL)^b,v1=(a+0x2468aceUL)^b,v2=(a+0x369d035UL)^b,v3=(a+0x48d159cUL)^b,v4=(a+0x5b05b03UL)^b,v5=(a+0x6d3a06aUL)^b,v6=(a+0x7f6e5d1UL)^b,v7=(a+0x91a2b38UL)^b,v8=(a+0xa3d709fUL)^b,v9=(a+0xb60b606UL)^b,v10=(a+0xc83fb6dUL)^b,v11=(a+0xda740d4UL)^b;u8 w0=(__bit)(v0&0x8000UL),w1=(__bit)(v1&0x8000UL),w2=(__bit)(v2&0x8000UL),w3=(__bit)(v3&0x8000UL),w4=(__bit)(v4&0x8000UL),w5=(__bit)(v5&0x8000UL),w6=(__bit)(v6&0x8000UL),w7=(__bit)(v7&0x8000UL),w8=(__bit)(v8&0x8000UL),w9=(__bit)(v9&0x8000UL),w10=(__bit)(v10&0x8000UL),w11=(__bit)(v11&0x8000UL);return ((u32)w0+w1+w2+w3+w4+w5+w6+w7+w8+w9+w10+w11)^(v0^v1^v2^v3^v4^v5^v6^v7^v8^v9^v10^v11);}
u32 pressure16(u32 a,u32 b) ABI {u32 v0=(a+0x1234567UL)^b,v1=(a+0x2468aceUL)^b,v2=(a+0x369d035UL)^b,v3=(a+0x48d159cUL)^b,v4=(a+0x5b05b03UL)^b,v5=(a+0x6d3a06aUL)^b,v6=(a+0x7f6e5d1UL)^b,v7=(a+0x91a2b38UL)^b,v8=(a+0xa3d709fUL)^b,v9=(a+0xb60b606UL)^b,v10=(a+0xc83fb6dUL)^b,v11=(a+0xda740d4UL)^b;u8 w0=(__bit)(v0&0x10000UL),w1=(__bit)(v1&0x10000UL),w2=(__bit)(v2&0x10000UL),w3=(__bit)(v3&0x10000UL),w4=(__bit)(v4&0x10000UL),w5=(__bit)(v5&0x10000UL),w6=(__bit)(v6&0x10000UL),w7=(__bit)(v7&0x10000UL),w8=(__bit)(v8&0x10000UL),w9=(__bit)(v9&0x10000UL),w10=(__bit)(v10&0x10000UL),w11=(__bit)(v11&0x10000UL);return ((u32)w0+w1+w2+w3+w4+w5+w6+w7+w8+w9+w10+w11)^(v0^v1^v2^v3^v4^v5^v6^v7^v8^v9^v10^v11);}
u32 pressure23(u32 a,u32 b) ABI {u32 v0=(a+0x1234567UL)^b,v1=(a+0x2468aceUL)^b,v2=(a+0x369d035UL)^b,v3=(a+0x48d159cUL)^b,v4=(a+0x5b05b03UL)^b,v5=(a+0x6d3a06aUL)^b,v6=(a+0x7f6e5d1UL)^b,v7=(a+0x91a2b38UL)^b,v8=(a+0xa3d709fUL)^b,v9=(a+0xb60b606UL)^b,v10=(a+0xc83fb6dUL)^b,v11=(a+0xda740d4UL)^b;u8 w0=(__bit)(v0&0x800000UL),w1=(__bit)(v1&0x800000UL),w2=(__bit)(v2&0x800000UL),w3=(__bit)(v3&0x800000UL),w4=(__bit)(v4&0x800000UL),w5=(__bit)(v5&0x800000UL),w6=(__bit)(v6&0x800000UL),w7=(__bit)(v7&0x800000UL),w8=(__bit)(v8&0x800000UL),w9=(__bit)(v9&0x800000UL),w10=(__bit)(v10&0x800000UL),w11=(__bit)(v11&0x800000UL);return ((u32)w0+w1+w2+w3+w4+w5+w6+w7+w8+w9+w10+w11)^(v0^v1^v2^v3^v4^v5^v6^v7^v8^v9^v10^v11);}
u32 pressure24(u32 a,u32 b) ABI {u32 v0=(a+0x1234567UL)^b,v1=(a+0x2468aceUL)^b,v2=(a+0x369d035UL)^b,v3=(a+0x48d159cUL)^b,v4=(a+0x5b05b03UL)^b,v5=(a+0x6d3a06aUL)^b,v6=(a+0x7f6e5d1UL)^b,v7=(a+0x91a2b38UL)^b,v8=(a+0xa3d709fUL)^b,v9=(a+0xb60b606UL)^b,v10=(a+0xc83fb6dUL)^b,v11=(a+0xda740d4UL)^b;u8 w0=(__bit)(v0&0x1000000UL),w1=(__bit)(v1&0x1000000UL),w2=(__bit)(v2&0x1000000UL),w3=(__bit)(v3&0x1000000UL),w4=(__bit)(v4&0x1000000UL),w5=(__bit)(v5&0x1000000UL),w6=(__bit)(v6&0x1000000UL),w7=(__bit)(v7&0x1000000UL),w8=(__bit)(v8&0x1000000UL),w9=(__bit)(v9&0x1000000UL),w10=(__bit)(v10&0x1000000UL),w11=(__bit)(v11&0x1000000UL);return ((u32)w0+w1+w2+w3+w4+w5+w6+w7+w8+w9+w10+w11)^(v0^v1^v2^v3^v4^v5^v6^v7^v8^v9^v10^v11);}
u32 pressure31(u32 a,u32 b) ABI {u32 v0=(a+0x1234567UL)^b,v1=(a+0x2468aceUL)^b,v2=(a+0x369d035UL)^b,v3=(a+0x48d159cUL)^b,v4=(a+0x5b05b03UL)^b,v5=(a+0x6d3a06aUL)^b,v6=(a+0x7f6e5d1UL)^b,v7=(a+0x91a2b38UL)^b,v8=(a+0xa3d709fUL)^b,v9=(a+0xb60b606UL)^b,v10=(a+0xc83fb6dUL)^b,v11=(a+0xda740d4UL)^b;u8 w0=(__bit)(v0&0x80000000UL),w1=(__bit)(v1&0x80000000UL),w2=(__bit)(v2&0x80000000UL),w3=(__bit)(v3&0x80000000UL),w4=(__bit)(v4&0x80000000UL),w5=(__bit)(v5&0x80000000UL),w6=(__bit)(v6&0x80000000UL),w7=(__bit)(v7&0x80000000UL),w8=(__bit)(v8&0x80000000UL),w9=(__bit)(v9&0x80000000UL),w10=(__bit)(v10&0x80000000UL),w11=(__bit)(v11&0x80000000UL);return ((u32)w0+w1+w2+w3+w4+w5+w6+w7+w8+w9+w10+w11)^(v0^v1^v2^v3^v4^v5^v6^v7^v8^v9^v10^v11);}
