/*-
	QNX Debug Protocol, i386
 */

#ifndef TM_I386QNX_H
#define TM_I386QNX_H 1

#define HAVE_I387_REGS

/* Pick up most of what we need from the generic i386 target include file. */

#include "i386/tm-i386.h"
#include "tm-qnxnto.h"

#define __QNXTARGET__
#define QNX_TARGET_CPUTYPE CPUTYPE_X86

/* Used in solib.c */
#define EBX_REGNUM       3

#undef DECR_PC_AFTER_BREAK
#define DECR_PC_AFTER_BREAK 0 /* neutrino rewinds to look more normal */

/* Use the alternate method of determining valid frame chains. */
#define FRAME_CHAIN_VALID(fp,fi) func_frame_chain_valid (fp, fi)

/* Offsets (in target ints) into jmp_buf.  Not defined in any system header
   file, so we have to step through setjmp/longjmp with a debugger and figure
   them out.  Note that <setjmp> defines _JBLEN as 10, which is the default
   if no specific machine is selected, even though we only use 6 slots. */

#define JB_ELEMENT_SIZE sizeof(int)	/* jmp_buf[_JBLEN] is array of ints */

#define JB_EBX	0
#define JB_ESI	1
#define JB_EDI	2
#define JB_EBP	3
#define JB_ESP	4
#define JB_EDX	5

#define JB_PC	JB_EDX	/* Setjmp()'s return PC saved in EDX */

/* Figure out where the longjmp will land.  Slurp the args out of the stack.
   We expect the first arg to be a pointer to the jmp_buf structure from which
   we extract the pc (JB_PC) that we will land at.  The pc is copied into ADDR.
   This routine returns true on success */
#if 0
extern int
i386_get_longjmp_target PARAMS ((CORE_ADDR *));

#define GET_LONGJMP_TARGET(ADDR) i386_get_longjmp_target(ADDR)
#endif

/* The following redefines make backtracing through sigtramp work.
   They manufacture a fake sigtramp frame and obtain the saved pc in sigtramp
   from the ucontext structure which is pushed by the kernel on the
   user stack. Unfortunately there are three variants of sigtramp handlers.  */

#define I386V4_SIGTRAMP_SAVED_PC
#define IN_SIGTRAMP(pc, name) ((name)					\
			       && (STREQ ("_sigreturn", name)		\
				   || STREQ ("_sigacthandler", name)	\
				   || STREQ ("sigvechandler", name)))

/* Saved Pc.  Get it from ucontext if within sigtramp.  */

#define sigtramp_saved_pc i386v4_sigtramp_saved_pc
extern CORE_ADDR i386v4_sigtramp_saved_pc PARAMS ((struct frame_info *));

/* Neutrino supports the 386 hardware debugging registers.  */

#define TARGET_HAS_HARDWARE_WATCHPOINTS

#define TARGET_CAN_USE_HARDWARE_WATCHPOINT(type, cnt, ot) 1

/* After a watchpoint trap, the PC points to the instruction after
   the one that caused the trap.  Therefore we don't need to step over it.
   But we do need to reset the status register to avoid another trap.  */
#define HAVE_CONTINUABLE_WATCHPOINT

#define STOPPED_BY_WATCHPOINT(W)  \
  qnx_stopped_by_watchpoint()

/* Use these macros for watchpoint insertion/removal.  */

#define target_insert_watchpoint(addr, len, type)  \
  qnx_insert_hw_watchpoint ( addr, len, type)

#define target_remove_watchpoint(addr, len, type)  \
  qnx_remove_hw_watchpoint ( addr, len, type)

/* Use these macros for watchpoint insertion/removal.  */

#define target_remove_hw_breakpoint(ADDR,SHADOW) \
  qnx_remove_hw_breakpoint(ADDR,SHADOW)

#define target_insert_hw_breakpoint(ADDR,SHADOW) \
  qnx_insert_hw_breakpoint(ADDR,SHADOW)

#undef target_pid_to_str
#define target_pid_to_str(PID) \
	qnx_pid_to_str(PID)
extern char *qnx_pid_to_str PARAMS ((ptid_t ptid));

#define FIND_NEW_THREADS qnx_find_new_threads
void qnx_find_new_threads PARAMS ((void));

/* default processor for search path for libs */
#define SOLIB_PROCESSOR "x86"

/* Use target_specific function to define link map offsets.  */
extern struct link_map_offsets *i386_qnx_svr4_fetch_link_map_offsets (void);
#define SVR4_FETCH_LINK_MAP_OFFSETS() i386_qnx_svr4_fetch_link_map_offsets ()

#define HANDLE_SVR4_EXEC_EMULATORS 1
#include "solib.h"		/* Support for shared libraries. */

#define NO_PTRACE_H

/* Use .ntox86-gdbinit */
#define EXTRA_GDBINIT_FILENAME ".ntox86-gdbinit"

#endif  /* ifndef TM_I386QNX_H */
