volatile __edata unsigned char shared = 0x42;

void update(volatile unsigned char __edata *p)
{
    ++*p;
}
