#undef GKM_DEBUG

#include <string.h>
#include "defs.h"
#include "serial.h"
#include "command.h"
#include "gdbcmd.h"
#include <fcntl.h>
#include <sys/types.h>
#include <signal.h>
#include "terminal.h"
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef __QNXTARGET__
#include "dsmsgs.h"  	/* This is for TS_TEXT_MAX_SIZE  */
#define re_comp ignore_re_comp // Use the one in defs.h...
#include <unix.h>
#undef re_comp
#endif

#ifndef PATH_MAX
#define PATH_MAX 255
#endif

#include <sys/time.h>

#ifdef HAVE_TERMIOS

struct qnxpty_ttystate
{
  struct termios termios;
};
#endif /* termios */

#ifdef HAVE_TERMIO

/* It is believed that all systems which have added job control to SVR3
   (e.g. sco) have also added termios.  Even if not, trying to figure out
   all the variations (TIOCGPGRP vs. TCGETPGRP, etc.) would be pretty
   bewildering.  So we don't attempt it.  */

struct qnxpty_ttystate
{
  struct termio termio;
};
#endif /* termio */

#ifdef HAVE_SGTTY
/* Needed for the code which uses select().  We would include <sys/select.h>
   too if it existed on all systems.  */
#include <sys/time.h>

struct qnxpty_ttystate
{
  struct sgttyb sgttyb;
  struct tchars tc;
  struct ltchars ltc;
  /* Line discipline flags.  */
  int lmode;
};
#endif /* sgtty */

static int qnxpty_open PARAMS ((struct serial *scb, const char *name));
static void qnxpty_raw PARAMS ((struct serial *scb));
static int wait_for PARAMS ((struct serial *scb, int timeout));
static int qnxpty_readchar PARAMS ((struct serial *scb, int timeout));
static int qnxpty_setbaudrate PARAMS ((struct serial *scb, int rate));
static int qnxpty_write PARAMS ((struct serial *scb, const char *str, int len));
/* FIXME: static void qnxpty_restore PARAMS ((struct serial *scb)); */
static void qnxpty_close PARAMS ((struct serial *scb));
static int get_tty_state PARAMS ((struct serial *scb, struct qnxpty_ttystate *state));
static int set_tty_state PARAMS ((struct serial *scb, struct qnxpty_ttystate *state));
static serial_ttystate qnxpty_get_tty_state PARAMS ((struct serial *scb));
static int qnxpty_set_tty_state PARAMS ((struct serial *scb, serial_ttystate state));
extern void qnx_outgoing_text(char *buf, int nbytes);

static char *qnxserver = NULL;
static char *qnxserverdev = NULL;
static pid_t qnxpid = -1;
static int qnx_ptydelay = 1;


/* 
 * Open up a pty/tty pair for IPC with the qnxserver (pdebug). 
 * We open both master and slave, fork, close the slave
 * in the child and dup2 stdin/out/err to the master.  We close 
 * the master in the parent, leaving the slave tty fd in the scb.  
 * This means pdebug has to use stdio, NOT the tty device, so we 
 * launch it now with "pdebug -", meaning "use stdio" as opposed
 * to "pdebug /dev/ptyNM".
 */

static int
qnxpty_open(scb, name)
     struct serial *scb;
     const char *name;
{
  char pty[PATH_MAX];
  char *p;
  int master; 

  if (openpty(&master, &scb->fd, &pty[0], 0, 0) == -1) {
    printf_unfiltered("openpty() failed: %s\n", strerror(errno));
    return -1;
  }

  p = strstr(pty, "tty");
  *p = 'p';
  printf_unfiltered("Launching on pty %s\n", pty);

  qnxpid = fork();

  if (qnxpid == -1) {
    printf_unfiltered("fork() failed: %s\n", strerror(errno));
    return -1;
  }
  
  else if (qnxpid == 0) {        /* child */
    sigset_t set;

    sigemptyset (&set);
    sigaddset (&set, SIGUSR1);
    sigprocmask (SIG_UNBLOCK, &set, NULL);

    setsid();

    close(scb->fd);     /* all done with slave in child */

    /* master becomes stdin/stdout/stderr of child */
    if (dup2(master, STDIN_FILENO) != STDIN_FILENO) {
      printf_unfiltered("dup2 of stdin failed.\n");
      close(master);
      exit(0);
    }
    if (dup2(master, STDOUT_FILENO) != STDOUT_FILENO) {
      printf_unfiltered("dup2 of stdout failed.\n");
      close(master);
      exit(0);
    }
    if (dup2(master, STDERR_FILENO) != STDERR_FILENO) {
      printf_unfiltered("dup2 of stderr failed.\n");
      close(master);
      exit(0);
    }

    /* Launch the qnxserver, which in this case has to be "pdebug -". */
    execlp(qnxserver, qnxserver, qnxserverdev, 0);

    printf_unfiltered("execlp(%s) failed: %s\n", qnxserver, strerror(errno));
    close(master);
    exit(1);
  } 
  close(master);
  printf_unfiltered("Debug server launched.\n");
  sleep(qnx_ptydelay); // wait for a while for the server, pdebug.
  return 0;
}

