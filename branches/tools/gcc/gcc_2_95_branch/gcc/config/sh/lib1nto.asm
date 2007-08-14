/*-
	lib1funcs.asm: %s/___/__/g
 */
/* Copyright (C) 1994, 1995, 1997, 1998 Free Software Foundation, Inc.

This file is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file with other programs, and to distribute
those programs without any restriction coming from the use of this
file.  (The General Public License restrictions do apply in other
respects; for example, they cover modification of the file, and
distribution when not linked into another program.)

This file is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.  */


!! libgcc1 routines for the Hitachi SH cpu.
!! Contributed by Steve Chamberlain.
!! sac@cygnus.com

!! ashiftrt_r4_x, __ashrsi3, __ashlsi3, __lshrsi3 routines
!! recoded in assembly by Toshiyasu Morita
!! tm@netcom.com

/* SH2 optimizations for __ashrsi3, __ashlsi3, __lshrsi3 and
   ELF local label prefixes by J"orn Rennecke
   amylaar@cygnus.com  */

#ifdef __ELF__
#define LOCAL(X) .L_##X
#else
#define LOCAL(X) L_##X
#endif

#ifdef __QNXNTO__
#define FPSCR_VALUES_SP 0x40000
#define FPSCR_VALUES_DP 0xc0000
#endif

#ifdef L_ashiftrt
	.global	__ashiftrt_r4_0
	.type __ashiftrt_r4_0,@function
	.global	__ashiftrt_r4_1
	.type __ashiftrt_r4_1,@function
	.global	__ashiftrt_r4_2
	.type __ashiftrt_r4_2,@function
	.global	__ashiftrt_r4_3
	.type __ashiftrt_r4_3,@function
	.global	__ashiftrt_r4_4
	.type __ashiftrt_r4_4,@function
	.global	__ashiftrt_r4_5
	.type __ashiftrt_r4_5,@function
	.global	__ashiftrt_r4_6
	.type __ashiftrt_r4_6,@function
	.global	__ashiftrt_r4_7
	.type __ashiftrt_r4_7,@function
	.global	__ashiftrt_r4_8
	.type __ashiftrt_r4_8,@function
	.global	__ashiftrt_r4_9
	.type __ashiftrt_r4_9,@function
	.global	__ashiftrt_r4_10
	.type __ashiftrt_r4_10,@function
	.global	__ashiftrt_r4_11
	.type __ashiftrt_r4_11,@function
	.global	__ashiftrt_r4_12
	.type __ashiftrt_r4_12,@function
	.global	__ashiftrt_r4_13
	.type __ashiftrt_r4_13,@function
	.global	__ashiftrt_r4_14
	.type __ashiftrt_r4_14,@function
	.global	__ashiftrt_r4_15
	.type __ashiftrt_r4_15,@function
	.global	__ashiftrt_r4_16
	.type __ashiftrt_r4_16,@function
	.global	__ashiftrt_r4_17
	.type __ashiftrt_r4_17,@function
	.global	__ashiftrt_r4_18
	.type __ashiftrt_r4_18,@function
	.global	__ashiftrt_r4_19
	.type __ashiftrt_r4_19,@function
	.global	__ashiftrt_r4_20
	.type __ashiftrt_r4_20,@function
	.global	__ashiftrt_r4_21
	.type __ashiftrt_r4_21,@function
	.global	__ashiftrt_r4_22
	.type __ashiftrt_r4_22,@function
	.global	__ashiftrt_r4_23
	.type __ashiftrt_r4_23,@function
	.global	__ashiftrt_r4_24
	.type __ashiftrt_r4_24,@function
	.global	__ashiftrt_r4_25
	.type __ashiftrt_r4_25,@function
	.global	__ashiftrt_r4_26
	.type __ashiftrt_r4_26,@function
	.global	__ashiftrt_r4_27
	.type __ashiftrt_r4_27,@function
	.global	__ashiftrt_r4_28
	.type __ashiftrt_r4_28,@function
	.global	__ashiftrt_r4_29
	.type __ashiftrt_r4_29,@function
	.global	__ashiftrt_r4_30
	.type __ashiftrt_r4_30,@function
	.global	__ashiftrt_r4_31
	.type __ashiftrt_r4_31,@function
	.global	__ashiftrt_r4_32
	.type __ashiftrt_r4_32,@function

	.align	1
