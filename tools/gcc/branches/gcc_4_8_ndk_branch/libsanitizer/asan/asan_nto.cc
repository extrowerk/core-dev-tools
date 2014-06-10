//===-- asan_nto.cc -------------------------------------------------------===//
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Neutrino-specific details.
//===----------------------------------------------------------------------===//
#ifdef __QNXNTO__

#include "asan_interceptors.h"
#include "asan_internal.h"
#include "asan_thread.h"
#include "asan_thread_registry.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_procmaps.h"

#include <ucontext.h>

namespace __asan {

void MaybeReexec() {
}

void *AsanDoesNotSupportStaticLinkage() {
  return 0;
}

void GetPcSpBp(void *context, uptr *pc, uptr *sp, uptr *bp) {
#if defined(__arm__)
  ucontext_t *ucontext = (ucontext_t*)context;
  *pc = ucontext->uc_mcontext.cpu.gpr[ARM_REG_PC];
  *bp = ucontext->uc_mcontext.cpu.gpr[ARM_REG_FP];
  *sp = ucontext->uc_mcontext.cpu.gpr[ARM_REG_SP];
# elif defined(__i386__)
  ucontext_t *ucontext = (ucontext_t*)context;
  *pc = ucontext->uc_mcontext.cpu.eip;
  *bp = ucontext->uc_mcontext.cpu.ebp;
  *sp = ucontext->uc_mcontext.cpu.esp;
#else
# error "Unsupported arch"
#endif
}

bool AsanInterceptsSignal(int signum) {
  return signum == SIGSEGV && flags()->handle_segv;
}

void AsanPlatformThreadInit() {
}

void GetStackTrace(StackTrace *stack, uptr max_s, uptr pc, uptr bp, bool fast) {
#if defined(__arm__)
  fast = false;
#endif

  if (!fast)
    return stack->SlowUnwindStack(pc, max_s);
  stack->size = 0;
  stack->trace[0] = pc;
  if (max_s > 1) {
    stack->max_size = max_s;
    if (!asan_inited) return;
    if (AsanThread *t = asanThreadRegistry().GetCurrent())
      stack->FastUnwindStack(pc, bp, t->stack_top(), t->stack_bottom());
  }
}

void ReadContextStack(void *context, uptr *stack, uptr *ssize) {
  UNIMPLEMENTED();
}

}  // namespace __asan

#endif  // __QNXNTO__
