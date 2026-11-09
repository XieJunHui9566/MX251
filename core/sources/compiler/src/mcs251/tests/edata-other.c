volatile __edata unsigned char other = 0x6b;

unsigned int exchange (volatile unsigned char __edata *p)
{
    ++*p;
    return *p;
}
