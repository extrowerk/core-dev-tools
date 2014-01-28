/*
 * $QNXtpLicenseC:  
 * Copyright 2005,2007, QNX Software Systems. All Rights Reserved.
 *
 * This source code may contain confidential information of QNX Software 
 * Systems (QSS) and its licensors.  Any use, reproduction, modification, 
 * disclosure, distribution or transfer of this software, or any software 
 * that includes or is based upon any of this code, is prohibited unless 
 * expressly authorized by QSS by written agreement.  For more information 
 * (including whether this source code file has been published) please
 * email licensing@qnx.com. $
*/

/*

   This file was derived from remote.c. It communicates with a
   target talking the Neutrino remote debug protocol.
   See nto-share/dsmsgs.h for details.

   Copyright 2003 Free Software Foundation, Inc.

   Contributed by QNX Software Systems Ltd.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "exceptions.h"
#include <fcntl.h>
#include <signal.h>

#include "gdb_string.h"
#include "terminal.h"
#include "inferior.h"
#include "target.h"
#include "gdbcmd.h"
#include "objfiles.h"
#include "gdbthread.h"
#include "completer.h"
#include "cli/cli-decode.h"
#include "regcache.h"
#include "gdbcore.h"
#include "serial.h"
#include "readline/readline.h"

#include "elf-bfd.h"
#include "elf/common.h"

#include "environ.h"

#include <time.h>

#include "nto-share/dsmsgs.h"
#include "nto-tdep.h"

#ifdef __QNX__
#include <sys/debug.h>
#include <sys/elf_notes.h>
#define __ELF_H_INCLUDED /* Needed for our link.h to avoid including elf.h.  */
#define Elf32_Phdr Elf32_External_Phdr
#include <sys/link.h>
typedef debug_thread_t nto_procfs_status;
typedef debug_process_t nto_procfs_info;
typedef siginfo_t nto_siginfo_t;
#else
#include "nto-share/debug.h"
#endif

#include "solib.h"


#ifdef __CYGWIN__
#include <sys/cygwin.h>
#endif

#ifdef __MINGW32__
#define	ENOTCONN	57		/* Socket is not connected */
#endif
#
#ifndef EOK
#define EOK 0
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define QNX_READ_MODE	0x0
#define QNX_WRITE_MODE	0x301
#define QNX_WRITE_PERMS	0x1ff

/* The following define does a cast to const gdb_byte * type.  */

#define EXTRACT_SIGNED_INTEGER(ptr, len, byte_order) \
  extract_signed_integer ((const gdb_byte *)(ptr), len, byte_order)
#define EXTRACT_UNSIGNED_INTEGER(ptr, len, byte_order) \
  extract_unsigned_integer ((const gdb_byte *)(ptr), len, byte_order)


typedef union
{
  unsigned char buf[DS_DATA_MAX_SIZE];
  DSMsg_union_t pkt;
  TSMsg_text_t text;
} DScomm_t;


#ifdef __MINGW32__
/* Name collision with a symbol declared in Winsock2.h.  */
#define recv recvb
#endif



static void init_nto_ops (void);

static int putpkt (const DScomm_t *tran, unsigned);

static int readchar (int timeout);

static int getpkt (DScomm_t *recv, int forever);

static unsigned nto_send_recv (const DScomm_t *tran, DScomm_t *recv, unsigned, int);

static int nto_write_bytes (CORE_ADDR memaddr, const gdb_byte *myaddr,
			    int len);

static int nto_read_bytes (CORE_ADDR memaddr, gdb_byte *myaddr, int len);

static void nto_files_info (struct target_ops *ignore);

static ptid_t nto_parse_notify (const DScomm_t *recv, struct target_ops *,
				struct target_waitstatus *status);

void nto_outgoing_text (char *buf, int nbytes);

static int nto_incoming_text (TSMsg_text_t *text, int len);

static void nto_send_init (DScomm_t *tran, unsigned cmd, unsigned subcmd, unsigned chan);

static int nto_send_env (const char *env);

static int nto_send_arg (const char *arg);

void nto_fetch_registers (struct target_ops *,
			  struct regcache *regcache, int regno);

static void nto_prepare_to_store (struct regcache *regcache);

static void nto_store_registers (struct target_ops *,
				 struct regcache *regcache, int regno);

static void nto_resume (struct target_ops *, ptid_t ptid, int step,
			enum gdb_signal sig);

static int nto_start_remote (char *dummy);


static void nto_open (char *name, int from_tty);
static void nto_close (int quitting);

static void nto_create_inferior (struct target_ops *,
				 char *exec_file, char *args, char **env,
				 int from_tty);

static void nto_mourn_inferior (struct target_ops *);

static int nto_fd_raw (int fd);

static ptid_t nto_wait (struct target_ops *,
			ptid_t ptid, struct target_waitstatus *status,
			int);

static void nto_kill (struct target_ops *);

static void nto_detach (struct target_ops *ops, char *args, int from_tty);

static void nto_interrupt (int signo);

static void nto_interrupt_twice (int signo);

static void interrupt_query (void);

static void upload_command (char *args, int from_tty);

static void download_command (char *args, int from_tty);

static void nto_add_commands (void);

static void nto_remove_commands (void);

static int nto_fileopen (char *fname, int mode, int perms);

static void nto_fileclose (int);

static int nto_fileread (char *buf, int size);

static int nto_filewrite (char *buf, int size);

static void nto_find_new_threads (struct target_ops *);

static void nto_pidlist (char *args, int from_tty);

static struct dsmapinfo *nto_mapinfo (unsigned addr, int first, int elfonly);

static void nto_meminfo (char *args, int from_tty);

static int nto_insert_hw_breakpoint (struct gdbarch *,
				     struct bp_target_info *);

static int nto_remove_hw_watchpoint (CORE_ADDR addr, int len, int type,
				     struct expression *);

static int nto_insert_hw_watchpoint (CORE_ADDR addr, int len, int type,
				     struct expression *);

static void nto_remote_inferior_data_cleanup (struct inferior *inf, void *dat);


struct nto_remote_inferior_data
{
  /* File to be executed on remote.  */
  char *remote_exe;

  /* Current working directory on remote.  */
  char *remote_cwd;

  /* Cached auxiliary vector */
  gdb_byte *auxv;
};

static struct nto_remote_inferior_data *nto_remote_inferior_data (void);

static struct target_ops nto_ops;

static const struct inferior_data *nto_remote_inferior_data_reg;

#ifdef __MINGW32__
static void
alarm (int sig)
{
  /* Do nothing, this is windows.  */
}

#define sleep(x) Sleep(1000 * (x))

#endif

struct pdebug_session
{
  /* Number of seconds to wait for a timeout on the remote side.  */
  int timeout;

  /* Whether to inherit environment from remote pdebug or host gdb.  */
  int inherit_env;

  /* File to be executed on remote.  Assigned to new inferiors.  */
  char *remote_exe;

  /* Current working directory on remote.  Assigned to new inferiors.  */
  char *remote_cwd;

  /* Descriptor for I/O to remote machine.  Initialize it to NULL so that
     nto_open knows that we don't have a file open when the program
     starts.  */
  struct serial *desc;

  /* NTO CPU type of the remote machine.  */
  int cputype;

  /* NTO CPU ID of the remote machine.  */
  unsigned cpuid;

  /* Communication channels to the remote.  */
  unsigned channelrd;
  unsigned channelwr;

  /* The version of the protocol used by the pdebug we connect to.
     Set in nto_start_remote().  */
  int target_proto_major;
  int target_proto_minor;

  /* Communication buffer used by to_resume and to_wait. Nothing else
   * should be using it, all other operations should use their own
   * buffers allocated on the stack or heap.  */
  DScomm_t recv;

};

struct pdebug_session only_session = {
  10,
  1,
  NULL,
  NULL,
  NULL,
  -1,
  0,
  SET_CHANNEL_DEBUG,
  SET_CHANNEL_DEBUG,
  0, /* target_proto_major */
  0, /* target_proto_minor */
};

/* Remote session (connection) to a QNX target. */
struct pdebug_session *current_session = &only_session;

/* Flag for whether upload command sets the current session's remote_exe.  */
static int upload_sets_exec = 1;

/* These define the version of the protocol implemented here.  */
#define HOST_QNX_PROTOVER_MAJOR	0
#define HOST_QNX_PROTOVER_MINOR	5

/* Stuff for dealing with the packets which are part of this protocol.  */

#define MAX_TRAN_TRIES 3
#define MAX_RECV_TRIES 3

#define FRAME_CHAR	0x7e
#define ESC_CHAR	0x7d

static char nak_packet[] =
  { FRAME_CHAR, SET_CHANNEL_NAK, 0, FRAME_CHAR };
static char ch_reset_packet[] =
  { FRAME_CHAR, SET_CHANNEL_RESET, 0xff, FRAME_CHAR };
static char ch_debug_packet[] =
  { FRAME_CHAR, SET_CHANNEL_DEBUG, 0xfe, FRAME_CHAR };
static char ch_text_packet[] =
  { FRAME_CHAR, SET_CHANNEL_TEXT, 0xfd, FRAME_CHAR };

#define SEND_NAK         serial_write(current_session->desc,nak_packet,sizeof(nak_packet))
#define SEND_CH_RESET    serial_write(current_session->desc,ch_reset_packet,sizeof(ch_reset_packet))
#define SEND_CH_DEBUG    serial_write(current_session->desc,ch_debug_packet,sizeof(ch_debug_packet))
#define SEND_CH_TEXT     serial_write(current_session->desc,ch_text_packet,sizeof(ch_text_packet))

/* Pdebug returns errno values on Neutrino that do not correspond to right
   errno values on host side.  */

#define NTO_ENAMETOOLONG        78
#define NTO_ELIBACC             83
#define NTO_ELIBBAD             84
#define NTO_ELIBSCN             85
#define NTO_ELIBMAX             86
#define NTO_ELIBEXEC            87
#define NTO_EILSEQ              88
#define NTO_ENOSYS              89

#if defined(__QNXNTO__) || defined (__SOLARIS__)
#define errnoconvert(x) x
#elif defined(__linux__) || defined (__CYGWIN__) || defined (__MINGW32__) || defined(__APPLE__)

struct errnomap_t { int nto; int other; };


static int
errnoconvert(int x)
{
  struct errnomap_t errnomap[] = {
    #if defined (__linux__)
      {NTO_ENAMETOOLONG, ENAMETOOLONG}, {NTO_ELIBACC, ELIBACC},
      {NTO_ELIBBAD, ELIBBAD}, {NTO_ELIBSCN, ELIBSCN}, {NTO_ELIBMAX, ELIBMAX},
      {NTO_ELIBEXEC, ELIBEXEC}, {NTO_EILSEQ, EILSEQ}, {NTO_ENOSYS, ENOSYS}
    #elif defined(__CYGWIN__)
      {NTO_ENAMETOOLONG, ENAMETOOLONG}, {NTO_ENOSYS, ENOSYS}
    #elif defined(__MINGW32__)
      /* The closest mappings from mingw's errno.h.  */
      {NTO_ENAMETOOLONG, ENAMETOOLONG}, {NTO_ELIBACC, ESRCH},
      {NTO_ELIBBAD, ESRCH}, {NTO_ELIBSCN, ENOEXEC}, {NTO_ELIBMAX, EPERM},
      {NTO_ELIBEXEC, ENOEXEC}, {NTO_EILSEQ, EILSEQ}, {NTO_ENOSYS, ENOSYS}
    #elif defined(__APPLE__)
      {NTO_ENAMETOOLONG, ENAMETOOLONG}, {NTO_ELIBACC, ESRCH},
      {NTO_ELIBBAD, ESRCH}, {NTO_ELIBSCN, ENOEXEC}, {NTO_ELIBMAX, EPERM},
      {NTO_ELIBEXEC, ENOEXEC}, {NTO_EILSEQ, EILSEQ}, {NTO_ENOSYS, ENOSYS}

    #endif
  };
  int i;

  for (i = 0; i < sizeof(errnomap) / sizeof(errnomap[0]); i++)
    if (errnomap[i].nto == x) return errnomap[i].other;
  return x;
}

#define errnoconvert(x) errnoconvert(x)
#else
#error errno mapping not setup for this host
#endif /* __QNXNTO__ */

/* Send a packet to the remote machine.  Also sets channelwr and informs
   target if channelwr has changed.  */
static int
putpkt (const DScomm_t *const tran, const unsigned len)
{
  int i;
  unsigned char csum = 0;
  unsigned char buf2[DS_DATA_MAX_SIZE * 2];
  unsigned char *p;

  /* Copy the packet into buffer BUF2, encapsulating it
     and giving it a checksum.  */

  p = buf2;
  *p++ = FRAME_CHAR;

  nto_trace (1) ("putpkt() - cmd %d, subcmd %d, mid %d\n",
			 tran->pkt.hdr.cmd, tran->pkt.hdr.subcmd,
			 tran->pkt.hdr.mid);

  if (remote_debug)
    printf_unfiltered ("Sending packet (len %d): ", len);

  for (i = 0; i < len; i++)
    {
      unsigned char c = tran->buf[i];

      if (remote_debug)
	printf_unfiltered ("%2.2x", c);
      csum += c;

      switch (c)
	{
	case FRAME_CHAR:
	case ESC_CHAR:
	  if (remote_debug)
	    printf_unfiltered ("[escape]");
	  *p++ = ESC_CHAR;
	  c ^= 0x20;
	  break;
	}
      *p++ = c;
    }

  csum ^= 0xff;

  if (remote_debug)
    {
      printf_unfiltered ("%2.2x\n", csum);
      gdb_flush (gdb_stdout);
    }
  switch (csum)
    {
    case FRAME_CHAR:
    case ESC_CHAR:
      *p++ = ESC_CHAR;
      csum ^= 0x20;
      break;
    }
  *p++ = csum;
  *p++ = FRAME_CHAR;

  /* GP added - June 17, 1999.  There used to be only 'channel'.
     Now channelwr and channelrd keep track of the state better.
     If channelwr is not in the right state, notify target and set channelwr.  */
  if (current_session->channelwr != tran->pkt.hdr.channel)
    {
      switch (tran->pkt.hdr.channel)
	{
	case SET_CHANNEL_TEXT:
	  SEND_CH_TEXT;
	  break;
	case SET_CHANNEL_DEBUG:
	  SEND_CH_DEBUG;
	  break;
	}
      current_session->channelwr = tran->pkt.hdr.channel;
    }

  if (serial_write (current_session->desc, (char *)buf2, p - buf2))
    perror_with_name ("putpkt: write failed");

  return len;
}

/* Read a single character from the remote end, masking it down to 8 bits.  */
static int
readchar (int timeout)
{
  int ch;

  ch = serial_readchar (current_session->desc, timeout);

  switch (ch)
    {
    case SERIAL_EOF:
      error ("Remote connection closed");
    case SERIAL_ERROR:
      perror_with_name ("Remote communication error");
    case SERIAL_TIMEOUT:
      return ch;
    default:
      return ch & 0xff;
    }
}

/* Come here after finding the start of the frame.  Collect the rest into BUF,
   verifying the checksum, length, and handling run-length compression.
   Returns 0 on any error, 1 on success.  */
static int
read_frame (unsigned char *const buf, const size_t bufsz)
{
  unsigned char csum;
  unsigned char *bp = buf;
  unsigned char modifier = 0;
  int c;

  if (remote_debug)
    printf_filtered ("Receiving data: ");

  csum = 0;

  memset (bp, -1, bufsz);
  for (;;)
    {
      c = readchar (current_session->timeout);

      switch (c)
	{
	case SERIAL_TIMEOUT:
	  puts_filtered ("Timeout in mid-packet, retrying\n");
	  return -1;
	case ESC_CHAR:
	  modifier = 0x20;
	  continue;
	case FRAME_CHAR:
	  if (bp == buf)
	    continue;		/* Ignore multiple start frames.  */
	  if (csum != 0xff)	/* Checksum error.  */
	    return -1;
	  return bp - buf - 1;
	default:
	  c ^= modifier;
	  if (remote_debug)
	    printf_filtered ("%2.2x", c);
	  csum += c;
	  *bp++ = c;
	  break;
	}
      modifier = 0;
    }
}

/* Read a packet from the remote machine, with error checking,
   and store it in recv.buf.  
   If FOREVER, wait forever rather than timing out; this is used
   while the target is executing user code.  */
