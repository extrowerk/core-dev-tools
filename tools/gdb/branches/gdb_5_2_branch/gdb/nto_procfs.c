/* Machine independent support for QNX Neutrino /proc (process file system) for GDB.
   Copyright 1991, 1992-99, 2000 Free Software Foundation, Inc.
   Written by Colin Burgess at QNX Software Systems Limited.  

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

/* native only */
#ifdef __QNXNTO__
#include "defs.h"

#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/debug.h>
#include <sys/procfs.h>
#include <sys/neutrino.h>
#include <sys/types.h>
#include <sys/syspage.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/netmgr.h>

#include "defs.h"
#include "gdb_string.h"
#include "gdbcore.h"
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
#include "gdb_stat.h"
#include "command.h"
#include "solist.h"

#define NULL_PID		0
#define _DEBUG_FLAG_TRACE	(_DEBUG_FLAG_TRACE_EXEC|_DEBUG_FLAG_TRACE_RD|_DEBUG_FLAG_TRACE_WR|_DEBUG_FLAG_TRACE_MODIFY)

static struct target_ops procfs_ops;
int ctl_fd;
static void (*ofunc) ();
static procfs_run run;

static void procfs_open PARAMS ((char *, int));
static int procfs_can_run PARAMS ((void));
static ptid_t procfs_wait PARAMS ((ptid_t, struct target_waitstatus *));
static int procfs_xfer_memory
       PARAMS ((CORE_ADDR, char *, int, int, struct mem_attrib *attrib, struct target_ops *));
static void procfs_fetch_registers PARAMS ((int));
static void notice_signals (void);

static void init_procfs_ops PARAMS ((void));
static ptid_t do_attach PARAMS ((ptid_t ptid));

extern char **qnx_parse_redirection 
       PARAMS (( char *start_argv[], char **in, char **out, char **err ));
extern short int qnx_swap16 PARAMS((int val));
extern int nto_find_and_open_solib PARAMS((char *, unsigned, char **));
extern void nto_init_solib_absolute_prefix PARAMS((void));
extern unsigned qnx_cpu_register_area 
       PARAMS((unsigned first_regno, unsigned last_regno, unsigned char *subcmd, unsigned *off, unsigned *len));
extern int qnx_cpu_register_store 
       PARAMS((int endian, unsigned first_regno, unsigned last_regno, void *data));

/* 
   These two globals are only ever set in procfs_open(), but are
   referenced elsewhere.  'nto_procfs_node' is a flag used to say
   whether we are local, or we should get the current node descriptor
   for the remote QNX node.
*/ 
static char nto_procfs_path[PATH_MAX] = {"/proc"};
static uint32_t nto_procfs_node = ND_LOCAL_NODE;

/* 
   This is a simple wrapper for the netmgr_strtond() function.  It is
   only called from the QNX_NODE macro declared below.  The reason for
   the macro and function are because QNX node descriptors are transient,
   so we have to re-acquire them every time.
*/
static uint32_t 
procfs_qnx_node(uint32_t node)
{
  if(node == -1)
  {
    error("Lost the QNX node.  Debug session probably over.");
    /* NOTREACHED */
  }
  return(node);
}

/* 
   This define returns the current QNX Node, or -1 on error.  It calls the above
   function which is a simple wrapper for the error() call.
*/
#define QNX_NODE (ND_NODE_CMP(nto_procfs_node, ND_LOCAL_NODE) == 0 ? ND_LOCAL_NODE : \
                   procfs_qnx_node(netmgr_strtond(nto_procfs_path, 0)))

/* 
   This is called when we call 'target procfs <arg>' from the (gdb) prompt.
   For QNX6 (nto), the only valid arg will be a QNX node string, 
   eg: "/net/gp".  If arg is not a valid QNX node, we will default to local. 
*/
static void
procfs_open (arg, from_tty)
	 char *arg;
	 int from_tty;
{
  char             *nodestr;
  char             *endstr;
  char             buffer[50];
  int              fd, total_size;
  procfs_sysinfo   *sysinfo;

  // Set the default node used for spawning to this one, and only override it
  // if there is a valid arg.

  nto_procfs_node = ND_LOCAL_NODE;
  nodestr = arg ? strdup(arg) : arg;

  init_thread_list ();

  if(nodestr)
  {
    nto_procfs_node = netmgr_strtond(nodestr, &endstr);
    if(nto_procfs_node == -1)
    {
      if(errno == ENOTSUP)
        printf_filtered("QNX Net Manager not found.\n");
      printf_filtered("Invalid QNX node %s: error %d (%s).\n", nodestr, errno, strerror(errno));
      free(nodestr);
      nodestr = NULL;
      nto_procfs_node = ND_LOCAL_NODE;
    }
    else
    if(*endstr)
    {
      if(*(endstr -1) == '/')
        *(endstr -1) = 0;
      else
        *endstr = 0;
    }
  }
  sprintf(nto_procfs_path,"%s%s", nodestr ? nodestr : "", "/proc");
  if(nodestr)
    free(nodestr);

  fd = open(nto_procfs_path, O_RDONLY);
  if(fd == -1)
  {
    printf_filtered("Error opening %s : %d (%s)\n", nto_procfs_path, errno, strerror(errno));
    error("Invalid procfs arg");
    /* NOTREACHED */
  }

  sysinfo = (void *)buffer;
  if (devctl(fd, DCMD_PROC_SYSINFO, sysinfo, sizeof buffer, 0) != EOK)
  {
    printf_filtered("Error getting size: %d (%s)\n", errno, strerror(errno));
    close(fd);
    error("Devctl failed.");
    /* NOTREACHED */
  }
  else
  {
    total_size = sysinfo->total_size;
    if(!(sysinfo = alloca(total_size)))
    {
      printf_filtered("Memory error: %d (%s)\n", errno, strerror(errno));
      close(fd);
      error("alloca failed.");
      /* NOTREACHED */
    }
    else
    {
      if (devctl(fd, DCMD_PROC_SYSINFO, sysinfo, total_size, 0) != EOK)
      {
        printf_filtered("Error getting sysinfo: %d (%s)\n", errno, strerror(errno));
        close(fd);
        error("Devctl failed.");
        /* NOTREACHED */
      }
      else
      {
        if(sysinfo->type != QNX_TARGET_CPUTYPE)
        {
          close(fd);
          error("Invalid target CPU.");
          /* NOTREACHED */
        }
      }
    }
  }
  close(fd);
  printf_filtered("Debugging using %s\n", nto_procfs_path);

}