static int
get_tty_state(scb, state)
     struct serial *scb;
     struct qnxpty_ttystate *state;
{
#ifdef HAVE_TERMIOS
  extern int errno;

  if (tcgetattr(scb->fd, &state->termios) < 0)
    return -1;

  return 0;
#endif

#ifdef HAVE_TERMIO
  if (ioctl (scb->fd, TCGETA, &state->termio) < 0)
    return -1;
  return 0;
#endif

#ifdef HAVE_SGTTY
  if (ioctl (scb->fd, TIOCGETP, &state->sgttyb) < 0)
    return -1;
  if (ioctl (scb->fd, TIOCGETC, &state->tc) < 0)
    return -1;
  if (ioctl (scb->fd, TIOCGLTC, &state->ltc) < 0)
    return -1;
  if (ioctl (scb->fd, TIOCLGET, &state->lmode) < 0)
    return -1;

  return 0;
#endif
}

static int
set_tty_state(scb, state)
     struct serial *scb;
     struct qnxpty_ttystate *state;
{
#ifdef HAVE_TERMIOS
  if (tcsetattr(scb->fd, TCSANOW, &state->termios) < 0)
    return -1;

  return 0;
#endif

#ifdef HAVE_TERMIO
  if (ioctl (scb->fd, TCSETA, &state->termio) < 0)
    return -1;
  return 0;
#endif

#ifdef HAVE_SGTTY
  if (ioctl (scb->fd, TIOCSETN, &state->sgttyb) < 0)
    return -1;
  if (ioctl (scb->fd, TIOCSETC, &state->tc) < 0)
    return -1;
  if (ioctl (scb->fd, TIOCSLTC, &state->ltc) < 0)
    return -1;
  if (ioctl (scb->fd, TIOCLSET, &state->lmode) < 0)
    return -1;

  return 0;
#endif
}

static serial_ttystate
qnxpty_get_tty_state(scb)
     struct serial *scb;
{
  struct qnxpty_ttystate *state;

  state = (struct qnxpty_ttystate *)xmalloc(sizeof *state);

  if (get_tty_state(scb, state))
    return NULL;

  return (serial_ttystate)state;
}

static int
qnxpty_set_tty_state(scb, ttystate)
     struct serial *scb;
     serial_ttystate ttystate;
{
  struct qnxpty_ttystate *state;

  state = (struct qnxpty_ttystate *)ttystate;

  return set_tty_state(scb, state);
}

static int
qnxpty_noflush_set_tty_state (scb, new_ttystate, old_ttystate)
     struct serial *scb;
     serial_ttystate new_ttystate;
     serial_ttystate old_ttystate;
{
  struct qnxpty_ttystate new_state;
#ifdef HAVE_SGTTY
  struct qnxpty_ttystate *state = (struct qnxpty_ttystate *) old_ttystate;
#endif

  new_state = *(struct qnxpty_ttystate *)new_ttystate;

  /* Don't change in or out of raw mode; we don't want to flush input.
     termio and termios have no such restriction; for them flushing input
     is separate from setting the attributes.  */

#ifdef HAVE_SGTTY
  if (state->sgttyb.sg_flags & RAW)
    new_state.sgttyb.sg_flags |= RAW;
  else
    new_state.sgttyb.sg_flags &= ~RAW;

  /* I'm not sure whether this is necessary; the manpage just mentions
     RAW not CBREAK.  */
  if (state->sgttyb.sg_flags & CBREAK)
    new_state.sgttyb.sg_flags |= CBREAK;
  else
    new_state.sgttyb.sg_flags &= ~CBREAK;
#endif

  return set_tty_state (scb, &new_state);
}

