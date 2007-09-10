/* Remote target communications for serial-line targets in Neutrino Pdebug protocol
   Copyright 1988, 1991, 1992, 1993, 1994, 1995, 1996 Free Software Foundation, Inc.
   Contributed by QNX Software Systems Limited.

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
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* This file was derived from remote.c. It communicates with a
   box talking the QNX Software Systems Limited's Neutrino remote debug
   protocol. See dsmsgs.h for details. */

#undef QNX_DEBUG
//#define QNX_DEBUG 2

#include "defs.h"
#include "gdb_string.h"
#include "gdbcore.h"
#include <fcntl.h>
#include <sys/stat.h>
#include "frame.h"
#include "inferior.h"
#include "bfd.h"
#include "symfile.h"
#include "target.h"
#include "gdb_wait.h"
#include "gdbcmd.h"
#include "objfiles.h"
#include "gdb-stabs.h"
#include "gdbthread.h"
#include "dsmsgs.h"
#include "dcache.h"
#include "completer.h"

#ifdef USG
#include <sys/types.h>
#endif

#include "nto_procfs.h"
#include "nto-cache.h"

#include <sys/stat.h>
#include <signal.h>
#include "serial.h"
#include <termios.h>

#ifdef __CYGWIN__
#include <sys/cygwin.h>
#endif

#ifndef EOK
#define EOK 0
#endif

#define QNX_READ_MODE	0x0
#define QNX_WRITE_MODE	0x301
#define QNX_WRITE_PERMS	0x1ff

#if defined(_WIN32) || defined(__CYGWIN__)
#define HOST_READ_MODE  O_RDONLY|O_BINARY
#define HOST_WRITE_MODE  O_WRONLY|O_CREAT|O_TRUNC|O_BINARY
#else
#define HOST_READ_MODE  O_RDONLY
#define HOST_WRITE_MODE  O_WRONLY|O_CREAT|O_TRUNC
#endif

/* Prototypes in the appropriate remote-nto<CPU>.c file */

int (*target_created_hook)( pid_t );
CORE_ADDR qnx_getbase( void );
extern unsigned qnx_get_regset_id( int regno );
extern void qnx_cpu_supply_regset( int endian, int regset, void *data);
extern unsigned qnx_get_regset_area( unsigned regset, char *subcmd );
extern unsigned qnx_cpu_register_area PARAMS ((unsigned first_regno, unsigned last_regno, unsigned char *subcmd, unsigned *off, unsigned *len));
extern void qnx_cpu_register_fetch PARAMS ((int endian, unsigned first_regno, unsigned last_regno, void *data));
extern int qnx_cpu_register_store PARAMS ((int endian, unsigned first_regno, unsigned last_regno, void *data));

/* Prototypes for local functions */

static void init_qnx_ops PARAMS ((void));
static int putpkt PARAMS ((unsigned));
static int readchar PARAMS ((int timeout));
static int getpkt PARAMS ((int forever));
static unsigned qnx_send PARAMS ((unsigned, int));
static int qnx_write_bytes PARAMS ((CORE_ADDR memaddr, char *myaddr, int len));
static int qnx_read_bytes PARAMS ((CORE_ADDR memaddr, char *myaddr, int len));
static void qnx_files_info PARAMS ((struct target_ops *ignore));
static void qnx_parse_notify PARAMS ((struct target_waitstatus *status));
static int qnx_xfer_memory PARAMS ((CORE_ADDR memaddr, char *myaddr, int len, int should_write, struct mem_attrib *attrib, struct target_ops *target));
static int qnx_incoming_text PARAMS ((int len));
void qnx_outgoing_text(char *buf, int nbytes);
void qnx_fetch_registers PARAMS ((int regno));
static void qnx_prepare_to_store PARAMS ((void));
static void qnx_store_registers PARAMS ((int regno));
static void qnx_resume PARAMS ((ptid_t ptid, int step, enum target_signal sig));
static int qnx_start_remote PARAMS ((char *dummy));
static void qnx_open PARAMS ((char *name, int from_tty));
static void qnx_close PARAMS ((int quitting));
static void qnx_mourn PARAMS ((void));
static ptid_t qnx_wait PARAMS ((ptid_t ptid, struct target_waitstatus *status));
static void qnx_kill PARAMS ((void));
static void qnx_detach PARAMS ((char *args, int from_tty));
static void qnx_interrupt PARAMS ((int signo));
static void qnx_interrupt_twice PARAMS ((int signo));
static void interrupt_query PARAMS ((void));
static void qnx_upload PARAMS ((char *args, int from_tty));
static void qnx_download PARAMS ((char *args, int from_tty));
static void qnx_add_commands PARAMS ((void));
static void qnx_remove_commands PARAMS ((void));
static int qnx_fileopen PARAMS ((char *fname, int mode, int perms));
static void qnx_fileclose PARAMS ((int));
static int qnx_fileread PARAMS ((char *buf, int size));
static int qnx_filewrite PARAMS ((char *buf, int size));
void nto_init_solib_absolute_prefix PARAMS((void));
int nto_find_and_open_solib PARAMS((char *, unsigned, char **));
char *qnx_pid_to_str(ptid_t ptid);
char **qnx_parse_redirection PARAMS (( char *start_argv[], char **in, char **out, char **err ));
enum target_signal target_signal_from_qnx PARAMS((int sig));
enum target_signal target_signal_to_qnx PARAMS((int sig));
static struct target_ops qnx_ops;

/* This was 5 seconds, which is a long time to sit and wait.
   Unless this is going though some terminal server or multiplexer or
   other form of hairy serial connection, I would think 2 seconds would
   be plenty.  */

/*-
   2 seconds is way too short considering commands load across the
   network
 */
static int qnx_timeout = 10;
static int qnx_inherit_env = 1;
int qnx_target_has_stack_frame = 0;
static char *qnx_remote_cwd = NULL;

/* Descriptor for I/O to remote machine.  Initialize it to NULL so that
   qnx_open knows that we don't have a file open when the program
   starts.  */
struct serial *qnx_desc = NULL;
int qnx_ostype = -1, qnx_cputype = -1;
uint32_t qnx_cpuid = 0;

/* Filled in cpu info structure and flag to indicate its validity. 
 * This is initialized in procfs_attach or qnx_start_remote depending on
 * our host/target.  It would only be invalid if we were talking to an
 * older pdebug which didn't support the cpuinfo message. */
struct dscpuinfo qnx_cpuinfo;
int qnx_cpuinfo_valid;

struct qnx_process 
{
    int	pid;
    int	tid;
};

static struct qnx_process curr_proc;

static int host_endian;
static unsigned channelrd = SET_CHANNEL_DEBUG;		// Set in getpkt()
static unsigned channelwr = SET_CHANNEL_DEBUG;		// Set in putpkt()

/* 
 * These store the version of the protocol used by the pdebug we connect to
 */
static int TargetQNXProtoverMajor = 0;			// Set in qnx_start_remote()
static int TargetQNXProtoverMinor = 0;			// Set in qnx_start_remote()

/*
 * These define the version of the protocol implemented here.
 */
#define HOST_QNX_PROTOVER_MAJOR	0
#define HOST_QNX_PROTOVER_MINOR	3

static union 
{
    unsigned char buf[DS_DATA_MAX_SIZE];
    DSMsg_union_t pkt;
}		tran, recv;

#define SWAP64( val ) ( \
				(((val) >> 56) & 0x00000000000000ff) \
			|	(((val) >> 40) & 0x000000000000ff00) \
			|	(((val) >> 24) & 0x0000000000ff0000) \
			|	(((val) >> 8)  & 0x00000000ff000000) \
			|	(((val) << 8)  & 0x000000ff00000000) \
			|	(((val) << 24) & 0x0000ff0000000000) \
			|	(((val) << 40) & 0x00ff000000000000) \
			|	(((val) << 56) & 0xff00000000000000) )

#define SWAP32( val ) ( (((val) >> 24) & 0x000000ff)	\
			  | (((val) >> 8)  & 0x0000ff00)\
			  | (((val) << 8)  & 0x00ff0000)\
			  | (((val) << 24) & 0xff000000) )

#define SWAP16( val ) ( (((val) >> 8) & 0xff) | (((val) << 8) & 0xff00) )


short int
qnx_swap16(int val)
{
    return(host_endian != TARGET_BYTE_ORDER ? SWAP16(val) : val);
}


long int
qnx_swap32(long int val)
{
    return(host_endian != TARGET_BYTE_ORDER ? SWAP32(val) : val);
}

long long int
qnx_swap64(long long int val)
{
    return(host_endian != TARGET_BYTE_ORDER ? SWAP64(val) : val);
}

/* Stuff for dealing with the packets which are part of this protocol. */

#define MAX_TRAN_TRIES 3
#define MAX_RECV_TRIES 3

#define FRAME_CHAR	0x7e
#define ESC_CHAR	0x7d

static unsigned char nak_packet[] = {FRAME_CHAR,SET_CHANNEL_NAK,0,FRAME_CHAR};
static unsigned char ch_reset_packet[] = {FRAME_CHAR,SET_CHANNEL_RESET,0xff,FRAME_CHAR};
static unsigned char ch_debug_packet[] = {FRAME_CHAR,SET_CHANNEL_DEBUG,0xfe,FRAME_CHAR};
static unsigned char ch_text_packet[] = {FRAME_CHAR,SET_CHANNEL_TEXT,0xfd,FRAME_CHAR};

#define SEND_NAK         serial_write(qnx_desc,nak_packet,sizeof(nak_packet))
#define SEND_CH_RESET    serial_write(qnx_desc,ch_reset_packet,sizeof(ch_reset_packet))
#define SEND_CH_DEBUG    serial_write(qnx_desc,ch_debug_packet,sizeof(ch_debug_packet))
#define SEND_CH_TEXT     serial_write(qnx_desc,ch_text_packet,sizeof(ch_text_packet))

/* Send a packet to the remote machine.  Also sets channelwr and informs
   target if channelwr has changed.  */

static int
putpkt(unsigned len)
{
	int 		i;
	unsigned char 	csum = 0;
	unsigned char 	buf2[DS_DATA_MAX_SIZE*2];
	unsigned char 	*p;
    
	/* Copy the packet into buffer BUF2, encapsulating it
	   and giving it a checksum.  */

	p = buf2;
	*p++ = FRAME_CHAR;
    
	#if defined(QNX_DEBUG)
	printf_unfiltered("putpkt() - cmd %d, subcmd %d, mid %d\n", tran.pkt.hdr.cmd, tran.pkt.hdr.subcmd, tran.pkt.hdr.mid);
	#endif

	if(remote_debug) 
		printf_unfiltered("Sending packet (len %d): ", len);

	for(i = 0; i < len; i++) 
	{
		unsigned char c = tran.buf[i];
	
		if(remote_debug) 
			printf_unfiltered("%2.2x", c);
		csum += c;
	
		switch(c) 
		{
			case FRAME_CHAR:
			case ESC_CHAR:
				if (remote_debug) 
					printf_unfiltered("[escape]");
				*p++ = ESC_CHAR;
				c ^= 0x20;
				break;
		}
		*p++ = c;
	}

	csum ^= 0xff;

	if(remote_debug) 
	{
		printf_unfiltered("%2.2x\n", csum);
		gdb_flush(gdb_stdout);
	}
	switch(csum) 
	{
		case FRAME_CHAR:
		case ESC_CHAR:
			*p++ = ESC_CHAR;
			csum ^= 0x20;
			break;
	}
	*p++ = csum;
	*p++ = FRAME_CHAR;

	/*	
	 * GP added - June 17, 1999.  There used to be only 'channel'.  now channelwr
	 * and channelrd keep track of the state better.  
	 * If channelwr is not in the right state, notify target and set channelwr  
	 */

	if(channelwr != tran.pkt.hdr.channel)
	{
		switch (tran.pkt.hdr.channel)
		{
			case SET_CHANNEL_TEXT:	SEND_CH_TEXT; 	break;
			case SET_CHANNEL_DEBUG:	SEND_CH_DEBUG;	break;
		}
		channelwr = tran.pkt.hdr.channel;
	} 

	if(serial_write(qnx_desc, buf2, p - buf2))
		perror_with_name("putpkt: write failed");

	return len;
}

/* Read a single character from the remote end, masking it down to 8 bits. */