static int
getpkt (DScomm_t *const recv, const int forever)
{
  int c;
  int tries;
  int timeout;
  unsigned len;

  if (remote_debug)
    printf_unfiltered ("getpkt(%d)\n", forever);

  if (forever)
    {
      timeout = watchdog > 0 ? watchdog : -1;
    }
  else
    {
      timeout = current_session->timeout;
    }

  for (tries = 0; tries < MAX_RECV_TRIES; tries++)
    {
      /* This can loop forever if the remote side sends us characters
         continuously, but if it pauses, we'll get a zero from readchar
         because of timeout.  Then we'll count that as a retry.

         Note that we will only wait forever prior to the start of a packet.
         After that, we expect characters to arrive at a brisk pace.  They
         should show up within nto_timeout intervals.  */
      do
	{
	  c = readchar (timeout);

	  if (c == SERIAL_TIMEOUT)
	    {
	      /* Watchdog went off.  Kill the target.  */
	      if (forever && watchdog > 0)
		{
		  target_mourn_inferior ();
		  error ("Watchdog has expired.  Target detached.");
		}
	      puts_filtered ("Timed out.\n");
	      return -1;
	    }
	}
      while (c != FRAME_CHAR);

      /* We've found the start of a packet, now collect the data.  */
      len = read_frame (recv->buf, sizeof recv->buf);

      if (remote_debug)
	printf_filtered ("\n");

      if (len >= sizeof (struct DShdr))
	{
	  if (recv->pkt.hdr.channel)	/* If hdr.channel is not 0, then hdr.channel is supported.  */
	    current_session->channelrd = recv->pkt.hdr.channel;

	  if (remote_debug)
	    {
	      printf_unfiltered ("getpkt() - len %d, channelrd %d,", len,
				 current_session->channelrd);
	      switch (current_session->channelrd)
		{
		case SET_CHANNEL_DEBUG:
		  printf_unfiltered (" cmd = %d, subcmd = %d, mid = %d\n",
				     recv->pkt.hdr.cmd, recv->pkt.hdr.subcmd,
				     recv->pkt.hdr.mid);
		  break;
		case SET_CHANNEL_TEXT:
		  printf_unfiltered (" text message\n");
		  break;
		case SET_CHANNEL_RESET:
		  printf_unfiltered (" set_channel_reset\n");
		  break;
		default:
		  printf_unfiltered (" unknown channel!\n");
		  break;
		}
	    }
	  return len;
	}
      if (len >= 1)
	{
	  /* Packet too small to be part of the debug protocol,
	     must be a transport level command.  */
	  if (recv->buf[0] == SET_CHANNEL_NAK)
	    {
	      /* Our last transmission didn't make it - send it again.  */
	      current_session->channelrd = SET_CHANNEL_NAK;
	      return -1;
	    }
	  if (recv->buf[0] <= SET_CHANNEL_TEXT)
	    current_session->channelrd = recv->buf[0];

	  if (remote_debug)
	    {
	      printf_unfiltered ("set channelrd to %d\n",
				 current_session->channelrd);
	    }
	  --tries;		/* Doesn't count as a retry.  */
	  continue;
	}
      SEND_NAK;
    }

  /* We have tried hard enough, and just can't receive the packet.  Give up.  */
  printf_unfiltered ("Ignoring packet error, continuing...");
  return 0;
}

void
nto_send_init (DScomm_t *const tran, unsigned cmd, const unsigned subcmd, const unsigned chan)
{
  static unsigned char mid;

  gdb_assert (tran != NULL);

  nto_trace (2) ("    nto_send_init(cmd %d, subcmd %d)\n", cmd,
			 subcmd);

  if (gdbarch_byte_order (target_gdbarch) == BFD_ENDIAN_BIG)
    cmd |= DSHDR_MSG_BIG_ENDIAN;

  tran->pkt.hdr.cmd = cmd;	/* TShdr.cmd.  */
  tran->pkt.hdr.subcmd = subcmd;	/* TShdr.console.  */
  tran->pkt.hdr.mid = ((chan == SET_CHANNEL_DEBUG) ? mid++ : 0);	/* TShdr.spare1.  */
  tran->pkt.hdr.channel = chan;	/* TShdr.channel.  */
}


/* Send text to remote debug daemon - Pdebug.  */

void
nto_outgoing_text (char *buf, int nbytes)
{
  DScomm_t tran;

  TSMsg_text_t *msg;

  msg = (TSMsg_text_t *) & tran;

  msg->hdr.cmd = TSMsg_text;
  msg->hdr.console = 0;
  msg->hdr.spare1 = 0;
  msg->hdr.channel = SET_CHANNEL_TEXT;

  memcpy (msg->text, buf, nbytes);

  putpkt (&tran, nbytes + offsetof (TSMsg_text_t, text));
}


/* Display some text that came back across the text channel.  */

static int
nto_incoming_text (TSMsg_text_t *const text, const int len)
{
  int textlen;
  const size_t buf_sz = TS_TEXT_MAX_SIZE + 1;
  char buf[buf_sz];

  textlen = len - offsetof (TSMsg_text_t, text);
  if (textlen <= 0)
    return 0;

  switch (text->hdr.cmd)
    {
    case TSMsg_text:
      snprintf (buf, buf_sz, "%s", text->text);
      buf[textlen] = '\0';
      //ui_file_write (gdb_stdtarg, buf, textlen);
      fputs_unfiltered (buf, gdb_stdtarg);
      return 0;
    default:
      return -1;
    }
}


/* Send env. string. Send multipart if env string too long and
   our protocol version allows multipart env string. 

   Returns > 0 if successful, 0 on error.  */

static int
nto_send_env (const char *env)
{
  int len; /* Length including zero terminating char.  */
  int totlen = 0;
  DScomm_t tran, recv;

  gdb_assert (env != NULL);
  len = strlen (env) + 1;
  if (current_session->target_proto_minor >= 2)
    {
	while (len > DS_DATA_MAX_SIZE)
	  {
	    nto_send_init (&tran, DStMsg_env, DSMSG_ENV_SETENV_MORE,
			   SET_CHANNEL_DEBUG);
	    memcpy (tran.pkt.env.data, env + totlen,
		    DS_DATA_MAX_SIZE);
	    if (!nto_send_recv (&tran, &recv, offsetof (DStMsg_env_t, data) +
			   DS_DATA_MAX_SIZE, 1))
	      {
		/* An error occured.  */
		return 0;
	      }
	    len -= DS_DATA_MAX_SIZE;
	    totlen += DS_DATA_MAX_SIZE;
	  }
    }
  else if (len > DS_DATA_MAX_SIZE)
    {
      /* Not supported by this protocol version.  */
      printf_unfiltered
	("** Skipping env var \"%.40s .....\" <cont>\n", env);
      printf_unfiltered
	("** Protovers under 0.2 do not handle env vars longer than %d\n", 
	  DS_DATA_MAX_SIZE - 1);
      return 0;
    }
  nto_send_init (&tran, DStMsg_env, DSMSG_ENV_SETENV, SET_CHANNEL_DEBUG);
  memcpy (tran.pkt.env.data, env + totlen, len);
  return nto_send_recv (&tran, &recv, offsetof (DStMsg_env_t, data) + len, 1);
}


/* Send an argument to inferior. Unfortunately, DSMSG_ENV_ADDARG
   does not support multipart strings limiting the length
   of single argument to DS_DATA_MAX_SIZE.  */

static int
nto_send_arg (const char *arg)
{
  int len;
  DScomm_t tran, recv;

  gdb_assert (arg != NULL);

  len = strlen(arg) + 1;
  if (len > DS_DATA_MAX_SIZE)
    {
      printf_unfiltered ("Argument too long: %.40s...\n", arg);
      return 0;
    }
  nto_send_init (&tran, DStMsg_env, DSMSG_ENV_ADDARG, SET_CHANNEL_DEBUG);
  memcpy (tran.pkt.env.data, arg, len);
  return nto_send_recv (&tran, &recv, offsetof (DStMsg_env_t, data) + len, 1);
}

/* Send the command in tran.buf to the remote machine,
   and read the reply into recv.buf.  */

static unsigned
nto_send_recv (const DScomm_t *const tran, DScomm_t *const recv,
	       const unsigned len, const int report_errors)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  int rlen;
  unsigned tries;

  if (current_session->desc == NULL)
    {
      errno = ENOTCONN;
      return 0;
    }

  for (tries = 0;; tries++)
    {
      if (tries >= MAX_TRAN_TRIES)
	{
	  unsigned char err = DSrMsg_err;

	  printf_unfiltered ("Remote exhausted %d retries.\n", tries);
	  if (gdbarch_byte_order (target_gdbarch) == BFD_ENDIAN_BIG)
	    err |= DSHDR_MSG_BIG_ENDIAN;
	  recv->pkt.hdr.cmd = err;
	  recv->pkt.err.err = EIO;
	  recv->pkt.err.err = EXTRACT_SIGNED_INTEGER (&recv->pkt.err.err,
						      4, byte_order);
	  rlen = sizeof (recv->pkt.err);
	  break;
	}
      putpkt (tran, len);
      for (;;)
	{
	  rlen = getpkt (recv, 0);
	  if ((current_session->channelrd != SET_CHANNEL_TEXT)
	      || (rlen == -1))
	    break;
	  nto_incoming_text (&recv->text, rlen);
	}
      if (rlen == -1)		/* Getpkt returns -1 if MsgNAK received.  */
	{
	  printf_unfiltered ("MsgNak received - resending\n");
	  continue;
	}
      if ((rlen >= 0) && (recv->pkt.hdr.mid == tran->pkt.hdr.mid))
	break;

      nto_trace (1) ("mid mismatch!\n");

    }
  /* Getpkt() sets channelrd to indicate where the message came from.
     now we switch on the channel (/type of message) and then deal
     with it.  */
  switch (current_session->channelrd)
    {
    case SET_CHANNEL_DEBUG:
      if (((recv->pkt.hdr.cmd & DSHDR_MSG_BIG_ENDIAN) != 0))
	{
	  char buff[sizeof(tran->buf)];

	  sprintf (buff, "set endian big");
	  if (gdbarch_byte_order (target_gdbarch) != BFD_ENDIAN_BIG)
	    execute_command (buff, 0);
	}
      else
	{
	  char buff[sizeof(tran->buf)];

	  sprintf (buff, "set endian little");
	  if (gdbarch_byte_order (target_gdbarch) != BFD_ENDIAN_LITTLE)
	    execute_command (buff, 0);
	}
      recv->pkt.hdr.cmd &= ~DSHDR_MSG_BIG_ENDIAN;
      if (recv->pkt.hdr.cmd == DSrMsg_err)
	{
	  errno = errnoconvert (EXTRACT_SIGNED_INTEGER (&recv->pkt.err.err, 4,
							byte_order));
	  if (report_errors)
	    {
	      switch (recv->pkt.hdr.subcmd)
		{
		case PDEBUG_ENOERR:
		  break;
		case PDEBUG_ENOPTY:
		  perror_with_name ("Remote (no ptys available)");
		  break;
		case PDEBUG_ETHREAD:
		  perror_with_name ("Remote (thread start error)");
		  break;
		case PDEBUG_ECONINV:
		  perror_with_name ("Remote (invalid console number)");
		  break;
		case PDEBUG_ESPAWN:
		  perror_with_name ("Remote (spawn error)");
		  break;
		case PDEBUG_EPROCFS:
		  perror_with_name ("Remote (procfs [/proc] error)");
		  break;
		case PDEBUG_EPROCSTOP:
		  perror_with_name ("Remote (devctl PROC_STOP error)");
		  break;
		case PDEBUG_EQPSINFO:
		  perror_with_name ("Remote (psinfo error)");
		  break;
		case PDEBUG_EQMEMMODEL:
		  perror_with_name
		    ("Remote (invalid memory model [not flat] )");
		  break;
		case PDEBUG_EQPROXY:
		  perror_with_name ("Remote (proxy error)");
		  break;
		case PDEBUG_EQDBG:
		  perror_with_name ("Remote (__nto_debug_* error)");
		  break;
		default:
		  perror_with_name ("Remote");
		}
	    }
	}
      break;
    case SET_CHANNEL_TEXT:
    case SET_CHANNEL_RESET:
      break;
    }
  return rlen;
}

static int
set_thread (const int th)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  DScomm_t tran, recv;

  nto_trace (0) ("set_thread(th %d pid %d, prev tid %ld)\n", th,
	 ptid_get_pid (inferior_ptid), ptid_get_tid (inferior_ptid));

  nto_send_init (&tran, DStMsg_select, DSMSG_SELECT_SET, SET_CHANNEL_DEBUG);
  tran.pkt.select.pid = ptid_get_pid (inferior_ptid);
  tran.pkt.select.pid = EXTRACT_SIGNED_INTEGER ((gdb_byte*)&tran.pkt.select.pid, 4,
						byte_order);
  tran.pkt.select.tid = EXTRACT_SIGNED_INTEGER (&th, 4, byte_order);
  nto_send_recv (&tran, &recv, sizeof (tran.pkt.select), 1);

  if (recv.pkt.hdr.cmd == DSrMsg_err)
    {
      nto_trace (0) ("Thread %d does not exist\n", th);
      return 0;
    }

  return 1;
}


/* Return nonzero if the thread TH is still alive on the remote system.  
   RECV will contain returned_tid. NOTE: Make sure this stays like that
   since we will use this side effect in other functions to determine
   first thread alive (for example, after attach).  */
static int
nto_thread_alive (struct target_ops *ops, ptid_t th)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  int alive = 0;
  DScomm_t tran, recv;

  nto_trace (0) ("nto_thread_alive -- pid %d, tid %ld \n",
		 ptid_get_pid (th), ptid_get_tid (th));

  nto_send_init (&tran, DStMsg_select, DSMSG_SELECT_QUERY, SET_CHANNEL_DEBUG);
  tran.pkt.select.pid = ptid_get_pid (th);
  tran.pkt.select.pid = EXTRACT_SIGNED_INTEGER (&tran.pkt.select.pid, 4,
						byte_order);
  tran.pkt.select.tid = ptid_get_tid (th);
  tran.pkt.select.tid = EXTRACT_SIGNED_INTEGER (&tran.pkt.select.tid, 4,
						byte_order);
  nto_send_recv (&tran, &recv, sizeof (tran.pkt.select), 0);
  if (recv.pkt.hdr.cmd == DSrMsg_okdata)
    {
      /* Data is tidinfo. 
	Note: tid returned might not be the same as requested.
	If it is not, then requested thread is dead.  */
      uintptr_t ptidinfoaddr = (uintptr_t) &recv.pkt.okdata.data;
      struct tidinfo *ptidinfo = (struct tidinfo *) ptidinfoaddr;
      int returned_tid = EXTRACT_SIGNED_INTEGER (&ptidinfo->tid, 2,
						 byte_order);
      alive = (ptid_get_tid (th) == returned_tid) && ptidinfo->state;
    }
  else if (recv.pkt.hdr.cmd == DSrMsg_okstatus)
    {
      /* This is the old behaviour. It doesn't really tell us
      what is the status of the thread, but rather answers question:
      "Does the thread exist?". Note that a thread might have already
      exited but has not been joined yet; we will show it here as 
      alive an well. Not completely correct.  */
      int returned_tid = EXTRACT_SIGNED_INTEGER (&recv.pkt.okstatus.status, 4,
						 byte_order);
      alive = (ptid_get_tid (th) == returned_tid);
    }

  nto_trace (0) ("Thread %lu is alive = %d\n", ptid_get_tid (th), alive);
  /* In case of a failure, return 0. This will happen when requested
    thread is dead and there is no alive thread with the larger tid.  */
  return alive;
}

static ptid_t
nto_get_thread_alive (struct target_ops *ops, ptid_t th)
{
  DScomm_t tran, recv;
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  int returned_tid;

  nto_send_init (&tran, DStMsg_select, DSMSG_SELECT_QUERY, SET_CHANNEL_DEBUG);
  tran.pkt.select.pid = ptid_get_pid (th);
  tran.pkt.select.pid = EXTRACT_SIGNED_INTEGER (&tran.pkt.select.pid, 4,
						byte_order);
  tran.pkt.select.tid = ptid_get_tid (th);
  tran.pkt.select.tid = EXTRACT_SIGNED_INTEGER (&tran.pkt.select.tid, 4,
						byte_order);
  nto_send_recv (&tran, &recv, sizeof (tran.pkt.select), 0);

  if (recv.pkt.hdr.cmd == DSrMsg_okdata)
    {
      /* Data is tidinfo. 
	Note: tid returned might not be the same as requested.
	If it is not, then requested thread is dead.  */
      const struct tidinfo *const ptidinfo = (struct tidinfo *) recv.pkt.okdata.data;
      returned_tid = EXTRACT_SIGNED_INTEGER (&ptidinfo->tid, 2,
					     byte_order);
      if (!nto_thread_alive (ops, ptid_build (PIDGET (th), 0, returned_tid)))
	{
	  return nto_get_thread_alive (ops, ptid_build (PIDGET (th), 0,
				       returned_tid+1));
	}
    }
  else if (recv.pkt.hdr.cmd == DSrMsg_okstatus)
    {
      /* This is the old behaviour. It doesn't really tell us
      what is the status of the thread, but rather answers question:
      "Does the thread exist?". Note that a thread might have already
      exited but has not been joined yet; we will show it here as 
      alive an well. Not completely correct.  */
      returned_tid = EXTRACT_SIGNED_INTEGER (&recv.pkt.okstatus.status,
					     4, byte_order);
    }
  else
    return minus_one_ptid;

 return ptid_build (ptid_get_pid (th), ptid_get_lwp (th), returned_tid);
}

/* Clean up connection to a remote debugger.  */
static int
nto_close_1 (char *dummy)
{
  DScomm_t tran, recv;

  nto_send_init (&tran, DStMsg_disconnect, 0, SET_CHANNEL_DEBUG);
  nto_send_recv (&tran, &recv, sizeof (tran.pkt.disconnect), 0);
  serial_close (current_session->desc);

  return 0;
}

static void
nto_close (int quitting)
{
  nto_trace (0) ("nto_close(quitting %d)\n", quitting);

  if (current_session->desc)
    {
      catch_errors ((catch_errors_ftype *) nto_close_1, NULL, "",
		    RETURN_MASK_ALL);
      current_session->desc = NULL;
      nto_remove_commands ();
    }
}


/* Reads procfs_info structure for the given process.

   Returns 1 on success, 0 otherwise.  */

static int
nto_read_procfsinfo (nto_procfs_info *pinfo)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  DScomm_t tran, recv;

  gdb_assert (pinfo != NULL && !! "pinfo must not be NULL\n");
  nto_send_init (&tran, DStMsg_procfsinfo, 0, SET_CHANNEL_DEBUG);
  tran.pkt.procfsinfo.pid = ptid_get_pid (inferior_ptid);
  tran.pkt.procfsinfo.pid = EXTRACT_SIGNED_INTEGER (&tran.pkt.procfsinfo.pid,
						    4, byte_order);
  nto_send_recv (&tran, &recv, sizeof (tran.pkt.procfsinfo), 0);
  if (recv.pkt.hdr.cmd == DSrMsg_okdata)
    {
      memcpy (pinfo, recv.pkt.okdata.data, sizeof (*pinfo));
      return 1;
    }
  else
    {
      nto_trace (0) ("DStMsg_procfsinfo not supported by the target.\n");
    }
  return 0;
}


