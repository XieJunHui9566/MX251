/* 计数器只在原生 JNE 的跳转路径读取，扫描两条后继才能保留必需的初始化。 */
typedef unsigned char u8;typedef unsigned long u32;
volatile __xdata u8 rx_ring[256],rx_tail,rx_head;
volatile __xdata u32 ticks;
__xdata u8 position,packet[32];
__xdata u32 stall_until,last_byte_tick,timeouts,noise;
static u32 tick(void){return ticks;}
extern void process(void);
void receive(void) {
 u8 value,n=0;u32 now=tick();
 if(now<stall_until)return;
 if(position&&now-last_byte_tick>25UL){position=0;timeouts++;}
 /* 每轮最多处理一帧，主循环数学任务仍持续前进。 */
 while(rx_tail!=rx_head&&n++<32) {
  value=rx_ring[rx_tail];rx_tail++;last_byte_tick=now;
  if(!position){if(value==0xa5){packet[0]=value;position=1;}else noise++;}
  else if(position==1){if(value==0x5a){packet[1]=value;position=2;}else if(value!=0xa5){position=0;noise+=2;}else noise++;}
  else {packet[position++]=value;if(position==32){process();break;}}
 }
}