__ashiftrt_r4_32:
__ashiftrt_r4_31:
	rotcl	r4
	rts
	subc	r4,r4

__ashiftrt_r4_30:
	shar	r4
__ashiftrt_r4_29:
	shar	r4
__ashiftrt_r4_28:
	shar	r4
__ashiftrt_r4_27:
	shar	r4
__ashiftrt_r4_26:
	shar	r4
__ashiftrt_r4_25:
	shar	r4
__ashiftrt_r4_24:
	shlr16	r4
	shlr8	r4
	rts
	exts.b	r4,r4

__ashiftrt_r4_23:
	shar	r4
__ashiftrt_r4_22:
	shar	r4
__ashiftrt_r4_21:
	shar	r4
__ashiftrt_r4_20:
	shar	r4
__ashiftrt_r4_19:
	shar	r4
__ashiftrt_r4_18:
	shar	r4
__ashiftrt_r4_17:
	shar	r4
__ashiftrt_r4_16:
	shlr16	r4
	rts
	exts.w	r4,r4

__ashiftrt_r4_15:
	shar	r4
__ashiftrt_r4_14:
	shar	r4
__ashiftrt_r4_13:
	shar	r4
__ashiftrt_r4_12:
	shar	r4
__ashiftrt_r4_11:
	shar	r4
__ashiftrt_r4_10:
	shar	r4
__ashiftrt_r4_9:
	shar	r4
__ashiftrt_r4_8:
	shar	r4
__ashiftrt_r4_7:
	shar	r4
__ashiftrt_r4_6:
	shar	r4
__ashiftrt_r4_5:
	shar	r4
__ashiftrt_r4_4:
	shar	r4
__ashiftrt_r4_3:
	shar	r4
__ashiftrt_r4_2:
	shar	r4
__ashiftrt_r4_1:
	rts
	shar	r4

__ashiftrt_r4_0:
	rts
	nop
#endif

#ifdef L_ashiftrt_n

!
! __ashrsi3
!
! Entry:
!
! r4: Value to shift
! r5: Shifts
!
! Exit:
!
! r0: Result
!
! Destroys:
!
! (none)
!

	.global	__ashrsi3
	.align	2
__ashrsi3:
	mov	#31,r0
	and	r0,r5
	mova	LOCAL(ashrsi3_table),r0
	mov.b	@(r0,r5),r5
#ifdef __sh1__
	add	r5,r0
	jmp	@r0
#else
	braf	r5
#endif
	mov	r4,r0

	.align	2
LOCAL(ashrsi3_table):
	.byte		LOCAL(ashrsi3_0)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_1)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_2)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_3)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_4)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_5)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_6)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_7)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_8)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_9)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_10)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_11)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_12)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_13)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_14)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_15)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_16)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_17)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_18)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_19)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_20)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_21)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_22)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_23)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_24)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_25)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_26)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_27)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_28)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_29)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_30)-LOCAL(ashrsi3_table)
	.byte		LOCAL(ashrsi3_31)-LOCAL(ashrsi3_table)

LOCAL(ashrsi3_31):
	rotcl	r0
	rts
	subc	r0,r0

LOCAL(ashrsi3_30):
	shar	r0
LOCAL(ashrsi3_29):
	shar	r0
LOCAL(ashrsi3_28):
	shar	r0
LOCAL(ashrsi3_27):
	shar	r0
LOCAL(ashrsi3_26):
	shar	r0
LOCAL(ashrsi3_25):
	shar	r0
LOCAL(ashrsi3_24):
	shlr16	r0
	shlr8	r0
	rts
	exts.b	r0,r0

LOCAL(ashrsi3_23):
	shar	r0
LOCAL(ashrsi3_22):
	shar	r0
LOCAL(ashrsi3_21):
	shar	r0
