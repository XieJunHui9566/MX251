typedef unsigned char u8;
typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
u32 p_consume(u32 *p,u32 x) ABI;
u32 p_phases(u32 *p,u32 x) ABI {
    u32 a[3],total;
    {
        u32 b=p[0],c=p[1],d=p[2],e=p[3],f=p[4],g=p[5],h=p[6],i=p[7],j=p[8],v;
        a[0]=x;a[1]=x+1;a[2]=x+2;v=p_consume(a,x);
        total=v+b+(c>>1)+(d>>2)+(e>>3)+f+(g>>1)+(h>>2)+(i>>3)+j+(b^c^d^e^f^g^h^i^j);
    }
    {
        u32 b=p[8],c=p[7],d=p[6],e=p[5],f=p[4],g=p[3],h=p[2],i=p[1],j=p[0],v;
        a[0]=x+3;a[1]=x+4;a[2]=x+5;v=p_consume(a,x+3);
        return total+v+b+(c>>1)+(d>>2)+(e>>3)+f+(g>>1)+(h>>2)+(i>>3)+j+(b^c^d^e^f^g^h^i^j);
    }
}
