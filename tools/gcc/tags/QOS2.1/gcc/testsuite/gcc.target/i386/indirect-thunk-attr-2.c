/* { dg-do compile } */
/* { dg-options "-O2 -mno-indirect-branch-register -mfunction-return=keep -fno-pic" } */

typedef void (*dispatch_t)(long offset);

dispatch_t dispatch[256];

__attribute__ ((indirect_branch("thunk")))
void
male_indirect_jump (long offset)
{
  dispatch[offset](offset);
}

/* { dg-final { scan-assembler "push(?:l|q)\[ \t\]*_?dispatch" { target { { ! x32 } && *-*-linux* } } } } */
/* { dg-final { scan-assembler "jmp\[ \t\]*__x86_indirect_thunk" { target { ! x32 } } } } */
/* { dg-final { scan-assembler "jmp\[ \t\]*__x86_indirect_thunk_ax" { target x32 } } } */
/* { dg-final { scan-assembler "jmp\[ \t\]*\.LIND" } } */
/* { dg-final { scan-assembler "call\[ \t\]*\.LIND" } } */
/* { dg-final { scan-assembler {\tpause} } } */
/* { dg-final { scan-assembler {\tlfence} } } */
