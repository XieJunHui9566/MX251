typedef unsigned char u8;typedef signed char s8;typedef unsigned int u16;typedef signed int s16;typedef unsigned long u32;typedef signed long s32;
#define ABI __reentrant __sdcccall(4)
#define XP __xdata
u32 touch(u32 XP *,u32) ABI;
static const __code u8 indexed_code[128]={7,20,33,46,59,72,85,98,111,124,137,150,163,176,189,202,215,228,241,254,11,24,37,50,63,76,89,102,115,128,141,154,167,180,193,206,219,232,245,2,15,28,41,54,67,80,93,106,119,132,145,158,171,184,197,210,223,236,249,6,19,32,45,58,71,84,97,110,123,136,149,162,175,188,201,214,227,240,253,10,23,36,49,62,75,88,101,114,127,140,153,166,179,192,205,218,231,244,1,14,27,40,53,66,79,92,105,118,131,144,157,170,183,196,209,222,235,248,5,18,31,44,57,70,83,96,109,122};
u32 idx8(u32 a,u32 b) ABI {return ((u8 XP *)a)[b&127];}
u32 idx16(u32 a,u32 b) ABI {return ((u16 XP *)a)[b&63];}
u32 idx32(u32 a,u32 b) ABI {return ((u32 XP *)a)[b&31];}
u32 idxs8(u32 a,u32 b) ABI {u8 XP *p=(u8 XP *)a+64;return p[((s32)(s8)b)>>2];}
u32 idxs16(u32 a,u32 b) ABI {u8 XP *p=(u8 XP *)a+64;return p[((s32)(s16)b)>>10];}
u32 idxs32(u32 a,u32 b) ABI {u8 XP *p=(u8 XP *)a+64;return p[((s32)b)>>26];}
u32 idxsub(u32 a,u32 b) ABI {u8 XP *p=(u8 XP *)a+96;return *(p-(b&63));}
u32 put8(u32 a,u32 b) ABI {u8 XP *p=(u8 XP *)a;p[b&127]=(u8)(b>>16);return p[(b+1)&127];}
u32 put16(u32 a,u32 b) ABI {u16 XP *p=(u16 XP *)a;p[b&63]=(u16)(b>>8);return p[(b+1)&63];}
u32 put32(u32 a,u32 b) ABI {u32 XP *p=(u32 XP *)a;p[b&31]=b^0x76543210UL;return p[(b+1)&31];}
u32 alias(u32 a,u32 b) ABI {u8 XP *p=(u8 XP *)a;u8 i=(u8)b&127,v=p[i];p[(u8)(i+1)&127]=v^0xa5;return p[i]+(u32)p[(u8)(i+1)&127];}
u32 copyloop(u32 a,u32 b) ABI {u8 XP *p=(u8 XP *)a;u8 i,k=(u8)b;u32 s=b;for(i=0;i<16;++i)p[64+i]=p[(i+(b&31))&63]^k;for(i=0;i<16;++i)s+=p[64+i];return s;}
u32 local(u32 a,u32 b) ABI {u8 v[16];u8 i;u32 s=b;for(i=0;i<16;++i)v[i]=(u8)b^i;for(i=0;i<16;++i)s+=v[(i+(b&15))&15];return s;}
u32 callbarrier(u32 a,u32 b) ABI {u32 XP *p=(u32 XP *)a;u32 XP *q=p+(b&31);u32 c=touch(p,b);return *q+c;}
u32 branch(u32 a,u32 b) ABI {u8 XP *p=(u8 XP *)a;u8 XP *q;if(b&1)q=p+8;else q=p+80;return q[b&7];}
u32 spill(u32 a,u32 b) ABI {u32 XP *p=(u32 XP *)a;u32 v0=p[0],v1=p[1],v2=p[2],v3=p[3],v4=p[4],v5=p[5],v6=p[6],v7=p[7],v8=p[8];u32 c=touch(p,b);return c+v0+(v1>>1)+(v2>>2)+(v3>>3)+v4+(v5>>1)+(v6>>2)+(v7>>3)+v8+(v0^v1^v2^v3^v4^v5^v6^v7^v8);}
u32 vol(u32 a,u32 b) ABI {volatile u32 XP *p=(volatile u32 XP *)a;p+=b&31;*p=*p+b;return *p;}
u32 coderead(u32 a,u32 b) ABI {return indexed_code[b&127]^b;}
