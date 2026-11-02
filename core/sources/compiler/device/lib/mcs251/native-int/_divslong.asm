; /*-------------------------------------------------------------------------
;    _mullong.c - routine for multiplication of 32 bit (unsigned) long
; 
;    Copyright (C) 1999, Sandeep Dutta . sandeep.dutta@usa.net
;    Copyright (C) 1999, Jean Louis VERN jlvern@writeme.com
; 
;    This library is free software; you can redistribute it and/or modify it
;    under the terms of the GNU General Public License as published by the
;    Free Software Foundation; either version 2, or (at your option) any
;    later version.
; 
;    This library is distributed in the hope that it will be useful,
;    but WITHOUT ANY WARRANTY; without even the implied warranty of
;    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;    GNU General Public License for more details.
; 
;    You should have received a copy of the GNU General Public License
;    along with this library; see the file COPYING. If not, write to the
;    Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
;    MA 02110-1301, USA.
; 
;    As a special exception, if you link this library with other files,
;    some of which are compiled with SDCC, to produce an executable,
;    this library does not by itself cause the resulting executable to
;    be covered by the GNU General Public License. This exception does
;    not however invalidate any other reasons why the executable file
;    might be covered by the GNU General Public License.
; -------------------------------------------------------------------------*/
; 
; 无符号模相反数可处理 INT32_MIN；符号位仅保存在本次调用的栈上。
; /0、%0 与 INT32_MIN/-1 的 C 表达式仍没有语言层保证。
.module native_divslong
.globl ___m251r4__divslong
.globl ___m251r4__divulong
.area CSEG (CODE)
___m251r4__divslong:
    mov a,r4
    xrl a,r12
    push acc
    mov a,r4
    jnb acc.7,00001$
    mov dr0,#0
    sub dr0,dr4
    mov dr4,dr0
00001$:
    mov a,r12
    jnb acc.7,00002$
    mov dr0,#0
    sub dr0,dr12
    mov dr12,dr0
00002$:
    ecall ___m251r4__divulong
    pop acc
    jnb acc.7,00003$
    mov dr0,#0
    sub dr0,dr4
    mov dr4,dr0
00003$:
    eret
