/* 普通并行复制也可使用 WR/DR，但不能覆盖其他尚未读取的源字节。
 * 只选择完整、对齐、按标量字节次序排列的 R0..R15；A/B 别名和
 * SFR 保留原路径。不借用临时寄存器、不增减栈、不改变分配结果。
 * 循环置换继续由 genCopy 原有快照处理；这里只执行已证明安全的一组。 */
static int
mcs251CopyRegisterGroup (asmop *result, int roffset, asmop *source,
                         int soffset, int count, bool assigned[8])
{
  if (result->type != AOP_REG || source->type != AOP_REG ||
      result->aop_is_volatile || source->aop_is_volatile ||
      result->aop_is_aggregate || source->aop_is_aggregate)
    return 0;

  for (int width = 4; width >= 2; width /= 2)
    for (int first = 0; first + width <= count; ++first)
      {
        int dest = result->aopu.aop_reg[roffset + first + width - 1]->offset;
        int src = source->aopu.aop_reg[soffset + first + width - 1]->offset;
        bool safe = dest >= 0 && dest + width <= 16 && !(dest % width) &&
                    src >= 0 && src + width <= 16 && !(src % width);
        for (int byte = 0; safe && byte < width; ++byte)
          {
            reg_info *d = result->aopu.aop_reg[roffset + first + byte];
            reg_info *s = source->aopu.aop_reg[soffset + first + byte];
            /* offset 单独不足以识别物理寄存器：DPL/A/B 的 offset 也为 0。 */
            if (assigned[first + byte] ||
                d->rIdx < R7_IDX || d->rIdx > R8_IDX ||
                s->rIdx < R7_IDX || s->rIdx > R8_IDX ||
                d->offset == 10 || d->offset == 11 ||
                s->offset == 10 || s->offset == 11 ||
                d->offset != dest + width - 1 - byte ||
                s->offset != src + width - 1 - byte)
              { safe = false; break; }
            for (int other = 0; other < count; ++other)
              if (!assigned[other] && (other < first || other >= first + width) &&
                  d == source->aopu.aop_reg[soffset + other])
                { safe = false; break; }
          }
        if (!safe)
          continue;
        /* 对齐且等宽的视图只能相同或完全不重叠，MOV 同时读取整组。 */
        if (dest != src)
          emitcode ("mov", "%s%d,%s%d", width == 4 ? "dr" : "wr", dest,
                    width == 4 ? "dr" : "wr", src);
        for (int byte = 0; byte < width; ++byte)
          assigned[first + byte] = true;
        return width;
      }
  return 0;
}