LOCAL(ashrsi3_20):
	shar	r0
LOCAL(ashrsi3_19):
	shar	r0
LOCAL(ashrsi3_18):
	shar	r0
LOCAL(ashrsi3_17):
	shar	r0
LOCAL(ashrsi3_16):
	shlr16	r0
	rts
	exts.w	r0,r0

LOCAL(ashrsi3_15):
	shar	r0
LOCAL(ashrsi3_14):
	shar	r0
LOCAL(ashrsi3_13):
	shar	r0
LOCAL(ashrsi3_12):
	shar	r0
LOCAL(ashrsi3_11):
	shar	r0
LOCAL(ashrsi3_10):
	shar	r0
LOCAL(ashrsi3_9):
	shar	r0
LOCAL(ashrsi3_8):
	shar	r0
LOCAL(ashrsi3_7):
	shar	r0
LOCAL(ashrsi3_6):
	shar	r0
LOCAL(ashrsi3_5):
	shar	r0
LOCAL(ashrsi3_4):
	shar	r0
LOCAL(ashrsi3_3):
	shar	r0
LOCAL(ashrsi3_2):
	shar	r0
LOCAL(ashrsi3_1):
	rts
	shar	r0

LOCAL(ashrsi3_0):
	rts
	nop

	.type __ashrsi3,@function
	.size __ashrsi3,.-__ashrsi3

#endif

#ifdef L_ashiftlt

!
! __ashlsi3
!
! Entry:
!
! r4: Value to shift
! r5: Shifts
!
! Exit:
!
! r0: Result
!
! Destroys:
!
! (none)
!
	.global	__ashlsi3
	.align	2
__ashlsi3:
	mov	#31,r0
	and	r0,r5
	mova	LOCAL(ashlsi3_table),r0
	mov.b	@(r0,r5),r5
#ifdef __sh1__
	add	r5,r0
	jmp	@r0
#else
	braf	r5
#endif
	mov	r4,r0

	.align	2
LOCAL(ashlsi3_table):
	.byte		LOCAL(ashlsi3_0)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_1)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_2)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_3)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_4)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_5)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_6)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_7)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_8)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_9)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_10)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_11)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_12)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_13)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_14)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_15)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_16)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_17)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_18)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_19)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_20)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_21)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_22)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_23)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_24)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_25)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_26)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_27)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_28)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_29)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_30)-LOCAL(ashlsi3_table)
	.byte		LOCAL(ashlsi3_31)-LOCAL(ashlsi3_table)

LOCAL(ashlsi3_6):
	shll2	r0
LOCAL(ashlsi3_4):
	shll2	r0
LOCAL(ashlsi3_2):
	rts
	shll2	r0

LOCAL(ashlsi3_7):
	shll2	r0
LOCAL(ashlsi3_5):
	shll2	r0
LOCAL(ashlsi3_3):
	shll2	r0
LOCAL(ashlsi3_1):
	rts
	shll	r0

LOCAL(ashlsi3_14):
	shll2	r0
LOCAL(ashlsi3_12):
	shll2	r0
LOCAL(ashlsi3_10):
	shll2	r0
LOCAL(ashlsi3_8):
	rts
	shll8	r0

LOCAL(ashlsi3_15):
	shll2	r0
LOCAL(ashlsi3_13):
	shll2	r0
LOCAL(ashlsi3_11):
	shll2	r0
LOCAL(ashlsi3_9):
	shll8	r0
	rts
	shll	r0

LOCAL(ashlsi3_22):
	shll2	r0
LOCAL(ashlsi3_20):
	shll2	r0
LOCAL(ashlsi3_18):
	shll2	r0
LOCAL(ashlsi3_16):
	rts
	shll16	r0

LOCAL(ashlsi3_23):
	shll2	r0
LOCAL(ashlsi3_21):
	shll2	r0
LOCAL(ashlsi3_19):
	shll2	r0
LOCAL(ashlsi3_17):
	shll16	r0
	rts
	shll	r0

LOCAL(ashlsi3_30):
	shll2	r0
