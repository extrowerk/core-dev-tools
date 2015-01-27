/* { dg-do compile } */
/* { dg-options "-O2 -fdump-tree-optimized" } */
/* { dg-options "-O2 -fdump-tree-optimized -march=i686" { target { { i?86-*-* x86_64-*-* } && ia32 } } } */

int
foo (int a, int b, int c)
{
  return ((a && !b && c) || (!a && b && c));
}

/* { dg-final { scan-tree-dump-times "\\\^" 1 "optimized" { xfail logical_op_short_circuit } } } */
/* { dg-final { cleanup-tree-dump "optimized" } } */
