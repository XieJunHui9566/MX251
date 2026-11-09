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
; 三个 16x16 部分积，只有交叉积低 16 位影响模 2^32 的结果。
; 输入和结果遵循 ABI4；只占用 DR0、DR4、WR12，保留高位寄存器。
.module native_mullong
.globl ___m251r4__mullong
.area CSEG (CODE)
___m251r4__mullong:
    mov wr0,wr4
    mul wr0,wr14
    mov wr0,wr6
    mul wr6,wr12
    add wr6,wr2
    mov wr12,wr6
    mul wr0,wr14
    add wr0,wr12
    mov dr4,dr0
    eret
