/*-------------------------------------------------------------------------
   tancotf.c - Computes tan or cot of a 32-bit float as outlined in [1]

   Copyright (C) 2001, 2002, Jesus Calvino-Fraga, jesusc@ieee.org

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

/* [1] William James Cody and W.  M.  Waite.  _Software manual for the
   elementary functions_, Englewood Cliffs, N.J.:Prentice-Hall, 1980. */

/* Version 1.0 - Initial release */

/* 旧ABI2 float/bool入口独立归档，原调用者仍可按需拉入。
 * 原生sin/cos/tan路径直接调用ABI4位型主体，不借此改变旧符号约定。 */
#include <math.h>
#include <stdbool.h>
#include "_mcs251_math_bits.h"
#if !(defined(__SDCC_mcs251) && defined(__SDCC_MCS251_NATIVE_RUNTIME))
#error This compatibility member requires the native runtime recipe.
#endif

float tancotf(float x, bool iscotan)
{
    mcs251_math_value value;
    value.f=x;
    value.u=mcs251_tancot_bits(value.u,(unsigned char)iscotan);
    return value.f;
}
