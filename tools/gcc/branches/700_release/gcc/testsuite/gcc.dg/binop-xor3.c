/* { dg-do compile } */
/* { dg-options "-O2 -fdump-tree-optimized" } */
/* { dg-options "-O2 -fdump-tree-optimized -march=i686" { target { { i?86-*-* x86_64-*-* } && ia32 } } } */

int
foo (int a, int b)
{
  return ((a && !b) || (!a && b));
}

/* { dg-final { scan-tree-dump-times "\\\^" 1 "optimized" { target i?86-*-* x86_64-*-* } } } */
/* { dg-final { cleanup-tree-dump "optimized" } } */