LOCAL(ashlsi3_28):
	shll2	r0
LOCAL(ashlsi3_26):
	shll2	r0
LOCAL(ashlsi3_24):
	shll16	r0
	rts
	shll8	r0

LOCAL(ashlsi3_31):
	shll2	r0
LOCAL(ashlsi3_29):
	shll2	r0
LOCAL(ashlsi3_27):
	shll2	r0
LOCAL(ashlsi3_25):
	shll16	r0
	shll8	r0
	rts
	shll	r0

LOCAL(ashlsi3_0):
	rts
	nop

	.type __ashlsi3,@function
	.size __ashlsi3,.-__ashlsi3

#endif

#ifdef L_lshiftrt

!
! __lshrsi3
!
! Entry:
!
! r4: Value to shift
! r5: Shifts
!
! Exit:
!
! r0: Result
!
! Destroys:
!
! (none)
!
	.global	__lshrsi3
	.align	2
__lshrsi3:
	mov	#31,r0
	and	r0,r5
	mova	LOCAL(lshrsi3_table),r0
	mov.b	@(r0,r5),r5
#ifdef __sh1__
	add	r5,r0
	jmp	@r0
#else
	braf	r5
#endif
	mov	r4,r0

	.align	2
LOCAL(lshrsi3_table):
	.byte		LOCAL(lshrsi3_0)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_1)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_2)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_3)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_4)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_5)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_6)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_7)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_8)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_9)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_10)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_11)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_12)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_13)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_14)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_15)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_16)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_17)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_18)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_19)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_20)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_21)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_22)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_23)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_24)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_25)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_26)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_27)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_28)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_29)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_30)-LOCAL(lshrsi3_table)
	.byte		LOCAL(lshrsi3_31)-LOCAL(lshrsi3_table)

LOCAL(lshrsi3_6):
	shlr2	r0
LOCAL(lshrsi3_4):
	shlr2	r0
LOCAL(lshrsi3_2):
	rts
	shlr2	r0

LOCAL(lshrsi3_7):
	shlr2	r0
LOCAL(lshrsi3_5):
	shlr2	r0
LOCAL(lshrsi3_3):
	shlr2	r0
LOCAL(lshrsi3_1):
	rts
	shlr	r0

LOCAL(lshrsi3_14):
	shlr2	r0
LOCAL(lshrsi3_12):
	shlr2	r0
LOCAL(lshrsi3_10):
	shlr2	r0
LOCAL(lshrsi3_8):
	rts
	shlr8	r0

LOCAL(lshrsi3_15):
	shlr2	r0
LOCAL(lshrsi3_13):
	shlr2	r0
LOCAL(lshrsi3_11):
	shlr2	r0
LOCAL(lshrsi3_9):
	shlr8	r0
	rts
	shlr	r0

LOCAL(lshrsi3_22):
	shlr2	r0
LOCAL(lshrsi3_20):
	shlr2	r0
LOCAL(lshrsi3_18):
	shlr2	r0
LOCAL(lshrsi3_16):
	rts
	shlr16	r0

LOCAL(lshrsi3_23):
	shlr2	r0
LOCAL(lshrsi3_21):
	shlr2	r0
LOCAL(lshrsi3_19):
	shlr2	r0
LOCAL(lshrsi3_17):
	shlr16	r0
	rts
	shlr	r0

LOCAL(lshrsi3_30):
	shlr2	r0
LOCAL(lshrsi3_28):
	shlr2	r0
LOCAL(lshrsi3_26):
	shlr2	r0
LOCAL(lshrsi3_24):
	shlr16	r0
	rts
	shlr8	r0

LOCAL(lshrsi3_31):
	shlr2	r0
LOCAL(lshrsi3_29):
	shlr2	r0
LOCAL(lshrsi3_27):
	shlr2	r0
LOCAL(lshrsi3_25):
	shlr16	r0
	shlr8	r0
	rts
	shlr	r0

LOCAL(lshrsi3_0):
	rts
	nop

	.type __lshrsi3,@function
	.size __lshrsi3,.-__lshrsi3