/* Reads procfs_status structure for the given process.

   Returns 1 on success, 0 otherwise.  */

static int
nto_read_procfsstatus (nto_procfs_status *pstatus)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  DScomm_t tran, recv;

  gdb_assert (pstatus != NULL && !! "pstatus must not be NULL\n");
  nto_send_init (&tran, DStMsg_procfsstatus, 0, SET_CHANNEL_DEBUG);
  tran.pkt.procfsstatus.pid = ptid_get_pid (inferior_ptid);
  tran.pkt.procfsstatus.pid = EXTRACT_SIGNED_INTEGER (&tran.pkt.procfsstatus.pid,
						    4, byte_order);
  nto_send_recv (&tran, &recv, sizeof (tran.pkt.procfsstatus), 0);
  if (recv.pkt.hdr.cmd == DSrMsg_okdata)
    {
      memcpy (pstatus, recv.pkt.okdata.data, sizeof (*pstatus));
      return 1;
    }
  else
    {
      nto_trace (0) ("DStMsg_procfsstatus not supported by the target.\n");
    }
  return 0;
}


/* This is a 'hack' to reset internal state maintained by gdb. It is 
   unclear why it doesn't do it automatically, but the same hack can be
   seen in linux, so I guess it is o.k. to use it here too.  */
extern void nullify_last_target_wait_ptid (void);

static int
nto_start_remote (char *dummy)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  int orig_target_endian;
  DScomm_t tran, recv;

  nto_trace (0) ("nto_start_remote, (dummy %s)\n", dummy ? dummy : "(null)");

  immediate_quit = 1;		/* Allow user to interrupt it.  */
  for (;;)
    {
      orig_target_endian = (gdbarch_byte_order (target_gdbarch) == BFD_ENDIAN_BIG);

      /* Reset remote pdebug.  */
      SEND_CH_RESET;

      nto_send_init (&tran, DStMsg_connect, 0, SET_CHANNEL_DEBUG);

      tran.pkt.connect.major = HOST_QNX_PROTOVER_MAJOR;
      tran.pkt.connect.minor = HOST_QNX_PROTOVER_MINOR;

      nto_send_recv (&tran, &recv, sizeof (tran.pkt.connect), 0);

      if (recv.pkt.hdr.cmd != DSrMsg_err)
	break;
      if (orig_target_endian == (gdbarch_byte_order (target_gdbarch) == BFD_ENDIAN_BIG))
	break;
      /* Send packet again, with opposite endianness.  */
    }
  if (recv.pkt.hdr.cmd == DSrMsg_err)
    {
      error ("Connection failed: %ld.",
	     (long) EXTRACT_SIGNED_INTEGER (&recv.pkt.err.err, 4, byte_order));
    }
  /* NYI: need to size transmit/receive buffers to allowed size in connect response.  */
  immediate_quit = 0;

  printf_unfiltered ("Remote target is %s-endian\n",
		     (gdbarch_byte_order (target_gdbarch) ==
		      BFD_ENDIAN_BIG) ? "big" : "little");

  /* Try to query pdebug for their version of the protocol.  */
  nto_send_init (&tran, DStMsg_protover, 0, SET_CHANNEL_DEBUG);
  tran.pkt.protover.major = HOST_QNX_PROTOVER_MAJOR;
  tran.pkt.protover.minor = HOST_QNX_PROTOVER_MINOR;
  nto_send_recv (&tran, &recv, sizeof (tran.pkt.protover), 0);
  if ((recv.pkt.hdr.cmd == DSrMsg_err)
      && (EXTRACT_SIGNED_INTEGER (&recv.pkt.err.err, 4, byte_order)
	  == EINVAL))	/* Old pdebug protocol version 0.0.  */
    {
      current_session->target_proto_major = 0;
      current_session->target_proto_minor = 0;
    }
  else if (recv.pkt.hdr.cmd == DSrMsg_okstatus)
    {
      current_session->target_proto_major =
	EXTRACT_SIGNED_INTEGER (&recv.pkt.okstatus.status, 4, byte_order);
      current_session->target_proto_minor =
	EXTRACT_SIGNED_INTEGER (&recv.pkt.okstatus.status, 4, byte_order);
      current_session->target_proto_major =
	(current_session->target_proto_major >> 8) & DSMSG_PROTOVER_MAJOR;
      current_session->target_proto_minor =
	current_session->target_proto_minor & DSMSG_PROTOVER_MINOR;
    }
  else
    {
      error ("Connection failed (Protocol Version Query): %ld.",
	     (long) EXTRACT_SIGNED_INTEGER (&recv.pkt.err.err, 4, byte_order));
    }

  nto_trace (0) ("Pdebug protover %d.%d, GDB protover %d.%d\n",
			 current_session->target_proto_major,
			 current_session->target_proto_minor,
			 HOST_QNX_PROTOVER_MAJOR, HOST_QNX_PROTOVER_MINOR);

  nto_send_init (&tran, DStMsg_cpuinfo, 0, SET_CHANNEL_DEBUG);
  nto_send_recv (&tran, &recv, sizeof (tran.pkt.cpuinfo), 1);
  /* If we had an inferior running previously, gdb will have some internal
     states which we need to clear to start fresh.  */
  registers_changed ();
  nullify_last_target_wait_ptid ();
  inferior_ptid = null_ptid;
  if (recv.pkt.hdr.cmd == DSrMsg_err)
    {
      nto_cpuinfo_valid = 0;
    }
  else
    {
      struct dscpuinfo foo;
      memcpy (&foo, recv.pkt.okdata.data, sizeof (struct dscpuinfo));
      nto_cpuinfo_flags = EXTRACT_SIGNED_INTEGER (&foo.cpuflags, 4, byte_order);
      nto_cpuinfo_valid = 1;
    }

  return 1;
}

static void
nto_semi_init (void)
{
  DScomm_t tran, recv;

  nto_send_init (&tran, DStMsg_disconnect, 0, SET_CHANNEL_DEBUG);
  nto_send_recv (&tran, &recv, sizeof (tran.pkt.disconnect), 0);

  inferior_ptid = null_ptid;

  if (!catch_errors ((catch_errors_ftype *) nto_start_remote, (char *) 0,
		     "Couldn't establish connection to remote target\n",
		     RETURN_MASK_ALL))
    {
      reinit_frame_cache ();
      pop_target ();
      nto_trace (2) ("nto_semi_init() - pop_target\n");
    }
}

static int nto_open_interrupted = 0;

static void
nto_open_break (int signo)
{
  nto_trace(0)("SIGINT in serial open\n");
  nto_open_interrupted = 1;
}

/* Open a connection to a remote debugger.
   NAME is the filename used for communication.  */
static void
nto_open (char *name, int from_tty)
{
  int tries = 0;
  void (*ofunc) ();

  nto_trace (0) ("nto_open(name '%s', from_tty %d)\n", name,
			 from_tty);

  nto_open_interrupted = 0;
  if (name == 0)
    error
      ("To open a remote debug connection, you need to specify what serial\ndevice is attached to the remote system (e.g. /dev/ttya).");

  immediate_quit = 1;		/* Allow user to interrupt it.  */

  target_preopen (from_tty);
  unpush_target (&nto_ops);

  ofunc = signal(SIGINT, nto_open_break);

  while (tries < MAX_TRAN_TRIES && !nto_open_interrupted)
  {
    current_session->desc = serial_open (name);

    if (nto_open_interrupted)
      break;

    /* Give the target some time to come up. When we are connecting
       immediately after disconnecting from the remote, pdebug
       needs some time to start listening to the port. */
    if (!current_session->desc)
      {
        tries++;
        sleep (1);
      }
    else
        break;
  }

  signal(SIGINT, ofunc);

  if (nto_open_interrupted)
    {
      immediate_quit = 0;
      return;
    }

  if (!current_session->desc)
    {
      immediate_quit = 0;
      perror_with_name (name);
    }

  if (baud_rate != -1)
    {
      if (serial_setbaudrate (current_session->desc, baud_rate))
	{
	  immediate_quit = 0;
	  serial_close (current_session->desc);
	  perror_with_name (name);
	}
    }

  serial_raw (current_session->desc);

  /* If there is something sitting in the buffer we might take it as a
     response to a command, which would be bad.  */
  serial_flush_input (current_session->desc);

  if (from_tty)
    {
      puts_filtered ("Remote debugging using ");
      puts_filtered (name);
      puts_filtered ("\n");
    }
  push_target (&nto_ops);	/* Switch to using remote target now.  */
  nto_add_commands ();
  nto_trace (3) ("nto_open() - push_target\n");

  inferior_ptid = null_ptid;

  /* Start the remote connection; if error (0), discard this target.
     In particular, if the user quits, be sure to discard it
     (we'd be in an inconsistent state otherwise).  */
  if (!catch_errors ((catch_errors_ftype *) nto_start_remote, (char *) 0,
		     "Couldn't establish connection to remote target\n",
		     RETURN_MASK_ALL))
    {
      immediate_quit = 0;
      pop_target ();

      nto_trace (0) ("nto_open() - pop_target\n");
    }
  immediate_quit = 0;
}

/* Perform remote attach.
 *
 * Use caller provided recv as the reply may be used by the caller. */

static int
nto_attach_only (const pid_t pid, DScomm_t *const recv)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  DScomm_t tran;

  nto_send_init (&tran, DStMsg_attach, 0, SET_CHANNEL_DEBUG);
  tran.pkt.attach.pid = pid;
  tran.pkt.attach.pid = EXTRACT_SIGNED_INTEGER (&tran.pkt.attach.pid, 4,
						byte_order);
  nto_send_recv (&tran, recv, sizeof (tran.pkt.attach), 0);

  if (recv->pkt.hdr.cmd != DSrMsg_okdata)
    {
      error (_("Failed to attach"));
      return 0;
    }
  return 1;
}

/* Attaches to a process on the target side.  Arguments are as passed
   to the `attach' command by the user.  This routine can be called
   when the target is not on the target-stack, if the target_can_run
   routine returns 1; in that case, it must push itself onto the stack.
   Upon exit, the target should be ready for normal operations, and
   should be ready to deliver the status of the process immediately
   (without waiting) to an upcoming target_wait call.  */
static void
nto_attach (struct target_ops *ops, char *args, int from_tty)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  ptid_t ptid;
  struct inferior *inf;
  struct nto_inferior_data *inf_data;
  DScomm_t tran, *recv = &current_session->recv;

  if (!ptid_equal (inferior_ptid, null_ptid))
    nto_semi_init ();

  nto_trace (0) ("nto_attach(args '%s', from_tty %d)\n",
			 args ? args : "(null)", from_tty);

  if (!args)
    error_no_arg ("process-id to attach");

  ptid = pid_to_ptid (atoi (args));

  if (symfile_objfile != NULL)
    exec_file_attach (symfile_objfile->name, from_tty);

  if (from_tty)
    {
      printf_unfiltered ("Attaching to %s\n", target_pid_to_str (ptid));
      gdb_flush (gdb_stdout);
    }

  if (!nto_attach_only (ptid_get_pid (ptid), recv))
    return;

  /* Hack this in here, since we will bypass the notify.  */
  current_session->cputype =
    EXTRACT_SIGNED_INTEGER (&recv->pkt.notify.un.pidload.cputype, 2,
			    byte_order);
  current_session->cpuid =
    EXTRACT_SIGNED_INTEGER (&recv->pkt.notify.un.pidload.cpuid, 4,
			    byte_order);
#ifdef QNX_SET_PROCESSOR_TYPE
  QNX_SET_PROCESSOR_TYPE (current_session->cpuid);	/* For mips.  */
#endif
  /* Get thread info as well.  */
  //ptid = nto_get_thread_alive (ptid);
  inferior_ptid = ptid_build (EXTRACT_SIGNED_INTEGER (&recv->pkt.notify.pid, 4,
						      byte_order),
			      0,
			      EXTRACT_SIGNED_INTEGER (&recv->pkt.notify.tid, 4,
						      byte_order));
  inf = current_inferior ();
  inf->attach_flag = 1;

  /* Remove LD_LIBRARY_PATH. In the future, we should fetch
   * it from the target and setup correctly prepended with
   * QNX_TARGET/<CPU> */
  set_in_environ (inf->environment, "LD_LIBRARY_PATH", "");

  inferior_appeared (inf, ptid_get_pid (ptid));

  if (symfile_objfile == NULL)
    {
      const pid_t pid = ptid_get_pid (ptid);
      struct dspidlist *pidlist = (void *)recv->pkt.okdata.data;

      /* Look for the binary executable name */
      nto_send_init (&tran, DStMsg_pidlist, DSMSG_PIDLIST_SPECIFIC,
		     SET_CHANNEL_DEBUG);
      tran.pkt.pidlist.pid = EXTRACT_UNSIGNED_INTEGER (&pid, 4, byte_order);
      tran.pkt.pidlist.tid = 0;
      nto_send_recv (&tran, recv, sizeof (tran.pkt.pidlist), 0);
      if (only_session.recv.pkt.hdr.cmd == DSrMsg_okdata)
	{
	  exec_file_attach (pidlist->name, from_tty);
	}
    }

  /* Initalize thread list.  */
  nto_find_new_threads (ops);

 /* NYI: add symbol information for process.  */
  /* Turn the PIDLOAD into a STOPPED notification so that when gdb
     calls nto_wait, we won't cycle around.  */
  recv->pkt.hdr.cmd = DShMsg_notify;
  recv->pkt.hdr.subcmd = DSMSG_NOTIFY_STOPPED;
  recv->pkt.notify.pid = ptid_get_pid (ptid);
  recv->pkt.notify.tid = ptid_get_tid (ptid);
  recv->pkt.notify.pid = EXTRACT_SIGNED_INTEGER (&recv->pkt.notify.pid, 4,
						 byte_order);
  recv->pkt.notify.tid = EXTRACT_SIGNED_INTEGER (&recv->pkt.notify.tid, 4,
						 byte_order);

  inf_data = nto_inferior_data (inf);
  inf_data->has_execution = 1;
  inf_data->has_stack = 1;
  inf_data->has_registers = 1;
  inf_data->has_memory = 1;
}

static void
nto_post_attach (pid_t pid)
{
  nto_trace (0) ("%s pid:%d\n", __func__, pid);
#ifdef SOLIB_CREATE_INFERIOR_HOOK
  if (exec_bfd)
    SOLIB_CREATE_INFERIOR_HOOK (pid);
#endif
}

/* This takes a program previously attached to and detaches it.  After
   this is done, GDB can be used to debug some other program.  We
   better not have left any breakpoints in the target program or it'll
   die when it hits one.  */
static void
nto_detach (struct target_ops *ops, char *args, int from_tty)
{
  DScomm_t tran, recv;
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  struct inferior *inf;
  struct nto_inferior_data *inf_data;

  inf = current_inferior ();

  gdb_assert (inf != NULL);

  nto_trace (0) ("nto_detach(args '%s', from_tty %d)\n",
			 args ? args : "(null)", from_tty);

  if (from_tty)
    {
      char *exec_file = get_exec_file (0);
      if (exec_file == 0)
	exec_file = "";

      printf_unfiltered ("Detaching from program: %s %d\n", exec_file,
			 PIDGET (inferior_ptid));
      gdb_flush (gdb_stdout);
    }
  if (args)
    {
      int sig = gdb_signal_to_nto (target_gdbarch, atoi (args));

      nto_send_init (&tran, DStMsg_kill, 0, SET_CHANNEL_DEBUG);
      tran.pkt.kill.signo = EXTRACT_SIGNED_INTEGER (&sig, 4, byte_order);
      nto_send_recv (&tran, &recv, sizeof (tran.pkt.kill), 1);
    }

  nto_send_init (&tran, DStMsg_detach, 0, SET_CHANNEL_DEBUG);
  tran.pkt.detach.pid = PIDGET (inferior_ptid);
  tran.pkt.detach.pid = EXTRACT_SIGNED_INTEGER (&tran.pkt.detach.pid, 4, byte_order);
  nto_send_recv (&tran, &recv, sizeof (tran.pkt.detach), 1);
  nto_mourn_inferior (ops);
  inferior_ptid = null_ptid;

  inf_data = nto_inferior_data (inf);
  inf_data->has_execution = 0;
  inf_data->has_stack = 0;
  inf_data->has_registers = 0;
  inf_data->has_memory = 0;
}


/* Tell the remote machine to resume.  */
static void
nto_resume (struct target_ops *ops, ptid_t ptid, int step,
	    enum gdb_signal sig)
{
  DScomm_t tran, *const recv = &current_session->recv;
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  int signo;
  const int runone = ptid_get_tid (ptid) > 0;

  nto_trace (0) ("nto_resume(pid %d, tid %ld, step %d, sig %d)\n",
		 PIDGET (ptid), ptid_get_tid (ptid),
		 step, gdb_signal_to_nto (target_gdbarch, sig));

  if (ptid_equal (inferior_ptid, null_ptid))
    return;

  gdb_assert (ptid_get_pid (inferior_ptid) == current_inferior ()->pid);

