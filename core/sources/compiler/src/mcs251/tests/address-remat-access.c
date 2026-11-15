#include "address-remat.h"

u32 remat_fields(remat_record *p,u8 count)
{
    u32 sum=0;
    while(count--) {
        sum+=p->a+p->b+p->x+p->y;
        remat_touch(&p->c,count);
        sum^=p->tag+(u32)p->c*257UL;
        p->x+=p->d;
        p->y^=p->x;
    }
    return sum;
}

u32 remat_cursor(u8 *p,u8 count)
{
    u8 i;
    u32 sum=0;
    for(i=0;i<count;++i) {
        u8 *q=p+i;
        sum+=*q;
        remat_touch(p+31,i);
        *q^=i+3;
        sum=(sum<<1)^*q;
    }
    return sum;
}

u32 remat_snapshot(u8 *p,u8 count)
{
    u8 *saved=p+5;
    u32 sum=0;
    while(count--) {
        ++p;
        remat_touch(saved,count);
        sum=(sum<<1)^*saved;
        sum+=p[2];
    }
    return sum;
}

u32 remat_branch(u8 *p,u8 mode)
{
    u8 *old=p+4;
    u32 sum=*old;
    switch(mode&3) {
    case 0:p+=7;break;
    case 1:p+=11;break;
    case 2:p+=3;break;
    default:p+=5;break;
    }
    remat_touch(old,mode);
    sum=(sum<<16)|((u16)*old<<8)|*p;
    return sum;
}