#endif

#ifdef L_movstr
	.text
! done all the large groups, do the remainder

! jump to movstr+
done:
	add	#64,r5
	mova	__movstrSI0,r0
	shll2	r6
	add	r6,r0
	jmp	@r0
	add	#64,r4
	.align	4
	.global	__movstrSI64
	.type __movstrSI64,@function
__movstrSI64:
	mov.l	@(60,r5),r0
	mov.l	r0,@(60,r4)
	.global	__movstrSI60
	.type __movstrSI60,@function
__movstrSI60:
	mov.l	@(56,r5),r0
	mov.l	r0,@(56,r4)
	.global	__movstrSI56
	.type __movstrSI56,@function
__movstrSI56:
	mov.l	@(52,r5),r0
	mov.l	r0,@(52,r4)
	.global	__movstrSI52
	.type __movstrSI52,@function
__movstrSI52:
	mov.l	@(48,r5),r0
	mov.l	r0,@(48,r4)
	.global	__movstrSI48
	.type __movstrSI48,@function
__movstrSI48:
	mov.l	@(44,r5),r0
	mov.l	r0,@(44,r4)
	.global	__movstrSI44
	.type __movstrSI44,@function
__movstrSI44:
	mov.l	@(40,r5),r0
	mov.l	r0,@(40,r4)
	.global	__movstrSI40
	.type __movstrSI40,@function
__movstrSI40:
	mov.l	@(36,r5),r0
	mov.l	r0,@(36,r4)
	.global	__movstrSI36
	.type __movstrSI36,@function
__movstrSI36:
	mov.l	@(32,r5),r0
	mov.l	r0,@(32,r4)
	.global	__movstrSI32
	.type __movstrSI32,@function
__movstrSI32:
	mov.l	@(28,r5),r0
	mov.l	r0,@(28,r4)
	.global	__movstrSI28
	.type __movstrSI28,@function
__movstrSI28:
	mov.l	@(24,r5),r0
	mov.l	r0,@(24,r4)
	.global	__movstrSI24
	.type __movstrSI24,@function
__movstrSI24:
	mov.l	@(20,r5),r0
	mov.l	r0,@(20,r4)
	.global	__movstrSI20
	.type __movstrSI20,@function
__movstrSI20:
	mov.l	@(16,r5),r0
	mov.l	r0,@(16,r4)
	.global	__movstrSI16
	.type __movstrSI16,@function
__movstrSI16:
	mov.l	@(12,r5),r0
	mov.l	r0,@(12,r4)
	.global	__movstrSI12
	.type __movstrSI12,@function
__movstrSI12:
	mov.l	@(8,r5),r0
	mov.l	r0,@(8,r4)
	.global	__movstrSI8
	.type __movstrSI8,@function
__movstrSI8:
	mov.l	@(4,r5),r0
	mov.l	r0,@(4,r4)
	.global	__movstrSI4
	.type __movstrSI4,@function
__movstrSI4:
	mov.l	@(0,r5),r0
	mov.l	r0,@(0,r4)
__movstrSI0:
	rts
	nop
#	or	r0,r0,r0

	.align	4

	.global	__movstr
__movstr:
	mov.l	@(60,r5),r0
	mov.l	r0,@(60,r4)

	mov.l	@(56,r5),r0
	mov.l	r0,@(56,r4)

	mov.l	@(52,r5),r0
	mov.l	r0,@(52,r4)

	mov.l	@(48,r5),r0
	mov.l	r0,@(48,r4)

	mov.l	@(44,r5),r0
	mov.l	r0,@(44,r4)

	mov.l	@(40,r5),r0
	mov.l	r0,@(40,r4)

	mov.l	@(36,r5),r0
	mov.l	r0,@(36,r4)

	mov.l	@(32,r5),r0
	mov.l	r0,@(32,r4)

	mov.l	@(28,r5),r0
	mov.l	r0,@(28,r4)

	mov.l	@(24,r5),r0
	mov.l	r0,@(24,r4)

	mov.l	@(20,r5),r0
	mov.l	r0,@(20,r4)

	mov.l	@(16,r5),r0
	mov.l	r0,@(16,r4)

	mov.l	@(12,r5),r0
	mov.l	r0,@(12,r4)

	mov.l	@(8,r5),r0
	mov.l	r0,@(8,r4)

	mov.l	@(4,r5),r0
	mov.l	r0,@(4,r4)

	mov.l	@(0,r5),r0
	mov.l	r0,@(0,r4)

	add	#-16,r6
	cmp/pl	r6
	bf	done

	add	#64,r5
	bra	__movstr
	add	#64,r4
	.type __movstr,@function
	.size __movstr,.-__movstr