  /* Select requested thread.  If minus_one_ptid is given, or selecting
     requested thread fails, select tid 1.  If tid 1 does not exist,
     first next available will be selected.  */
  if (!ptid_equal (ptid, minus_one_ptid))
    {
      ptid_t ptid_alive = nto_get_thread_alive (ops, ptid);

      /* If returned thread is minus_one_ptid, then requested thread is
	 dead and there are no alive threads with tid > ptid_get_tid (ptid).
	 Try with first alive with tid >= 1.  */
      if (ptid_equal (ptid_alive, minus_one_ptid))
	{
	  nto_trace (0) ("Thread %ld does not exist. Trying with tid >= 1\n",
			 ptid_get_tid (ptid));
	  ptid_alive = nto_get_thread_alive (ops, ptid_build (
					     ptid_get_pid (ptid), 0, 1));
	  nto_trace (1) ("First next tid found is: %ld\n", 
			 ptid_get_tid (ptid_alive));
	}
      if (!ptid_equal (ptid_alive, minus_one_ptid))
	{
	  if (!set_thread (ptid_get_tid (ptid_alive))) 
	    {
	      nto_trace (0) ("Failed to set thread: %ld\n", 
			     ptid_get_tid (ptid_alive));
	    }
	}
    }

  /* The HandleSig stuff is part of the new protover 0.1, but has not
     been implemented in all pdebugs that reflect that version.  If
     the HandleSig comes back with an error, then revert to protover 0.0
     behaviour, regardless of actual protover.
     The handlesig msg sends the signal to pass, and a char array
     'signals', which is the list of signals to notice.  */
  nto_send_init (&tran, DStMsg_handlesig, 0, SET_CHANNEL_DEBUG);
  tran.pkt.handlesig.sig_to_pass = gdb_signal_to_nto (target_gdbarch, sig);
  tran.pkt.handlesig.sig_to_pass =
    EXTRACT_SIGNED_INTEGER (&tran.pkt.handlesig.sig_to_pass, 4, byte_order);
  for (signo = 0; signo < QNXNTO_NSIG; signo++)
    {
      if (signal_stop_state (gdb_signal_from_nto (target_gdbarch,
						     signo)) == 0
	  && signal_print_state (gdb_signal_from_nto (target_gdbarch,
							 signo)) == 0
	  && signal_pass_state (gdb_signal_from_nto (target_gdbarch,
						        signo)) == 1)
	{
	  tran.pkt.handlesig.signals[signo] = 0;
	}
      else
	{
	  tran.pkt.handlesig.signals[signo] = 1;
	}
    }
  nto_send_recv (&tran, recv, sizeof (tran.pkt.handlesig), 0);
  if (recv->pkt.hdr.cmd == DSrMsg_err)
    if (sig != GDB_SIGNAL_0)
      {
	nto_send_init (&tran, DStMsg_kill, 0, SET_CHANNEL_DEBUG);
	tran.pkt.kill.signo = gdb_signal_to_nto (target_gdbarch, sig);
	tran.pkt.kill.signo =
	  EXTRACT_SIGNED_INTEGER (&tran.pkt.kill.signo, 4, byte_order);
	nto_send_recv (&tran, recv, sizeof (tran.pkt.kill), 1);
      }

  if (gdbarch_software_single_step_p (target_gdbarch))
    {
      /* Do not interfere with gdb logic. */
      nto_send_init (&tran, DStMsg_run, DSMSG_RUN,
		     SET_CHANNEL_DEBUG);
    }
  else
    {
      nto_send_init (&tran, DStMsg_run, (step || runone) ? DSMSG_RUN_COUNT
							 : DSMSG_RUN,
		     SET_CHANNEL_DEBUG);
      tran.pkt.run.step.count = 1;
      tran.pkt.run.step.count =
	EXTRACT_UNSIGNED_INTEGER (&tran.pkt.run.step.count, 4, byte_order);
    }
  nto_send_recv (&tran, recv, sizeof (tran.pkt.run), 1);
}

static void (*ofunc) ();
#ifndef __MINGW32__
static void (*ofunc_alrm) ();
#endif

/* Yucky but necessary globals used to track state in nto_wait() as a
   result of things done in nto_interrupt(), nto_interrupt_twice(),
   and nto_interrupt_retry().  */
static sig_atomic_t SignalCount = 0;	/* Used to track ctl-c retransmits.  */
static sig_atomic_t InterruptedTwice = 0;	/* Set in nto_interrupt_twice().  */
static sig_atomic_t WaitingForStopResponse = 0;	/* Set in nto_interrupt(), cleared in nto_wait().  */

#define QNX_TIMER_TIMEOUT 5
#define QNX_CTL_C_RETRIES 3

static void
nto_interrupt_retry (signo)
{
  SignalCount++;
  if (SignalCount >= QNX_CTL_C_RETRIES)	/* Retry QNX_CTL_C_RETRIES times after original transmission.  */
    {
      printf_unfiltered
	("CTL-C transmit - 3 retries exhausted.  Ending debug session.\n");
      WaitingForStopResponse = 0;
      SignalCount = 0;
      target_mourn_inferior ();
      deprecated_throw_reason (RETURN_QUIT);
    }
  else
    {
      nto_interrupt (SIGINT);
    }
}


/* Ask the user what to do when an interrupt is received.  */
static void
interrupt_query ()
{
  alarm (0);
  signal (SIGINT, ofunc);
#ifndef __MINGW32__
  signal (SIGALRM, ofunc_alrm);
#endif
  target_terminal_ours ();
  InterruptedTwice = 0;

  if (query
      ("Interrupted while waiting for the program.\n Give up (and stop debugging it)? "))
    {
      SignalCount = 0;
      target_mourn_inferior ();
      deprecated_throw_reason (RETURN_QUIT);
    }
  target_terminal_inferior ();
#ifndef __MINGW32__
  signal (SIGALRM, nto_interrupt_retry);
#endif
  signal (SIGINT, nto_interrupt_twice);
  alarm (QNX_TIMER_TIMEOUT);
}


/* The user typed ^C twice.  */
static void
nto_interrupt_twice (int signo)
{
  InterruptedTwice = 1;
}

/* Send ^C to target to halt it.  Target will respond, and send us a
   packet.  */

/* GP - Dec 21, 2000.  If the target sends a NotifyHost at the same time as
   GDB sends a DStMsg_stop, then we would get into problems as both ends
   would be waiting for a response, and not the sent messages.  Now, we put
   the pkt and set the global flag 'WaitingForStopResponse', and return.
   This then goes back to the the main loop in nto_wait() below where we
   now check against the debug message received, and handle both.
   All retries of the DStMsg_stop are handled via SIGALRM and alarm(timeout).  */
static void
nto_interrupt (int signo)
{
  DScomm_t tran;

  nto_trace (0) ("nto_interrupt(signo %d)\n", signo);

  /* If this doesn't work, try more severe steps.  */
  signal (signo, nto_interrupt_twice);
#ifndef __MINGW32__
  signal (SIGALRM, nto_interrupt_retry);
#endif

  WaitingForStopResponse = 1;

  nto_send_init (&tran, DStMsg_stop, DSMSG_STOP_PIDS, SET_CHANNEL_DEBUG);
  putpkt (&tran, sizeof (tran.pkt.stop));

  /* Set timeout.  */
  alarm (QNX_TIMER_TIMEOUT);
}

/* Wait until the remote machine stops, then return,
   storing status in STATUS just as `wait' would.
   Returns "pid".  */
static ptid_t
nto_wait (struct target_ops *ops,
	  ptid_t ptid, struct target_waitstatus *status, int i)
{
  DScomm_t *const recv = &current_session->recv;
  ptid_t returned_ptid = inferior_ptid;

  nto_trace (0) ("nto_wait pid %d, inferior pid %d tid %ld\n",
		 ptid_get_pid (ptid), ptid_get_pid (inferior_ptid),
		 ptid_get_tid (ptid));

  status->kind = TARGET_WAITKIND_STOPPED;
  status->value.sig = GDB_SIGNAL_0;

  if (ptid_equal (inferior_ptid, null_ptid))
    return null_ptid;

  nto_inferior_data (NULL)->stopped_flags = 0;

  gdb_assert (ptid_get_pid (inferior_ptid) == current_inferior ()->pid);

  if (recv->pkt.hdr.cmd != DShMsg_notify)
    {
      int len;
      char waiting_for_notify;

      waiting_for_notify = 1;
      SignalCount = 0;
      InterruptedTwice = 0;

      ofunc = (void (*)()) signal (SIGINT, nto_interrupt);
#ifndef __MINGW32__
      ofunc_alrm = (void (*)()) signal (SIGALRM, nto_interrupt_retry);
#endif
      for (;;)
	{
	  len = getpkt (recv, 1);
	  if (len < 0)		/* Error - probably received MSG_NAK.  */
	    {
	      if (WaitingForStopResponse)
		{
		  /* We do not want to get SIGALRM while calling it's handler
		     the timer is reset in the handler.  */
		  alarm (0);
#ifndef __MINGW32__
		  nto_interrupt_retry (SIGALRM);
#else
		  nto_interrupt_retry (0);
#endif
		  continue;
		}
	      else
		{
		  /* Turn off the alarm, and reset the signals, and return.  */
		  alarm (0);
		  signal (SIGINT, ofunc);
#ifndef __MINGW32__
		  signal (SIGALRM, ofunc_alrm);
#endif
		  return null_ptid;
		}
	    }
	  if (current_session->channelrd == SET_CHANNEL_TEXT)
	    nto_incoming_text (&recv->text, len);
	  else			/* DEBUG CHANNEL.  */
	    {
	      recv->pkt.hdr.cmd &= ~DSHDR_MSG_BIG_ENDIAN;
	      /* If we have sent the DStMsg_stop due to a ^C, we expect
	         to get the response, so check and clear the flag
	         also turn off the alarm - no need to retry,
	         we did not lose the packet.  */
	      if ((WaitingForStopResponse) && (recv->pkt.hdr.cmd == DSrMsg_ok))
		{
		  WaitingForStopResponse = 0;
		  status->value.sig = GDB_SIGNAL_INT;
		  alarm (0);
		  if (!waiting_for_notify)
		    break;
		}
	      /* Else we get the Notify we are waiting for.  */
	      else if (recv->pkt.hdr.cmd == DShMsg_notify)
		{
		  DScomm_t tran;

		  waiting_for_notify = 0;
		  /* Send an OK packet to acknowledge the notify.  */
		  nto_send_init (&tran, DSrMsg_ok, recv->pkt.hdr.mid,
				 SET_CHANNEL_DEBUG);
		  tran.pkt.hdr.mid = recv->pkt.hdr.mid;
		  putpkt (&tran, sizeof (tran.pkt.ok));
		  /* Handle old pdebug protocol behavior, where out of order msgs get dropped
		     version 0.0 does this, so we must resend after a notify.  */
		  if ((current_session->target_proto_major == 0)
		      && (current_session->target_proto_minor == 0))
		    {
		      if (WaitingForStopResponse)
			{
			  alarm (0);

			  /* Change the command to something other than notify
			     so we don't loop in here again - leave the rest of
			     the packet alone for nto_parse_notify() below!!!  */
			  recv->pkt.hdr.cmd = DSrMsg_ok;
			  nto_interrupt (SIGINT);
			}
		    }
		  returned_ptid = nto_parse_notify (recv, ops, status);

		  if (!WaitingForStopResponse)
		    break;
		}
	    }
	}
      gdb_flush (gdb_stdtarg);
      gdb_flush (gdb_stdout);
      alarm (0);

      /* Hitting Ctl-C sends a stop request, a second ctl-c means quit, 
         so query here, after handling the results of the first ctl-c
         We know we were interrupted twice because the yucky global flag
         'InterruptedTwice' is set in the handler, and cleared in
         interrupt_query().  */
      if (InterruptedTwice)
	interrupt_query ();

      signal (SIGINT, ofunc);
#ifndef __MINGW32__
      signal (SIGALRM, ofunc_alrm);
#endif
    }

  recv->pkt.hdr.cmd = DSrMsg_ok;	/* To make us wait the next time.  */
  return returned_ptid;
}

static ptid_t
nto_parse_notify (const DScomm_t *const recv, struct target_ops *ops,
		  struct target_waitstatus *status)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  pid_t pid, tid;
  CORE_ADDR stopped_pc = 0;
  struct inferior *inf;
  struct nto_inferior_data *inf_data;

  inf = current_inferior ();

  gdb_assert (inf != NULL);

  inf_data = nto_inferior_data (inf);

  gdb_assert (inf_data != NULL);

  nto_trace (0) ("nto_parse_notify(status) - subcmd %d\n",
			 recv->pkt.hdr.subcmd);

  pid = EXTRACT_SIGNED_INTEGER (&recv->pkt.notify.pid, 4, byte_order);
  tid = EXTRACT_SIGNED_INTEGER (&recv->pkt.notify.tid, 4, byte_order);
  if (tid == 0)
    tid = 1;

  switch (recv->pkt.hdr.subcmd)
    {
    case DSMSG_NOTIFY_PIDUNLOAD:
      /* Added a new struct pidunload_v3 to the notify.un.  This includes a
         faulted flag so we can tell if the status value is a signo or an
         exit value.  See dsmsgs.h, protoverminor bumped to 3. GP Oct 31 2002.  */
      if ((current_session->target_proto_major == 0)
	  && (current_session->target_proto_minor >= 3))
	{
	  if (recv->pkt.notify.un.pidunload_v3.faulted)
	    {
	      status->value.integer =
		gdb_signal_from_nto
		  (target_gdbarch, EXTRACT_SIGNED_INTEGER
				    (&recv->pkt.notify.un.pidunload_v3.status,
				     4, byte_order));
	      if (status->value.integer)
		status->kind = TARGET_WAITKIND_SIGNALLED;	/* Abnormal death.  */
	      else
		status->kind = TARGET_WAITKIND_EXITED;	/* Normal death.  */
	    }
	  else
	    {
	      status->value.integer =
		EXTRACT_SIGNED_INTEGER (&recv->pkt.notify.un.pidunload_v3.
					status, 4, byte_order);
	      status->kind = TARGET_WAITKIND_EXITED;	/* Normal death, possibly with exit value.  */
	    }
	}
      else
	{
	  status->value.integer =
	    gdb_signal_from_nto (target_gdbarch, EXTRACT_SIGNED_INTEGER
				     (&recv->pkt.notify.un.pidunload.status,
				      4, byte_order));
	  if (status->value.integer)
	    status->kind = TARGET_WAITKIND_SIGNALLED;	/* Abnormal death.  */
	  else
	    status->kind = TARGET_WAITKIND_EXITED;	/* Normal death.  */
	  /* Current inferior is gone, switch to something else */

	}
      inf_data->has_execution = 0;
      inf_data->has_stack = 0;
      inf_data->has_registers = 0;
      inf_data->has_memory = 0;
      break;
    case DSMSG_NOTIFY_BRK:
      inf_data->stopped_flags =
	EXTRACT_UNSIGNED_INTEGER (&recv->pkt.notify.un.brk.flags, 4,
				  byte_order);
      stopped_pc = EXTRACT_UNSIGNED_INTEGER (&recv->pkt.notify.un.brk.ip,
					     4, byte_order);
      inf_data->stopped_pc = stopped_pc;
      /* NOTE: We do not have New thread notification. This will cause
	 gdb to think that breakpoint stop is really a new thread event if
	 it happens to be in a thread unknown prior to this stop.
	 We add new threads here to be transparent to the rest 
	 of the gdb.  */
      if (current_session->target_proto_major == 0 &&
	  current_session->target_proto_minor < 4)
	{
	  nto_find_new_threads (ops);
	}
      /* Fallthrough.  */
    case DSMSG_NOTIFY_STEP:
      /* NYI: could update the CPU's IP register here.  */
      status->kind = TARGET_WAITKIND_STOPPED;
      status->value.sig = GDB_SIGNAL_TRAP;
      break;
    case DSMSG_NOTIFY_SIGEV:
      status->kind = TARGET_WAITKIND_STOPPED;
      status->value.sig =
	gdb_signal_from_nto (target_gdbarch, EXTRACT_SIGNED_INTEGER
				 (&recv->pkt.notify.un.sigev.signo,
				  4, byte_order));
      break;
    case DSMSG_NOTIFY_PIDLOAD:
      current_session->cputype =
	EXTRACT_SIGNED_INTEGER (&recv->pkt.notify.un.pidload.cputype, 2,
				byte_order);
      current_session->cpuid =
	EXTRACT_SIGNED_INTEGER (&recv->pkt.notify.un.pidload.cpuid, 4,
				byte_order);
#ifdef QNX_SET_PROCESSOR_TYPE
      QNX_SET_PROCESSOR_TYPE (current_session->cpuid);	/* For mips.  */
#endif
      inf_data->has_execution = 1;
      inf_data->has_stack = 1;
      inf_data->has_registers = 1;
      inf_data->has_memory = 1;
      status->kind = TARGET_WAITKIND_LOADED;
      break;
    case DSMSG_NOTIFY_TIDLOAD:
      {
	struct thread_info *ti;
	struct private_thread_info *priv;

	status->kind = nto_stop_on_thread_events
			 ? TARGET_WAITKIND_STOPPED : TARGET_WAITKIND_SPURIOUS;
	status->value.sig = 0;
	tid = EXTRACT_UNSIGNED_INTEGER (&recv->pkt.notify.un.thread_event.tid,
					2, byte_order);
	nto_trace (0) ("New thread event: tid %d\n", tid);

#if 0
	    stopped_pc = EXTRACT_UNSIGNED_INTEGER (&recv->pkt.notify.un.brk.ip,
						   4, byte_order);

#endif

	priv = XCALLOC (1, struct private_thread_info);
	priv->tid = tid;
//    priv->starting_ip = stopped_pc;
	ti = add_thread_with_info (ptid_build (pid, 0, tid), priv);
      }
      break;
    case DSMSG_NOTIFY_TIDUNLOAD:
      {
	struct thread_info *ti;
	ptid_t cur = ptid_build (pid, 0, tid);
	const int tid_exited = EXTRACT_SIGNED_INTEGER
	  (&recv->pkt.notify.un.thread_event.tid, 4, byte_order);

	nto_trace (0) ("Thread destroyed: tid: %d active: %d\n", tid_exited,
		       tid);

	status->kind = nto_stop_on_thread_events
			 ? TARGET_WAITKIND_STOPPED : TARGET_WAITKIND_SPURIOUS;
	status->value.sig = 0;
	/* Must determine an alive thread for this to work. */
	if (!ptid_equal (inferior_ptid, cur))
	  {
	    switch_to_thread (cur);
	  }
      }
      break;
#ifdef _DEBUG_WHAT_VFORK
    case DSMSG_NOTIFY_FORK:
      {
	nto_trace (0) ("DSMSG_NOTIFY_FORK %d\n", recv->pkt.notify.un.fork_event.pid);
	inf_data->child_pid
	  = EXTRACT_SIGNED_INTEGER (&recv->pkt.notify.un.fork_event.pid, 4,
				    byte_order);
	nto_trace (0) ("inf data child pid: %d\n", inf_data->child_pid);
	status->value.related_pid = ptid_build (inf_data->child_pid, 0, 1);
	inf_data->vfork
	  = EXTRACT_SIGNED_INTEGER (&recv->pkt.notify.un.fork_event.vfork, 4,
				    byte_order)
	    & _DEBUG_WHAT_VFORK;
	status->kind = inf_data->vfork ? TARGET_WAITKIND_VFORKED
				       : TARGET_WAITKIND_FORKED;
	nto_trace (0) ("child_pid=%d\n", ptid_get_pid (status->value.related_pid));
      }
      break;
#endif
    case DSMSG_NOTIFY_DLLLOAD:
    case DSMSG_NOTIFY_DLLUNLOAD:
      status->kind = TARGET_WAITKIND_SPURIOUS;
      break;
    case DSMSG_NOTIFY_STOPPED:
      status->kind = TARGET_WAITKIND_STOPPED;
      break;
    default:
      warning ("Unexpected notify type %d", recv->pkt.hdr.subcmd);
      break;
    }
  nto_trace (0) ("nto_parse_notify: pid=%d, tid=%d ip=0x%s\n",
		 pid, tid, paddress (target_gdbarch, stopped_pc));
  return ptid_build (pid, 0, tid);
}

