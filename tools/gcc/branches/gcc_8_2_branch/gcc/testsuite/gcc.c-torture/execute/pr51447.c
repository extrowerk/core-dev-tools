/* PR rtl-optimization/51447 */
/* { dg-require-effective-target label_values } */
/* { dg-require-effective-target indirect_jumps } */

extern void abort (void);

#if defined __x86_64__ && defined __QNXNTO__
register void *ptr asm ("rbx");
#else
void *ptr;
#endif

int
main (void)
{
  __label__ nonlocal_lab;
#if defined __x86_64__ && defined __QNXNTO__
  void *saved_rbx;
  asm volatile ("movq %%rbx, %0" : "=r" (saved_rbx) : : );
#endif
  __attribute__((noinline, noclone)) void
    bar (void *func)
      {
	ptr = func;
	goto nonlocal_lab;
      }
  bar (&&nonlocal_lab);
#if defined __x86_64__ && defined __QNXNTO__
  asm volatile ("movq %0, %%rbx" : : "r" (saved_rbx) : "rbx" );
#endif
  return 1;
nonlocal_lab:
  if (ptr != &&nonlocal_lab)
    abort ();
#if defined __x86_64__ && defined __QNXNTO__
  asm volatile ("movq %0, %%rbx" : : "r" (saved_rbx) : "rbx" );
#endif
  return 0;
}