static void
procfs_set_thread(ptid)
     ptid_t ptid;
{
    pid_t tid;

    tid = ptid_get_tid(ptid);
    devctl( ctl_fd, DCMD_PROC_CURTHREAD, &tid, sizeof(tid), 0 );
}

/*  Return nonzero if the thread TH is still alive.  */
static int
procfs_thread_alive(ptid_t ptid)
{
	pid_t		tid;

	tid = ptid_get_tid(ptid);
	if ( devctl( ctl_fd, DCMD_PROC_CURTHREAD, &tid, sizeof(tid), 0 ) == EOK )
    		return 1;
	return 0;
}


void procfs_find_new_threads(void)
{
	procfs_status status;
	pid_t pid;
	ptid_t ptid;

	if ( ctl_fd == -1 )
		return;

	pid = ptid_get_pid(inferior_ptid);

	for ( status.tid = 1; ; ++status.tid ) 
	{
		if ( devctl( ctl_fd, DCMD_PROC_TIDSTATUS, &status, sizeof(status), 0) != EOK && status.tid != 0 ) 
		{
			break;
		}
		ptid = ptid_build(pid, 0, qnx_swap16(status.tid));
		if (!in_thread_list(ptid))
			add_thread(ptid);
	}
	return;
}

/* FIXME: this is probably redundant in some way with find_new_threads above.  */
void procfs_tidinfo(char *args, int from_tty)
{
	int                     fd = -1;
	char                    buf[512];
	procfs_info             *pidinfo = NULL;
	procfs_debuginfo        *info = NULL;
	procfs_status           *status = NULL;
	pid_t			num_threads = 0, cur_pid, cur_tid;
	char			name[512];

	cur_pid = ptid_get_pid(inferior_ptid);
	cur_tid = ptid_get_tid(inferior_ptid);

        if(!cur_pid){
		fprintf_unfiltered(gdb_stderr, "No inferior.\n");
		return;
	}

	sprintf(buf, "%s/%d/as", nto_procfs_path, cur_pid);
	// open the procfs path
	if((fd = open(buf, O_RDONLY)) == -1) 
	{
		printf("failed to open %s - %d (%s)\n", buf, errno, strerror(errno));
		return;
	}

	pidinfo = (procfs_info*)buf;
	if (devctl(fd, DCMD_PROC_INFO, pidinfo, sizeof(buf), 0) != EOK)
	{
		printf("devctl DCMD_PROC_INFO failed - %d (%s)\n", errno, strerror(errno));
		close(fd);
		return;
	}
	num_threads = pidinfo->num_threads;

	info = (procfs_debuginfo *)buf;
	if(devctl(fd, DCMD_PROC_MAPDEBUG_BASE, info, sizeof(buf), 0) != EOK) 
		strcpy(name, "unavailable");
	else
		strcpy(name, info->path);

	//
	// Collect state info on all the threads.
	//
	status = (procfs_status *)buf;
	printf_filtered("Threads for pid %d (%s)\nTid:\tState:\tFlags:\n", cur_pid, name);
	for(status->tid = 1; status->tid <= num_threads; status->tid++) 
	{
		ptid_t ptid = ptid_build(cur_pid, 0, status->tid);
		if(devctl(fd, DCMD_PROC_TIDSTATUS, status, sizeof(buf), 0) != EOK && status->tid != 0) 
			break;
		if(status->tid != 0){
			if (!in_thread_list(ptid))
				add_thread(ptid);
			printf_filtered("%c%d\t%d\t%d\n", status->tid == cur_tid ? '*' : ' ', status->tid,
				       	status->state, status->flags );
		}
	}
	close(fd);
 
	return;
}

void procfs_pidlist(char *args, int from_tty)
{
	static DIR              *dp = NULL;
	struct dirent           *dirp = NULL;
	int                     fd = -1;
	char                    buf[512];
	procfs_info             *pidinfo = NULL;
	procfs_debuginfo        *info = NULL;
	procfs_status           *status = NULL;
	pid_t			num_threads = 0;
	pid_t			pid;
	char			name[512];

	dp = opendir(nto_procfs_path);
	if(dp == NULL) 
	{
		printf("failed to opendir \"%s\" - %d (%s)", nto_procfs_path, errno, strerror(errno)); 
		return;
	}

	// start scan at first pid
	rewinddir(dp);

	do
	{
		// Get the right pid and procfs path for the pid
		do 
		{
			if((dirp = readdir(dp)) == NULL)
			{
				closedir(dp);
				return;
			}
			strcat(strcat(strcat(strcpy(buf, nto_procfs_path), "/"), dirp->d_name), "/as");
		} while((pid = atoi(dirp->d_name)) == 0);

		// open the procfs path
		if((fd = open(buf, O_RDONLY)) == -1) 
		{
			printf("failed to open %s - %d (%s)\n", buf, errno, strerror(errno));
			closedir(dp);
			return;
		}

		pidinfo = (procfs_info*)buf;
		if (devctl(fd, DCMD_PROC_INFO, pidinfo, sizeof(buf), 0) != EOK)
		{
			printf("devctl DCMD_PROC_INFO failed - %d (%s)\n", errno, strerror(errno));
			break;
		}
		num_threads = pidinfo->num_threads;

		info = (procfs_debuginfo *)buf;
		if(devctl(fd, DCMD_PROC_MAPDEBUG_BASE, info, sizeof(buf), 0) != EOK) 
			strcpy(name, "unavailable");
		else
			strcpy(name, info->path);

		//
		// Collect state info on all the threads.
		//
		status = (procfs_status *)buf;
		for(status->tid = 1; status->tid <= num_threads; status->tid++) 
		{
			if(devctl(fd, DCMD_PROC_TIDSTATUS, status, sizeof(buf), 0) != EOK && status->tid != 0) 
				break;
			if(status->tid != 0)
				printf_filtered("%s - %d/%d\n", name, pid, status->tid );
		}
		close(fd);
	} while(dirp != NULL);
 
	close(fd);
	closedir(dp);
	return;
}


