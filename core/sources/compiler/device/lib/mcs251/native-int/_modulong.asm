; /*-------------------------------------------------------------------------
;    _modulong.c - routine for modulus of 32 bit unsigned long
; 
;    Copyright (C) 1999, Sandeep Dutta . sandeep.dutta@usa.net
;    Bug fixes by Martijn van Balen, aed@iae.nl
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
.module native_modulong
.globl ___m251r4__modulong
.area CSEG (CODE)
___m251r4__modulong:
    mov dr0,dr4
    mov dr4,dr12
    cmp dr4,#0
    jne 00020$
    ejmp 00004$
00020$:
    cmp dr0,dr4
    jnc 00021$
    ejmp 00005$
00021$:
    cmp wr0,#0
    jne 00022$
    ejmp 00006$
00022$:
    cmp dr4,#1
    je 00009$
    mov dr12,dr0
    sub dr12,dr4
    cmp dr12,dr4
    jc 00010$
    mov wr18,#0
    cmp wr4,#0
    jne 00014$
    mov wr18,wr0
    div wr18,wr6
    mov wr12,#0
    mov wr14,wr16
    mov wr0,wr2
    mov wr2,#0
    mov b,#16
    sjmp 00001$
00014$:
    cmp r4,#0
    jne 00011$
    mov wr12,#0
    mov wr14,wr0
    mov wr0,wr2
    mov wr2,#0
    mov b,#16
    sjmp 00001$
00011$:
    mov r12,#0
    mov r13,r0
    mov r14,r1
    mov r15,r2
    mov r0,r3
    mov r1,#0
    mov wr2,#0
    mov b,#8
00001$:
    add dr0,dr0
    jc 00002$
    add dr12,dr12
    sjmp 00003$
00002$:
    add dr12,dr12
    inc dr12
00003$:
    cmp dr12,dr4
    jc 00007$
    sub dr12,dr4
    inc dr0
00007$:
    djnz b,00001$
    mov dr0,dr12
    sjmp 00008$
00009$:
    mov dr0,#0
    sjmp 00008$
00010$:
    mov dr0,dr12
    sjmp 00008$
00004$:
    sjmp 00008$
00005$:
    sjmp 00008$
00006$:
    div wr2,wr6
    mov wr2,wr0
    mov wr0,#0
00008$:
    mov dr4,dr0
    eret
