/* MCS251 bounded physical caller-save packing. GPL-2.0-or-later. */
#ifndef MCS251_WIDE_CALL_SAVE_PLAN_H
#define MCS251_WIDE_CALL_SAVE_PLAN_H
#include "register_facts.h"

typedef struct { unsigned first, width; } mcs251_wide_save_step;
typedef struct {
  mcs251_wide_save_step steps[32];
  unsigned count, bytes;
  uint32_t mask;
  unsigned reordered;
} mcs251_wide_save_plan;

/* mode1 完全保留 PUSH 的逐字节顺序；mode2 仅重排调用者私有保存区。
 * 两者都不保存额外字节，不省略任何活字节，不修改调用参数/返回布局。
 * mode2 的显式实验契约要求完整镜像无私有保存区观察者，不能仅凭模式1
 * 的测试授予其资格。A/B、保留 scratch 及不可编码高组 byte 都拒绝。 */
static int
mcs251BuildWideSavePlan(const mcs251_wide_save_step *original,unsigned count,
                       unsigned mode,mcs251_wide_save_plan *plan)
{
  unsigned originalBytes[32],ordered[32],total=0;
  uint32_t mask=0;
  plan->count=plan->bytes=plan->reordered=0;plan->mask=0;
  if((mode!=1 && mode!=2) || !count || count>32)return 0;
  for(unsigned i=0;i<count;++i) {
    unsigned first=original[i].first,width=original[i].width;
    uint32_t part=mcs251RegisterBytes(first,width);
    if(!mcs251RegisterEncodable(first,width) || !part || (mask&part) ||
       (part&(MCS251_AB_BYTES|mcs251RegisterBytes(16,4)|mcs251RegisterBytes(20,4))) ||
       total+width>32)return 0;
    for(unsigned j=0;j<width;++j)originalBytes[total++]=first+j;
    mask|=part;
  }
  unsigned n=0;
  if(mode==1)for(unsigned i=0;i<total;++i)ordered[n++]=originalBytes[i];
  else for(unsigned first=0;first<32;++first)if(mask&(UINT32_C(1)<<first))ordered[n++]=first;
  if(n!=total)return 0;
  for(unsigned i=0;i<total;) {
    unsigned chosen=0;
    for(unsigned width=4;width;width/=2) {
      if(i+width>total || !mcs251RegisterEncodable(ordered[i],width))continue;
      unsigned j=0;
      while(j<width && ordered[i+j]==ordered[i]+j)++j;
      if(j==width){chosen=width;break;}
    }
    if(!chosen)return 0;
    plan->steps[plan->count++]=(mcs251_wide_save_step){ordered[i],chosen};
    plan->bytes+=chosen;i+=chosen;
  }
  if(plan->count>=count || plan->bytes!=total)return 0;
  for(unsigned i=0;i<total;++i)plan->reordered|=ordered[i]!=originalBytes[i];
  if(mode==1 && plan->reordered)return 0;
  plan->mask=mask;
  return 1;
}
#endif