void procfs_meminfo(char *args, int from_tty)
{
	procfs_mapinfo		*mapinfos = NULL;
	static int		num_mapinfos = 0;
	procfs_mapinfo		*mapinfo_p, *mapinfo_p2;
	int 			flags = ~0, err, num, i, j;

	struct
	{
		procfs_debuginfo        info;
		char                buff[_POSIX_PATH_MAX];
	} map;

	struct info 
	{
		uint32_t	addr;
		uint32_t	size;
		uint32_t	flags;
		uint32_t	debug_vaddr;
		uint64_t	offset;
	};

	struct printinfo 
	{
		uint64_t	ino;
		uint32_t	dev;
		struct info	text;
		struct info	data;
		char		name[256];
	}printme;


	// Get the number of map entrys
	if((err = devctl(ctl_fd, DCMD_PROC_MAPINFO, NULL, 0, &num )) != EOK)
	{
		printf("failed devctl num mapinfos - %d (%s)\n", err, strerror(err));
		return;
	}

	// malloc enough memory for all of them
	if ( (mapinfos = malloc( num*sizeof(procfs_mapinfo) )) == NULL )
	{
		printf("failed malloc - %d (%s)\n", err, strerror(err));
		return;
	}

	num_mapinfos = num;
	mapinfo_p = mapinfos;

	// fill the map entrys	
	if((err = devctl(ctl_fd, DCMD_PROC_MAPINFO, mapinfo_p, num*sizeof(procfs_mapinfo), &num)) != EOK)
	{
		printf("failed devctl mapinfos - %d (%s)\n", err, strerror(err));
		free(mapinfos);
		return;
	}

	num = min(num,num_mapinfos);

	//
	// Run through the list of mapinfo's, and store the data and text info
	// so we can print it at the bottom of the loop.
	//
	for ( mapinfo_p = mapinfos, i = 0; i < num; i++, mapinfo_p++ )
	{
		if ( !(mapinfo_p->flags & flags) )
			mapinfo_p->ino = 0;

		if ( mapinfo_p->ino == 0 ) /* already visited */
			continue;

		map.info.vaddr = mapinfo_p->vaddr;

		if((err = devctl(ctl_fd, DCMD_PROC_MAPDEBUG, &map, sizeof(map), 0)) != EOK)
			continue;

		memset( &printme, 0, sizeof printme );
		printme.dev = mapinfo_p->dev;
		printme.ino = mapinfo_p->ino;
		printme.text.addr = mapinfo_p->vaddr;
		printme.text.size = mapinfo_p->size;
		printme.text.flags = mapinfo_p->flags;
		printme.text.offset = mapinfo_p->offset;
		printme.text.debug_vaddr = map.info.vaddr;
		strcpy( printme.name, map.info.path );

		/* check for matching data */
		for ( mapinfo_p2 = mapinfos, j = 0; j < num; j++, mapinfo_p2++ )
		{
			if ( mapinfo_p2->vaddr != mapinfo_p->vaddr && 
			     mapinfo_p2->ino == mapinfo_p->ino && mapinfo_p2->dev == mapinfo_p->dev )
			{
				map.info.vaddr = mapinfo_p2->vaddr;
				if((err = devctl(ctl_fd, DCMD_PROC_MAPDEBUG, &map, sizeof(map), 0)) != EOK)
					continue;

				if (strcmp( map.info.path, printme.name ) ) {
					continue;
				}

				/*
				 * lower debug_vaddr is always text, if nessessary, swap
				 */
				if ((int) map.info.vaddr < (int) printme.text.debug_vaddr) 
				{
					memcpy(&(printme.data), &(printme.text), sizeof(printme.data));
					printme.text.addr = mapinfo_p2->vaddr;
					printme.text.size = mapinfo_p2->size;
					printme.text.flags = mapinfo_p2->flags;
					printme.text.offset = mapinfo_p2->offset;
					printme.text.debug_vaddr = map.info.vaddr;
				}
				else
				{
					printme.data.addr =  mapinfo_p2->vaddr;
					printme.data.size =  mapinfo_p2->size;
					printme.data.flags =  mapinfo_p2->flags;
					printme.data.offset =  mapinfo_p2->offset;
					printme.data.debug_vaddr = map.info.vaddr;
				}
				mapinfo_p2->ino = 0;
			}
		}
		mapinfo_p->ino = 0;

		printf_filtered( "%s\n", printme.name);
		printf_filtered( "\ttext=%08x bytes @ 0x%08x\n",  printme.text.size, printme.text.addr);
		printf_filtered( "\t\tflags=%08x\n", printme.text.flags );
		printf_filtered( "\t\tdebug=%08x\n", printme.text.debug_vaddr );
		printf_filtered( "\t\toffset=%016llx\n", printme.text.offset );
		if ( printme.data.size ) 
		{
			printf_filtered( "\tdata=%08x bytes @ 0x%08x\n", printme.data.size, printme.data.addr );
			printf_filtered( "\t\tflags=%08x\n", printme.data.flags );
			printf_filtered( "\t\tdebug=%08x\n", printme.data.debug_vaddr );
			printf_filtered( "\t\toffset=%016llx\n", printme.data.offset );
		}
		printf_filtered( "\tdev=0x%x\n", printme.dev );
		printf_filtered( "\tino=0x%x\n", (unsigned int)printme.ino );
	}
	free(mapinfos);
	return;
}

/* Print status information about what we're accessing.  */

