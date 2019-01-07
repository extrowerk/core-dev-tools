/* { dg-do compile { target { i?86-*-* x86_64-*-* } } } */
/* { dg-options "-O2 -fdump-rtl-expand" } */
/* { dg-options "-O2 -fdump-rtl-expand -march=i686" { target { { i?86-*-* x86_64-*-* } && ia32 } } } */

int foo(float a, float b) {
  if (a == b)
    return 1;
  else
    return 2;
}

/* { dg-final { scan-rtl-dump-times "REG_BR_PROB 100" 1 "expand"} } */
/* { dg-final { cleanup-rtl-dump "expand" } } */