static void
qnxpty_print_tty_state (scb, ttystate)
     struct serial *scb;
     serial_ttystate ttystate;
{
  struct qnxpty_ttystate *state = (struct qnxpty_ttystate *) ttystate;
  int i;

#ifdef HAVE_TERMIOS
  printf_filtered ("c_iflag = 0x%x, c_oflag = 0x%x,\n",
		   (unsigned int)state->termios.c_iflag, (unsigned int)state->termios.c_oflag);
  printf_filtered ("c_cflag = 0x%x, c_lflag = 0x%x\n",
		   (unsigned int)state->termios.c_cflag, (unsigned int)state->termios.c_lflag);
#if 0
  /* This not in POSIX, and is not really documented by those systems
     which have it (at least not Sun).  */
  printf_filtered ("c_line = 0x%x.\n", state->termios.c_line);
#endif
  printf_filtered ("c_cc: ");
  for (i = 0; i < NCCS; i += 1)
    printf_filtered ("0x%x ", state->termios.c_cc[i]);
  printf_filtered ("\n");
#endif

#ifdef HAVE_TERMIO
  printf_filtered ("c_iflag = 0x%x, c_oflag = 0x%x,\n",
		   state->termio.c_iflag, state->termio.c_oflag);
  printf_filtered ("c_cflag = 0x%x, c_lflag = 0x%x, c_line = 0x%x.\n",
		   state->termio.c_cflag, state->termio.c_lflag,
		   state->termio.c_line);
  printf_filtered ("c_cc: ");
  for (i = 0; i < NCC; i += 1)
    printf_filtered ("0x%x ", state->termio.c_cc[i]);
  printf_filtered ("\n");
#endif

#ifdef HAVE_SGTTY
  printf_filtered ("sgttyb.sg_flags = 0x%x.\n", state->sgttyb.sg_flags);

  printf_filtered ("tchars: ");
  for (i = 0; i < (int)sizeof (struct tchars); i++)
    printf_filtered ("0x%x ", ((unsigned char *)&state->tc)[i]);
  printf_filtered ("\n");

  printf_filtered ("ltchars: ");
  for (i = 0; i < (int)sizeof (struct ltchars); i++)
    printf_filtered ("0x%x ", ((unsigned char *)&state->ltc)[i]);
  printf_filtered ("\n");

  printf_filtered ("lmode:  0x%x\n", state->lmode);
#endif
}

static int
qnxpty_flush_output (scb)
     struct serial *scb;
{
#ifdef HAVE_TERMIOS
  return tcflush (scb->fd, TCOFLUSH);
#endif

#ifdef HAVE_TERMIO
  return ioctl (scb->fd, TCFLSH, 1);
#endif

#ifdef HAVE_SGTTY
  /* This flushes both input and output, but we can't do better.  */
  return ioctl (scb->fd, TIOCFLUSH, 0);
#endif  
}

static int
qnxpty_flush_input (scb)
     struct serial *scb;
{
  scb->bufcnt = 0;
  scb->bufp = scb->buf;

#ifdef HAVE_TERMIOS
  return tcflush (scb->fd, TCIFLUSH);
#endif

#ifdef HAVE_TERMIO
  return ioctl (scb->fd, TCFLSH, 0);
#endif

#ifdef HAVE_SGTTY
  /* This flushes both input and output, but we can't do better.  */
  return ioctl (scb->fd, TIOCFLUSH, 0);
#endif  
}

