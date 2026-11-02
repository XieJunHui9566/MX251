typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)

u32 p_touch(u8 *a,u32 x) ABI {
    u8 k;u32 sum=0;
    for(k=0;k<7;++k){a[k]^=(u8)(x>>(k&3));sum+=a[k];}
    return sum;
}

u32 p_mixed(u32 *p,u32 x) ABI {
    u8 a[7],k,b=(u8)p[0],c=(u8)p[1];
    u16 d=(u16)p[2],e=(u16)p[3];
    u32 f=p[4],g=p[5],h=p[6],i=p[7],j=p[8],q;
    for(k=0;k<7;++k)a[k]=(u8)(x+k);
    q=p_touch(a,x);
    if(x&1)q+=b+(d>>3)+f+(h^j);
    else q+=c+(e>>5)+g+(i^j);
    return q+(u32)(b^c)+(u32)(d^e)+f+g+h+i+j+a[x&3];
}

u32 p_consume(u32 *p,u32 x) ABI {
    u8 a[40],k;u32 sum=0;
    for(k=0;k<40;++k)a[k]=(u8)(x+k);
    for(k=0;k<40;++k)sum+=a[k];
    p[1]^=x;return sum+p[0]+p[1]+p[2];
}

u32 p_spill(u32 *p,u32 x) ABI {
    u32 a[3],v;
    u32 b=p[0],c=p[1],d=p[2],e=p[3],f=p[4],g=p[5],h=p[6],i=p[7],j=p[8];
    a[0]=x;a[1]=x+1;a[2]=x+2;v=p_consume(a,x);
    return v+b+(c>>1)+(d>>2)+(e>>3)+f+(g>>1)+(h>>2)+(i>>3)+j+(b^c^d^e^f^g^h^i^j);
}