/* Fetch the regset, returning true if successful.  If supply is true,
   then supply these registers to gdb as well.  */
static int
fetch_regs (struct regcache *regcache, int regset, int supply)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  unsigned offset;
  int len;
  int rlen;
  DScomm_t tran, recv;

  len = nto_register_area (target_gdbarch, -1, regset, &offset);
  if (len < 1)
    return 0;

  nto_send_init (&tran, DStMsg_regrd, regset, SET_CHANNEL_DEBUG);
  tran.pkt.regrd.offset = 0;	/* Always get whole set.  */
  tran.pkt.regrd.size = EXTRACT_SIGNED_INTEGER (&len, 2,
						byte_order);

  rlen = nto_send_recv (&tran, &recv, sizeof (tran.pkt.regrd), 0);

  if (recv.pkt.hdr.cmd == DSrMsg_err)
    return 0;

/* FIXME: this i386 specific stuff should be taken out once we move
   to the new procnto which has a long enough structure to hold
   floating point registers.  */
  if (gdbarch_bfd_arch_info (target_gdbarch) != NULL
      && strcmp (gdbarch_bfd_arch_info (target_gdbarch)->arch_name, "i386")
	 == 0
      && regset == NTO_REG_FLOAT && rlen <= 128)
    return 0;		/* Trying to get x86 fpregs from an old proc.  */

  if (supply)
    nto_supply_regset (regcache, regset, recv.pkt.okdata.data);
  return 1;
}

/* Read register REGNO, or all registers if REGNO == -1, from the contents
   of REGISTERS.  */
void
nto_fetch_registers (struct target_ops *ops,
		     struct regcache *regcache, int regno)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  int regset;

  nto_trace (0) ("nto_fetch_registers(regcache %p ,regno %d)\n",
		 regcache, regno);

  if (ptid_equal (inferior_ptid, null_ptid))
    {
      nto_trace (0) ("ptid is null_ptid, can not fetch registers\n");
      return;
    }

  if (!set_thread (ptid_get_tid (inferior_ptid)))
    return;

  if (regno == -1)
    {				/* Get all regsets.  */
      for (regset = NTO_REG_GENERAL; regset < NTO_REG_END; regset++)
	{
	  fetch_regs (regcache, regset, 1);
	}
    }
  else
    {
      regset = nto_regset_id (regno);
      fetch_regs (regcache, regset, 1);
    }
}

/* Prepare to store registers.  Don't have to do anything.  */
static void
nto_prepare_to_store (struct regcache *regcache)
{
   nto_trace (0) ("nto_prepare_to_store()\n");
}


/* Store register REGNO, or all registers if REGNO == -1, from the contents
   of REGISTERS.  */
void
nto_store_registers (struct target_ops *ops,
		     struct regcache *regcache, int regno)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  int len, regset;
  unsigned int off;
  DScomm_t tran, recv;

  nto_trace (0) ("nto_store_registers(regno %d)\n", regno);

  if (ptid_equal (inferior_ptid, null_ptid))
    return;

  if (!set_thread (ptid_get_tid (inferior_ptid)))
    return;

  if (regno == -1)		/* Send them all.  */
    {
      for (regset = NTO_REG_GENERAL; regset < NTO_REG_END; regset++)
	{
	  len = nto_register_area (target_gdbarch, -1, regset, &off);
	  if (len < 1)
	    continue;

	  /* Fetch the regset and copy it to our outgoing data before we fill
	     it with gdb's registers.  This avoids the possibility of sending
	     garbage to the remote.  */
	  if (!fetch_regs (regcache, regset, 0))
	    continue;

	  memcpy (tran.pkt.regwr.data, recv.pkt.okdata.data,
		  sizeof (recv.pkt.okdata.data));

	  if (nto_regset_fill (regcache, regset, tran.pkt.regwr.data) == -1)
	    continue;

	  nto_send_init (&tran, DStMsg_regwr, regset, SET_CHANNEL_DEBUG);
	  tran.pkt.regwr.offset = 0;
	  nto_send_recv (&tran, &recv, offsetof (DStMsg_regwr_t, data) + len, 1);
	}
      return;
    }

  /* Only sending one register.  */
  regset = nto_regset_id (regno);
  len = nto_register_area (target_gdbarch, regno, regset, &off);
  if (len < 1)			/* Don't know about this register.  */
    return;

  nto_send_init (&tran, DStMsg_regwr, regset, SET_CHANNEL_DEBUG);
  tran.pkt.regwr.offset = EXTRACT_SIGNED_INTEGER (&off, 2,
						  byte_order);
  regcache_raw_collect (regcache, regno, tran.pkt.regwr.data);
  nto_send_recv (&tran, &recv, offsetof (DStMsg_regwr_t, data) + len, 1);
}

/* Use of the data cache *used* to be disabled because it loses for looking at
   and changing hardware I/O ports and the like.  Accepting `volatile'
   would perhaps be one way to fix it.  Another idea would be to use the
   executable file for the text segment (for all SEC_CODE sections?
   For all SEC_READONLY sections?).  This has problems if you want to
   actually see what the memory contains (e.g. self-modifying code,
   clobbered memory, user downloaded the wrong thing).

   Because it speeds so much up, it's now enabled, if you're playing
   with registers you turn it off (set remotecache 0).  */

/* Write memory data directly to the remote machine.
   This does not inform the data cache; the data cache uses this.
   MEMADDR is the address in the remote memory space.
   MYADDR is the address of the buffer in our space.
   LEN is the number of bytes.

   Returns number of bytes transferred, or 0 for error.  */
static int
nto_write_bytes (CORE_ADDR memaddr, const gdb_byte *myaddr, int len)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  long long addr;
  DScomm_t tran, recv;

  nto_trace (0) ("nto_write_bytes(to %s, from %p, len %d)\n",
		 paddress (target_gdbarch, memaddr), myaddr, len);

  /* NYI: need to handle requests bigger than largest allowed packet.  */
  nto_send_init (&tran, DStMsg_memwr, 0, SET_CHANNEL_DEBUG);
  addr = memaddr;
  tran.pkt.memwr.addr = EXTRACT_UNSIGNED_INTEGER (&addr, 8,
						  byte_order);
  memcpy (tran.pkt.memwr.data, myaddr, len);
  nto_send_recv (&tran, &recv, offsetof (DStMsg_memwr_t, data) + len, 0);

  switch (recv.pkt.hdr.cmd)
    {
    case DSrMsg_ok:
      return len;
    case DSrMsg_okstatus:
      return EXTRACT_SIGNED_INTEGER (&recv.pkt.okstatus.status, 4,
				     byte_order);
    }
  return 0;
}

/* Read memory data directly from the remote machine.
   This does not use the data cache; the data cache uses this.
   MEMADDR is the address in the remote memory space.
   MYADDR is the address of the buffer in our space.
   LEN is the number of bytes.

   Returns number of bytes transferred, or 0 for error.  */
static int
nto_read_bytes (CORE_ADDR memaddr, gdb_byte *myaddr, int len)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  int rcv_len, tot_len, ask_len;
  long long addr;

  if (remote_debug)
    {
      printf_unfiltered ("nto_read_bytes(from %s, to %p, len %d)\n",
			 paddress (target_gdbarch, memaddr), myaddr, len);
    }

  tot_len = rcv_len = ask_len = 0;

  /* NYI: Need to handle requests bigger than largest allowed packet.  */
  do
    {
      DScomm_t tran, recv;
      nto_send_init (&tran, DStMsg_memrd, 0, SET_CHANNEL_DEBUG);
      addr = memaddr + tot_len;
      tran.pkt.memrd.addr = EXTRACT_UNSIGNED_INTEGER (&addr, 8,
						      byte_order);
      ask_len =
	((len - tot_len) >
	 DS_DATA_MAX_SIZE) ? DS_DATA_MAX_SIZE : (len - tot_len);
      tran.pkt.memrd.size = EXTRACT_SIGNED_INTEGER (&ask_len, 2,
						    byte_order);
      rcv_len = nto_send_recv (&tran, &recv, sizeof (tran.pkt.memrd), 0) - sizeof (recv.pkt.hdr);
      if (rcv_len <= 0)
	break;
      if (recv.pkt.hdr.cmd == DSrMsg_okdata)
	{
	  memcpy (myaddr + tot_len, recv.pkt.okdata.data, rcv_len);
	  tot_len += rcv_len;
	}
      else
	break;
    }
  while (tot_len != len);

  return (tot_len);
}

static LONGEST
nto_xfer_partial (struct target_ops *ops, enum target_object object,
		  const char *annex, gdb_byte *readbuf,
		  const gdb_byte *writebuf, const ULONGEST offset,
		  const LONGEST len)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);

  if (object == TARGET_OBJECT_MEMORY)
    {
      if (readbuf != NULL)
	return nto_read_bytes (offset, readbuf, len);
      else if (writebuf != NULL)
	return nto_write_bytes (offset, writebuf, len);
    }
  else if (object == TARGET_OBJECT_AUXV
	   && readbuf)
    {
      /* For 32-bit architecture, size of auxv_t is 8 bytes.  */
      const unsigned int sizeof_auxv_t = 8;
      const unsigned int sizeof_tempbuf = 20 * sizeof_auxv_t;
      int tempread = 0;
      gdb_byte *tempbuf = alloca (sizeof_tempbuf);
      nto_procfs_info procfs_info;

      if (!tempbuf)
        return -1;

      /* We first try to read auxv using initial stack.  The problem is, older
         pdebug-s don't support reading procfs_info.  */

      if (nto_read_procfsinfo (&procfs_info))
	{
	  struct inferior *const inf = current_inferior ();
	  struct nto_inferior_data *inf_data;
	  struct nto_remote_inferior_data *inf_rdata;
	  CORE_ADDR initial_stack;

	  inf_rdata = nto_remote_inferior_data ();

	  if (inf_rdata->auxv == NULL)
	    {
	      const CORE_ADDR initial_stack
		= EXTRACT_SIGNED_INTEGER (&procfs_info.initial_stack,
					  sizeof_auxv_t, byte_order);

	      inf_rdata->auxv = xcalloc (1, sizeof_tempbuf);
	      tempread = nto_read_auxv_from_initial_stack (initial_stack,
							   inf_rdata->auxv,
							   sizeof_tempbuf);
	    }
	  else
	    {
	      tempread = sizeof_tempbuf;
	    }
	  tempbuf = inf_rdata->auxv;
	}
      else if (exec_bfd && exec_bfd->tdata.elf_obj_data != NULL
	       && exec_bfd->tdata.elf_obj_data->phdr != NULL)
	{
	  /* Fallback for older pdebug-s. They do not support
	     procfsinfo transfer, so we have to read auxv from
	     executable file.  */
	  unsigned int phdr = 0, phnum = 0;
	  gdb_byte *buff = readbuf;

	  /* Simply copy what we have in exec_bfd to the readbuf.  */
	  while (exec_bfd->tdata.elf_obj_data->phdr[phnum].p_type != PT_NULL)
	    {
	      if (exec_bfd->tdata.elf_obj_data->phdr[phnum].p_type == PT_PHDR)
		phdr = exec_bfd->tdata.elf_obj_data->phdr[phnum].p_vaddr;
	      phnum++;
	    }

	  /* Create artificial auxv, with AT_PHDR, AT_PHENT and AT_PHNUM
	     elements.  */
	  *(int*)buff = AT_PHNUM;
	  *(int*)buff = extract_signed_integer (buff, sizeof (int),
						byte_order);
	  buff += 4;
	  *(int*)buff = EXTRACT_SIGNED_INTEGER (&phnum,
						sizeof (phnum),
						byte_order);
	  buff += 4;

	  *(int*)buff = AT_PHENT;
	  *(int*)buff = extract_signed_integer (buff, sizeof (int),
						byte_order);
	  buff += 4;
	  *(int*)buff = 0x20; /* for Elf32 */
	  *(int*)buff = extract_signed_integer (buff, sizeof (int),
						byte_order);
	  buff += 4;

	  *(int*)buff = AT_PHDR;
	  *(int*)buff = extract_signed_integer (buff, sizeof (int),
						byte_order);
	  buff += 4;
	  *(int*)buff = phdr;
	  *(int*)buff = extract_signed_integer (buff, sizeof (int),
						byte_order);
	  buff += 4;
	  tempread = (int)(buff - readbuf);
	}
      tempread = min (tempread, len) - offset;
      memcpy (readbuf, tempbuf + offset, tempread);
      return tempread;
    }  /* TARGET_OBJECT_AUXV */
  else if (object == TARGET_OBJECT_SIGNAL_INFO
	   && readbuf)
    {
      nto_procfs_status status;
      nto_siginfo_t siginfo;
      LONGEST mylen = len;

      if ((offset + mylen) > sizeof (nto_siginfo_t))
	{
	  if (offset < sizeof (nto_siginfo_t))
	    mylen = sizeof (nto_siginfo_t) - offset;
	  else
	    return 0;
	}

      if (!nto_read_procfsstatus (&status))
	return 0;

      // does byte order translation
      nto_get_siginfo_from_procfs_status (&status, &siginfo);
      memcpy (readbuf, (gdb_byte *)&siginfo + offset, mylen);
      return len;
    }
  if (ops->beneath && ops->beneath->to_xfer_partial)
    return ops->beneath->to_xfer_partial (ops, object, annex, readbuf,
					  writebuf, offset, len);
  return -1;
}

static void
nto_files_info (struct target_ops *ignore)
{
  nto_trace (0) ("nto_files_info(ignore %p)\n", ignore);

  puts_filtered ("Debugging a target over a serial line.\n");
}


static int
nto_kill_1 (char *dummy)
{
  DScomm_t tran, recv;
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  const pid_t pid = PIDGET (inferior_ptid);

  nto_trace (0) ("nto_kill_1(dummy %p)\n", dummy);

  if (!ptid_equal (inferior_ptid, null_ptid))
    {
      nto_send_init (&tran, DStMsg_kill, DSMSG_KILL_PID, SET_CHANNEL_DEBUG);
      tran.pkt.kill.signo = 9;	/* SIGKILL  */
      tran.pkt.kill.signo = EXTRACT_SIGNED_INTEGER (&tran.pkt.kill.signo,
						    4, byte_order);
      nto_send_recv (&tran, &recv, sizeof (tran.pkt.kill), 0);
#if 0
      nto_send_init (&tran, DStMsg_detach, 0, SET_CHANNEL_DEBUG);
      tran.pkt.detach.pid = PIDGET (inferior_ptid);
      tran.pkt.detach.pid = EXTRACT_SIGNED_INTEGER (&tran.pkt.detach.pid,
						    4, byte_order);
      nto_send_recv (&tran, &recv, sizeof (tran.pkt.detach), 1);
#endif
    }

  return 0;
}

static void
nto_kill (struct target_ops *ops)
{
  char *cmd = "continue";
  int steps = 5;
  struct target_waitstatus wstatus;
  ptid_t ptid;

  nto_trace (0) ("nto_kill()\n");

  remove_breakpoints ();
  get_last_target_status (&ptid, &wstatus);

  /* Use catch_errors so the user can quit from gdb even when we aren't on
     speaking terms with the remote system.  */
  catch_errors
      ((catch_errors_ftype *) nto_kill_1, (char *) 0, "", RETURN_MASK_ERROR);

  nto_mourn_inferior (ops);

  return;
}

