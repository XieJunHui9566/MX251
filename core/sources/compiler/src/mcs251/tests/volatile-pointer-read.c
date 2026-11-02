/* volatile 限定指向对象，不限定指针本身。存储后的读取不能被前递值替代。 */
typedef unsigned char u8;
u8 volatile_read(volatile __idata u8 *p) __reentrant
{
    *p = 0x35;
    return *p;
}
u8 ordinary_read(__idata u8 *p) __reentrant
{
    *p = 0x35;
    return *p;
}
