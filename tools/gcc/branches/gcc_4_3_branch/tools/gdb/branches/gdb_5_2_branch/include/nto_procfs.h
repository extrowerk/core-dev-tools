#ifndef __DEBUG_H_INCLUDED
#define __DEBUG_H_INCLUDED

#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <nto_inttypes.h>

#define QNX_NOTE_NAME	"QNX"

enum Elf_qnx_note_types {
	QNT_NULL = 0,
	QNT_DEBUG_FULLPATH,
	QNT_DEBUG_RELOC,
	QNT_STACK,
	QNT_GENERATOR,
	QNT_DEFAULT_LIB,
	QNT_CORE_SYSINFO,
	QNT_CORE_INFO,
	QNT_CORE_STATUS,
	QNT_CORE_GREG,
	QNT_CORE_FPREG,
	QNT_NUM
};

#ifndef __QNXNTO__
typedef uint64_t _uint64;
typedef uint32_t _uint32;
typedef uint32_t _uintptr;
typedef uint16_t _uint16;
typedef uint8_t  _uint8;

typedef int64_t _int64;
typedef int32_t _int32;
typedef int32_t _intptr;
typedef int16_t _int16;
typedef int8_t  _int8;
#endif

typedef struct {
	long bits[2];
} nto_sigset_t;

union nto_sigval {
    int         sival_int;
    void       *sival_ptr;
};

typedef struct nto_siginfo {
	int				si_signo;
	int				si_code;		/* if SI_NOINFO, only si_signo is valid */
	void			(*si_handler)();
	union {
		int				_pad[6];
		struct {
			pid_t			_pid;
			union {
				struct {
					union nto_sigval	_value;
					uid_t			_uid;
				}				_kill;		/* si_code <= 0 SI_FROMUSER */
				struct {
					int				_status;	/* CLD_EXITED status, else signo */
					clock_t			_utime;
					clock_t			_stime;
				}				_chld;		/* si_signo=SIGCHLD si_code=CLD_* */
			}				_pdata;
		}				_proc;
		struct {
			int				_fltno;
			void			*_addr;	
			void			*_fltip;	
		}				_fault;				/* si_signo=SIGSEGV,ILL,FPE,TRAP,BUS */
	}				_data;
}				nto_siginfo_t;

typedef struct arm_cpu_registers {
	_uint32	gpr[16];
	_uint32	spsr;
} ARM_CPU_REGISTERS;

typedef struct arm_fpu_registers {
	/*
	 * There is no architecturally defined FPU
	 */
	unsigned	__dummy;
} ARM_FPU_REGISTERS;

typedef struct arm_alt_registers {
	union {
		struct xscale_cp0 {
			unsigned	acc0_lo;
			unsigned	acc0_hi;
		} xscale;
	} un;
} ARM_ALT_REGISTERS;

typedef union {
	_uint64	u64;
	double		f;
} mipsfloat;

typedef struct mips_cpu_registers {
	_uint32	regs[74];
	_uint64	regs_alignment;
} MIPS_CPU_REGISTERS;

typedef struct mips_fpu_registers {
	mipsfloat	fpr[32];
	_uint32	fpcr31;
} MIPS_FPU_REGISTERS;

typedef struct mips_alt_registers {
	union {
		struct mips_tx79 {
			_uint64	gpr_hi[32];
			_uint64	lo1;
			_uint64	hi1;
			_uint32	sa;
		} tx79;
	} un;
} MIPS_ALT_REGISTERS;

#ifdef __BIGREGS__
	typedef _uint64	ppcint;
#else
	typedef _uint32	ppcint;
#endif

typedef union {
	_uint64		u64;
	double			f;
} ppcfloat;

typedef union {
	_uint64		u64[2];
	_uint32		u32[4];
	_uint8			u8[16];
	float			f32[4];
} ppcvmx;

typedef struct ppc_cpu_registers {
	ppcint		gpr[32];
	ppcint		ctr;
	ppcint		lr;
	ppcint		msr;
	ppcint		iar;
	_uint32		cr;
	_uint32		xer;
	_uint32		ear;	/* not present on all chips */
	_uint32		mq;		/* only on the 601 */
	_uint32		vrsave;	/* on Altivec CPU's, SPR256 */
} PPC_CPU_REGISTERS;