static void
nto_mourn_inferior (struct target_ops *ops)
{
  DScomm_t tran, recv;
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  const pid_t pid = PIDGET (inferior_ptid);
  struct inferior *inf = current_inferior ();
  struct nto_inferior_data *inf_data;
  struct nto_remote_inferior_data *inf_rdata;

  gdb_assert (inf != NULL);

  inf_data = nto_inferior_data (inf);

  gdb_assert (inf_data != NULL);

  inf_rdata = nto_remote_inferior_data ();

  nto_trace (0) ("nto_mourn_inferior()\n");

  xfree (inf_rdata->auxv);
  inf_rdata->auxv = NULL;

  nto_send_init (&tran, DStMsg_detach, 0, SET_CHANNEL_DEBUG);
  tran.pkt.detach.pid = PIDGET (inferior_ptid);
  tran.pkt.detach.pid = EXTRACT_SIGNED_INTEGER (&tran.pkt.detach.pid,
						4, byte_order);
  nto_send_recv (&tran, &recv, sizeof (tran.pkt.detach), 1);

  generic_mourn_inferior ();
  delete_inferior (pid);
  inferior_ptid = null_ptid;
  inf_data->has_execution = 0;
  inf_data->has_stack = 0;
  inf_data->has_registers = 0;
  inf_data->has_memory = 0;
}

static int
nto_fd_raw (int fd)
{
#ifndef __MINGW32__
  struct termios termios_p;

  if (tcgetattr (fd, &termios_p))
    return (-1);

  termios_p.c_cc[VMIN] = 1;
  termios_p.c_cc[VTIME] = 0;
  termios_p.c_lflag &= ~(ECHO | ICANON | ISIG | ECHOE | ECHOK | ECHONL);
  termios_p.c_oflag &= ~(OPOST);
  return (tcsetattr (fd, TCSADRAIN, &termios_p));
#else
  return 0;
#endif
}

static void
nto_create_inferior (struct target_ops *ops, char *exec_file, char *args,
		     char **env, int from_tty)
{
  DScomm_t tran, recv;
  unsigned argc;
  unsigned envc;
  char **start_argv, **argv, **pargv,  *p;
  int fd;
  struct target_waitstatus status;
  const char *in, *out, *err;
  int errors = 0;
  struct inferior *const inf = current_inferior ();
  struct nto_inferior_data *inf_data;
  struct nto_remote_inferior_data *inf_rdata;

  gdb_assert (inf != NULL);

  inf_data = nto_inferior_data (inf);

  gdb_assert (inf_data != NULL);

  inf_rdata = nto_remote_inferior_data ();

  gdb_assert (inf_rdata != NULL);

  inf_data->stopped_flags = 0;

  remove_breakpoints ();

  if (inf_rdata->remote_exe == NULL && current_session->remote_exe != NULL)
    {
      inf_rdata->remote_exe = xstrdup (current_session->remote_exe);
    }

  if (inf_rdata->remote_cwd == NULL && current_session->remote_cwd != NULL)
    {
      inf_rdata->remote_cwd = xstrdup (current_session->remote_cwd);
    }

  if (inf_rdata->remote_exe && inf_rdata->remote_exe[0] != '\0')
    {
      exec_file = inf_rdata->remote_exe;
      fprintf_unfiltered (gdb_stdout, "Remote: %s\n", exec_file);
    }

  if (current_session->desc == NULL)
    nto_open ("pty", 0);

  if (!ptid_equal (inferior_ptid, null_ptid))
    nto_semi_init ();

  nto_trace (0) ("nto_create_inferior(exec_file '%s', args '%s', environ)\n",
			 exec_file ? exec_file : "(null)",
			 args ? args : "(null)");

  nto_send_init (&tran, DStMsg_env, DSMSG_ENV_CLEARENV, SET_CHANNEL_DEBUG);
  nto_send_recv (&tran, &recv, sizeof (DStMsg_env_t), 1);

  if (!current_session->inherit_env)
    {
      for (envc = 0; *env; env++, envc++)
	errors += !nto_send_env (*env);
      if (errors)
	warning ("Error(s) occured while sending environment variables.\n");
    }

  if (inf_rdata->remote_cwd != NULL)
    {
      nto_send_init (&tran, DStMsg_cwd, DSMSG_CWD_SET, SET_CHANNEL_DEBUG);
      strcpy ((char *)tran.pkt.cwd.path, inf_rdata->remote_cwd);
      nto_send_recv (&tran, &recv, offsetof (DStMsg_cwd_t, path)
		+ strlen ((const char *)tran.pkt.cwd.path) + 1, 1);
    }

  nto_send_init (&tran, DStMsg_env, DSMSG_ENV_CLEARARGV, SET_CHANNEL_DEBUG);
  nto_send_recv (&tran, &recv, sizeof (DStMsg_env_t), 1);

  pargv = buildargv (args);
  if (pargv == NULL)
    malloc_failure (0);
  start_argv = nto_parse_redirection (pargv, &in, &out, &err);

  if (in[0])
    {
      if ((fd = open (in, O_RDONLY)) == -1)
	perror (in);
      else
	nto_fd_raw (fd);
    }

  if (out[0])
    {
      if ((fd = open (out, O_WRONLY)) == -1)
	perror (out);
      else
	nto_fd_raw (fd);
    }

  if (err[0])
    {
      if ((fd = open (err, O_WRONLY)) == -1)
	perror (err);
      else
	nto_fd_raw (fd);
    }

  in = "";
  out = "";
  err = "";
  argc = 0;
  if (exec_file != NULL)
    {
      errors = !nto_send_arg (exec_file);
      /* Send it twice - first as cmd, second as argv[0]. */
      if (!errors)
	errors = !nto_send_arg (exec_file);

      if (errors)
	{
	  error ("Failed to send executable file name.\n");
	  goto freeargs;
	}
    }
  else if (*start_argv == NULL)
    {
      error ("No executable specified.");
      errors = 1;
      goto freeargs;
    }
  else
    {
      /* Send arguments (starting from index 1, argv[0] has already been
         sent above. */
      if (symfile_objfile != NULL)
	exec_file_attach (symfile_objfile->name, 0);

      exec_file = *start_argv;

      errors = !nto_send_arg (*start_argv);

      if (errors)
	{
	  error ("Failed to send argument.\n");
	  goto freeargs;
	}
    }

  errors = 0;
  for (argv = start_argv; *argv && **argv; argv++, argc++)
    {
      errors |= !nto_send_arg (*argv);
    }

  if (errors)
    {
      error ("Error(s) encountered while sending arguments.\n");
    }

freeargs:
  freeargv (pargv);
  free (start_argv);
  if (errors)
    return;

  /* NYI: msg too big for buffer.  */
  if (current_session->inherit_env)
    nto_send_init (&tran, DStMsg_load, DSMSG_LOAD_DEBUG | DSMSG_LOAD_INHERIT_ENV,
		   SET_CHANNEL_DEBUG);
  else
    nto_send_init (&tran, DStMsg_load, DSMSG_LOAD_DEBUG, SET_CHANNEL_DEBUG);

  p = tran.pkt.load.cmdline;

  tran.pkt.load.envc = 0;
  tran.pkt.load.argc = 0;

  strcpy (p, exec_file);
  p += strlen (p);
  *p++ = '\0';			/* load_file */

  strcpy (p, in);
  p += strlen (p);
  *p++ = '\0';			/* stdin */

  strcpy (p, out);
  p += strlen (p);
  *p++ = '\0';			/* stdout */

  strcpy (p, err);
  p += strlen (p);
  *p++ = '\0';			/* stderr */

  nto_send_recv (&tran, &recv, offsetof (DStMsg_load_t, cmdline) + p - tran.pkt.load.cmdline + 1,
	    1);
  /* Comes back as an DSrMsg_okdata, but it's really a DShMsg_notify. */
  if (recv.pkt.hdr.cmd == DSrMsg_okdata)
    {
      struct inferior *inf;

      inferior_ptid  = nto_parse_notify (&recv, ops, &status);
      inf = current_inferior ();
      inferior_appeared (inf, ptid_get_pid (inferior_ptid));
      add_thread_silent (inferior_ptid);
      inf->attach_flag = 1;
    }

  /* NYI: add the symbol info somewhere?  */
#ifdef SOLIB_CREATE_INFERIOR_HOOK
  if (exec_bfd)
    SOLIB_CREATE_INFERIOR_HOOK (pid);
#endif
}

static int
nto_insert_breakpoint (CORE_ADDR addr, gdb_byte *contents_cache)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  DScomm_t tran, recv;

  nto_trace (0) ("nto_insert_breakpoint(addr %s, contents_cache %p) pid:%d\n", 
                 paddress (target_gdbarch, addr), contents_cache,
		 ptid_get_pid (inferior_ptid));

  tran.pkt.brk.size = nto_breakpoint_size (addr);
  nto_send_init (&tran, DStMsg_brk, DSMSG_BRK_EXEC, SET_CHANNEL_DEBUG);
  tran.pkt.brk.addr = EXTRACT_UNSIGNED_INTEGER (&addr, 4,
						byte_order);
  nto_send_recv (&tran, &recv, sizeof (tran.pkt.brk), 0);
  if (recv.pkt.hdr.cmd == DSrMsg_err)
    {
      nto_trace (0) ("FAIL\n");
    }
  return recv.pkt.hdr.cmd == DSrMsg_err;
}

/* To be called from breakpoint.c through
  current_target.to_insert_breakpoint.  */

static int
nto_to_insert_breakpoint (struct gdbarch *gdbarch,
			  struct bp_target_info *bp_tg_inf)
{
  if (bp_tg_inf == 0)
    {
      internal_error(__FILE__, __LINE__, _("Target info invalid."));
    }

  /* Must select appropriate inferior.  Due to our pdebug protocol,
     the following looks convoluted.  But in reality all we are doing is
     making sure pdebug selects an existing thread in the inferior_ptid.
     We need to swithc pdebug internal current prp pointer.   */
  if (!set_thread (
        ptid_get_tid (nto_get_thread_alive (NULL, pid_to_ptid (
				        PIDGET (inferior_ptid))))))
    {
      nto_trace (0) ("Could not set (pid,tid):(%d,%ld)\n",
		     PIDGET (inferior_ptid), ptid_get_tid (inferior_ptid));
      return 0;
    }

  return nto_insert_breakpoint (bp_tg_inf->placed_address,
				bp_tg_inf->shadow_contents);
}


static int
nto_remove_breakpoint (CORE_ADDR addr, gdb_byte *contents_cache)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  DScomm_t tran, recv;

  nto_trace (0)	("nto_remove_breakpoint(addr %s, contents_cache %p) (pid %d)\n",
                 paddress (target_gdbarch, addr), contents_cache,
		 ptid_get_pid (inferior_ptid));


  /* This got changed to send DSMSG_BRK_EXEC with a size of -1
     nto_send_init(DStMsg_brk, DSMSG_BRK_REMOVE, SET_CHANNEL_DEBUG).  */
  nto_send_init (&tran, DStMsg_brk, DSMSG_BRK_EXEC, SET_CHANNEL_DEBUG);
  tran.pkt.brk.addr = EXTRACT_UNSIGNED_INTEGER (&addr, 4,
						byte_order);
  tran.pkt.brk.size = -1;
  tran.pkt.brk.size = EXTRACT_SIGNED_INTEGER (&tran.pkt.brk.size,
					      4, byte_order);
  nto_send_recv (&tran, &recv, sizeof (tran.pkt.brk), 0);
  if (recv.pkt.hdr.cmd == DSrMsg_err)
    {
      nto_trace (0) ("FAIL\n");
    }
  return recv.pkt.hdr.cmd == DSrMsg_err;
}

static int
nto_to_remove_breakpoint (struct gdbarch *gdbarch,
			  struct bp_target_info *bp_tg_inf)
{
  nto_trace (0) ("%s ( bp_tg_inf=%p )\n", __func__, bp_tg_inf);

  if (bp_tg_inf == 0)
    {
      internal_error (__FILE__, __LINE__, _("Target info invalid."));
    }

  /* Must select appropriate inferior.  Due to our pdebug protocol,
     the following looks convoluted.  But in reality all we are doing is
     making sure pdebug selects an existing thread in the inferior_ptid.
     We need to swithc pdebug internal current prp pointer.   */
  if (!set_thread (
        ptid_get_tid (nto_get_thread_alive (NULL, pid_to_ptid (
				        PIDGET (inferior_ptid))))))
    {
      nto_trace (0) ("Could not set (pid,tid):(%d,%ld)\n",
		     PIDGET (inferior_ptid), ptid_get_tid (inferior_ptid));
      return 0;
    }

  return nto_remove_breakpoint (bp_tg_inf->placed_address,
				bp_tg_inf->shadow_contents);
}

#if defined(__CYGWIN__) || defined(__MINGW32__)
static void
slashify (char *buf)
{
  int i = 0;
  while (buf[i])
    {
      /* Not sure why we would want to leave an escaped '\', but seems
         safer.  */
      if (buf[i] == '\\')
	{
	  if (buf[i + 1] == '\\')
	    i++;
	  else
	    buf[i] = '/';
	}
      i++;
    }
}
#endif

static void
upload_command (char *args, int fromtty)
{
#if defined(__CYGWIN__)
  char cygbuf[PATH_MAX];
#endif
  int fd;
  int len;
  char buf[DS_DATA_MAX_SIZE];
  char *from, *to;
  char **argv;
  char *filename_opened = NULL; //full file name. Things like $cwd will be expanded.
  // see source.c, openp and exec.c, file_command for more details.
  //
  struct inferior *inf = current_inferior ();
  struct nto_remote_inferior_data *inf_rdata;

  gdb_assert (inf != NULL);

  inf_rdata = nto_remote_inferior_data ();

  if (args == 0)
    {
      printf_unfiltered ("You must specify a filename to send.\n");
      return;
    }

#if defined(__CYGWIN__) || defined(__MINGW32__)
  /* We need to convert back slashes to forward slashes for DOS
     style paths, else buildargv will remove them.  */
  slashify (args);
#endif
  argv = buildargv (args);

  if (argv == NULL)
    malloc_failure (0);

  if (*argv == NULL)
    error (_("No source file name was specified"));

#if defined(__CYGWIN__)
  cygwin_conv_to_posix_path (argv[0], cygbuf);
  from = cygbuf;
#else
  from = argv[0];
#endif
  to = argv[1] ? argv[1] : from;

  from = tilde_expand (*argv);

  if ((fd = openp (NULL, OPF_TRY_CWD_FIRST, from,
                   O_RDONLY | O_BINARY, &filename_opened)) < 0)
    {
      printf_unfiltered ("Unable to open '%s': %s\n", from, strerror (errno));
      return;
    }

  nto_trace(0) ("Opened %s for reading\n", filename_opened);

  if (nto_fileopen (to, QNX_WRITE_MODE, QNX_WRITE_PERMS) == -1)
    {
      printf_unfiltered ("Remote was unable to open '%s': %s\n", to,
			 strerror (errno));
      close (fd);
      xfree (filename_opened);
      xfree (from);
      return;
    }

  while ((len = read (fd, buf, sizeof buf)) > 0)
    {
      if (nto_filewrite (buf, len) == -1)
	{
	  printf_unfiltered ("Remote was unable to complete write: %s\n",
			     strerror (errno));
	  goto exit;
	}
    }
  if (len == -1)
    {
      printf_unfiltered ("Local read failed: %s\n", strerror (errno));
      goto exit;
    }

  /* Everything worked so set remote exec file.  */
  if (upload_sets_exec)
    {
      xfree (inf_rdata->remote_exe);
      inf_rdata->remote_exe = xstrdup (to);
      if (only_session.remote_exe == NULL)
	only_session.remote_exe = xstrdup (to);
    }

exit:
  nto_fileclose (fd);
  xfree (filename_opened);
  xfree (from);
  close (fd);
}

static void
download_command (char *args, int fromtty)
{
#if defined(__CYGWIN__)
  char cygbuf[PATH_MAX];
#endif
  int fd;
  int len;
  char buf[DS_DATA_MAX_SIZE];
  char *from, *to;
  char **argv;

  if (args == 0)
    {
      printf_unfiltered ("You must specify a filename to get.\n");
      return;
    }

#if defined(__CYGWIN__) || defined(__MINGW32__)
  slashify (args);
#endif

  argv = buildargv (args);
  if (argv == NULL)
    malloc_failure (0);

  from = argv[0];
#if defined(__CYGWIN__)
  if (argv[1])
    {
      cygwin_conv_to_posix_path (argv[1], cygbuf);
      to = cygbuf;
    }
  else
    to = from;
#else
  to = argv[1] ? argv[1] : from;
#endif

  if ((fd = open (to, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666)) == -1)
    {
      printf_unfiltered ("Unable to open '%s': %s\n", to, strerror (errno));
      goto cleanup;
    }

  if (nto_fileopen (from, QNX_READ_MODE, 0) == -1)
    {
      printf_unfiltered ("Remote was unable to open '%s': %s\n", from,
			 strerror (errno));
      close (fd);
      goto cleanup;
    }

  while ((len = nto_fileread (buf, sizeof buf)) > 0)
    {
      if (write (fd, buf, len) == -1)
	{
	  printf_unfiltered ("Local write failed: %s\n", strerror (errno));
	  close (fd);
	  goto cleanup;
	}
    }

  if (len == -1)
    printf_unfiltered ("Remote read failed: %s\n", strerror (errno));
  nto_fileclose (fd);
  close (fd);

cleanup:
  freeargv (argv);
}

static void
nto_add_commands ()
{
  struct cmd_list_element *c;

  c =
    add_com ("upload", class_obscure, upload_command,
	     "Send a file to the target (upload {local} [{remote}])");
  set_cmd_completer (c, filename_completer);
  add_com ("download", class_obscure, download_command,
	   "Get a file from the target (download {remote} [{local}])");
}

static void
nto_remove_commands ()
{
  //extern struct cmd_list_element *cmdlist;

//  delete_cmd ("upload", &cmdlist);
// FIXME  delete_cmd ("download", &cmdlist);
}

static int nto_remote_fd = -1;

