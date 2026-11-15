/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MCS251_WIDE_CALL_REUSE_H
#define MCS251_WIDE_CALL_REUSE_H
#include "instruction_effects.h"
#include <stdio.h>
#include <stdbool.h>

typedef struct { unsigned instructions,weighted,bridges; } wide_call_reuse_result;

/* Argument setup may overwrite a held register before reading it. Track
 * definite writes along straight-line setup, then add the ABI entry reads.
 * This permits literal ABI4 arguments without restoring their former values. */
static uint32_t
wide_call_input_touch(const char *text,size_t length,uint32_t inputs,bool *complete)
{
  uint32_t defined=0,touch=0;*complete=true;
  static const char *allowed[]={"mov","movz","movs","push","pushw","add","addc",
    "sub","subb","anl","orl","xrl","inc","dec","clr","xch","mul","div",
    "sll","srl","sra","rl","rr","rlc","rrc","cpl"};
  for(size_t start=0;start<length;) {
    size_t end=start;while(end<length && text[end]!='\n')++end;
    const char *q=text+start;while(q<text+end && isspace((unsigned char)*q))++q;
    if(q<text+end && *q!=';') {
      bool known=false;
      for(unsigned i=0;i<sizeof allowed/sizeof *allowed;++i) {
        size_t n=strlen(allowed[i]);
        if((size_t)(text+end-q)>n && !memcmp(q,allowed[i],n) && isspace((unsigned char)q[n]))known=true;
      }
      mx251_effects e=mx251InstructionEffects(text+start,end-start,text,length);
      if(!known || e.unknown) {*complete=false;return UINT32_MAX;}
      touch|=e.reads&~defined;defined|=e.writes;
    }
    start=end+1;
  }
  return touch|(inputs&~defined);
}

/* Only emitter-owned save/restore regions participate. Between regions SP
 * differs from the original, so allow register-only moves there. Once the
 * identical save layout is reached, SP and every argument/frame offset agree.
 * The next call's setup and ABI inputs must not read their former contents.
 * Keep text offsets intact: branch relaxation and later proposals own them. */
static wide_call_reuse_result
wide_reuse_call_saves(char *text,size_t length)
{
  wide_call_reuse_result result={0};
  size_t pops[32],pushes[32];uint32_t groups[32],held=0;
  unsigned state=0,n=0,p=0,popWeight=0,pushWeight=0,wordMultiply=0;
  for(size_t start=0;start<length;) {
    size_t end=start;while(end<length && text[end]!='\n')++end;
    char line[512];size_t size=end-start;
    if(size>=sizeof line) {state=0;start=end+1;continue;}
    memcpy(line,text+start,size);line[size]=0;
    /* A later fixed-allocation replacement owns its entire call region.
     * Do not tie its stack saves to a neighbouring call before it commits. */
    if(!strcmp(line,"; mx251-wordmul-begin"))wordMultiply=1;
    if(wordMultiply) {
      if(!strcmp(line,"; mx251-wordmul-end"))wordMultiply=0;
      state=0;start=end+1;continue;
    }
    if(!strncmp(line,"; rotate-",9) || !strncmp(line,"; reroll-",9))state=0;
    unsigned weight,mask,complete;
    if(sscanf(line,"; wide restore-begin weight=%u",&weight)==1) {
      state=1;n=p=0;held=0;popWeight=weight;
    } else if(state==1 && !strcmp(line,"; wide restore-end")) {
      state=n?2:0;
    } else if(state==2 && sscanf(line,"; wide save-begin weight=%u",&weight)==1) {
      state=3;pushWeight=weight;
    } else if(state==3 && !strcmp(line,"; wide save-end")) {
      state=p==n?4:0;
    } else if(state==4 && sscanf(line,"; wide call-inputs touch=%x complete=%u",&mask,&complete)==2) {
      if(complete && !(mask&held)) {
        for(unsigned i=0;i<n;++i) {text[pops[i]]=';';text[pushes[i]]=';';}
        result.instructions+=2*n;result.weighted+=n*(popWeight+pushWeight);++result.bridges;
      }
      state=0;
    } else if(state && state!=4) {
      const char *q=line;while(isspace((unsigned char)*q))++q;
      if(*q && *q!=';') {
        mx251_effects e=mx251InstructionEffects(line,size,line,size);
        if(state==1 && !strncmp(q,"pop\t",4) && !e.unknown && e.writes &&
           !(e.writes&(held|MCS251_AB_BYTES)) && n<32) {
          groups[n]=e.writes;pops[n++]=start;held|=e.writes;
        } else if(state==3 && !strncmp(q,"push\t",5) && !e.unknown && p<n &&
                  e.reads==groups[n-1-p]) {
          pushes[p++]=start;
        } else if(state==2 && !strncmp(q,"mov\t",4) && !e.unknown && !e.memory &&
                  !e.specialReads && !e.specialWrites && !((e.reads|e.writes)&held)) {
          /* Register moves do not observe the temporarily higher stack. */
        } else state=0;
      }
    }
    start=end+1;
  }
  return result;
}
#endif