static int
qnxpty_send_break (scb)
     struct serial *scb;
{
#ifdef HAVE_TERMIOS
  return tcsendbreak (scb->fd, 0);
#endif

#ifdef HAVE_TERMIO
  return ioctl (scb->fd, TCSBRK, 0);
#endif

#ifdef HAVE_SGTTY
  {
    int status;
    struct timeval timeout;

    status = ioctl (scb->fd, TIOCSBRK, 0);

    /* Can't use usleep; it doesn't exist in BSD 4.2.  */
    /* Note that if this select() is interrupted by a signal it will not wait
       the full length of time.  I think that is OK.  */
    timeout.tv_sec = 0;
    timeout.tv_usec = 250000;
    select (0, 0, 0, 0, &timeout);
    status = ioctl (scb->fd, TIOCCBRK, 0);
    return status;
  }
#endif  
}

static void
qnxpty_raw(scb)
     struct serial *scb;
{
  struct qnxpty_ttystate state;

  if (get_tty_state(scb, &state))
    fprintf_unfiltered(gdb_stderr, "get_tty_state failed: %s\n", safe_strerror(errno));

#ifdef HAVE_TERMIOS
  state.termios.c_iflag = 0;
  state.termios.c_oflag = 0;
  state.termios.c_lflag = 0;
  state.termios.c_cflag &= ~(CSIZE|PARENB);
  state.termios.c_cflag |= CLOCAL | CS8;
  state.termios.c_cc[VMIN] = 0;
  state.termios.c_cc[VTIME] = 0;
#endif

#ifdef HAVE_TERMIO
  state.termio.c_iflag = 0;
  state.termio.c_oflag = 0;
  state.termio.c_lflag = 0;
  state.termio.c_cflag &= ~(CSIZE|PARENB);
  state.termio.c_cflag |= CLOCAL | CS8;
  state.termio.c_cc[VMIN] = 0;
  state.termio.c_cc[VTIME] = 0;
#endif

#ifdef HAVE_SGTTY
  state.sgttyb.sg_flags |= RAW | ANYP;
  state.sgttyb.sg_flags &= ~(CBREAK | ECHO);
#endif

  scb->current_timeout = 0;

  if (set_tty_state (scb, &state))
    fprintf_unfiltered(gdb_stderr, "set_tty_state failed: %s\n", safe_strerror(errno));
}

/* Wait for input on scb, with timeout seconds.  Returns 0 on success,
   otherwise SERIAL_TIMEOUT or SERIAL_ERROR.

   For termio{s}, we actually just setup VTIME if necessary, and let the
   timeout occur in the read() in qnxpty_read().
 */

static int
wait_for(scb, timeout)
     struct serial *scb;
     int timeout;
{
  scb->timeout_remaining = 0;

// We want to use the select so we can easily handle stdinput with the select
#if defined(HAVE_SGTTY) || defined(__QNXTARGET__)
  {
    struct timeval tv;
    fd_set readfds;

    tv.tv_sec = timeout;
    tv.tv_usec = 0;


    while (1)
      {
	int numfds;

    	FD_ZERO (&readfds);
    	FD_SET(scb->fd, &readfds);
	if(timeout < 0)
		FD_SET(STDIN_FILENO, &readfds);

	if (timeout >= 0)
	  numfds = select(scb->fd+1, &readfds, 0, 0, &tv);
	else
	  numfds = select(scb->fd+1, &readfds, 0, 0, 0);

	if (numfds <= 0)
	{
	  if (numfds == 0)
	    return SERIAL_TIMEOUT;
	  else if (errno == EINTR)
	    continue;
	  else
	    return SERIAL_ERROR;	/* Got an error from select or poll */
	}
	if((timeout < 0) && (FD_ISSET(STDIN_FILENO, &readfds)))
	{
		int i;
		char buf[TS_TEXT_MAX_SIZE];
		i = read(STDIN_FILENO, buf, TS_TEXT_MAX_SIZE);
		qnx_outgoing_text(buf, i);
	}
	else
		return 0;
      }
  }
#else	/* end of HAVE_SGTTY  || __QNXTARGET__ */

#if defined HAVE_TERMIO || defined HAVE_TERMIOS
  if (timeout == scb->current_timeout)
    return 0;

  scb->current_timeout = timeout;

  {
    struct qnxpty_ttystate state;

    if (get_tty_state(scb, &state))
      fprintf_unfiltered(gdb_stderr, "get_tty_state failed: %s\n", safe_strerror(errno));

#ifdef HAVE_TERMIOS
    if (timeout < 0)
      {
	/* No timeout.  */
	state.termios.c_cc[VTIME] = 0;
	state.termios.c_cc[VMIN] = 1;
      }
    else
      {
	state.termios.c_cc[VMIN] = 0;
	state.termios.c_cc[VTIME] = timeout * 10;
	if (state.termios.c_cc[VTIME] != timeout * 10)
	  {

	    /* If c_cc is an 8-bit signed character, we can't go 
	       bigger than this.  If it is always unsigned, we could use
	       25.  */

	    scb->current_timeout = 12;
	    state.termios.c_cc[VTIME] = scb->current_timeout * 10;
	    scb->timeout_remaining = timeout - scb->current_timeout;
	  }
      }
#endif

#ifdef HAVE_TERMIO
    if (timeout < 0)
      {
	/* No timeout.  */
	state.termio.c_cc[VTIME] = 0;
	state.termio.c_cc[VMIN] = 1;
      }
    else
      {
	state.termio.c_cc[VMIN] = 0;
	state.termio.c_cc[VTIME] = timeout * 10;
	if (state.termio.c_cc[VTIME] != timeout * 10)
	  {
	    /* If c_cc is an 8-bit signed character, we can't go 
	       bigger than this.  If it is always unsigned, we could use
	       25.  */

	    scb->current_timeout = 12;
	    state.termio.c_cc[VTIME] = scb->current_timeout * 10;
	    scb->timeout_remaining = timeout - scb->current_timeout;
	  }
      }
#endif

    if (set_tty_state (scb, &state))
      fprintf_unfiltered(gdb_stderr, "set_tty_state failed: %s\n", safe_strerror(errno));

    return 0;
  }
#endif	/* HAVE_TERMIO || HAVE_TERMIOS */
#endif
}

