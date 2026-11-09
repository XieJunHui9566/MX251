#include "symbol-address.h"
u16 address_first(const u8 *p,u8 count,u16 salt) {
    while(count--) salt=(salt<<3)^salt^*p++;
    return salt;
}
u16 address_stack(u16 salt,const u8 *p,u8 count,const u8 *q) {
    return address_first(p,count,salt)^address_first(q,count,salt+17);
}
u16 address_code(const u8 __code *p,u8 count,u16 salt) {
    while(count--) salt=(salt<<3)^salt^*p++;
    return salt;
}
