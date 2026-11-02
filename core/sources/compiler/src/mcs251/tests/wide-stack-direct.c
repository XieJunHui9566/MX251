typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)

/* 分支汇合处不允许继承单一路径的对象身份。 */
u32 d_select(u32 x,u16 n) ABI {
 volatile u32 a[2],b[2];volatile u32 *p;
 a[0]=x;a[1]=x+1;b[0]=~x;b[1]=x^0x12345678UL;
 if(n&1)p=a;else p=b;
 return p[0]^p[1];
}
/* 循环回边和指针自增不能误用首次迭代的偏移。 */
u32 d_loop(u32 x,u16 n) ABI {
 volatile u32 a[4];volatile u32 *p;u16 i;u32 r=0;
 for(i=0;i<4;++i)a[i]=x+(u32)i+(u32)n;
 p=a;for(i=0;i<4;++i)r^=*p++;
 return r;
}
/* 固定偏移跨越 256 字节边界，不能截断到 8 位。 */
u32 d_page(u32 x) ABI {
 volatile u8 a[280];
 a[0]=(u8)x;a[255]=(u8)(x>>8);a[256]=(u8)(x>>16);a[279]=(u8)(x>>24);
 return (u32)a[0]+a[255]+a[256]+a[279];
}
/* 字符指针别名改变字节，随后必须重新读取完整对象。 */
u32 d_cast(u32 x) ABI {
 volatile u32 a[2];volatile u8 *p;
 a[0]=x;a[1]=~x;p=(volatile u8 *)a;p[1]^=0x5a;p[7]=0xc3;
 return a[0]^a[1];
}
u32 d_back(volatile u32 *p,u32 x) ABI {--p;*p^=x;return *p;}
/* one-past 可以传递给调用者，但实际解引用必须退回对象内。 */
u32 d_end(u32 x) ABI {
 volatile u32 a[4];u32 r;
 a[3]=x+7;r=d_back(a+4,x);return r+a[3];
}
