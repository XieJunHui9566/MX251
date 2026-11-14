#include "stress.h"
typedef struct { u8 bytes[16]; u8 head, tail; u16 dropped; } Queue;
static __edata Queue q;

u32 ring_clean(u8 seed)
{
    u8 i; u32 sum = 0;
    q.head = q.tail = 0; q.dropped = 0;
    for (i = 0; i < 96; ++i) {
        u8 next = (q.head + 1) % 16;
        if (next != q.tail) { q.bytes[q.head] = (u8)(seed + i); q.head = next; }
        else ++q.dropped;
        if ((i & 3) == 0 && q.head != q.tail) {
            sum = (sum << 1) ^ q.bytes[q.tail]; q.tail = (q.tail + 1) % 16;
        }
    }
    return sum ^ ((u32)q.dropped << 16) ^ ((u16)q.head << 8) ^ q.tail;
}

u32 ring_messy(u8 seed)
{
    Queue *p = &q; u8 i = 0, next; u32 sum = 0;
    p->tail = 0; q.head = 0; p->dropped = 0;
loop:
    next = (p->head + 1) & 15;
    if (next == q.tail) goto full;
    *(p->bytes + p->head) = (u8)(seed + i); q.head = next; goto pop;
full:
    q.dropped = (u16)(p->dropped + 1);
pop:
    if (i & 3) goto advance;
    if (q.head == p->tail) goto advance;
    sum = (sum << 1) ^ *(q.bytes + p->tail); p->tail = (q.tail + 1) & 15;
advance:
    if (++i < 96) goto loop;
    return sum ^ ((u32)p->dropped << 16) ^ ((u16)p->head << 8) ^ p->tail;
}
