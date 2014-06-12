#ifdef __QNXNTO__
#include <stdlib.h>
#include <signal.h>
#include <sys/neutrino.h>

static int suspend_other_threads = 1;
static void (*previous_handler)(int) = SIG_ERR;

static const int default_signal = SIGUSR2;

static void
flush_cov_data(int signo)
{
  if(suspend_other_threads)
    ThreadCtl(_NTO_TCTL_THREADS_HOLD, 0);
  __gcov_flush();
  if(suspend_other_threads)
    ThreadCtl(_NTO_TCTL_THREADS_CONT, 0);
  if (previous_handler == SIG_DFL
      || previous_handler == SIG_IGN
      || previous_handler == SIG_ERR)
    {
      /* IDE uses default_signal. For any other signal,
	 perform default action.  */
      if (signo != default_signal)
	{
	  signal(signo, SIG_DFL);
	  kill(getpid(), signo);
	}
    }
  else
    {
      (*previous_handler)(signo); /* This will chain through all
				     handlers.  It will only work
				     if the application did not
				     register its own handler (which
				     does not call the previous handler).  */
    }
}

static void
__flush_init(void)
{
  if (previous_handler != SIG_ERR)
    {
      /* Already initalized for this binary. */
      return;
    }
  else
    {
      int sigNo = default_signal;
      char* s;
      s = getenv( "COV_DATA_FLUSH" );
      if( s != 0 ) {
	sigNo = atoi(s);
      }
      s = getenv( "COV_NO_SUSPEND_THREADS" );
      if(0 != s)
	suspend_other_threads=0;
      if(sigNo > 0) // Use 0 to disable signal trap
	previous_handler = signal(sigNo, flush_cov_data);
    }
}
#endif /* __QNXNTO__ */