static void
procfs_files_info (ignore)
	 struct target_ops *ignore;
{
	printf_unfiltered ("\tUsing the running image of %s %s via %s.\n",
					   attach_flag ? "attached" : "child",
					   target_pid_to_str (inferior_ptid),
					   nto_procfs_path);
}

/* Mark our target-struct as eligible for stray "run" and "attach" commands.  */
static int
procfs_can_run ()
{
	return 1;
}

static int
procfs_can_async ()
{
	return 0;
}

/* Attach to process PID, then initialize for debugging it */
static void
procfs_attach (args, from_tty)
	 char *args;
	 int from_tty;
{
	char *exec_file;
	int pid;

	if (!args)
		error_no_arg ("process-id to attach");

	pid = atoi (args);

	if (pid == getpid ())	
		error ("Attaching GDB to itself is not a good idea...");

	if (from_tty)
	  {
		  exec_file = (char *) get_exec_file (0);

		  if (exec_file)
			  printf_unfiltered ("Attaching to program `%s', %s\n", exec_file,
								 target_pid_to_str (pid_to_ptid(pid)));
		  else
			  printf_unfiltered ("Attaching to %s\n",
								 target_pid_to_str (pid_to_ptid(pid)));

		  gdb_flush (gdb_stdout);
	  }
	inferior_ptid = do_attach (pid_to_ptid(pid));
	push_target (&procfs_ops);
}

static void
procfs_post_attach(pid_t pid)
{
	#ifdef SOLIB_CREATE_INFERIOR_HOOK
	if ( exec_bfd != NULL || (symfile_objfile != NULL && symfile_objfile->obfd != NULL) )
	    SOLIB_CREATE_INFERIOR_HOOK (pid);
	#endif
}

static ptid_t
do_attach (ptid_t ptid)
{
	procfs_status status;
	struct sigevent event;
	char path[100];

	sprintf (path, "%s/%d/as", nto_procfs_path, PIDGET(ptid));
	ctl_fd = open (path, O_RDWR);
	if (ctl_fd == -1)
	  {
		  error ("Couldn't open proc file %s, error %d (%s)", path, errno, strerror(errno));
		  /* NOTREACHED */
	  }
	if (devctl (ctl_fd, DCMD_PROC_STOP, &status, sizeof (status), 0) != EOK)
	  {
		  error ("Couldn't stop process");
		  /* NOTREACHED */
	  }

	// Define a sigevent for process stopped notification.
	event.sigev_notify = SIGEV_SIGNAL_THREAD;
	event.sigev_signo = SIGUSR1;
	event.sigev_code = 0;
	event.sigev_value.sival_ptr = NULL;
	event.sigev_priority = -1;
	devctl (ctl_fd, DCMD_PROC_EVENT, &event, sizeof (event), 0);

	if (devctl (ctl_fd, DCMD_PROC_STATUS, &status, sizeof (status), 0) == EOK)
	  {
	    if (status.flags & _DEBUG_FLAG_STOPPED)
	    {
	      SignalKill(QNX_NODE, PIDGET(ptid), 0, SIGCONT, 0, 0);
	    }
	  }
	attach_flag = 1;
	nto_init_solib_absolute_prefix();
	return ptid;
}

/* Ask the user what to do when an interrupt is received.  */

static void
interrupt_query ()
{
	target_terminal_ours ();

	if (query ("Interrupted while waiting for the program.\n\
Give up (and stop debugging it)? "))
	  {
		  target_mourn_inferior ();
		  throw_exception (RETURN_QUIT);
	  }

	target_terminal_inferior ();
}

/* The user typed ^C twice.  */
static void
qnx_interrupt_twice (signo)
	 int signo;
{
	signal (signo, ofunc);
	interrupt_query ();
	signal (signo, qnx_interrupt_twice);
}

static void
qnx_interrupt (int signo)
{
	/* If this doesn't work, try more severe steps.  */
	signal (signo, qnx_interrupt_twice);

	target_stop ();
}

static ptid_t
procfs_wait (ptid, ourstatus)
	 ptid_t ptid;
	 struct target_waitstatus *ourstatus;
{
	sigset_t set;
	siginfo_t info;
	procfs_status status;
	static int exit_signo = 0;  //used to track signals that cause termination

	ourstatus->kind = TARGET_WAITKIND_SPURIOUS;

	if (ptid_equal(inferior_ptid, null_ptid))
	  {
		  ourstatus->kind = TARGET_WAITKIND_STOPPED;
		  ourstatus->value.sig = TARGET_SIGNAL_0;
		  exit_signo = 0;
		  return null_ptid;
	  }

	sigemptyset (&set);
	sigaddset (&set, SIGUSR1);

	devctl (ctl_fd, DCMD_PROC_STATUS, &status, sizeof (status), 0);
	while (!(status.flags & _DEBUG_FLAG_ISTOP))
	  {
		  ofunc = (void (*)()) signal (SIGINT, qnx_interrupt);
		  sigwaitinfo (&set, &info);
		  signal (SIGINT, ofunc);
		  devctl (ctl_fd, DCMD_PROC_STATUS, &status, sizeof (status), 0);
	  }

