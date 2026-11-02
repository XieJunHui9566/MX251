/* MCS251 frame locations. GPL-2.0-or-later, same license as the backend. */
#ifndef SDCC_MCS251_FRAME_H
#define SDCC_MCS251_FRAME_H

/* 视图只表示对象中的一次内存访问，不另占栈空间，也不是缓存的值。
   布局完成后仍通过 object 查询位置，避免溢出槽改变布局后偏移失效。 */
typedef struct mcs251_frame_view
{
  symbol *view;
  symbol *object;
  int byteOffset;
  bool byteAccess;
  struct mcs251_frame_view *next;
} mcs251_frame_view;

void mcs251LowerFrameAccesses (eBBlock **blocks, int count);
const mcs251_frame_view *mcs251FrameView (operand *op);
void mcs251ClearFrameAccesses (void);
#endif