static int
nto_fileopen (char *fname, int mode, int perms)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  DScomm_t tran, recv;

  if (nto_remote_fd != -1)
    {
      printf_unfiltered
	("Remote file currently open, it must be closed before you can open another.\n");
      errno = EAGAIN;
      return -1;
    }

  nto_send_init (&tran, DStMsg_fileopen, 0, SET_CHANNEL_DEBUG);
  strcpy (tran.pkt.fileopen.pathname, fname);
  tran.pkt.fileopen.mode = EXTRACT_SIGNED_INTEGER (&mode, 4,
						   byte_order);
  tran.pkt.fileopen.perms = EXTRACT_SIGNED_INTEGER (&perms, 4,
						    byte_order);
  nto_send_recv (&tran, &recv, sizeof tran.pkt.fileopen, 0);

  if (recv.pkt.hdr.cmd == DSrMsg_err)
    {
      errno = errnoconvert (EXTRACT_SIGNED_INTEGER (&recv.pkt.err.err,
						    4, byte_order));
      return -1;
    }
  return nto_remote_fd = 0;
}

static void
nto_fileclose (int fd)
{
  DScomm_t tran, recv;

  if (nto_remote_fd == -1)
    return;

  nto_send_init (&tran, DStMsg_fileclose, 0, SET_CHANNEL_DEBUG);
  tran.pkt.fileclose.mtime = 0;
  nto_send_recv (&tran, &recv, sizeof tran.pkt.fileclose, 1);
  nto_remote_fd = -1;
}

static int
nto_fileread (char *buf, int size)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  int len;
  DScomm_t tran, recv;

  nto_send_init (&tran, DStMsg_filerd, 0, SET_CHANNEL_DEBUG);
  tran.pkt.filerd.size = EXTRACT_SIGNED_INTEGER (&size, 2,
						 byte_order);
  len = nto_send_recv (&tran, &recv, sizeof tran.pkt.filerd, 0);

  if (recv.pkt.hdr.cmd == DSrMsg_err)
    {
      errno = errnoconvert (recv.pkt.err.err);
      return -1;
    }

  len -= sizeof recv.pkt.okdata.hdr;
  memcpy (buf, recv.pkt.okdata.data, len);
  return len;
}

static int
nto_filewrite (char *buf, int size)
{
  int len, siz;
  DScomm_t tran, recv;

  for (siz = size; siz > 0; siz -= len, buf += len)
    {
      len =
	siz < sizeof tran.pkt.filewr.data ? siz : sizeof tran.pkt.filewr.data;
      nto_send_init (&tran, DStMsg_filewr, 0, SET_CHANNEL_DEBUG);
      memcpy (tran.pkt.filewr.data, buf, len);
      nto_send_recv (&tran, &recv, sizeof (tran.pkt.filewr.hdr) + len, 0);

      if (recv.pkt.hdr.cmd == DSrMsg_err)
	{
	  errno = errnoconvert (recv.pkt.err.err);
	  return size - siz;
	}
    }
  return size;
}

static int
nto_can_run (void)
{
  nto_trace (0) ("%s ()\n", __func__);
  return 0;
}

static int
nto_can_use_hw_breakpoint (int type, int cnt, int othertype)
{
  return 1;
}

static int
nto_has_registers (struct target_ops *ops)
{
  struct inferior *inf;

  if (!default_child_has_registers (ops)) return 0;
  inf = find_inferior_pid (ptid_get_pid (inferior_ptid));
  if (!inf) return 0;

  return nto_inferior_data (inf)->has_registers;
}

static int
nto_has_execution (struct target_ops *ops, ptid_t ptid)
{
  struct inferior *inf;

  if (!default_child_has_registers (ops)) return 0;
  inf = find_inferior_pid (ptid_get_pid (inferior_ptid));
  if (!inf) return 0;

  return nto_inferior_data (inf)->has_execution;
}

static int
nto_has_memory (struct target_ops *ops)
{
  struct inferior *inf;

  if (!default_child_has_registers (ops)) return 0;
  inf = find_inferior_pid (ptid_get_pid (inferior_ptid));
  if (!inf) return 0;

  return nto_inferior_data (inf)->has_memory;
}

static int
nto_has_stack (struct target_ops *ops)
{
  struct inferior *inf;

  if (!default_child_has_registers (ops)) return 0;
  inf = find_inferior_pid (ptid_get_pid (inferior_ptid));
  if (!inf) return 0;

  return nto_inferior_data (inf)->has_stack;
}

static int
nto_has_all_memory (struct target_ops *ops)
{
  struct inferior *inf;

  if (!default_child_has_registers (ops)) return 0;
  inf = find_inferior_pid (ptid_get_pid (inferior_ptid));
  if (!inf) return 0;

  return nto_inferior_data (inf)->has_memory;
}



/* GDB makes assumption that after VFORK/FORK we are attached to both.
   This is not the case on QNX but to make gdb behave, we do attach
   to both anyway, regardless of the scenario. */
static int
nto_follow_fork (struct target_ops *ops, int follow_child)
{
  struct thread_info *ti = find_thread_ptid (inferior_ptid);
  struct inferior *const inf = find_inferior_pid (ptid_get_pid (inferior_ptid));
  int child_pid, parent_pid;
  nto_trace (1) ("%s follow_child: %d\n", __func__, follow_child);

  gdb_assert (inf != NULL);


  child_pid = nto_inferior_data (inf)->child_pid;
  parent_pid = ptid_get_pid (inferior_ptid);

  nto_trace (0) ("Child pid: %d %cforked\n", child_pid,
		 nto_inferior_data (inf)->vfork ? 'v' : ' ');

  if (follow_child)
    {
      /* Attach to the child process, then detach from the parent. */
      struct inferior *parent_inf, *child_inf;
      struct nto_inferior_data *inf_data;
      DScomm_t recv;

      if (!nto_attach_only (child_pid, &recv))
	return 1;

      child_inf = add_inferior (child_pid);
      child_inf->attach_flag = 1;

      parent_inf = current_inferior ();

      if (nto_inferior_data (parent_inf)->vfork)
	{
	  child_inf->vfork_parent = parent_inf;
	  child_inf->pending_detach = 0;
	  parent_inf->vfork_child = child_inf;
	  parent_inf->pending_detach = detach_fork;
	  parent_inf->waiting_for_vfork_done = 0;

	  child_inf->pspace = parent_inf->pspace;
	  child_inf->aspace = parent_inf->pspace->aspace;

	  inferior_ptid = ptid_build (child_pid, 0, 1);
	  add_thread (inferior_ptid);
	}
      else
	{
	  if (detach_fork)
	    {
	      child_inf->pspace = parent_inf->pspace;
	      child_inf->aspace = parent_inf->pspace->aspace;
	      target_detach (NULL, 0);
	      inferior_ptid = ptid_build (child_pid, 0, 1);
	      add_thread (inferior_ptid);
	    }
	  else
	    {
	      inferior_ptid = ptid_build (child_pid, 0, 1);
	      add_thread (inferior_ptid);

	      child_inf->aspace = new_address_space ();
	      child_inf->pspace = add_program_space (child_inf->aspace);
	      child_inf->removable = 1;
	      set_current_program_space (child_inf->pspace);
	      clone_program_space (child_inf->pspace, parent_inf->pspace);
	      solib_create_inferior_hook (0);
	    }
	}

      inf_data = nto_inferior_data (child_inf);
      inf_data->has_execution = 1;
      inf_data->has_stack = 1;
      inf_data->has_registers = 1;
      inf_data->has_memory = 1;
    }
  else /* !follow_child */
    {
      struct nto_inferior_data *inf_data;

      if (!detach_fork)
	{
	  struct inferior *parent_inf, *child_inf;
	  const ptid_t old_inferior_ptid = inferior_ptid;
	  struct program_space *const old_program_space
	    = current_program_space;
	  DScomm_t recv;

	  nto_trace (0)("%s: parent, attach child\n", __func__);
	  if (nto_inferior_data (inf)->vfork)
	    {
	      nto_trace (0)("Can not attach to vforked child and not follow it\n");
	      return 0;
	    }

	  if (!nto_attach_only (child_pid, &recv))
	    {
	      error (_("Could not attach to %d\n"), child_pid);
	      return 0;
	    }

	  child_inf = add_inferior (child_pid);

	  parent_inf = current_inferior ();
	  child_inf->attach_flag = parent_inf->attach_flag;
	  copy_terminal_info (child_inf, parent_inf);

	  inferior_ptid = nto_get_thread_alive (ops, pid_to_ptid (child_pid));
	  add_thread (inferior_ptid);

	  if (nto_inferior_data (parent_inf)->vfork)
	    {
	      child_inf->vfork_parent = parent_inf;
	      child_inf->pending_detach = 0;
	      parent_inf->vfork_child = child_inf;
	      parent_inf->pending_detach = 0;
	    }
	  else
	    {
	      child_inf->aspace = new_address_space ();
	      child_inf->pspace = add_program_space (child_inf->aspace);
	      child_inf->removable = 1;
	      set_current_program_space (child_inf->pspace);
	      clone_program_space (child_inf->pspace, parent_inf->pspace);

	      solib_create_inferior_hook (0);
	    }
	  inf_data = nto_inferior_data (child_inf);
	  inf_data->has_execution = 1;
	  inf_data->has_stack = 1;
	  inf_data->has_registers = 1;
	  inf_data->has_memory = 1;

	  /* Restore */
	  set_current_program_space (old_program_space);
	  inferior_ptid = old_inferior_ptid;
	  set_thread (ptid_get_tid (inferior_ptid));
	}
    }

  return 0;
}

static int
nto_supports_multi_process (void)
{
  return 1;
}

static int
nto_verify_memory (struct target_ops *ops, const gdb_byte *data,
		   CORE_ADDR memaddr, ULONGEST size)
{
  // TODO: This should be more optimal, similar to remote.c 
  // implementation and pass address, size and crc32 to pdebug
  // so it can perform crc32 there and save network traffic
  gdb_byte *const buf = xmalloc (size);
  int match;

  if (target_read_memory (memaddr, buf, size) != 0)
    {
      warning (_("Error reading memory"));
      return -1;
    }

  match = (memcmp (buf, data, size) == 0);
  xfree (buf);
  return match;
}

static void
init_nto_ops ()
{
  nto_ops.to_shortname = "qnx";
  nto_ops.to_longname =
    "Remote serial target using the QNX Debugging Protocol";
  nto_ops.to_doc =
    "Debug a remote machine using the QNX Debugging Protocol.\n\
Specify the device it is connected to (e.g. /dev/ser1, <rmt_host>:<port>)\n\
or `pty' to launch `pdebug' for debugging.";
  nto_ops.to_open = nto_open;
  nto_ops.to_close = nto_close;
  nto_ops.to_attach = nto_attach;
  nto_ops.to_post_attach = nto_post_attach;
  nto_ops.to_detach = nto_detach;
  //TODO: nto_ops.to_disconnect = nto_disconnect;
  nto_ops.to_resume = nto_resume;
  nto_ops.to_wait = nto_wait;
  nto_ops.to_fetch_registers = nto_fetch_registers;
  nto_ops.to_store_registers = nto_store_registers;
  nto_ops.to_prepare_to_store = nto_prepare_to_store;
  nto_ops.to_xfer_partial = nto_xfer_partial;
  nto_ops.to_files_info = nto_files_info;
  nto_ops.to_can_use_hw_breakpoint = nto_can_use_hw_breakpoint;
  nto_ops.to_insert_breakpoint = nto_to_insert_breakpoint;
  nto_ops.to_remove_breakpoint = nto_to_remove_breakpoint;
  nto_ops.to_insert_hw_breakpoint = nto_insert_hw_breakpoint;
  nto_ops.to_remove_hw_breakpoint = nto_to_remove_breakpoint;
  nto_ops.to_insert_watchpoint = nto_insert_hw_watchpoint;
  nto_ops.to_remove_watchpoint = nto_remove_hw_watchpoint;
  nto_ops.to_stopped_by_watchpoint = nto_stopped_by_watchpoint;
  nto_ops.to_kill = nto_kill;
  nto_ops.to_load = generic_load;
  nto_ops.to_create_inferior = nto_create_inferior;
  nto_ops.to_mourn_inferior = nto_mourn_inferior;
  nto_ops.to_can_run = nto_can_run;
  nto_ops.to_thread_alive = nto_thread_alive;
  nto_ops.to_find_new_threads = nto_find_new_threads;
  nto_ops.to_stop = 0;
  /* nto_ops.to_query = nto_query;  */
  nto_ops.to_stratum = process_stratum;
  nto_ops.to_has_all_memory = nto_has_all_memory;
  nto_ops.to_has_memory = nto_has_memory;
  nto_ops.to_has_stack = nto_has_stack;
  nto_ops.to_has_registers = nto_has_registers;
  nto_ops.to_has_execution = nto_has_execution;
  nto_ops.to_pid_to_str = nto_pid_to_str;
  nto_ops.to_has_thread_control = tc_schedlock; /* can lock scheduler */
  nto_ops.to_magic = OPS_MAGIC;
  nto_ops.to_have_continuable_watchpoint = 1;
  nto_ops.to_extra_thread_info = nto_extra_thread_info;
  nto_ops.to_read_description = nto_read_description;
  nto_ops.to_follow_fork = nto_follow_fork;
  nto_ops.to_supports_multi_process = nto_supports_multi_process;
  nto_ops.to_verify_memory = nto_verify_memory;
}

static void
update_threadnames ()
{
  DScomm_t tran, recv;
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  struct dstidnames *tidnames = (void *) recv.pkt.okdata.data;
  pid_t cur_pid;
  unsigned int numleft;

  nto_trace (0) ("%s ()\n", __func__);

  cur_pid = ptid_get_pid (inferior_ptid);
  if(!cur_pid)
    {
      fprintf_unfiltered(gdb_stderr, "No inferior.\n");
      return;
    }

  do
    {
      unsigned int i, numtids;
      char *buf;

      nto_send_init (&tran, DStMsg_tidnames, 0, SET_CHANNEL_DEBUG);
      nto_send_recv (&tran, &recv, sizeof(tran.pkt.tidnames), 0);
      if (recv.pkt.hdr.cmd == DSrMsg_err)
        {
	  errno = errnoconvert (EXTRACT_SIGNED_INTEGER
				  (&recv.pkt.err.err, 4,
				   byte_order));
	  if (errno != EINVAL) /* Not old pdebug, but something else.  */
	    {
	      warning ("Warning: could not retrieve tidnames (errno=%d)\n",
		       errno);
	    }
	  return;
	}

      numtids = EXTRACT_UNSIGNED_INTEGER (&tidnames->numtids, 4,
					  byte_order);
      numleft = EXTRACT_UNSIGNED_INTEGER (&tidnames->numleft, 4,
					  byte_order);
      buf = (char *)tidnames + sizeof(*tidnames);
      for(i = 0 ; i < numtids ; i++)
	{
	  struct thread_info *ti;
	  struct private_thread_info *priv;
	  ptid_t ptid;
	  pid_t tid;
	  int namelen;
	  char *tmp;

	  tid = strtol(buf, &tmp, 10);
	  buf = tmp + 1; /* Skip the null terminator.  */
	  namelen = strlen(buf);

	  nto_trace (0) ("Thread %d name: %s\n", tid, buf);

	  ptid = ptid_build (cur_pid, 0, tid);
	  ti = find_thread_ptid (ptid);
	  if(ti)
	    {
	      nto_trace (0) ("Reallocating private data\n");
	      priv = xrealloc(ti->private,
			      sizeof(struct private_thread_info) + 
			      namelen + 1);
	      memcpy(priv->name, buf, namelen + 1);
	      ti->private = priv;
	    }
	  buf += namelen + 1;
	}
    } while(numleft > 0);
}

static void
nto_find_new_threads (struct target_ops *ops)
{
  DScomm_t tran, recv;
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  pid_t cur_pid, start_tid = 1, total_tids = 0, num_tids;
  struct dspidlist *pidlist = (void *) recv.pkt.okdata.data;
  struct tidinfo *tip;
  char subcmd;

  nto_trace (0) ("%s ()\n", __func__);

  cur_pid = ptid_get_pid (inferior_ptid);
  if(!cur_pid){
    fprintf_unfiltered(gdb_stderr, "No inferior.\n");
    return;
  }
  subcmd = DSMSG_PIDLIST_SPECIFIC;

  do {
    nto_send_init (&tran, DStMsg_pidlist, subcmd, SET_CHANNEL_DEBUG );
    tran.pkt.pidlist.pid = EXTRACT_UNSIGNED_INTEGER (&cur_pid, 4,
						     byte_order);
    tran.pkt.pidlist.tid = EXTRACT_UNSIGNED_INTEGER (&start_tid, 4,
						     byte_order);
    nto_send_recv (&tran, &recv, sizeof(tran.pkt.pidlist), 0);
    if (recv.pkt.hdr.cmd == DSrMsg_err)
    {
      errno = errnoconvert (EXTRACT_SIGNED_INTEGER
			      (&recv.pkt.err.err, 4, byte_order));
      return;
    }
    if (recv.pkt.hdr.cmd != DSrMsg_okdata)
    {
      errno = EOK;
      nto_trace (1) ("msg not DSrMsg_okdata!\n");
      return;
    }
    num_tids = EXTRACT_UNSIGNED_INTEGER (&pidlist->num_tids, 4,
					 byte_order);
    for (tip = (void *) &pidlist->name[(strlen(pidlist->name) + 1 + 3) & ~3];
	 tip->tid != 0; tip++ )
    {
      struct thread_info *new_thread;
      ptid_t ptid;

      tip->tid =  EXTRACT_UNSIGNED_INTEGER (&tip->tid, 2,
					    byte_order);
      ptid = ptid_build(cur_pid, 0, tip->tid);

      if (tip->tid < 0)
	{
	  //warning ("TID < 0\n");
	  continue;
	}

      new_thread = find_thread_ptid (ptid);
      if(!new_thread && tip->state != 0)
        new_thread = add_thread (ptid);
      if(new_thread && !new_thread->private){
        new_thread->private = xmalloc(sizeof(struct private_thread_info));
        new_thread->private->name[0] = '\0';
      }
      if (new_thread && new_thread->private)
	memcpy(new_thread->private, tip, sizeof(*tip));
      total_tids++;
    }
    subcmd = DSMSG_PIDLIST_SPECIFIC_TID;
    start_tid = total_tids + 1;
  } while(total_tids < num_tids);

  update_threadnames ();
}

