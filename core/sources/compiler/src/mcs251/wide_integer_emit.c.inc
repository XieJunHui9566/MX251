/* SPDX-License-Identifier: GPL-2.0-or-later */
static void
wword_product(wide_function *f,wide_node *n)
{
  /* U(a)*U(b) - sign(a)*(b<<16) - sign(b)*(a<<16), modulo 2^32.
   * Save corrections before MUL overwrites its first word with the high
   * product. DR16/DR20 and B:A are emitter scratch, never allocated values. */
  wload(f,n->left,16,2);wload(f,n->right,20,2);
  we(f,"; wide word-origin multiply signed-mask=%u\n",n->multiplySignedMask);
  if(n->multiplySignedMask) {
    wi(f,22,2,0);
    if(n->multiplySignedMask&1) {
      int skip=wt(f);we(f,"\tcmp\twr16,#0\n");wj(f,"jsge",skip);
      we(f,"\tsub\twr22,wr20\n");wl(f,skip);
    }
    if(n->multiplySignedMask&2) {
      int skip=wt(f);we(f,"\tcmp\twr20,#0\n");wj(f,"jsge",skip);
      we(f,"\tsub\twr22,wr16\n");wl(f,skip);
    }
  }
  we(f,"\tmul\twr16,wr20\n");
  if(n->multiplySignedMask)we(f,"\tadd\twr16,wr22\n");
  wstore(f,n->result,16);
}

static void
wsigned_power2(wide_function *f,wide_node *n)
{
  unsigned count=n->right.literal;int width=n->width;
  int work=f->values[n->result].reg>=0?f->values[n->result].reg:16;
  wload(f,n->left,work,width);
  we(f,"; wide signed power2 quotient width=%d shift=%u rounding=zero\n",width,count);
  if(count) {
    /* Bias only negative inputs. INT_MIN + (2^k-1) remains representable;
     * no absolute value or overflowing negation is needed. */
    int skip=wt(f);we(f,"\tcmp\twr%d,#0\n",work);wj(f,"jsge",skip);
    uint32_t bias=(UINT32_C(1)<<count)-1;
    if(bias<=65535)wstep(f,work,width,false,bias);
    else {wi(f,20,width,bias);we(f,"\tadd\t%s%d,%s20\n",wp(width),work,wp(width));}
    wl(f,skip);
    if(width==4 && wshift_boundary_right(f,work,count,true)) {
      wstore(f,n->result,work);return;
    }
    /* At least sixteen shifts discard the entire low input word. Shift the
     * high word alone and extend only the final signed quotient. */
    bool wordOnly=width==4 && count>=16;
    int shiftWork=work,shiftWidth=width;
    if(wordOnly) {count-=16;shiftWidth=2;}
    if(count>=8) {wshift_byte(f,shiftWork,shiftWidth,true,true);count-=8;}
    if(count<=4)while(count--)wshift_one(f,shiftWork,shiftWidth,true,true);
    else {
      int loop=wt(f);we(f,"\tmov\ta,#%u\n",count);wl(f,loop);
      wshift_one(f,shiftWork,shiftWidth,true,true);we(f,"\tdec\ta\n");wj(f,"jnz",loop);
    }
    if(wordOnly) {we(f,"\tmov\twr%d,wr%d\n",work+2,work);wsign_word(f,work,work+2);}
  }
  wstore(f,n->result,work);
}