/* Read a character with user-specified timeout.  TIMEOUT is number of seconds
   to wait, or -1 to wait forever.  Use timeout of 0 to effect a poll.  Returns
   char if successful.  Returns SERIAL_TIMEOUT if timeout expired, EOF if line
   dropped dead, or SERIAL_ERROR for any other error (see errno in that case).  */

static int
qnxpty_readchar(scb, timeout)
     struct serial *scb;
     int timeout;
{
  int status;

  if (scb->bufcnt-- > 0)
    return *scb->bufp++;

  while (1)
    {
      status = wait_for (scb, timeout);

      if (status < 0)
	return status;

      scb->bufcnt = read (scb->fd, scb->buf, BUFSIZ);

      if (scb->bufcnt <= 0)
	{
	  if (scb->bufcnt == 0)
	    {
	      /* Zero characters means timeout (it could also be EOF, but
		 we don't (yet at least) distinguish).  */
	      if (scb->timeout_remaining > 0)
		{
		  timeout = scb->timeout_remaining;
		  continue;
		}
	      else
		return SERIAL_TIMEOUT;
	    }
	  else if (errno == EINTR)
	    continue;
	  else
	    return SERIAL_ERROR;	/* Got an error from read */
	}

      scb->bufcnt--;
      scb->bufp = scb->buf;
      return *scb->bufp++;
    }
}

#if 0

#ifndef B19200
#define B19200 EXTA
#endif

#ifndef B38400
#define B38400 EXTB
#endif

/* Translate baud rates from integers to damn B_codes.  Unix should
   have outgrown this crap years ago, but even POSIX wouldn't buck it.  */

static struct
{
  int rate;
  int code;
}
baudtab[] =
{
  {50, B50},
  {75, B75},
  {110, B110},
  {134, B134},
  {150, B150},
  {200, B200},
  {300, B300},
  {600, B600},
  {1200, B1200},
  {1800, B1800},
  {2400, B2400},
  {4800, B4800},
  {9600, B9600},
  {19200, B19200},
  {38400, B38400},
  {57600, B57600},
  {76800, B76800},
  {115200, B115200},
  {-1, -1},
};

static int 
rate_to_code(rate)
     int rate;
{
  int i;

  for (i = 0; baudtab[i].rate != -1; i++)
    if (rate == baudtab[i].rate)  
      return baudtab[i].code;

  return -1;
}
#else
#define rate_to_code(x) (x)
#endif

