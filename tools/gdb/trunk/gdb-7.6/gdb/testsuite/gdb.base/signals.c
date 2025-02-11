/* Test GDB dealing with stuff like stepping into sigtramp.  */

#include <signal.h>
#include <unistd.h>


static int count = 0;

#ifdef PROTOTYPES
static void
handler (int sig)
#else
static void
handler (sig)
     int sig;
#endif
{
  signal (sig, handler);
  ++count;
}

static void
func1 ()
{
  ++count;
}

static void
func2 ()
{
  ++count;
}

int
main ()
{
#ifdef SIGALRM
  signal (SIGALRM, handler);
#endif
#ifdef SIGHUP
  signal (SIGHUP, handler);
#endif
  alarm (1);
  ++count; /* first */
  alarm (1);
  ++count; /* second */
  func1 ();
  alarm (1);
  func2 ();
  return count;
}