typedef struct ppc_fpu_registers {
	ppcfloat	fpr[32];
	_uint32		fpscr;
	_uint32		fpscr_val;
	/* load/store of fpscr is done through fprs, the real value is in [32,63] of a fpr.
		fpscr is the address for lfd, stfd. fpscr_val is the real value of fpscr */
} PPC_FPU_REGISTERS;

typedef struct ppc_vmx_registers {
	ppcvmx		vmxr[32];
	ppcvmx		vscr;
} PPC_VMX_REGISTERS;

typedef struct ppc_alt_registers {
	PPC_VMX_REGISTERS	vmx;
} PPC_ALT_REGISTERS;

typedef _uint32	shint;

typedef _uint32	shfloat;

typedef struct sh_cpu_registers {
	shint		gr[16];
	shint		sr;
	shint		pc;
/*	shint		dbr; */
	shint		gbr;
	shint		mach;
	shint		macl;
	shint		pr;
} SH_CPU_REGISTERS;

typedef struct sh_fpu_registers {
	shfloat		fpr_bank0[16];
	shfloat		fpr_bank1[16];
	_uint32	fpul;
	_uint32	fpscr;
} SH_FPU_REGISTERS;

typedef struct sh_alt_registers {
	/*
	 * There are no architecturally defined alt regs
	 */
	unsigned	__dummy;
} SH_ALT_REGISTERS;

typedef struct x86_cpu_registers {
#ifdef __SEGMENTS__
	_uint32	gs, fs;
	_uint32	es, ds;
#endif
	_uint32	edi, esi, ebp, exx, ebx, edx, ecx, eax;
	_uint32	eip, cs, efl;
	_uint32	esp, ss;
} X86_CPU_REGISTERS;

typedef struct fsave_area {
	_uint32	fpu_control_word;
	_uint32	fpu_status_word;
	_uint32	fpu_tag_word;
	_uint32	fpu_ip;
	_uint32	fpu_cs;
	_uint32	fpu_op;
	_uint32	fpu_ds;
	_uint8		st_regs[80]; /* each register is 10 bytes! */
} X86_FSAVE_REGISTERS;

typedef struct fxsave_area {
	_uint16	fpu_control_word;
	_uint16	fpu_status_word;
	_uint16	fpu_tag_word;
	_uint16	fpu_operand;
	_uint32	fpu_ip;
	_uint32	fpu_cs;
	_uint32	fpu_op;
	_uint32	fpu_ds;
	_uint32	mxcsr;
	_uint32	reserved;
	_uint8		st_regs[128];
	_uint8		xmm_regs[128];
	_uint8		reserved2[224];
} X86_FXSAVE_REGISTERS;

typedef union x86_fpu_registers {
	X86_FSAVE_REGISTERS	fsave_area;
	X86_FXSAVE_REGISTERS	fxsave_area;
	_uint8			data[512];		/* Needs to be this big for the emulator. */
} X86_FPU_REGISTERS;

#ifdef __QNX__
__BEGIN_DECLS

#include <_pack64.h>
#endif

#define _DEBUG_FLAG_STOPPED			0x00000001	/* Thread is not running */
#define _DEBUG_FLAG_ISTOP			0x00000002	/* Stopped at point of interest */
#define _DEBUG_FLAG_IPINVAL			0x00000010	/* IP is not valid */
#define _DEBUG_FLAG_ISSYS			0x00000020	/* System process */
#define _DEBUG_FLAG_SSTEP			0x00000040	/* Stopped because of single step */
#define _DEBUG_FLAG_CURTID			0x00000080	/* Thread is current thread */
#define _DEBUG_FLAG_TRACE_EXEC		0x00000100	/* Stopped because of breakpoint */
#define _DEBUG_FLAG_TRACE_RD		0x00000200	/* Stopped because of read access */
#define _DEBUG_FLAG_TRACE_WR		0x00000400	/* Stopped because of write access */
#define _DEBUG_FLAG_TRACE_MODIFY	0x00000800	/* Stopped because of modified memory */
#define _DEBUG_FLAG_RLC				0x00010000	/* Run-on-Last-Close flag is set */
#define _DEBUG_FLAG_KLC				0x00020000	/* Kill-on-Last-Close flag is set */
#define _DEBUG_FLAG_FORK			0x00040000	/* Child inherits flags (Stop on fork/spawn) */
#define _DEBUG_FLAG_MASK			0x000f0000	/* Flags that can be changed */

