/* Requires the EDATA development compiler and its matching linker. */
volatile __edata unsigned char buffer[260];
volatile __edata unsigned int count = 7;
volatile __xdata unsigned char result;
extern volatile __edata unsigned char shared;
void update(volatile unsigned char __edata *p);

void main(void)
{
    static __edata unsigned int saved;
    buffer[259] = shared;
    update(&buffer[259]);
    saved = count;
    result = buffer[259] == 0x43 && saved == 7 && buffer[258] == 0;
    for (;;) {}
}
