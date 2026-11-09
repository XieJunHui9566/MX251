/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MX251_SYMBOL_IMMEDIATE_H
#define MX251_SYMBOL_IMMEDIATE_H
#include <ctype.h>
/* A linker-resolved low word has fixed encoding and reads no GPR. Parse the
 * emitter's complete #(symbol+decimal) spelling without joining whitespace. */
static inline int mx251SymbolImmediate(const char *p,const char *end)
{
  while(p<end && isspace((unsigned char)*p))++p;
  if(end-p<4 || *p++!='#' || *p++!='(')return 0;
  if(p==end || (!isalpha((unsigned char)*p) && *p!='_' && *p!='$' && *p!='.'))return 0;
  do {++p;} while(p<end && (isalnum((unsigned char)*p) || *p=='_' || *p=='$' || *p=='.'));
  if(p<end && (*p=='+' || *p=='-')) {
    ++p;if(p==end || !isdigit((unsigned char)*p))return 0;
    do {++p;} while(p<end && isdigit((unsigned char)*p));
  }
  if(p==end || *p++!=')')return 0;
  while(p<end && isspace((unsigned char)*p))++p;
  return p==end || *p==';';
}
#endif
