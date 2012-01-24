/* nto-signals.c - QNX Neutrino signal translation.

   Copyright (C) 2009 Free Software Foundation, Inc.

   Contributed by QNX Software Systems Ltd.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* Nto signal to gdb's enum target_signal translation. */

/* On hosts other than neutrino, signals may differ. */

#include "defs.h"
#include "nto-signals.h"
#include "target.h"

#ifndef __QNXNTO__

#define NTO_SIGHUP      1   /* hangup */
#define NTO_SIGINT      2   /* interrupt */
#define NTO_SIGQUIT     3   /* quit */
#define NTO_SIGILL      4   /* illegal instruction (not reset when caught) */
#define NTO_SIGTRAP     5   /* trace trap (not reset when caught) */
#define NTO_SIGIOT      6   /* IOT instruction */
#define NTO_SIGABRT     6   /* used by abort */
#define NTO_SIGEMT      7   /* EMT instruction */
#define NTO_SIGDEADLK   7   /* Mutex deadlock */
#define NTO_SIGFPE      8   /* floating point exception */
#define NTO_SIGKILL     9   /* kill (cannot be caught or ignored) */
#define NTO_SIGBUS      10  /* bus error */
#define NTO_SIGSEGV     11  /* segmentation violation */
#define NTO_SIGSYS      12  /* bad argument to system call */
#define NTO_SIGPIPE     13  /* write on pipe with no reader */
#define NTO_SIGALRM     14  /* real-time alarm clock */
#define NTO_SIGTERM     15  /* software termination signal from kill */
#define NTO_SIGUSR1     16  /* user defined signal 1 */
#define NTO_SIGUSR2     17  /* user defined signal 2 */
#define NTO_SIGCHLD     18  /* death of child */
#define NTO_SIGPWR      19  /* power-fail restart */
#define NTO_SIGWINCH    20  /* window change */
#define NTO_SIGURG      21  /* urgent condition on I/O channel */
#define NTO_SIGPOLL     22  /* System V name for NTO_SIGIO */
#define NTO_SIGIO       NTO_SIGPOLL
#define NTO_SIGSTOP     23  /* sendable stop signal not from tty */
#define NTO_SIGTSTP     24  /* stop signal from tty */
#define NTO_SIGCONT     25  /* continue a stopped process */
#define NTO_SIGTTIN     26  /* attempted background tty read */
#define NTO_SIGTTOU     27  /* attempted background tty write */
#define NTO_SIGVTALRM   28  /* virtual timer expired */
#define NTO_SIGPROF     29  /* profileing timer expired */
#define NTO_SIGXCPU     30  /* exceded cpu limit */
#define NTO_SIGXFSZ     31  /* exceded file size limit */
#define NTO_SIGRTMIN    41  /* Realtime signal 41 (SIGRTMIN) */
#define NTO_SIGRTMAX    56  /* Realtime signal 56 (SIGRTMAX) */
#define NTO_SIGSELECT   (NTO_SIGRTMAX + 1)
#define NTO_SIGPHOTON   (NTO_SIGRTMAX + 2)

static struct
  {
    int nto_sig;
    enum target_signal gdb_sig;
  }
sig_map[] =
{
  {NTO_SIGHUP, TARGET_SIGNAL_HUP},
  {NTO_SIGINT, TARGET_SIGNAL_INT},
  {NTO_SIGQUIT, TARGET_SIGNAL_QUIT},
  {NTO_SIGILL, TARGET_SIGNAL_ILL},
  {NTO_SIGTRAP, TARGET_SIGNAL_TRAP},
  {NTO_SIGABRT, TARGET_SIGNAL_ABRT},
  {NTO_SIGEMT, TARGET_SIGNAL_EMT},
  {NTO_SIGFPE, TARGET_SIGNAL_FPE},
  {NTO_SIGKILL, TARGET_SIGNAL_KILL},
  {NTO_SIGBUS, TARGET_SIGNAL_BUS},
  {NTO_SIGSEGV, TARGET_SIGNAL_SEGV},
  {NTO_SIGSYS, TARGET_SIGNAL_SYS},
  {NTO_SIGPIPE, TARGET_SIGNAL_PIPE},
  {NTO_SIGALRM, TARGET_SIGNAL_ALRM},
  {NTO_SIGTERM, TARGET_SIGNAL_TERM},
  {NTO_SIGUSR1, TARGET_SIGNAL_USR1},
  {NTO_SIGUSR2, TARGET_SIGNAL_USR2},
  {NTO_SIGCHLD, TARGET_SIGNAL_CHLD},
  {NTO_SIGPWR, TARGET_SIGNAL_PWR},
  {NTO_SIGWINCH, TARGET_SIGNAL_WINCH},
  {NTO_SIGURG, TARGET_SIGNAL_URG},
  {NTO_SIGPOLL, TARGET_SIGNAL_POLL},
  {NTO_SIGSTOP, TARGET_SIGNAL_STOP},
  {NTO_SIGTSTP, TARGET_SIGNAL_TSTP},
  {NTO_SIGCONT, TARGET_SIGNAL_CONT},
  {NTO_SIGTTIN, TARGET_SIGNAL_TTIN},
  {NTO_SIGTTOU, TARGET_SIGNAL_TTOU},
  {NTO_SIGVTALRM, TARGET_SIGNAL_VTALRM},
  {NTO_SIGPROF, TARGET_SIGNAL_PROF},
  {NTO_SIGXCPU, TARGET_SIGNAL_XCPU},
  {NTO_SIGXFSZ, TARGET_SIGNAL_XFSZ}
};
#endif // ndef __QNXNTO__

/* Convert nto signal to gdb signal.  */
enum target_signal
target_signal_from_nto(struct gdbarch *gdbarch, int sig)
{
#ifndef __QNXNTO__
  int i;
  if (sig == 0)
    return 0;

  for (i = 0; i != ARRAY_SIZE (sig_map); i++)
    {
      if (sig_map[i].nto_sig == sig)
        return sig_map[i].gdb_sig;
    }

  if (sig >= NTO_SIGRTMIN && sig <= NTO_SIGRTMAX)
    return TARGET_SIGNAL_REALTIME_41 + sig - NTO_SIGRTMIN;

#endif /* __QNXNTO__ */
  return target_signal_from_host(sig);
}

/* Convert gdb signal to nto signal.  */

int
target_signal_to_nto(struct gdbarch *gdbarch, enum target_signal sig)
{
#ifndef __QNXNTO__
  int i;
  if (sig == 0)
    return 0;
    
  for (i = 0; i != ARRAY_SIZE (sig_map); i++)
    {
      if (sig_map[i].gdb_sig == sig)
        return sig_map[i].nto_sig;
    }

  if (sig >= TARGET_SIGNAL_REALTIME_41
      && sig <= TARGET_SIGNAL_REALTIME_41 + (NTO_SIGRTMAX - NTO_SIGRTMIN + 1))
    return (sig - TARGET_SIGNAL_REALTIME_41 + NTO_SIGRTMIN);
#endif /* __QNXNTO__ */
  return target_signal_to_host(sig);
}