enum {
	_DEBUG_WHY_REQUESTED,
	_DEBUG_WHY_SIGNALLED,
	_DEBUG_WHY_FAULTED,
	_DEBUG_WHY_JOBCONTROL,
	_DEBUG_WHY_TERMINATED,
	_DEBUG_WHY_CHILD,
	_DEBUG_WHY_EXEC
};

#define _DEBUG_RUN_CLRSIG			0x00000001	/* Clear pending signal */
#define _DEBUG_RUN_CLRFLT			0x00000002	/* Clear pending fault */
#define _DEBUG_RUN_TRACE			0x00000004	/* Trace mask flags interesting signals */
#define _DEBUG_RUN_HOLD				0x00000008	/* Hold mask flags interesting signals */
#define _DEBUG_RUN_FAULT			0x00000010	/* Fault mask flags interesting faults */
#define _DEBUG_RUN_VADDR			0x00000020	/* Change ip before running */
#define _DEBUG_RUN_STEP				0x00000040	/* Single step only one thread */
#define _DEBUG_RUN_STEP_ALL			0x00000080	/* Single step one thread, other threads run */
#define _DEBUG_RUN_CURTID			0x00000100	/* Change current thread (target thread) */
#define _DEBUG_RUN_ARM				0x00000200	/* Deliver event at point of interest */

typedef struct _debug_process_info {
	pid_t						pid;
	pid_t						parent;
	_uint32						flags;
	_uint32						umask;
	pid_t						child;
	pid_t						sibling;
	pid_t						pgrp;
	pid_t						sid;
	_uintptr					base_address;
	_uintptr					initial_stack;
	uid_t						uid;
	gid_t						gid;
	uid_t						euid;
	gid_t						egid;
	uid_t						suid;
	gid_t						sgid;
	nto_sigset_t					sig_ignore;
	nto_sigset_t					sig_queue;
	nto_sigset_t					sig_pending;
	_uint32						num_chancons;
	_uint32						num_fdcons;
	_uint32						num_threads;
	_uint32						num_timers;
	_uint64						reserved[20];
}							nto_procfs_info;;

typedef struct _debug_thread_info {
	pid_t						pid;
	_uint32  					tid;
	_uint32						flags;
	_uint16						why;
	_uint16						what;
	_uintptr					ip;
	_uintptr					sp;
	_uintptr					stkbase;
	_uintptr					tls;
	_uint32						stksize;
	_uint32						tid_flags;
	_uint8						priority;
	_uint8						real_priority;
	_uint8						policy;
	_uint8						state;
	_int16						syscall;
	_uint16						last_cpu;
	_uint32						timeout;
	_int32						last_chid;
	nto_sigset_t					sig_blocked;
	nto_sigset_t					sig_pending;
	nto_siginfo_t					info;
	_uint32						reserved1;
	union {
		struct {
			_uint32  					tid;
		}							join;
		struct {
			_int32						id;
			_uintptr					sync;
		}							sync;
		struct {
			_uint32						nid;
			pid_t						pid;
			_int32						coid;
			_int32						chid;
			_int32						scoid;
		}							connect;
		struct {
			_int32						chid;
		}							channel;
		struct {
			pid_t						pid;
			_uintptr					vaddr;
			_uint32						flags;
		}							waitpage;
		struct {
			_uint32						size;
		}							stack;
		_uint64						filler[4];
	}							blocked;
	_uint64						reserved2[8];
}							nto_procfs_status;

typedef union _debug_gregs {
	ARM_CPU_REGISTERS			arm;
	MIPS_CPU_REGISTERS			mips;
	PPC_CPU_REGISTERS			ppc;
	SH_CPU_REGISTERS			sh;
	X86_CPU_REGISTERS			x86;
	_uint64						padding[1024];
}							nto_gregset_t;

typedef union _debug_fpregs {
/*	ARM_FPU_REGISTERS			arm;	*/
	MIPS_FPU_REGISTERS			mips;
	PPC_FPU_REGISTERS			ppc;
	SH_FPU_REGISTERS			sh;
	X86_FPU_REGISTERS			x86;
	_uint64						padding[1024];
}							nto_fpregset_t;

typedef union _debug_altregs {
	ARM_ALT_REGISTERS			arm;
	MIPS_ALT_REGISTERS			mips;	
	PPC_ALT_REGISTERS			ppc;
/*	SH_ALT_REGISTERS			sh;	*/
/*	X86_ALT_REGISTERS			x86;	*/
	_uint64						padding[1024];
}							debug_altreg_t;

#ifdef __QNX__
#include <_packpop.h>

__END_DECLS
#endif

#endif