static int
readchar(int timeout)
{
	int ch;

	ch = serial_readchar(qnx_desc, timeout);
    
	switch(ch) 
	{
		case SERIAL_EOF:
			error("Remote connection closed");
		case SERIAL_ERROR:
			perror_with_name("Remote communication error");
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
read_frame ()
{
	unsigned char csum;
	unsigned char *bp;
	unsigned char modifier = 0;
	int c;

	if(remote_debug) 
		printf_filtered("Receiving data: ");

	csum = 0;
	bp = recv.buf;

	memset(bp, -1, sizeof recv.buf);
	for(;;) 
	{
		c = readchar(qnx_timeout);

		switch(c) 
		{
			case SERIAL_TIMEOUT:
				puts_filtered("Timeout in mid-packet, retrying\n");
				return -1;
			case ESC_CHAR:
				modifier = 0x20;
				continue;
			case FRAME_CHAR:
				if(bp == recv.buf) 
					continue; /* Ignore multiple start frames */
				if(csum != 0xff)  /* Checksum error */
					return -1;
				return bp - recv.buf - 1;
			default:
				c ^= modifier;
				if(remote_debug) 
					printf_filtered("%2.2x", c);
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
getpkt(int forever)
{
	int c;
	int tries;
	int timeout;
	unsigned len;

	#ifdef QNX_DEBUG
	printf_unfiltered("getpkt(%d)\n", forever );
	#endif

	if(forever) 
	{
		#ifdef MAINTENANCE_CMDS
		timeout = watchdog > 0 ? watchdog : -1;
		#else
		timeout = -1;
		#endif
	} 
	else 
	{
		timeout = qnx_timeout;
	}

	for(tries = 0; tries < MAX_RECV_TRIES; tries++) 
	{
		/* This can loop forever if the remote side sends us characters
		   continuously, but if it pauses, we'll get a zero from readchar
		   because of timeout.  Then we'll count that as a retry.  */
	
		/* Note that we will only wait forever prior to the start of a packet.
		   After that, we expect characters to arrive at a brisk pace.  They
		   should show up within qnx_timeout intervals.  */

		do 
		{
			c = readchar(timeout);

			if(c == SERIAL_TIMEOUT) 
			{
				#ifdef MAINTENANCE_CMDS
				if(forever) 	/* Watchdog went off.  Kill the target. */
				{
					target_mourn_inferior();
					error("Watchdog has expired.  Target detached.");
				}
				#endif
				puts_filtered("Timed out.\n");
				return -1;
			}
		} while(c != FRAME_CHAR);

		/* We've found the start of a packet, now collect the data.  */

		len = read_frame();

		if(remote_debug) 
			printf_filtered("\n");

		if(len >= sizeof(struct DShdr)) 
		{
			if(recv.pkt.hdr.channel)  // if hdr.channel is not 0, then hdr.channel is supported
				channelrd = recv.pkt.hdr.channel;

			#if defined(QNX_DEBUG) && (QNX_DEBUG == 2)
			printf_unfiltered("getpkt() - len %d, channelrd %d,", len, channelrd);
			switch (channelrd) 
			{
				case SET_CHANNEL_DEBUG:
					printf_unfiltered(" cmd = %d, subcmd = %d, mid = %d\n", recv.pkt.hdr.cmd, recv.pkt.hdr.subcmd, recv.pkt.hdr.mid);
					break;
				case SET_CHANNEL_TEXT:
					printf_unfiltered(" text message\n");
					break;
				case SET_CHANNEL_RESET:
					printf_unfiltered(" set_channel_reset\n");
					break;
				default:
					printf_unfiltered(" unknown channel!\n");
					break;
			}
			#endif
			return len;
		}
		if(len >= 1) 
		{
			/* Packet too small to be part of the debug protocol,
			   must be a transport level command */
			if(recv.buf[0] == SET_CHANNEL_NAK) 
			{
				/* Our last transmission didn't make it - send it again. */
				channelrd = SET_CHANNEL_NAK;
				return -1;
			}
			if(recv.buf[0] <= SET_CHANNEL_TEXT)
				channelrd = recv.buf[0];

			#if defined(QNX_DEBUG) && (QNX_DEBUG == 2)
			printf_unfiltered("set channelrd to %d\n", channelrd);
			#endif
			--tries; /* doesn't count as a retry */
			continue;
		}
	SEND_NAK;
	}

	/* We have tried hard enough, and just can't receive the packet.  Give up. */
	printf_unfiltered("Ignoring packet error, continuing...");
	return 0;
}

void
qnx_send_init(unsigned cmd, unsigned subcmd, unsigned chan)
{
	static unsigned char mid;

	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("    qnx_send_init(cmd %d, subcmd %d)\n", cmd, subcmd);
	#endif

	if(TARGET_BYTE_ORDER == BFD_ENDIAN_BIG) 
		cmd |= DSHDR_MSG_BIG_ENDIAN;

	tran.pkt.hdr.cmd = cmd;											//TShdr.cmd
	tran.pkt.hdr.subcmd = subcmd;		  							//TShdr.console
	tran.pkt.hdr.mid = ((chan == SET_CHANNEL_DEBUG) ? mid++ : 0) ; 	//TShdr.spare1
	tran.pkt.hdr.channel = chan;									//TShdr.channel
}


/* Send text to remote debug daemon - Pdebug */
void
qnx_outgoing_text(char *buf, int nbytes)
{
	TSMsg_text_t    *msg;

	msg = (TSMsg_text_t*)&tran;

	msg->hdr.cmd = TSMsg_text;
	msg->hdr.console = 0;
	msg->hdr.spare1 = 0;
	msg->hdr.channel = SET_CHANNEL_TEXT;	

	memcpy(msg->text, buf, nbytes);

	putpkt(nbytes + offsetof(TSMsg_text_t, text));
}


/* Display some text that came back across the text channel */

static int
qnx_incoming_text(int len)
{
	int textlen;
	TSMsg_text_t *text;
	char fmt[20];
	char buf[TS_TEXT_MAX_SIZE];

	text = (void *)&recv.buf[0];
	textlen = len - offsetof(TSMsg_text_t, text);

	switch (text->hdr.cmd) 
	{
		case TSMsg_text:
			sprintf(fmt, "%%%d.%ds", textlen, textlen);
			sprintf(buf, fmt, text->text);
			ui_file_write (gdb_stdtarg, buf, textlen);
			return 0;
		default:
			return -1;
	}
}

/* Send the command in tran.buf to the remote machine,
   and read the reply into recv.buf. */

static unsigned
qnx_send(unsigned len, int report_errors)
{
	int		rlen;
	unsigned	tries;

	if ( qnx_desc == NULL ) 
	{
		errno = ENOTCONN;
		return 0;
	}

	for (tries = 0;; tries++) 
	{
		if(tries >= MAX_TRAN_TRIES) 
		{
			unsigned char	err = DSrMsg_err;

			printf_unfiltered("Remote exhausted %d retries.\n", tries);
			if(TARGET_BYTE_ORDER == BFD_ENDIAN_BIG) 
				err |= DSHDR_MSG_BIG_ENDIAN;
			recv.pkt.hdr.cmd = err;
			recv.pkt.err.err = qnx_swap32(EIO);
			rlen = sizeof(recv.pkt.err);
			break;
		}
		putpkt(len);
		for (;;) 
		{
			rlen = getpkt(0);
			if ((channelrd != SET_CHANNEL_TEXT) || (rlen == -1))
				break;
			qnx_incoming_text(rlen);
		}
		if(rlen == -1)  // getpkt returns -1 if MsgNAK received.
		{
			printf_unfiltered("MsgNak received - resending\n");
			continue;
		}
		if((rlen >= 0) && (recv.pkt.hdr.mid == tran.pkt.hdr.mid))
			break;

		#if defined(QNX_DEBUG) && (QNX_DEBUG == 2)
		printf_unfiltered("mid mismatch!\n");
		#endif
	}
	/*
	 * getpkt() sets channelrd to indicate where the message came from.
	 * now we switch on the channel (/type of message) and then deal
	 * with it.
	 */
	switch (channelrd) 
	{
		case SET_CHANNEL_DEBUG:
			if (((recv.pkt.hdr.cmd & DSHDR_MSG_BIG_ENDIAN) != 0)) {
			    sprintf(  tran.buf, "set endian big" );
			    if ( TARGET_BYTE_ORDER != BFD_ENDIAN_BIG )
				execute_command( tran.buf, 0 );
			}
			else {
			    sprintf(  tran.buf, "set endian little" );
			    if ( TARGET_BYTE_ORDER != BFD_ENDIAN_LITTLE )
				execute_command( tran.buf, 0 );
			}
			recv.pkt.hdr.cmd &= ~DSHDR_MSG_BIG_ENDIAN;
			if(recv.pkt.hdr.cmd == DSrMsg_err) 
			{
				errno = qnx_swap32(recv.pkt.err.err);
				if (report_errors) 
				{
					switch(recv.pkt.hdr.subcmd) 
					{
						case PDEBUG_ENOERR: 	
							break;
						case PDEBUG_ENOPTY:
							perror_with_name("Remote (no ptys available)");
							break;
						case PDEBUG_ETHREAD:
							perror_with_name("Remote (thread start error)");
							break;
						case PDEBUG_ECONINV:
							perror_with_name("Remote (invalid console number)");
							break;
						case PDEBUG_ESPAWN:
							perror_with_name("Remote (spawn error)");
							break;
						case PDEBUG_EPROCFS:
							perror_with_name("Remote (procfs [/proc] error)");
							break;
						case PDEBUG_EPROCSTOP:
							perror_with_name("Remote (devctl PROC_STOP error)");
							break;
						case PDEBUG_EQPSINFO:
							perror_with_name("Remote (psinfo error)");
							break;
						case PDEBUG_EQMEMMODEL:	
							perror_with_name("Remote (invalid memory model [not flat] )");
							break;
						case PDEBUG_EQPROXY:
							perror_with_name("Remote (proxy error)");
							break;
						case PDEBUG_EQDBG:
							perror_with_name("Remote (__qnx_debug_* error)");
							break;
						default:
							perror_with_name("Remote" );
					}
				}
			}
			break;
		case SET_CHANNEL_TEXT:
		case SET_CHANNEL_RESET: /* no-ops */
			break;
	}
	return rlen;
}

/* FIXME: should we be ignoring th? */
static void
set_thread(int th)
{
	int		new_pid;
	int		new_tid;

	new_pid = ptid_get_pid(inferior_ptid);
	new_tid = ptid_get_tid(inferior_ptid);
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("set_thread(th %d) -- th is unused (using pid %d, tid %d)\n", th, new_pid, new_tid);
	#endif

	if(new_pid != curr_proc.pid || new_tid != curr_proc.tid) 
	{
		curr_proc.pid = new_pid;
		curr_proc.tid = new_tid;
		qnx_send_init(DStMsg_select, DSMSG_SELECT_SET, SET_CHANNEL_DEBUG);
		tran.pkt.select.pid = qnx_swap32(new_pid);
		tran.pkt.select.tid = qnx_swap32(new_tid);
		qnx_send(sizeof(tran.pkt.select), 1);
	}
}


/*  Return nonzero if the thread TH is still alive on the remote system.  */

static int
qnx_thread_alive(ptid_t th)
{
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_thread_alive(th %d) -- pid %d, tid %d\n", th, ptid_get_pid(th), ptid_get_tid(th));
	#endif

	qnx_send_init(DStMsg_select, DSMSG_SELECT_QUERY, SET_CHANNEL_DEBUG);
	tran.pkt.select.pid = qnx_swap32(ptid_get_pid(th));
	tran.pkt.select.tid = qnx_swap32(ptid_get_tid(th));
	qnx_send(sizeof(tran.pkt.select), 0);
	return recv.pkt.hdr.cmd != DSrMsg_err;
}

/* Clean up connection to a remote debugger.  */

/* ARGSUSED */
static int
qnx_close_1(char *dummy)
{
	qnx_send_init(DStMsg_disconnect, 0, SET_CHANNEL_DEBUG);
	qnx_send(sizeof(tran.pkt.disconnect), 0);
	serial_close (qnx_desc);

	return 0;
}

/* ARGSUSED */
static void
qnx_close(int quitting)
{
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_close(quitting %d)\n", quitting);
	#endif

	if(qnx_desc) 
	{
		catch_errors((catch_errors_ftype *)qnx_close_1, NULL, "", RETURN_MASK_ALL );
		qnx_desc = NULL;
		qnx_remove_commands();
	}
}

/* Stub for catch_errors.  */

static int
qnx_start_remote(char *dummy)
{
	int		orig_target_endian;

	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_start_remote, recv.pkt.hdr.cmd %d, (dummy %d)\n", recv.pkt.hdr.cmd, dummy);
	#endif

	immediate_quit = 1;		/* Allow user to interrupt it */
	for( ;; ) 
	{
		orig_target_endian = (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG);

		/* reset remote pdebug */
		SEND_CH_RESET;

		/*
		 * This is the first significant backward incompatible change made to
		 * pdebug.  We used to handle a notifyhost/ctl-c race condition by dropping
		 * the out of order debug message in pdebug.  Now we handle the message.
		 * The new pdebug will know this by checking against the minor version,
		 * which is now being set to 1, as opposed to 0.  We then have to query
		 * the remote agent for their protover, so we can behave accordingly.
		 */

		qnx_send_init(DStMsg_connect, 0, SET_CHANNEL_DEBUG);

		tran.pkt.connect.major = HOST_QNX_PROTOVER_MAJOR;
		tran.pkt.connect.minor = HOST_QNX_PROTOVER_MINOR;
 
		qnx_send(sizeof(tran.pkt.connect), 0);

		if(recv.pkt.hdr.cmd != DSrMsg_err) 
			break;
		if(orig_target_endian == (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)) 
			break;
		/* Send packet again, with opposite endianness */
	}
	if(recv.pkt.hdr.cmd == DSrMsg_err) 
	{
		error("Connection failed: %d.", qnx_swap32(recv.pkt.err.err));
	}
	/* NYI: need to size transmit/receive buffers to allowed size in connect response */
	immediate_quit = 0;

	#ifdef TARGET_BYTE_ORDER_SELECTABLE
	printf_unfiltered("Remote target is %s-endian\n", (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG) ? "big":"little" );
/*	if ( t_endian )
		TARGET_BYTE_ORDER = BFD_ENDIAN_BIG;
	else
		TARGET_BYTE_ORDER = BFD_ENDIAN_LITTLE; */
	target_byte_order_auto = 1;
	#endif
	nto_init_solib_absolute_prefix();

	/*
	 * Try to query pdebug for their version of the protocol
	 */

	qnx_send_init(DStMsg_protover, 0, SET_CHANNEL_DEBUG); 
	tran.pkt.protover.major = HOST_QNX_PROTOVER_MAJOR;
	tran.pkt.protover.minor = HOST_QNX_PROTOVER_MINOR;
	qnx_send(sizeof(tran.pkt.protover), 0);
	if((recv.pkt.hdr.cmd == DSrMsg_err) && (qnx_swap32(recv.pkt.err.err) == EINVAL)) // old pdebug protocol version 0.0
	{
		TargetQNXProtoverMajor = 0;
		TargetQNXProtoverMinor = 0;
	}	
	else
	if(recv.pkt.hdr.cmd == DSrMsg_okstatus)
	{
		TargetQNXProtoverMajor = qnx_swap32(recv.pkt.okstatus.status);
		TargetQNXProtoverMinor = qnx_swap32(recv.pkt.okstatus.status);
		TargetQNXProtoverMajor = (TargetQNXProtoverMajor >> 8) & DSMSG_PROTOVER_MAJOR;
		TargetQNXProtoverMinor =  TargetQNXProtoverMinor & DSMSG_PROTOVER_MINOR;
	}
	else 
	{
		error("Connection failed (Protocol Version Query): %d.", qnx_swap32(recv.pkt.err.err));
	} 

	#ifdef QNX_DEBUG
	printf_unfiltered("Pdebug protover %d.%d, GDB protover %d.%d\n", \
	TargetQNXProtoverMajor, TargetQNXProtoverMinor, HOST_QNX_PROTOVER_MAJOR, HOST_QNX_PROTOVER_MINOR);
	#endif

	target_has_execution = 0;
	target_has_stack = 0;
	qnx_target_has_stack_frame = 0;
	start_remote();		/* Initialize gdb process mechanisms */

	qnx_send_init(DStMsg_cpuinfo, 0, SET_CHANNEL_DEBUG);
	qnx_send(sizeof(tran.pkt.cpuinfo), 1);
	if(recv.pkt.hdr.cmd == DSrMsg_err){
		qnx_cpuinfo_valid = 0;
	}
	else{
		memcpy(&qnx_cpuinfo, recv.pkt.okdata.data, sizeof(struct dscpuinfo));
		qnx_cpuinfo.cpuflags = qnx_swap32(qnx_cpuinfo.cpuflags);
		qnx_cpuinfo_valid = 1;
	}

	return 1;
}

/* Open a connection to a remote debugger.
   NAME is the filename used for communication.  */

static void qnx_semi_init(void)
{
	qnx_send_init(DStMsg_disconnect, 0, SET_CHANNEL_DEBUG);
	qnx_send(sizeof(tran.pkt.disconnect), 0);

	curr_proc.pid = 0;
	curr_proc.tid = 0;

	/* Without this, some commands which require an active target (such as kill)
	   won't work.  This variable serves (at least) double duty as both the pid
	   of the target process (if it has such), and as a flag indicating that a
	   target is active.  These functions should be split out into seperate
	   variables, especially since GDB will someday have a notion of debugging
	   several processes.  */

	inferior_ptid = null_ptid;
	init_thread_list ();

	if (!catch_errors ((catch_errors_ftype *)qnx_start_remote, (char *)0, 
		"Couldn't establish connection to remote target\n", RETURN_MASK_ALL)) 
	{
		flush_cached_frames();
		pop_target();
		#if defined(QNX_DEBUG) && (QNX_DEBUG == 3)
		printf_unfiltered("qnx_semi_init() - pop_target\n");
		#endif
	}
}

static void
qnx_open (char *name, int from_tty)
{
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_open(name '%s', from_tty %d)\n", name, from_tty);
	#endif

	if (name == 0)
		error ("To open a remote debug connection, you need to specify what serial\ndevice is attached to the remote system (e.g. /dev/ttya).");

	immediate_quit = 1;             /* Allow user to interrupt it */

	target_preopen (from_tty);
	unpush_target (&qnx_ops);

	#if defined(QNX_DEBUG) && (QNX_DEBUG == 3)
	printf_unfiltered("qnx_open() - unpush_target\n");
	#endif

	qnx_desc = serial_open (name);

	if (!qnx_desc)
	{
		immediate_quit = 0;
		perror_with_name (name);
	}

	if (baud_rate != -1)
	{
		if (serial_setbaudrate (qnx_desc, baud_rate))
		{
			immediate_quit = 0;
			serial_close (qnx_desc);
			perror_with_name (name);
		}
	}

	serial_raw (qnx_desc);

	/* If there is something sitting in the buffer we might take it as a
	   response to a command, which would be bad.  */
	   serial_flush_input (qnx_desc);

	if (from_tty)
	{
		puts_filtered ("Remote debugging using ");
		puts_filtered (name);
		puts_filtered ("\n");
	}
	push_target (&qnx_ops);	/* Switch to using remote target now */
	qnx_add_commands();
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 3)
	printf_unfiltered("qnx_open() - push_target\n");
	#endif

	curr_proc.pid = 0;
	curr_proc.tid = 0;

	/* Without this, some commands which require an active target (such as kill)
	   won't work.  This variable serves (at least) double duty as both the pid
	   of the target process (if it has such), and as a flag indicating that a
	   target is active.  These functions should be split out into seperate
	   variables, especially since GDB will someday have a notion of debugging
	   several processes.  */

	inferior_ptid = null_ptid;
	init_thread_list ();

	/* Start the remote connection; if error (0), discard this target.
	   In particular, if the user quits, be sure to discard it
	   (we'd be in an inconsistent state otherwise).  */

	if (!catch_errors ((catch_errors_ftype *)qnx_start_remote, (char *)0, 
		"Couldn't establish connection to remote target\n", RETURN_MASK_ALL)) 
	{
		immediate_quit = 0;
		pop_target();

		#if defined(QNX_DEBUG) && (QNX_DEBUG == 3)
		printf_unfiltered("qnx_open() - pop_target\n");
		#endif
	}
	immediate_quit = 0;
}

/* Attaches to a process on the target side.  Arguments are as passed
   to the `attach' command by the user.  This routine can be called
   when the target is not on the target-stack, if the target_can_run
   routine returns 1; in that case, it must push itself onto the stack.  
   Upon exit, the target should be ready for normal operations, and
   should be ready to deliver the status of the process immediately 
   (without waiting) to an upcoming target_wait call.  */

static void
qnx_attach(char *args, int from_tty)
{
	ptid_t ptid;

	if ( !ptid_equal(inferior_ptid, null_ptid))
		qnx_semi_init();

	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_attach(args '%s', from_tty %d)\n", args ? args : "(null)", from_tty);
	#endif

	if(!args)
		error_no_arg ("process-id to attach");

	ptid = pid_to_ptid(atoi(args));

	if(from_tty) 
	{
		printf_unfiltered("Attaching to %s\n", target_pid_to_str(ptid));
		gdb_flush (gdb_stdout);
	}

	qnx_send_init(DStMsg_attach, 0, SET_CHANNEL_DEBUG);
	tran.pkt.attach.pid = qnx_swap32(PIDGET(ptid));
	qnx_send(sizeof(tran.pkt.attach), 1);

	/* NYI: add symbol information for process */
	/*
	 * Turn the PIDLOAD into a STOPPED notification so that when gdb
	 * calls qnx_wait, we won't cycle around.
	 */
	recv.pkt.hdr.cmd = DShMsg_notify;
	recv.pkt.hdr.subcmd = DSMSG_NOTIFY_STOPPED;

	/* hack this in here, since we will bypass the notify */
	qnx_ostype = qnx_swap16(recv.pkt.notify.un.pidload.ostype);
	qnx_cputype = qnx_swap16(recv.pkt.notify.un.pidload.cputype);
	qnx_cpuid = qnx_swap32(recv.pkt.notify.un.pidload.cpuid);
	#ifdef QNX_SET_PROCESSOR_TYPE
	QNX_SET_PROCESSOR_TYPE(qnx_cpuid); // for mips
	#endif
	inferior_ptid = ptid;
	target_has_execution = 1;
	target_has_stack = 1;
	target_has_registers = 1;
	qnx_target_has_stack_frame = 1;
	if ( target_created_hook )
		target_created_hook( curr_proc.pid );
	#if 0
	/*
	  I'd like to only do this if we aren't already on the stack. Otherwise
	  we'll pop the previous incarnation, causing a disconnect message
	  to get sent.
	*/
	push_target(&qnx_ops);
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 3)
	printf_unfiltered("qnx_attach() - push_target\n");
	#endif
	#endif

	attach_flag = 1;
}

/* This takes a program previously attached to and detaches it.  After
   this is done, GDB can be used to debug some other program.  We
   better not have left any breakpoints in the target program or it'll
   die when it hits one.  */

static void
qnx_detach(char *args, int from_tty)
{
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_detach(args '%s', from_tty %d)\n", args ? args : "(null)", from_tty);
	#endif
	if(from_tty) 
	{
		char *exec_file = get_exec_file (0);
		if(exec_file == 0) 
			exec_file = "";

		printf_unfiltered("Detaching from program: %s %d\n", exec_file, PIDGET(inferior_ptid));
		gdb_flush (gdb_stdout);
	}
	if(args) 
	{
		int sig = target_signal_to_qnx(atoi(args));

		qnx_send_init(DStMsg_kill, 0, SET_CHANNEL_DEBUG);
		tran.pkt.kill.signo = qnx_swap32(sig);
		qnx_send(sizeof(tran.pkt.kill), 1);
	}

	qnx_send_init(DStMsg_detach, 0, SET_CHANNEL_DEBUG);
	tran.pkt.detach.pid = qnx_swap32(PIDGET(inferior_ptid));
	qnx_send(sizeof(tran.pkt.detach), 1);
	inferior_ptid = null_ptid;
	init_thread_list ();
	target_has_execution = 0;
	target_has_stack = 0;
	target_has_registers = 0;
	qnx_target_has_stack_frame = 0;

	attach_flag = 0;
}


/* Tell the remote machine to resume.  */

static void
qnx_resume(ptid_t ptid, int step, enum target_signal sig)
{
	int signo;

	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_resume(pid %d, step %d, sig %d)\n", PIDGET(ptid), step, target_signal_to_qnx(sig));
	#endif

	if(ptid_equal(inferior_ptid, null_ptid))
		return;

	set_thread(ptid_equal(ptid, minus_one_ptid) ? ptid_get_tid(inferior_ptid) : ptid_get_tid(ptid));

	// The HandleSig stuff is part of the new protover 0.1, but has not
	// been implemented in all pdebugs that reflect that version.  If
	// the HandleSig comes back with an error, then revert to protover 0.0
	// behaviour, regardless of actual protover.
	// The handlesig msg sends the signal to pass, and a char array
	// 'signals', which is the list of signals to notice.

	qnx_send_init(DStMsg_handlesig, 0, SET_CHANNEL_DEBUG);
	tran.pkt.handlesig.sig_to_pass = qnx_swap32(target_signal_to_qnx(sig));
	for(signo = 0; signo < QNXNTO_NSIG; signo ++)
	{
		if (signal_stop_state (target_signal_from_qnx (signo)) == 0 &&
		    signal_print_state (target_signal_from_qnx (signo)) == 0 &&
		    signal_pass_state (target_signal_from_qnx (signo)) == 1)
		{
			tran.pkt.handlesig.signals[signo] = 0;
		}
		else
		{
			tran.pkt.handlesig.signals[signo] = 1;
		}
	}
	qnx_send(sizeof(tran.pkt.handlesig), 0);
	if(recv.pkt.hdr.cmd == DSrMsg_err)
		if(sig != TARGET_SIGNAL_0) 
		{
			qnx_send_init(DStMsg_kill, 0, SET_CHANNEL_DEBUG);
			tran.pkt.kill.signo = qnx_swap32(target_signal_to_qnx(sig));
			qnx_send(sizeof(tran.pkt.kill), 1);
		}

	qnx_send_init(DStMsg_run, step ? DSMSG_RUN_COUNT : DSMSG_RUN, SET_CHANNEL_DEBUG);
	tran.pkt.run.step.count = qnx_swap32(1);
	nto_cache_invalidate();
	qnx_send(sizeof(tran.pkt.run), 1);
}

static void (*ofunc)();
static void (*ofunc_alrm)();

//
// Yucky but necessary globals used to track state in qnx_wait() as a result of 
// things done in qnx_interrupt(), qnx_interrupt_twice(), and qnx_interrupt_retry();
//
static sig_atomic_t SignalCount = 0;		// used to track ctl-c retransmits
static sig_atomic_t InterruptedTwice = 0;	// Set in qnx_interrupt_twice()
static sig_atomic_t WaitingForStopResponse = 0;	// Set in qnx_interrupt(), cleared in qnx_wait()

#define QNX_TIMER_TIMEOUT 5
#define QNX_CTL_C_RETRIES 3

static void
qnx_interrupt_retry(signo)
{
        SignalCount++;
        if(SignalCount >= QNX_CTL_C_RETRIES) // retry QNX_CTL_C_RETRIES times after original tranmission
        {
                printf_unfiltered("CTL-C transmit - 3 retries exhausted.  Ending debug session.\n");
                WaitingForStopResponse = 0;
                SignalCount = 0;
                target_mourn_inferior();
                throw_exception(RETURN_QUIT);
        }
        else
        {
                qnx_interrupt(SIGINT);
        }
}


/* Ask the user what to do when an interrupt is received.  */

static void
interrupt_query()
{
	alarm(0);
	signal(SIGINT, ofunc);
	signal(SIGALRM, ofunc_alrm);
	target_terminal_ours ();
	InterruptedTwice = 0;

	if(query("Interrupted while waiting for the program.\n Give up (and stop debugging it)? ")) 
	{
		SignalCount = 0;
		target_mourn_inferior();
		throw_exception(RETURN_QUIT);
	}
	target_terminal_inferior();
	signal(SIGALRM, qnx_interrupt_retry);
	signal(SIGINT, qnx_interrupt_twice);
	alarm(QNX_TIMER_TIMEOUT);
}


/* The user typed ^C twice.  */
static void
qnx_interrupt_twice(signo)
     int signo;
{
	InterruptedTwice = 1;
}

/* Send ^C to target to halt it.  Target will respond, and send us a
   packet.  */

//
// GP - Dec 21, 2000.  If the target sends a NotifyHost at the same time as
// GDB sends a DStMsg_stop, then we would get into problems as both ends
// would be waiting for a response, and not the sent messages.  Now, we put
// the pkt and set the global flag 'WaitingForStopResponse', and return.
// This then goes back to the the main loop in qnx_wait() below where we
// now check against the debug message received, and handle both.
// All retries of the DStMsg_stop are handled via SIGALRM and alarm(timeout);
//

static void
qnx_interrupt( int signo )
{
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_interrupt(signo %d)\n", signo);
	#endif

	/* If this doesn't work, try more severe steps.  */
	signal(signo, qnx_interrupt_twice);
	signal(SIGALRM, qnx_interrupt_retry);

	WaitingForStopResponse = 1; 

	qnx_send_init(DStMsg_stop, DSMSG_STOP_PIDS, SET_CHANNEL_DEBUG);
	putpkt(sizeof(tran.pkt.stop));

	// Set timeout
	alarm(QNX_TIMER_TIMEOUT);
}

static void
qnx_post_attach(pid_t pid)
{
	#ifdef SOLIB_CREATE_INFERIOR_HOOK
	if ( exec_bfd != NULL || (symfile_objfile != NULL && symfile_objfile->obfd != NULL) )
	    SOLIB_CREATE_INFERIOR_HOOK (pid);
	#endif
}

/* Wait until the remote machine stops, then return,
   storing status in STATUS just as `wait' would.
   Returns "pid". */

static ptid_t
qnx_wait(ptid_t ptid, struct target_waitstatus *status)
{
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_wait pid %d, inferior_ptid %d\n", PIDGET(ptid), inferior_ptid);
	#endif

	status->kind = TARGET_WAITKIND_STOPPED;
	status->value.sig = TARGET_SIGNAL_0;

	if(ptid_equal(inferior_ptid,null_ptid))
		return null_ptid;

	if(recv.pkt.hdr.cmd != DShMsg_notify) 
	{
		int	len;
		char waiting_for_notify;

		waiting_for_notify = 1;
		SignalCount = 0;
		InterruptedTwice = 0;

		ofunc = (void (*)()) signal(SIGINT, qnx_interrupt);
		ofunc_alrm = (void (*)()) signal(SIGALRM, qnx_interrupt_retry);
		for(;;) 
		{
			len = getpkt(1);
			if (len < 0) // error - probably received MSG_NAK
			{
				if (WaitingForStopResponse)
				{
					// we do not want to get SIGALRM while calling it's handler
					// the timer is reset in the handler
					alarm(0);
					qnx_interrupt_retry(SIGALRM);
					continue; 
				}
				else
				{
					// turn off the alarm, and reset the signals, and return
					alarm(0);
					signal(SIGINT, ofunc);
					signal(SIGALRM, ofunc_alrm);
					return null_ptid;
				}
			}
			if (channelrd == SET_CHANNEL_TEXT)
				qnx_incoming_text(len);
			else // DEBUG CHANNEL
			{
				recv.pkt.hdr.cmd &= ~DSHDR_MSG_BIG_ENDIAN;
				// If we have sent the DStMsg_stop due to a ^C, we expect
				// to get the response, so check and clear the flag
				// also turn off the alarm - no need to retry, we did not lose the packet.
				if((WaitingForStopResponse) && (recv.pkt.hdr.cmd == DSrMsg_ok))
				{
					WaitingForStopResponse = 0;
					status->value.sig = TARGET_SIGNAL_INT;
					alarm(0);
					if(!waiting_for_notify)
						break;
				}
				else  // else we get the Notify we are waiting for
				if(recv.pkt.hdr.cmd == DShMsg_notify)
				{
					waiting_for_notify = 0;
					// Send an OK packet to acknowledge the notify.
					tran.pkt.hdr.cmd = DSrMsg_ok;
					if((TARGET_BYTE_ORDER == BFD_ENDIAN_BIG))
						tran.pkt.hdr.cmd |= DSHDR_MSG_BIG_ENDIAN;
					tran.pkt.hdr.channel = SET_CHANNEL_DEBUG;
					tran.pkt.hdr.mid = recv.pkt.hdr.mid;
					SEND_CH_DEBUG;
					putpkt(sizeof(tran.pkt.ok));
					// Handle old pdebug protocol behavior, where out of order msgs get dropped
					// version 0.0 does this, so we must resend after a notify.
					if((TargetQNXProtoverMajor==0) && (TargetQNXProtoverMinor==0))
					{
						if(WaitingForStopResponse)
						{
							alarm(0);
							// change the command to something other than notify
							// so we don't loop in here again - leave the rest of
							// the packet alone for qnx_parse_notify() below!!!
							recv.pkt.hdr.cmd = DSrMsg_ok; 
							qnx_interrupt(SIGINT);
						}
					}
					qnx_parse_notify(status);

					if(!WaitingForStopResponse)
						break;
				}
			}
		}
		alarm(0);

		//
		// Hitting Ctl-C sends a stop request, a second ctl-c means quit, 
		// so query here, after handling the results of the first ctl-c
		// We know we were interrupted twice because the yucky global flag
		// 'InterruptedTwice' is set in the handler, and cleared in interrupt_query()
		//
		if(InterruptedTwice)
			interrupt_query();

		signal(SIGINT, ofunc);
		signal(SIGALRM, ofunc_alrm);
	}

	recv.pkt.hdr.cmd = DSrMsg_ok; /* to make us wait the next time */
	return inferior_ptid;
}

static void
qnx_parse_notify(struct target_waitstatus *status)
{
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_parse_notify(status) - subcmd %d\n", recv.pkt.hdr.subcmd);
	#endif
	curr_proc.pid = qnx_swap32(recv.pkt.notify.pid);
	curr_proc.tid = qnx_swap32(recv.pkt.notify.tid);
	if (curr_proc.tid == 0)
		curr_proc.tid = 1;

	// This was added for arm.  See arm_init_extra_frame_info()
	// in arm-tdep.c.  arm_scan_prologue() causes a memory_error()
	// if there is not a valid stack frame, and when the inferior
	// is loaded, but has not started executing, the stack frame
	// is invalid.  The default is to assume a stack frame, and
	// this is set to 0 if we have a DSMSG_NOTIFY_PIDLOAD
	// GP July 5, 2001.

	qnx_target_has_stack_frame = 1;

	switch (recv.pkt.hdr.subcmd) 
	{
		case DSMSG_NOTIFY_PIDUNLOAD:
			/*
			 * Added a new struct pidunload_v3 to the notify.un.  This includes a 
			 * faulted flag so we can tell if the status value is a signo or an 
			 * exit value.  See dsmsgs.h, protoverminor bumped to 3. GP Oct 31 2002.
			 */
			if((TargetQNXProtoverMajor==0) && (TargetQNXProtoverMinor >= 3))
			{
				if(recv.pkt.notify.un.pidunload_v3.faulted)
				{
					status->value.integer = target_signal_from_qnx(qnx_swap32(recv.pkt.notify.un.pidunload_v3.status));
					if(status->value.integer)
						status->kind = TARGET_WAITKIND_SIGNALLED; //abnormal death
					else
						status->kind = TARGET_WAITKIND_EXITED;    //normal death
				}
				else
				{
					status->value.integer = qnx_swap32(recv.pkt.notify.un.pidunload_v3.status);
					status->kind = TARGET_WAITKIND_EXITED;    //normal death, possibly with exit value
				}
			}
			else
			{
				status->value.integer = target_signal_from_qnx(qnx_swap32(recv.pkt.notify.un.pidunload.status));
				if(status->value.integer)
					status->kind = TARGET_WAITKIND_SIGNALLED; //abnormal death
				else
					status->kind = TARGET_WAITKIND_EXITED;    //normal death
			}
			target_has_execution = 0;
			target_has_stack = 0;
			target_has_registers = 0;
			qnx_target_has_stack_frame = 0;
			break;
		case DSMSG_NOTIFY_BRK:
		case DSMSG_NOTIFY_STEP:
			/* NYI: could update the CPU's IP register here. */
			status->kind = TARGET_WAITKIND_STOPPED;
			status->value.sig = TARGET_SIGNAL_TRAP;
			break;
		case DSMSG_NOTIFY_SIGEV:
			status->kind = TARGET_WAITKIND_STOPPED;
			status->value.sig = target_signal_from_qnx(qnx_swap32(recv.pkt.notify.un.sigev.signo));
			break;
		case DSMSG_NOTIFY_PIDLOAD:
			qnx_ostype = qnx_swap16(recv.pkt.notify.un.pidload.ostype);
			qnx_cputype = qnx_swap16(recv.pkt.notify.un.pidload.cputype);
			qnx_cpuid = qnx_swap32(recv.pkt.notify.un.pidload.cpuid);
			#ifdef QNX_SET_PROCESSOR_TYPE
			QNX_SET_PROCESSOR_TYPE(qnx_cpuid); // for mips
			#endif
			target_has_execution = 1;
			target_has_stack = 1;
			target_has_registers = 1;
			qnx_target_has_stack_frame = 0;
			if ( target_created_hook )
				target_created_hook( curr_proc.pid );
			status->kind = TARGET_WAITKIND_LOADED;
			break;
		case DSMSG_NOTIFY_DLLLOAD:
		case DSMSG_NOTIFY_TIDLOAD:
		case DSMSG_NOTIFY_TIDUNLOAD:
		case DSMSG_NOTIFY_DLLUNLOAD:
			status->kind = TARGET_WAITKIND_SPURIOUS;
			break;
		case DSMSG_NOTIFY_STOPPED:
			status->kind = TARGET_WAITKIND_STOPPED;
			break;
		default:
			warning("Unexpected notify type %d", recv.pkt.hdr.subcmd);
			break;
	}
	inferior_ptid = ptid_build(curr_proc.pid, 0, curr_proc.tid);
}

/* Read register REGNO, or all registers if REGNO == -1, from the contents
   of REGISTERS.  */
void
qnx_fetch_registers(int regno)
{
	unsigned	first;
	unsigned	last;
	int		len;
	int		minusone = -1;

	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_fetch_registers(regno %d)\n", regno);
	#endif

	if(ptid_equal(inferior_ptid, null_ptid)) 
		return;

	set_thread(ptid_get_tid(inferior_ptid));

	if(regno == -1){ /* get all regsets */
		first = 0;
		last = qnx_get_regset_id( -1 );
	}else{ /* get regset regno is in */
		first = qnx_get_regset_id( regno );
		last =	first+1;
		if( first == -1 ){ /* don't support reg */
		    supply_register( regno, (char *)&minusone );
		    return;
		}
	}
	for( ; first < last; ++first ) {
    	 	qnx_send_init(DStMsg_regrd, 0, SET_CHANNEL_DEBUG);
		len = qnx_get_regset_area( first, &tran.pkt.hdr.subcmd );
		if(len > 0) {
		    void *data;
#ifdef TM_I386QNX_H
/* FIXME: these ifdefs should be taken out once we move to the new proc which
 * has a long enough structure to hold floating point registers */
		    int rlen;
#endif
		    
			tran.pkt.regrd.offset = qnx_swap16( 0 ); /* Always get whole set */
			tran.pkt.regrd.size = qnx_swap16(len);
#ifdef TM_I386QNX_H
			rlen = qnx_send(sizeof(tran.pkt.regrd), 0);
#else
			qnx_send(sizeof(tran.pkt.regrd), 0);
#endif
			if(recv.pkt.hdr.cmd != DSrMsg_err){
			    data = recv.pkt.okdata.data;
			}else{
				data = NULL;
				return;
			}
#ifdef TM_I386QNX_H
			if(first == 1 && rlen <= 128)
				return; //trying to get fpregs from an old proc

#endif
			qnx_cpu_supply_regset((TARGET_BYTE_ORDER == BFD_ENDIAN_BIG), first, data);
		}
	}
}

/* Prepare to store registers.  Don't have to do anything. */

static void 
qnx_prepare_to_store()
{
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_prepare_to_store()\n");
	#endif
}


/* Store register REGNO, or all registers if REGNO == -1, from the contents
   of REGISTERS.  */

void
qnx_store_registers(int regno)
{
	unsigned	first;
	unsigned	last;
	unsigned	end;
	unsigned	off;
	unsigned	len;

	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_store_registers(regno %d)\n", regno);
	#endif

	if(ptid_equal(inferior_ptid, null_ptid))
		return;

	set_thread(ptid_get_tid(inferior_ptid));

	if(regno == -1) 
	{
		first = 0;
		last = NUM_REGS-1;
	} 
	else 
	{
		first = regno;
		last = regno;
	}

	while(first <= last) 
	{
		qnx_send_init(DStMsg_regwr, 0, SET_CHANNEL_DEBUG);
		end = qnx_cpu_register_area(first, last,
		&tran.pkt.hdr.subcmd, &off, &len);
		if(len > 0) 
		{
			tran.pkt.regwr.offset = qnx_swap16(off);
			if(qnx_cpu_register_store((TARGET_BYTE_ORDER == BFD_ENDIAN_BIG), first, end, tran.pkt.regwr.data)) 
				qnx_send(offsetof(DStMsg_regwr_t, data) + len, 1);
		}
		first = end + 1;
	}
}

/* 
   Use of the data cache *used* to be disabled because it loses for looking at
   and changing hardware I/O ports and the like.  Accepting `volatile'
   would perhaps be one way to fix it.  Another idea would be to use the
   executable file for the text segment (for all SEC_CODE sections?
   For all SEC_READONLY sections?).  This has problems if you want to
   actually see what the memory contains (e.g. self-modifying code,
   clobbered memory, user downloaded the wrong thing).  

   Because it speeds so much up, it's now enabled, if you're playing
   with registers you turn it off (set remotecache 0)
*/


/* Write memory data directly to the remote machine.
   This does not inform the data cache; the data cache uses this.
   MEMADDR is the address in the remote memory space.
   MYADDR is the address of the buffer in our space.
   LEN is the number of bytes.

   Returns number of bytes transferred, or 0 for error.  */

static int
qnx_write_bytes(CORE_ADDR memaddr, char *myaddr, int len)
{
	long long addr;
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_write_bytes(to %x, from %x, len %d)\n", memaddr, myaddr, len);
	#endif

	/* NYI: need to handle requests bigger than largest allowed packet */
	qnx_send_init(DStMsg_memwr, 0, SET_CHANNEL_DEBUG);
	addr = memaddr;
	tran.pkt.memwr.addr = qnx_swap64(addr);
	memcpy(tran.pkt.memwr.data, myaddr, len);
	qnx_send(offsetof(DStMsg_memwr_t, data) + len, 0);

	switch(recv.pkt.hdr.cmd) 
	{
		case DSrMsg_ok:
			return len;
		case DSrMsg_okstatus:
			return qnx_swap32(recv.pkt.okstatus.status);
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
qnx_read_bytes(CORE_ADDR memaddr, char *myaddr, int len)
{
	int		rcv_len, tot_len, ask_len; 
	long long 	addr;

	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_read_bytes(from %x, to %x, len %d)\n", memaddr, myaddr, len);
	#endif

	tot_len = rcv_len = ask_len = 0;

	/* GP, Jan 27,2000 - need to handle requests bigger than largest allowed packet */
	do
	{
		qnx_send_init(DStMsg_memrd, 0, SET_CHANNEL_DEBUG);
		addr = memaddr + tot_len;
		tran.pkt.memrd.addr = qnx_swap64(addr);
		ask_len = ((len - tot_len) > DS_DATA_MAX_SIZE) ? DS_DATA_MAX_SIZE : (len - tot_len);
		tran.pkt.memrd.size = qnx_swap16(ask_len);
		rcv_len = qnx_send(sizeof(tran.pkt.memrd), 0) - sizeof(recv.pkt.hdr);
		if(rcv_len <= 0)
			break;
		if(recv.pkt.hdr.cmd == DSrMsg_okdata)
		{ 
			memcpy(myaddr + tot_len, recv.pkt.okdata.data, rcv_len);
			tot_len += rcv_len;
		}
		else	
			break;
	} 
	while(tot_len != len);

	return(tot_len);
}

/* Read or write LEN bytes from inferior memory at MEMADDR, transferring
   to or from debugger address MYADDR.  Write to inferior if SHOULD_WRITE is
   nonzero.  Returns length of data written or read; 0 for error.  */

/* ARGSUSED */
static int
qnx_xfer_memory(CORE_ADDR memaddr, char *myaddr, int len, int should_write, struct mem_attrib *attrib, struct target_ops *ignore)
{
int res;
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_xfer_memory(memaddr %x, myaddr %x, len %d, should_write %d, target %d)\n", memaddr, myaddr, len, should_write, ignore);
	#endif
	if(ptid_equal(inferior_ptid, null_ptid)){
		/* pretend to read if no inferior but fail on write */
		if(should_write)
			return 0;
		memset(myaddr, 0, len);
		return len;
	}
    if (should_write){
	res = qnx_write_bytes (memaddr, myaddr, len);
	if(nto_cache_min)
	    nto_cache_store(memaddr, res, myaddr);
    }
    else if(!nto_cache_min){
	res = qnx_read_bytes (memaddr, myaddr, len);
    }    
    else {
	char *buf = alloca(nto_cache_min);
	
	res = nto_cache_fetch(memaddr, len, myaddr);
	if(res == len)
		return res;
	
	if(len < nto_cache_min){
		res = qnx_read_bytes (memaddr, buf, nto_cache_min);
		nto_cache_store(memaddr, res, buf);
		res = res < len ? res : len;
		memcpy(myaddr, buf, res);
	}
	else{
		res = qnx_read_bytes (memaddr, myaddr, len);
		nto_cache_store(memaddr, res, myaddr);
	}
    }
    return res;
}


static void
qnx_files_info(struct target_ops *ignore)
{
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_files_info(ignore %d)\n", ignore);
	#endif
	puts_filtered("Debugging a target over a serial line.\n");
}


static int
qnx_kill_1(char *dummy)
{
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_kill_1(dummy %d)\n", dummy);
	#endif

	if(!ptid_equal(inferior_ptid, null_ptid))
	{
		qnx_send_init(DStMsg_kill, DSMSG_KILL_PID, SET_CHANNEL_DEBUG);
		tran.pkt.kill.signo = qnx_swap32(9); /* SIGKILL */
		qnx_send(sizeof(tran.pkt.kill), 0);
	}
	return 0;
}

static void
qnx_kill()
{
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_kill()\n");
	#endif

	/* Use catch_errors so the user can quit from gdb even when we aren't on
	   speaking terms with the remote system.  */
	if ( catch_errors((catch_errors_ftype *)qnx_kill_1, (char *)0, "", RETURN_MASK_ERROR) )
		target_mourn_inferior();
}

static void
qnx_mourn()
{
	extern int show_breakpoint_hit_counts;

	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_mourn()\n");
	#endif

	init_thread_list ();

	attach_flag = 0;
	breakpoint_init_inferior (inf_exited);
	registers_changed ();

	#ifdef CLEAR_DEFERRED_STORES
	/* Delete any pending stores to the inferior... */
	CLEAR_DEFERRED_STORES;
	#endif

	inferior_ptid = null_ptid;

	reopen_exec_file ();
	reinit_frame_cache ();

	/* It is confusing to the user for ignore counts to stick around
	   from previous runs of the inferior.  So clear them.  */
	/* However, it is more confusing for the ignore counts to disappear when
	   using hit counts.  So don't clear them if we're counting hits.  */

	if (!show_breakpoint_hit_counts)
		breakpoint_clear_ignore_counts ();

}

int qnx_fd_raw( int fd )
{
	struct termios termios_p;

	if( tcgetattr( fd, &termios_p ) )
		return( -1 );

	termios_p.c_cc[VMIN]  =  1;
	termios_p.c_cc[VTIME] =  0;
	termios_p.c_lflag &= ~( ECHO|ICANON|ISIG| ECHOE|ECHOK|ECHONL );
	termios_p.c_oflag &= ~( OPOST );
	return( tcsetattr( fd, TCSADRAIN, &termios_p ) );
}

static void
qnx_create(char *exec_file, char *args, char **env)
{
	unsigned			argc;
	unsigned			envc;
	char				**start_argv;
	char				**argv;
	char				*p;
	int				fd;
	struct target_waitstatus 	status;
	char 				*in, *out, *err;
	int				len = 0;
	int				totlen = 0;

	if ( qnx_desc == NULL )
		qnx_open("pty", 0);

	if (!ptid_equal(inferior_ptid, null_ptid))
		qnx_semi_init();

	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_create(exec_file '%s', args '%s', environ)\n", exec_file ? exec_file : "(null)", args ? args : "(null)");
	#endif

	qnx_send_init(DStMsg_env, DSMSG_ENV_CLEARENV, SET_CHANNEL_DEBUG );
	qnx_send(sizeof(DStMsg_env_t), 1);

	if ( !qnx_inherit_env ) 
	{
		for(envc = 0; *env; env++, envc++) 
		{
			len = strlen(*env);
			totlen = 0;
			if (TargetQNXProtoverMinor >= 2) /* HOST_QNX_PROTOVER_MINOR == 2 */
			{
				if (len > DS_DATA_MAX_SIZE)
				{
					while (len > DS_DATA_MAX_SIZE) 
					{ 
						qnx_send_init(DStMsg_env, DSMSG_ENV_SETENV_MORE, SET_CHANNEL_DEBUG );
						memcpy( tran.pkt.env.data, *env+totlen, DS_DATA_MAX_SIZE);
						qnx_send(offsetof(DStMsg_env_t,data)+DS_DATA_MAX_SIZE, 1);
						len -= DS_DATA_MAX_SIZE;
						totlen += DS_DATA_MAX_SIZE;
					}
				}
			}
			else if (len > DS_DATA_MAX_SIZE)
			{
				printf_unfiltered("** Skipping env var \"%.40s .....\" <cont>\n", *env);
				printf_unfiltered("** Protovers under 0.2 do not handle env vars longer than %d\n", DS_DATA_MAX_SIZE);
				continue;
			}
			qnx_send_init(DStMsg_env, DSMSG_ENV_SETENV, SET_CHANNEL_DEBUG );
			strcpy( tran.pkt.env.data, *env+totlen ); 
			qnx_send(offsetof(DStMsg_env_t,data)+strlen(tran.pkt.env.data)+1, 1);
		}
	}

	if( qnx_remote_cwd != NULL ) 
	{
		qnx_send_init(DStMsg_cwd, DSMSG_CWD_SET, SET_CHANNEL_DEBUG );
		strcpy( tran.pkt.cwd.path, qnx_remote_cwd );
		qnx_send(offsetof(DStMsg_cwd_t,path)+strlen(tran.pkt.cwd.path)+1, 1);
	}

	qnx_send_init(DStMsg_env, DSMSG_ENV_CLEARARGV, SET_CHANNEL_DEBUG );
	qnx_send(sizeof(DStMsg_env_t), 1);

	start_argv = buildargv(args);
	if(start_argv == NULL) nomem(0);
	start_argv = qnx_parse_redirection( start_argv, &in, &out, &err );
//	printf("stdin=%s, stdout=%s, stderr=%s\n", in, out, err );

	if ( in[0] ) 
	{
		if ( (fd = open(in,O_RDONLY)) == -1 ) 
			perror(in);
		else
			qnx_fd_raw(fd);
	}

	if ( out[0] ) 
	{
		if ( (fd = open(out,O_WRONLY)) == -1 ) 
			perror(out);
		else
			qnx_fd_raw(fd);
	}

	if ( err[0] ) 
	{
		if ( (fd = open(err,O_WRONLY)) == -1 ) 
			perror(err);
		else
			qnx_fd_raw(fd);
	}

	in = ""; out = ""; err = "";
	argc = 0;
	if(exec_file != NULL) 
	{
		qnx_send_init(DStMsg_env, DSMSG_ENV_ADDARG, SET_CHANNEL_DEBUG );
		strcpy( tran.pkt.env.data, exec_file );
		/* send it twice - first as cmd, second as argv[0] */
		qnx_send(offsetof(DStMsg_env_t,data)+strlen(tran.pkt.env.data)+1, 1);
		qnx_send_init(DStMsg_env, DSMSG_ENV_ADDARG, SET_CHANNEL_DEBUG );
		strcpy( tran.pkt.env.data, exec_file );
		qnx_send(offsetof(DStMsg_env_t,data)+strlen(tran.pkt.env.data)+1, 1);
	} 
	else if(*start_argv == NULL) 
	{
		error("No executable specified.");
		freeargv(start_argv);
		return;
	} 
	else 
	{
		exec_file = *start_argv;
		qnx_send_init(DStMsg_env, DSMSG_ENV_ADDARG, SET_CHANNEL_DEBUG );
		strcpy( tran.pkt.env.data, *start_argv );
		qnx_send(offsetof(DStMsg_env_t,data)+strlen(tran.pkt.env.data)+1, 1);
	}

	for(argv = start_argv; *argv && **argv; argv++, argc++) 
	{
		qnx_send_init(DStMsg_env, DSMSG_ENV_ADDARG, SET_CHANNEL_DEBUG );
		strcpy( tran.pkt.env.data, *argv );
		qnx_send(offsetof(DStMsg_env_t,data)+strlen(tran.pkt.env.data)+1, 1);
	}
	freeargv(start_argv);

	/* NYI: msg too big for buffer */
	if ( qnx_inherit_env )
		qnx_send_init(DStMsg_load, DSMSG_LOAD_DEBUG|DSMSG_LOAD_INHERIT_ENV, SET_CHANNEL_DEBUG);
	else
		qnx_send_init(DStMsg_load, DSMSG_LOAD_DEBUG, SET_CHANNEL_DEBUG);

	p = tran.pkt.load.cmdline;

	tran.pkt.load.envc = 0;
	tran.pkt.load.argc = 0;

	strcpy( p, exec_file );
	p+=strlen(p);
	*p++ = '\0'; /* load_file */

	strcpy( p, in );
	p+=strlen(p);
	*p++ = '\0'; /* stdin */

	strcpy( p, out );
	p+=strlen(p);
	*p++ = '\0'; /* stdout */

	strcpy( p, err );
	p+=strlen(p);
	*p++ = '\0'; /* stderr */

	qnx_send(offsetof(DStMsg_load_t, cmdline) + p - tran.pkt.load.cmdline + 1, 1);
	/* comes back as an DSrMsg_okdata, but it's really a DShMsg_notify */
	if (recv.pkt.hdr.cmd == DSrMsg_okdata) 
	{
		qnx_parse_notify(&status);
		add_thread(inferior_ptid);
	}
	/* NYI: add the symbol info somewhere? */
	#ifdef SOLIB_CREATE_INFERIOR_HOOK
	if ( exec_bfd != NULL || (symfile_objfile != NULL && symfile_objfile->obfd != NULL) )
	    SOLIB_CREATE_INFERIOR_HOOK (pid);
	#endif
	attach_flag = 0;
}

static int
qnx_insert_breakpoint(CORE_ADDR addr, char *contents_cache)
{
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_insert_breakpoint(addr %x, contents_cache %x)\n", addr, contents_cache);
	#endif

	qnx_send_init(DStMsg_brk, DSMSG_BRK_EXEC, SET_CHANNEL_DEBUG);
	tran.pkt.brk.addr = qnx_swap32(addr);
	tran.pkt.brk.size = 0;
	qnx_send(sizeof(tran.pkt.brk), 0);
	return recv.pkt.hdr.cmd == DSrMsg_err;
}

static int
qnx_remove_breakpoint(CORE_ADDR addr, char *contents_cache)
{
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_remove_breakpoint(addr %x, contents_cache %x)\n", addr, contents_cache);
	#endif

	/* This got changed to send DSMSG_BRK_EXEC with a size of -1 
	 * qnx_send_init(DStMsg_brk, DSMSG_BRK_REMOVE, SET_CHANNEL_DEBUG); 
	 */
	qnx_send_init(DStMsg_brk, DSMSG_BRK_EXEC, SET_CHANNEL_DEBUG);
	tran.pkt.brk.addr = qnx_swap32(addr);
	tran.pkt.brk.size = qnx_swap32(-1);
	qnx_send(sizeof(tran.pkt.brk), 0);
	return recv.pkt.hdr.cmd == DSrMsg_err;
}

#ifdef __CYGWIN__
void
slashify(char *buf)
{
  int i = 0;
  while(buf[i])
  {
    /* Not sure why we would want to leave an escaped '\', but seems 
       safer.  */
    if(buf[i] == '\\') 
    {
      if(buf[i+1] == '\\')
        i++;
      else
        buf[i] = '/';
    }
    i++;
  }
}
#endif

static void
qnx_upload(char *args, int fromtty)
{
	#ifdef __CYGWIN__
	char cygbuf[PATH_MAX];
	#endif
	int fd;
	int len;
	char buf[DS_DATA_MAX_SIZE];
	char *from, *to;
	char **argv;

	if (args == 0) 
	{
		printf_unfiltered("You must specify a filename to send.\n");
		return;
	}

        #ifdef __CYGWIN__
	/*
	 * We need to convert back slashes to forward slashes for DOS
	 * style paths, else buildargv will remove them.
	 */
	slashify(args);
        #endif
	argv = buildargv(args);

	if(argv == NULL) 
	    nomem(0);

	#ifdef __CYGWIN__
	cygwin_conv_to_posix_path(argv[0], cygbuf);
	from = cygbuf;
	#else
	from = argv[0];
	#endif
	to = argv[1] ? argv[1] : from;

	if ((fd = open(from, HOST_READ_MODE)) == -1) 
	{
		printf_unfiltered("Unable to open '%s': %s\n", from, strerror(errno));
		return;
	}

	if (qnx_fileopen(to, QNX_WRITE_MODE, QNX_WRITE_PERMS ) == -1) 
	{
		printf_unfiltered("Remote was unable to open '%s': %s\n", to, strerror(errno));
		close(fd);
		return;
	}

	while ((len = read(fd, buf, sizeof buf)) > 0) 
	{
		if (qnx_filewrite(buf, len) == -1) 
		{
			printf_unfiltered("Remote was unable to complete write: %s\n", strerror(errno));
			close(fd);
			return;
		}
	}
	if (len == -1)
		printf_unfiltered("Local read failed: %s\n", strerror(errno));
	qnx_fileclose(fd);
	close(fd);
}

static void
qnx_download(char *args, int fromtty)
{
	#ifdef __CYGWIN__
	char cygbuf[PATH_MAX];
	#endif
	int fd;
	int len;
	char buf[DS_DATA_MAX_SIZE];
	char *from, *to;
	char **argv;

	if (args == 0) 
	{
		printf_unfiltered("You must specify a filename to get.\n");
		return;
	}

	#ifdef __CYGWIN__
	/*
	 * We need to convert back slashes to forward slashes for DOS
	 * style paths, else buildargv will remove them.
	 */
	slashify(args);
	#endif

	argv = buildargv(args);
	if(argv == NULL)
	    nomem(0);

	from = argv[0];
	#ifdef __CYGWIN__
	if(argv[1])
	{
		cygwin_conv_to_posix_path(argv[1], cygbuf);
		to = cygbuf;
	}
	else
		to = from;
	#else
	to = argv[1] ? argv[1] : from;
	#endif

	if ((fd = open(to, HOST_WRITE_MODE, 0666)) == -1) 
	{
		printf_unfiltered("Unable to open '%s': %s\n", to, strerror(errno));
		return;
	}

	if (qnx_fileopen(from, QNX_READ_MODE, 0) == -1) 
	{
		printf_unfiltered("Remote was unable to open '%s': %s\n", from, strerror(errno));
		close(fd);
		return;
	}

	while ((len = qnx_fileread(buf, sizeof buf)) > 0) 
	{
		if (write(fd, buf, len) == -1) 
		{
			printf_unfiltered("Local write failed: %s\n", strerror(errno));
			close(fd);
			return;
		}
	}

	if (len == -1)
		printf_unfiltered("Remote read failed: %s\n", strerror(errno));
	qnx_fileclose(fd);
	close(fd);
}

static void
qnx_add_commands ()
{
	struct cmd_list_element *c;
	
	c = add_com ("upload", class_obscure, qnx_upload, "Send a file to the target (upload {local} [{remote}])");
	c->completer = filename_completer;
	add_com ("download", class_obscure, qnx_download, "Get a file from the target (download {remote} [{local}])");
}

static void
qnx_remove_commands ()
{
	extern struct cmd_list_element *cmdlist;

	delete_cmd ("upload", &cmdlist);
	delete_cmd ("download", &cmdlist);
}

static int qnx_remote_fd = -1;

static int
qnx_fileopen(char *fname, int mode, int perms )
{
	if (qnx_remote_fd != -1) 
	{
		printf_unfiltered("Remote file currently open, it must be closed before you can open another.\n");
		errno = EAGAIN;
		return -1;
	}

	qnx_send_init(DStMsg_fileopen, 0, SET_CHANNEL_DEBUG);
	strcpy(tran.pkt.fileopen.pathname, fname);
	tran.pkt.fileopen.mode = qnx_swap32(mode);
	tran.pkt.fileopen.perms = qnx_swap32(perms);
	qnx_send(sizeof tran.pkt.fileopen, 0);

	if (recv.pkt.hdr.cmd == DSrMsg_err) 
	{
		errno = qnx_swap32(recv.pkt.err.err);
		return -1;
	}
	return qnx_remote_fd = 0;
}

static void
qnx_fileclose(int fd)
{
	if (qnx_remote_fd == -1)
		return;

	qnx_send_init(DStMsg_fileclose, 0, SET_CHANNEL_DEBUG);
	tran.pkt.fileclose.mtime = 0;
	qnx_send(sizeof tran.pkt.fileclose, 1);
	qnx_remote_fd = -1;
}

static int
qnx_fileread(char *buf, int size) 
{
	int len;

	qnx_send_init(DStMsg_filerd, 0, SET_CHANNEL_DEBUG);
	tran.pkt.filerd.size = qnx_swap16(size);
	len = qnx_send(sizeof tran.pkt.filerd, 0);

	if (recv.pkt.hdr.cmd == DSrMsg_err) 
	{
		errno = recv.pkt.err.err;
		return -1;
	}

	len -= sizeof recv.pkt.okdata.hdr;
	memcpy(buf, recv.pkt.okdata.data, len);
	return len;
}

static int
qnx_filewrite(char *buf, int size) 
{
	int len, siz;

	for (siz = size; siz > 0; siz -= len, buf += len) 
	{
		len = siz < sizeof tran.pkt.filewr.data ? siz : sizeof tran.pkt.filewr.data;
		qnx_send_init(DStMsg_filewr, 0, SET_CHANNEL_DEBUG);
		memcpy(tran.pkt.filewr.data, buf, len);
		qnx_send(sizeof(tran.pkt.filewr.hdr)+len, 0);

		if (recv.pkt.hdr.cmd == DSrMsg_err) 
		{
			errno = recv.pkt.err.err;
			return size - siz;
		}
	}
	return size;
}

static int
qnx_can_run (void)
{
	return 0;
}

static void
init_qnx_ops ()
{
	qnx_ops.to_shortname = "qnx";
	qnx_ops.to_longname = "Remote serial target using the QNX Debugging Protocol";
	qnx_ops.to_doc =
	"Debug a remote machine using the QNX Debugging Protocol.\n\
Specify the device it is connected to (e.g. /dev/ser1, <rmt_host>:<port>)\n\
or `pty' to launch `pdebug' for debugging.";
	qnx_ops.to_open =  qnx_open;
	qnx_ops.to_close = qnx_close;
	qnx_ops.to_attach = qnx_attach; 
	qnx_ops.to_post_attach = qnx_post_attach; 
	qnx_ops.to_detach = qnx_detach;
	qnx_ops.to_resume = qnx_resume;
	qnx_ops.to_wait = qnx_wait;
	qnx_ops.to_fetch_registers = qnx_fetch_registers;
	qnx_ops.to_store_registers = qnx_store_registers;
	qnx_ops.to_prepare_to_store = qnx_prepare_to_store;
	qnx_ops.to_xfer_memory = qnx_xfer_memory;
	qnx_ops.to_files_info = qnx_files_info;
	qnx_ops.to_insert_breakpoint = qnx_insert_breakpoint;
	qnx_ops.to_remove_breakpoint = qnx_remove_breakpoint;
	qnx_ops.to_kill = qnx_kill;
	qnx_ops.to_load = generic_load;
	qnx_ops.to_create_inferior = qnx_create;
	qnx_ops.to_mourn_inferior = qnx_mourn;
	qnx_ops.to_can_run = qnx_can_run;
	qnx_ops.to_thread_alive = qnx_thread_alive;
	qnx_ops.to_find_new_threads = qnx_find_new_threads;
	qnx_ops.to_stop = 0;
	// qnx_ops.to_query = qnx_query;
	qnx_ops.to_stratum = process_stratum;
	qnx_ops.to_has_all_memory = 1;
	qnx_ops.to_has_memory = 1;
	qnx_ops.to_has_stack = 0;  // 1;
	qnx_ops.to_has_registers = 1;
	qnx_ops.to_has_execution = 0;
	qnx_ops.to_pid_to_str = qnx_pid_to_str;
	// qnx_ops.to_has_thread_control = tc_schedlock; /* can lock scheduler */
	qnx_ops.to_magic = OPS_MAGIC;
}


void qnx_find_new_threads(void)
{
	pid_t pid, start_tid, total_tid;
	ptid_t ptid;
	struct dspidlist *pidlist = (void *)recv.pkt.okdata.data;
	struct tidinfo *tip;
	char subcmd;

	pid = ptid_get_pid(inferior_ptid);
	start_tid = 1;
	total_tid = 1;
	subcmd = DSMSG_PIDLIST_SPECIFIC_TID;

	do 
	{
		tran.pkt.pidlist.tid = qnx_swap32(start_tid);
		qnx_send_init( DStMsg_pidlist, subcmd, SET_CHANNEL_DEBUG );
		tran.pkt.pidlist.pid = qnx_swap32(pid);
		qnx_send(sizeof(tran.pkt.pidlist), 0);

		if (recv.pkt.hdr.cmd == DSrMsg_err)
		{
			if(subcmd == DSMSG_PIDLIST_SPECIFIC_TID) 
			{
				subcmd = DSMSG_PIDLIST_SPECIFIC;
				start_tid = 0;
				tran.pkt.pidlist.tid = qnx_swap32(start_tid);
				qnx_send_init( DStMsg_pidlist, subcmd, SET_CHANNEL_DEBUG );
				tran.pkt.pidlist.pid = qnx_swap32(pid);
				qnx_send(sizeof(tran.pkt.pidlist), 1); //no need to check error, as '1' means no return on error
			}
			else 
			{
				errno = recv.pkt.err.err;
				return;
			}
		}
		#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
		printf_unfiltered("pid = %ld\n", qnx_swap32(pidlist->pid));
		printf_unfiltered("start tid = %ld, total tid = %ld\n",qnx_swap32(start_tid), qnx_swap32(pidlist->num_tids) );
		printf_unfiltered("name = %s\n", pidlist->name );
		#endif

		for ( tip = (void *) &pidlist->name[(strlen(pidlist->name) + 1 + 3) & ~3]; tip->tid != 0; tip++ )
		{
			total_tid++;
			ptid = ptid_build(pid, 0, qnx_swap16(tip->tid));
			#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
			printf_unfiltered("%s - %d/%d\n", pidlist->name, pid, qnx_swap16(tip->tid) );
			#endif
			if (!in_thread_list(ptid))
				add_thread(ptid);
		}
		start_tid = total_tid +1;
		#ifdef QNX_DEBUG
		printf_unfiltered("total_tids = %ld, pidlist->num_tids = %ld\n", total_tid, qnx_swap32(pidlist->num_tids));
		#endif
	} 
	while ((total_tid < qnx_swap32(pidlist->num_tids)) && (subcmd == DSMSG_PIDLIST_SPECIFIC_TID));

	return;
}

/* FIXME: this is probably redundant in some way with find_new_threads above.  */
void qnx_tidinfo(char *args, int from_tty)
{
	struct dspidlist *pidlist = (void *)recv.pkt.okdata.data;
	struct tidinfo *tip;
	pid_t cur_pid, cur_tid, start_tid = 1;
	char subcmd;
	int total_tids = 0;

	#if defined(__QNXNTO__) && defined(__QNXNTO_USE_PROCFS__)
	/* FIXME: need procfs version.... */
	extern void procfs_tidinfo(char *args, int from_tty);
        if ( qnx_desc == NULL )
                return procfs_tidinfo(args, from_tty);
	#endif
	cur_pid = ptid_get_pid(inferior_ptid);
	cur_tid = ptid_get_tid(inferior_ptid);

	if(!cur_pid){
		fprintf_unfiltered(gdb_stderr, "No inferior.\n");
		return;
	}
	subcmd = DSMSG_PIDLIST_SPECIFIC;
	do {
		qnx_send_init( DStMsg_pidlist, subcmd, SET_CHANNEL_DEBUG );
		tran.pkt.pidlist.pid = qnx_swap32(cur_pid);
		tran.pkt.pidlist.tid = qnx_swap32(start_tid);
		qnx_send(sizeof(tran.pkt.pidlist), 0);
		if (recv.pkt.hdr.cmd == DSrMsg_err) 
		{
			errno = qnx_swap32(recv.pkt.err.err);
			return;
		}
		if (recv.pkt.hdr.cmd != DSrMsg_okdata) 
		{
			errno = EOK;
			return;
		}
	
		printf_filtered("Threads for pid %d (%s)\nTid:\tState:\tFlags:\n", cur_pid, pidlist->name);
	
		for ( tip = (void *) &pidlist->name[(strlen(pidlist->name) + 1 + 3) & ~3]; tip->tid != 0; tip++ ) 
		{
			pid_t tid = qnx_swap16(tip->tid);
			ptid_t ptid = ptid_build(cur_pid, 0, tid);
			if (!in_thread_list(ptid))
				add_thread(ptid);
			printf_filtered("%c%d\t%d\t%d\n", tid == cur_tid ? '*' : ' ', tid, tip->state, tip->flags );
			total_tids++;
		}
		subcmd = DSMSG_PIDLIST_SPECIFIC_TID;
		start_tid = total_tids + 1;
	} while(total_tids < qnx_swap32(pidlist->num_tids));

	return;
}

void qnx_pidlist(char *args, int from_tty)
{
	struct dspidlist *pidlist = (void *)recv.pkt.okdata.data;
	struct tidinfo *tip;
	char specific_tid_supported = 0;
	pid_t pid, start_tid, total_tid;
	char subcmd;

	#if defined(__QNXNTO__) && defined(__QNXNTO_USE_PROCFS__)
	extern void procfs_pidlist(char *args, int from_tty);
        if ( qnx_desc == NULL )
                return procfs_pidlist(args, from_tty);
	#endif
	start_tid = 1;
	total_tid = 0;
	pid = 1;
	subcmd = DSMSG_PIDLIST_BEGIN;

	//
	// Send a DSMSG_PIDLIST_SPECIFIC_TID to see if it is supported
	// 
	qnx_send_init( DStMsg_pidlist, DSMSG_PIDLIST_SPECIFIC_TID, SET_CHANNEL_DEBUG );
	tran.pkt.pidlist.pid = qnx_swap32(pid);
	tran.pkt.pidlist.tid = qnx_swap32(start_tid);
	qnx_send(sizeof(tran.pkt.pidlist), 0);

	if (recv.pkt.hdr.cmd == DSrMsg_err)
		specific_tid_supported = 0;
	else
		specific_tid_supported = 1;

	while( 1 ) 
	{
		qnx_send_init( DStMsg_pidlist, subcmd, SET_CHANNEL_DEBUG );
		tran.pkt.pidlist.pid = qnx_swap32(pid);
		tran.pkt.pidlist.tid = qnx_swap32(start_tid);
		qnx_send(sizeof(tran.pkt.pidlist), 0);
		if (recv.pkt.hdr.cmd == DSrMsg_err) 
		{
			errno = qnx_swap32(recv.pkt.err.err);
			return;
		}
		if (recv.pkt.hdr.cmd != DSrMsg_okdata) 
		{
			errno = EOK;
			return;
		}

		for ( tip = (void *) &pidlist->name[(strlen(pidlist->name) + 1 + 3) & ~3]; tip->tid != 0; tip++ ) 
		{
			printf_filtered("%s - %ld/%d\n", pidlist->name, qnx_swap32(pidlist->pid), qnx_swap16(tip->tid) );
			total_tid ++;
		}
		pid = qnx_swap32(pidlist->pid);
		if(specific_tid_supported)
		{
			if(total_tid < qnx_swap32(pidlist->num_tids))
			{
				subcmd = DSMSG_PIDLIST_SPECIFIC_TID;
				start_tid = total_tid +1;
				continue;
			}
		}
		start_tid = 1;
		total_tid = 0;
		subcmd = DSMSG_PIDLIST_NEXT; 
	}
    return;
}

struct dsmapinfo *qnx_mapinfo( uint32_t addr, int first, int elfonly )
{
	//struct dsmapinfo	*map = (void *)recv.pkt.okdata.data;
	struct dsmapinfo	map;
	static struct dsmapinfo	dmap;
	DStMsg_mapinfo_t	*mapinfo = (DStMsg_mapinfo_t *)&tran.pkt;
	char			subcmd;

	if ( core_bfd != NULL ) { /* have to implement corefile mapinfo */
		errno = EOK;
		return NULL;
	}

	subcmd = addr ? DSMSG_MAPINFO_SPECIFIC:
		first? DSMSG_MAPINFO_BEGIN:DSMSG_MAPINFO_NEXT;
	if ( elfonly )
		subcmd |= DSMSG_MAPINFO_ELF;

	qnx_send_init( DStMsg_mapinfo, subcmd, SET_CHANNEL_DEBUG );
	mapinfo->addr = qnx_swap32(addr);
	qnx_send(sizeof(*mapinfo), 0);
	if (recv.pkt.hdr.cmd == DSrMsg_err) {
		errno = qnx_swap32(recv.pkt.err.err);
		return NULL;
	}
	if (recv.pkt.hdr.cmd != DSrMsg_okdata) {
		errno = EOK;
		return NULL;
	}
	
	memset( &dmap, 0, sizeof(dmap) );
		memcpy( &map, &recv.pkt.okdata.data[0], sizeof( map ) );
	dmap.ino = qnx_swap64(map.ino);
	dmap.dev = qnx_swap32(map.dev);
	
	dmap.text.addr        = qnx_swap32(map.text.addr);
	dmap.text.size        = qnx_swap32(map.text.size);
	dmap.text.flags       = qnx_swap32(map.text.flags);
	dmap.text.debug_vaddr = qnx_swap32(map.text.debug_vaddr);
	dmap.text.offset      = qnx_swap64(map.text.offset);
	
	dmap.data.addr        = qnx_swap32(map.data.addr);
	dmap.data.size        = qnx_swap32(map.data.size);
	dmap.data.flags       = qnx_swap32(map.data.flags);
	dmap.data.debug_vaddr = qnx_swap32(map.data.debug_vaddr);
	dmap.data.offset      = qnx_swap64(map.data.offset);
	
	strcpy( dmap.name, map.name );
	
	return &dmap;
}

void qnx_meminfo(char *args, int from_tty)
{
	struct dsmapinfo	*dmp;
	int 			first = 1;

	#if defined(__QNXNTO__) && defined(__QNXNTO_USE_PROCFS__)
        extern void procfs_meminfo(char *args, int from_tty);
        if ( qnx_desc == NULL )
                return procfs_meminfo(args, from_tty);
	#endif

	while ( (dmp = qnx_mapinfo( 0, first, 0 )) != NULL ) 
	{
		first = 0;
		printf_filtered( "%s\n", dmp->name );
		printf_filtered( "\ttext=%08x bytes @ 0x%08x\n", dmp->text.size, dmp->text.addr );
		printf_filtered( "\t\tflags=%08x\n", dmp->text.flags );
		printf_filtered( "\t\tdebug=%08x\n", dmp->text.debug_vaddr );
		printf_filtered( "\t\toffset=%016llx\n", dmp->text.offset );
		if ( dmp->data.size ) 
		{
			printf_filtered( "\tdata=%08x bytes @ 0x%08x\n", dmp->data.size, dmp->data.addr );
			printf_filtered( "\t\tflags=%08x\n", dmp->data.flags );
			printf_filtered( "\t\tdebug=%08x\n", dmp->data.debug_vaddr );
			printf_filtered( "\t\toffset=%016llx\n", dmp->data.offset );
		}
		printf_filtered( "\tdev=0x%x\n", dmp->dev );
		printf_filtered( "\tino=0x%llx\n", dmp->ino );
	}
}

/*

   GLOBAL FUNCTION

   fetch_core_registers -- fetch current registers from core file

   SYNOPSIS

   void fetch_core_registers (char *core_reg_sect,
   unsigned core_reg_size,
   int which, CORE_ADDR reg_addr)

   DESCRIPTION

   Read the values of either the general register set (WHICH equals 0)
   or the floating point register set (WHICH equals 2) from the core
   file data (pointed to by CORE_REG_SECT), and update gdb's idea of
   their current values.  The CORE_REG_SIZE parameter is ignored.

   NOTES

   Use the indicated sizes to validate the gregset and fpregset
   structures.
 */

extern void nto_supply_gregset PARAMS((nto_gregset_t *));
extern void nto_supply_fpregset PARAMS((nto_fpregset_t *));

static void
fetch_core_registers(core_reg_sect, core_reg_size, which, reg_addr)
char *core_reg_sect;
unsigned core_reg_size;
int which;
CORE_ADDR reg_addr;				/* Unused in this version */
{
	nto_gregset_t gregset;
	nto_fpregset_t fpregset;

	nto_init_solib_absolute_prefix();

	if (which == 0) {
		memcpy((char *) &gregset, core_reg_sect, min(core_reg_size,sizeof(gregset)));
		nto_supply_gregset(&gregset);
	} else if (which == 2) {
		memcpy((char *) &fpregset, core_reg_sect, min(core_reg_size,sizeof(fpregset)));
		nto_supply_fpregset(&fpregset);
	}
}

#include "solist.h"
#include "solib-svr4.h"
/* struct lm_info, LM_ADDR and qnx_truncate_ptr are copied from
   solib-svr4.c to support qnx_relocate_section_addresses which is different
   from the svr4 version. */

struct lm_info
  {
    /* Pointer to copy of link map from inferior.  The type is char *
       rather than void *, so that we may use byte offsets to find the
       various fields without the need for a cast.  */
    char *lm;
  };

static CORE_ADDR
LM_ADDR (struct so_list *so)
{
  struct link_map_offsets *lmo = SVR4_FETCH_LINK_MAP_OFFSETS ();

  return (CORE_ADDR) extract_signed_integer (so->lm_info->lm + lmo->l_addr_offset, 
					     lmo->l_addr_size);
}

static CORE_ADDR
qnx_truncate_ptr (CORE_ADDR addr)
{
  if (TARGET_PTR_BIT == sizeof (CORE_ADDR) * 8)
    /* We don't need to truncate anything, and the bit twiddling below
       will fail due to overflow problems.  */
    return addr;
  else
    return addr & (((CORE_ADDR) 1 << TARGET_PTR_BIT) - 1);
}

#include "elf-bfd.h"
Elf_Internal_Phdr *find_load_phdr( bfd *abfd )
{
  Elf32_Internal_Phdr *phdr;
  unsigned int i;

  phdr = elf_tdata (abfd)->phdr;
  for (i = 0; i < elf_elfheader (abfd)->e_phnum; i++, phdr++) {
    if (phdr->p_type == PT_LOAD && (phdr->p_flags & PF_X))
      return phdr;
  }
  return NULL;
}


static void
qnx_relocate_section_addresses (struct so_list *so,
                                 struct section_table *sec)
{
  /* On some platforms, (ie. QNXnto, NetBSD) LM_ADDR is the assigned
     address, not the offset.
     The addresses are formed as follows:
     LM_ADDR is the target address where the shared library file
     is mapped, so the actual section start address is LM_ADDR plus
     the section offset within the shared library file.  The end
     address is that plus the section length.  Note that we pay no
     attention to the section start address as recorded in the
     library header.
  */
  Elf32_Internal_Phdr *phdr = find_load_phdr(sec->bfd);
  unsigned vaddr = phdr?phdr->p_vaddr:0;

  sec->addr    = qnx_truncate_ptr (sec->addr    + LM_ADDR (so) - vaddr);
  sec->endaddr = qnx_truncate_ptr (sec->endaddr + LM_ADDR (so) - vaddr);
}

/* Register that we are able to handle ELF file formats using standard
   procfs "regset" structures.  */

static struct core_fns regset_core_fns =
{
  bfd_target_elf_flavour,               /* core_flavour */
  default_check_format,                 /* check_format */
  default_core_sniffer,                 /* core_sniffer */
  fetch_core_registers,                 /* core_read_registers */
  NULL                                  /* next */
};
static int
nto_in_dynsym_resolve_code(CORE_ADDR pc)
{
	if(in_plt_section(pc, NULL))
		return 1;
	return 0;
}

void
_initialize_nto()
{
	int endian = 1;

	if(*(char *)&endian != 1) 
		host_endian = BFD_ENDIAN_BIG;
	else
		host_endian = BFD_ENDIAN_LITTLE;

	#ifdef __QNXNTO__
	munlockall();
	#endif

	init_qnx_ops();
	add_target(&qnx_ops);
	add_show_from_set(add_set_cmd("qnxtimeout", no_class,
				  var_integer, (char *)&qnx_timeout,
				  "Set timeout value for remote read.\n", &setlist),
				  &showlist);

	add_show_from_set(add_set_cmd("qnxinheritenv", no_class,
				  var_boolean, (char *)&qnx_inherit_env,
				  "Set where remote process inherits env from pdebug, or has it set by gdb\n", &setlist),
				  &showlist);

	add_show_from_set (add_set_cmd ("qnxremotecwd", class_support, var_string,
				  (char *) &qnx_remote_cwd,
				  "Set the working directory for the remote process",
				  &setlist), &showlist);

	add_info ("pidlist", qnx_pidlist, "pidlist" );
	add_info ("tidinfo", qnx_tidinfo, "List threads for current process." );
	add_info ("meminfo", qnx_meminfo, "memory information" );

	//
	// We use SIG45 for pulses, or something, so nostop, noprint
	// and pass them.
	//
	signal_stop_update  (target_signal_from_name("SIG45"), 0);
	signal_print_update (target_signal_from_name("SIG45"), 0);
	signal_pass_update  (target_signal_from_name("SIG45"), 1);

	/* register core file support */
	add_core_fns (&regset_core_fns);

	/* Our loader handles solib relocations slightly differently than svr4 */
	TARGET_SO_RELOCATE_SECTION_ADDRESSES = qnx_relocate_section_addresses;

	TARGET_SO_FIND_AND_OPEN_SOLIB = nto_find_and_open_solib;

	TARGET_SO_IN_DYNSYM_RESOLVE_CODE = nto_in_dynsym_resolve_code;
}

int
qnx_insert_hw_breakpoint(CORE_ADDR addr, char *contents_cache)
{
	if ( qnx_desc == NULL )
		return -1;

	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_insert_hw_breakpoint(addr %x, contents_cache %x)\n", addr, contents_cache);
	#endif

	qnx_send_init(DStMsg_brk, DSMSG_BRK_EXEC|DSMSG_BRK_HW, SET_CHANNEL_DEBUG);
	tran.pkt.brk.addr = qnx_swap32(addr);
	qnx_send(sizeof(tran.pkt.brk), 0);
	return recv.pkt.hdr.cmd == DSrMsg_err;
}

int
qnx_remove_hw_breakpoint(CORE_ADDR addr, char *contents_cache)
{
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_remove_hw_breakpoint(addr %x, contents_cache %x)\n", addr, contents_cache);
	#endif

	return qnx_remove_breakpoint(addr,contents_cache);
}

int qnx_stopped_by_watchpoint(void)
{
    return 0;
}


int qnx_hw_watchpoint (addr, len, type)
{
	unsigned subcmd;

	#if defined(__QNXNTO__) && defined(__QNXNTO_USE_PROCFS__)
	extern int procfs_hw_watchpoint(int,int,int);

	if ( qnx_desc == NULL ) 
	{
		return procfs_hw_watchpoint(addr,type,len);
	}
	#endif
	#if defined(QNX_DEBUG) && (QNX_DEBUG == 1)
	printf_unfiltered("qnx_hw_watchpoint(addr %x, len %x, type %x)\n", addr, len, type);
	#endif

	switch(type) 
	{
		case 1: /* Read */
			subcmd = DSMSG_BRK_RD;
			break;
		case 2: /* Read/Write */
			subcmd = DSMSG_BRK_WR;
			break;
		default: /* Modify */
			subcmd = DSMSG_BRK_MODIFY;
	}
	subcmd |= DSMSG_BRK_HW;

	qnx_send_init(DStMsg_brk, subcmd, SET_CHANNEL_DEBUG);
	tran.pkt.brk.addr = qnx_swap32(addr);
	tran.pkt.brk.size = qnx_swap32(len);
	qnx_send(sizeof(tran.pkt.brk), 0);
	return recv.pkt.hdr.cmd == DSrMsg_err ? -1:0;
}

int qnx_remove_hw_watchpoint (addr, len, type)
{
	return qnx_hw_watchpoint( addr, -1, type );
}

int qnx_insert_hw_watchpoint (addr, len, type)
{
	return qnx_hw_watchpoint( addr, len, type );
}

char **qnx_parse_redirection( char *pargv[], char **pin, char **pout, char **perr )
{
	char **argv;
	char *in, *out, *err, *p;
	int argc, i, n;

	for ( n = 0; pargv[n]; n++ );
	if ( n == 0 )
		return NULL;
	in = ""; out = ""; err = "";

	argv = calloc( n+1, sizeof argv[0] );
	if ( argv == NULL )
		nomem(0);
	argc = n;
	for ( i = 0, n = 0; n < argc; n++ ) 
	{
		p = pargv[n];
		if ( *p == '>' ) 
		{
			p++;
			if ( *p )
				out = p;
			else
				out = pargv[++n];
		}
		else if ( *p == '<' ) 
		{
			p++;
			if ( *p )
				in = p;
			else
				in = pargv[++n];
		}
		else if ( *p++ == '2' && *p++ == '>' ) 
		{
			if ( *p == '&' && *(p+1) == '1' ) 
				err = out;
			else if ( *p )
				err = p;
			else
				err = pargv[++n];
		}
		else
			argv[i++] = pargv[n];
	}
	*pin = in; *pout = out; *perr = err;
	return argv;
}

struct tidinfo *qnx_thread_info( pid_t pid, int16_t tid )
{
	struct dspidlist *pidlist = (void *)recv.pkt.okdata.data;
	struct tidinfo *tip;

	tran.pkt.pidlist.tid = tid;
	qnx_send_init( DStMsg_pidlist, DSMSG_PIDLIST_SPECIFIC_TID, SET_CHANNEL_DEBUG );
	tran.pkt.pidlist.pid = qnx_swap32(pid);
	qnx_send(sizeof(tran.pkt.pidlist), 0);

	if (recv.pkt.hdr.cmd == DSrMsg_err) 
	{
		qnx_send_init( DStMsg_pidlist, DSMSG_PIDLIST_SPECIFIC, SET_CHANNEL_DEBUG );
		tran.pkt.pidlist.pid = qnx_swap32(pid);
		qnx_send(sizeof(tran.pkt.pidlist), 1);
		if (recv.pkt.hdr.cmd == DSrMsg_err) 
		{
			errno = recv.pkt.err.err;
			return NULL;
		}
	}

	for ( tip = (void *) &pidlist->name[(strlen(pidlist->name) + 1 + 3) & ~3]; tip->tid != 0; tip++ ) 
	{
		if ( tid == qnx_swap16(tip->tid) )
		return tip;
	}

	return NULL;
}

char *qnx_pid_to_str(ptid_t ptid)
{
	static char buf[1024];
	int pid, tid, n;
	struct tidinfo *tip;

	pid = ptid_get_pid(ptid); // returns pid if listed, else returns ptid
	tid = ptid_get_tid(ptid); // returns tid if listed, else 0

	n = sprintf (buf, "process %d", pid);

	tip = qnx_thread_info( pid, tid );
	if ( tip != NULL ) 
		sprintf( &buf[n], " (state = 0x%02x)", tip->state );

	return buf;
}

CORE_ADDR qnx_getbase( void )
{
	uint64_t base_address;
	#if defined(__QNXNTO__) && defined(__QNXNTO_USE_PROCFS__)
	extern int procfs_getbase(void);

	if ( qnx_desc == NULL ) 
	{
		return procfs_getbase();
	}
	#endif

	qnx_send_init( DStMsg_base_address, 0, SET_CHANNEL_DEBUG );
	qnx_send( sizeof(tran.pkt.baseaddr), 0);

	if (recv.pkt.hdr.cmd == DSrMsg_err) 
	{
		errno = recv.pkt.err.err;
		return -1;
	}
	memcpy( &base_address, &recv.pkt.okdata.data[0], sizeof(base_address));
	if ( base_address == 0 )
		return (CORE_ADDR)-1;
	return (CORE_ADDR)qnx_swap64(base_address);
}

#define IO_PORT_HACKS
#ifdef IO_PORT_HACKS
static int qnx_read_ioport( long long addr, short len, void *dst )
{
	qnx_send_init(DStMsg_memrd, DSMSG_MEM_IO, SET_CHANNEL_DEBUG);
	tran.pkt.memrd.addr = qnx_swap64(addr);
	tran.pkt.memrd.size = qnx_swap16(len);
	len = qnx_send(sizeof(tran.pkt.memrd), 0) - sizeof(recv.pkt.hdr);

	if(recv.pkt.hdr.cmd == DSrMsg_okdata) 
	{
		memcpy( dst, recv.pkt.okdata.data, len );
		return 0;
	}
	errno = recv.pkt.err.err;
	printf( "error reading ioport %#llx: %s\n", addr, strerror(errno) );
	return -1;
}

void qnx_read_ioport_8( char *args, int from_tty )
{
	unsigned char data;
	long long addr;

	addr = strtol( args, NULL, 0 );
	if ( qnx_read_ioport( addr, 1, (void *)&data ) == 0 ) 
	{
		printf( "ioport @ %#llx: ", addr );
		printf( "0x%02x\n", (int)data );
	}
}

void qnx_read_ioport_16( char *args, int from_tty )
{
	unsigned short data;
	long long addr;

	addr = strtol( args, NULL, 0 );
	if ( qnx_read_ioport( addr, 2, (void *)&data ) == 0 ) 
	{
		printf( "ioport @ %#llx: ", addr );
		printf( "0x%04x\n", (int)data );
	}
}

void qnx_read_ioport_32( char *args, int from_tty )
{
	unsigned int data;
	long long addr;

	addr = strtol( args, NULL, 0 );
	if ( qnx_read_ioport( addr, 4, (void *)&data ) == 0 ) 
	{
		printf( "ioport @ %#llx: ", addr );
		printf( "0x%08x\n", (int)data );
	}
}

static int qnx_write_ioport( long long addr, short len, void *src )
{
	int i;

	qnx_send_init(DStMsg_memwr, DSMSG_MEM_IO, SET_CHANNEL_DEBUG);
	tran.pkt.memwr.addr = qnx_swap64(addr);
	printf("iowrite to %#llx: ", addr );
	memcpy(tran.pkt.memwr.data, src, len);

	for ( i = 0; i < len; i++ )
		printf("%02x ", tran.pkt.memwr.data[i] );
	printf("\n");

	qnx_send(offsetof(DStMsg_memwr_t, data) + len, 0);

	if(recv.pkt.hdr.cmd != DSrMsg_err) 
	{
		return 0;
	}
	errno = recv.pkt.err.err;
	printf_filtered( "error writing to ioport %#llx: %s\n", addr, strerror(errno) );
	return -1;
}

void qnx_write_ioport_8( char *args, int from_tty )
{
	unsigned char data = 0;
	long long addr = 0;
	char	*p;

	p = strtok( args, " ,\t" );
	if ( p != NULL )
		addr = strtol( p, NULL, 0 );
	p = strtok( NULL, " ,\t" );
	if ( p != NULL )
		data = (unsigned char)strtol( p, NULL, 0 );
	qnx_write_ioport( addr, 1, (void *)&data );
}

void qnx_write_ioport_16( char *args, int from_tty )
{
	unsigned char data = 0;
	long long addr = 0;
	char	*p;

	p = strtok( args, " ,\t" );
	if ( p != NULL )
		addr = strtol( p, NULL, 0 );
	p = strtok( NULL, " ,\t" );
	if ( p != NULL )
		data = (unsigned char)strtol( p, NULL, 0 );
	qnx_write_ioport( addr, 1, (void *)&data );
}

void qnx_write_ioport_32( char *args, int from_tty )
{
	unsigned char data = 0;
	long long addr = 0;
	char	*p;

	p = strtok( args, " ,\t" );
	if ( p != NULL )
		addr = strtol( p, NULL, 0 );
	p = strtok( NULL, " ,\t" );
	if ( p != NULL )
		data = (unsigned char)strtol( p, NULL, 0 );
	qnx_write_ioport( addr, 1, (void *)&data );
}
#endif

#ifndef __QNXNTO__

#define QNXSIGHUP      1   /* hangup */
#define QNXSIGINT      2   /* interrupt */
#define QNXSIGQUIT     3   /* quit */
#define QNXSIGILL      4   /* illegal instruction (not reset when caught) */
#define QNXSIGTRAP     5   /* trace trap (not reset when caught) */
#define QNXSIGIOT      6   /* IOT instruction */
#define QNXSIGABRT     6   /* used by abort */
#define QNXSIGEMT      7   /* EMT instruction */
#define QNXSIGDEADLK   7   /* Mutex deadlock */
#define QNXSIGFPE      8   /* floating point exception */
#define QNXSIGKILL     9   /* kill (cannot be caught or ignored) */
#define QNXSIGBUS      10  /* bus error */
#define QNXSIGSEGV     11  /* segmentation violation */
#define QNXSIGSYS      12  /* bad argument to system call */
#define QNXSIGPIPE     13  /* write on pipe with no reader */
#define QNXSIGALRM     14  /* real-time alarm clock */
#define QNXSIGTERM     15  /* software termination signal from kill */
#define QNXSIGUSR1     16  /* user defined signal 1 */
#define QNXSIGUSR2     17  /* user defined signal 2 */
#define QNXSIGCHLD     18  /* death of child */
#define QNXSIGPWR      19  /* power-fail restart */
#define QNXSIGWINCH    20  /* window change */
#define QNXSIGURG      21  /* urgent condition on I/O channel */
#define QNXSIGPOLL     22  /* System V name for QNXSIGIO */
#define QNXSIGIO       QNXSIGPOLL
#define QNXSIGSTOP     23  /* sendable stop signal not from tty */
#define QNXSIGTSTP     24  /* stop signal from tty */
#define QNXSIGCONT     25  /* continue a stopped process */
#define QNXSIGTTIN     26  /* attempted background tty read */
#define QNXSIGTTOU     27  /* attempted background tty write */
#define QNXSIGVTALRM   28  /* virtual timer expired */
#define QNXSIGPROF     29  /* profileing timer expired */
#define QNXSIGXCPU     30  /* exceded cpu limit */
#define QNXSIGXFSZ     31  /* exceded file size limit */

#endif

/* Convert qnx signal to gdb signal.  */
enum target_signal
target_signal_from_qnx(sig)
     int sig;
{
#ifndef __QNXNTO__
  switch(sig)
  {
    case 0: return 0; break;
    case QNXSIGHUP: return TARGET_SIGNAL_HUP; break;
    case QNXSIGINT: return TARGET_SIGNAL_INT; break;
    case QNXSIGQUIT: return TARGET_SIGNAL_QUIT; break;
    case QNXSIGILL: return TARGET_SIGNAL_ILL; break;
    case QNXSIGTRAP: return TARGET_SIGNAL_TRAP; break;
    case QNXSIGABRT: return TARGET_SIGNAL_ABRT; break;
    case QNXSIGEMT: return TARGET_SIGNAL_EMT; break;
    case QNXSIGFPE: return TARGET_SIGNAL_FPE; break;
    case QNXSIGKILL: return TARGET_SIGNAL_KILL; break;
    case QNXSIGBUS: return TARGET_SIGNAL_BUS; break;
    case QNXSIGSEGV: return TARGET_SIGNAL_SEGV; break;
    case QNXSIGSYS: return TARGET_SIGNAL_SYS; break;
    case QNXSIGPIPE: return TARGET_SIGNAL_PIPE; break;
    case QNXSIGALRM: return TARGET_SIGNAL_ALRM; break;
    case QNXSIGTERM: return TARGET_SIGNAL_TERM; break;
    case QNXSIGUSR1: return TARGET_SIGNAL_USR1; break;
    case QNXSIGUSR2: return TARGET_SIGNAL_USR2; break;
    case QNXSIGCHLD: return TARGET_SIGNAL_CHLD; break;
    case QNXSIGPWR: return TARGET_SIGNAL_PWR; break;
    case QNXSIGWINCH: return TARGET_SIGNAL_WINCH; break;
    case QNXSIGURG: return TARGET_SIGNAL_URG; break;
    case QNXSIGPOLL: return TARGET_SIGNAL_POLL; break;
//	case QNXSIGIO: return TARGET_SIGNAL_IO; break;
    case QNXSIGSTOP: return TARGET_SIGNAL_STOP; break;
    case QNXSIGTSTP: return TARGET_SIGNAL_TSTP; break;
    case QNXSIGCONT: return TARGET_SIGNAL_CONT; break;
    case QNXSIGTTIN: return TARGET_SIGNAL_TTIN; break;
    case QNXSIGTTOU: return TARGET_SIGNAL_TTOU; break;
    case QNXSIGVTALRM: return TARGET_SIGNAL_VTALRM; break;
    case QNXSIGPROF: return TARGET_SIGNAL_PROF; break;
    case QNXSIGXCPU: return TARGET_SIGNAL_XCPU; break;
    case QNXSIGXFSZ: return TARGET_SIGNAL_XFSZ; break;
    default: break;
  }
#endif /* __QNXNTO__ */
  return target_signal_from_host(sig);
}


/* Convert gdb signal to qnx signal.  */
enum target_signal
target_signal_to_qnx(sig)
     int sig;
{
#ifndef __QNXNTO__
  switch(sig)
  {
    case 0: return 0; break;
    case TARGET_SIGNAL_HUP: return QNXSIGHUP; break;
    case TARGET_SIGNAL_INT: return QNXSIGINT; break;
    case TARGET_SIGNAL_QUIT: return QNXSIGQUIT; break;
    case TARGET_SIGNAL_ILL: return QNXSIGILL; break;
    case TARGET_SIGNAL_TRAP: return QNXSIGTRAP; break;
    case TARGET_SIGNAL_ABRT: return QNXSIGABRT; break;
    case TARGET_SIGNAL_EMT: return QNXSIGEMT; break;
    case TARGET_SIGNAL_FPE: return QNXSIGFPE; break;
    case TARGET_SIGNAL_KILL: return QNXSIGKILL; break;
    case TARGET_SIGNAL_BUS: return QNXSIGBUS; break;
    case TARGET_SIGNAL_SEGV: return QNXSIGSEGV; break;
    case TARGET_SIGNAL_SYS: return QNXSIGSYS; break;
    case TARGET_SIGNAL_PIPE: return QNXSIGPIPE; break;
    case TARGET_SIGNAL_ALRM: return QNXSIGALRM; break;
    case TARGET_SIGNAL_TERM: return QNXSIGTERM; break;
    case TARGET_SIGNAL_USR1: return QNXSIGUSR1; break;
    case TARGET_SIGNAL_USR2: return QNXSIGUSR2; break;
    case TARGET_SIGNAL_CHLD: return QNXSIGCHLD; break;
    case TARGET_SIGNAL_PWR: return QNXSIGPWR; break;
    case TARGET_SIGNAL_WINCH: return QNXSIGWINCH; break;
    case TARGET_SIGNAL_URG: return QNXSIGURG; break;
    case TARGET_SIGNAL_IO: return QNXSIGIO; break;
    case TARGET_SIGNAL_POLL: return QNXSIGPOLL; break;
    case TARGET_SIGNAL_STOP: return QNXSIGSTOP; break;
    case TARGET_SIGNAL_TSTP: return QNXSIGTSTP; break;
    case TARGET_SIGNAL_CONT: return QNXSIGCONT; break;
    case TARGET_SIGNAL_TTIN: return QNXSIGTTIN; break;
    case TARGET_SIGNAL_TTOU: return QNXSIGTTOU; break;
    case TARGET_SIGNAL_VTALRM: return QNXSIGVTALRM; break;
    case TARGET_SIGNAL_PROF: return QNXSIGPROF; break;
    case TARGET_SIGNAL_XCPU: return QNXSIGXCPU; break;
    case TARGET_SIGNAL_XFSZ: return QNXSIGXFSZ; break;
    default: break;
  }
#endif /* __QNXNTO__ */
  return target_signal_to_host(sig);
}

#ifdef __CYGWIN__
char default_qnx_target[] = "C:\\QNXsdk\\target\\qnx6";
#elif defined(__QNX__) && !defined(__QNXNTO__)
char default_qnx_target[] = "/usr/nto";
#elif defined(__sun__) || defined(linux)
char default_qnx_target[] = "/opt/QNXsdk/target/qnx6";
#else
char default_qnx_target[] = "";
#endif

char *qnx_target(void)
{
  char *p = getenv("QNX_TARGET");

#ifdef __CYGWIN__
  static char buf[PATH_MAX];
  if(p)
    cygwin_conv_to_posix_path(p, buf);
  else
    cygwin_conv_to_posix_path(default_qnx_target, buf);
  return buf;
#else
  return p ? p : default_qnx_target;
#endif
}

#include <solist.h>

int
proc_iterate_over_mappings (int (*func) (int, CORE_ADDR))
{
struct dsmapinfo	*lm;
char *tmp_pathname;
int fd;

	lm = qnx_mapinfo( 0, 1, 1 );
	while ( lm != NULL ) {
		if ( (fd = solib_open( lm->name, &tmp_pathname )) != -1 ) {
			func( fd, lm->text.addr );
			if ( tmp_pathname )
				free( tmp_pathname );
		}
		lm = qnx_mapinfo( 0, 0, 1 );
	}
	return 0;
}

int
nto_find_and_open_solib (char *solib, unsigned o_flags, char **temp_pathname)
{
  char *buf, arch_path[PATH_MAX], *nto_root, *endian, *base;
  const char *arch;
  char *path_fmt = "%s/lib:%s/usr/lib:%s/usr/photon/lib\
:%s/usr/photon/dll:%s/lib/dll";
  int ret;

  nto_root = qnx_target ();
  if (strcmp (TARGET_ARCHITECTURE->arch_name, "i386") == 0)
    {
      arch = "x86";
      endian = "";
    }
  else if (strcmp (TARGET_ARCHITECTURE->arch_name, "rs6000") == 0
          || strcmp (TARGET_ARCHITECTURE->arch_name, "powerpc") == 0)
    {
      arch = "ppc";
      endian = "be";
    }
  else
    {
      arch = TARGET_ARCHITECTURE->arch_name;
      endian = TARGET_BYTE_ORDER == BFD_ENDIAN_BIG ? "be" : "le";
    }

  sprintf (arch_path, "%s/%s%s", nto_root, arch, endian);

  buf = alloca (strlen (path_fmt) + strlen (arch_path) * 5 + 1);
  sprintf (buf, path_fmt, arch_path, arch_path, arch_path, arch_path,
	   arch_path);

  /* Don't assume basename() isn't destructive.  */
  base = strrchr (solib, '/');
  if (!base)
    base = solib;
  else
    base++; /* Skip over '/'.  */
  
  ret = openp (buf, 1, base, o_flags, 0, temp_pathname);
    if (ret < 0 && base != solib){
      sprintf(arch_path, "/%s", solib);
      ret = open(arch_path, o_flags, 0);
      if(temp_pathname)
        *temp_pathname = gdb_realpath(arch_path);
  }
  return ret;
}

void
nto_init_solib_absolute_prefix (void)
{
  char buf[PATH_MAX * 2], arch_path[PATH_MAX];
  char *nto_root, *endian;
  const char *arch;
  char *sap_cmd = "solib-absolute-prefix";
  struct cmd_list_element *c;

  c = lookup_cmd (&sap_cmd, setlist, "", -1, 1);

  /* Only do this if it hasn't been set. */
  if (!c || *((char **)c->var) != NULL)
    return;

  nto_root = qnx_target ();
  if (strcmp (TARGET_ARCHITECTURE->arch_name, "i386") == 0)
    {
      arch = "x86";
      endian = "";
    }
  else if (strcmp (TARGET_ARCHITECTURE->arch_name, "rs6000") == 0
          || strcmp (TARGET_ARCHITECTURE->arch_name, "powerpc") == 0)
    {
      arch = "ppc";
      endian = "be";
    }
  else
    {
      arch = TARGET_ARCHITECTURE->arch_name;
      endian = TARGET_BYTE_ORDER == BFD_ENDIAN_BIG ? "be" : "le";
    }

  sprintf (arch_path, "%s/%s%s", nto_root, arch, endian);

  sprintf (buf, "set solib-absolute-prefix %s", arch_path);
  execute_command (buf, 0);
}
