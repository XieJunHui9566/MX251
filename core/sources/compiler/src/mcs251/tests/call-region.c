/* GPL-2.0-or-later. Stack topology and conservative parse boundaries. */
#include "../call_region.h"
#include <assert.h>
int main(void)
{
  unsigned checks=0;char out[M251_OUTLINE_TEXT/2],key[M251_OUTLINE_TEXT],body[M251_OUTLINE_TEXT];
  for (int depth=0;depth<=32;++depth) for (int d=-64;d<=0;++d)
    for (unsigned w=1;w<=4;w*=2) {
      char src[64];unsigned growth=0;
      if (d) sprintf(src,"@spx-0x%04x",-d);else strcpy(src,"@spx");
      int lo=depth+d,hi=lo+(int)w-1;
      int expected=hi<=depth && !(lo<=0 && hi>0);
      int accepted=m251CallStackOperand(src,w,depth,out,&growth);
      assert(accepted==expected);
      if (accepted) {
        int revised=!strcmp(out,"@spx")?0:-(int)strtol(out+5,NULL,0);
        /* 原帧物理地址不变；区域新槽整体平移3字节，内容顺序不变。 */
        assert(depth+3+revised==depth+d+(hi<=0?0:3));
        assert(growth==(!d && hi<=0?2u:0u));
      }
      ++checks;
    }
  for (unsigned width=1;width<=4;width*=2) {
    unsigned growth=0;assert(!m251CallStackOperand("@spx-0x8000",width,0,out,&growth));
    assert(!m251CallStackOperand("@spx+2",width,0,out,&growth));
    assert(!m251CallStackOperand("@spx-symbol",width,0,out,&growth));
  }
  m251_call_stack s={0};
  assert(m251CallStackPush(&s,4,0));assert(m251CallStackPush(&s,1,0));assert(m251CallStackPush(&s,2,0));
  assert(m251CallStackPush(&s,4,1));assert(!m251CallStackPop(&s,1));
  assert(!m251CallStackInvoke(&s,3));assert(m251CallStackInvoke(&s,4));assert(!m251CallStackInvoke(&s,4));
  assert(m251CallStackPop(&s,4));assert(m251CallStackPop(&s,2));assert(m251CallStackPop(&s,1));assert(m251CallStackPop(&s,4));assert(!s.depth);
  memset(&s,0,sizeof s);assert(m251CallStackPush(&s,4,1));assert(m251CallStackPush(&s,1,0));assert(!m251CallStackInvoke(&s,4));
  memset(&s,0,sizeof s);assert(!m251CallStackInvoke(&s,4));assert(!m251CallStackPop(&s,1));
  unsigned growth=0;
  assert(m251CallRegionLine("push ar3",&s,0,key,body,&growth));
  assert(!m251CallRegionLine("push spx",&s,0,key,body,&growth));
  assert(!m251CallRegionLine("mov dr4,spx",&s,0,key,body,&growth));
  assert(!m251CallRegionLine("mov @spx,wr4",&s,0,key,body,&growth));
  assert(!m251CallRegionLine("mov wr4,@spx-0x0001",&s,0,key,body,&growth));
  assert(!m251CallRegionLine("ecall _callee",&s,0,key,body,&growth));
  assert(!m251CallRegionLine("push #1",&s,0,key,body,&growth));
  assert(m251CallRegionLine("pushw #0x1234",&s,1,key,body,&growth));
  assert(m251CallRegionLine("mov a,@spx",&s,0,key,body,&growth));assert(!strcmp(body,"mov a,@spx"));
  assert(m251CallRegionLine("mov a,@spx-0x0003",&s,0,key,body,&growth));assert(!strcmp(body,"mov a,@spx-0x0006"));
  printf("%u stack-address relations and stack-ownership/unknown-operation boundaries passed\n",checks);
  return 0;
}
