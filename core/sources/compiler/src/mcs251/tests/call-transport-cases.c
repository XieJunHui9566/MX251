#include "call-transport.h"
u32 transport_case(u8 kind,u8 seed)
{
    return kind?transport_right(seed):transport_left(seed);
}