#endif

#ifdef L_movstr_i4
#if defined(__SH4__) || defined(__SH4_SINGLE__) || defined(__SH4_SINGLE_ONLY__)
	.text
	.global	__movstr_i4_even
	.type __movstr_i4_even,@function
	.global	__movstr_i4_odd
	.type __movstr_i4_odd,@function
	.global	__movstrSI12_i4
	.type __movstrSI12_i4,@function

	.p2align	5
L_movstr_2mod4_end:
	mov.l	r0,@(16,r4)
	rts
	mov.l	r1,@(20,r4)

	.p2align	2

__movstr_i4_odd:
	mov.l	@r5+,r1
	add	#-4,r4
	mov.l	@r5+,r2
	mov.l	@r5+,r3
	mov.l	r1,@(4,r4)
	mov.l	r2,@(8,r4)

L_movstr_loop:
	mov.l	r3,@(12,r4)
	dt	r6
	mov.l	@r5+,r0
	bt/s	L_movstr_2mod4_end
	mov.l	@r5+,r1
	add	#16,r4
L_movstr_start_even:
	mov.l	@r5+,r2
	mov.l	@r5+,r3
	mov.l	r0,@r4
	dt	r6
	mov.l	r1,@(4,r4)
	bf/s	L_movstr_loop
	mov.l	r2,@(8,r4)
	rts
	mov.l	r3,@(12,r4)

__movstr_i4_even:
	mov.l	@r5+,r0
	bra	L_movstr_start_even
	mov.l	@r5+,r1

	.p2align	4
__movstrSI12_i4:
	mov.l	@r5,r0
	mov.l	@(4,r5),r1
	mov.l	@(8,r5),r2
	mov.l	r0,@r4
	mov.l	r1,@(4,r4)
	rts
	mov.l	r2,@(8,r4)
#endif /* ! __SH4__ */
#endif

#ifdef L_mulsi3


	.global	__mulsi3

! r4 =       aabb
! r5 =       ccdd
! r0 = aabb*ccdd  via partial products
!
! if aa == 0 and cc = 0
! r0 = bb*dd
!
! else
! aa = bb*dd + (aa*dd*65536) + (cc*bb*65536)
!

__mulsi3:
	mulu    r4,r5		! multiply the lsws  macl=bb*dd
	mov     r5,r3		! r3 = ccdd
	swap.w  r4,r2		! r2 = bbaa
	xtrct   r2,r3		! r3 = aacc
	tst  	r3,r3		! msws zero ?
	bf      hiset
	rts			! yes - then we have the answer
	sts     macl,r0

hiset:	sts	macl,r0		! r0 = bb*dd
	mulu	r2,r5		! brewing macl = aa*dd
	sts	macl,r1
	mulu	r3,r4		! brewing macl = cc*bb
	sts	macl,r2
	add	r1,r2
	shll16	r2
	rts
	add	r2,r0

	.type __mulsi3,@function
	.size __mulsi3,.-__mulsi3


#endif
#ifdef L_sdivsi3_i4
	.title "SH DIVIDE"
!! 4 byte integer Divide code for the Hitachi SH
#ifdef __SH4__
!! args in r4 and r5, result in fpul, clobber dr0, dr2

	.global	__sdivsi3_i4
