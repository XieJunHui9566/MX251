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
; DR16/20/24/28 是生成器既有调用者暂存区；输入和结果遵循 ABI4。
.module native_mullong
.globl ___m251r4__mullong
.area CSEG (CODE)
___m251r4__mullong:
    mov wr16,wr6
    mov wr20,wr14
    mul wr16,wr20
    mov wr20,wr4
    mov wr24,wr14
    mul wr20,wr24
    mov wr24,wr6
    mov wr28,wr12
    mul wr24,wr28
    add wr22,wr26
    add wr16,wr22
    mov dr4,dr16
    eret
