/* PR target/56564 */
/* { dg-do compile { target { fpic && lp64 } } } */
/* { dg-skip-if "No symbol interposition for PIC" { *-*-mingw* *-*-cygwin* *-*-darwin* } } */
/* { dg-options "-O3 -fpic -fdump-tree-optimized" } */

__thread struct S { long a, b; } s = { 5, 6 };
__thread char t[16] = { 7 };

int
foo (void)
{
  return ((__UINTPTR_TYPE__) &s) & 15;
}

/* For backwards compatibility we don't assume that t must
   be aligned to 16 bytes, but align it anyway.  */

int
bar (void)
{
  return ((__UINTPTR_TYPE__) &t[0]) & 15;
}

/* { dg-final { scan-tree-dump-times "&s" 1 "optimized" { target { ! tls_emulated } } } } */
/* { dg-final { scan-tree-dump-times "&t" 1 "optimized" { target { ! tls_emulated } } } } */
/* { dg-final { scan-tree-dump-times "__emutls_get_address.*&__emutls_v.s" 1 "optimized" { target { tls_emulated } } } } */
/* { dg-final { scan-tree-dump-times "__emutls_get_address.*&__emutls_v.t" 1 "optimized" { target { tls_emulated } } } } */
/* { dg-final { scan-tree-dump-times "return 0" 0 "optimized" } } */
/* { dg-final { scan-assembler-not ".align\[ \t]*16\[^:]*\[\n\r]s:" { target { *-*-linux* } } } } */
/* { dg-final { scan-assembler ".align\[ \t]*16\[^:]*\[\n\r]t:" { target { *-*-linux* } } } } */
/* { dg-final { cleanup-tree-dump "optimized" } } */
