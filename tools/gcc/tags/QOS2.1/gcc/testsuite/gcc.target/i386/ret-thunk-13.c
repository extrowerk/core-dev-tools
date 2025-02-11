/* { dg-do compile } */
/* { dg-options "-O2 -mno-indirect-branch-register -mfunction-return=keep -mindirect-branch=thunk-inline -fno-pic" } */

extern void (*bar) (void);
extern int foo (void) __attribute__ ((function_return("thunk")));

int
foo (void)
{
  bar ();
  return 0;
}

/* { dg-final { scan-assembler "jmp\[ \t\]*__x86_return_thunk" } } */
/* { dg-final { scan-assembler-times {\tpause} 2 } } */
/* { dg-final { scan-assembler-times {\tlfence} 2 } } */
/* { dg-final { scan-assembler "push(?:l|q)\[ \t\]*_?bar" { target { { ! x32 } && *-*-linux* } } } } */
/* { dg-final { scan-assembler-times "jmp\[ \t\]*\.LIND" 3 } } */
/* { dg-final { scan-assembler-times "call\[ \t\]*\.LIND" 3 } } */
/* { dg-final { scan-assembler-not "jmp\[ \t\]*__x86_indirect_thunk" } } */
/* { dg-final { scan-assembler-not "call\[ \t\]*__x86_indirect_thunk_ax" { target { x32 } }  } } */
/* { dg-final { scan-assembler-not "pushq\[ \t\]%rax" { target x32 } } } */
