/* Replace c_float only in the supplemental word-home corpus. Values cross
 * real calls and loop edges; signed16 extension must retain its bit meaning. */
s16 home_word(s16 a,u8 n) OLD
{u32 v=0;while(n--)v=old_pair(v,(u32)a);return (s16)(u16)(v^(u32)a);}
u16 home_uword(u16 a,u16 b) OLD
{return (u16)(old_pair((u32)b,(u32)a)^a);}
u32 c_float(u32 a,u32 b) OLD
{s16 x=home_word((s16)a,(u8)(b&3U));u16 y=home_uword((u16)b,(u16)a);return (u32)x+y+a+b;}