#ifndef HAVE_RAW_DECL_STRCASESTR
extern char *strcasestr(const char *, const char *);

char *strcasestr(const char *const haystack, const char *const needle)
{
  char buff_h[1024];
  char buff_n[1024];
  const char *p;
  int i;

  p = haystack;
  for (p = haystack, i = 0; *p && i < sizeof(buff_h)-1; ++p, ++i)
    buff_h[i] = toupper(*p);
  buff_h[i] = '\0';
  for (p = needle, i = 0; *p && i < sizeof(buff_n)-1; ++p, ++i)
    buff_n[i] = toupper(*p);
  buff_n[i] = '\0';

  return strstr(buff_h, buff_n);
}
#endif /* HAVE_RAW_DECL_STRCASESTR */

void
nto_pidlist (char *args, int from_tty)
{
  DScomm_t tran, recv;
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  struct dspidlist *pidlist = (void *) recv.pkt.okdata.data;
  struct tidinfo *tip;
  char specific_tid_supported = 0;
  pid_t pid, start_tid, total_tid;
  char subcmd;

  start_tid = 1;
  total_tid = 0;
  pid = 1;
  subcmd = DSMSG_PIDLIST_BEGIN;

  /* Send a DSMSG_PIDLIST_SPECIFIC_TID to see if it is supported.  */
  nto_send_init (&tran, DStMsg_pidlist, DSMSG_PIDLIST_SPECIFIC_TID,
		 SET_CHANNEL_DEBUG);
  tran.pkt.pidlist.pid = EXTRACT_SIGNED_INTEGER (&pid, 4,
						 byte_order);
  tran.pkt.pidlist.tid = EXTRACT_SIGNED_INTEGER (&start_tid, 4,
						 byte_order);
  nto_send_recv (&tran, &recv, sizeof (tran.pkt.pidlist), 0);

  if (recv.pkt.hdr.cmd == DSrMsg_err)
    specific_tid_supported = 0;
  else
    specific_tid_supported = 1;

  while (1)
    {
      nto_send_init (&tran, DStMsg_pidlist, subcmd, SET_CHANNEL_DEBUG);
      tran.pkt.pidlist.pid = EXTRACT_SIGNED_INTEGER (&pid, 4,
						     byte_order);
      tran.pkt.pidlist.tid = EXTRACT_SIGNED_INTEGER (&start_tid, 4,
						     byte_order);
      nto_send_recv (&tran, &recv, sizeof (tran.pkt.pidlist), 0);
      if (recv.pkt.hdr.cmd == DSrMsg_err)
	{
	  errno = errnoconvert (EXTRACT_SIGNED_INTEGER
				  (&recv.pkt.err.err, 4,
				   byte_order));
	  return;
	}
      if (recv.pkt.hdr.cmd != DSrMsg_okdata)
	{
	  errno = EOK;
	  return;
	}

      for (tip =
	   (void *) &pidlist->name[(strlen (pidlist->name) + 1 + 3) & ~3];
	   tip->tid != 0; tip++)
	{
	  if ((args != NULL && strcasestr(pidlist->name, args) != NULL)
	      || args == NULL)
	    printf_filtered ("%s - %ld/%ld\n", pidlist->name,
			     (long) EXTRACT_SIGNED_INTEGER (&pidlist->pid,
							    4, byte_order),
			     (long) EXTRACT_SIGNED_INTEGER (&tip->tid, 2,
							    byte_order));
	  total_tid++;
	}
      pid = EXTRACT_SIGNED_INTEGER (&pidlist->pid, 4, byte_order);
      if (specific_tid_supported)
	{
	  if (total_tid < EXTRACT_SIGNED_INTEGER
			    (&pidlist->num_tids, 4, byte_order))
	    {
	      subcmd = DSMSG_PIDLIST_SPECIFIC_TID;
	      start_tid = total_tid + 1;
	      continue;
	    }
	}
      start_tid = 1;
      total_tid = 0;
      subcmd = DSMSG_PIDLIST_NEXT;
    }
  return;
}

static struct dsmapinfo *
nto_mapinfo (unsigned addr, int first, int elfonly)
{
  DScomm_t tran, recv;
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  struct dsmapinfo map;
  static struct dsmapinfo dmap;
  DStMsg_mapinfo_t *mapinfo = (DStMsg_mapinfo_t *) & tran.pkt;
  char subcmd;

  if (core_bfd != NULL)
    {				/* Have to implement corefile mapinfo.  */
      errno = EOK;
      return NULL;
    }

  subcmd = addr ? DSMSG_MAPINFO_SPECIFIC :
    first ? DSMSG_MAPINFO_BEGIN : DSMSG_MAPINFO_NEXT;
  if (elfonly)
    subcmd |= DSMSG_MAPINFO_ELF;

  nto_send_init (&tran, DStMsg_mapinfo, subcmd, SET_CHANNEL_DEBUG);
  mapinfo->addr = EXTRACT_UNSIGNED_INTEGER (&addr, 4, byte_order);
  nto_send_recv (&tran, &recv, sizeof (*mapinfo), 0);
  if (recv.pkt.hdr.cmd == DSrMsg_err)
    {
      errno = errnoconvert (EXTRACT_SIGNED_INTEGER
			      (&recv.pkt.err.err, 4, byte_order));
      return NULL;
    }
  if (recv.pkt.hdr.cmd != DSrMsg_okdata)
    {
      errno = EOK;
      return NULL;
    }

  memset (&dmap, 0, sizeof (dmap));
  memcpy (&map, &recv.pkt.okdata.data[0], sizeof (map));
  dmap.ino = EXTRACT_UNSIGNED_INTEGER (&map.ino, 8, byte_order);
  dmap.dev = EXTRACT_SIGNED_INTEGER (&map.dev, 4, byte_order);

  dmap.text.addr = EXTRACT_UNSIGNED_INTEGER (&map.text.addr, 4,
					     byte_order);
  dmap.text.size = EXTRACT_SIGNED_INTEGER (&map.text.size, 4,
					   byte_order);
  dmap.text.flags = EXTRACT_SIGNED_INTEGER (&map.text.flags, 4,
					    byte_order);
  dmap.text.debug_vaddr =
    EXTRACT_UNSIGNED_INTEGER (&map.text.debug_vaddr, 4,
			      byte_order);
  dmap.text.offset = EXTRACT_UNSIGNED_INTEGER (&map.text.offset, 8,
					       byte_order);
  dmap.data.addr = EXTRACT_UNSIGNED_INTEGER (&map.data.addr, 4,
					     byte_order);
  dmap.data.size = EXTRACT_SIGNED_INTEGER (&map.data.size, 4,
					   byte_order);
  dmap.data.flags = EXTRACT_SIGNED_INTEGER (&map.data.flags, 4,
					    byte_order);
  dmap.data.debug_vaddr =
    EXTRACT_UNSIGNED_INTEGER (&map.data.debug_vaddr, 4, byte_order);
  dmap.data.offset = EXTRACT_UNSIGNED_INTEGER (&map.data.offset, 8,
					       byte_order);

  strcpy (dmap.name, map.name);

  return &dmap;
}

static void
nto_meminfo (char *args, int from_tty)
{
  struct dsmapinfo *dmp;
  int first = 1;

  while ((dmp = nto_mapinfo (0, first, 0)) != NULL)
    {
      first = 0;
      printf_filtered ("%s\n", dmp->name);
      printf_filtered ("\ttext=%08x bytes @ 0x%08x\n", dmp->text.size,
		       dmp->text.addr);
      printf_filtered ("\t\tflags=%08x\n", dmp->text.flags);
      printf_filtered ("\t\tdebug=%08x\n", dmp->text.debug_vaddr);
      printf_filtered ("\t\toffset=%016llx\n", dmp->text.offset);
      if (dmp->data.size)
	{
	  printf_filtered ("\tdata=%08x bytes @ 0x%08x\n", dmp->data.size,
			   dmp->data.addr);
	  printf_filtered ("\t\tflags=%08x\n", dmp->data.flags);
	  printf_filtered ("\t\tdebug=%08x\n", dmp->data.debug_vaddr);
	  printf_filtered ("\t\toffset=%016llx\n", dmp->data.offset);
	}
      printf_filtered ("\tdev=0x%x\n", dmp->dev);
      printf_filtered ("\tino=0x%llx\n", dmp->ino);
    }
}

static int
nto_insert_hw_breakpoint (struct gdbarch *gdbarch,
			  struct bp_target_info *bp_tg_inf)
{
  DScomm_t tran, recv;
  const enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);

  nto_trace (0) ("nto_insert_hw_breakpoint(addr %s, contents_cache %p)\n",
		 paddress (gdbarch, bp_tg_inf->placed_address),
		 bp_tg_inf->shadow_contents);

  if (bp_tg_inf == NULL)
    return -1;

  nto_send_init (&tran, DStMsg_brk, DSMSG_BRK_EXEC | DSMSG_BRK_HW,
		 SET_CHANNEL_DEBUG);
  tran.pkt.brk.addr
    = EXTRACT_SIGNED_INTEGER (&bp_tg_inf->placed_address, 4,
			      byte_order);
  nto_send_recv (&tran, &recv, sizeof (tran.pkt.brk), 0);
  return recv.pkt.hdr.cmd == DSrMsg_err;
}

static int
nto_hw_watchpoint (CORE_ADDR addr, int len, int type)
{
  DScomm_t tran, recv;
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  unsigned subcmd;

  nto_trace (0) ("nto_hw_watchpoint(addr %s, len %x, type %x)\n",
		 paddress (target_gdbarch, addr), len, type);

  switch (type)
    {
    case 1:			/* Read.  */
      subcmd = DSMSG_BRK_RD;
      break;
    case 2:			/* Read/Write.  */
      subcmd = DSMSG_BRK_WR;
      break;
    default:			/* Modify.  */
      subcmd = DSMSG_BRK_MODIFY;
    }
  subcmd |= DSMSG_BRK_HW;

  nto_send_init (&tran, DStMsg_brk, subcmd, SET_CHANNEL_DEBUG);
  tran.pkt.brk.addr = EXTRACT_UNSIGNED_INTEGER (&addr, 4,
						byte_order);
  tran.pkt.brk.size = EXTRACT_SIGNED_INTEGER (&len, 4, byte_order);
  nto_send_recv (&tran, &recv, sizeof (tran.pkt.brk), 0);
  return recv.pkt.hdr.cmd == DSrMsg_err ? -1 : 0;
}

static int
nto_remove_hw_watchpoint (CORE_ADDR addr, int len, int type,
			  struct expression *exp)
{
  return nto_hw_watchpoint (addr, -1, type);
}

static int
nto_insert_hw_watchpoint (CORE_ADDR addr, int len, int type,
			  struct expression *exp)
{
  return nto_hw_watchpoint (addr, len, type);
}

#if 0
static struct tidinfo *
nto_thread_info (pid_t pid, short tid)
{
  const enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  struct dspidlist *pidlist = (void *) recv.pkt.okdata.data;
  struct tidinfo *tip;

  nto_send_init (&tran, DStMsg_pidlist, DSMSG_PIDLIST_SPECIFIC_TID,
		 SET_CHANNEL_DEBUG);
  tran.pkt.pidlist.tid = EXTRACT_SIGNED_INTEGER (&tid, 2,
						 byte_order);
  tran.pkt.pidlist.pid = EXTRACT_SIGNED_INTEGER (&pid, 4,
						 byte_order);
  nto_send_recv (&tran, &recv, sizeof (tran.pkt.pidlist), 0);

  if (recv.pkt.hdr.cmd == DSrMsg_err)
    {
      nto_send_init (&tran, DStMsg_pidlist, DSMSG_PIDLIST_SPECIFIC,
		     SET_CHANNEL_DEBUG);
      tran.pkt.pidlist.pid = EXTRACT_SIGNED_INTEGER (&pid, 4,
						     byte_order);
      nto_send_recv (&tran, &recv, sizeof (tran.pkt.pidlist), 0);
      if (recv.pkt.hdr.cmd == DSrMsg_err)
	{
	  errno = errnoconvert (recv.pkt.err.err);
	  return NULL;
	}
    }

  /* Tidinfo structures are 4-byte aligned and start after name.  */
  for (tip = (void *) &pidlist->name[(strlen (pidlist->name) + 1 + 3) & ~3];
       tip->tid != 0; tip++)
    {
      if (tid == EXTRACT_SIGNED_INTEGER (&tip->tid, 2, byte_order))
	return tip;
    }

  return NULL;
}
#endif


static void
nto_remote_inferior_data_cleanup (struct inferior *const inf, void *const dat)
{
  struct nto_remote_inferior_data *const inf_rdata = dat;

  if (dat)
    {
      xfree (inf_rdata->auxv);
      inf_rdata->auxv = NULL;
      xfree (inf_rdata->remote_exe);
      inf_rdata->remote_exe = NULL;
      xfree (inf_rdata->remote_cwd);
      inf_rdata->remote_cwd = NULL;
    }
  xfree (dat);
}


static struct nto_remote_inferior_data *
nto_remote_inferior_data (void)
{
  struct inferior *const inf = current_inferior ();
  struct nto_remote_inferior_data *inf_data;

  gdb_assert (inf != NULL);

  inf_data = inferior_data (inf, nto_remote_inferior_data_reg);
  if (inf_data == NULL)
    {
      inf_data = XCALLOC (1, struct nto_remote_inferior_data);
      set_inferior_data (inf, nto_remote_inferior_data_reg, inf_data);
    }

  return inf_data;
}

static void
set_nto_exe (char *args, int from_tty,
	     struct cmd_list_element *c)
{
  struct inferior *const inf = current_inferior ();
  struct nto_remote_inferior_data *const inf_rdat
    = nto_remote_inferior_data ();

  xfree (inf_rdat->remote_exe);
  inf_rdat->remote_exe = xstrdup (current_session->remote_exe);
}

static void
show_nto_exe (struct ui_file *file, int from_tty,
              struct cmd_list_element *c, const char *value)
{
  struct inferior *const inf = current_inferior ();
  struct nto_remote_inferior_data *const inf_rdat
    = nto_remote_inferior_data ();

  deprecated_show_value_hack (file, from_tty, c, inf_rdat->remote_exe);
}

static void
set_nto_cwd (char *args, int from_tty, struct cmd_list_element *c)
{
  struct inferior *const inf = current_inferior ();
  struct nto_remote_inferior_data *const inf_rdat
    = nto_remote_inferior_data ();

  xfree (inf_rdat->remote_cwd);
  inf_rdat->remote_cwd = xstrdup (current_session->remote_cwd);
}

static void
show_nto_cwd (struct ui_file *file, int from_tty,
              struct cmd_list_element *c, const char *value)
{
  struct inferior *const inf = current_inferior ();
  struct nto_remote_inferior_data *const inf_rdat
    = nto_remote_inferior_data ();

  deprecated_show_value_hack (file, from_tty, c, inf_rdat->remote_cwd);
}

extern initialize_file_ftype _initialize_nto;

void
_initialize_nto ()
{
  init_nto_ops ();
  add_target (&nto_ops);

  nto_remote_inferior_data_reg
    = register_inferior_data_with_cleanup (nto_remote_inferior_data_cleanup);

  add_setshow_zinteger_cmd ("nto-timeout", no_class, 
			    &only_session.timeout, _("\
Set timeout value for communication with the remote."), _("\
Show timeout value for communication with the remote."), _("\
The remote will timeout after nto-timeout seconds."),
			    NULL, NULL, &setlist, &showlist);

  add_setshow_boolean_cmd ("nto-inherit-env", no_class, 
			   &only_session.inherit_env, _("\
Set if the inferior should inherit environment from pdebug or gdb."), _("\
Show nto-inherit-env value."), _("\
If nto-inherit-env is off, the process spawned on the remote \
will have its environment set by gdb.  Otherwise, it will inherit its \
environment from pdebug."), NULL, NULL, 
			  &setlist, &showlist);

  add_setshow_string_cmd ("nto-cwd", class_support, &only_session.remote_cwd,
			  _("\
Set the working directory for the remote process."), _("\
Show current working directory for the remote process."), _("\
Working directory for the remote process. This directory must be \
specified before remote process is run."), 
			  &set_nto_cwd, &show_nto_cwd, &setlist, &showlist);

  add_setshow_string_cmd ("nto-executable", class_files, 
			  &only_session.remote_exe, _("\
Set the binary to be executed on the remote QNX Neutrino target."), _("\
Show currently set binary to be executed on the remote QNX Neutrino target."),
			_("\
Binary to be executed on the remote QNX Neutrino target when "\
"'run' command is used."), 
			  &set_nto_exe, &show_nto_exe, &setlist, &showlist);

  add_setshow_boolean_cmd ("upload-sets-exec", class_files,
			   &upload_sets_exec, _("\
Set the flag for upload to set nto-executable."), _("\
Show nto-executable flag."), _("\
If set, upload will set nto-executable. Otherwise, nto-executable \
will not change."),
			   NULL, NULL, &setlist, &showlist);

  add_info ("pidlist", nto_pidlist, _("List processes on the target.  Optional argument will filter out process names not containing (case insensitive) argument string."));
  add_info ("meminfo", nto_meminfo, "memory information");
}