	if (status.flags & _DEBUG_FLAG_SSTEP)
	  {
		  ourstatus->kind = TARGET_WAITKIND_STOPPED;
		  ourstatus->value.sig = TARGET_SIGNAL_TRAP;
	  }
	// Was it a breakpoint?
	else if (status.flags & _DEBUG_FLAG_TRACE)
	  {
		  ourstatus->kind = TARGET_WAITKIND_STOPPED;
		  ourstatus->value.sig = TARGET_SIGNAL_TRAP;
	  }
	else if (status.flags & _DEBUG_FLAG_ISTOP)
	  {
		  switch (status.why)
			{
			case _DEBUG_WHY_SIGNALLED:
				ourstatus->kind = TARGET_WAITKIND_STOPPED;
				ourstatus->value.sig = target_signal_from_host(status.info.si_signo);
				exit_signo = 0;
				break;
			case _DEBUG_WHY_FAULTED:
				ourstatus->kind = TARGET_WAITKIND_STOPPED;
				if(status.info.si_signo == SIGTRAP) {
					ourstatus->value.sig = 0;
					exit_signo = 0;
				}
				else {
				 	ourstatus->value.sig = target_signal_from_host(status.info.si_signo);
					exit_signo = ourstatus->value.sig;
				}
				break;

			case _DEBUG_WHY_TERMINATED:
				{
					int waitval = 0;

					waitpid(PIDGET(inferior_ptid), &waitval, WNOHANG);
					if(exit_signo) {
						ourstatus->kind = TARGET_WAITKIND_SIGNALLED; //abnormal death
						ourstatus->value.sig = exit_signo;
					}
					else {
						ourstatus->kind = TARGET_WAITKIND_EXITED;    //normal death
						ourstatus->value.integer = WEXITSTATUS(waitval); //status.what;
					}
					exit_signo = 0;
					break;
				}

			case _DEBUG_WHY_REQUESTED:
				// we are assuming a requested stop is due to a SIGINT
				ourstatus->kind = TARGET_WAITKIND_STOPPED;
				ourstatus->value.sig = TARGET_SIGNAL_INT;
				exit_signo = 0;
				break;
			}
	  }
	inferior_ptid = ptid_build(status.pid, 0, status.tid);

	return inferior_ptid;
}

/*

LOCAL FUNCTION

	procfs_fetch_registers -- fetch current registers from inferior

SYNOPSIS

	void procfs_fetch_registers (int regno)

DESCRIPTION

	Read the current values of the inferior's registers, both the
	general register set and floating point registers (if supported)
	and update gdb's idea of their current values.

*/

extern void nto_supply_gregset (procfs_greg * gregsetp);
extern void nto_supply_fpregset (procfs_fpreg * fpregsetp);

static void
procfs_fetch_registers (regno)
	 int regno;
{
	union
	{
		procfs_greg greg;
		procfs_fpreg fpreg;
	}
	reg;
	int regsize;

	procfs_set_thread(inferior_ptid);
	if (devctl (ctl_fd, DCMD_PROC_GETGREG, &reg, sizeof (reg), &regsize) == EOK)
	{
		nto_supply_gregset (&reg.greg);
	}

	if (devctl (ctl_fd, DCMD_PROC_GETFPREG, &reg, sizeof (reg), &regsize) == EOK)
	{
		nto_supply_fpregset (&reg.fpreg);
	}
}

/*

LOCAL FUNCTION

	procfs_xfer_memory -- copy data to or from inferior memory space

SYNOPSIS

	int procfs_xfer_memory (CORE_ADDR memaddr, char *myaddr, int len,
		int dowrite, struct target_ops target)

DESCRIPTION

	Copy LEN bytes to/from inferior's memory starting at MEMADDR
	from/to debugger memory starting at MYADDR.  Copy from inferior
	if DOWRITE is zero or to inferior if DOWRITE is nonzero.
  
	Returns the length copied, which is either the LEN argument or
	zero.  This xfer function does not do partial moves, since procfs_ops
	doesn't allow memory operations to cross below us in the target stack
	anyway.

NOTES

	The /proc interface makes this an almost trivial task.
 */

static int
procfs_xfer_memory (memaddr, myaddr, len, dowrite, attrib, target)
	 CORE_ADDR memaddr;
	 char *myaddr;
	 int len;
	 int dowrite;
	 struct mem_attrib *attrib;	/* ignored */ 
	 struct target_ops *target;	/* ignored */
{
	int nbytes = 0;

	if (lseek (ctl_fd, (off_t) memaddr, SEEK_SET) == (off_t) memaddr)
	  {
		  if (dowrite)
			{
				nbytes = write (ctl_fd, myaddr, len);
			}
		  else
			{
				nbytes = read (ctl_fd, myaddr, len);
			}
		  if (nbytes < 0)
			{
				nbytes = 0;
			}
	  }
	return (nbytes);
}

/* Take a program previously attached to and detaches it.
   The program resumes execution and will no longer stop
   on signals, etc.  We'd better not have left any breakpoints
   in the program or it'll die when it hits one.  For this
   to work, it may be necessary for the process to have been
   previously attached.  It *might* work if the program was
   started via the normal ptrace (PTRACE_TRACEME).  */

static void
procfs_detach (args, from_tty)
	 char *args;
	 int from_tty;
{
	int siggnal = 0;

	if (from_tty)
	  {
		  char *exec_file = get_exec_file (0);
		  if (exec_file == 0)
			  exec_file = "";
		  printf_unfiltered ("Detaching from program: %s %s\n",
							 exec_file, target_pid_to_str(inferior_ptid));
		  gdb_flush (gdb_stdout);
	  }
	if (args)
		siggnal = atoi (args);

	if (siggnal)
		SignalKill(QNX_NODE, PIDGET(inferior_ptid), 0, siggnal, 0, 0);

	close (ctl_fd);
	ctl_fd = -1;
	init_thread_list ();
	inferior_ptid = null_ptid;
	attach_flag = 0;
	unpush_target (&procfs_ops);    /* Pop out of handling an inferior */
}

static int
nto_breakpoint (addr, type, size)
	 CORE_ADDR addr;
	 int type;
	 int size;
{
	procfs_break brk;

	brk.type = type;
	brk.addr = addr;
	brk.size = size;
	if ((errno = devctl (ctl_fd, DCMD_PROC_BREAK, &brk, sizeof (brk), 0)) !=
		EOK)
	  {
		  return 1;
	  }
	return 0;
}

static int
procfs_insert_breakpoint (addr, contents_cache)
	 CORE_ADDR addr;
	 char *contents_cache;
{
	return nto_breakpoint (addr, _DEBUG_BREAK_EXEC, 0);
}

static int
procfs_remove_breakpoint (addr, contents_cache)
	 CORE_ADDR addr;
	 char *contents_cache;
{
	return nto_breakpoint (addr, _DEBUG_BREAK_EXEC, -1);
}


