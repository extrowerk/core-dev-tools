/* PR rtl-optimization/51447 */

extern void abort (void);

#ifdef __x86_64__
register void *ptr asm ("rbx");
#else
void *ptr;
#endif

int
main (void)
{
  __label__ nonlocal_lab;
#ifdef __x86_64__
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
#ifdef __x86_64__
  asm volatile ("movq %0, %%rbx" : : "r" (saved_rbx) : "rbx" );
#endif
  return 1;
nonlocal_lab:
  if (ptr != &&nonlocal_lab)
    abort ();
#ifdef __x86_64__
  asm volatile ("movq %0, %%rbx" : : "r" (saved_rbx) : "rbx" );
#endif
  return 0;
}
