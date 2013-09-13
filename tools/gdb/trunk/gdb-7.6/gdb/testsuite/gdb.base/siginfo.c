/* This testcase is part of GDB, the GNU debugger.

   Copyright 2004-2013 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <errno.h>

static volatile int done;

#ifdef SA_SIGINFO
static void /* HANDLER */
handler (int sig, siginfo_t *info, void *context)
{
  done = 1;
} /* handler */
#else
static void
handler (int sig)
{
  done = 1;
} /* handler */
#endif

main ()
{
  int res;
  /* Set up the signal handler.  */
  {
    struct sigaction action;
    memset (&action, 0, sizeof (action));
#ifdef SA_SIGINFO
    action.sa_sigaction = handler;
    action.sa_flags |= SA_SIGINFO;
#else
    action.sa_handler = handler;
#endif
    sigaction (SIGVTALRM, &action, NULL);
    sigaction (SIGALRM, &action, NULL);
  }

  /* Set up a one-off timer.  A timer, rather than SIGSEGV, is used as
     after a timer handler finishes the interrupted code can safely
     resume.  */
  {
    struct itimerval itime;
    memset (&itime, 0, sizeof (itime));
    itime.it_value.tv_usec = 250 * 1000;

    res = setitimer (ITIMER_VIRTUAL, &itime, NULL);
    if (res == -1)
      {
	res = setitimer (ITIMER_REAL, &itime, NULL);
	if (res == -1)
	  {
	    printf ("Second call to setitimer failed. errno = %d\r\n", errno);
	    return 1;
	  }
      }
  }
  /* Wait.  */
  while (!done);
  return 0;
} /* main */