static void
procfs_resume (ptid_t ptid, int step, enum target_signal signo)
{
	int signal_to_pass;
	procfs_status status;

	if(ptid_equal(inferior_ptid, null_ptid))
	        return;

	procfs_set_thread(ptid_equal(ptid, minus_one_ptid) ? inferior_ptid : ptid);


	run.flags = _DEBUG_RUN_FAULT|_DEBUG_RUN_TRACE;
	if (step)
		run.flags |= _DEBUG_RUN_STEP;

	sigemptyset ((sigset_t *) & run.fault);
	sigaddset ((sigset_t *) & run.fault, FLTBPT);
	sigaddset ((sigset_t *) & run.fault, FLTTRACE);
	sigaddset ((sigset_t *) & run.fault, FLTILL);
	sigaddset ((sigset_t *) & run.fault, FLTPRIV);
	sigaddset ((sigset_t *) & run.fault, FLTBOUNDS);
	sigaddset ((sigset_t *) & run.fault, FLTIOVF);
	sigaddset ((sigset_t *) & run.fault, FLTIZDIV);
	sigaddset ((sigset_t *) & run.fault, FLTFPE);
	/* Peter V will be changing this at some point... */
	sigaddset ((sigset_t *) & run.fault, FLTPAGE);

	run.flags |= _DEBUG_RUN_ARM;

	sigemptyset (&run.trace);
	notice_signals();
	signal_to_pass = target_signal_to_host (signo);

	if ( signal_to_pass ) {
		devctl( ctl_fd, DCMD_PROC_STATUS, &status, sizeof(status), 0 );
		signal_to_pass = target_signal_to_host (signo);
		if ( status.why & (_DEBUG_WHY_SIGNALLED|_DEBUG_WHY_FAULTED) ) {
			if ( signal_to_pass != status.info.si_signo ) {
				SignalKill(QNX_NODE, PIDGET(inferior_ptid), 0, signal_to_pass, 0, 0);
				run.flags |= _DEBUG_RUN_CLRFLT | _DEBUG_RUN_CLRSIG;
			}
			else {
				/* let it kill the program without telling us */
				sigdelset( &run.trace, signal_to_pass );
			}
		}
	}
	else {
		run.flags |= _DEBUG_RUN_CLRSIG | _DEBUG_RUN_CLRFLT;
	}
	if ((errno = devctl (ctl_fd, DCMD_PROC_RUN, &run, sizeof (run), 0)) != EOK)
	{
		perror ("run error!\n");
		return;
	}
}

static void
procfs_mourn_inferior ()
{
	if (!ptid_equal(inferior_ptid, null_ptid))
	  {
		  SignalKill(QNX_NODE, PIDGET(inferior_ptid), 0, SIGKILL, 0, 0);
		  close (ctl_fd);
	  }
	inferior_ptid = null_ptid;
	init_thread_list ();
	unpush_target (&procfs_ops);
	generic_mourn_inferior ();
	attach_flag = 0;
}

/* This function breaks up an argument string into an argument
 * vector suitable for passing to execvp().
 * E.g., on "run a b c d" this routine would get as input
 * the string "a b c d", and as output it would fill in argv with
 * the four arguments "a", "b", "c", "d".  The only additional
 * functionality is simple quoting.  The gdb command:
 *	run a "b c d" f
 * will fill in argv with the three args "a", "b c d", "e".  
 */
static void
breakup_args (scratch, argv)
	 char *scratch;
	 char **argv;
{
	char *pp, *cp = scratch;
	char quoting = 0;

#if DEBUGGING
	printf ("breakup_args: input = %s\n", scratch);
#endif
	for (;;)
	  {

		  /* Scan past leading separators */
		  quoting = 0;
		  while (*cp == ' ' || *cp == '\t' || *cp == '\n')
		  {
			  cp++;
		  }

		  /* Break if at end of string */
		  if (*cp == '\0')
			  break;

		  /* Take an arg */
		  if (*cp == '"')
		  {
			  cp++;
			  quoting = strchr(cp, '"') ? 1 : 0;
		  }

		  *argv++ = cp;

		  /* Scan for next arg separator */
		  pp = cp;
		  if (quoting)
			  cp = strchr(pp, '"');
		  if ((cp == NULL) || (!quoting)) 
		  	cp = strchr (pp, ' ');
		  if (cp == NULL)
			  cp = strchr (pp, '\t');
		  if (cp == NULL)
			  cp = strchr (pp, '\n');

		  /* No separators => end of string => break */
		  if (cp == NULL)
		  {
		 	  pp = cp;
			  break;
		  }

		  /* Replace the separator with a terminator */
		  *cp++ = '\0';
	  }

	/* execv requires a null-terminated arg vector */
	*argv = NULL;

}

