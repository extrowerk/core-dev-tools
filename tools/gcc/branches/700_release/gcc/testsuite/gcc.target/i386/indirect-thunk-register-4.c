/* { dg-do compile } */
/* { dg-options "-O2 -mindirect-branch=keep -fno-pic" } */

extern void (*func_p) (void);

void
foo (void)
{
  asm("call __x86_indirect_thunk_%V0" : : "a" (func_p));
}

/* { dg-final { scan-assembler "call\[ \t\]*__x86_indirect_thunk_ax" { target ia32 } } } */
/* { dg-final { scan-assembler "call\[ \t\]*__x86_indirect_thunk_ax" { target { ! ia32 } } } } */
