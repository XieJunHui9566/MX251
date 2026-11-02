typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
typedef union {u32 u;float f;u8 b[4];} bits;
#define ABI __reentrant __sdcccall(4)
u32 s_array(u32 x,u32 index) ABI {
    u8 a[31];u8 i;u32 sum=0;
    for(i=0;i<31;++i)a[i]=(u8)(x+i);
    a[index&15]^=0xa5;
    for(i=0;i<31;++i)sum+=a[i];
    return sum;
}
float s_float(const float *p) ABI {return *p;}
float s_union(u32 x) ABI {bits a;a.u=x;return s_float(&a.f);}
u32 s_consume(u32 *p,u32 x) ABI {
    u8 a[40];u8 i;u32 sum=0;
    for(i=0;i<40;++i)a[i]=(u8)(x+i);
    for(i=0;i<40;++i)sum+=a[i];
    p[1]^=x;
    return sum+p[0]+p[1]+p[2];
}
u32 s_tail(u32 x) ABI {u32 a[3];a[0]=x;a[1]=x+1;a[2]=x+2;return s_consume(a,x);}
u32 s_nested(u32 x) ABI {
    u32 a[3];u32 v;a[0]=x;a[1]=x+1;a[2]=x+2;
    v=s_consume(a,x);v+=s_consume(a,x+1);
    return v+a[1];
}
u32 s_page(u32 x) ABI {
    u8 a[280];u16 i;u32 sum=0;
    for(i=0;i<280;++i)a[i]=(u8)(x+i);
    for(i=0;i<280;++i)sum+=a[i];
    return sum;
}
u32 s_objects(u32 x,u32 n) ABI {
    u8 a[3];u32 b[3];bits c;
    a[0]=(u8)x;a[1]=(u8)(x>>8);a[2]=(u8)(x>>16);
    b[0]=x;b[1]=x^0x01234567UL;b[2]=x+0x89abcdefUL;c.u=x;
    b[n&1]+=a[n&1];c.b[n&3]^=0xa5;
    return b[0]^b[1]^b[2]^c.u^a[2];
}
u32 s_spill(u32 *p,u32 x) ABI {
    u32 a[3];u32 v;
    u32 b=p[0],c=p[1],d=p[2],e=p[3],f=p[4],g=p[5],h=p[6],i=p[7],j=p[8];
    a[0]=x;a[1]=x+1;a[2]=x+2;v=s_consume(a,x);
    return v+b+(c>>1)+(d>>2)+(e>>3)+f+(g>>1)+(h>>2)+(i>>3)+j+(b^c^d^e^f^g^h^i^j);
}
u32 s_volatile(u32 x,u32 n) ABI {
    volatile u32 a[3];a[0]=x;a[1]=x+1;a[2]=x+2;a[n&1]^=x;
    return a[0]+a[1]+a[2];
}
u32 s_two(u32 x,u32 n) ABI {u8 a[5],b[7];a[n&3]=(u8)x;b[n&3]=(u8)(x>>8);return (u32)a[n&3]+b[n&3];}
u32 s_recurse(u32 x,u32 depth) ABI {
    u32 a[3];u32 v;a[0]=x;a[1]=x+1;a[2]=x+2;
    if(depth)v=s_recurse(x+3,depth-1);else v=0;
    return v+a[0]+a[1]+a[2];
}
u32 s_addressed_scalar(u32 x) ABI {u32 a=x;u32 *p=&a;*p^=0xa5a5a5a5UL;return a;}
u32 s_too_big(u32 x) ABI {u8 a[513];a[x&511]=(u8)x;return a[x&511];}