__sdivsi3_i4:
	lds r4,fpul
	float fpul,dr0
	lds r5,fpul
	float fpul,dr2
	fdiv dr2,dr0
	rts
	ftrc dr0,fpul
	.type __sdivsi3_i4,@function
	.size __sdivsi3_i4,.-__sdivsi3_i4

#elif defined(__SH4_SINGLE__) || defined(__SH4_SINGLE_ONLY__)
!! args in r4 and r5, result in fpul, clobber r2, dr0, dr2

	.global	__sdivsi3_i4
__sdivsi3_i4:
	sts.l fpscr,@-r15
	mov #8,r2
	swap.w r2,r2
	lds r2,fpscr
	lds r4,fpul
	float fpul,dr0
	lds r5,fpul
	float fpul,dr2
	fdiv dr2,dr0
	ftrc dr0,fpul
	rts
	lds.l @r15+,fpscr

	.type __sdivsi3_i4,@function
	.size __sdivsi3_i4,.-__sdivsi3_i4

#endif /* ! __SH4__ */
#endif

#ifdef L_sdivsi3
/* __SH4_SINGLE_ONLY__ keeps this part for link compatibility with
   sh3e code.  */
#if ! defined(__SH4__) && ! defined (__SH4_SINGLE__)
!!
!! Steve Chamberlain
!! sac@cygnus.com
!!
!!

!! args in r4 and r5, result in r0 clobber r1,r2,r3

	.global	__sdivsi3
__sdivsi3:
	mov	r4,r1
	mov	r5,r0

	tst	r0,r0
	bt	div0
	mov	#0,r2
	div0s	r2,r1
	subc	r3,r3
	subc	r2,r1
	div0s	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	div1	r0,r3
	rotcl	r1
	addc	r2,r1
	rts
	mov	r1,r0


div0:	rts
	mov	#0,r0

	.type __sdivsi3,@function
	.size __sdivsi3,.-__sdivsi3

#endif /* ! __SH4__ */
#endif
#ifdef L_udivsi3_i4

	.title "SH DIVIDE"
!! 4 byte integer Divide code for the Hitachi SH
#ifdef __SH4__
!! args in r4 and r5, result in fpul, clobber r0, r1, r4, r5, dr0, dr2, dr4

	.global	__udivsi3_i4
__udivsi3_i4:
	mov #1,r1
	cmp/hi r1,r5
	bf trivial
	rotr r1
	xor r1,r4
	lds r4,fpul
	mova L1,r0
#ifdef FMOVD_WORKS
	fmov.d @r0+,dr4
#else
#ifdef __LITTLE_ENDIAN__
	fmov.s @r0+,fr5
	fmov.s @r0,fr4
#else
	fmov.s @r0+,fr4
	fmov.s @r0,fr5
#endif
#endif
	float fpul,dr0
	xor r1,r5
	lds r5,fpul
	float fpul,dr2
	fadd dr4,dr0
	fadd dr4,dr2
	fdiv dr2,dr0
	rts
	ftrc dr0,fpul

trivial:
	rts
	lds r4,fpul

	.align 2
L1:
	.double 2147483648
	.type __udivsi3_i4,@function
	.size __udivsi3_i4,.-__udivsi3_i4

#elif defined(__SH4_SINGLE__) || defined(__SH4_SINGLE_ONLY__)
!! args in r4 and r5, result in fpul, clobber r0, r1, r4, r5, dr0, dr2, dr4

	.global	__udivsi3_i4
__udivsi3_i4:
	mov #1,r1
	cmp/hi r1,r5
	bf trivial
	sts.l fpscr,@-r15
	mova L1,r0
	lds.l @r0+,fpscr
	rotr r1
	xor r1,r4
	lds r4,fpul
#ifdef FMOVD_WORKS
	fmov.d @r0+,dr4
#else
#ifdef __LITTLE_ENDIAN__
	fmov.s @r0+,fr5
	fmov.s @r0,fr4
#else
	fmov.s @r0+,fr4
	fmov.s @r0,fr5
#endif
#endif
	float fpul,dr0
	xor r1,r5
	lds r5,fpul
	float fpul,dr2
	fadd dr4,dr0
	fadd dr4,dr2
	fdiv dr2,dr0
	ftrc dr0,fpul
	rts
	lds.l @r15+,fpscr