static int
qnxpty_setbaudrate(scb, rate)
     struct serial *scb;
     int rate;
{
  struct qnxpty_ttystate state;

  if (get_tty_state(scb, &state))
    return -1;

#ifdef HAVE_TERMIOS
  cfsetospeed (&state.termios, rate_to_code (rate));
  cfsetispeed (&state.termios, rate_to_code (rate));
#endif

#ifdef HAVE_TERMIO
#ifndef CIBAUD
#define CIBAUD CBAUD
#endif

  state.termio.c_cflag &= ~(CBAUD | CIBAUD);
  state.termio.c_cflag |= rate_to_code (rate);
#endif

#ifdef HAVE_SGTTY
  state.sgttyb.sg_ispeed = rate_to_code (rate);
  state.sgttyb.sg_ospeed = rate_to_code (rate);
#endif

  return set_tty_state (scb, &state);
}

static int
qnxpty_setstopbits(scb, num)
     struct serial *scb;
     int num;
{
  struct qnxpty_ttystate state;
  int newbit;

  if (get_tty_state(scb, &state))
    return -1;

  switch (num)
    {
    case SERIAL_1_STOPBITS:
      newbit = 0;
      break;
    case SERIAL_1_AND_A_HALF_STOPBITS:
    case SERIAL_2_STOPBITS:
      newbit = 1;
      break;
    default:
      return 1;
    }

#ifdef HAVE_TERMIOS
  if (!newbit)
    state.termios.c_cflag &= ~CSTOPB;
  else
    state.termios.c_cflag |= CSTOPB; /* two bits */
#endif

#ifdef HAVE_TERMIO
  if (!newbit)
    state.termio.c_cflag &= ~CSTOPB;
  else
    state.termio.c_cflag |= CSTOPB; /* two bits */
#endif

#ifdef HAVE_SGTTY
  return 0;			/* sgtty doesn't support this */
#endif

  return set_tty_state (scb, &state);
}

static int
qnxpty_write(scb, str, len)
     struct serial *scb;
     const char *str;
     int len;
{
  int cc;

  while (len > 0)
    {
      cc = write(scb->fd, str, len);

      if (cc < 0)
	return 1;
      len -= cc;
      str += cc;
    }
  return 0;
}

static void
qnxpty_close(scb)
     struct serial *scb;
{
  if (scb->fd < 0)
    return;

  kill(qnxpid, SIGTERM);
  close(scb->fd);
  scb->fd = -1;
}

static int
qnxpty_nop_drain_output (struct serial *scb)
{
  return 0;
}

static void
qnxpty_nop_async (struct serial *scb,
                int async_p)
{
	return;
}

void
_initialize_ser_qnxpty ()
{
  struct serial_ops *ops = xmalloc(sizeof(struct serial_ops));
  memset (ops, sizeof (struct serial_ops), 0);
  ops->name = "pty";
  ops->next = 0;
  ops->open = qnxpty_open;
  ops->close = qnxpty_close;
  ops->readchar = qnxpty_readchar;
  ops->write = qnxpty_write;
  ops->flush_output = qnxpty_flush_output;
  ops->flush_input = qnxpty_flush_input;
  ops->send_break = qnxpty_send_break;
  ops->go_raw = qnxpty_raw;
  ops->get_tty_state = qnxpty_get_tty_state;
  ops->set_tty_state = qnxpty_set_tty_state;
  ops->print_tty_state = qnxpty_print_tty_state;
  ops->noflush_set_tty_state = qnxpty_noflush_set_tty_state;
  ops->setbaudrate = qnxpty_setbaudrate;
  ops->setstopbits = qnxpty_setstopbits;
  ops->drain_output = qnxpty_nop_drain_output;
  ops->async = qnxpty_nop_async;
  serial_add_interface (ops);

  qnxserver = strdup("pdebug");
  qnxserverdev = strdup("-");
  add_show_from_set(add_set_cmd("qnxserver", no_class,
                                 var_filename, (char *)&qnxserver,
                                "Set QNX debug protocol server.\n", &setlist),
                                &showlist);
  add_show_from_set(add_set_cmd("qnxptydelay", no_class,
			         var_integer, (char *)&qnx_ptydelay,
			        "Set delay before attempting to talk to pdebug.\n", &setlist),
		                &showlist);
}