static void
procfs_create_inferior (exec_file, allargs, env)
	 char *exec_file;
	 char *allargs;
	 char **env;
{
    struct inheritance inherit;
    pid_t pid;
    int flags, errn;
    char **argv, *args;
    char *in = "", *out = "", *err = "";
    int fd, fds[3];
    sigset_t set;

    argv = (char **) xmalloc (((strlen (allargs) + 1) / (unsigned) 2 + 2) * sizeof (*argv));
    argv[0] = get_exec_file(1);
    if(!argv[0])
    {
        if(exec_file)
            argv[0] = exec_file;
        else
            return;
    }

    args = strdup(allargs);
    breakup_args (args, exec_file ? &argv[1]:&argv[0]);

    argv = qnx_parse_redirection( argv, &in, &out, &err );

    fds[0] = STDIN_FILENO;
    fds[1] = STDOUT_FILENO;
    fds[2] = STDERR_FILENO;

    /* If the user specified I/O via gdb's --tty= arg, use it, but only
       if the i/o is not also being specified via redirection. */
    if (inferior_io_terminal) {
        if ( ! in[0] )
            in = inferior_io_terminal;
        if ( ! out[0] )
            out = inferior_io_terminal;
        if ( ! err[0] )
            err = inferior_io_terminal;
    }

    if ( in[0] ) {
    	if ( (fd = open(in,O_RDONLY)) == -1 ) {
    		perror(in);
    	}
    	else
    		fds[0] = fd;
    }
    if ( out[0] ) {
    	if ( (fd = open(out,O_WRONLY)) == -1 ) {
    		perror(out);
    	}
    	else
    		fds[1] = fd;
    }
    if ( err[0] ) {
    	if ( (fd = open(err,O_WRONLY)) == -1 ) {
    		perror(err);
    	}
    	else
    		fds[2] = fd;
    }

    /* Clear any pending SIGUSR1's but keep the behavior the same. */
    signal(SIGUSR1, signal(SIGUSR1, SIG_IGN));

    sigemptyset (&set);
    sigaddset (&set, SIGUSR1);
    sigprocmask (SIG_UNBLOCK, &set, NULL);

    memset (&inherit, 0, sizeof (inherit));

    if(ND_NODE_CMP(nto_procfs_node, ND_LOCAL_NODE) != 0) 
    {
        inherit.nd = QNX_NODE;
        inherit.flags |= SPAWN_SETND;
        inherit.flags &= ~SPAWN_EXEC;
    }
    inherit.flags |= SPAWN_SETGROUP | SPAWN_HOLD;
    inherit.pgroup = SPAWN_NEWPGROUP;
    pid = spawnp (argv[0], 3, fds, &inherit, argv, ND_NODE_CMP(nto_procfs_node, ND_LOCAL_NODE) == 0 ? env : 0);
    free(args);

    sigprocmask (SIG_BLOCK, &set, NULL);

    if(pid == -1)
        error("Error spawning %s: %d (%s)", argv[0], errno, strerror(errno));

    if ( fds[0] != STDIN_FILENO ) close(fds[0]);
    if ( fds[1] != STDOUT_FILENO ) close(fds[1]);
    if ( fds[2] != STDERR_FILENO ) close(fds[2]);

    inferior_ptid = do_attach (pid_to_ptid(pid));

    attach_flag = 0;
    flags = _DEBUG_FLAG_KLC;        // Kill-on-Last-Close flag
    if ( (errn = devctl(ctl_fd, DCMD_PROC_SET_FLAG, &flags, sizeof(flags), 0)) != EOK )
    {
//        warning( "Failed to set Kill-on-Last-Close flag: errno = %d(%s)\n", errn, strerror(errn) );
    }
    push_target (&procfs_ops);
    target_terminal_init ();

    /* NYI: add the symbol info somewhere? */
#ifdef SOLIB_CREATE_INFERIOR_HOOK
    if ( exec_bfd != NULL || (symfile_objfile != NULL && symfile_objfile->obfd != NULL) )
	SOLIB_CREATE_INFERIOR_HOOK (pid);
#endif
}

static void
procfs_stop ()
{
	devctl (ctl_fd, DCMD_PROC_STOP, NULL, 0, 0);
}

static void
procfs_kill_inferior ()
{
	target_mourn_inferior ();
}

/* Store register REGNO, or all registers if REGNO == -1, from the contents
   of REGISTERS.  */

static void
procfs_prepare_to_store ()
{
#if defined(GKM_DEBUG) && (GKM_DEBUG == 1)
	printf_unfiltered ("GKM: qnx_prepare_to_store()\n");
#endif
}

void
procfs_store_registers (regno)
	 int regno;
{
	union
	{
		procfs_greg greg;
		procfs_fpreg fpreg;
	}
	reg;
	unsigned first;
	unsigned last;
	unsigned end;
	unsigned off;
	unsigned len;
	unsigned char subcmd;
	char *data;

	if (ptid_equal(inferior_ptid, null_ptid))
		return;
	procfs_set_thread(inferior_ptid);

	if (regno == -1)
	  {
		  first = 0;
		  last = NUM_REGS - 1;
	  }
	else
	  {
		  first = regno;
		  last = regno;
	  }
	while (first <= last)
	  {
		  end = qnx_cpu_register_area (first, last, &subcmd, &off, &len);
		  switch (subcmd)
			{
			case DSMSG_REG_GENERAL:
				if (devctl
					(ctl_fd, DCMD_PROC_GETGREG, &reg.greg, sizeof (reg.greg),
					 0) != EOK)
				  {
					  printf ("error fetching general registers\n");
					  break;
				  }
				data = (char *) &reg.greg + off;
				if (qnx_cpu_register_store (0, first, end, data))
				  {
					  devctl (ctl_fd, DCMD_PROC_SETGREG, &reg.greg,
							  sizeof (reg.greg), 0);
				  }
				break;
			case DSMSG_REG_FLOAT:
				if (devctl
					(ctl_fd, DCMD_PROC_GETFPREG, &reg.fpreg,
					 sizeof (reg.fpreg), 0) != EOK)
				  {
					  /* Do not print a warning.  Not all inferiors 
					     have fpregs.  */
					  break;
				  }
				data = (char *) &reg.fpreg + off;
				if (qnx_cpu_register_store (0, first, end, data))
				  {
					  devctl (ctl_fd, DCMD_PROC_SETFPREG, &reg.fpreg,
							  sizeof (reg.fpreg), 0);
				  }
				break;
			case DSMSG_REG_SYSTEM:
				break;
			default:
				break;
			}
		  first = end + 1;
	  }
}

static void
notice_signals (void)
{
	int signo;

	for (signo = 1; signo < NSIG; signo++)
	  {
		  if (signal_stop_state (target_signal_from_host (signo)) == 0 &&
			  signal_print_state (target_signal_from_host (signo)) == 0 &&
			  signal_pass_state (target_signal_from_host (signo)) == 1)
			{
				sigdelset (&run.trace, signo);
			}
		  else
			{
				sigaddset (&run.trace, signo);
			}
	  }
}

/*

GLOBAL FUNCTION

	procfs_notice_signals

SYNOPSIS

	static void procfs_notice_signals (int pid);

DESCRIPTION

	When the user changes the state of gdb's signal handling via the
	"handle" command, this function gets called to see if any change
	in the /proc interface is required.  It is also called internally
	by other /proc interface functions to initialize the state of
	the traced signal set.

	One thing it does is that signals for which the state is "nostop",
	"noprint", and "pass", have their trace bits reset in the pr_trace
	field, so that they are no longer traced.  This allows them to be
	delivered directly to the inferior without the debugger ever being
	involved.
 */