trivial:
	rts
	lds r4,fpul

	.align 2
L1:
#if defined (__LITTLE_ENDIAN__) || ! defined (FMOVD_WORKS)
	.long 0x80000
#else
	.long 0x180000
#endif
	.double 2147483648
	.type __udivsi3_i4,@function
	.size __udivsi3_i4,.-__udivsi3_i4

#endif /* ! __SH4__ */
#endif

#ifdef L_udivsi3
/* __SH4_SINGLE_ONLY__ keeps this part for link compatibility with
   sh3e code.  */
#if ! defined(__SH4__) && ! defined (__SH4_SINGLE__)
!!
!! Steve Chamberlain
!! sac@cygnus.com
!!
!!

!! args in r4 and r5, result in r0, clobbers r4, pr, and t bit
	.global	__udivsi3

__udivsi3:
longway:
	mov	#0,r0
	div0u
	! get one bit from the msb of the numerator into the T
	! bit and divide it by whats in r5.  Put the answer bit
	! into the T bit so it can come out again at the bottom

	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0

	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
shortway:
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0

vshortway:
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4 ; div1 r5,r0
	rotcl	r4
ret:	rts
	mov	r4,r0

	.type __udivsi3,@function
	.size __udivsi3,.-__udivsi3

#endif /* __SH4__ */
#endif
#ifdef L_set_fpscr
#if defined (__SH3E__) || defined(__SH4_SINGLE__) || defined(__SH4__) || defined(__SH4_SINGLE_ONLY__)
	.global __set_fpscr
__set_fpscr:
#ifdef __PIC__ /* handle shared libgcc.a */
	mov.l	r12,@-r15
	mov.l	r14,@-r15
	mova	got1,r0
	mov.l	got1,r12
	add	r0,r12
#endif
	lds r4,fpscr
#ifdef __PIC__
	mov.l __set_fpscr_L1,r0
	mov.l @(r0,r12),r1
#else
	mov.l __set_fpscr_L1,r1
#endif
	swap.w r4,r0
	or #24,r0
#ifndef FMOVD_WORKS
	xor #16,r0
#endif
#if defined(__SH4__)
	swap.w r0,r3
	mov.l r3,@(4,r1)
#else /* defined(__SH3E__) || defined(__SH4_SINGLE*__) */
	swap.w r0,r2
	mov.l r2,@r1
#endif
#ifndef FMOVD_WORKS
	xor #8,r0
#else
	xor #24,r0
#endif
#if defined(__SH4__)
	swap.w r0,r2
#ifdef __PIC__
	mov.l r2,@r1
	mov.l	@r15+,r14
	mov.l	@r15+,r12
	rts
	nop
#else
	rts
	mov.l r2,@r1
#endif
#else /* defined(__SH3E__) || defined(__SH4_SINGLE*__) */
	swap.w r0,r3
	rts
	mov.l r3,@(4,r1)
#endif
	.align 2
#ifdef __PIC__ /* handle shared libgcc.a */
got1:
	.long _GLOBAL_OFFSET_TABLE_
__set_fpscr_L1:
	.long __fpscr_values@GOT
#else
__set_fpscr_L1:
	.long __fpscr_values
#endif /* ! __PIC__ */
#if defined(FPSCR_VALUES_SP) && defined(FPSCR_VALUES_DP)
	.data
	.align 2
	.global __fpscr_values
	.type __fpscr_values,@object
	.size __fpscr_values,8
__fpscr_values:
	.long FPSCR_VALUES_SP
	.long FPSCR_VALUES_DP
	.previous
#else
#ifdef __ELF__
        .comm   __fpscr_values,8,4
#else
        .comm   __fpscr_values,8
#endif /* ELF */
#endif /* FPSCR_VALUES_SET */
	.type __set_fpscr,@function
	.size __set_fpscr,.-__set_fpscr
#endif /* SH3E / SH4 */
#endif /* L_set_fpscr */
