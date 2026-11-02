/*-------------------------------------------------------------------------
   _mullong.c - routine for multiplication of 32 bit (unsigned) long

   Copyright (C) 1999, Sandeep Dutta . sandeep.dutta@usa.net
   Copyright (C) 1999, Jean Louis VERN jlvern@writeme.com

   This library is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this library; see the file COPYING. If not, write to the
   Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.

   As a special exception, if you link this library with other files,
   some of which are compiled with SDCC, to produce an executable,
   this library does not by itself cause the resulting executable to
   be covered by the GNU General Public License. This exception does
   not however invalidate any other reasons why the executable file
   might be covered by the GNU General Public License.
-------------------------------------------------------------------------*/

#include "native-int.h"
long _mullong(long a,long b) NATIVE_INT
{
  unsigned int al=(unsigned int)(unsigned long)a, bl=(unsigned int)(unsigned long)b;
  unsigned int ah=(unsigned int)((unsigned long)a>>16), bh=(unsigned int)((unsigned long)b>>16);
  unsigned long low=(unsigned long)al*(unsigned long)bl;
  unsigned long cross=(unsigned long)ah*(unsigned long)bl+(unsigned long)al*(unsigned long)bh;
  /* 无符号部分积保持模 2^32；无共享参数、静态局部或有符号溢出。 */
  return (long)(low+(cross<<16));
}