static void
procfs_notice_signals (pid)
     int pid;
{
  sigemptyset (&run.trace);
  notice_signals ();
}

static char *
procfs_pid_to_exec_file (int pid)
{
	struct {
		procfs_debuginfo info;
		char pad[1024];
	} name;
	int fd, len;
	char buf[PATH_MAX];
	static char *path = NULL;

	sprintf(buf, "/proc/%d/as", pid);
	fd = open(buf, O_RDONLY);
	if (fd == -1)
		return NULL;

	if (devctl(fd, DCMD_PROC_MAPDEBUG_BASE, &name, sizeof(name) - 1, 0) != EOK){
		close(fd);
		return NULL;
	}

	close(fd);

	len = strlen(name.info.path);
	path = xrealloc(path, len + 2);
	if(strchr(name.info.path, '/') != NULL)
		sprintf(path, "/%s", name.info.path);
	else
		strcpy(path, name.info.path);

	return path;
}

static void
init_procfs_ops ()
{
	procfs_ops.to_shortname = "procfs";
	procfs_ops.to_longname = "QNX Neutrino procfs child process";
	procfs_ops.to_doc =
		"QNX Neutrino procfs child process (started by the \"run\" command).\n\
	target procfs <node>";
	procfs_ops.to_open = procfs_open;
	procfs_ops.to_attach = procfs_attach;
	procfs_ops.to_post_attach = procfs_post_attach;
	procfs_ops.to_detach = procfs_detach;
	procfs_ops.to_resume = procfs_resume;
	procfs_ops.to_wait = procfs_wait;
	procfs_ops.to_fetch_registers = procfs_fetch_registers;
	procfs_ops.to_store_registers = procfs_store_registers;
	procfs_ops.to_prepare_to_store = procfs_prepare_to_store;
	procfs_ops.to_xfer_memory = procfs_xfer_memory;
	procfs_ops.to_files_info = procfs_files_info;
	procfs_ops.to_insert_breakpoint = procfs_insert_breakpoint;
	procfs_ops.to_remove_breakpoint = procfs_remove_breakpoint;
	procfs_ops.to_terminal_init = terminal_init_inferior;
	procfs_ops.to_terminal_inferior = terminal_inferior;
	procfs_ops.to_terminal_ours_for_output = terminal_ours_for_output;
	procfs_ops.to_terminal_ours = terminal_ours;
	procfs_ops.to_terminal_info = child_terminal_info;
	procfs_ops.to_kill = procfs_kill_inferior;
	procfs_ops.to_create_inferior = procfs_create_inferior;
	procfs_ops.to_mourn_inferior = procfs_mourn_inferior;
	procfs_ops.to_can_run = procfs_can_run;
	procfs_ops.to_can_async_p = procfs_can_async;
	procfs_ops.to_notice_signals = procfs_notice_signals;
	procfs_ops.to_thread_alive = procfs_thread_alive; 
	procfs_ops.to_find_new_threads = procfs_find_new_threads; 
	procfs_ops.to_pid_to_str = qnx_pid_to_str;
	procfs_ops.to_pid_to_exec_file = procfs_pid_to_exec_file;
	procfs_ops.to_stop = procfs_stop;
	procfs_ops.to_stratum = process_stratum;
	procfs_ops.to_has_all_memory = 1;
	procfs_ops.to_has_memory = 1;
	procfs_ops.to_has_stack = 1;
	procfs_ops.to_has_registers = 1;
	procfs_ops.to_has_execution = 1;
	procfs_ops.to_magic = OPS_MAGIC;
}

extern int qnx_ostype;

void
_initialize_procfs ()
{
	sigset_t set;
  	extern struct dscpuinfo qnx_cpuinfo;
	extern int qnx_cpuinfo_valid;

	init_procfs_ops ();
	add_target (&procfs_ops);
	qnx_ostype = OSTYPE_NTO;

	//
	// We use SIGUSR1 to gain control after we block waiting for a process.
	// We use sigwaitevent to wait.
	//
	sigemptyset (&set);
	sigaddset (&set, SIGUSR1);
	sigprocmask (SIG_BLOCK, &set, NULL);

	/* Set up trace and fault sets, as gdb expects them. */
	sigemptyset (&run.trace);
	notice_signals ();

	/* Stuff some information */
  	qnx_cpuinfo.cpuflags = SYSPAGE_ENTRY(cpuinfo)->flags;
 	qnx_cpuinfo_valid = 1;

	TARGET_SO_FIND_AND_OPEN_SOLIB = nto_find_and_open_solib;
}


int procfs_hw_watchpoint (int addr, int len, int type)
{
procfs_break brk;
int	err;

    switch(type) {
    case 1: /* Read */
    	brk.type = _DEBUG_BREAK_RD;
    	break;
    case 2: /* Read/Write */
    	brk.type = _DEBUG_BREAK_RW;
    	break;
    default: /* Modify */
/*    	brk.type = _DEBUG_BREAK_RWM; This should work, shouldn't it? I get EINVAL */
    	brk.type = _DEBUG_BREAK_RW;
    }
    brk.type |= _DEBUG_BREAK_HW; /* always ask for HW */
    brk.addr = addr;
    brk.size = len;

    if ( (err = devctl( ctl_fd, DCMD_PROC_BREAK, &brk, sizeof(brk), 0 )) != EOK ) {
    	errno = err;
    	perror("Failed to set hardware watchpoint");
    	return -1;
    }
    return 0;
}

CORE_ADDR procfs_getbase(void)
{
procfs_info         procinfo;

    if ( devctl( ctl_fd, DCMD_PROC_INFO, &procinfo, sizeof procinfo, 0) != EOK )
    	return (CORE_ADDR)NULL;
    return procinfo.base_address;
}

#endif /* __QNXNTO__ */
